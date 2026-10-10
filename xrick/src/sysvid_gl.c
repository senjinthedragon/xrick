/*
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

/*
 * OpenGL shader chain between the game's frame and the screen (ENABLE_SHADERS, config.h).
 *
 * sysvid.c converts the 8-bit frame buffer to RGBA as before and hands the whole frame
 * to sysvid_gl_present, which uploads it and runs it through the passes of `chain`
 * below: each pass renders into an offscreen texture (FBO) that the next pass samples,
 * and the last pass renders into the window, letterboxed like SDL_Renderer's
 * SDL_LOGICAL_PRESENTATION_LETTERBOX.
 *
 * Passes are libretro glsl-shaders sources (.glsl, both stages in one file split by
 * #if defined(VERTEX) / FRAGMENT), compiled in from src/shaders/ (see embed.sh there),
 * and are fed what RetroArch's GLSL driver feeds them (gfx/drivers_shader/shader_glsl.c):
 *   attributes  VertexCoord, TexCoord (and OrigTexCoord, PassNTexCoord, ...)
 *   uniforms    MVPMatrix, FrameCount, FrameDirection, InputSize, TextureSize,
 *               OutputSize, FinalViewportSize, Texture (the previous pass's output)
 *   earlier     Orig* = the game frame; Pass<j+1>* = PassPrev<p-j>* = output of pass j;
 *               PassPrev<p+1>* = the game frame, for pass p (0-based)
 * Not supported: #pragma parameter uniforms (PARAMETER_UNIFORM stays undefined, so the
 * shaders use their built-in defaults), LUT textures, frame history (Prev*), feedback,
 * float/sRGB framebuffers, mipmaps. Every texture is exactly its image size, so
 * TextureSize == InputSize and all texture coordinates run 0..1.
 *
 * Desktop: GL 3.3 core, functions loaded through SDL_GL_GetProcAddress (nothing to link).
 * Web: WebGL 2 (GLES 3.0), called directly; needs -sMAX_WEBGL_VERSION=2 at link.
 */

#include "config.h"

#ifdef ENABLE_SHADERS

#include <stdio.h>  /* snprintf */
#include <string.h> /* strstr, strchr */

#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <GLES3/gl3.h>
#else
#include <SDL3/SDL_opengl.h>
#endif

#include <stdlib.h> /* malloc */

#include "system.h"
#include "sysarg.h"
#include "sysvid_gl.h"
#include "sysvid_gpu.h" /* menu/settings getter-setter API, shared with sysvid_gpu.c (see its own header) */
#include "sysvid_overlay.h"
#include "menu.h"
#include "bezels_embedded.h"
#include "thirdparty/stb_image.h"



/*
 * the chain: passes run top to bottom. mirrors a .glslp preset:
 *   filter_linear  filter_linearN -- how this pass samples its input (Texture)
 *   scale_type     scale_typeN    -- size of this pass's output:
 *                    SCALE_SOURCE   input size * scale
 *                    SCALE_VIEWPORT letterboxed window area * scale
 *                    SCALE_ABSOLUTE scale pixels
 *   max_scale      not in .glslp -- if > 0, the output is at most input size *
 *                  max_scale, whatever scale_type gives (aspect kept)
 * the last pass always renders straight into the window at the letterboxed size;
 * its scale_type/scale/max_scale are not used.
 * to add a shader: put its .glsl under src/shaders/, run embed.sh, add its passes here.
 */
enum { SCALE_SOURCE, SCALE_VIEWPORT, SCALE_ABSOLUTE };

typedef struct {
	const char *name;
	const char *source;
	int filter_linear;
	int scale_type;
	float scale;
	float max_scale;
} shader_pass_t;

/* src/shaders/stock.glsl: plain copy */
static const char stock[] =
#include "shaders/stock.glsl.inc"
;
static const char sharp_bilinear[] =
#include "shaders/sharp-bilinear.glsl.inc"
;
static const char fsr_easu[] =
#include "shaders/fsr-easu.glsl.inc"
;
static const char fsr_rcas[] =
#include "shaders/fsr-rcas.glsl.inc"
;
static const char crt_easymode[] =
#include "shaders/crt-easymode.glsl.inc"
;
static const char crt_lottes[] =
#include "shaders/crt-lottes.glsl.inc"
;
/* bezel screen curvature, run as an extra pass after the chain when a bezel is active --
   see sysvid_gl_present()'s bezel branch and the shader's own header comment */
static const char curvature[] =
#include "shaders/curvature.glsl.inc"
;

/*
 * runtime-switchable chains (sysvid_getUpscale/setUpscale/getCrt/setCrt, menu.c's
 * VIDEO page), mirroring sysvid_gpu.h's upscaleMode_t/crtMode_t. A CRT mode other than
 * CRT_NONE takes priority over the upscaler (same as sysvid_gpu.c: easymode/lottes are
 * complete standalone effects run directly on the raw game frame, not something layered
 * on top of a separate upscale pass) -- see activeChain() below. CRT_ROYALE needs a LUT
 * texture (the phosphor mask) this driver's GLSL binder doesn't support yet, so it falls
 * back to CRT_NONE's chain visually while still storing the setting.
 */
typedef struct {
	const shader_pass_t *list;
	int count;
} shader_chain_t;

static const shader_pass_t stock_chain[] = {
	{ "stock", stock, 0, SCALE_VIEWPORT, 1.0f, 0 }, /* UPSCALE_NONE: nearest */
};
static const shader_pass_t sharp_chain[] = {
	{ "sharp-bilinear", sharp_bilinear, 1, SCALE_VIEWPORT, 1.0f, 0 },
};
static const shader_pass_t fsr1_chain[] = {
	{ "fsr-easu", fsr_easu, 0, SCALE_VIEWPORT, 1.0f, 0 },
	{ "fsr-rcas", fsr_rcas, 0, SCALE_VIEWPORT, 1.0f, 0 },
};
static const shader_pass_t crt_easymode_chain[] = {
	{ "crt-easymode", crt_easymode, 0, SCALE_VIEWPORT, 1.0f, 0 },
};
static const shader_pass_t crt_lottes_chain[] = {
	{ "crt-lottes", crt_lottes, 0, SCALE_VIEWPORT, 1.0f, 0 },
};

static const shader_chain_t upscale_chains[] = {
	{ stock_chain, 1 }, /* UPSCALE_NONE */
	{ sharp_chain, 1 }, /* UPSCALE_SHARP */
	{ fsr1_chain, 2 },  /* UPSCALE_FSR1 */
};
static const shader_chain_t crt_chains[] = {
	{ crt_easymode_chain, 1 }, /* CRT_EASYMODE */
	{ crt_lottes_chain, 1 },   /* CRT_LOTTES */
};
#define MAX_PASSES 2

static int upscaleMode;
static int crtMode;
static int bezelMode;
static int aspectMode;
static const shader_chain_t *cur_chain = &upscale_chains[0];
static int npasses = 1;

/* crtMode's effective chain, or NULL to fall through to upscale_chains[upscaleMode] --
   see the runtime-switchable-chains comment above */
static const shader_chain_t *activeChain(void)
{
	if (crtMode == CRT_EASYMODE) return &crt_chains[0];
	if (crtMode == CRT_LOTTES) return &crt_chains[1];
	return &upscale_chains[upscaleMode];
}



/*
 * GL entry points. desktop: pointers loaded from SDL_GL_GetProcAddress, called as
 * GL(glFoo)(...); web: the functions themselves.
 */
#define GL_FUNCS(X) \
	X(void,   glActiveTexture, (GLenum texture)) \
	X(void,   glAttachShader, (GLuint program, GLuint shader)) \
	X(void,   glBindBuffer, (GLenum target, GLuint buffer)) \
	X(void,   glBindFramebuffer, (GLenum target, GLuint framebuffer)) \
	X(void,   glBindTexture, (GLenum target, GLuint texture)) \
	X(void,   glBindVertexArray, (GLuint array)) \
	X(void,   glBlendFunc, (GLenum sfactor, GLenum dfactor)) \
	X(void,   glBufferData, (GLenum target, GLsizeiptr size, const void *data, GLenum usage)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum target)) \
	X(void,   glClear, (GLbitfield mask)) \
	X(void,   glClearColor, (GLfloat r, GLfloat g, GLfloat b, GLfloat a)) \
	X(void,   glColorMask, (GLboolean r, GLboolean g, GLboolean b, GLboolean a)) \
	X(void,   glCompileShader, (GLuint shader)) \
	X(GLuint, glCreateProgram, (void)) \
	X(GLuint, glCreateShader, (GLenum type)) \
	X(void,   glDeleteBuffers, (GLsizei n, const GLuint *buffers)) \
	X(void,   glDeleteFramebuffers, (GLsizei n, const GLuint *framebuffers)) \
	X(void,   glDeleteProgram, (GLuint program)) \
	X(void,   glDeleteShader, (GLuint shader)) \
	X(void,   glDeleteTextures, (GLsizei n, const GLuint *textures)) \
	X(void,   glDeleteVertexArrays, (GLsizei n, const GLuint *arrays)) \
	X(void,   glDisable, (GLenum cap)) \
	X(void,   glDisableVertexAttribArray, (GLuint index)) \
	X(void,   glDrawArrays, (GLenum mode, GLint first, GLsizei count)) \
	X(void,   glEnable, (GLenum cap)) \
	X(void,   glEnableVertexAttribArray, (GLuint index)) \
	X(void,   glFramebufferTexture2D, (GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level)) \
	X(void,   glGenBuffers, (GLsizei n, GLuint *buffers)) \
	X(void,   glGenFramebuffers, (GLsizei n, GLuint *framebuffers)) \
	X(void,   glGenTextures, (GLsizei n, GLuint *textures)) \
	X(void,   glGenVertexArrays, (GLsizei n, GLuint *arrays)) \
	X(GLint,  glGetAttribLocation, (GLuint program, const GLchar *name)) \
	X(void,   glGetProgramInfoLog, (GLuint program, GLsizei size, GLsizei *length, GLchar *log)) \
	X(void,   glGetProgramiv, (GLuint program, GLenum pname, GLint *params)) \
	X(void,   glGetShaderInfoLog, (GLuint shader, GLsizei size, GLsizei *length, GLchar *log)) \
	X(void,   glGetShaderiv, (GLuint shader, GLenum pname, GLint *params)) \
	X(GLint,  glGetUniformLocation, (GLuint program, const GLchar *name)) \
	X(void,   glLinkProgram, (GLuint program)) \
	X(void,   glPixelStorei, (GLenum pname, GLint param)) \
	X(void,   glShaderSource, (GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length)) \
	X(void,   glTexImage2D, (GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLint border, GLenum format, GLenum type, const void *pixels)) \
	X(void,   glTexParameteri, (GLenum target, GLenum pname, GLint param)) \
	X(void,   glTexSubImage2D, (GLenum target, GLint level, GLint x, GLint y, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels)) \
	X(void,   glUniform1i, (GLint location, GLint v0)) \
	X(void,   glUniform2f, (GLint location, GLfloat v0, GLfloat v1)) \
	X(void,   glUniformMatrix4fv, (GLint location, GLsizei count, GLboolean transpose, const GLfloat *value)) \
	X(void,   glUseProgram, (GLuint program)) \
	X(void,   glVertexAttrib4f, (GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w)) \
	X(void,   glVertexAttribPointer, (GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer)) \
	X(void,   glViewport, (GLint x, GLint y, GLsizei w, GLsizei h))

#ifdef __EMSCRIPTEN__
#define GL(f) f
#define GLSL_VERSION "#version 300 es\n"
/* GLSL ES fragment shaders have no default float precision, and a float declaration
   before the shader's own `precision` line is an error: stock.glsl declares its
   `out vec4 FragColor` first, and failed to compile on WebGL. a default here comes
   before any source; the shader's own precision line still overrides it. GLES 3.0
   requires highp in fragment shaders */
#define GLSL_FRAGMENT_PRELUDE "precision highp float;\n"
#else
#define GL(f) p_##f
#define GLSL_VERSION "#version 330 core\n"
#define GLSL_FRAGMENT_PRELUDE ""
#define X(ret, name, args) typedef ret (APIENTRY *name##_t) args; static name##_t p_##name;
GL_FUNCS(X)
#undef X
#endif

/* GL 3.3 core and GLES 3.0 both guarantee at least 16 vertex attributes and 16
   fragment texture units */
#define MAX_ATTRIBS 16
#define MAX_UNITS 16



typedef struct {
	GLuint prog;
	GLuint fbo, tex; /* output, all passes but the last */
	int w, h;        /* output size */
} pass_t;

/* a bezel PNG (src/bezels/, embedded by bezels_embedded.c) plus where its screen cutout
   sits within it, as fractions of the whole image -- same layout as sysvid_gpu.c's own
   bezelInfo_t, duplicated rather than shared since the two backends' texture handles
   are different types */
typedef struct {
	GLuint tex;
	int w, h;
	float x0, y0, x1, y1;
} bezel_t;

static SDL_Window *window;
static SDL_GLContext context;
static pass_t passes[MAX_PASSES];
static GLuint vao, vbo, orig; /* orig: the game frame */
static U32 *gl_pixels; /* composited RGBA frame, our own copy -- see sysvid_gl_present */
/* plain copy for sysvid_gl_showImage: pictures that are not the game frame skip the chain */
static const shader_pass_t image_pass = { "image", stock, 1, SCALE_VIEWPORT, 1.0f, 0 };
static GLuint image_prog;
static const shader_pass_t curvature_pass = { "curvature", curvature, 1, SCALE_VIEWPORT, 1.0f, 0 };
static GLuint curvature_prog;
static bezel_t bezels[BEZEL_COUNT]; /* [BEZEL_NONE] left zeroed/unused */
static int orig_w, orig_h;
static int frame_count;

/* the full-screen quad as a triangle strip. VertexCoord is (x, y, 0, 1) and the same
   buffer, read two floats at a time, is every *TexCoord: texture t = 0 is the frame's
   top row. FBO passes map y = 0 to their row 0, so every intermediate texture keeps
   that orientation; only the pass into the window flips (see mvp) */
static const GLfloat quad[] = {
	0, 0, 0, 1,
	1, 0, 0, 1,
	0, 1, 0, 1,
	1, 1, 0, 1,
};

/* column-major ortho(0,1,0,1): y up into an FBO, y down into the window */
static const GLfloat mvp_fbo[16]    = { 2, 0, 0, 0,  0, 2, 0, 0,  0, 0, -1, 0,  -1, -1, 0, 1 };
static const GLfloat mvp_window[16] = { 2, 0, 0, 0,  0, -2, 0, 0,  0, 0, -1, 0,  -1, 1, 0, 1 };



/*
 * compile one stage of a libretro .glsl: our #version, then VERTEX or FRAGMENT, then
 * the source. a #version the source carries itself is dropped, as RetroArch does.
 */
static GLuint compile(const shader_pass_t *p, GLenum type)
{
	const GLchar *src[4];
	const char *body = p->source;
	const char *v = strstr(body, "#version");
	GLuint sh;
	GLint ok;
	char log[1024];

	if (v)
	{
		const char *eol = strchr(v, '\n');
		body = eol ? eol + 1 : v + strlen(v);
	}
	src[0] = GLSL_VERSION;
	src[1] = type == GL_VERTEX_SHADER ? "#define VERTEX\n" : "#define FRAGMENT\n";
	src[2] = type == GL_VERTEX_SHADER ? "" : GLSL_FRAGMENT_PRELUDE;
	src[3] = body;

	sh = GL(glCreateShader)(type);
	GL(glShaderSource)(sh, 4, src, NULL);
	GL(glCompileShader)(sh);
	GL(glGetShaderiv)(sh, GL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		GL(glGetShaderInfoLog)(sh, sizeof(log), NULL, log);
		sys_printf("xrick/video: %s: %s shader: %s\n", p->name,
			type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		GL(glDeleteShader)(sh);
		return 0;
	}
	return sh;
}

static GLuint build_program(const shader_pass_t *p)
{
	GLuint vs, fs, prog;
	GLint ok;
	char log[1024];

	if (!(vs = compile(p, GL_VERTEX_SHADER)))
		return 0;
	if (!(fs = compile(p, GL_FRAGMENT_SHADER)))
	{
		GL(glDeleteShader)(vs);
		return 0;
	}
	prog = GL(glCreateProgram)();
	GL(glAttachShader)(prog, vs);
	GL(glAttachShader)(prog, fs);
	GL(glLinkProgram)(prog);
	GL(glDeleteShader)(vs); /* freed with the program */
	GL(glDeleteShader)(fs);
	GL(glGetProgramiv)(prog, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		GL(glGetProgramInfoLog)(prog, sizeof(log), NULL, log);
		sys_printf("xrick/video: %s: link: %s\n", p->name, log);
		GL(glDeleteProgram)(prog);
		return 0;
	}
	return prog;
}



static void set_filter(GLuint tex, int linear)
{
	GLint f = linear ? GL_LINEAR : GL_NEAREST;

	GL(glBindTexture)(GL_TEXTURE_2D, tex);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, f);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, f);
}

static GLuint new_texture(int w, int h)
{
	GLuint tex;

	GL(glGenTextures)(1, &tex);
	GL(glBindTexture)(GL_TEXTURE_2D, tex);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	GL(glTexParameteri)(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	GL(glTexImage2D)(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
	set_filter(tex, 0);
	return tex;
}

/* (re)allocate pass <p>'s output when its size changes (window zoom, fullscreen) */
static int size_output(pass_t *p, int w, int h)
{
	if (p->fbo && p->w == w && p->h == h)
		return 1;
	if (p->tex)
		GL(glDeleteTextures)(1, &p->tex);
	if (!p->fbo)
		GL(glGenFramebuffers)(1, &p->fbo);
	p->tex = new_texture(w, h);
	p->w = w;
	p->h = h;
	GL(glBindFramebuffer)(GL_FRAMEBUFFER, p->fbo);
	GL(glFramebufferTexture2D)(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, p->tex, 0);
	if (GL(glCheckFramebufferStatus)(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
	{
		sys_printf("xrick/video: %dx%d framebuffer incomplete\n", w, h);
		return 0;
	}
	return 1;
}



/*
 * loadBezelTexture
 *
 * decodes an embedded bezel PNG (stb_image) and uploads it as a GL texture. Leaves
 * tex == 0 without panicking on failure -- same as sysvid_gpu.c's loadPNGTexture, a
 * missing bezel just means that option quietly isn't offered (see sysvid_cycleBezel).
 */
static void loadBezelTexture(bezel_t *bz, const unsigned char *png, unsigned long len,
	float x0, float y0, float x1, float y1)
{
	int w, h, comp;
	unsigned char *pix = stbi_load_from_memory(png, (int)len, &w, &h, &comp, 4);

	(void)comp;
	if (!pix) return;

	bz->tex = new_texture(w, h);
	GL(glBindTexture)(GL_TEXTURE_2D, bz->tex);
	GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pix);
	stbi_image_free(pix);

	bz->w = w;
	bz->h = h;
	bz->x0 = x0;
	bz->y0 = y0;
	bz->x1 = x1;
	bz->y1 = y1;
}

/*
 * computeBezelViewports
 *
 * <outer> is the bezel image, letterboxed into the window like any other picture;
 * <inner> is the game screen's cutout within it, from the bezel's own screenX0..Y1
 * fractions of <outer> -- mirrors sysvid_gpu.c's own function of the same name, with
 * one deliberate difference: SDL_GPU viewports are top-left origin (y grows downward,
 * same as the bezel's own screenY0/Y1 fractions and sysvid_gpu.c's own oy/iy), but
 * plain OpenGL's glViewport is bottom-left origin (y grows upward). The outer rect is
 * always centered in the window, so that flip is invisible on it ((dh-h)/2 is the same
 * measured from either edge) -- it only actually matters for the inner rect, offset
 * asymmetrically within the outer one by the bezel's own cutout fractions, which is
 * exactly the rect every caller here passes straight into glViewport(). *iy below is
 * therefore returned already in GL's bottom-origin convention, not top-origin like
 * sysvid_gpu.c's.
 */
static void computeBezelViewports(int dw, int dh, const bezel_t *bz,
	int *ox, int *oy, int *ow, int *oh, int *ix, int *iy, int *iw, int *ih)
{
	int iyTop;

	if ((long)dw * bz->h <= (long)dh * bz->w)
	{
		*ow = dw;
		*oh = (int)((long)dw * bz->h / bz->w);
	}
	else
	{
		*oh = dh;
		*ow = (int)((long)dh * bz->w / bz->h);
	}
	*ox = (dw - *ow) / 2;
	*oy = (dh - *oh) / 2;

	*ix = *ox + (int)(bz->x0 * (float)*ow);
	iyTop = *oy + (int)(bz->y0 * (float)*oh);
	*iw = (int)((bz->x1 - bz->x0) * (float)*ow);
	*ih = (int)((bz->y1 - bz->y0) * (float)*oh);
	*iy = dh - iyTop - *ih;
}



/*
 * bind texture <tex> (w x h) to the <prefix>Texture, <prefix>TextureSize,
 * <prefix>InputSize uniforms and <prefix>TexCoord attribute of <prog>, if it has them
 */
static void bind_frame(GLuint prog, const char *prefix, GLuint tex, int w, int h, GLuint *unit)
{
	char name[64];
	GLint loc;

	snprintf(name, sizeof(name), "%sTexture", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0 && *unit < MAX_UNITS)
	{
		GL(glActiveTexture)(GL_TEXTURE0 + *unit);
		GL(glBindTexture)(GL_TEXTURE_2D, tex);
		GL(glUniform1i)(loc, (GLint)*unit);
		(*unit)++;
	}
	snprintf(name, sizeof(name), "%sTextureSize", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0)
		GL(glUniform2f)(loc, (GLfloat)w, (GLfloat)h);
	snprintf(name, sizeof(name), "%sInputSize", prefix);
	if ((loc = GL(glGetUniformLocation)(prog, name)) >= 0)
		GL(glUniform2f)(loc, (GLfloat)w, (GLfloat)h);
	snprintf(name, sizeof(name), "%sTexCoord", prefix);
	if ((loc = GL(glGetAttribLocation)(prog, name)) >= 0)
	{
		GL(glEnableVertexAttribArray)((GLuint)loc);
		GL(glVertexAttribPointer)((GLuint)loc, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	}
}



/*
 * sysvid_gl_init
 *
 * see sysvid_gl.h
 */
SDL_Window *sysvid_gl_init(const char *title, int win_w, int win_h, SDL_WindowFlags flags,
	int fb_w, int fb_h)
{
	int i;

	orig_w = fb_w;
	orig_h = fb_h;

	upscaleMode = sysarg_args_upscale;
	crtMode = (sysarg_args_crt == CRT_ROYALE) ? CRT_NONE : sysarg_args_crt; /* see sysvid_setCrt */
	bezelMode = sysarg_args_bezel;
	aspectMode = sysarg_args_aspect ? 1 : 0;
	cur_chain = activeChain();
	npasses = cur_chain->count;

	gl_pixels = (U32 *)malloc((size_t)orig_w * (size_t)orig_h * sizeof(U32));
	memset(gl_pixels, 0, (size_t)orig_w * (size_t)orig_h * sizeof(U32));
	sysvid_overlay_init(gl_pixels, (U16)orig_w, (U16)orig_h);

	SDL_GL_ResetAttributes();
#ifdef __EMSCRIPTEN__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3); /* WebGL 2 */
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#else
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
#ifdef __APPLE__
	SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);
#endif
#endif
	SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);

	window = SDL_CreateWindow(title, win_w, win_h, flags | SDL_WINDOW_OPENGL);
	if (!window)
		goto fail;
	context = SDL_GL_CreateContext(window);
	if (!context)
		goto fail;

#ifndef __EMSCRIPTEN__
#define X(ret, name, args) \
	if (!(p_##name = (name##_t)SDL_GL_GetProcAddress(#name))) \
	{ \
		sys_printf("xrick/video: no GL function " #name "\n"); \
		goto fail; \
	}
	GL_FUNCS(X)
#undef X
	/* no vsync, like the SDL_Renderer path: the game paces itself. not on the web,
	   where SDL maps the swap interval onto the main loop's timing mode */
	SDL_GL_SetSwapInterval(0);
#endif

	for (i = 0; i < npasses; i++)
		if (!(passes[i].prog = build_program(&cur_chain->list[i])))
			goto fail;
	if (!(image_prog = build_program(&image_pass)))
		goto fail;
	if (!(curvature_prog = build_program(&curvature_pass)))
		goto fail;

	GL(glGenVertexArrays)(1, &vao);
	GL(glBindVertexArray)(vao);
	GL(glGenBuffers)(1, &vbo);
	GL(glBindBuffer)(GL_ARRAY_BUFFER, vbo);
	GL(glBufferData)(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);

	orig = new_texture(orig_w, orig_h);
	GL(glPixelStorei)(GL_UNPACK_ALIGNMENT, 4);

	/* bezels: screenX0/Y0/X1/Y1 are the same hand-measured cutout fractions as
	   sysvid_gpu.c's own bezels[] (see its init for how they were derived) */
	loadBezelTexture(&bezels[BEZEL_COMMODORE_1084S], bezel_commodore_1084s_png,
		bezel_commodore_1084s_png_len, 0.216326f, 0.075000f, 0.783673f, 0.848214f);
	loadBezelTexture(&bezels[BEZEL_ATARI_SC1224], bezel_atari_sc1224_png,
		bezel_atari_sc1224_png_len, 0.255469f, 0.106944f, 0.744531f, 0.758796f);
	if (bezelMode != BEZEL_NONE && bezels[bezelMode].tex == 0)
		bezelMode = BEZEL_NONE; /* failed to load -- fall back quietly, like a bad CRT mode */

	return window; /* silent on success; failures above are logged */

fail:
	sys_printf("xrick/video: shaders unavailable (%s), falling back to SDL_Renderer\n", SDL_GetError());
	sysvid_gl_shutdown();
	return NULL;
}



/*
 * sysvid_gl_present
 *
 * see sysvid_gl.h
 */
/*
 * the window's drawable size <dw> x <dh> and, inside it, the letterboxed area
 * <vx>,<vy> <vw> x <vh>: the game frame scaled to fit, aspect kept. FALSE when the
 * window has no area (minimised)
 */
static int letterbox(int *dw, int *dh, int *vx, int *vy, int *vw, int *vh)
{
	SDL_GetWindowSizeInPixels(window, dw, dh);
	if (*dw <= 0 || *dh <= 0)
		return 0;
	if ((long)*dw * orig_h <= (long)*dh * orig_w)
	{
		*vw = *dw;
		*vh = (int)((long)*dw * orig_h / orig_w);
	}
	else
	{
		*vh = *dh;
		*vw = (int)((long)*dh * orig_w / orig_h);
	}
	*vx = (*dw - *vw) / 2;
	*vy = (*dh - *vh) / 2;
	return 1;
}

/* target the window: cleared to black, drawing into the letterboxed area only */
static void begin_window(int dw, int dh, int vx, int vy, int vw, int vh)
{
	GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
	GL(glViewport)(0, 0, dw, dh);
	GL(glClearColor)(0, 0, 0, 1);
	GL(glClear)(GL_COLOR_BUFFER_BIT);
	GL(glViewport)(vx, vy, vw, vh);
	/* the window stays opaque whatever alpha the shader writes: on the web
	   the canvas has an alpha channel (see sysvid_setDisplayPalette) */
	GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
}

/* make <prog> current with the quad and the uniforms that do not depend on inputs */
static void use_program(GLuint prog, const GLfloat *mvp, int ow, int oh, int vw, int vh)
{
	GLuint a;
	GLint loc;

	GL(glUseProgram)(prog);
	for (a = 0; a < MAX_ATTRIBS; a++)
		GL(glDisableVertexAttribArray)(a);
	if ((loc = GL(glGetAttribLocation)(prog, "VertexCoord")) >= 0)
	{
		GL(glEnableVertexAttribArray)((GLuint)loc);
		GL(glVertexAttribPointer)((GLuint)loc, 4, GL_FLOAT, GL_FALSE, 4 * sizeof(GLfloat), NULL);
	}
	if ((loc = GL(glGetAttribLocation)(prog, "COLOR")) >= 0)
		GL(glVertexAttrib4f)((GLuint)loc, 1, 1, 1, 1);
	if ((loc = GL(glGetAttribLocation)(prog, "Color")) >= 0)
		GL(glVertexAttrib4f)((GLuint)loc, 1, 1, 1, 1);

	if ((loc = GL(glGetUniformLocation)(prog, "MVPMatrix")) >= 0)
		GL(glUniformMatrix4fv)(loc, 1, GL_FALSE, mvp);
	if ((loc = GL(glGetUniformLocation)(prog, "FrameCount")) >= 0)
		GL(glUniform1i)(loc, frame_count);
	if ((loc = GL(glGetUniformLocation)(prog, "FrameDirection")) >= 0)
		GL(glUniform1i)(loc, 1);
	if ((loc = GL(glGetUniformLocation)(prog, "OutputSize")) >= 0)
		GL(glUniform2f)(loc, (GLfloat)ow, (GLfloat)oh);
	if ((loc = GL(glGetUniformLocation)(prog, "FinalViewportSize")) >= 0)
		GL(glUniform2f)(loc, (GLfloat)vw, (GLfloat)vh);
}



/*
 * sysvid_gl_present
 *
 * see sysvid_gl.h
 */
void sysvid_gl_present(const Uint8 *rgba)
{
	int dw, dh, vw, vh, vx, vy, in_w, in_h, i;
	GLuint in_tex;
	const bezel_t *bz = (bezelMode != BEZEL_NONE && bezels[bezelMode].tex) ? &bezels[bezelMode] : NULL;
	int bx = 0, by = 0, bw = 0, bh = 0; /* outer bezel-image rect, only set when bz */

	if (!letterbox(&dw, &dh, &vx, &vy, &vw, &vh))
		return;

	/* a bezel overrides the chain's own target size to its screen cutout, drawn within
	   the bezel image's own letterboxed rect -- the chain then runs exactly as it would
	   fill the whole window, just smaller and offset; see the final curvature pass
	   below for where that output actually reaches the window */
	if (bz) computeBezelViewports(dw, dh, bz, &bx, &by, &bw, &bh, &vx, &vy, &vw, &vh);

	/* our own copy (like sysvid_gpu.c's `pixels`) so the OSD and settings menu can draw
	   on top without touching sysvid.c's buffer -- needed now that menu.c can link into
	   the web build too (see build.sh) */
	memcpy(gl_pixels, rgba, (size_t)orig_w * (size_t)orig_h * sizeof(U32));
	if (sysvid_osdActive())
		sysvid_drawOSD();
	if (menu_active())
		menu_draw();

	GL(glActiveTexture)(GL_TEXTURE0);
	GL(glBindTexture)(GL_TEXTURE_2D, orig);
	GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, orig_w, orig_h, GL_RGBA, GL_UNSIGNED_BYTE, gl_pixels);

	GL(glBindVertexArray)(vao);
	GL(glBindBuffer)(GL_ARRAY_BUFFER, vbo);

	if (bz)
	{
		/* background: the bezel artwork itself, letterboxed into the window. Drawn
		   first so the chain's own passes (never cleared themselves) land on top of
		   it within the cutout -- same layering as sysvid_gpu.c's bezel background +
		   curvature passes. */
		GLuint unit = 0;

		GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
		GL(glViewport)(0, 0, dw, dh);
		GL(glClearColor)(0, 0, 0, 1);
		GL(glClear)(GL_COLOR_BUFFER_BIT);
		GL(glViewport)(bx, by, bw, bh);
		GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		/* the bezel PNG's area outside the monitor shape is transparent, not opaque
		   black -- blend it against the clear color instead of copying its (unused,
		   possibly garbage-white) RGB straight in, same as sysvid_gpu.c's own
		   gpuPassthroughPipeline for this draw (its only blend-enabled pipeline) */
		GL(glEnable)(GL_BLEND);
		GL(glBlendFunc)(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
		set_filter(bz->tex, 1);
		use_program(image_prog, mvp_window, bw, bh, bw, bh);
		bind_frame(image_prog, "", bz->tex, bz->w, bz->h, &unit);
		bind_frame(image_prog, "Orig", bz->tex, bz->w, bz->h, &unit);
		GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
		GL(glDisable)(GL_BLEND);
	}

	in_tex = orig;
	in_w = orig_w;
	in_h = orig_h;
	for (i = 0; i < npasses; i++)
	{
		const shader_pass_t *c = &cur_chain->list[i];
		pass_t *p = &passes[i];
		int last = i == npasses - 1 && !bz; /* with a bezel, the curvature pass below is last */
		int ow, oh, j;
		GLuint unit;
		char name[32];

		if (last)
		{
			ow = vw;
			oh = vh;
			begin_window(dw, dh, vx, vy, vw, vh);
		}
		else
		{
			switch (c->scale_type)
			{
			case SCALE_VIEWPORT:
				ow = (int)((float)vw * c->scale);
				oh = (int)((float)vh * c->scale);
				break;
			case SCALE_ABSOLUTE:
				ow = oh = (int)c->scale;
				break;
			default:
				ow = (int)((float)in_w * c->scale);
				oh = (int)((float)in_h * c->scale);
				break;
			}
			if (c->max_scale > 0 && (ow > (int)((float)in_w * c->max_scale) ||
				oh > (int)((float)in_h * c->max_scale)))
			{
				ow = (int)((float)in_w * c->max_scale);
				oh = (int)((float)in_h * c->max_scale);
			}
			if (ow < 1) ow = 1;
			if (oh < 1) oh = 1;
			if (!size_output(p, ow, oh))
				return;
			GL(glBindFramebuffer)(GL_FRAMEBUFFER, p->fbo);
			GL(glViewport)(0, 0, ow, oh);
		}

		use_program(p->prog, last ? mvp_window : mvp_fbo, ow, oh, vw, vh);

		/* this pass's input, then what came before it */
		set_filter(in_tex, c->filter_linear);
		unit = 0;
		bind_frame(p->prog, "", in_tex, in_w, in_h, &unit);
		bind_frame(p->prog, "Orig", orig, orig_w, orig_h, &unit);
		if (i > 0)
		{
			snprintf(name, sizeof(name), "PassPrev%d", i + 1);
			bind_frame(p->prog, name, orig, orig_w, orig_h, &unit);
		}
		for (j = 0; j < i; j++)
		{
			snprintf(name, sizeof(name), "Pass%d", j + 1);
			bind_frame(p->prog, name, passes[j].tex, passes[j].w, passes[j].h, &unit);
			snprintf(name, sizeof(name), "PassPrev%d", i - j);
			bind_frame(p->prog, name, passes[j].tex, passes[j].w, passes[j].h, &unit);
		}

		GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);

		in_tex = p->tex;
		in_w = ow;
		in_h = oh;
	}

	if (bz)
	{
		/* screen curvature, straight into the cutout -- no clear, so the bezel
		   background drawn above stays visible around it (see sysvid_gpu.c's
		   crt-curvature.frag comment for why this is decoupled from the CRT chain
		   itself: a physical tube curves inside its bezel regardless of which
		   filter produced the picture) */
		GLuint unit = 0;

		GL(glBindFramebuffer)(GL_FRAMEBUFFER, 0);
		GL(glViewport)(vx, vy, vw, vh);
		GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_FALSE);
		use_program(curvature_prog, mvp_window, vw, vh, vw, vh);
		set_filter(in_tex, 1);
		bind_frame(curvature_prog, "", in_tex, in_w, in_h, &unit);
		GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
	}
	GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	SDL_GL_SwapWindow(window);
	frame_count++;
}



/*
 * sysvid_gl_showImage
 *
 * see sysvid_gl.h
 */
void sysvid_gl_showImage(const Uint8 *rgba, int w, int h, int pitch)
{
	int dw, dh, vw, vh, vx, vy;
	GLuint tex, unit = 0;

	if (!letterbox(&dw, &dh, &vx, &vy, &vw, &vh))
		return;

	GL(glBindVertexArray)(vao);
	GL(glBindBuffer)(GL_ARRAY_BUFFER, vbo);

	GL(glActiveTexture)(GL_TEXTURE0);
	tex = new_texture(w, h);
	GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, pitch / 4);
	GL(glTexSubImage2D)(GL_TEXTURE_2D, 0, 0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
	GL(glPixelStorei)(GL_UNPACK_ROW_LENGTH, 0);
	set_filter(tex, image_pass.filter_linear);

	begin_window(dw, dh, vx, vy, vw, vh);
	use_program(image_prog, mvp_window, vw, vh, vw, vh);
	bind_frame(image_prog, "", tex, w, h, &unit);
	bind_frame(image_prog, "Orig", tex, w, h, &unit);
	GL(glDrawArrays)(GL_TRIANGLE_STRIP, 0, 4);
	GL(glColorMask)(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);

	SDL_GL_SwapWindow(window);
	GL(glDeleteTextures)(1, &tex);
}



/*
 * sysvid_gl_shutdown
 *
 * releases the GL objects, the context and the window. safe on a partial init.
 */
void sysvid_gl_shutdown(void)
{
	int i;

	if (context)
	{
		for (i = 0; i < MAX_PASSES; i++)
		{
			if (passes[i].prog) GL(glDeleteProgram)(passes[i].prog);
			if (passes[i].tex) GL(glDeleteTextures)(1, &passes[i].tex);
			if (passes[i].fbo) GL(glDeleteFramebuffers)(1, &passes[i].fbo);
		}
		if (image_prog) GL(glDeleteProgram)(image_prog);
		if (curvature_prog) GL(glDeleteProgram)(curvature_prog);
		for (i = 0; i < BEZEL_COUNT; i++)
			if (bezels[i].tex) GL(glDeleteTextures)(1, &bezels[i].tex);
		if (orig) GL(glDeleteTextures)(1, &orig);
		if (vbo) GL(glDeleteBuffers)(1, &vbo);
		if (vao) GL(glDeleteVertexArrays)(1, &vao);
		SDL_GL_DestroyContext(context);
	}
	if (window)
		SDL_DestroyWindow(window);
	SDL_memset(passes, 0, sizeof(passes));
	SDL_memset(bezels, 0, sizeof(bezels));
	orig = vbo = vao = image_prog = curvature_prog = 0;
	context = NULL;
	window = NULL;
	if (gl_pixels) { free(gl_pixels); gl_pixels = NULL; }
	SDL_GL_ResetAttributes();
}



/*
 * Getters/setters for the settings menu (menu.c), matching sysvid_gpu.h's API
 * one-for-one so the same menu code works against either backend.
 * isFullscreen/getWindowZoom query the live SDL window state, same as sysvid_gpu.c.
 */
U8
sysvid_isFullscreen(void)
{
	return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) ? TRUE : FALSE;
}

U8
sysvid_getWindowZoom(void)
{
	int w = 0, h = 0;

	if (!window || orig_w == 0) return 1;
	SDL_GetWindowSize(window, &w, &h);
	return (U8)SDL_max(1, w / orig_w);
}

int
sysvid_getUpscale(void)
{
	return upscaleMode;
}

/*
 * switching chains tears down and rebuilds the GL programs/FBOs for the passes that
 * differ in count/shape from the previous chain -- simplest to just rebuild all of
 * them, same cost as sysvid_gl_init's own first build and only happens on a menu
 * change, never per-frame.
 */
static void
rebuildChain(void)
{
	int i;

	if (!context) return;

	for (i = 0; i < npasses; i++)
	{
		if (passes[i].prog) GL(glDeleteProgram)(passes[i].prog);
		if (passes[i].tex) GL(glDeleteTextures)(1, &passes[i].tex);
		if (passes[i].fbo) GL(glDeleteFramebuffers)(1, &passes[i].fbo);
	}
	SDL_memset(passes, 0, sizeof(passes));

	cur_chain = activeChain();
	npasses = cur_chain->count;
	for (i = 0; i < npasses; i++)
		passes[i].prog = build_program(&cur_chain->list[i]);
}

void
sysvid_setUpscale(int v)
{
	if (v < 0 || v >= UPSCALE_COUNT || v == upscaleMode) return;
	upscaleMode = v;
	if (crtMode == CRT_NONE) rebuildChain(); /* else the CRT chain is active -- see activeChain() */
}

int
sysvid_getCrt(void)
{
	return crtMode;
}

void
sysvid_setCrt(int v)
{
	/* CRT_ROYALE needs a LUT texture (phosphor mask) this driver's GLSL binder doesn't
	   support yet, so it's refused outright -- never offered as a menu choice on this
	   backend (menu.c's I_CRT handler already retries on rejection, same as it does for
	   sysvid_gpu.c's own royale-mask-failed-to-load case) and never settable via -crt
	   either, unlike bezelMode/a missing bezel, which can still be *stored* since a
	   bezel PNG could in principle load later -- royale can't, on this backend, ever */
	if (v < 0 || v >= CRT_COUNT || v == CRT_ROYALE || v == crtMode) return;
	crtMode = v;
	rebuildChain();
}

int
sysvid_getBezel(void)
{
	return bezelMode;
}

void
sysvid_setBezel(int v)
{
	if (v >= 0 && v < BEZEL_COUNT && (v == BEZEL_NONE || bezels[v].tex != 0))
		bezelMode = v;
}

int
sysvid_getAspect(void)
{
	return aspectMode;
}

void
sysvid_setAspect(int v)
{
	aspectMode = v ? 1 : 0;
}

/*
 * F10/F11/F12 direct key handling (sysevt.c) -- cycling still works without the menu;
 * no OSD confirmation here (unlike sysvid_gpu.c's own cycle* functions) since the next
 * sysvid_gl_present already redraws the frame right after these run.
 */
void
sysvid_cycleUpscale(void)
{
	sysvid_setUpscale((upscaleMode + 1) % UPSCALE_COUNT);
}

void
sysvid_cycleCrt(void)
{
	int next = crtMode;

	do {
		next = (next + 1) % CRT_COUNT;
	} while (next == CRT_ROYALE);
	sysvid_setCrt(next);
}

void
sysvid_cycleBezel(void)
{
	int next = bezelMode;

	do {
		next = (next + 1) % BEZEL_COUNT;
	} while (next != BEZEL_NONE && bezels[next].tex == 0);
	bezelMode = next;
}

#endif /* ENABLE_SHADERS */

/* eof */

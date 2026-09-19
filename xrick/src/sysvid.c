/*
 * xrick/src/sysvid.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 *
 * SDL3 port and later modifications by Senjin the Dragon.
 */

/*
 * The purpose of this file is to implement a set of functions so that the
 * 8bit, palettized frame buffer onto which the entire game is painted can
 * be displayed onto the computer's screen.
 *
 * The only dependency between this and the game is that here we know the
 * frame buffer is 8bit. We don't know its size.
 *
 * Presentation goes through SDL_GPU (not the classic SDL_Renderer): the
 * game composites its 8bit framebuffer into a plain RGBA texture on the
 * CPU exactly as before, then a short chain of fullscreen-triangle GPU
 * passes (upscale filter, then CRT effect -- each independently
 * selectable and combinable) samples that texture onto the swapchain.
 * See xrick/src/shaders/.
 */


#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

#include <SDL3/SDL.h>

#include "sysvid.h"
#include "menu.h"
#include "sysarg.h"
#include "debug.h"
#include "fb.h"
#include "img.h"
#include "shaders_embedded.h"
#include "bezels_embedded.h"
#include "masks_embedded.h"
#include "thirdparty/stb_image.h"


#ifdef __MSVC__
#include <memory.h> /* memset */
#endif


rect_t SCREENRECT = {0, 0, FB_WIDTH, FB_HEIGHT, NULL}; /* whole fb */

static U16 paln;		       /* palette size */
static SDL_Color pals[256], pald[256]; /* fixme: explain */
static U32 *pixels;		       /* composited RGBA frame, CPU side */
static SDL_Window *screen;
static U8 isFullscreen;
static U8 gamma;
static U16 fb_width, fb_height;

/* three independent, freely-combinable axes -- see sysvid_cycleUpscale/Crt/Bezel */
typedef enum { UPSCALE_NONE = 0,
	       UPSCALE_SHARP,
	       UPSCALE_FSR1,
	       UPSCALE_COUNT } upscaleMode_t;
typedef enum { CRT_NONE = 0,
	       CRT_EASYMODE,
	       CRT_LOTTES,
	       CRT_ROYALE,
	       CRT_COUNT } crtMode_t;
typedef enum { BEZEL_NONE = 0,
	       BEZEL_ATARI_SC1224,
	       BEZEL_COMMODORE_1084S,
	       BEZEL_COUNT } bezelMode_t;
static upscaleMode_t upscaleMode = UPSCALE_NONE;
static crtMode_t crtMode = CRT_NONE;
static bezelMode_t bezelMode = BEZEL_NONE;
/* 0: 4:3 pixel-aspect corrected (the original's 320x200 was shown on 4:3
 * monitors, so pixels were slightly tall); 1: square pixels (1.6:1) */
static U8 aspectMode = 0;

/* a static bezel: a monitor photo with a "screen" area the game's own
 * output gets composited into. screenX0/Y0/X1/Y1 are that area's bounds,
 * as fractions of the full bezel image, measured once from the source art. */
typedef struct {
	SDL_GPUTexture *texture;
	Uint32 texW, texH;
	float screenX0, screenY0, screenX1, screenY1;
} bezelInfo_t;
static bezelInfo_t bezels[BEZEL_COUNT];

/* a 1x1 opaque black PNG, drawn behind the picture inside a bezel's screen
 * opening so the bars left by a narrower aspect ratio are black rather than
 * the monitor's grey face */
static const unsigned char blackPng[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x48, 0x44, 0x52, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1f, 0x15, 0xc4, 0x89, 0x00, 0x00, 0x00, 0x0d, 0x49, 0x44, 0x41, 0x54, 0x78, 0x9c, 0x63, 0x60, 0x60, 0x60, 0xf8, 0x0f, 0x00, 0x01, 0x04, 0x01, 0x00, 0x5f, 0xe5, 0xc3, 0x4b, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4e, 0x44, 0xae, 0x42, 0x60, 0x82};
static SDL_GPUTexture *gpuBlackTexture; /* [BEZEL_NONE] left zeroed/unused */

static SDL_GPUDevice *gpuDevice;
static SDL_GPUTexture *gpuTexture;	      /* holds the composited frame */
static SDL_GPUTransferBuffer *gpuTransferBuf; /* CPU->GPU upload staging */
static SDL_GPUSampler *gpuSampler;

static SDL_GPUShader *gpuVertShader;
static SDL_GPUShader *gpuPassthroughFragShader;
static SDL_GPUShader *gpuSharpFragShader;
static SDL_GPUShader *gpuCrtLottesFragShader;
static SDL_GPUShader *gpuCrtEasymodeFragShader;	    /* pre-upscale: does its own resample */
static SDL_GPUShader *gpuCrtEasymodePostFragShader; /* post-upscale: samples 1:1 */
static SDL_GPUShader *gpuEasuFragShader;
static SDL_GPUShader *gpuRcasFragShader;

static SDL_GPUGraphicsPipeline *gpuPassthroughPipeline;		      /* bezel background draw, and the final "no curvature" blit -- both -> swapchain */
static SDL_GPUGraphicsPipeline *gpuPassthroughToIntermediatePipeline; /* no upscale, no crt -> gpuFinalIntermediate */
static SDL_GPUGraphicsPipeline *gpuSharpToIntermediatePipeline;	      /* sharp upscale -> gpuIntermediate1 (then crt post) */
static SDL_GPUGraphicsPipeline *gpuSharpToFinalPipeline;	      /* sharp upscale, no crt -> gpuFinalIntermediate */
static SDL_GPUGraphicsPipeline *gpuCrtLottesPipeline;		      /* crt-lottes: does its own resample -> gpuFinalIntermediate */
static SDL_GPUGraphicsPipeline *gpuCrtEasymodePipeline;		      /* no upscale, crt -> gpuFinalIntermediate */
static SDL_GPUGraphicsPipeline *gpuEasuPipeline;		      /* upscale pass 1 -> intermediate */
static SDL_GPUGraphicsPipeline *gpuRcasToSwapchainPipeline;	      /* upscale, no crt, pass 2 -> gpuFinalIntermediate */
static SDL_GPUGraphicsPipeline *gpuRcasToIntermediatePipeline;	      /* upscale+crt, pass 2 -> intermediate */
static SDL_GPUGraphicsPipeline *gpuCrtEasymodePostPipeline;	      /* upscale+crt, pass 3 -> gpuFinalIntermediate */
static SDL_GPUShader *gpuCurvatureFragShader;
static SDL_GPUGraphicsPipeline *gpuCurvaturePipeline; /* final pass, bezel active: gpuFinalIntermediate -> swapchain, warped */
static SDL_GPUShader *gpuFinalEncodeFragShader;
static SDL_GPUGraphicsPipeline *gpuFinalEncodePipeline; /* final pass, no bezel: gpuFinalIntermediate -> swapchain, straight copy (+ crt-royale's deferred gamma-encode) */

/* intermediate textures for the upscale chain, sized to the output
 * viewport (so (re)allocated on demand as that changes with zoom/
 * fullscreen/window resize); intermediate2 is only used when a CRT
 * effect is also chained on top of the upscaler */
static SDL_GPUTexture *gpuIntermediate1, *gpuIntermediate2;
static Uint32 intermediateW, intermediateH;

/* every filter combination's flat output lands here (see the unified
 * final-pass comment in sysvid_update()); (re)allocated on demand like
 * gpuIntermediate1/2 above */
static SDL_GPUTexture *gpuFinalIntermediate;
static Uint32 finalIntermediateW, finalIntermediateH;
static Uint32 fsrFrameCount;

/* crt-royale: a 6-pass pipeline (see xrick/src/shaders/crt-royale-*.frag
 * for the full derivation/porting notes). CRT_ROYALE is mutually
 * exclusive with the upscale axis -- its own vertical/horizontal scanline
 * passes already resample to the full output resolution, the same job
 * FSR1 would otherwise do, and the real crt-royale.slangp preset doesn't
 * chain with a separate upscaler either. */
static SDL_GPUShader *gpuRoyaleLinearizeFragShader;
static SDL_GPUShader *gpuRoyaleVscanFragShader;
static SDL_GPUShader *gpuRoyaleBloomApproxFragShader;
static SDL_GPUShader *gpuRoyaleHscanMaskFragShader;
static SDL_GPUShader *gpuRoyaleBrightpassFragShader;
static SDL_GPUShader *gpuRoyaleBloomBlurFragShader;
static SDL_GPUShader *gpuRoyaleReconstituteFragShader;

static SDL_GPUGraphicsPipeline *gpuRoyaleLinearizePipeline;
static SDL_GPUGraphicsPipeline *gpuRoyaleVscanPipeline;
static SDL_GPUGraphicsPipeline *gpuRoyaleBloomApproxPipeline;
static SDL_GPUGraphicsPipeline *gpuRoyaleHscanMaskPipeline;
static SDL_GPUGraphicsPipeline *gpuRoyaleBrightpassPipeline;
static SDL_GPUGraphicsPipeline *gpuRoyaleBloomBlurPipeline;    /* run twice: v then h */
static SDL_GPUGraphicsPipeline *gpuRoyaleReconstitutePipeline; /* final pass -> swapchain (viewport-clipped to the bezel cutout when one's active) */

static SDL_GPUTexture *gpuPhosphorMaskTexture;
static Uint32 phosphorMaskTexW, phosphorMaskTexH;
static float royaleMaskAmplify; /* 1 / (selected mask type's average color); set alongside the texture load */

/* fixed-size royale buffers, allocated once (never depend on the output
 * viewport): the linearized source frame (320x200) and the small fixed
 * 320x240 bloom-approx target crt-royale.slangp itself specifies. */
static SDL_GPUTexture *gpuRoyaleLinearized;
static SDL_GPUTexture *gpuRoyaleBloomApprox;
/* viewport-sized royale buffers, (re)allocated together on resize */
static SDL_GPUTexture *gpuRoyaleVscan;		 /* 320 x viewport height */
static SDL_GPUTexture *gpuRoyaleMaskedScanlines; /* full viewport */
static SDL_GPUTexture *gpuRoyaleBrightpass;	 /* full viewport */
static SDL_GPUTexture *gpuRoyaleBloomV;		 /* full viewport, vertically blurred brightpass */
static SDL_GPUTexture *gpuRoyaleBloomH;		 /* full viewport, fully blurred brightpass */
static Uint32 royaleViewportW, royaleViewportH;

static SDL_GPUSampler *gpuSamplerLinearClamp;  /* bilinear, clamp -- bloom-approx resize */
static SDL_GPUSampler *gpuSamplerLinearRepeat; /* bilinear, repeat -- tiled phosphor mask */
static SDL_GPUSampler *gpuSamplerMipmap;       /* trilinear, clamp -- curvature's warped lookup */

static U8 zoom = 0;		    /* actual zoom level */
static U8 wmzoom = SYSVID_ZOOM;	    /* window mode zoom level */
static U8 mxzoom = SYSVID_ZOOM * 2; /* max zoom level */

/*
 * OSD: brief "Shader: X" / "Upscaling: X" confirmation shown for a few
 * seconds after F10/F11. Stamped directly onto the composited RGBA frame
 * (not into the game's own 8bit fb) so it can't collide with or get stuck
 * in game state -- every sysvid_update() call re-draws it from scratch for
 * as long as its timer is running, and simply stops once expired.
 */
#define OSD_DURATION_MS 5000
#define OSD_SCALE 1
#define OSD_MARGIN 6
#define OSD_BG_ALPHA 110 /* 0-255 opacity of the backing box, blended over whatever's underneath -- ~43% opaque / 57% see-through */
#define OSD_MSG_MAX 32

static char osdMessage[OSD_MSG_MAX];
static U32 osdExpireAt;

/* tiny 5x7 bitmap font (upper case, digits, common punctuation) -- used by
 * the OSD and the settings menu; add more glyphs as needed */
typedef struct {
	char c;
	U8 rows[7];
} osdGlyph_t;
static const osdGlyph_t osdFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'\\', {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'*', {0x00, 0x11, 0x0A, 0x1F, 0x0A, 0x11, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
#define OSD_FONT_COUNT (sizeof(osdFont) / sizeof(osdFont[0]))


#include "img_icon.e"


/*
 * sysvid_setPaletteFromImg
 *
 * sets the palette according to an image palette.
 */
void
sysvid_setPaletteFromImg(img_t *img)
{
	U16 i; // FIXME is it ok to have 256 (not 255) colors?

	if ((paln = img->ncolors) == 0) return;

	for (i = 0; i < paln; ++i) {
		pals[i].r = img->colors[i].r;
		pals[i].g = img->colors[i].g;
		pals[i].b = img->colors[i].b;
	}

	sysvid_setDisplayPalette();
}


/*
 * sysvid_setPaletteFromRGB
 *
 * sets the palette according to RGB infos.
 */
void
sysvid_setPaletteFromRGB(U8 *r, U8 *g, U8 *b, U16 n)
{
	U16 i;

	if ((paln = n) == 0) return;

	for (i = 0; i < paln; ++i) {
		pals[i].r = r[i];
		pals[i].g = g[i];
		pals[i].b = b[i];
	}

	sysvid_setDisplayPalette();
}


/*
 * sysvid_setDisplayPalette
 *
 * sets (again) the display palette, useful when visibility has changed.
 */
void
sysvid_setDisplayPalette(void)
{
	U16 i;

	if (paln == 0) return;

	for (i = 0; i < paln; i++) {
		pald[i].r = pals[i].r * gamma / 255;
		pald[i].g = pals[i].g * gamma / 255;
		pald[i].b = pals[i].b * gamma / 255;
		pald[i].a = 255;
	}
}


/*
 * makeIconSurface
 *
 * builds an RGBA32 surface for the window icon out of the palettized
 * icon image, treating the color of the first pixel as transparent.
 */
static SDL_Surface *
makeIconSurface(void)
{
	SDL_Surface *s;
	U32 *dst;
	U8 *src, tpix;
	U32 i, len;

	len = (U32)IMG_ICON->w * IMG_ICON->h;
	tpix = *(IMG_ICON->pixels);

	s = SDL_CreateSurface(IMG_ICON->w, IMG_ICON->h, SDL_PIXELFORMAT_RGBA32);
	if (!s) return NULL;

	src = IMG_ICON->pixels;
	dst = (U32 *)s->pixels;
	for (i = 0; i < len; i++) {
		U8 pix = src[i];
		img_color_t *c = &IMG_ICON->colors[pix];
		dst[i] = SDL_MapRGBA(SDL_GetPixelFormatDetails(SDL_PIXELFORMAT_RGBA32), NULL,
				     c->r, c->g, c->b, pix == tpix ? 0 : 255);
	}

	return s;
}


/*
 * computeLetterboxViewportFor
 *
 * fits srcW:srcH into winW x winH, centered, preserving aspect ratio --
 * SDL_Renderer's logical presentation did this for free; on raw SDL_GPU
 * it's ours to compute. Used both for the game's own fb_width:fb_height
 * (no bezel) and for a bezel image's aspect ratio (see computeBezelViewports).
 */
static void
computeLetterboxViewportFor(Uint32 srcW, Uint32 srcH, Uint32 winW, Uint32 winH, SDL_GPUViewport *vp)
{
	float srcAspect = (float)srcW / (float)srcH;
	float dstAspect = (float)winW / (float)winH;
	float w, h;

	if (dstAspect > srcAspect) {
		h = (float)winH;
		w = h * srcAspect;
	} else {
		w = (float)winW;
		h = w / srcAspect;
	}

	vp->x = ((float)winW - w) / 2.0f;
	vp->y = ((float)winH - h) / 2.0f;
	vp->w = w;
	vp->h = h;
	vp->min_depth = 0.0f;
	vp->max_depth = 1.0f;
}


/*
 * computeLetterboxViewport
 *
 * the no-bezel case: fits fb_width:fb_height into winW x winH.
 */
static void
computeLetterboxViewport(Uint32 winW, Uint32 winH, SDL_GPUViewport *vp)
{
	if (aspectMode == 0)
		computeLetterboxViewportFor(4, 3, winW, winH, vp);
	else
		computeLetterboxViewportFor(fb_width, fb_height, winW, winH, vp);
}


/*
 * computeBezelViewports
 *
 * fits the bezel image into winW x winH (the largest size that fits,
 * same as before), then -- rather than deriving the "screen" cutout's
 * viewport as an arbitrary fraction of that -- shrinks the whole bezel
 * uniformly (preserving its own aspect ratio) by whatever factor makes
 * the cutout's rendered HEIGHT an exact integer multiple of fb_height,
 * and re-centers the result. The cutout is where the game's own output
 * (after any upscale/CRT passes) gets drawn.
 *
 * Why: crt-royale-vscan.frag's scanline-beam resampling assumes a
 * roughly constant sample phase from one output row to the next, which
 * only holds when the output height is an exact multiple of the
 * source's 200 rows -- at any other scale, the phase drifts slowly down
 * the screen and beats against the pixel grid as a faint but real
 * moving band (confirmed real via pass isolation and offline
 * simulation; this is also exactly why the artifact disappears entirely
 * with no bezel active, which already integer-scales via zoom).
 * Fitting the CONTENT to an integer size first, then
 * fitting the bezel artwork around that (rather than the reverse), is
 * Senjin's fix and removes the non-integer scale at the source instead
 * of trying to hide its symptom downstream. Only the vertical axis is
 * forced to an integer multiple here -- crt-royale-hscan-mask.frag's
 * horizontal resampling doesn't show this symptom, so leaving the
 * cutout's width to float (whatever the uniform bezel scale produces)
 * is fine. */
static void
computeBezelViewports(Uint32 winW, Uint32 winH, const bezelInfo_t *bz,
		      SDL_GPUViewport *outerVp, SDL_GPUViewport *innerVp)
{
	SDL_GPUViewport rawOuter;
	float rawInnerH, scale;
	int n;

	computeLetterboxViewportFor(bz->texW, bz->texH, winW, winH, &rawOuter);
	rawInnerH = (bz->screenY1 - bz->screenY0) * rawOuter.h;

	n = (int)(rawInnerH / (float)fb_height); /* round down: never spill past the window */
	if (n < 1) n = 1;
	scale = (n * (float)fb_height) / rawInnerH;

	outerVp->w = rawOuter.w * scale;
	outerVp->h = rawOuter.h * scale;
	outerVp->x = ((float)winW - outerVp->w) / 2.0f;
	outerVp->y = ((float)winH - outerVp->h) / 2.0f;
	outerVp->min_depth = 0.0f;
	outerVp->max_depth = 1.0f;

	innerVp->x = outerVp->x + bz->screenX0 * outerVp->w;
	innerVp->y = outerVp->y + bz->screenY0 * outerVp->h;
	innerVp->w = (bz->screenX1 - bz->screenX0) * outerVp->w;
	innerVp->h = (bz->screenY1 - bz->screenY0) * outerVp->h;
	innerVp->min_depth = 0.0f;
	innerVp->max_depth = 1.0f;
}


/*
 * ensureIntermediates
 *
 * (re)allocates the upscale-chain intermediate textures if the output
 * viewport size (window resize/zoom/fullscreen) has changed since last time.
 */
static void
ensureIntermediates(Uint32 w, Uint32 h)
{
	SDL_GPUTextureCreateInfo texInfo;

	if (gpuIntermediate1 && intermediateW == w && intermediateH == h)
		return;

	if (gpuIntermediate1) SDL_ReleaseGPUTexture(gpuDevice, gpuIntermediate1);
	if (gpuIntermediate2) SDL_ReleaseGPUTexture(gpuDevice, gpuIntermediate2);

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texInfo.width = w;
	texInfo.height = h;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	gpuIntermediate1 = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	gpuIntermediate2 = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	intermediateW = w;
	intermediateH = h;
}


/*
 * ensureFinalIntermediate
 *
 * (re)allocates gpuFinalIntermediate -- every filter combination's flat
 * render target -- when the output viewport size changes. Has a full mip
 * chain: the curvature pass's nonlinear warp locally minifies the image
 * by a varying amount, and without mipmapping that aliases the phosphor
 * mask/scanline pattern's fine repeating structure into a visible moire
 * (hardware trilinear filtering picks the right mip level automatically
 * from the warped uv's screen-space derivatives -- see gpuSamplerMipmap).
 */
static void
ensureFinalIntermediate(Uint32 w, Uint32 h)
{
	SDL_GPUTextureCreateInfo texInfo;
	Uint32 maxDim, levels;

	if (gpuFinalIntermediate && finalIntermediateW == w && finalIntermediateH == h)
		return;

	if (gpuFinalIntermediate) SDL_ReleaseGPUTexture(gpuDevice, gpuFinalIntermediate);

	maxDim = (w > h) ? w : h;
	levels = 1;
	while (maxDim > 1) {
		maxDim >>= 1;
		levels++;
	}

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	/* HDR-capable (not R8G8B8A8_UNORM, which hard-clamps to [0,1]):
	 * crt-royale's phosphor-mask-amplified bright pixels can exceed 1.0
	 * before its own final clamp -- if that clamp happens here (pre-
	 * curvature), curvature's later mip/aniso averaging blends an
	 * already-clipped bright signal against the mask's dark gaps and
	 * comes out visibly darker than correct, worse wherever the warp's
	 * local minification is strongest. Deferring royale's clamp to
	 * *after* curvature's averaging (see crt-royale-reconstitute.frag
	 * and crt-curvature.frag's applyGamma uniform) needs this buffer to
	 * actually hold values above 1.0 in the meantime. Harmless for
	 * every other mode, which already writes clamped [0,1] content here. */
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texInfo.width = w;
	texInfo.height = h;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = levels;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	gpuFinalIntermediate = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	finalIntermediateW = w;
	finalIntermediateH = h;
}


/*
 * ensureRoyaleIntermediates
 *
 * (re)allocates crt-royale's viewport-sized buffers (vscan, masked
 * scanlines, brightpass, and the two bloom-blur passes) when the output
 * viewport size changes. Mirrors ensureIntermediates(); the two buffers
 * that DON'T depend on viewport size (linearized source, bloom-approx)
 * are allocated once in sysvid_init instead.
 */
static void
ensureRoyaleIntermediates(Uint32 w, Uint32 h)
{
	SDL_GPUTextureCreateInfo texInfo;

	if (gpuRoyaleVscan && royaleViewportW == w && royaleViewportH == h)
		return;

	if (gpuRoyaleVscan) SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleVscan);
	if (gpuRoyaleMaskedScanlines) SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleMaskedScanlines);
	if (gpuRoyaleBrightpass) SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBrightpass);
	if (gpuRoyaleBloomV) SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBloomV);
	if (gpuRoyaleBloomH) SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBloomH);

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	/* HDR-capable, not R8G8B8A8_UNORM: crt-royale deliberately lets
	 * intermediate values exceed 1.0 through this whole chain (that's
	 * what levels_autodim_temp's headroom is for -- see
	 * crt-royale-vscan.frag), only clamping once at the very end. A
	 * hard-clamped UNORM buffer here throws that away mid-pipeline.
	 *
	 * Full float32, not R16G16B16A16_FLOAT: crt-royale-reconstitute.frag
	 * computes `dimpass = intensityDim - brightpass`, subtracting two
	 * independently-rounded values that are often nearly equal (most of
	 * a bright/flat-color frame ends up almost entirely claimed by
	 * brightpass, leaving a near-zero remainder) -- classic catastrophic
	 * cancellation. That remainder then gets amplified ~11x
	 * (maskAmplify * UNDIM_FACTOR) before display. float16's ~10-bit
	 * mantissa was just barely coarse enough, after 4-5 passes' worth of
	 * accumulated rounding, for the amplified noise to become visible as
	 * colorful static -- confirmed by direct A/B: reproduced even with
	 * curvature bypassed entirely (so not a curvature/sampling bug), and
	 * disappeared when these buffers were widened to float32 (real bug,
	 * not a fundamental filtering limitation). */
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;

	texInfo.width = fb_width; /* vscan keeps the source width, only its height tracks the viewport */
	texInfo.height = h;
	gpuRoyaleVscan = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	texInfo.width = w;
	texInfo.height = h;
	gpuRoyaleMaskedScanlines = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	gpuRoyaleBrightpass = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	gpuRoyaleBloomV = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	gpuRoyaleBloomH = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	royaleViewportW = w;
	royaleViewportH = h;
}


/*
 * runPass
 *
 * runs one fullscreen-triangle GPU pass: bind pipeline, sample srcTex,
 * write into dstTex within the given viewport rect, with an optional
 * fragment uniform payload. loadOp is CLEAR for a fresh target, LOAD to
 * draw over existing content without disturbing pixels outside the
 * viewport (needed when compositing game content into a bezel's screen
 * cutout -- CLEAR/DONT_CARE apply to the whole attachment, not just the
 * viewport, so LOAD is the only safe choice there), or DONT_CARE for an
 * intermediate texture about to be fully overwritten anyway.
 */
static void
runPassMulti(SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipeline,
	     SDL_GPUTexture **srcTexs, SDL_GPUSampler **srcSamplers, Uint32 numTex,
	     SDL_GPUTexture *dstTex,
	     float vpX, float vpY, float vpW, float vpH, SDL_GPULoadOp loadOp,
	     const void *uniformData, Uint32 uniformLen)
{
	SDL_GPUColorTargetInfo colorInfo;
	SDL_GPURenderPass *pass;
	SDL_GPUViewport vp;
	SDL_GPUTextureSamplerBinding bindings[3];
	Uint32 i;

	memset(&colorInfo, 0, sizeof(colorInfo));
	colorInfo.texture = dstTex;
	colorInfo.load_op = loadOp;
	colorInfo.store_op = SDL_GPU_STOREOP_STORE;
	if (loadOp == SDL_GPU_LOADOP_CLEAR) {
		colorInfo.clear_color.r = 0.0f;
		colorInfo.clear_color.g = 0.0f;
		colorInfo.clear_color.b = 0.0f;
		colorInfo.clear_color.a = 1.0f;
	}

	pass = SDL_BeginGPURenderPass(cmd, &colorInfo, 1, NULL);
	SDL_BindGPUGraphicsPipeline(pass, pipeline);

	vp.x = vpX;
	vp.y = vpY;
	vp.w = vpW;
	vp.h = vpH;
	vp.min_depth = 0.0f;
	vp.max_depth = 1.0f;
	SDL_SetGPUViewport(pass, &vp);

	for (i = 0; i < numTex; i++) {
		bindings[i].texture = srcTexs[i];
		bindings[i].sampler = srcSamplers[i];
	}
	SDL_BindGPUFragmentSamplers(pass, 0, bindings, numTex);

	if (uniformData)
		SDL_PushGPUFragmentUniformData(cmd, 0, uniformData, uniformLen);

	SDL_DrawGPUPrimitives(pass, 3, 1, 0, 0);
	SDL_EndGPURenderPass(pass);
}


/*
 * runPass
 *
 * single-texture convenience wrapper around runPassMulti(), sampling with
 * the shared nearest/clamp sampler -- see runPassMulti() for what loadOp
 * to pick.
 */
static void
runPass(SDL_GPUCommandBuffer *cmd, SDL_GPUGraphicsPipeline *pipeline,
	SDL_GPUTexture *srcTex, SDL_GPUTexture *dstTex,
	float vpX, float vpY, float vpW, float vpH, SDL_GPULoadOp loadOp,
	const void *uniformData, Uint32 uniformLen)
{
	SDL_GPUTexture *texs[1] = {srcTex};
	SDL_GPUSampler *samplers[1] = {gpuSampler};

	runPassMulti(cmd, pipeline, texs, samplers, 1, dstTex,
		     vpX, vpY, vpW, vpH, loadOp, uniformData, uniformLen);
}


/*
 * createPipeline
 *
 * small helper: fullscreen-triangle pipeline, shared vertex shader,
 * given fragment shader and color target format. enableBlend turns on
 * standard src-alpha/one-minus-src-alpha blending -- needed only for
 * the bezel background draw, whose PNG has transparent pixels outside
 * the artwork (their RGB is meaningless "don't care" filler picked by
 * whatever tool exported the PNG -- must not be trusted as an opaque
 * color, only the alpha channel says what's really visible). Every
 * other pipeline draws fully opaque content, for which blending is a
 * no-op, so this only needs to be true at the one bezel call site.
 */
static SDL_GPUGraphicsPipeline *
createPipeline(SDL_GPUShader *fragShader, SDL_GPUTextureFormat targetFormat, bool enableBlend)
{
	SDL_GPUColorTargetDescription colorTarget;
	SDL_GPUGraphicsPipelineCreateInfo pipeInfo;

	memset(&colorTarget, 0, sizeof(colorTarget));
	colorTarget.format = targetFormat;
	if (enableBlend) {
		colorTarget.blend_state.enable_blend = true;
		colorTarget.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
		colorTarget.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
		colorTarget.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
		colorTarget.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
		colorTarget.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ZERO;
		colorTarget.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	}

	memset(&pipeInfo, 0, sizeof(pipeInfo));
	pipeInfo.vertex_shader = gpuVertShader;
	pipeInfo.fragment_shader = fragShader;
	pipeInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeInfo.rasterizer_state.fill_mode = SDL_GPU_FILLMODE_FILL;
	pipeInfo.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
	pipeInfo.rasterizer_state.front_face = SDL_GPU_FRONTFACE_COUNTER_CLOCKWISE;
	pipeInfo.multisample_state.sample_count = SDL_GPU_SAMPLECOUNT_1;
	pipeInfo.target_info.color_target_descriptions = &colorTarget;
	pipeInfo.target_info.num_color_targets = 1;

	return SDL_CreateGPUGraphicsPipeline(gpuDevice, &pipeInfo);
}


/*
 * loadPNGTexture
 *
 * decodes an embedded PNG (via stb_image) and uploads it as a GPU
 * texture, one-off (its own command buffer/transfer buffer, not the
 * per-frame ones used for the game's composited output). Returns NULL,
 * without panicking, if decoding or upload fails -- a missing bezel just
 * means that option quietly isn't offered (see sysvid_cycleBezel).
 */
static SDL_GPUTexture *
loadPNGTextureEx(const unsigned char *pngData, unsigned long pngLen, Uint32 *outW, Uint32 *outH)
{
	int w, h, comp;
	unsigned char *pix;
	SDL_GPUTexture *tex;
	SDL_GPUTextureCreateInfo texInfo;
	SDL_GPUTransferBufferCreateInfo tbInfo;
	SDL_GPUTransferBuffer *tb;
	void *mapped;
	SDL_GPUCommandBuffer *cmd;
	SDL_GPUCopyPass *copyPass;
	SDL_GPUTextureTransferInfo src;
	SDL_GPUTextureRegion dst;

	pix = stbi_load_from_memory(pngData, (int)pngLen, &w, &h, &comp, 4);
	(void)comp;
	if (!pix) return NULL;

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	texInfo.width = (Uint32)w;
	texInfo.height = (Uint32)h;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	tex = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	if (!tex) {
		stbi_image_free(pix);
		return NULL;
	}

	memset(&tbInfo, 0, sizeof(tbInfo));
	tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tbInfo.size = (Uint32)((size_t)w * h * 4);
	tb = SDL_CreateGPUTransferBuffer(gpuDevice, &tbInfo);
	if (!tb) {
		stbi_image_free(pix);
		SDL_ReleaseGPUTexture(gpuDevice, tex);
		return NULL;
	}

	mapped = SDL_MapGPUTransferBuffer(gpuDevice, tb, false);
	memcpy(mapped, pix, (size_t)w * h * 4);
	SDL_UnmapGPUTransferBuffer(gpuDevice, tb);
	stbi_image_free(pix);

	cmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
	memset(&src, 0, sizeof(src));
	src.transfer_buffer = tb;
	src.pixels_per_row = (Uint32)w;
	src.rows_per_layer = (Uint32)h;
	memset(&dst, 0, sizeof(dst));
	dst.texture = tex;
	dst.w = (Uint32)w;
	dst.h = (Uint32)h;
	dst.d = 1;
	copyPass = SDL_BeginGPUCopyPass(cmd);
	SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
	SDL_EndGPUCopyPass(copyPass);
	SDL_SubmitGPUCommandBuffer(cmd);

	SDL_ReleaseGPUTransferBuffer(gpuDevice, tb);

	*outW = (Uint32)w;
	*outH = (Uint32)h;
	return tex;
}

static SDL_GPUTexture *
loadPNGTexture(const unsigned char *pngData, unsigned long pngLen, Uint32 *outW, Uint32 *outH)
{
	return loadPNGTextureEx(pngData, pngLen, outW, outH);
}


/*
 * loadMipmappedPNGTexture
 *
 * like loadPNGTexture, but uploads a full, pre-rendered mip chain (one PNG
 * per level, level 0 first) instead of a single level -- for the
 * phosphor mask specifically, whose mip levels are generated offline with
 * wrap-aware (toroidal) resizing so every level is genuinely seamless
 * when hardware-tiled (see masks_embedded.h). SDL_GenerateMipmapsForGPUTexture
 * clamps at tile edges rather than wrapping and would silently reintroduce
 * that exact seam at every level above 0, so this never calls it.
 */
static SDL_GPUTexture *
loadMipmappedPNGTexture(const unsigned char *const *pngData, const unsigned long *pngLen, int numLevels)
{
	int w0 = 0, h0 = 0;
	unsigned char *pixels[MASK_MIP_LEVELS];
	SDL_GPUTexture *tex;
	SDL_GPUTextureCreateInfo texInfo;
	SDL_GPUTransferBufferCreateInfo tbInfo;
	SDL_GPUTransferBuffer *tb;
	void *mapped;
	SDL_GPUCommandBuffer *cmd;
	SDL_GPUCopyPass *copyPass;
	SDL_GPUTextureTransferInfo src;
	SDL_GPUTextureRegion dst;
	int level, w, h, comp;
	size_t totalSize = 0, offset;

	for (level = 0; level < numLevels; level++) {
		pixels[level] = stbi_load_from_memory(pngData[level], (int)pngLen[level], &w, &h, &comp, 4);
		if (!pixels[level]) {
			while (--level >= 0)
				stbi_image_free(pixels[level]);
			return NULL;
		}
		if (level == 0) {
			w0 = w;
			h0 = h;
		}
		totalSize += (size_t)w * h * 4;
	}

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	texInfo.width = (Uint32)w0;
	texInfo.height = (Uint32)h0;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = (Uint32)numLevels;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	tex = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	if (!tex) {
		for (level = 0; level < numLevels; level++)
			stbi_image_free(pixels[level]);
		return NULL;
	}

	memset(&tbInfo, 0, sizeof(tbInfo));
	tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tbInfo.size = (Uint32)totalSize;
	tb = SDL_CreateGPUTransferBuffer(gpuDevice, &tbInfo);
	if (!tb) {
		for (level = 0; level < numLevels; level++)
			stbi_image_free(pixels[level]);
		SDL_ReleaseGPUTexture(gpuDevice, tex);
		return NULL;
	}

	mapped = SDL_MapGPUTransferBuffer(gpuDevice, tb, false);
	offset = 0;
	for (level = 0; level < numLevels; level++) {
		w = (level == 0) ? w0 : SDL_max(1, w0 >> level);
		h = (level == 0) ? h0 : SDL_max(1, h0 >> level);
		memcpy((unsigned char *)mapped + offset, pixels[level], (size_t)w * h * 4);
		offset += (size_t)w * h * 4;
		stbi_image_free(pixels[level]);
	}
	SDL_UnmapGPUTransferBuffer(gpuDevice, tb);

	cmd = SDL_AcquireGPUCommandBuffer(gpuDevice);
	copyPass = SDL_BeginGPUCopyPass(cmd);
	offset = 0;
	for (level = 0; level < numLevels; level++) {
		w = (level == 0) ? w0 : SDL_max(1, w0 >> level);
		h = (level == 0) ? h0 : SDL_max(1, h0 >> level);
		memset(&src, 0, sizeof(src));
		src.transfer_buffer = tb;
		src.offset = (Uint32)offset;
		src.pixels_per_row = (Uint32)w;
		src.rows_per_layer = (Uint32)h;
		memset(&dst, 0, sizeof(dst));
		dst.texture = tex;
		dst.mip_level = (Uint32)level;
		dst.w = (Uint32)w;
		dst.h = (Uint32)h;
		dst.d = 1;
		SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
		offset += (size_t)w * h * 4;
	}
	SDL_EndGPUCopyPass(copyPass);
	SDL_SubmitGPUCommandBuffer(cmd);

	SDL_ReleaseGPUTransferBuffer(gpuDevice, tb);

	return tex;
}


/*
 * loadRoyaleMask
 *
 * loads crt-royale's phosphor mask LUT (the slot mask -- crt-royale's own
 * default).  mask_slot_avg_color is crt-royale's documented constant
 * (user-cgp-constants.h); maskAmplify = 1/avg_color.
 */
static void
loadRoyaleMask(void)
{
	const unsigned char *maskPng[MASK_MIP_LEVELS];
	unsigned long maskPngLen[MASK_MIP_LEVELS];

	maskPng[0] = mask_phosphor_slot_mip0_png;
	maskPngLen[0] = mask_phosphor_slot_mip0_png_len;
	maskPng[1] = mask_phosphor_slot_mip1_png;
	maskPngLen[1] = mask_phosphor_slot_mip1_png_len;
	maskPng[2] = mask_phosphor_slot_mip2_png;
	maskPngLen[2] = mask_phosphor_slot_mip2_png_len;
	maskPng[3] = mask_phosphor_slot_mip3_png;
	maskPngLen[3] = mask_phosphor_slot_mip3_png_len;
	maskPng[4] = mask_phosphor_slot_mip4_png;
	maskPngLen[4] = mask_phosphor_slot_mip4_png_len;
	royaleMaskAmplify = 255.0f / 46.0f;
	phosphorMaskTexW = 24;
	phosphorMaskTexH = 24;
	gpuPhosphorMaskTexture = loadMipmappedPNGTexture(maskPng, maskPngLen, MASK_MIP_LEVELS);
}


/*
 * sysvid_init
 *
 * initialize the video layer.
 */
void
sysvid_init(U16 width, U16 height)
{
	SDL_Surface *s;
	SDL_WindowFlags flags;
	SDL_GPUShaderCreateInfo shInfo;
	SDL_GPUTextureCreateInfo texInfo;
	SDL_GPUTransferBufferCreateInfo tbInfo;
	SDL_GPUSamplerCreateInfo sampInfo;
	SDL_GPUTextureFormat swapchainFormat;

	fb_width = width;
	fb_height = height;

	upscaleMode = (upscaleMode_t)sysarg_args_upscale;
	crtMode = (crtMode_t)sysarg_args_crt;
	aspectMode = sysarg_args_aspect ? 1 : 0;

	IFDEBUG_VIDEO(sys_printf("xrick/video: start\n"););

	/* various WM stuff */
	SDL_HideCursor();

	s = makeIconSurface();
	IFDEBUG_VIDEO(
	    {
		    U8 tpix = *(IMG_ICON->pixels);
		    sys_printf("xrick/video: icon is %dx%d\n", IMG_ICON->w, IMG_ICON->h);
		    sys_printf("xrick/video: icon transp. color is #%d (%d,%d,%d)\n", tpix,
			       IMG_ICON->colors[tpix].r,
			       IMG_ICON->colors[tpix].g,
			       IMG_ICON->colors[tpix].b);
	    });

	/* if a zoom was specified, use it -- but check it is ok */
	if (sysarg_args_zoom) {
		zoom = sysarg_args_zoom > 0 && sysarg_args_zoom <= mxzoom ? sysarg_args_zoom : mxzoom;
		wmzoom = zoom;
	}

	/* prepare for fullscreen, initialize zoom w/default values */
	flags = 0;
	if (sysarg_args_fullscreen) {
		isFullscreen = TRUE;
		flags |= SDL_WINDOW_FULLSCREEN;
		zoom = 1;
	} else {
		isFullscreen = FALSE;
		zoom = wmzoom;
	}

	/* create pixels */
	/* FIXME free pixels! */
	pixels = (U32 *)malloc(fb_width * fb_height * sizeof(U32));

	/* create window/screen */
	screen = SDL_CreateWindow("xrick", fb_width * zoom, fb_height * zoom, flags);
	if (s) {
		SDL_SetWindowIcon(screen, s);
		SDL_DestroySurface(s);
	}

	/* GPU device + swapchain */
	gpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV, false, NULL);
	if (!gpuDevice)
		sys_panic("xrick/video: could not create GPU device (%s)", SDL_GetError());
	if (!SDL_ClaimWindowForGPUDevice(gpuDevice, screen))
		sys_panic("xrick/video: could not claim window for GPU device (%s)", SDL_GetError());

	/* shaders -- one shared vertex shader (fullscreen triangle), one
	 * fragment shader per available look/pass */
	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_passthrough_vert_spv;
	shInfo.code_size = shader_passthrough_vert_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	gpuVertShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_passthrough_frag_spv;
	shInfo.code_size = shader_passthrough_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	gpuPassthroughFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_easymode_frag_spv;
	shInfo.code_size = shader_crt_easymode_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuCrtEasymodeFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_easymode_post_frag_spv;
	shInfo.code_size = shader_crt_easymode_post_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuCrtEasymodePostFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_sharp_bilinear_frag_spv;
	shInfo.code_size = shader_sharp_bilinear_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuSharpFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_lottes_frag_spv;
	shInfo.code_size = shader_crt_lottes_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuCrtLottesFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_fsr_easu_frag_spv;
	shInfo.code_size = shader_fsr_easu_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuEasuFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_fsr_rcas_frag_spv;
	shInfo.code_size = shader_fsr_rcas_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuRcasFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_linearize_frag_spv;
	shInfo.code_size = shader_crt_royale_linearize_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	gpuRoyaleLinearizeFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_vscan_frag_spv;
	shInfo.code_size = shader_crt_royale_vscan_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuRoyaleVscanFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_bloom_approx_frag_spv;
	shInfo.code_size = shader_crt_royale_bloom_approx_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	gpuRoyaleBloomApproxFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_hscan_mask_frag_spv;
	shInfo.code_size = shader_crt_royale_hscan_mask_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 2;
	shInfo.num_uniform_buffers = 1;
	gpuRoyaleHscanMaskFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_brightpass_frag_spv;
	shInfo.code_size = shader_crt_royale_brightpass_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 2;
	shInfo.num_uniform_buffers = 1;
	gpuRoyaleBrightpassFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_bloom_blur_frag_spv;
	shInfo.code_size = shader_crt_royale_bloom_blur_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuRoyaleBloomBlurFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_royale_reconstitute_frag_spv;
	shInfo.code_size = shader_crt_royale_reconstitute_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 3;
	shInfo.num_uniform_buffers = 1;
	gpuRoyaleReconstituteFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_curvature_frag_spv;
	shInfo.code_size = shader_crt_curvature_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuCurvatureFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	memset(&shInfo, 0, sizeof(shInfo));
	shInfo.code = shader_crt_final_encode_frag_spv;
	shInfo.code_size = shader_crt_final_encode_frag_spv_len;
	shInfo.entrypoint = "main";
	shInfo.format = SDL_GPU_SHADERFORMAT_SPIRV;
	shInfo.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	shInfo.num_samplers = 1;
	shInfo.num_uniform_buffers = 1;
	gpuFinalEncodeFragShader = SDL_CreateGPUShader(gpuDevice, &shInfo);

	if (!gpuVertShader || !gpuPassthroughFragShader || !gpuCrtEasymodeFragShader || !gpuSharpFragShader || !gpuCrtLottesFragShader || !gpuCrtEasymodePostFragShader || !gpuEasuFragShader || !gpuRcasFragShader || !gpuRoyaleLinearizeFragShader || !gpuRoyaleVscanFragShader || !gpuRoyaleBloomApproxFragShader || !gpuRoyaleHscanMaskFragShader || !gpuRoyaleBrightpassFragShader || !gpuRoyaleBloomBlurFragShader || !gpuRoyaleReconstituteFragShader || !gpuCurvatureFragShader || !gpuFinalEncodeFragShader)
		sys_panic("xrick/video: could not compile GPU shaders (%s)", SDL_GetError());

	/* sampled texture holding the composited frame */
	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	texInfo.width = fb_width;
	texInfo.height = fb_height;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	gpuTexture = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	/* CPU->GPU upload staging buffer */
	memset(&tbInfo, 0, sizeof(tbInfo));
	tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tbInfo.size = (Uint32)(fb_width * fb_height * sizeof(U32));
	gpuTransferBuf = SDL_CreateGPUTransferBuffer(gpuDevice, &tbInfo);

	/* nearest sampling -- pixel-perfect; the shaders that want smoothing
	 * (FSR1's EASU) do their own filtering rather than relying on this */
	memset(&sampInfo, 0, sizeof(sampInfo));
	sampInfo.min_filter = SDL_GPU_FILTER_NEAREST;
	sampInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
	sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	gpuSampler = SDL_CreateGPUSampler(gpuDevice, &sampInfo);

	/* bilinear, clamp -- crt-royale's bloom-approx resize */
	memset(&sampInfo, 0, sizeof(sampInfo));
	sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	gpuSamplerLinearClamp = SDL_CreateGPUSampler(gpuDevice, &sampInfo);

	/* trilinear + anisotropic, repeat -- crt-royale's tiled phosphor mask.
	 * Needs its own mip chain (see loadPNGTextureEx's mipmap flag, used
	 * only for this texture): the mask is a fixed 24px tile sampled at
	 * whatever the output viewport's pixel width happens to be, and that
	 * ratio is virtually never a clean integer -- without mips to let
	 * automatic LOD selection band-limit the tile to the actual on-screen
	 * texel density, the mismatch beats against the output's pixel grid
	 * as a slowly-varying moire, worse whenever the bezel-cutout
	 * viewport's specific width happens to land on an unfavorable
	 * fraction (confirmed real: reproduces with curvature's own warp
	 * math fully bypassed, absent entirely with no bezel active at all
	 * -- i.e. tied to viewport pixel width, not the curvature warp
	 * itself). */
	memset(&sampInfo, 0, sizeof(sampInfo));
	sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
	sampInfo.min_lod = 0.0f;
	sampInfo.max_lod = 16.0f;
	sampInfo.enable_anisotropy = true;
	sampInfo.max_anisotropy = 16.0f;
	gpuSamplerLinearRepeat = SDL_CreateGPUSampler(gpuDevice, &sampInfo);

	/* trilinear, clamp, full mip range -- the curvature pass samples
	 * gpuFinalIntermediate through this. Its nonlinear warp locally
	 * minifies the image by a varying amount; without mip-based
	 * filtering that aliases the phosphor mask/scanline pattern's fine
	 * repeating structure into a visible moire. Hardware picks the
	 * right mip level automatically from the warped uv's screen-space
	 * derivatives -- no manual LOD math needed on our end. */
	memset(&sampInfo, 0, sizeof(sampInfo));
	sampInfo.min_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
	sampInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_LINEAR;
	sampInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampInfo.min_lod = 0.0f;
	sampInfo.max_lod = 16.0f;
	sampInfo.enable_anisotropy = true;
	sampInfo.max_anisotropy = 16.0f;
	gpuSamplerMipmap = SDL_CreateGPUSampler(gpuDevice, &sampInfo);

	/* pipelines: no vertex buffer, 3 vertices generated in the shader */
	swapchainFormat = SDL_GetGPUSwapchainTextureFormat(gpuDevice, screen);

	/* gpuPassthroughPipeline stays swapchain-format: it's used both for
	 * the bezel background draw and the final "no curvature" blit, which
	 * write straight to the swapchain. Every filter combination's own
	 * "tail" pass now targets gpuFinalIntermediate (R8G8B8A8_UNORM)
	 * instead -- see sysvid_update()'s unified final-pass comment. */
	gpuPassthroughPipeline = createPipeline(gpuPassthroughFragShader, swapchainFormat, true);
	gpuPassthroughToIntermediatePipeline = createPipeline(gpuPassthroughFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuSharpToIntermediatePipeline = createPipeline(gpuSharpFragShader, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);
	gpuSharpToFinalPipeline = createPipeline(gpuSharpFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuCrtLottesPipeline = createPipeline(gpuCrtLottesFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuCrtEasymodePipeline = createPipeline(gpuCrtEasymodeFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuRcasToSwapchainPipeline = createPipeline(gpuRcasFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuCrtEasymodePostPipeline = createPipeline(gpuCrtEasymodePostFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);
	gpuEasuPipeline = createPipeline(gpuEasuFragShader, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);
	gpuRcasToIntermediatePipeline = createPipeline(gpuRcasFragShader, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);

	gpuRoyaleLinearizePipeline = createPipeline(gpuRoyaleLinearizeFragShader, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);
	gpuRoyaleVscanPipeline = createPipeline(gpuRoyaleVscanFragShader, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false);
	gpuRoyaleBloomApproxPipeline = createPipeline(gpuRoyaleBloomApproxFragShader, SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM, false);
	gpuRoyaleHscanMaskPipeline = createPipeline(gpuRoyaleHscanMaskFragShader, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false);
	gpuRoyaleBrightpassPipeline = createPipeline(gpuRoyaleBrightpassFragShader, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false);
	gpuRoyaleBloomBlurPipeline = createPipeline(gpuRoyaleBloomBlurFragShader, SDL_GPU_TEXTUREFORMAT_R32G32B32A32_FLOAT, false);
	gpuRoyaleReconstitutePipeline = createPipeline(gpuRoyaleReconstituteFragShader, SDL_GPU_TEXTUREFORMAT_R16G16B16A16_FLOAT, false);

	gpuCurvaturePipeline = createPipeline(gpuCurvatureFragShader, swapchainFormat, false);
	gpuFinalEncodePipeline = createPipeline(gpuFinalEncodeFragShader, swapchainFormat, false);

	if (!gpuPassthroughPipeline || !gpuPassthroughToIntermediatePipeline || !gpuCrtEasymodePipeline || !gpuSharpToIntermediatePipeline || !gpuSharpToFinalPipeline || !gpuCrtLottesPipeline || !gpuRcasToSwapchainPipeline || !gpuCrtEasymodePostPipeline || !gpuEasuPipeline || !gpuRcasToIntermediatePipeline || !gpuRoyaleLinearizePipeline || !gpuRoyaleVscanPipeline || !gpuRoyaleBloomApproxPipeline || !gpuRoyaleHscanMaskPipeline || !gpuRoyaleBrightpassPipeline || !gpuRoyaleBloomBlurPipeline || !gpuRoyaleReconstitutePipeline || !gpuCurvaturePipeline || !gpuFinalEncodePipeline)
		sys_panic("xrick/video: could not create GPU pipelines (%s)", SDL_GetError());

	/* crt-royale: phosphor mask LUT (mask type picked via -royale-mask,
	 * startup-only -- see sysarg.c) + the two fixed-size buffers that
	 * never depend on the output viewport (see the static declarations
	 * above). mask_*_avg_color values are crt-royale's own documented
	 * constants (user-cgp-constants.h); maskAmplify = 1/avg_color. */
	loadRoyaleMask();
	IFDEBUG_VIDEO(
	    if (!gpuPhosphorMaskTexture)
		sys_printf("xrick/video: could not load phosphor mask texture (%s)\n", SDL_GetError()););

	memset(&texInfo, 0, sizeof(texInfo));
	texInfo.type = SDL_GPU_TEXTURETYPE_2D;
	texInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER | SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texInfo.layer_count_or_depth = 1;
	texInfo.num_levels = 1;
	texInfo.sample_count = SDL_GPU_SAMPLECOUNT_1;
	texInfo.width = fb_width;
	texInfo.height = fb_height;
	gpuRoyaleLinearized = SDL_CreateGPUTexture(gpuDevice, &texInfo);
	texInfo.width = 320;
	texInfo.height = 240;
	gpuRoyaleBloomApprox = SDL_CreateGPUTexture(gpuDevice, &texInfo);

	if (!gpuRoyaleLinearized || !gpuRoyaleBloomApprox)
		sys_panic("xrick/video: could not create crt-royale resources (%s)", SDL_GetError());

	if (crtMode == CRT_ROYALE && !gpuPhosphorMaskTexture)
		crtMode = CRT_NONE; /* phosphor mask failed to load -- fall back quietly, like a bad bezel */

	/* bezels: static monitor photos with a "screen" cutout the game's own
	 * output is composited into (see sysvid_update). screenX0/Y0/X1/Y1 were
	 * measured once from the source art (Duimon-Mega-Bezel's Commodore
	 * 1084S graphic) as the bounding box of its placeholder "screen" fill
	 * color -- not re-derived at runtime. */
	memset(bezels, 0, sizeof(bezels));
	bezels[BEZEL_COMMODORE_1084S].texture = loadPNGTexture(
	    bezel_commodore_1084s_png, bezel_commodore_1084s_png_len,
	    &bezels[BEZEL_COMMODORE_1084S].texW, &bezels[BEZEL_COMMODORE_1084S].texH);
	bezels[BEZEL_COMMODORE_1084S].screenX0 = 0.216326f;
	bezels[BEZEL_COMMODORE_1084S].screenY0 = 0.075000f;
	bezels[BEZEL_COMMODORE_1084S].screenX1 = 0.783673f;
	bezels[BEZEL_COMMODORE_1084S].screenY1 = 0.848214f;
	/* Atari SC1224: the pack's graphic for it is just the monitor body -- the
	 * screen and frame are drawn by the Mega Bezel shader, not baked in, so
	 * there's no placeholder to measure. These come from the pack's own
	 * preset for the ST + this monitor (res/scale/Atari_ST/monitor.params):
	 * 4:3 screen, 65.17% of the image height, shifted up by 67 units,
	 * centered horizontally, on the 3840x2160 image. */
	bezels[BEZEL_ATARI_SC1224].texture = loadPNGTexture(
	    bezel_atari_sc1224_png, bezel_atari_sc1224_png_len,
	    &bezels[BEZEL_ATARI_SC1224].texW, &bezels[BEZEL_ATARI_SC1224].texH);
	bezels[BEZEL_ATARI_SC1224].screenX0 = 0.255469f;
	bezels[BEZEL_ATARI_SC1224].screenY0 = 0.106944f;
	bezels[BEZEL_ATARI_SC1224].screenX1 = 0.744531f;
	bezels[BEZEL_ATARI_SC1224].screenY1 = 0.758796f;

	IFDEBUG_VIDEO(
	    if (!bezels[BEZEL_COMMODORE_1084S].texture)
		sys_printf("xrick/video: could not load bezel 'commodore_1084s' (%s)\n", SDL_GetError()););

	{
		Uint32 bw, bh;
		gpuBlackTexture = loadPNGTexture(blackPng, sizeof(blackPng), &bw, &bh);
	}

	bezelMode = (bezelMode_t)sysarg_args_bezel;
	if (bezelMode != BEZEL_NONE && bezels[bezelMode].texture == NULL)
		bezelMode = BEZEL_NONE; /* requested bezel failed to load -- fall back quietly */

	IFDEBUG_VIDEO(sys_printf("xrick/video: ready\n"););
}


/*
 * sysvid_shutdown
 *
 * shutdown the video layer.
 */
void
sysvid_shutdown(void)
{
	int i;

	free(pixels);
	pixels = NULL;

	if (!gpuDevice)
		return;

	/*
	 * Wait for any in-flight GPU work to finish before tearing anything
	 * down -- releasing resources the GPU might still be using is asking
	 * for trouble.
	 */
	SDL_WaitForGPUIdle(gpuDevice);

	/* pipelines first (they reference shaders/formats) */
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuPassthroughPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuPassthroughToIntermediatePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuSharpToIntermediatePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuSharpToFinalPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuCrtLottesPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuCrtEasymodePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuCrtEasymodePostPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuEasuPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRcasToSwapchainPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRcasToIntermediatePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuCurvaturePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuFinalEncodePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleLinearizePipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleVscanPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleBloomApproxPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleHscanMaskPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleBrightpassPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleBloomBlurPipeline);
	SDL_ReleaseGPUGraphicsPipeline(gpuDevice, gpuRoyaleReconstitutePipeline);

	/* shaders */
	SDL_ReleaseGPUShader(gpuDevice, gpuVertShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuPassthroughFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuSharpFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuCrtLottesFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuCrtEasymodeFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuCrtEasymodePostFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuEasuFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRcasFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuCurvatureFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuFinalEncodeFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleLinearizeFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleVscanFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleBloomApproxFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleHscanMaskFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleBrightpassFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleBloomBlurFragShader);
	SDL_ReleaseGPUShader(gpuDevice, gpuRoyaleReconstituteFragShader);

	/* textures */
	SDL_ReleaseGPUTexture(gpuDevice, gpuTexture);
	SDL_ReleaseGPUTexture(gpuDevice, gpuIntermediate1);
	SDL_ReleaseGPUTexture(gpuDevice, gpuIntermediate2);
	SDL_ReleaseGPUTexture(gpuDevice, gpuFinalIntermediate);
	SDL_ReleaseGPUTexture(gpuDevice, gpuPhosphorMaskTexture);
	if (gpuBlackTexture) SDL_ReleaseGPUTexture(gpuDevice, gpuBlackTexture);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleLinearized);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBloomApprox);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleVscan);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleMaskedScanlines);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBrightpass);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBloomV);
	SDL_ReleaseGPUTexture(gpuDevice, gpuRoyaleBloomH);
	for (i = 0; i < BEZEL_COUNT; i++)
		SDL_ReleaseGPUTexture(gpuDevice, bezels[i].texture);

	/* samplers + transfer buffer */
	SDL_ReleaseGPUSampler(gpuDevice, gpuSampler);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerLinearClamp);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerLinearRepeat);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerMipmap);
	SDL_ReleaseGPUTransferBuffer(gpuDevice, gpuTransferBuf);

	/* window + device last */
	SDL_ReleaseWindowFromGPUDevice(gpuDevice, screen);
	SDL_DestroyGPUDevice(gpuDevice);
	gpuDevice = NULL;
}


/*
 * sysvid_showOSD
 *
 * arms the on-screen message shown for OSD_DURATION_MS after this call.
 */
static void
sysvid_showOSD(const char *msg)
{
	strncpy(osdMessage, msg, OSD_MSG_MAX - 1);
	osdMessage[OSD_MSG_MAX - 1] = '\0';
	osdExpireAt = sys_gettime() + OSD_DURATION_MS;
}


/*
 * sysvid_osdActive
 *
 * true while the OSD's timer is still counting down. sysevt_wait() checks
 * this to avoid blocking indefinitely on a static screen (menu, pause,
 * Hall of Fame) -- without it, the game loop simply wouldn't tick again
 * until the next real input event, so the OSD would never get a chance
 * to notice its timer expired and clear itself.
 */
U8
sysvid_osdActive(void)
{
	return sys_gettime() < osdExpireAt;
}


static const U8 *
osdFindGlyph(char c)
{
	U32 i;
	for (i = 0; i < OSD_FONT_COUNT; i++)
		if (osdFont[i].c == c) return osdFont[i].rows;
	return NULL; /* unknown char -- just skip it */
}


/*
 * drawOSD
 *
 * stamps the current OSD message into the composited RGBA frame
 * (top-left corner), with a translucent dark backing box -- blended by
 * hand against whatever's already in the buffer, since the GPU passes
 * downstream don't do alpha blending -- for legibility without fully
 * hiding the gameplay underneath. Text itself stays fully opaque.
 */
static void
drawOSD(void)
{
	int len = (int)strlen(osdMessage);
	int textW = len * 6 * OSD_SCALE - OSD_SCALE; /* 5px glyph + 1px gap, scaled */
	int textH = 7 * OSD_SCALE;
	int x0 = OSD_MARGIN;
	int y0 = OSD_MARGIN;
	int i, gx, gy;

	for (gy = -OSD_MARGIN / 2; gy < textH + OSD_MARGIN / 2; gy++) {
		int py = y0 + gy;
		if (py < 0 || py >= (int)fb_height) continue;
		for (gx = -OSD_MARGIN / 2; gx < textW + OSD_MARGIN / 2; gx++) {
			int px = x0 + gx;
			U8 *p;
			if (px < 0 || px >= (int)fb_width) continue;
			p = (U8 *)pixels + (py * fb_width + px) * 4;
			/* blend toward black at OSD_BG_ALPHA/255 opacity */
			p[0] = (U8)((int)p[0] * (255 - OSD_BG_ALPHA) / 255);
			p[1] = (U8)((int)p[1] * (255 - OSD_BG_ALPHA) / 255);
			p[2] = (U8)((int)p[2] * (255 - OSD_BG_ALPHA) / 255);
			p[3] = 255;
		}
	}

	for (i = 0; i < len; i++) {
		const U8 *glyphRows = osdFindGlyph(osdMessage[i]);
		if (!glyphRows) continue;
		for (gy = 0; gy < 7; gy++) {
			U8 row = glyphRows[gy];
			for (gx = 0; gx < 5; gx++) {
				int sx, sy;
				if (!(row & (0x10 >> gx))) continue;
				for (sy = 0; sy < OSD_SCALE; sy++) {
					for (sx = 0; sx < OSD_SCALE; sx++) {
						int px = x0 + (i * 6 + gx) * OSD_SCALE + sx;
						int py = y0 + gy * OSD_SCALE + sy;
						U8 *p;
						if (px < 0 || px >= (int)fb_width || py < 0 || py >= (int)fb_height) continue;
						p = (U8 *)pixels + (py * fb_width + px) * 4;
						p[0] = 255;
						p[1] = 255;
						p[2] = 255;
						p[3] = 255;
					}
				}
			}
		}
	}
}


/*
 * blitRectToPixels
 *
 * composites one dirty rect of the game's 8bit palettized fb into the
 * RGBA `pixels` buffer. Factored out of sysvid_update() so the OSD-area
 * refresh below can reuse it on a rect the caller didn't necessarily ask
 * to redraw.
 */
static void
blitRectToPixels(rect_t *rect)
{
	U16 o = rect->x + rect->y * fb_width;
	U8 *src0 = ((U8 *)&fb) + o;
	U8 *dst0 = (U8 *)pixels + o * 4;
	for (int y = rect->y; y < rect->y + rect->height; y++) {
		U8 *srcx = src0;
		U8 *dstx = dst0;

		for (int x = rect->x; x < rect->x + rect->width; x++) {
			/* R8G8B8A8, unlike the old SDL_Renderer ARGB8888 path */
			*dstx = pald[*srcx].r;
			dstx++;
			*dstx = pald[*srcx].g;
			dstx++;
			*dstx = pald[*srcx].b;
			dstx++;
			*dstx = pald[*srcx].a;
			dstx++;
			srcx++;
		}

		src0 += fb_width;
		dst0 += fb_width * 4;
	}
}


/*
 * fitAspectInside
 *
 * places the picture inside a bezel's screen opening at the chosen aspect
 * ratio (4:3 or square pixels), centered, leaving bars where the opening is
 * a different shape. crt-royale needs the picture height to be a whole
 * multiple of the source's 200 lines (see computeBezelViewports), so it gets
 * snapped down to one.
 */
static void
fitAspectInside(const SDL_GPUViewport *opening, SDL_GPUViewport *out)
{
	float aspect = (aspectMode == 0) ? 4.0f / 3.0f : (float)fb_width / (float)fb_height;
	float w = opening->w, h = opening->h;

	if (w / h > aspect)
		w = h * aspect;
	else
		h = w / aspect;

	if (crtMode == CRT_ROYALE) {
		int n = (int)(h / (float)fb_height + 0.01f);
		if (n >= 1) {
			h = n * (float)fb_height;
			w = h * aspect;
		}
	}

	w = (float)(int)(w + 0.5f);
	h = (float)(int)(h + 0.5f);
	out->x = (float)(int)(opening->x + (opening->w - w) / 2.0f + 0.5f);
	out->y = (float)(int)(opening->y + (opening->h - h) / 2.0f + 0.5f);
	out->w = w;
	out->h = h;
	out->min_depth = 0.0f;
	out->max_depth = 1.0f;
}


/*
 * sysvid_update
 *
 * display the 8bit palettized frame buffer onto the screen. zoom, filter, whatever.
 */
void
sysvid_update(rect_t *rects)
{
	static U8 osdWasActive = FALSE;
	static U8 menuWasActive = FALSE;
	rect_t *rect;
	U8 osdActiveNow, menuNow;
	void *mapped;
	SDL_GPUCommandBuffer *cmd;
	SDL_GPUCopyPass *copyPass;
	SDL_GPUTextureTransferInfo src;
	SDL_GPUTextureRegion dst;
	SDL_GPUTexture *swapTex;
	Uint32 swW, swH;

	osdActiveNow = sysvid_osdActive();
	menuNow = menu_active();

	/* Normally bail out if there's nothing to redraw -- but not while the
	 * OSD needs attention: it's alpha-blended directly into `pixels`
	 * (which persists across calls), so we still need one more pass
	 * through, even with no other dirty rects, whenever it's freshly
	 * active (to draw it) or was active last call but just expired (to
	 * clean up after it -- see the osdRect refresh below). */
	if (rects == NULL && !osdActiveNow && !osdWasActive && !menuNow && !menuWasActive)
		return;

	rect = rects;
	while (rect) {
		blitRectToPixels(rect);
		rect = rect->next;
	}

	if (osdActiveNow || osdWasActive) {
		/* Always restore real game content under the OSD's fixed area
		 * first, whether or not it was already part of the caller's
		 * rects -- otherwise repeated draws while active would
		 * progressively darken the backing box further each time
		 * (it's a blend, not an overwrite), and on a static screen
		 * with no other dirty rects, the OSD would never get cleaned
		 * up after expiring at all. */
		rect_t osdRect;
		osdRect.x = 0;
		osdRect.y = 0;
		osdRect.width = OSD_MARGIN * 2 + OSD_MSG_MAX * 6 * OSD_SCALE;
		osdRect.height = OSD_MARGIN * 2 + 7 * OSD_SCALE;
		if (osdRect.width > fb_width) osdRect.width = fb_width;
		if (osdRect.height > fb_height) osdRect.height = fb_height;
		osdRect.next = NULL;
		blitRectToPixels(&osdRect);
	}

	if (menuNow || menuWasActive) {
		/* the settings menu blends over the whole frame, so start each
		 * pass (and the one right after it closes) from clean game content */
		rect_t full;
		full.x = 0;
		full.y = 0;
		full.width = fb_width;
		full.height = fb_height;
		full.next = NULL;
		blitRectToPixels(&full);
	}

	if (osdActiveNow)
		drawOSD();

	if (menuNow)
		menu_draw();

	osdWasActive = osdActiveNow;
	menuWasActive = menuNow;

	mapped = SDL_MapGPUTransferBuffer(gpuDevice, gpuTransferBuf, false);
	memcpy(mapped, pixels, (size_t)fb_width * fb_height * sizeof(U32));
	SDL_UnmapGPUTransferBuffer(gpuDevice, gpuTransferBuf);

	cmd = SDL_AcquireGPUCommandBuffer(gpuDevice);

	memset(&src, 0, sizeof(src));
	src.transfer_buffer = gpuTransferBuf;
	src.pixels_per_row = fb_width;
	src.rows_per_layer = fb_height;

	memset(&dst, 0, sizeof(dst));
	dst.texture = gpuTexture;
	dst.w = fb_width;
	dst.h = fb_height;
	dst.d = 1;

	copyPass = SDL_BeginGPUCopyPass(cmd);
	SDL_UploadToGPUTexture(copyPass, &src, &dst, false);
	SDL_EndGPUCopyPass(copyPass);

	if (SDL_WaitAndAcquireGPUSwapchainTexture(cmd, screen, &swapTex, &swW, &swH) && swapTex) {
		SDL_GPUViewport vp;
		float outputSize[2];
		SDL_GPULoadOp finalLoadOp;
		const bezelInfo_t *bz = (bezelMode != BEZEL_NONE) ? &bezels[bezelMode] : NULL;

		if (bz) {
			/* bezel active: fit the bezel image itself into the window,
			 * draw it first (this is the only pass that clears the
			 * swapchain), then the game's own output -- upscaled/CRT'd
			 * exactly as without a bezel -- gets composited into the
			 * "screen" cutout viewport derived from it. That composite
			 * pass (and only that one) must LOAD rather than CLEAR/
			 * DONT_CARE, since either of those would wipe the bezel
			 * artwork just drawn outside the cutout. */
			SDL_GPUViewport outerVp, opening;

			computeBezelViewports(swW, swH, bz, &outerVp, &opening);

			runPass(cmd, gpuPassthroughPipeline, bz->texture, swapTex,
				outerVp.x, outerVp.y, outerVp.w, outerVp.h, SDL_GPU_LOADOP_CLEAR, NULL, 0);

			/* black behind the picture, then the picture itself at the
			 * chosen aspect ratio inside the opening */
			if (gpuBlackTexture)
				runPass(cmd, gpuPassthroughPipeline, gpuBlackTexture, swapTex,
					opening.x, opening.y, opening.w, opening.h, SDL_GPU_LOADOP_LOAD, NULL, 0);
			fitAspectInside(&opening, &vp);

			finalLoadOp = SDL_GPU_LOADOP_LOAD;
		} else {
			computeLetterboxViewport(swW, swH, &vp);
			finalLoadOp = SDL_GPU_LOADOP_CLEAR;
		}

		outputSize[0] = vp.w;
		outputSize[1] = vp.h;

		/* Every mode below now renders its "flat" output into
		 * gpuFinalIntermediate (R8G8B8A8_UNORM, sized to the viewport)
		 * rather than straight to the swapchain -- unified so a single
		 * final pass afterward can either warp it with screen curvature
		 * (bezel active -- a flat monitor doesn't curve, so this only
		 * makes sense with a bezel around it) or just copy it through
		 * unchanged (no bezel), without needing two format-matched
		 * pipeline variants of every filter combination. */
		ensureFinalIntermediate((Uint32)vp.w, (Uint32)vp.h);

		if (crtMode == CRT_ROYALE) {
			/* crt-royale is mutually exclusive with the upscale axis --
			 * its own vertical/horizontal scanline passes already
			 * resample to the full viewport resolution, the same job
			 * FSR1 would otherwise do (see the pipeline's declaration
			 * comment for why). See xrick/src/shaders/crt-royale-*.frag
			 * for what each pass below actually does. */
			SDL_GPUTexture *multiTex[3];
			SDL_GPUSampler *multiSamp[3];
			float vscanUniform[2];
			float hscanUniform[2];
			float blurUniformV[2];
			float blurUniformH[2];

			ensureRoyaleIntermediates((Uint32)vp.w, (Uint32)vp.h);

			runPass(cmd, gpuRoyaleLinearizePipeline, gpuTexture, gpuRoyaleLinearized,
				0.0f, 0.0f, (float)fb_width, (float)fb_height, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);

			vscanUniform[0] = (float)fb_width;
			vscanUniform[1] = vp.h;
			runPass(cmd, gpuRoyaleVscanPipeline, gpuRoyaleLinearized, gpuRoyaleVscan,
				0.0f, 0.0f, (float)fb_width, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				vscanUniform, sizeof(vscanUniform));

			multiTex[0] = gpuRoyaleLinearized;
			multiSamp[0] = gpuSamplerLinearClamp;
			runPassMulti(cmd, gpuRoyaleBloomApproxPipeline, multiTex, multiSamp, 1, gpuRoyaleBloomApprox,
				     0.0f, 0.0f, 320.0f, 240.0f, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);

			hscanUniform[0] = vp.w;
			hscanUniform[1] = vp.h;
			multiTex[0] = gpuRoyaleVscan;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuPhosphorMaskTexture;
			multiSamp[1] = gpuSamplerLinearRepeat;
			runPassMulti(cmd, gpuRoyaleHscanMaskPipeline, multiTex, multiSamp, 2, gpuRoyaleMaskedScanlines,
				     0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				     hscanUniform, sizeof(hscanUniform));

			multiTex[0] = gpuRoyaleMaskedScanlines;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuRoyaleBloomApprox;
			multiSamp[1] = gpuSamplerLinearClamp;
			runPassMulti(cmd, gpuRoyaleBrightpassPipeline, multiTex, multiSamp, 2, gpuRoyaleBrightpass,
				     0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				     &royaleMaskAmplify, sizeof(royaleMaskAmplify));

			blurUniformV[0] = 0.0f;
			blurUniformV[1] = 1.0f / vp.h;
			runPass(cmd, gpuRoyaleBloomBlurPipeline, gpuRoyaleBrightpass, gpuRoyaleBloomV,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				blurUniformV, sizeof(blurUniformV));

			blurUniformH[0] = 1.0f / vp.w;
			blurUniformH[1] = 0.0f;
			runPass(cmd, gpuRoyaleBloomBlurPipeline, gpuRoyaleBloomV, gpuRoyaleBloomH,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				blurUniformH, sizeof(blurUniformH));

			multiTex[0] = gpuRoyaleBloomH;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuRoyaleMaskedScanlines;
			multiSamp[1] = gpuSampler;
			multiTex[2] = gpuRoyaleBrightpass;
			multiSamp[2] = gpuSampler;
			runPassMulti(cmd, gpuRoyaleReconstitutePipeline, multiTex, multiSamp, 3, gpuFinalIntermediate,
				     0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE,
				     &royaleMaskAmplify, sizeof(royaleMaskAmplify));
		} else if (crtMode == CRT_LOTTES) {
			/* like royale, crt-lottes does its own resample to the
			 * viewport, so the upscale axis doesn't apply either */
			runPass(cmd, gpuCrtLottesPipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
		} else if (upscaleMode == UPSCALE_SHARP) {
			SDL_GPUTexture *sharpTex[1] = {gpuTexture};
			SDL_GPUSampler *sharpSamp[1] = {gpuSamplerLinearClamp};

			if (crtMode == CRT_EASYMODE) {
				ensureIntermediates((Uint32)vp.w, (Uint32)vp.h);
				runPassMulti(cmd, gpuSharpToIntermediatePipeline, sharpTex, sharpSamp, 1, gpuIntermediate1,
					     0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
				runPass(cmd, gpuCrtEasymodePostPipeline, gpuIntermediate1, gpuFinalIntermediate,
					0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			} else {
				runPassMulti(cmd, gpuSharpToFinalPipeline, sharpTex, sharpSamp, 1, gpuFinalIntermediate,
					     0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			}
		} else if (upscaleMode == UPSCALE_FSR1) {
			float rcasUniform[3];

			ensureIntermediates((Uint32)vp.w, (Uint32)vp.h);

			rcasUniform[0] = vp.w;
			rcasUniform[1] = vp.h;
			rcasUniform[2] = (float)(fsrFrameCount++);

			/* pass 1: EASU, composited frame -> intermediate1 (full-size, no letterbox) */
			runPass(cmd, gpuEasuPipeline, gpuTexture, gpuIntermediate1,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));

			if (crtMode == CRT_EASYMODE) {
				/* pass 2: RCAS -> intermediate2; pass 3: CRT post -> gpuFinalIntermediate */
				runPass(cmd, gpuRcasToIntermediatePipeline, gpuIntermediate1, gpuIntermediate2,
					0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, rcasUniform, sizeof(rcasUniform));
				runPass(cmd, gpuCrtEasymodePostPipeline, gpuIntermediate2, gpuFinalIntermediate,
					0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			} else {
				/* pass 2: RCAS -> gpuFinalIntermediate */
				runPass(cmd, gpuRcasToSwapchainPipeline, gpuIntermediate1, gpuFinalIntermediate,
					0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, rcasUniform, sizeof(rcasUniform));
			}
		} else if (crtMode == CRT_EASYMODE) {
			runPass(cmd, gpuCrtEasymodePipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
		} else {
			runPass(cmd, gpuPassthroughToIntermediatePipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, vp.w, vp.h, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);
		}

		/* final pass: warp with curvature into the bezel's screen cutout,
		 * or just copy straight to the swapchain if there's no bezel.
		 * Either way, this is also where crt-royale's deliberately-
		 * deferred clamp/gamma-encode finally happens (see
		 * crt-royale-reconstitute.frag) -- every other mode already
		 * wrote clamped, display-ready color into gpuFinalIntermediate,
		 * so applyGamma is a no-op for them. */
		{
			float applyGamma = (crtMode == CRT_ROYALE) ? 1.0f : 0.0f;

			if (bz) {
				SDL_GPUTexture *curvTex[1] = {gpuFinalIntermediate};
				SDL_GPUSampler *curvSamp[1] = {gpuSamplerMipmap};

				/* must run outside any render pass; build the mip chain
				 * gpuSamplerMipmap relies on to avoid aliasing the mask/
				 * scanline pattern under curvature's nonlinear warp */
				SDL_GenerateMipmapsForGPUTexture(cmd, gpuFinalIntermediate);

				runPassMulti(cmd, gpuCurvaturePipeline, curvTex, curvSamp, 1, swapTex,
					     vp.x, vp.y, vp.w, vp.h, finalLoadOp, &applyGamma, sizeof(applyGamma));
			} else
				runPass(cmd, gpuFinalEncodePipeline, gpuFinalIntermediate, swapTex,
					vp.x, vp.y, vp.w, vp.h, finalLoadOp, &applyGamma, sizeof(applyGamma));
		}
	}

	SDL_SubmitGPUCommandBuffer(cmd);
}


/*
 * sysvid_zoom
 *
 * increases or decreases zoom by <z>, if possible.
 */
void
sysvid_zoom(S8 z)
{
	if (isFullscreen) return;

	if ((z < 0 && zoom + z > 0) || (z > 0 && zoom + z <= mxzoom)) {
		zoom += z;
		wmzoom = zoom;

		IFDEBUG_VIDEO(
		    sys_printf("xrick/video: zoom=%d window=%dx%d\n", zoom, fb_width * zoom, fb_height * zoom););

		SDL_SetWindowSize(screen, fb_width * zoom, fb_height * zoom);

		sysvid_setDisplayPalette();
		sysvid_update(&SCREENRECT); /* repaint all */ /* FIXME */
	}
}


/*
 * sysvid_toggleFullscreen
 *
 * toggles fullscreen.
 */
void
sysvid_toggleFullscreen(void)
{
	isFullscreen = !isFullscreen;
	SDL_SetWindowFullscreen(screen, isFullscreen);

	zoom = isFullscreen ? 1 : wmzoom;

	sysvid_setDisplayPalette();
	sysvid_update(&SCREENRECT); /* repaint all */ /* FIXME */
}


/*
 * sysvid_cycleUpscale
 *
 * cycles through the available upscale filters (F10).
 */
void
sysvid_cycleUpscale(void)
{
	upscaleMode = (upscaleMode + 1) % UPSCALE_COUNT;

	IFDEBUG_VIDEO(
	    sys_printf("xrick/video: upscale=%d\n", upscaleMode););

	sysvid_showOSD(upscaleMode == UPSCALE_NONE ? "UPSCALING: NONE" : upscaleMode == UPSCALE_FSR1 ? "UPSCALING: FSR1"
												     : "UPSCALING: SHARP");
	sysvid_update(&SCREENRECT); /* repaint with the new setting */
}


/*
 * sysvid_cycleCrt
 *
 * cycles through the available CRT effects (F11).
 */
void
sysvid_cycleCrt(void)
{
	do {
		crtMode = (crtMode + 1) % CRT_COUNT;
	} while (crtMode == CRT_ROYALE && gpuPhosphorMaskTexture == NULL);

	IFDEBUG_VIDEO(
	    sys_printf("xrick/video: crt=%d\n", crtMode););

	sysvid_showOSD(crtMode == CRT_NONE ? "SHADER: NONE" : crtMode == CRT_EASYMODE ? "SHADER: EASYMODE"
							  : crtMode == CRT_ROYALE     ? "SHADER: ROYALE"
										      : "SHADER: LOTTES");
	sysvid_update(&SCREENRECT); /* repaint with the new setting */
}


/*
 * sysvid_cycleBezel
 *
 * cycles through the available bezels (F12). Skips any entry whose
 * texture failed to load (see sysvid_init/loadPNGTexture) so a bad
 * asset just isn't offered rather than getting stuck showing nothing.
 */
void
sysvid_cycleBezel(void)
{
	bezelMode_t next = bezelMode;

	do {
		next = (next + 1) % BEZEL_COUNT;
	} while (next != BEZEL_NONE && bezels[next].texture == NULL);
	bezelMode = next;

	IFDEBUG_VIDEO(
	    sys_printf("xrick/video: bezel=%d\n", bezelMode););

	sysvid_showOSD(bezelMode == BEZEL_NONE ? "BEZEL: NONE" : bezelMode == BEZEL_COMMODORE_1084S ? "BEZEL: 1084S"
												    : "BEZEL: SC1224");
	sysvid_update(&SCREENRECT); /* repaint with the new setting */
}


/*
 * sysvid_setGamma
 *
 * sets a "gamma" indication ranging from 0 (dark) to 255 (normal).
 */
void
sysvid_setGamma(U8 g)
{
	// FIXME changing the GAMMA without changing the PALETTE just CANNOT WORK if GAMMA is not HARDWARE?
	gamma = g;
	sysvid_setDisplayPalette();
}

/*
 * Overlay drawing, for the settings menu: alpha-blended rects and text
 * stamped straight into the composited frame (menu_draw() is called from
 * sysvid_update() after the game frame is in place).
 */
void
sysvid_overlayRect(int x, int y, int w, int h, U8 r, U8 g, U8 b, U8 a)
{
	int px, py;

	for (py = y; py < y + h; py++) {
		if (py < 0 || py >= (int)fb_height) continue;
		for (px = x; px < x + w; px++) {
			U8 *p;
			if (px < 0 || px >= (int)fb_width) continue;
			p = (U8 *)pixels + (py * fb_width + px) * 4;
			p[0] = (U8)(((int)p[0] * (255 - a) + (int)r * a) / 255);
			p[1] = (U8)(((int)p[1] * (255 - a) + (int)g * a) / 255);
			p[2] = (U8)(((int)p[2] * (255 - a) + (int)b * a) / 255);
			p[3] = 255;
		}
	}
}


int
sysvid_overlayTextWidth(const char *s)
{
	int len = (int)strlen(s);
	return len ? len * 6 - 1 : 0;
}


void
sysvid_overlayText(int x, int y, const char *s, U8 r, U8 g, U8 b)
{
	int i, gx, gy;

	for (i = 0; s[i]; i++) {
		char c = s[i];
		const U8 *rows;
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		rows = osdFindGlyph(c);
		if (!rows) continue;
		for (gy = 0; gy < 7; gy++) {
			for (gx = 0; gx < 5; gx++) {
				int px = x + i * 6 + gx, py = y + gy;
				U8 *p;
				if (!(rows[gy] & (0x10 >> gx))) continue;
				if (px < 0 || px >= (int)fb_width || py < 0 || py >= (int)fb_height) continue;
				p = (U8 *)pixels + (py * fb_width + px) * 4;
				p[0] = r;
				p[1] = g;
				p[2] = b;
				p[3] = 255;
			}
		}
	}
}


/*
 * Getters/setters for the settings menu. Unlike the F-key cycle functions
 * above, these don't pop up the OSD message.
 */
U8
sysvid_isFullscreen(void)
{
	return isFullscreen;
}

U8
sysvid_getWindowZoom(void)
{
	return wmzoom;
}

int
sysvid_getUpscale(void)
{
	return upscaleMode;
}

void
sysvid_setUpscale(int v)
{
	if (v >= 0 && v < UPSCALE_COUNT) upscaleMode = (upscaleMode_t)v;
	sysvid_update(&SCREENRECT);
}

int
sysvid_getCrt(void)
{
	return crtMode;
}

/* silently ignores crt-royale when its phosphor mask failed to load */
void
sysvid_setCrt(int v)
{
	if (v >= 0 && v < CRT_COUNT && !(v == CRT_ROYALE && gpuPhosphorMaskTexture == NULL))
		crtMode = (crtMode_t)v;
	sysvid_update(&SCREENRECT);
}

int
sysvid_getBezel(void)
{
	return bezelMode;
}

/* silently ignores a bezel whose texture failed to load */
void
sysvid_setBezel(int v)
{
	if (v >= 0 && v < BEZEL_COUNT && (v == BEZEL_NONE || bezels[v].texture != NULL))
		bezelMode = (bezelMode_t)v;
	sysvid_update(&SCREENRECT);
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
	sysvid_update(&SCREENRECT);
}

/* eof */

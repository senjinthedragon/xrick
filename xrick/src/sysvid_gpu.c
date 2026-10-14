/*
 * xrick/src/sysvid_gpu.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * SDL_GPU renderer, implementing the same sysvid_gl_init/present/
 * showImage/shutdown interface sysvid_gl.h declares and sysvid.c calls --
 * a drop-in alternative to sysvid_gl.c's OpenGL/WebGL chain for native
 * (Linux/Windows) builds, selected by which of the two files the
 * Makefile/project compiles (never both: see build.sh's CSRC exclusion
 * and xrick.vcxproj, which still lists sysvid_gl.c). The web build keeps
 * using sysvid_gl.c -- SDL_GPU has no WebGPU backend yet.
 *
 * sysvid.c still owns the 8bit-to-RGBA palette compositing and hands us
 * the finished frame as a plain RGBA8 buffer; this file owns everything
 * from there on: upload to the GPU, the upscale/CRT shader chain, bezel
 * compositing with screen curvature, and the overlay-drawing API the
 * settings menu (menu.c) draws itself with, operating on its own copy
 * of that buffer.
 *
 * See xrick/src/shaders/gpu/ for the actual shaders.
 */

#include <stdbool.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

#include <SDL3/SDL.h>

#include "system.h"
#include "sysarg.h"
#include "debug.h"
#include "sysvid.h"
#include "sysvid_gl.h"
#include "sysvid_gpu.h"
#include "menu.h"
#include "shaders_embedded.h"
#include "shaders_embedded_msl.h"
#include "bezels_embedded.h"
#include "masks_embedded.h"
#include "thirdparty/stb_image.h"

static U32 *pixels; /* composited RGBA frame, our own copy -- see sysvid_gl_present */
static SDL_Window *screen;
static U16 fb_width, fb_height;

/* upscaleMode_t/crtMode_t/bezelMode_t now declared in sysvid_gpu.h, shared with sysvid_gl.c */
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

/*
 * OSD: brief "Shader: X" / "Upscaling: X" confirmation shown for a few
 * seconds after F10/F11. Stamped directly onto the composited RGBA frame
 * (not into the game's own 8bit fb) so it can't collide with or get stuck
 * in game state -- every sysvid_update() call re-draws it from scratch for
 * as long as its timer is running, and simply stops once expired.
 */
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
 * fits the bezel image into winW x winH as large as it goes without
 * stretching it (so full height on a wide window, full width on a narrow
 * one) and derives the "screen" opening's viewport from the fractions
 * measured for that bezel. The opening is where the game's picture goes;
 * it's drawn at an integer-scaled size first and then resampled to fit
 * (see chainSize), so the bezel doesn't need to be a particular size.
 */
static void
computeBezelViewports(Uint32 winW, Uint32 winH, const bezelInfo_t *bz,
		      SDL_GPUViewport *outerVp, SDL_GPUViewport *innerVp)
{
	computeLetterboxViewportFor(bz->texW, bz->texH, winW, winH, outerVp);

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
 * sysvid_gpu_createShader
 *
 * picks MSL over SPIR-V when the device actually backs onto Metal and an MSL
 * version of this shader was embedded (non-NULL only on macOS -- see
 * shaders_embedded_msl.h), else falls back to the SPIR-V everyone else uses.
 * Metal's entrypoint is always "main0": SPIRV-Cross renames the function
 * (Metal reserves the name "main"), and SDL_shadercross doesn't rename it
 * back.
 */
static SDL_GPUShader *
sysvid_gpu_createShader(SDL_GPUDevice *device, SDL_GPUShaderFormat supportedFormats,
    const unsigned char *spirv, unsigned long spirvLen,
    const unsigned char *msl, unsigned long mslLen,
    SDL_GPUShaderStage stage, Uint32 numSamplers, Uint32 numUniformBuffers)
{
	SDL_GPUShaderCreateInfo info;

	memset(&info, 0, sizeof(info));
	info.stage = stage;
	info.num_samplers = numSamplers;
	info.num_uniform_buffers = numUniformBuffers;

	if ((supportedFormats & SDL_GPU_SHADERFORMAT_MSL) && msl != NULL) {
		info.code = msl;
		info.code_size = mslLen;
		info.entrypoint = "main0";
		info.format = SDL_GPU_SHADERFORMAT_MSL;
	} else {
		info.code = spirv;
		info.code_size = spirvLen;
		info.entrypoint = "main";
		info.format = SDL_GPU_SHADERFORMAT_SPIRV;
	}

	return SDL_CreateGPUShader(device, &info);
}

/*
 * sysvid_gl_init
 *
 * creates the window and GPU device, and builds the whole shader/pipeline
 * chain. Returns NULL (having released anything it already created) on
 * failure, per sysvid_gl.h's contract -- sysvid.c then falls back to its
 * plain SDL_Renderer path, so a machine with no Vulkan/D3D12/Metal support
 * doesn't just crash.
 */
SDL_Window *
sysvid_gl_init(const char *title, int win_w, int win_h, SDL_WindowFlags flags, int fb_w, int fb_h)
{
	SDL_GPUTextureCreateInfo texInfo;
	SDL_GPUTransferBufferCreateInfo tbInfo;
	SDL_GPUSamplerCreateInfo sampInfo;
	SDL_GPUTextureFormat swapchainFormat;

	fb_width = (U16)fb_w;
	fb_height = (U16)fb_h;

	upscaleMode = (upscaleMode_t)sysarg_args_upscale;
	crtMode = (crtMode_t)sysarg_args_crt;
	aspectMode = sysarg_args_aspect ? 1 : 0;

	pixels = (U32 *)malloc((size_t)fb_width * fb_height * sizeof(U32));
	memset(pixels, 0, (size_t)fb_width * fb_height * sizeof(U32));
	sysvid_overlay_init(pixels, fb_width, fb_height);

	screen = SDL_CreateWindow(title, win_w, win_h, flags);
	if (!screen) {
		free(pixels);
		pixels = NULL;
		return NULL;
	}

	gpuDevice = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL, false, NULL);
	if (!gpuDevice) {
		IFDEBUG_VIDEO(sys_printf("xrick/video: no SDL_GPU device (%s)\n", SDL_GetError());)
		SDL_DestroyWindow(screen);
		screen = NULL;
		free(pixels);
		pixels = NULL;
		return NULL;
	}
	if (!SDL_ClaimWindowForGPUDevice(gpuDevice, screen)) {
		IFDEBUG_VIDEO(sys_printf("xrick/video: could not claim window for GPU device (%s)\n", SDL_GetError());)
		SDL_DestroyGPUDevice(gpuDevice);
		gpuDevice = NULL;
		SDL_DestroyWindow(screen);
		screen = NULL;
		free(pixels);
		pixels = NULL;
		return NULL;
	}

	/* shaders -- one shared vertex shader (fullscreen triangle), one
	 * fragment shader per available look/pass. Each is embedded as both
	 * SPIR-V and (macOS only) MSL; sysvid_gpu_createShader picks whichever
	 * the device actually backs onto. */
	{
		SDL_GPUShaderFormat gpuShaderFormats = SDL_GetGPUShaderFormats(gpuDevice);

		gpuVertShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_passthrough_vert_spv, shader_passthrough_vert_spv_len,
			shader_passthrough_vert_msl, shader_passthrough_vert_msl_len,
			SDL_GPU_SHADERSTAGE_VERTEX, 0, 0);

		gpuPassthroughFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_passthrough_frag_spv, shader_passthrough_frag_spv_len,
			shader_passthrough_frag_msl, shader_passthrough_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

		gpuCrtEasymodeFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_easymode_frag_spv, shader_crt_easymode_frag_spv_len,
			shader_crt_easymode_frag_msl, shader_crt_easymode_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuCrtEasymodePostFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_easymode_post_frag_spv, shader_crt_easymode_post_frag_spv_len,
			shader_crt_easymode_post_frag_msl, shader_crt_easymode_post_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuSharpFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_sharp_bilinear_frag_spv, shader_sharp_bilinear_frag_spv_len,
			shader_sharp_bilinear_frag_msl, shader_sharp_bilinear_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuCrtLottesFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_lottes_frag_spv, shader_crt_lottes_frag_spv_len,
			shader_crt_lottes_frag_msl, shader_crt_lottes_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuEasuFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_fsr_easu_frag_spv, shader_fsr_easu_frag_spv_len,
			shader_fsr_easu_frag_msl, shader_fsr_easu_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuRcasFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_fsr_rcas_frag_spv, shader_fsr_rcas_frag_spv_len,
			shader_fsr_rcas_frag_msl, shader_fsr_rcas_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuCurvatureFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_curvature_frag_spv, shader_crt_curvature_frag_spv_len,
			shader_crt_curvature_frag_msl, shader_crt_curvature_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuFinalEncodeFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_final_encode_frag_spv, shader_crt_final_encode_frag_spv_len,
			shader_crt_final_encode_frag_msl, shader_crt_final_encode_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuRoyaleLinearizeFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_linearize_frag_spv, shader_crt_royale_linearize_frag_spv_len,
			shader_crt_royale_linearize_frag_msl, shader_crt_royale_linearize_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

		gpuRoyaleVscanFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_vscan_frag_spv, shader_crt_royale_vscan_frag_spv_len,
			shader_crt_royale_vscan_frag_msl, shader_crt_royale_vscan_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuRoyaleBloomApproxFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_bloom_approx_frag_spv, shader_crt_royale_bloom_approx_frag_spv_len,
			shader_crt_royale_bloom_approx_frag_msl, shader_crt_royale_bloom_approx_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 0);

		gpuRoyaleHscanMaskFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_hscan_mask_frag_spv, shader_crt_royale_hscan_mask_frag_spv_len,
			shader_crt_royale_hscan_mask_frag_msl, shader_crt_royale_hscan_mask_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);

		gpuRoyaleBrightpassFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_brightpass_frag_spv, shader_crt_royale_brightpass_frag_spv_len,
			shader_crt_royale_brightpass_frag_msl, shader_crt_royale_brightpass_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 2, 1);

		gpuRoyaleBloomBlurFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_bloom_blur_frag_spv, shader_crt_royale_bloom_blur_frag_spv_len,
			shader_crt_royale_bloom_blur_frag_msl, shader_crt_royale_bloom_blur_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 1, 1);

		gpuRoyaleReconstituteFragShader = sysvid_gpu_createShader(gpuDevice, gpuShaderFormats,
			shader_crt_royale_reconstitute_frag_spv, shader_crt_royale_reconstitute_frag_spv_len,
			shader_crt_royale_reconstitute_frag_msl, shader_crt_royale_reconstitute_frag_msl_len,
			SDL_GPU_SHADERSTAGE_FRAGMENT, 3, 1);
	}

	if (!gpuVertShader || !gpuPassthroughFragShader || !gpuCrtEasymodeFragShader || !gpuCrtEasymodePostFragShader ||
	    !gpuSharpFragShader || !gpuCrtLottesFragShader || !gpuEasuFragShader || !gpuRcasFragShader ||
	    !gpuCurvatureFragShader || !gpuFinalEncodeFragShader || !gpuRoyaleLinearizeFragShader ||
	    !gpuRoyaleVscanFragShader || !gpuRoyaleBloomApproxFragShader || !gpuRoyaleHscanMaskFragShader ||
	    !gpuRoyaleBrightpassFragShader || !gpuRoyaleBloomBlurFragShader || !gpuRoyaleReconstituteFragShader)
		sys_panic("xrick/video: could not create GPU shaders (%s)", SDL_GetError());

	/* the composited frame, uploaded fresh every sysvid_gl_present() call */
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

	memset(&tbInfo, 0, sizeof(tbInfo));
	tbInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	tbInfo.size = (Uint32)fb_width * fb_height * sizeof(U32);
	gpuTransferBuf = SDL_CreateGPUTransferBuffer(gpuDevice, &tbInfo);

	if (!gpuTexture || !gpuTransferBuf)
		sys_panic("xrick/video: could not create GPU frame texture (%s)", SDL_GetError());

	/* nearest, clamp -- the default sampler for most passes */
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

	/* trilinear + anisotropic, repeat -- crt-royale's tiled phosphor mask */
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

	/* trilinear, clamp, full mip range -- the curvature/final-encode pass's
	 * warped or downscaled lookup into gpuFinalIntermediate */
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
	 * output is composited into. screenX0/Y0/X1/Y1 were measured once from
	 * the source art. */
	memset(bezels, 0, sizeof(bezels));
	bezels[BEZEL_COMMODORE_1084S].texture = loadPNGTexture(
	    bezel_commodore_1084s_png, bezel_commodore_1084s_png_len,
	    &bezels[BEZEL_COMMODORE_1084S].texW, &bezels[BEZEL_COMMODORE_1084S].texH);
	bezels[BEZEL_COMMODORE_1084S].screenX0 = 0.216326f;
	bezels[BEZEL_COMMODORE_1084S].screenY0 = 0.075000f;
	bezels[BEZEL_COMMODORE_1084S].screenX1 = 0.783673f;
	bezels[BEZEL_COMMODORE_1084S].screenY1 = 0.848214f;

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
	return screen;
}


/*
 * sysvid_gl_shutdown
 *
 * releases every GPU resource, then the window itself -- sysvid.c's own
 * shutdown path skips SDL_DestroyWindow when we were the active backend,
 * since it's our job here (see sysvid_gl.h).
 */
void
sysvid_gl_shutdown(void)
{
	int i;

	free(pixels);
	pixels = NULL;

	if (!gpuDevice) {
		if (screen) SDL_DestroyWindow(screen);
		screen = NULL;
		return;
	}

	SDL_WaitForGPUIdle(gpuDevice);

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

	SDL_ReleaseGPUSampler(gpuDevice, gpuSampler);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerLinearClamp);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerLinearRepeat);
	SDL_ReleaseGPUSampler(gpuDevice, gpuSamplerMipmap);
	SDL_ReleaseGPUTransferBuffer(gpuDevice, gpuTransferBuf);

	SDL_ReleaseWindowFromGPUDevice(gpuDevice, screen);
	SDL_DestroyGPUDevice(gpuDevice);
	gpuDevice = NULL;

	SDL_DestroyWindow(screen);
	screen = NULL;
}


/*
 * chainSize
 *
 * the size the picture is actually rendered at before being resampled into
 * its on-screen viewport: the next whole multiple of the source's 200 lines
 * up from the viewport height (never below it), at the same aspect ratio.
 * Drawing the scanline/mask shaders at a whole-number scale keeps their
 * line pattern locked to the pixel grid (a fractional scale makes it beat
 * against the grid as drifting bands); the final pass then scales that down
 * a little with a mip-mapped filter, which keeps it smooth. Works out to
 * exactly the viewport size whenever that's already a whole multiple.
 */
static void
chainSize(const SDL_GPUViewport *vp, float *cw, float *ch)
{
	float aspect = (aspectMode == 0) ? 4.0f / 3.0f : (float)fb_width / (float)fb_height;
	float lines = vp->h / (float)fb_height - 0.02f;
	int n = (int)lines;

	if ((float)n < lines) n++; /* round up */

	if (n < 1) n = 1;
	*ch = (float)(n * (int)fb_height);
	*cw = (float)(int)(*ch * aspect + 0.5f);
}


/*
 * fitAspectInside
 *
 * places the picture inside a bezel's screen opening at the chosen aspect
 * ratio (4:3 or square pixels), centered, leaving bars where the opening is
 * a different shape.
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
 * runChainAndPresent
 *
 * uploads our own `pixels` copy of the frame and runs it through the
 * upscale/CRT/bezel chain onto the swapchain. Shared by sysvid_gl_present
 * (after copying in the caller's frame) and the cycle/setter functions
 * below (which just want to redraw the current frame with a changed
 * setting, same as sysvid_update(&SCREENRECT) did before sysvid.c took
 * over frame compositing).
 */
static void
runChainAndPresent(void)
{
	void *mapped;
	SDL_GPUCommandBuffer *cmd;
	SDL_GPUCopyPass *copyPass;
	SDL_GPUTextureTransferInfo src;
	SDL_GPUTextureRegion dst;
	SDL_GPUTexture *swapTex;
	Uint32 swW, swH;

	if (!gpuDevice) return;

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
		float cw, ch; /* size the picture is rendered at, see chainSize */
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

		chainSize(&vp, &cw, &ch);
		outputSize[0] = cw;
		outputSize[1] = ch;

		/* Every mode below now renders its "flat" output into
		 * gpuFinalIntermediate (R8G8B8A8_UNORM, sized to the viewport)
		 * rather than straight to the swapchain -- unified so a single
		 * final pass afterward can either warp it with screen curvature
		 * (bezel active) or just copy it through unchanged (no bezel). */
		ensureFinalIntermediate((Uint32)cw, (Uint32)ch);

		if (crtMode == CRT_ROYALE) {
			SDL_GPUTexture *multiTex[3];
			SDL_GPUSampler *multiSamp[3];
			float vscanUniform[2];
			float hscanUniform[2];
			float blurUniformV[2];
			float blurUniformH[2];

			ensureRoyaleIntermediates((Uint32)cw, (Uint32)ch);

			runPass(cmd, gpuRoyaleLinearizePipeline, gpuTexture, gpuRoyaleLinearized,
				0.0f, 0.0f, (float)fb_width, (float)fb_height, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);

			vscanUniform[0] = (float)fb_width;
			vscanUniform[1] = ch;
			runPass(cmd, gpuRoyaleVscanPipeline, gpuRoyaleLinearized, gpuRoyaleVscan,
				0.0f, 0.0f, (float)fb_width, ch, SDL_GPU_LOADOP_DONT_CARE,
				vscanUniform, sizeof(vscanUniform));

			multiTex[0] = gpuRoyaleLinearized;
			multiSamp[0] = gpuSamplerLinearClamp;
			runPassMulti(cmd, gpuRoyaleBloomApproxPipeline, multiTex, multiSamp, 1, gpuRoyaleBloomApprox,
				     0.0f, 0.0f, 320.0f, 240.0f, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);

			hscanUniform[0] = cw;
			hscanUniform[1] = ch;
			multiTex[0] = gpuRoyaleVscan;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuPhosphorMaskTexture;
			multiSamp[1] = gpuSamplerLinearRepeat;
			runPassMulti(cmd, gpuRoyaleHscanMaskPipeline, multiTex, multiSamp, 2, gpuRoyaleMaskedScanlines,
				     0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE,
				     hscanUniform, sizeof(hscanUniform));

			multiTex[0] = gpuRoyaleMaskedScanlines;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuRoyaleBloomApprox;
			multiSamp[1] = gpuSamplerLinearClamp;
			runPassMulti(cmd, gpuRoyaleBrightpassPipeline, multiTex, multiSamp, 2, gpuRoyaleBrightpass,
				     0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE,
				     &royaleMaskAmplify, sizeof(royaleMaskAmplify));

			blurUniformV[0] = 0.0f;
			blurUniformV[1] = 1.0f / ch;
			runPass(cmd, gpuRoyaleBloomBlurPipeline, gpuRoyaleBrightpass, gpuRoyaleBloomV,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE,
				blurUniformV, sizeof(blurUniformV));

			blurUniformH[0] = 1.0f / cw;
			blurUniformH[1] = 0.0f;
			runPass(cmd, gpuRoyaleBloomBlurPipeline, gpuRoyaleBloomV, gpuRoyaleBloomH,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE,
				blurUniformH, sizeof(blurUniformH));

			multiTex[0] = gpuRoyaleBloomH;
			multiSamp[0] = gpuSampler;
			multiTex[1] = gpuRoyaleMaskedScanlines;
			multiSamp[1] = gpuSampler;
			multiTex[2] = gpuRoyaleBrightpass;
			multiSamp[2] = gpuSampler;
			runPassMulti(cmd, gpuRoyaleReconstitutePipeline, multiTex, multiSamp, 3, gpuFinalIntermediate,
				     0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE,
				     &royaleMaskAmplify, sizeof(royaleMaskAmplify));
		} else if (crtMode == CRT_LOTTES) {
			runPass(cmd, gpuCrtLottesPipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
		} else if (upscaleMode == UPSCALE_SHARP) {
			SDL_GPUTexture *sharpTex[1] = {gpuTexture};
			SDL_GPUSampler *sharpSamp[1] = {gpuSamplerLinearClamp};

			if (crtMode == CRT_EASYMODE) {
				ensureIntermediates((Uint32)cw, (Uint32)ch);
				runPassMulti(cmd, gpuSharpToIntermediatePipeline, sharpTex, sharpSamp, 1, gpuIntermediate1,
					     0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
				runPass(cmd, gpuCrtEasymodePostPipeline, gpuIntermediate1, gpuFinalIntermediate,
					0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			} else {
				runPassMulti(cmd, gpuSharpToFinalPipeline, sharpTex, sharpSamp, 1, gpuFinalIntermediate,
					     0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			}
		} else if (upscaleMode == UPSCALE_FSR1) {
			float rcasUniform[3];

			ensureIntermediates((Uint32)cw, (Uint32)ch);

			rcasUniform[0] = cw;
			rcasUniform[1] = ch;
			rcasUniform[2] = (float)(fsrFrameCount++);

			runPass(cmd, gpuEasuPipeline, gpuTexture, gpuIntermediate1,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));

			if (crtMode == CRT_EASYMODE) {
				runPass(cmd, gpuRcasToIntermediatePipeline, gpuIntermediate1, gpuIntermediate2,
					0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, rcasUniform, sizeof(rcasUniform));
				runPass(cmd, gpuCrtEasymodePostPipeline, gpuIntermediate2, gpuFinalIntermediate,
					0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
			} else {
				runPass(cmd, gpuRcasToSwapchainPipeline, gpuIntermediate1, gpuFinalIntermediate,
					0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, rcasUniform, sizeof(rcasUniform));
			}
		} else if (crtMode == CRT_EASYMODE) {
			runPass(cmd, gpuCrtEasymodePipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, outputSize, sizeof(outputSize));
		} else {
			runPass(cmd, gpuPassthroughToIntermediatePipeline, gpuTexture, gpuFinalIntermediate,
				0.0f, 0.0f, cw, ch, SDL_GPU_LOADOP_DONT_CARE, NULL, 0);
		}

		/* final pass: warp with curvature into the bezel's screen cutout,
		 * or just copy straight to the swapchain if there's no bezel --
		 * also where crt-royale's deliberately-deferred clamp/gamma-encode
		 * finally happens; every other mode already wrote clamped,
		 * display-ready color into gpuFinalIntermediate, so applyGamma is
		 * a no-op for them. */
		{
			float applyGamma = (crtMode == CRT_ROYALE) ? 1.0f : 0.0f;

			if (bz) {
				SDL_GPUTexture *curvTex[1] = {gpuFinalIntermediate};
				SDL_GPUSampler *curvSamp[1] = {gpuSamplerMipmap};

				SDL_GenerateMipmapsForGPUTexture(cmd, gpuFinalIntermediate);

				runPassMulti(cmd, gpuCurvaturePipeline, curvTex, curvSamp, 1, swapTex,
					     vp.x, vp.y, vp.w, vp.h, finalLoadOp, &applyGamma, sizeof(applyGamma));
			} else {
				SDL_GPUTexture *encTex[1] = {gpuFinalIntermediate};
				SDL_GPUSampler *encSamp[1] = {gpuSamplerMipmap};

				SDL_GenerateMipmapsForGPUTexture(cmd, gpuFinalIntermediate);
				runPassMulti(cmd, gpuFinalEncodePipeline, encTex, encSamp, 1, swapTex,
					     vp.x, vp.y, vp.w, vp.h, finalLoadOp, &applyGamma, sizeof(applyGamma));
			}
		}
	}

	SDL_SubmitGPUCommandBuffer(cmd);
}


/*
 * sysvid_gl_present
 *
 * copies the caller's finished RGBA frame into our own buffer (so the OSD
 * and the settings menu can draw on top of it without touching sysvid.c's
 * own copy), then runs the chain. The menu is drawn here, not in upstream's
 * shared sysvid.c, because it's only ever usable through this backend's own
 * overlay API (sysvid_overlayRect/overlayText) -- sysvid_gl.c/WebGL has no
 * equivalent, so the web build never links menu.c (see build.sh).
 */
void
sysvid_gl_present(const Uint8 *rgba)
{
	if (!pixels) return;

	memcpy(pixels, rgba, (size_t)fb_width * fb_height * sizeof(U32));

	if (sysvid_osdActive())
		sysvid_drawOSD();

	if (menu_active())
		menu_draw();

	runChainAndPresent();
}


/*
 * sysvid_cycleUpscale / sysvid_cycleCrt / sysvid_cycleBezel
 *
 * F10/F11/F12: cycle one axis and show a brief OSD confirmation.
 */
void
sysvid_cycleUpscale(void)
{
	if (!gpuDevice) { /* plain SDL_Renderer fallback (e.g. no SPIR-V on macOS): nothing to switch */
		sysvid_showOSD("NO GPU SHADERS");
		return;
	}

	upscaleMode = (upscaleMode + 1) % UPSCALE_COUNT;

	sysvid_showOSD(upscaleMode == UPSCALE_NONE ? "UPSCALING: NONE" : upscaleMode == UPSCALE_FSR1 ? "UPSCALING: FSR1"
												     : "UPSCALING: SHARP");
	sysvid_drawOSD();
	runChainAndPresent();
}

void
sysvid_cycleCrt(void)
{
	if (!gpuDevice) { /* plain SDL_Renderer fallback (e.g. no SPIR-V on macOS): nothing to switch */
		sysvid_showOSD("NO GPU SHADERS");
		return;
	}

	do {
		crtMode = (crtMode + 1) % CRT_COUNT;
	} while (crtMode == CRT_ROYALE && gpuPhosphorMaskTexture == NULL);

	sysvid_showOSD(crtMode == CRT_NONE ? "SHADER: NONE" : crtMode == CRT_EASYMODE ? "SHADER: EASYMODE"
							  : crtMode == CRT_ROYALE     ? "SHADER: ROYALE"
												      : "SHADER: LOTTES");
	sysvid_drawOSD();
	runChainAndPresent();
}

void
sysvid_cycleBezel(void)
{
	bezelMode_t next = bezelMode;

	if (!gpuDevice) { /* plain SDL_Renderer fallback (e.g. no SPIR-V on macOS): nothing to switch */
		sysvid_showOSD("NO GPU SHADERS");
		return;
	}

	do {
		next = (next + 1) % BEZEL_COUNT;
	} while (next != BEZEL_NONE && bezels[next].texture == NULL);
	bezelMode = next;

	sysvid_showOSD(bezelMode == BEZEL_NONE ? "BEZEL: NONE" : bezelMode == BEZEL_COMMODORE_1084S ? "BEZEL: 1084S"
												    : "BEZEL: SC1224");
	sysvid_drawOSD();
	runChainAndPresent();
}


/*
 * Getters/setters for a future settings menu. isFullscreen/getWindowZoom
 * query the live SDL window state rather than tracking a shadow copy,
 * since sysvid.c owns the real zoom/fullscreen bookkeeping.
 */
U8
sysvid_isFullscreen(void)
{
	SDL_Window *win = sysvid_getWindow(); /* not `screen`: gone if the renderer fallback took over */

	return win && (SDL_GetWindowFlags(win) & SDL_WINDOW_FULLSCREEN) ? TRUE : FALSE;
}

U8
sysvid_getWindowZoom(void)
{
	SDL_Window *win = sysvid_getWindow();
	int w = 0, h = 0;

	if (!win || fb_width == 0) return 1;
	SDL_GetWindowSize(win, &w, &h);
	return (U8)SDL_max(1, w / fb_width);
}

int
sysvid_getUpscale(void)
{
	return upscaleMode;
}

void
sysvid_setUpscale(int v)
{
	if (!gpuDevice) return; /* keep the saved choice for when a GPU is there */
	if (v >= 0 && v < UPSCALE_COUNT) upscaleMode = (upscaleMode_t)v;
	runChainAndPresent();
}

int
sysvid_getCrt(void)
{
	return crtMode;
}

void
sysvid_setCrt(int v)
{
	if (!gpuDevice) return; /* keep the saved choice for when a GPU is there */
	if (v >= 0 && v < CRT_COUNT && !(v == CRT_ROYALE && gpuPhosphorMaskTexture == NULL))
		crtMode = (crtMode_t)v;
	runChainAndPresent();
}

int
sysvid_getBezel(void)
{
	return bezelMode;
}

void
sysvid_setBezel(int v)
{
	if (!gpuDevice) return; /* keep the saved choice for when a GPU is there */
	if (v >= 0 && v < BEZEL_COUNT && (v == BEZEL_NONE || bezels[v].texture != NULL))
		bezelMode = (bezelMode_t)v;
	runChainAndPresent();
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
	runChainAndPresent();
}

/* eof */

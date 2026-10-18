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
  * The purpose of this file is to implement a set of functions so that the
  * 8bit, palettized frame buffer onto which the entire game is painted can
  * be displayed onto the computer's screen.
  *
  * The only dependency between this and the game is that here we know the
  * frame buffer is 8bit. We don't know its size.
  */



#include <stdlib.h> /* malloc */
#include <string.h> /* memcpy */

#include <SDL3/SDL.h>

#include "sysvid.h"
#include "sysarg.h"
#include "debug.h"
#include "fb.h"
#include "img.h"
#include "sysvid_gl.h"
#include "sysvid_overlay.h"
#include "menu.h"


#ifdef __MSVC__
#include <memory.h> /* memset */
#endif



#undef BPP8
#define BPP32

/* SDL3 dropped the separate "fullscreen desktop" flag: SDL_WINDOW_FULLSCREEN now
   always means borderless-at-desktop-resolution unless an exclusive display mode
   is set via SDL_SetWindowFullscreenMode(), which this file never does. */
#define SDL_FULLSCREEN SDL_WINDOW_FULLSCREEN



rect_t SCREENRECT = {0, 0, FB_WIDTH, FB_HEIGHT, NULL}; /* whole fb */

static U16 paln; /* palette size */
static SDL_Color pals[256], pald[256]; /* fixme: explain */
static U32* pixels;
static SDL_Window *screen;
static SDL_Renderer *renderer;
static SDL_Texture* texture;
static SDL_WindowFlags videoFlags; /* SDL3 window flags are 64-bit */
/* review-plan.md R0.1: renamed from 'gamma', which collides with libm's gamma().
   File-static, build portability only. */
static U8 vid_gamma;
static U16 fb_width, fb_height;

static U8 zoom = 0; /* actual zoom level */
static U8 wmzoom = SYSVID_ZOOM; /* window mode zoom level */
static U8 mxzoom = SYSVID_MAXZOOM; /* max zoom level */

/* the last known windowed (non-fullscreen) size in real pixels, kept in sync by
   sysvid_notifyResized() from SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED (sysevt.c) --
   covers the window getting resized by anything other than our own zoom keys
   (a user drag-resize, or a tiling compositor imposing its own geometry), so
   leaving fullscreen restores to wherever it actually was, not to whatever
   fb_width*zoom last happened to be. Starts at 0 and gets its first real value
   from the window's actual size right after creation, in sysvid_init(). */
static U16 winW, winH;

#ifdef ENABLE_SHADERS
/* the frame goes through the GL shader chain (sysvid_gl.c) instead of the renderer.
   pixels then holds the whole frame as RGBA bytes, which is what GL uploads */
static U8 use_gl = 0;
#endif



#include "img_icon.e"



/*
 * sysvid_setPaletteFromRGB
 *
 * sets the palette according to RGB infos.
 */
void sysvid_setPaletteFromRGB(U8 *r, U8 *g, U8 *b, U16 n)
{
	U16 i;

	if ((paln = n) == 0) return;

	for (i = 0; i < paln; ++i)
	{
		pals[i].r = r[i];
		pals[i].g = g[i];
		pals[i].b = b[i];
	}

	sysvid_setDisplayPalette();

#ifdef BPP8
	//SDL_SetColors(screen, (SDL_Color *)&pald, 0, paln);
#endif
}



/*
 * sysvid_setDisplayPalette
 *
 * sets (again) the display palette, useful when visibility has changed.
 */
void sysvid_setDisplayPalette(void)
{
	U16 i;

	if (paln == 0) return;

	for (i = 0; i < paln; i++)
	{
		pald[i].r = pals[i].r * vid_gamma / 255;
		pald[i].g = pals[i].g * vid_gamma / 255;
		pald[i].b = pals[i].b * vid_gamma / 255;
	}

	/*
	 * opaque pixels, all 256 entries: sysvid_update copies .a into the ARGB
	 * texture. it used to stay 0, which the texture's SDL_BLENDMODE_NONE hid on
	 * the desktop, but on the web SDL3 creates the WebGL canvas with an alpha
	 * channel (gl_config.alpha_size 8) and premultiplied alpha, so every frame
	 * was colour with alpha 0 -- shown as fully transparent by Chrome on Apple
	 * silicon (a black page background behind an invisible canvas), while
	 * Chrome/Windows and iOS Safari happened to display the colours.
	 */
	for (i = 0; i < 256; i++)
		pald[i].a = 255;
}



/*
 * sysvid_init
 *
 * initialize the video layer.
 */
void sysvid_init(U16 width, U16 height)
{
	SDL_Surface *s;
	U8 *mask, tpix;
	U32 len, i;

	fb_width = width;
	fb_height = height;

	IFDEBUG_VIDEO(sys_printf("xrick/video: start\n"););

	/* various WM stuff */
	SDL_HideCursor();

	/* SDL3 has no SDL_CreateRGBSurfaceFrom (mask-based format inference); build the
	   indexed surface and its palette explicitly instead. */
	s = SDL_CreateSurface(IMG_ICON->w, IMG_ICON->h, SDL_PIXELFORMAT_INDEX8);
	if (s) {
		SDL_Palette *pal = SDL_CreatePalette(IMG_ICON->ncolors);
		for (i = 0; i < IMG_ICON->ncolors; i++) {
			pal->colors[i].r = IMG_ICON->colors[i].r;
			pal->colors[i].g = IMG_ICON->colors[i].g;
			pal->colors[i].b = IMG_ICON->colors[i].b;
			pal->colors[i].a = 255;
		}
		SDL_SetSurfacePalette(s, pal);
		SDL_DestroyPalette(pal);
		for (i = 0; i < IMG_ICON->h; i++)
			memcpy((U8 *)s->pixels + i * (U32)s->pitch, IMG_ICON->pixels + i * (U32)IMG_ICON->w, IMG_ICON->w);
	}
	tpix = *(IMG_ICON->pixels);
IFDEBUG_VIDEO(
	sys_printf("xrick/video: icon is %dx%d\n", IMG_ICON->w, IMG_ICON->h);
	sys_printf("xrick/video: icon transp. color is #%d (%d,%d,%d)\n", tpix,
		IMG_ICON->colors[tpix].r,
		IMG_ICON->colors[tpix].g,
		IMG_ICON->colors[tpix].b);
);
	/*

	* old dirty stuff to implement transparency. SetColorKey does it
	* on Windows w/out problems. Linux? FIXME!

	len = IMG_ICON->w * IMG_ICON->h;
	mask = (U8 *)malloc(len/8);
	memset(mask, 0, len/8);
	for (i = 0; i < len; i++)
	if (IMG_ICON->pixels[i] != tpix) mask[i/8] |= (0x80 >> (i%8));
	*/
	/*
	* FIXME
	* Setting a mask produces strange results depending on the
	* Window Manager. On fvwm2 it is shifted to the right ...
	*/
	/*SDL_WM_SetIcon(s, mask);*/

	// fixme?
	//SDL_SetColorKey(s,
	//	SDL_SRCCOLORKEY,
	//	SDL_MapRGB(s->format,IMG_ICON->colors[tpix].r,IMG_ICON->colors[tpix].g,IMG_ICON->colors[tpix].b));

	// fixme - which window?!
	//SDL_WM_SetIcon(s, NULL);

	/* video modes and screen */
	videoFlags = 0;
#ifdef BPP8
	videoFlags |= SDL_HWPALETTE;
#endif
	/* RESIZABLE matters beyond letting the user drag-resize: on native Wayland
	   (confirmed on Hyprland, 2026-10-18), SDL3's SetWindowSize on a window
	   created WITHOUT this flag takes a different, fixed-size-only path (sets
	   xdg_toplevel min==max size) -- compositors that treat window geometry as
	   their own for floating windows (Hyprland) silently ignore that, so F2/F3
	   zoom and the fullscreen-restore size never actually changed the window.
	   KWin happened to honour it anyway, which is why this only ever showed up
	   there. Both render paths already derive their output size freshly every
	   frame (sysvid_gpu.c's swapchain query, this file's SDL_RenderLogicalPresentation
	   letterboxing below) rather than caching a size computed at zoom-change time,
	   so no further per-backend work is needed to scale into whatever size the
	   window actually ends up at once it can really change size. */
	videoFlags |= SDL_WINDOW_RESIZABLE;
	//chkVideo();  /* check video modes */

	/* if a zoom was specified, use it -- but check it is ok. it is the window mode
	   zoom: setting <zoom> here was overwritten just below, so -zoom had no effect */
	if (sysarg_args_zoom)
	{
		wmzoom = sysarg_args_zoom > 0 && sysarg_args_zoom <= mxzoom ? (U8)sysarg_args_zoom : mxzoom;
	}

	/* prepare for fullscreen, initialize zoom w/default values */
	if (sysarg_args_fullscreen)
	{
		videoFlags |= SDL_FULLSCREEN;
		zoom = 1;
	}
	else
	{
		zoom = wmzoom;
	}

	/* initialize screen surface */
//#ifdef BPP8
//	screen = SDL_SetVideoMode(fb_width * zoom, fb_height * zoom,
//		8, videoFlags);
//#endif
//#ifdef BPP32
//	screen = SDL_SetVideoMode(fb_width * zoom, fb_height * zoom,
//		32, videoFlags);
//#endif

	// create pixels
	// FIXME free pixels!
	pixels = (U32*)malloc(fb_width * fb_height * sizeof(U32));

#ifdef ENABLE_SHADERS
	/* the GL shader chain creates its own (GL) window; on failure it has released
	   everything and the renderer path below runs as without shaders */
	screen = sysvid_gl_init("xrick", fb_width * zoom, fb_height * zoom, videoFlags,
		fb_width, fb_height);
	if (screen)
	{
		use_gl = 1;
		memset(pixels, 0, fb_width * fb_height * sizeof(U32));
		SDL_SetWindowIcon(screen, s);
		SDL_DestroySurface(s);
		sysvid_gl_present((U8 *)pixels);
		winW = fb_width * zoom;
		winH = fb_height * zoom;
		IFDEBUG_VIDEO(sys_printf("xrick/video: ready\n"););
		return;
	}
#endif

	// create window/screen
	// SDL3 SDL_CreateWindow drops the x/y position args (always undefined-position
	// equivalent; use SDL_SetWindowPosition if a specific spot is ever needed).
	screen = SDL_CreateWindow("xrick", fb_width * zoom, fb_height * zoom, videoFlags);
	SDL_SetWindowIcon(screen, s);
	SDL_DestroySurface(s);
	winW = fb_width * zoom;
	winH = fb_height * zoom;

	// create renderer -- SDL3 takes a driver name (NULL = default) instead of an index/flags pair
	renderer = SDL_CreateRenderer(screen, NULL);

	// needed for fullscreen-desktop mode?
	SDL_SetRenderLogicalPresentation(renderer, fb_width, fb_height, SDL_LOGICAL_PRESENTATION_LETTERBOX);

	// clear
	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
	SDL_RenderClear(renderer);
	SDL_RenderPresent(renderer);

	// fixme this is temp
	// not using rects for now but we could ...
	texture = SDL_CreateTexture(renderer,
		SDL_PIXELFORMAT_ARGB8888,
		SDL_TEXTUREACCESS_STREAMING,
		fb_width, fb_height);

	// scaling quality: SDL3 dropped SDL_HINT_RENDER_SCALE_QUALITY in favor of a
	// per-texture scale mode (SDL_SCALEMODE_LINEAR is also the SDL3 default).
	SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);

	// This is an opaque raster framebuffer. pald[].a used to be left at 0; SDL2
	// apparently defaulted new textures to a blend mode that ignores it; SDL3
	// renders an all-zero alpha channel as fully transparent by default, which
	// blended the whole frame down to the black SDL_RenderClear() color
	// underneath it. Blending stays off, and since 2026-09-29 the palette's alpha
	// is 255 as well (sysvid_setDisplayPalette): with alpha 0 the web canvas
	// itself came out transparent on Chrome/macOS.
	SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_NONE);

	/* the settings menu and OSD draw into an RGBA frame; this path composes one in
	   `pixels` while either is up (see sysvid_update) */
	sysvid_overlay_init(pixels, fb_width, fb_height);

	SDL_UpdateTexture(texture, NULL, pixels, fb_width * sizeof(U32));
	SDL_RenderClear(renderer);
	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);

// http://www.linuxdevcenter.com/pub/a/linux/2003/08/07/sdl_anim.html
// http://www.linuxdevcenter.com/linux/2003/08/07/examples/hardlines.cpp

	// fixme what shall we do with this?
//IFDEBUG_VIDEO2(
//	sys_printf("xrick/video: mode: %dx%d %dbpp pitch=%d %s\n",
//		screen->w, screen->h, screen->format->BitsPerPixel, screen->pitch,
//		videoFlags & SDL_FULLSCREEN ? "fullscreen" : "");
//	sys_printf("xrick/video: HWSURFACE: %s\n", screen->flags & SDL_HWSURFACE ? "Y" : "N");
//	sys_printf("xrick/video: DOUBLEBUF: %s\n", screen->flags & SDL_DOUBLEBUF ? "Y" : "N");
//	/* FIXME also report the REAL HW infos i.e. the actual screen resolution */
//	/* i.e. if we ask for 996x600 SDL says OK but then it maps <screen> to the actual screen */
//);

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
	free(pixels);
	pixels = NULL;

#ifdef ENABLE_SHADERS
	if (use_gl)
	{
		sysvid_gl_shutdown(); /* also destroys the window */
		use_gl = 0;
		screen = NULL;
		return;
	}
#endif
	SDL_DestroyWindow(screen);
}




/*
 * sysvid_update
 *
 * display the 8bit palettized frame buffer onto the screen. zoom, filter, whatever.
 */
void
sysvid_update(rect_t *rects)
{
	rect_t *rect;
	U8 n;

	if (rects == NULL) /* nothing to do? */
		return;

#ifdef ENABLE_SHADERS
	if (use_gl)
	{
		/* dirty rects into the RGBA copy of the frame, then the whole frame
		   through the shader chain (shaders look at neighbouring pixels, and
		   the window is redrawn in full anyway) */
		for (rect = rects; rect; rect = rect->next)
		{
			for (int y = rect->y; y < rect->y + rect->height; y++)
			{
				U8 *srcx = ((U8 *)&fb) + rect->x + y * fb_width;
				U8 *dstx = (U8 *)(pixels + rect->x + y * fb_width);

				for (int x = 0; x < rect->width; x++)
				{
					*dstx++ = pald[*srcx].r;
					*dstx++ = pald[*srcx].g;
					*dstx++ = pald[*srcx].b;
					*dstx++ = pald[*srcx].a;
					srcx++;
				}
			}
		}
		sysvid_gl_present((U8 *)pixels);
		return;
	}
#endif

	/* settings menu / OSD: the dirty-rect upload below only knows the game frame,
	   so while either is up (and once more after, to wipe it off the texture)
	   compose the whole frame in `pixels`, draw them on top, and upload that */
	{
		static U8 overlay_shown = 0;
		U8 overlay = sysvid_osdActive() || menu_active();

		if (overlay || overlay_shown)
		{
			U8 *srcx = (U8 *)&fb;
			U8 *dstx = (U8 *)pixels;

			for (int i = 0; i < fb_width * fb_height; i++)
			{
				*dstx++ = pald[*srcx].r;
				*dstx++ = pald[*srcx].g;
				*dstx++ = pald[*srcx].b;
				*dstx++ = pald[*srcx].a;
				srcx++;
			}
			if (sysvid_osdActive())
				sysvid_drawOSD();
			if (menu_active())
				menu_draw();
			sysvid_updateRGBA(pixels);
			overlay_shown = overlay;
			return;
		}
	}

	n = 0;
	rect = rects;
	while (rect)
	{
		SDL_Rect sdlrect = { rect->x, rect->y, rect->width, rect->height };
		int pitch;
		U32 *pixelx;

		/* Lock exactly this rect, not the whole texture: SDL3's D3D11 backend
		   allocates a fresh, uninitialized staging texture sized to the locked
		   area on every call and blits the WHOLE staging buffer back on unlock.
		   Locking the whole texture (NULL) while only painting the game's dirty
		   sub-rects into it left the rest of that staging buffer uninitialized/
		   stale, which then got stamped over the real texture -- visible as
		   flicker/tearing on Windows (not on WSL's OpenGL/software renderers,
		   which back streaming textures with one persistent buffer per texture
		   and only ever touch the locked rect). See sdl3-dx-fix.md. */
		if (!SDL_LockTexture(texture, &sdlrect, (void **)&pixelx, &pitch))
		{
			rect = rect->next;
			continue;
		}

		U16 o = rect->x + rect->y * fb_width;
		U8 *src0 = ((U8 *)&fb) + o;
		/* The returned pointer is the top-left of the locked rect itself, not
		   an offset into a full-texture buffer -- so the destination write
		   starts at (0,0), unlike the old whole-texture-lock code which had to
		   seek into the buffer by rect->x/rect->y. */
		U8 *dst0 = (U8 *)pixelx;

		for (int y = 0; y < rect->height; y++)
		{
			U8 *srcx = src0;
			U8 *dstx = dst0;

			for (int x = 0; x < rect->width; x++)
			{
				*dstx = pald[*srcx].b;
				dstx++;
				*dstx = pald[*srcx].g;
				dstx++;
				*dstx = pald[*srcx].r;
				dstx++;
				*dstx = pald[*srcx].a;
				dstx++;
				srcx++;
			}

			src0 += fb_width;
			dst0 += pitch;
		}

		SDL_UnlockTexture(texture);

		rect = rect->next;
		n++;
	}

	SDL_RenderTexture(renderer, texture, NULL, NULL);
	SDL_RenderPresent(renderer);
}



/*
 * sysvid_updateRGBA
 *
 * see sysvid.h. <rgba> is exactly fb_width x fb_height pixels (SYSVID_WIDTH x
 * SYSVID_HEIGHT) -- same assumption sysvid_update's own use_gl branch makes about
 * `pixels`, since that's the only size sysvid_init ever allocates it at.
 */
SDL_Window *
sysvid_getWindow(void)
{
	return screen;
}

void
sysvid_updateRGBA(const U32 *rgba)
{
#ifdef ENABLE_SHADERS
	if (use_gl)
	{
		memcpy(pixels, rgba, (size_t)fb_width * fb_height * sizeof(U32));
		sysvid_gl_present((U8 *)pixels);
		return;
	}
#endif
	{
		SDL_Rect sdlrect = { 0, 0, fb_width, fb_height };
		int pitch;
		U32 *pixelx;

		if (!SDL_LockTexture(texture, &sdlrect, (void **)&pixelx, &pitch))
			return;

		for (int y = 0; y < fb_height; y++)
		{
			const U32 *srcx = rgba + y * fb_width;
			U8 *dstx = (U8 *)pixelx + y * pitch;

			for (int x = 0; x < fb_width; x++)
			{
				U32 v = *srcx++;
				*dstx++ = (U8)((v >> 16) & 0xFF); /* b */
				*dstx++ = (U8)((v >> 8) & 0xFF);  /* g */
				*dstx++ = (U8)(v & 0xFF);         /* r */
				*dstx++ = (U8)((v >> 24) & 0xFF); /* a */
			}
		}

		SDL_UnlockTexture(texture);
		SDL_RenderTexture(renderer, texture, NULL, NULL);
		SDL_RenderPresent(renderer);
	}
}



/*
 * sysvid_getZoom
 *
 * the current zoom preset -- for the renderer to size its windowed-mode
 * content at (see sysvid_gpu.c's computeLetterboxViewport), independent of
 * whatever the OS window's actual current size happens to be.
 */
U8
sysvid_getZoom(void)
{
	return zoom;
}



/*
 * sysvid_zoom
 *
 * increases or decreases zoom by <z>, if possible.
 */
void
sysvid_zoom(S8 z)
{
	// FIXME fullscreen

	if ((z < 0 && zoom + z > 0) || (z > 0 && zoom + z <= mxzoom))
	{
		zoom += z;
		wmzoom = zoom;

		IFDEBUG_VIDEO(
			sys_printf("xrick/video: zoom=%d window=%dx%d\n", zoom, fb_width * zoom, fb_height * zoom);
		);

		SDL_SetWindowSize(screen, fb_width * zoom, fb_height * zoom);
		if (!(videoFlags & SDL_FULLSCREEN))
		{
			winW = fb_width * zoom;
			winH = fb_height * zoom;
		}

		sysvid_setDisplayPalette();
		sysvid_update(&SCREENRECT); /* repaint all */ /* FIXME */

		// fixme what shall we do with this?
//IFDEBUG_VIDEO2(
//	sys_printf("xrick/video: mode: %dx%d %dbpp pitch=%d %s\n",
//		screen->w, screen->h, screen->format->BitsPerPixel, screen->pitch,
//		videoFlags & SDL_FULLSCREEN ? "fullscreen" : "");
//);
	}
}



/*
 * sysvid_notifyResized
 *
 * called from sysevt.c on SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, i.e. whenever the
 * window's actual pixel size changes for ANY reason -- a user drag-resize, or a
 * tiling/floating compositor imposing its own geometry, not just our own zoom
 * keys (sysvid_zoom already updates winW/winH itself for that case). Both render
 * paths already pick up the new size on their own on the very next frame (the
 * GPU path queries the swapchain fresh every present; the SDL_Renderer fallback's
 * SDL_RenderLogicalPresentation letterboxes to whatever the renderer's current
 * output size is) -- this function's only job is keeping winW/winH correct so
 * sysvid_toggleFullscreen() restores to the right place, not to recompute
 * anything that affects what gets drawn.
 */
void
sysvid_notifyResized(int w, int h)
{
	if (w <= 0 || h <= 0 || (videoFlags & SDL_FULLSCREEN))
		return;
	winW = (U16)w;
	winH = (U16)h;
}

#ifdef __EMSCRIPTEN__
/*
 * sysvid_reassertSize
 *
 * Mobile web (reported: Chrome on a Samsung Galaxy S23 Edge, 2026-10-12): the canvas
 * sometimes ends up with width/height 0 shortly after sysvid_gl_init() creates it --
 * audio keeps running fine, so the game itself isn't stuck, only the canvas's actual
 * pixel buffer. Confirmed from the browser side that toggling fullscreen is what fixes
 * it, and sysvid_toggleFullscreen() above already shows why: leaving fullscreen
 * re-asserts the window size via this exact SDL_SetWindowSize call and forces a full
 * repaint, rather than trusting the browser to have kept the right size on its own.
 * Doing the same thing once, a moment after startup, from player.js (via
 * Module.ccall, see build.sh's EXPORTED_FUNCTIONS) fixes the same race without needing
 * the player to find and tap the fullscreen button themselves.
 */
void
sysvid_reassertSize(void)
{
	if (!screen)
		return;
	SDL_SetWindowSize(screen, fb_width * zoom, fb_height * zoom);
	sysvid_setDisplayPalette();
	sysvid_update(&SCREENRECT);
}

/*
 * sysvid_setBrowserSize
 *
 * The web page's own FULLSCREEN button (player.js) only ever calls the plain
 * screenDiv.requestFullscreen() browser API directly, never sysvid_toggleFullscreen()
 * below -- a real user gesture is required to grant browser fullscreen, and going
 * through SDL's event queue first (a frame after the keypress/click) isn't "direct
 * enough" for browsers to honour that from (same reasoning sysvid_toggleFullscreen's
 * own comment gives for the settings menu's FULLSCREEN item not being usable for this).
 * That means this renderer never got told to resize for the browser's real fullscreen
 * dimensions, so going fullscreen on the web just stretched the small windowed-size
 * canvas via CSS -- soft/blurry, defeating the whole point of the CRT shaders and
 * bezel (reported 2026-10-12: "used to look nice and sharp in fullscreen", referring
 * to the in-game menu's own SDL-backed FULLSCREEN toggle, a different code path).
 * Called from player.js's 'fullscreenchange' handler, after the fact -- once already
 * in real browser fullscreen, resizing isn't gesture-gated, so this is safe to call
 * from an event handler rather than needing the original click/tap itself.
 */
void
sysvid_setBrowserSize(int w, int h)
{
	if (!screen || w <= 0 || h <= 0)
		return;
	SDL_SetWindowSize(screen, w, h);
	sysvid_setDisplayPalette();
	sysvid_update(&SCREENRECT);
}
#endif

/*
 * sysvid_toggleFullscreen
 *
 * toggles fullscreen.
 */
void
sysvid_toggleFullscreen(void)
{
	videoFlags ^= SDL_FULLSCREEN;
	/* SDL3 SDL_SetWindowFullscreen takes a bool, not a flag value. */
	SDL_SetWindowFullscreen(screen, (videoFlags & SDL_FULLSCREEN) != 0);

	zoom = videoFlags & SDL_FULLSCREEN ? 1 : wmzoom;

	/* leaving fullscreen: don't rely on SDL/the window manager to restore
	   the pre-fullscreen window size on their own -- found 2026-10-10 not
	   to happen reliably (KDE/KWin), landing back at 1x instead of whatever
	   zoom (e.g. 2x/3x) the window was at before. Set it explicitly, same
	   as sysvid_zoom() does for a normal zoom change -- to winW/winH (the
	   last real windowed size, kept current by sysvid_notifyResized() and
	   by sysvid_zoom() itself) rather than recomputing fb_width*zoom, so a
	   plain drag-resize the user did (not through the zoom keys) is honoured
	   too, not just overwritten back to the last integer zoom step. */
	if (!(videoFlags & SDL_FULLSCREEN))
		SDL_SetWindowSize(screen, winW, winH);

	sysvid_setDisplayPalette();
	sysvid_update(&SCREENRECT); /* repaint all */ /* FIXME */

	// fixme what shall we do with this
//IFDEBUG_VIDEO2(
//	sys_printf("xrick/video: mode: %dx%d %dbpp pitch=%d %s\n",
//		screen->w, screen->h, screen->format->BitsPerPixel, screen->pitch,
//		videoFlags & SDL_FULLSCREEN ? "fullscreen" : "");
//);

}



/*
 * sysvid_setGamma
 *
 * sets a "gamma" indication ranging from 0 (dark) to 255 (normal).
 */
void sysvid_setGamma(U8 g)
{
	// FIXME changing the GAMMA without changing the PALETTE just CANNOT WORK if GAMMA is not HARDWARE?
	vid_gamma = g;
	sysvid_setDisplayPalette();

#ifdef BPP8
	//SDL_SetColors(screen, (SDL_Color*)& pald, 0, paln);
#endif
}



/* eof */




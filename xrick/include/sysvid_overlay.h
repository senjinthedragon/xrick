/*
 * xrick/include/sysvid_overlay.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * CPU-side RGBA pixel drawing shared by both render backends: the OSD
 * (F10/F11/F12 confirmation messages) and the overlay primitives the
 * settings menu (menu.c) draws itself with. Pure buffer manipulation,
 * no GPU/GL calls -- sysvid_gpu.c and sysvid_gl.c each own a composited
 * RGBA8 frame buffer and register it here with sysvid_overlay_init()
 * before calling anything else below.
 */

#ifndef _SYSVID_OVERLAY_H
#define _SYSVID_OVERLAY_H

#include "system.h"

extern void sysvid_overlay_init(U32 *pixels, U16 w, U16 h);

extern void sysvid_showOSD(const char *msg);
extern U8 sysvid_osdActive(void);
extern void sysvid_drawOSD(void);

extern void sysvid_overlayRect(int x, int y, int w, int h, U8 r, U8 g, U8 b, U8 a);
extern int sysvid_overlayTextWidth(const char *);
extern void sysvid_overlayText(int x, int y, const char *, U8 r, U8 g, U8 b);

/* same glyphs, alpha-blended into any caller-owned RGBA8 buffer (not just the
   registered overlay target) -- gameselect.c's carousel frame, for one */
extern void sysvid_drawTextAlpha(U32 *pixels, U16 w, U16 h, int x, int y,
	const char *s, U8 r, U8 g, U8 b, U8 alpha);

#endif /* _SYSVID_OVERLAY_H */

/* eof */

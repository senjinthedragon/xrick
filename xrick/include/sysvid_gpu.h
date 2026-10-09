/*
 * xrick/include/sysvid_gpu.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * sysvid_gpu.c-specific API (upscale/CRT/bezel/aspect state and overlay
 * drawing) that a future settings menu and F10/F11/F12 key handling will
 * need -- deliberately separate from sysvid.h (upstream's own minimal
 * palette/window API, untouched) and sysvid_gl.h (the init/present/
 * showImage/shutdown interface sysvid.c actually calls).
 */

#ifndef _SYSVID_GPU_H
#define _SYSVID_GPU_H

#include "system.h"

extern void sysvid_cycleUpscale(void);
extern void sysvid_cycleCrt(void);
extern void sysvid_cycleBezel(void);

extern U8 sysvid_isFullscreen(void);
extern U8 sysvid_getWindowZoom(void);

extern int sysvid_getUpscale(void);
extern void sysvid_setUpscale(int);
extern int sysvid_getCrt(void);
extern void sysvid_setCrt(int);
extern int sysvid_getBezel(void);
extern void sysvid_setBezel(int);
extern int sysvid_getAspect(void);
extern void sysvid_setAspect(int);

extern U8 sysvid_osdActive(void);

extern void sysvid_overlayRect(int x, int y, int w, int h, U8 r, U8 g, U8 b, U8 a);
extern int sysvid_overlayTextWidth(const char *);
extern void sysvid_overlayText(int x, int y, const char *, U8 r, U8 g, U8 b);

#endif /* _SYSVID_GPU_H */

/* eof */

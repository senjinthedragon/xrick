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

#ifndef _SYSEVT_H
#define _SYSEVT_H

#include "system.h"

extern void sysevt_poll(void);
extern void sysevt_wait(void);

/* Senjin's SDL3 port: forget every held input (see sysevt.c) */
extern void sysevt_resetInput(void);

#ifdef __EMSCRIPTEN__
/* mobile web on-screen gameplay touch zones: zoneMask is the OR of (1 << zone) for every
   currently-held zone (1-9, numpad-numbered, 2/5 unused). See sysevt.c. */
extern void touch_setZones(int zoneMask);
#endif

#endif /* _SYSEVT_H */

/* eof */

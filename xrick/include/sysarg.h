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

#ifndef _SYSARG_H
#define _SYSARG_H

#include "system.h"

extern int sysarg_args_period;
extern int sysarg_args_map;
extern int sysarg_args_submap;
extern int sysarg_args_mapset;
extern int sysarg_args_fullscreen;
extern int sysarg_args_zoom;
/* Senjin's SDL3 port: sysvid_gpu.c's renderer options -- defaults (0) are each
 * mode's "none"/off entry; see sysvid_gpu.c's upscaleMode_t/crtMode_t/bezelMode_t
 * enums for the values -upscale/-crt/-bezel select. */
extern int sysarg_args_upscale;
extern int sysarg_args_crt;
extern int sysarg_args_bezel;
extern int sysarg_args_aspect;
#ifdef ENABLE_SOUND
extern int sysarg_args_nosound;
extern int sysarg_args_vol;
extern int sysarg_args_game;
#endif
#ifdef ENABLE_DEMO
extern int sysarg_args_demo;
extern char* sysarg_args_record;
extern char* sysarg_args_trace;
#endif

extern void sysarg_init(int, char**);

#endif /* _SYSARG_H */

/* eof */

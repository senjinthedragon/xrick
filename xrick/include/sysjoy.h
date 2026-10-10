/*
 * xrick/include/sysjoy.h
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

#ifndef _SYSJOY_H
#define _SYSJOY_H

#include <SDL3/SDL.h>

#include "system.h"

#ifdef ENABLE_JOYSTICK
extern void sysjoy_init(void);
extern void sysjoy_shutdown(void);
extern int sysjoy_btn_jump, sysjoy_btn_fire, sysjoy_btn_shoot,
    sysjoy_btn_bomb, sysjoy_btn_pause, sysjoy_btn_menu;
extern void sysjoy_resetDefaults(void);
extern const char *sysjoy_buttonName(int);
extern void sysjoy_added(SDL_JoystickID);
extern U8 sysjoy_removed(SDL_JoystickID);
extern U8 sysjoy_isActive(SDL_JoystickID);
#endif

#endif /* _SYSJOY_H */

/* eof */

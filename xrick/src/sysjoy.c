/*
 * xrick/src/sysjoy.c
 *
 * Copyright (C) 1998-2019 bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include <SDL3/SDL.h>

#include "config.h"

#ifdef ENABLE_JOYSTICK

#include "system.h"
#include "debug.h"

static SDL_Joystick *j = NULL;

void
sysjoy_init(void)
{
	int jcount, i;
	SDL_JoystickID *ids;

	if (!SDL_InitSubSystem(SDL_INIT_JOYSTICK)) {
		IFDEBUG_JOYSTICK(
		    sys_printf("xrick/joystick: can not initialize joystick subsystem\n"););
		return;
	}

	ids = SDL_GetJoysticks(&jcount);
	if (!ids || !jcount) { /* no joystick on this system */
		IFDEBUG_JOYSTICK(sys_printf("xrick/joystick: no joystick available\n"););
		SDL_free(ids);
		return;
	}

	/* use the first joystick that we can open */
	for (i = 0; i < jcount; i++) {
		j = SDL_OpenJoystick(ids[i]);
		if (j)
			break;
	}
	SDL_free(ids);

	/* joystick events are enabled by default in SDL3 */
}

void
sysjoy_shutdown(void)
{
	if (j)
		SDL_CloseJoystick(j);
}

#endif /* ENABLE_JOYSTICK */

/* eof */

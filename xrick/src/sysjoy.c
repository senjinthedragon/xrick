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
#include "sysjoy.h"
#include "debug.h"

/*
 * Gamepad support, via SDL's gamepad API (which normalizes button layouts
 * across controllers). One gamepad is active at a time: the first one found,
 * or the next one available if it's unplugged.
 */
static SDL_Gamepad *pad = NULL;

static void
openFirst(void)
{
	int count, i;
	SDL_JoystickID *ids;

	ids = SDL_GetGamepads(&count);
	if (!ids)
		return;
	for (i = 0; i < count && !pad; i++)
		pad = SDL_OpenGamepad(ids[i]);
	SDL_free(ids);
}

void
sysjoy_init(void)
{
	if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
		IFDEBUG_JOYSTICK(
		    sys_printf("xrick/joystick: can not initialize gamepad subsystem\n"););
		return;
	}

	openFirst();
}

void
sysjoy_added(SDL_JoystickID id)
{
	if (!pad)
		pad = SDL_OpenGamepad(id);
}

/* returns TRUE if the removed gamepad was the active one */
U8
sysjoy_removed(SDL_JoystickID id)
{
	if (!pad || SDL_GetGamepadID(pad) != id)
		return FALSE;

	SDL_CloseGamepad(pad);
	pad = NULL;
	openFirst();
	return TRUE;
}

U8
sysjoy_isActive(SDL_JoystickID id)
{
	return pad && SDL_GetGamepadID(pad) == id;
}

void
sysjoy_shutdown(void)
{
	if (pad)
		SDL_CloseGamepad(pad);
	pad = NULL;
	SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

#endif /* ENABLE_JOYSTICK */

/* eof */

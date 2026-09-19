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

/* remappable action buttons (SDL_GamepadButton values); movement is
 * always the d-pad / left stick */
int sysjoy_btn_jump = SDL_GAMEPAD_BUTTON_SOUTH;
int sysjoy_btn_fire = SDL_GAMEPAD_BUTTON_NORTH;
int sysjoy_btn_shoot = SDL_GAMEPAD_BUTTON_EAST;
int sysjoy_btn_bomb = SDL_GAMEPAD_BUTTON_WEST;
int sysjoy_btn_pause = SDL_GAMEPAD_BUTTON_START;
int sysjoy_btn_menu = SDL_GAMEPAD_BUTTON_BACK;

void
sysjoy_resetDefaults(void)
{
	sysjoy_btn_jump = SDL_GAMEPAD_BUTTON_SOUTH;
	sysjoy_btn_fire = SDL_GAMEPAD_BUTTON_NORTH;
	sysjoy_btn_shoot = SDL_GAMEPAD_BUTTON_EAST;
	sysjoy_btn_bomb = SDL_GAMEPAD_BUTTON_WEST;
	sysjoy_btn_pause = SDL_GAMEPAD_BUTTON_START;
	sysjoy_btn_menu = SDL_GAMEPAD_BUTTON_BACK;
}

/*
 * Human-readable name for a button: face buttons are labelled to match the
 * connected controller (PlayStation or Xbox style), PlayStation names when
 * none is connected.
 */
const char *
sysjoy_buttonName(int button)
{
	static const char *const names[] = {
	    "CROSS", "CIRCLE", "SQUARE", "TRIANGLE", "SELECT", "PS", "START",
	    "L3", "R3", "L1", "R1", "DPAD UP", "DPAD DOWN", "DPAD LEFT", "DPAD RIGHT",
	    "MISC", "PADDLE", "PADDLE", "PADDLE", "PADDLE", "TOUCHPAD"};

	if (button < 0 || button >= (int)(sizeof(names) / sizeof(names[0])))
		return "NONE";

	if (pad && button <= SDL_GAMEPAD_BUTTON_NORTH) {
		switch (SDL_GetGamepadButtonLabel(pad, (SDL_GamepadButton)button)) {
		case SDL_GAMEPAD_BUTTON_LABEL_A:
			return "A";
		case SDL_GAMEPAD_BUTTON_LABEL_B:
			return "B";
		case SDL_GAMEPAD_BUTTON_LABEL_X:
			return "X";
		case SDL_GAMEPAD_BUTTON_LABEL_Y:
			return "Y";
		default:
			break;
		}
	}
	return names[button];
}

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

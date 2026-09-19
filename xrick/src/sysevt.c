/*
 * xrick/src/sysevt.c
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

/*
 * 20021010 SDL_SCANCODE_n replaced by SDL_SCANCODE_Fn because some non-US keyboards
 *          requires that SHIFT be pressed to input numbers.
 */

#include <SDL3/SDL.h>

#include "system.h"
#include "syskbd.h"
#include "sysvid.h"
#include "sysarg.h"
#include "game.h"
#include "debug.h"

#include "control.h"
#include "draw.h"
#include "e_rick.h"
#include "sysjoy.h"
#include "menu.h"
#include "settings.h"

#define SETBIT(x, b) x |= (b)
#define CLRBIT(x, b) x &= ~(b)

static SDL_Event event;

/*
 * Modern controls' dedicated shoot/bomb inputs (keyboard or gamepad): each
 * just synthesizes the fire+up / fire+down combo e_rick.c already looks
 * for. Releasing also resets the shoot debounce, which the classic combo
 * only ever gets by keeping fire held (see e_rick_resetShootDebounce).
 */
static void
setShoot(int down)
{
	if (down) {
		SETBIT(control_status, CONTROL_FIRE | CONTROL_UP);
	} else {
		CLRBIT(control_status, CONTROL_FIRE | CONTROL_UP);
		e_rick_resetShootDebounce();
	}
	control_last = CONTROL_FIRE;
}

static void
setBomb(int down)
{
	if (down) {
		SETBIT(control_status, CONTROL_FIRE | CONTROL_DOWN);
	} else {
		CLRBIT(control_status, CONTROL_FIRE | CONTROL_DOWN);
		e_rick_resetShootDebounce();
	}
	control_last = CONTROL_FIRE;
}

#ifdef ENABLE_JOYSTICK
#define PAD_DEADZONE 12000

/*
 * Gamepad directions come from two sources (d-pad and left stick) that
 * can overlap, so track each, and only ever clear the bits the gamepad
 * itself set -- not ones a held keyboard key is also contributing.
 */
static U8 padStick = 0, padDpad = 0, padJump = 0, padApplied = 0;
static U8 padFire = 0, padShoot = 0, padBomb = 0;

static void
padApplyDirs(void)
{
	U8 want = padStick | padDpad | padJump;

	CLRBIT(control_status, padApplied & ~want);
	SETBIT(control_status, want);
	padApplied = want;
}

static void
padSetBit(U8 *mask, U8 bit, int on)
{
	if (on)
		*mask |= bit;
	else
		*mask &= ~bit;
	padApplyDirs();
}

static void
padSetFire(int down)
{
	if (down)
		SETBIT(control_status, CONTROL_FIRE);
	else
		CLRBIT(control_status, CONTROL_FIRE);
	control_last = CONTROL_FIRE;
	padFire = down;
}

static void
padButton(int button, int down)
{
	int modern = (sysarg_args_controls == CONTROLS_MODERN);

	/* movement is always the d-pad; everything else is remappable */
	if (button == SDL_GAMEPAD_BUTTON_DPAD_UP) {
		padSetBit(&padDpad, CONTROL_UP, down);
		control_last = CONTROL_UP;
	} else if (button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) {
		padSetBit(&padDpad, CONTROL_DOWN, down);
		control_last = CONTROL_DOWN;
	} else if (button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) {
		padSetBit(&padDpad, CONTROL_LEFT, down);
		control_last = CONTROL_LEFT;
	} else if (button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) {
		padSetBit(&padDpad, CONTROL_RIGHT, down);
		control_last = CONTROL_RIGHT;
	} else if (button == sysjoy_btn_jump) {
		padSetBit(&padJump, CONTROL_UP, down);
		control_last = CONTROL_UP;
	} else if (button == sysjoy_btn_fire) {
		padSetFire(down);
	} else if (button == sysjoy_btn_shoot) {
		if (modern) {
			setShoot(down);
			padShoot = down;
		} else {
			padSetFire(down);
		}
	} else if (button == sysjoy_btn_bomb) {
		if (modern) {
			setBomb(down);
			padBomb = down;
		} else {
			padSetFire(down);
		}
	} else if (button == sysjoy_btn_pause) {
		if (down)
			SETBIT(control_status, CONTROL_PAUSE);
		else
			CLRBIT(control_status, CONTROL_PAUSE);
		control_last = CONTROL_PAUSE;
	}
}

/* release everything the active gamepad was holding (it was unplugged) */
static void
padReleaseAll(void)
{
	padStick = padDpad = padJump = 0;
	padApplyDirs();
	if (padShoot)
		setShoot(0);
	if (padBomb)
		setBomb(0);
	if (padFire)
		padSetFire(0);
	padShoot = padBomb = 0;
}
#endif /* ENABLE_JOYSTICK */

/*
 * Forget every held input: used when the settings menu opens or closes,
 * so a key or button released while the menu had the focus can't leave
 * the game thinking it's still held.
 */
void
sysevt_resetInput(void)
{
	control_status = 0;
	control_last = 0;
#ifdef ENABLE_JOYSTICK
	padStick = padDpad = padJump = padApplied = 0;
	padFire = padShoot = padBomb = 0;
#endif
}

/*
 * Process an event
 */
static void
processEvent()
{
	U16 key;

	/* the settings menu (Escape / gamepad menu button) gets first pick */
	if (menu_handleEvent(&event))
		return;

	switch (event.type) {
	case SDL_EVENT_KEY_DOWN:
		key = event.key.scancode;
		if (key == syskbd_up || key == SDL_SCANCODE_UP) {
			SETBIT(control_status, CONTROL_UP);
			control_last = CONTROL_UP;
		} else if (key == syskbd_down || key == SDL_SCANCODE_DOWN) {
			SETBIT(control_status, CONTROL_DOWN);
			control_last = CONTROL_DOWN;
		} else if (key == syskbd_left || key == SDL_SCANCODE_LEFT) {
			SETBIT(control_status, CONTROL_LEFT);
			control_last = CONTROL_LEFT;
		} else if (key == syskbd_right || key == SDL_SCANCODE_RIGHT) {
			SETBIT(control_status, CONTROL_RIGHT);
			control_last = CONTROL_RIGHT;
		} else if (key == syskbd_pause) {
			SETBIT(control_status, CONTROL_PAUSE);
			control_last = CONTROL_PAUSE;
		} else if (key == syskbd_end) {
			SETBIT(control_status, CONTROL_END);
			control_last = CONTROL_END;
		} else if (key == syskbd_fire) {
			SETBIT(control_status, CONTROL_FIRE);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_shoot) {
			setShoot(1);
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_bomb) {
			setBomb(1);
		} else if (key == SDL_SCANCODE_F1) {
			sysvid_toggleFullscreen();
		} else if (key == SDL_SCANCODE_F2) {
			sysvid_zoom(-1);
		} else if (key == SDL_SCANCODE_F3) {
			sysvid_zoom(+1);
		}
#ifdef ENABLE_SOUND
		else if (key == SDL_SCANCODE_F4) {
			syssnd_toggleMute();
		} else if (key == SDL_SCANCODE_F5) {
			syssnd_vol(-1);
		} else if (key == SDL_SCANCODE_F6) {
			syssnd_vol(+1);
		}
#endif
		else if (key == SDL_SCANCODE_F7) {
			game_toggleCheat(1);
		} else if (key == SDL_SCANCODE_F8) {
			game_toggleCheat(2);
		} else if (key == SDL_SCANCODE_F9) {
			game_toggleCheat(3);
		} else if (key == SDL_SCANCODE_F10) {
			sysvid_cycleUpscale();
		} else if (key == SDL_SCANCODE_F11) {
			sysvid_cycleCrt();
		} else if (key == SDL_SCANCODE_F12) {
			sysvid_cycleBezel();
		}
		if ((key >= SDL_SCANCODE_F1 && key <= SDL_SCANCODE_F6) || (key >= SDL_SCANCODE_F10 && key <= SDL_SCANCODE_F12))
			settings_save();
		break;
	case SDL_EVENT_KEY_UP:
		key = event.key.scancode;
		if (key == syskbd_up || key == SDL_SCANCODE_UP) {
			CLRBIT(control_status, CONTROL_UP);
			control_last = CONTROL_UP;
		} else if (key == syskbd_down || key == SDL_SCANCODE_DOWN) {
			CLRBIT(control_status, CONTROL_DOWN);
			control_last = CONTROL_DOWN;
		} else if (key == syskbd_left || key == SDL_SCANCODE_LEFT) {
			CLRBIT(control_status, CONTROL_LEFT);
			control_last = CONTROL_LEFT;
		} else if (key == syskbd_right || key == SDL_SCANCODE_RIGHT) {
			CLRBIT(control_status, CONTROL_RIGHT);
			control_last = CONTROL_RIGHT;
		} else if (key == syskbd_pause) {
			CLRBIT(control_status, CONTROL_PAUSE);
			control_last = CONTROL_PAUSE;
		} else if (key == syskbd_end) {
			CLRBIT(control_status, CONTROL_END);
			control_last = CONTROL_END;
		} else if (key == syskbd_fire) {
			CLRBIT(control_status, CONTROL_FIRE);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_shoot) {
			setShoot(0);
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_bomb) {
			setBomb(0);
		}
		break;
	case SDL_EVENT_QUIT:
		/* player tries to close the window -- this is the same as pressing ESC */
		SETBIT(control_status, CONTROL_EXIT);
		control_last = CONTROL_EXIT;
		break;
#ifdef ENABLE_FOCUS
	case SDL_EVENT_WINDOW_FOCUS_GAINED:
		control_active = TRUE;
		break;
	case SDL_EVENT_WINDOW_FOCUS_LOST:
		control_active = FALSE;
		break;
#endif
#ifdef ENABLE_JOYSTICK
	case SDL_EVENT_GAMEPAD_ADDED:
		sysjoy_added(event.gdevice.which);
		break;
	case SDL_EVENT_GAMEPAD_REMOVED:
		if (sysjoy_removed(event.gdevice.which))
			padReleaseAll();
		break;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (!sysjoy_isActive(event.gaxis.which))
			break;
		if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTX) {
			padStick &= ~(CONTROL_LEFT | CONTROL_RIGHT);
			if (event.gaxis.value < -PAD_DEADZONE)
				padStick |= CONTROL_LEFT;
			else if (event.gaxis.value > PAD_DEADZONE)
				padStick |= CONTROL_RIGHT;
			padApplyDirs();
		} else if (event.gaxis.axis == SDL_GAMEPAD_AXIS_LEFTY) {
			padStick &= ~(CONTROL_UP | CONTROL_DOWN);
			if (event.gaxis.value < -PAD_DEADZONE)
				padStick |= CONTROL_UP;
			else if (event.gaxis.value > PAD_DEADZONE)
				padStick |= CONTROL_DOWN;
			padApplyDirs();
		}
		break;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		if (sysjoy_isActive(event.gbutton.which))
			padButton(event.gbutton.button, event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN);
		break;
#endif
	default:
		break;
	}
}

/*
 * Process events, if any, then return
 */
void
sysevt_poll(void)
{
	while (SDL_PollEvent(&event))
		processEvent();
}

/*
 * Wait for an event, then process it and return
 */
void
sysevt_wait(void)
{
	/* Block indefinitely as usual, except while the OSD's few-second
	 * message is still counting down: bound the wait so the game loop
	 * keeps ticking (and sysvid_update() keeps checking the OSD timer)
	 * even on an otherwise-static screen like a menu or Hall of Fame,
	 * where nothing else would wake this loop up before the next real
	 * input event -- which could be much later than 5 seconds away. */
	if (sysvid_osdActive()) {
		if (SDL_WaitEventTimeout(&event, 100))
			processEvent();
	} else {
		SDL_WaitEvent(&event);
		processEvent();
	}
}

/* eof */

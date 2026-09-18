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

#define SYSJOY_RANGE 3280

#define SETBIT(x, b) x |= (b)
#define CLRBIT(x, b) x &= ~(b)

static SDL_Event event;

/*
 * Process an event
 */
static void
processEvent()
{
	U16 key;

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
		} else if (key == syskbd_xtra) {
			SETBIT(control_status, CONTROL_EXIT);
			control_last = CONTROL_EXIT;
		} else if (key == syskbd_fire) {
			SETBIT(control_status, CONTROL_FIRE);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_shoot) {
			/* dedicated shoot key: same fire+up combo e_rick.c already
			 * looks for, just synthesized from one key instead of two */
			SETBIT(control_status, CONTROL_FIRE | CONTROL_UP);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_bomb) {
			SETBIT(control_status, CONTROL_FIRE | CONTROL_DOWN);
			control_last = CONTROL_FIRE;
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
		} else if (key == syskbd_xtra) {
			CLRBIT(control_status, CONTROL_EXIT);
			control_last = CONTROL_EXIT;
		} else if (key == syskbd_fire) {
			CLRBIT(control_status, CONTROL_FIRE);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_shoot) {
			CLRBIT(control_status, CONTROL_FIRE | CONTROL_UP);
			control_last = CONTROL_FIRE;
		} else if (sysarg_args_controls == CONTROLS_MODERN && key == syskbd_bomb) {
			CLRBIT(control_status, CONTROL_FIRE | CONTROL_DOWN);
			control_last = CONTROL_FIRE;
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
	case SDL_EVENT_JOYSTICK_AXIS_MOTION:
		IFDEBUG_EVENTS(sys_printf("xrick/events: joystick\n"););
		if (event.jaxis.axis == 0) {			 /* left-right */
			if (event.jaxis.value < -SYSJOY_RANGE) { /* left */
				SETBIT(control_status, CONTROL_LEFT);
				CLRBIT(control_status, CONTROL_RIGHT);
			} else if (event.jaxis.value > SYSJOY_RANGE) { /* right */
				SETBIT(control_status, CONTROL_RIGHT);
				CLRBIT(control_status, CONTROL_LEFT);
			} else { /* center */
				CLRBIT(control_status, CONTROL_RIGHT);
				CLRBIT(control_status, CONTROL_LEFT);
			}
		}
		if (event.jaxis.axis == 1) {			 /* up-down */
			if (event.jaxis.value < -SYSJOY_RANGE) { /* up */
				SETBIT(control_status, CONTROL_UP);
				CLRBIT(control_status, CONTROL_DOWN);
			} else if (event.jaxis.value > SYSJOY_RANGE) { /* down */
				SETBIT(control_status, CONTROL_DOWN);
				CLRBIT(control_status, CONTROL_UP);
			} else { /* center */
				CLRBIT(control_status, CONTROL_DOWN);
				CLRBIT(control_status, CONTROL_UP);
			}
		}
		break;
	case SDL_EVENT_JOYSTICK_BUTTON_DOWN:
		SETBIT(control_status, CONTROL_FIRE);
		break;
	case SDL_EVENT_JOYSTICK_BUTTON_UP:
		CLRBIT(control_status, CONTROL_FIRE);
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

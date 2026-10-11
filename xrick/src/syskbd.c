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

#include <SDL3/SDL.h>

#include "system.h"

/*
 * Using the SDL_SCANCODE_xxx keysyms, which map to a QWERTY keyboard.
 * We get them via SDL_KEYDOWN.
 * We do *not* use SDL_TEXTINPUT nor SDLK_ to get true key mappings, so
 * for instance a binding to 'q' is the key labelled 'a' on an AZERTY keyboard.
 */

U8 syskbd_up = SDL_SCANCODE_O;
U8 syskbd_down = SDL_SCANCODE_K;
U8 syskbd_left = SDL_SCANCODE_S;
U8 syskbd_right = SDL_SCANCODE_D;
U8 syskbd_pause = SDL_SCANCODE_P;
U8 syskbd_end = SDL_SCANCODE_E;
U8 syskbd_xtra = SDL_SCANCODE_ESCAPE;
U8 syskbd_fire = SDL_SCANCODE_SPACE;
/* Senjin's SDL3 port: modern controls' dedicated shoot/bomb keys (see
 * control.h). Z/X rather than master's A/S, since this tree's left/right
 * already sit on S/D. */
U8 syskbd_shoot = SDL_SCANCODE_Z;
U8 syskbd_bomb = SDL_SCANCODE_X;
/* rd2 only: the ST's S key ($1f), debug sound-id remap [$1a5ce]. Not F10
 * (the original choice, made before F10/F11/F12 became the renderer's
 * upscale/CRT/bezel cycle hotkeys, user decision 2026-10-10) to avoid
 * colliding with those, and not an F key at all any more once TAB became
 * the settings menu's hotkey (2026-10-11): deliberately an obscure key
 * (backtick/grave), not one someone trying the F keys might hit by
 * accident and get confused by (user decision 2026-10-11). */
U8 syskbd_sndset = SDL_SCANCODE_GRAVE;

/*
 * Senjin's SDL3 port: resets the menu's remappable keys (left/right/up/down/
 * fire/shoot/bomb/pause) to their defaults -- end/xtra/sndset aren't exposed
 * to remapping, so they're left untouched.
 */
void
syskbd_resetDefaults(void)
{
	syskbd_up = SDL_SCANCODE_O;
	syskbd_down = SDL_SCANCODE_K;
	syskbd_left = SDL_SCANCODE_S;
	syskbd_right = SDL_SCANCODE_D;
	syskbd_pause = SDL_SCANCODE_P;
	syskbd_fire = SDL_SCANCODE_SPACE;
	syskbd_shoot = SDL_SCANCODE_Z;
	syskbd_bomb = SDL_SCANCODE_X;
}

/* eof */



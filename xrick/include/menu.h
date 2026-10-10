/*
 * xrick/include/menu.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * The in-game settings menu: opened with Escape (or the gamepad's menu
 * button), pauses the game, and draws itself over the frame. Shared by
 * both rd1 and rd2 (sysarg_args_game picks which audio/cheat API the
 * GAME/AUDIO pages dispatch to -- see menu.c).
 */

#ifndef _MENU_H
#define _MENU_H

#include <SDL3/SDL.h>

#include "system.h"

extern U8 menu_active(void);
extern void menu_draw(void);

/* returns TRUE if the menu consumed the event (the game shouldn't see it) */
extern U8 menu_handleEvent(const SDL_Event *);

#endif /* _MENU_H */

/* eof */

/*
 * xrick/include/menu.h
 *
 * The in-game settings menu: opened with Escape (or the gamepad's menu
 * button), pauses the game, and draws itself over the frame.
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

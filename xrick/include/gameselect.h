/*
 * xrick/include/gameselect.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * The game select screen: a diskette carousel (RD1/RD2), shown instead of the
 * usual splash when xrick is launched with no -game argument (xrick.c). Chooses
 * sysarg_args_game, same effect as if -game had been passed on the command line.
 *
 * Same start/done/run split as splash.c, for the same reason (the web build can't
 * block before its own main loop starts): gameselect_done() both checks completion
 * and advances one tick (input, animation, draw, present), so it must be called
 * every frame; gameselect_run() is the native blocking loop around that.
 */

#ifndef _GAMESELECT_H
#define _GAMESELECT_H

#include "system.h"

extern void gameselect_start(void);
extern U8 gameselect_done(void);
extern void gameselect_run(void);

#endif

/* eof */

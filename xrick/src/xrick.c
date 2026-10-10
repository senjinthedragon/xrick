/*
 * XRICK
 *
 * Copyright (C) 1998-NOW bigorno (bigorno@bigorno.net). All rights reserved.
 *
 * The use and distribution terms for this software are contained in the file
 * named README, which can be found in the root of this distribution. By
 * using this software in any fashion, you are agreeing to be bound by the
 * terms of this license.
 *
 * You must not remove this notice, or any other, from this software.
 */

#include "system.h"
#include "sysarg.h"
#include "sysvid.h"
#include "game.h"
#include "fb.h"
#include "rd2_game.h"
#include "rd2_snd.h"
#include "settings.h"
#include "sysjoy.h"
#include "gameselect.h"

#include <SDL3/SDL.h>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif
#include <signal.h>
#include <stdlib.h> /* atexit, exit -- SDL2's SDL.h pulled this in transitively, SDL3's doesn't */
#ifdef __WIN32__
#include <windows.h>
#endif


/*
 * Sets a console, if possible
 */
/* review-plan.md R0.1: implicit int return is a hard error in modern C. Windows-only
   body; build portability only. */
static void setConsole(void)
{
	// NOTE: does not handle parent process console being redirected
	// eg "./xrick > stdout.txt" still writes to the actual console

#ifdef __WIN32__
	// try to attach to parent process console
	if (AttachConsole(-1))
	{
		// reopen (SDL2 closed everything)
		freopen("CONIN$", "r", stdin);
		freopen("CONOUT$", "w", stdout);
		freopen("CONOUT$", "w", stderr);
	}
#endif
}



 /*
  * Initialize system
  */
void
sys_init(int argc, char** argv)
{
	setConsole();

	/* saved settings first, so command-line options override them. On the web
	   build this reads from the IDBFS-backed /libsdl mount player.js syncs from
	   IndexedDB before calling Module.callMain(), so the file is already there
	   by the time this runs synchronously -- see player.js's setupIDBFS(). */
	settings_load();
	sysarg_init(argc, argv);

	// FIXME not writing to stdxxx.txt files anymore?
	// SDL3 dropped SDL_INIT_TIMER (timers are always available) and SDL_INIT_EVENTS
	// (implied by SDL_INIT_VIDEO); SDL_Init now returns bool, true on success.
#ifdef __EMSCRIPTEN__
	// the build links with -sASYNCIFY for RD2 (rd2_sys_pump sleeps). With it SDL would also
	// sleep on its own (SDL_Delay, and every buffer swap), including inside RD1's
	// emscripten_set_main_loop callback, and build.sh instruments only the functions that
	// reach xrick's own emscripten_sleep calls. So SDL must never sleep.
	SDL_SetHint(SDL_HINT_EMSCRIPTEN_ASYNCIFY, "0");
#endif
	if (!SDL_Init(SDL_INIT_VIDEO))
		sys_panic("xrick/video: could not init SDL\n");

	// FIXME logging
	// Solved (embarrassingly simple) :
	// SDL 1.2 did that outputting into files, SDL 2 does not.However most information(toturials and such) on the net is about SDL 1.2 since SDL 2 is new.
	// Furthermore compiling with -mwindows sends all stdout and stderr to null.
	// Compiling without solves my problem.

	//SDL_Log("SDL!");

	sysvid_init(FB_WIDTH, FB_HEIGHT);
#ifdef ENABLE_JOYSTICK
	sysjoy_init();
#endif

	atexit(sys_shutdown);
	signal(SIGINT, exit);
	signal(SIGTERM, exit);
}

/*
 * sys_initSound
 *
 * picks and initializes the chosen game's own sound engine (rd1's syssnd or
 * rd2's rd2_snd), then applies the saved volume/mute to it. Split out of
 * sys_init() because sysarg_args_game isn't final until main() either reads
 * -game explicitly or the carousel (gameselect.c) picks one -- sys_init()
 * runs before either, so calling this from there always saw sysarg_args_game
 * at whatever the command line left it (0, same as RD1), meaning the carousel
 * picking RD2 never ran rd2_snd_init() at all: no engine, no sound. main()
 * calls this once sysarg_args_game is actually settled. Found 2026-10-10.
 */
void
sys_initSound(void)
{
#ifdef ENABLE_SOUND
	if (sysarg_args_nosound == 0) {
		if (sysarg_args_game == 2)
			rd2_snd_init();
		else
			syssnd_init();
	}
#endif
	settings_apply();
}



/*
 * Shutdown system
 */
void
sys_shutdown(void)
{
#ifdef ENABLE_SOUND
	if (sysarg_args_game == 2)
		rd2_snd_shutdown();
	else
		syssnd_shutdown();
#endif
#ifdef ENABLE_JOYSTICK
	sysjoy_shutdown();
#endif
	sysvid_shutdown();

	SDL_Quit();
}



/*
 * main
 */
int
main(int argc, char *argv[])
{
	sys_init(argc, argv);

	/* no -game given: the diskette carousel picks one instead -- it's interactive,
	   so it always needs its own loop (gameselect.c), both natively and on the web
	   (emscripten_sleep there, same yield-to-the-browser trick RD2's own main loop
	   already relies on). Either way, nothing sits in front of the chosen game's own
	   title/attract sequence any more -- see menu.c's CREDITS page for the old
	   splash's text, now reachable from the settings menu instead of forced on
	   every launch. */
	if (!sysarg_args_game_explicit)
		gameselect_run();

	sys_initSound(); /* sysarg_args_game is final now either way -- see sys_initSound() */

	if (sysarg_args_game == 2)
		rd2_game_run();
	else
		game_run();

	sys_shutdown();
	return 0;
}

/* eof */

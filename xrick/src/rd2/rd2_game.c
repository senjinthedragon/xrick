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

/*
 * Rick Dangerous 2 -- boot ($10000) and game_main ($10992-$10c22), RAM model.
 * Transcribed from the disassembly (read 2026-09-24) and algo-flow.md §2/§6/§8/§13.
 * game_main is ONE function with gotos, like the original's flat labels; one frame
 * ($10a54-$10bec) is rd2_frame_step, for xrick-core. MAPDONE is the literal
 * original, map 5 included (user decision 2026-09-24, port-rd2.md §7).
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h> /* SDL_GetPrefPath, for rd2_hofLoad/Save -- see settings.c's own filePath() */

#include "sysarg.h"
#include "rd2_mem.h"
#include "rd2_sys.h"
#include "rd2_game.h"
#include "rd2_snd.h" /* rd2_snd_rw: rd2_boot()'s hadLoop check */
#include "demo.h"

#ifdef ENABLE_DEMO
/* host: true if the run now ending was driven by AUTOPLAY/-demo, so
   END_OF_RUN can skip the high-score name entry the same way it already
   does for the game's own built-in attract demo (RD2_DEMO). */
static U8 demo_wasActive;
#endif

#define RD2_HOF_BASE 0x17d0eu
#define RD2_HOF_LEN (8 * 0x1eu) /* 8 entries x 0x1e bytes -- rd2_17f22, rd2_flow.c */

static char *
rd2_hofPath(void)
{
	char *dir = SDL_GetPrefPath("xrick", "xrick");
	char *path;
	size_t n;

	if (!dir) return NULL;
	n = strlen(dir) + strlen("hiscores_rd2.dat") + 1;
	path = (char *)malloc(n);
	if (path) snprintf(path, n, "%shiscores_rd2.dat", dir);
	SDL_free(dir);
	return path;
}

/*
 * rd2_hofLoad / rd2_hofSave
 *
 * STORE HIGHSCORES (settings menu, GAME page; Senjin 2026-10-12), off by
 * default -- see game_hscoresLoad/Save's own comment (rd1/game.c) for the
 * rationale shared with RD1. RD2's hall of fame lives in emulated ST
 * memory ($17d0e, 8 entries x $1e bytes -- rd2_17f22, rd2_flow.c); copied
 * here as a raw byte range rather than decoded field by field, since
 * nothing here needs to understand its internal layout, only preserve it
 * exactly. Load is called once from rd2_boot() below; save from
 * rd2_17f22() itself, right after it inserts a new entry.
 */
void
rd2_hofLoad(void)
{
	char *path;
	FILE *f;
	U8 buf[RD2_HOF_LEN];
	U32 i;

	if (!sysarg_args_storeHighscores) return;
	path = rd2_hofPath();
	if (!path) return;
	f = fopen(path, "rb");
	free(path);
	if (!f) return;
	if (fread(buf, sizeof buf, 1, f) == 1)
		for (i = 0; i < RD2_HOF_LEN; i++)
			rd2_wb(RD2_HOF_BASE + i, buf[i]);
	fclose(f);
}

void
rd2_hofSave(void)
{
	char *path;
	FILE *f;
	U8 buf[RD2_HOF_LEN];
	U32 i;

	if (!sysarg_args_storeHighscores) return;
	path = rd2_hofPath();
	if (!path) return;
	f = fopen(path, "wb");
	free(path);
	if (!f) return;
	for (i = 0; i < RD2_HOF_LEN; i++)
		buf[i] = rd2_rb(RD2_HOF_BASE + i);
	fwrite(buf, sizeof buf, 1, f);
	fclose(f);
}

/* boot $10000 (algo-flow.md §13). TOS save ($18f26), MFP/vector setup and the IKBD
   commands are hardware; their RAM writes are only read by the exit path. */
static void
rd2_boot(void)
{
	/* host: rd2_123ca() below -- authentic, the real game's own "silent load"
	   reset -- unconditionally stops all sound (rd2_1a5d0()) every time this
	   runs, including the carousel's handoff where music (and the confirm
	   scream) may already be live. A genuinely fresh, non-carousel launch has
	   nothing playing yet (this reads 0 for that path, same as always), so
	   only the handoff case is affected: remember whether the loop was
	   already going, so it can be resumed right after, instead of silently
	   staying stopped until TITLE's rd2_178dc() notices and replays the
	   ~6-chord sting from scratch (reported 2026-10-11 despite gameselect.c
	   polling right up to the handoff -- this reset runs after that, so no
	   amount of polling beforehand could have caught it). */
	U8 hadLoop = rd2_snd_rw(0x1aa08) != 0;

	rd2_mem_init();                 /* program at $f8b8; $53400-$7ffff zero ($18fbe) */
	rd2_hofLoad();                  /* host: STORE HIGHSCORES, after mem_init zeroes it */
	rd2_sys_init();
	rd2_sys_setbase(0x07, 0x80);    /* $18ffc: Setscreen($78000, $78000, 0) */
	rd2_ww(0x18ed8, 2);             /* $1901a */
	rd2_19106();                    /* palette $18ee6 */
	rd2_ww(RD2_MAP_PLAYING, 1);
	rd2_123ca();                    /* silent map-1 load */
	if (hadLoop)
		rd2_1a6aa(0, 1);         /* host: resume -- see above */
}

/* one game_main frame, $10a54 to the `bra` back at $10bec: deaths and submap slides
   complete inside it. Returns where game_main goes next (RD2_STEP_FRAME: the next
   frame). Split out of rd2_game_run for xrick-core (PLAN.md T47 phase 2); the code
   is unchanged. */
int
rd2_frame_step(void)
{
	rd2_ww(RD2_SHOT_HIT, 0);                    /* $10a54 */
	rd2_14594();
	rd2_15bc0();
	rd2_14d48();
	rd2_150a2();
	rd2_15826();
	rd2_13096();
	rd2_13e14();
	rd2_13e98();
	if (rd2_rw(RD2_SHOT_HIT) != 0)              /* $10a8c */
		rd2_ww(0x16902, 0);
	rd2_16658();
	rd2_18dac();
	rd2_18782();
	rd2_170b6();
	rd2_177a8();
	rd2_19216();
	rd2_191e6();
	rd2_14362();

	if (rd2_rw(RD2_MAPDONE) != 0)               /* $10acc */
		return RD2_STEP_MAPDONE;

	/* $10b2a: S key */
	if (rd2_rw(RD2_DEMO) == 0 && rd2_rb(RD2_KEY) == 0x1f) {
		if (rd2_rw(RD2_SKEY_LATCH) == 0) {
			rd2_ww(RD2_SKEY_LATCH, 0xffff);
			rd2_1a5d0();
			rd2_ww(RD2_ID_REMAP, (U16)(rd2_rw(RD2_ID_REMAP) ^ 0xffff));
		}
	} else {
		rd2_ww(RD2_SKEY_LATCH, 0);              /* $10b5e */
	}

	if (rd2_rw(RD2_DEMO) != 0) {                /* $10b66 */
		if (rd2_rw(RD2_DEMO_ENDED) != 0)
			return RD2_STEP_END_OF_RUN;
		if (rd2_rb(RD2_JOY) & 0x80) {
			rd2_ww(RD2_DEMO, 0);
			return RD2_STEP_PICK;
		}
	} else if (rd2_rb(RD2_KEY) == 0x01) {       /* $10b90: ESC */
		rd2_1a5d0();
		return RD2_STEP_TITLE;
	} else if (rd2_rb(RD2_KEY) == 0x19) {       /* $10ba6: P, pause */
		/* host: rd1's pause (PAUSED box, P again resumes); the ST draws nothing and resumes on
		   fire: do $191e6 while !(btst #7,[$1a4fb]) */
		rd2_sys_paused(1);
		do rd2_191e6(); while (rd2_sys_pausekey());     /* P released */
		do rd2_191e6(); while (!rd2_sys_pausekey());    /* P pressed again */
		do rd2_191e6(); while (rd2_sys_pausekey());     /* P released */
		rd2_sys_paused(0);
		rd2_191e6();
	} else if (rd2_sys_endreq()) {              /* host: rd1's E ends the game (not in the original) */
		rd2_1a5d0();
		return RD2_STEP_END_OF_RUN;
	}

	if (rd2_rw(RD2_RICK_DEAD) != 0 && rd2_rws(RD2_RICK_Y) >= 0x100) {   /* $10bc6 */
		if (rd2_rw(RD2_LIVES) == 0)
			return RD2_STEP_END_OF_RUN;
		rd2_149c2();
		rd2_142fc();
	}
	return RD2_STEP_FRAME;
}

void
rd2_game_run(void)
{
	rd2_boot();
	rd2_demo_init();                            /* host: -demo / -record */

	/* $10992: rte at $19044, MFP/vector restore -- hardware */
	rd2_wb(RD2_JOY, 0);                         /* $10a12 */

TITLE:
	rd2_demo_stop();                            /* host: demo playback over */
	rd2_sys_info(0);                            /* host: map/submap numbers off */
	if (sysarg_args_mapset) {                   /* host: -map N, once: as if N were picked */
		sysarg_args_mapset = 0;                 /* on SELECT LEVEL. MAPDONE reads the choice */
		rd2_ww(RD2_PICKER_CHOICE, (U16)(sysarg_args_map + 1));  /* (map 4 ends the run unless 1) */
		goto PICKED;
	}
	rd2_178dc();                                /* $10a18 */
	if (rd2_demo_autostart()) {                 /* host: -demo with a solved game (PLAN.md T47): */
		rd2_ww(RD2_DEMO, 0);                    /* after the title, a real game on map 1 */
		rd2_ww(RD2_PICKER_CHOICE, 1);           /* instead of the original attract demo */
		goto PICKED;
	}
PICK:
	rd2_17a46();                                /* $10a1e */
PICKED:
	rd2_1771c();                                /* $10a24 */
	rd2_123a0();                                /* $10a2a */
LOAD:
	rd2_sys_info(0);                            /* host: off during the loading banner */
	rd2_19388();                                /* $10a30 */
	rd2_dbg_load();                             /* debug only (env RD2_FORCE_MAP) */
	rd2_123b0();                                /* $10a36 */
	rd2_17760();                                /* $10a3c */
	rd2_ww(RD2_MAPDONE, 0);                     /* $10a42 */
	rd2_19388();                                /* $10a48 */
	rd2_142a0();                                /* $10a4e */
	rd2_demo_level();                           /* host: demo segment = this map */
	rd2_sys_info(1);                            /* host: rd1's map/submap numbers */
FRAME:
	rd2_dbg_frame();                            /* debug only (env RD2_TRACE) */
	rd2_demo_frame();                           /* host: play or record [$1a4fb] */
	switch (rd2_frame_step()) {
	case RD2_STEP_MAPDONE:    goto MAPDONE;
	case RD2_STEP_END_OF_RUN: goto END_OF_RUN;
	case RD2_STEP_TITLE:      goto TITLE;
	case RD2_STEP_PICK:       goto PICK;
	default:                  goto FRAME;
	}

MAPDONE:                                        /* $10ad4 */
	/* host: both branches below used to fall into NEXT (-> LOAD -> rd2_123ca()),
	   which tries to load "map 5" -- never a real level on the original disk,
	   its file descriptor's checksum never satisfies the loader, so the
	   original game's own loader hangs forever with the border set to bright
	   red trying to load it ($11fb8 -- a genuine, documented bug in the
	   shipped 1990 game, not something this port introduced; see rd2_load.c's
	   own header comment). $10afc's own MAP_PLAYING==5 branch below is where
	   that (never actually reachable) successful load would have ended up
	   anyway, so both go straight there instead -- same destination, same
	   extra life for finishing the level (rd2_17782(), as NEXT would have
	   granted), just without the broken detour through the loader. Senjin's
	   decision 2026-10-12: patch this so finishing the game actually ends it,
	   rather than preserve the authentic hang. */
	if (rd2_rw(RD2_MAP_PLAYING) == 4) {
		if (rd2_rw(RD2_PICKER_ROWS) == 5) {
			rd2_17782();
			rd2_ww(RD2_MAP_PLAYING, 5);
			goto ENDING;
		}
		if (rd2_rw(RD2_PICKER_CHOICE) == 1) {
			rd2_ww(RD2_PICKER_ROWS, 5);         /* $10af2 */
			rd2_17782();
			rd2_ww(RD2_MAP_PLAYING, 5);
			goto ENDING;
		}
		rd2_17bf4();                            /* $10b12 */
		goto END_OF_RUN;
	}
	if (rd2_rw(RD2_MAP_PLAYING) == 5) {         /* $10afc */
		if (rd2_rw(RD2_PICKER_CHOICE) == 1)
			goto ENDING;
		rd2_17bf4();
		goto END_OF_RUN;
	}
	/* $10b1a: worlds 1-3 finishing fall through here -- world 4 no longer
	   does (see the comment above), so nothing gotos this by name any more;
	   left unlabeled to avoid an unused-label warning. */
	rd2_ww(RD2_MAP_PLAYING, (U16)(rd2_rw(RD2_MAP_PLAYING) + 1));
	rd2_17782();
	goto LOAD;

ENDING:                                         /* $10bf2 */
	/* bsr $10c28: MFP save + computed returns, traced in algo-flow.md §8; RAM effects: */
	rd2_ww(0x1164a, 0x4e71);
	rd2_wb(RD2_JOY, 0);
	rd2_1789a();
	rd2_17bda();

END_OF_RUN:                                     /* $10c00 */
#ifdef ENABLE_DEMO
	/* host: capture before clearing it, and clear unconditionally here rather
	   than through rd2_demo_stop() (gated on a script being loaded, which is
	   false if this run ended before one was -- e.g. the menu's AUTOPLAY
	   force-ending it right away). rd2_demo_resumeAutoplay() then restores it
	   if that's exactly what happened (AUTOPLAY's rd2_demo_rearm(), not a
	   real demo naturally finishing) -- the demo_active=TRUE the menu set to
	   drive this END_OF_RUN was otherwise wiped out here before
	   rd2_demo_autostart() ever got a chance to see it TRUE at TITLE, so
	   AUTOPLAY silently never started a run (found 2026-10-10). */
	demo_wasActive = demo_active;
	demo_end();
	rd2_demo_resumeAutoplay();
	rd2_sys_joyresync();                        /* host: keyboard for game over / name entry */
#else
	rd2_demo_stop();                            /* host: keyboard for game over / name entry */
#endif
	rd2_sys_info(0);
	if (rd2_rw(RD2_DEMO) != 0
#ifdef ENABLE_DEMO
	    || demo_wasActive                       /* host: skip name entry for AUTOPLAY/-demo too */
#endif
	    || rd2_cheat_trainer || rd2_cheat_invincible || rd2_cheat_highlight /* host: or any cheat */
	    ) {
		rd2_ww(RD2_DEMO, 0);
		goto TITLE;
	}
	rd2_17c06();
	rd2_17f22();
	goto TITLE;
}

/* eof */

/*
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * A cheap "coming out of a small CRT's built-in speaker, on a desk a few feet
 * away" coloration for the game-select carousel's music: a bit of bass
 * rolloff (tiny speakers have none), a bit of treble rolloff (not hi-fi), and
 * a short, quiet comb-filter reverb standing in for the room/cabinet, not
 * meant to be heard as "reverb" so much as a boxy, speaker-constrained
 * character. Applied as the very last step before a sample leaves the mixer
 * (see syssnd.c's / rd2_snd.c's audio callback) -- it's standing in for the
 * physical speaker, not anything inside the emulated chip, so it has no
 * business running during real gameplay. Bypassed (bit-identical passthrough)
 * unless explicitly turned on; gameselect.c turns it on for its own carousel
 * engine and off again at the exact moment it hands off to the real game.
 */

#ifndef _AUDIO_LOFI_H
#define _AUDIO_LOFI_H

#include "system.h"

#define LOFI_NUM_COMBS 3
#define LOFI_COMB_MAX 1300 /* samples -- covers every entry in audio_lofi.c's own combLen[] */

typedef struct {
	float hpLow;                         /* one-pole state subtracted from the signal for bass rolloff */
	float lpState;                       /* one-pole state for treble rolloff */
	S16 comb[LOFI_NUM_COMBS][LOFI_COMB_MAX];
	U16 combPos[LOFI_NUM_COMBS];
	float combDamp[LOFI_NUM_COMBS];      /* one-pole damping inside each comb's own feedback path */
} lofi_t;

extern void lofi_reset(lofi_t *lf);
extern S16 lofi_process(lofi_t *lf, S16 in, U8 enabled);

#endif /* _AUDIO_LOFI_H */

/* eof */

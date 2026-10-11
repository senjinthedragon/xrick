/*
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * See audio_lofi.h. Three cheap stages, in order: bass rolloff (subtract a
 * slow one-pole lowpass from the signal), treble rolloff (a faster one-pole
 * lowpass), then a short, quiet 3-tap comb reverb mixed in at a low wet
 * level. The comb delay lengths are plain, mutually prime-ish sample counts
 * (not round millisecond numbers) so their echoes don't reinforce each other
 * into an audible, ringing pitch -- the same reason classic Freeverb-style
 * reverbs pick odd delay lengths rather than round ones.
 */

#include <string.h> /* memset */

#include "audio_lofi.h"

#define FREQ 44100.0f
#define PI2 6.28318530718f

#define HP_CUTOFF_HZ 220.0f   /* below this: rolled off, standing in for a speaker too small to reproduce it */
#define LP_CUTOFF_HZ 4200.0f  /* above this: rolled off, standing in for a cheap, non-hi-fi driver */
#define COMB_FEEDBACK 0.30f
#define COMB_DAMP_COEFF 0.25f /* one-pole lowpass inside each comb's feedback -- higher = darker tail */
#define WET_MIX 0.16f         /* kept low on purpose: a hint of room/cabinet, not an audible echo */

static const int combLen[LOFI_NUM_COMBS] = { 750, 1013, 1277 };

void
lofi_reset(lofi_t *lf)
{
	int i;

	lf->hpLow = 0.0f;
	lf->lpState = 0.0f;
	for (i = 0; i < LOFI_NUM_COMBS; i++) {
		memset(lf->comb[i], 0, sizeof(lf->comb[i]));
		lf->combPos[i] = 0;
		lf->combDamp[i] = 0.0f;
	}
}

S16
lofi_process(lofi_t *lf, S16 in, U8 enabled)
{
	float x, hpOut, toneOut, wet, out;
	int i;

	if (!enabled)
		return in;

	x = (float)in;

	/* bass rolloff: x minus a slow lowpass of x is a highpass */
	lf->hpLow += (HP_CUTOFF_HZ * PI2 / FREQ) * (x - lf->hpLow);
	hpOut = x - lf->hpLow;

	/* treble rolloff */
	lf->lpState += (LP_CUTOFF_HZ * PI2 / FREQ) * (hpOut - lf->lpState);
	toneOut = lf->lpState;

	/* short, quiet comb reverb */
	wet = 0.0f;
	for (i = 0; i < LOFI_NUM_COMBS; i++) {
		U16 pos = lf->combPos[i];
		S16 delayed = lf->comb[i][pos];
		float fed;

		lf->combDamp[i] += COMB_DAMP_COEFF * ((float)delayed - lf->combDamp[i]);
		fed = toneOut + lf->combDamp[i] * COMB_FEEDBACK;
		if (fed > 32767.0f) fed = 32767.0f;
		if (fed < -32768.0f) fed = -32768.0f;
		lf->comb[i][pos] = (S16)fed;

		wet += (float)delayed;
		lf->combPos[i] = (U16)((pos + 1) % (U16)combLen[i]);
	}
	wet /= (float)LOFI_NUM_COMBS;

	out = toneOut * (1.0f - WET_MIX) + wet * WET_MIX;
	if (out > 32767.0f) out = 32767.0f;
	if (out < -32768.0f) out = -32768.0f;
	return (S16)out;
}

/* eof */

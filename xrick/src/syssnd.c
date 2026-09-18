/*
 * xrick/src/syssnd.c
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

#include <SDL3/SDL.h>
#include <stdlib.h>
#include <memory.h>
#include <string.h> /* strlen, strncpy */

#include <vorbis/vorbisfile.h>

#include "syssnd.h"

#ifdef ENABLE_SOUND

#include "sysarg.h"
#include "game.h"
#include "debug.h"
#include "data.h"

/* SDL3 dropped SDL_MIX_MAXVOLUME; this is the same value SDL2 used */
#define MIX_MAXVOLUME 128

#define ADJVOL(S) (((S) * sndVol) / MIX_MAXVOLUME)

static U8 isAudioActive = FALSE;
static channel_t channel[SYSSND_MIXCHANNELS];

static U8 sndVol = MIX_MAXVOLUME;  /* internal volume */
static U8 sndUVol = SYSSND_MAXVOL; /* user-selected volume */
static U8 sndMute = FALSE;	   /* mute flag */

static SDL_AudioStream *audioStream;
static SDL_Mutex *sndlock;

/*
 * prototypes
 */
static void end_channel(U8);

/*
 * Callback -- this is also where all sound mixing is done
 *
 * Note: it may not be that much a good idea to do all the mixing here ; it
 * may be more efficient to mix samples every frame, or maybe everytime a
 * new sound is sent to be played. I don't know.
 */
static void
syssnd_callback(UNUSED(void *userdata), SDL_AudioStream *stream, int additional_amount, UNUSED(int total_amount))
{
	U8 c;
	S32 s;
	int i;
	static S16 mixbuf[SYSSND_MIXSAMPLES]; /* scratch space, resized below if needed */
	S16 *buf;
	int nsamples = additional_amount / (int)sizeof(S16);

	buf = (nsamples <= (int)(sizeof(mixbuf) / sizeof(mixbuf[0]))) ? mixbuf : malloc(nsamples * sizeof(S16));
	if (!buf) return;

	SDL_LockMutex(sndlock);

	for (i = 0; i < nsamples; i++) {
		s = 0;
		for (c = 0; c < SYSSND_MIXCHANNELS; c++) {
			if (channel[c].loop != 0) /* channel is active */
			{
				if (channel[c].len > 0) /* not ending */
				{
					s += ADJVOL(*channel[c].buf);
					channel[c].buf++;
					channel[c].len--;
				} else /* ending */
				{
					if (channel[c].loop > 0) channel[c].loop--;
					if (channel[c].loop) /* just loop */
					{
						IFDEBUG_AUDIO2(sys_printf("xrick/audio: channel %d - loop\n", c););
						channel[c].buf = channel[c].snd->buf;
						channel[c].len = channel[c].snd->len;
						s += ADJVOL(*channel[c].buf);
						channel[c].buf++;
						channel[c].len--;
					} else /* end for real */
					{
						IFDEBUG_AUDIO2(sys_printf("xrick/audio: channel %d - end\n", c););
						end_channel(c);
					}
				}
			}
		}

		if (sndMute) {
			buf[i] = 0;
		} else {
			if (s > 32767) s = 32767;
			if (s < -32768) s = -32768;
			buf[i] = (S16)s;
		}
	}

	SDL_UnlockMutex(sndlock);

	SDL_PutAudioStreamData(stream, buf, nsamples * (int)sizeof(S16));

	if (buf != mixbuf) free(buf);
}

static void
end_channel(U8 c)
{
	channel[c].loop = 0;
	if (channel[c].snd->dispose)
		syssnd_free(channel[c].snd);
	channel[c].snd = NULL;
}

void
syssnd_init(void)
{
	SDL_AudioSpec desired;
	U16 c;

	if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
		IFDEBUG_AUDIO(
		    sys_printf("xrick/audio: can not initialize audio subsystem\n"););
		return;
	}

	desired.freq = SYSSND_FREQ;
	desired.format = SDL_AUDIO_S16;
	desired.channels = SYSSND_CHANNELS;

	audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &desired, syssnd_callback, NULL);
	if (!audioStream) {
		IFDEBUG_AUDIO(
		    sys_printf("xrick/audio: can not open audio (%s)\n", SDL_GetError()););
		return;
	}

	sndlock = SDL_CreateMutex();
	if (sndlock == NULL) {
		IFDEBUG_AUDIO(sys_printf("xrick/audio: can not create lock\n"););
		SDL_DestroyAudioStream(audioStream);
		audioStream = NULL;
		return;
	}

	if (sysarg_args_vol != 0) {
		sndUVol = sysarg_args_vol;
		sndVol = MIX_MAXVOLUME * sndUVol / SYSSND_MAXVOL;
	}

	for (c = 0; c < SYSSND_MIXCHANNELS; c++)
		channel[c].loop = 0; /* deactivate */

	isAudioActive = TRUE;
	SDL_ResumeAudioStreamDevice(audioStream);

	IFDEBUG_AUDIO(sys_printf("xrick/audio: initialized\n"););
}

/*
 * Shutdown
 */
void
syssnd_shutdown(void)
{
	if (!isAudioActive) return;

	SDL_DestroyAudioStream(audioStream);
	SDL_DestroyMutex(sndlock);
	isAudioActive = FALSE;
}

/*
 * Toggle mute
 *
 * When muted, sounds are still managed but not sent to the dsp, hence
 * it is possible to un-mute at any time.
 */
void
syssnd_toggleMute(void)
{
	SDL_LockMutex(sndlock);
	sndMute = !sndMute;
	SDL_UnlockMutex(sndlock);
}

void
syssnd_vol(S8 d)
{
	if ((d < 0 && sndUVol > 0) ||
	    (d > 0 && sndUVol < SYSSND_MAXVOL)) {
		sndUVol += d;
		SDL_LockMutex(sndlock);
		sndVol = MIX_MAXVOLUME * sndUVol / SYSSND_MAXVOL;
		SDL_UnlockMutex(sndlock);
	}
}

/*
 * Play a sound
 *
 * loop: number of times the sound should be played, -1 to loop forever
 * returns: channel number, or -1 if none was available
 *
 * NOTE if sound is already playing, simply reset it (i.e. can not have
 * twice the same sound playing -- tends to become noisy when too many
 * bad guys die at the same time).
 */
S8
syssnd_play(sound_t *sound, S8 loop)
{
	S8 c;

	if (!isAudioActive) return -1;
	if (sound == NULL) return -1;

	c = 0;
	SDL_LockMutex(sndlock);
	while ((channel[c].snd != sound || channel[c].loop == 0) &&
	       channel[c].loop != 0 &&
	       c < SYSSND_MIXCHANNELS)
		c++;
	if (c == SYSSND_MIXCHANNELS)
		c = -1;

	IFDEBUG_AUDIO(
	    if (channel[c].snd == sound && channel[c].loop != 0)
		sys_printf("xrick/sound: already playing %s on channel %d - resetting\n",
			   sound->name, c);
	    else if (c >= 0)
		sys_printf("xrick/sound: playing %s on channel %d\n", sound->name, c););

	if (c >= 0) {
		channel[c].loop = loop;
		channel[c].snd = sound;
		channel[c].buf = sound->buf;
		channel[c].len = sound->len;
	}
	SDL_UnlockMutex(sndlock);

	return c;
}

/*
 * Pause
 *
 * pause: TRUE or FALSE
 * clear: TRUE to cleanup all sounds and make sure we start from scratch
 */
void
syssnd_pause(U8 pause, U8 clear)
{
	U8 c;

	if (!isAudioActive) return;

	if (clear == TRUE) {
		SDL_LockMutex(sndlock);
		for (c = 0; c < SYSSND_MIXCHANNELS; c++)
			channel[c].loop = 0;
		SDL_UnlockMutex(sndlock);
	}

	if (pause == TRUE)
		SDL_PauseAudioStreamDevice(audioStream);
	else
		SDL_ResumeAudioStreamDevice(audioStream);
}

/*
 * Stop a channel
 */
void
syssnd_stopchan(S8 chan)
{
	if (chan < 0 || chan > SYSSND_MIXCHANNELS)
		return;

	SDL_LockMutex(sndlock);
	if (channel[chan].snd) end_channel(chan);
	SDL_UnlockMutex(sndlock);
}

/*
 * Stop a sound
 */
void
syssnd_stopsound(sound_t *sound)
{
	U8 i;

	if (!sound) return;

	SDL_LockMutex(sndlock);
	for (i = 0; i < SYSSND_MIXCHANNELS; i++)
		if (channel[i].snd == sound) end_channel(i);
	SDL_UnlockMutex(sndlock);
}

/*
 * See if a sound is playing
 */
int
syssnd_isplaying(sound_t *sound)
{
	U8 i, playing;

	playing = 0;
	SDL_LockMutex(sndlock);
	for (i = 0; i < SYSSND_MIXCHANNELS; i++)
		if (channel[i].snd == sound) playing = 1;
	SDL_UnlockMutex(sndlock);
	return playing;
}

/*
 * Stops all channels.
 */
void
syssnd_stopall(void)
{
	U8 i;

	SDL_LockMutex(sndlock);
	for (i = 0; i < SYSSND_MIXCHANNELS; i++)
		if (channel[i].snd) end_channel(i);
	SDL_UnlockMutex(sndlock);
}

/*
 * Vorbisfile I/O callbacks, wrapping our data_file_t abstraction (plain
 * directory, or in-memory zip entry -- see data.c).
 */
static size_t
vorbisIO_read(void *ptr, size_t size, size_t nmemb, void *datasource)
{
	int n = data_file_read((data_file_t *)datasource, ptr, size, nmemb);
	return n < 0 ? 0 : (size_t)n;
}

static int
vorbisIO_seek(void *datasource, ogg_int64_t offset, int whence)
{
	return data_file_seek((data_file_t *)datasource, (long)offset, whence);
}

static int
vorbisIO_close(void *datasource)
{
	data_file_close((data_file_t *)datasource);
	return 0;
}

static long
vorbisIO_tell(void *datasource)
{
	return data_file_tell((data_file_t *)datasource);
}

/*
 * Load a sound (Ogg Vorbis, mono), fully decoded to S16 PCM up-front.
 */
sound_t *
syssnd_load(char *name)
{
	sound_t *s;
	OggVorbis_File vf;
	ov_callbacks cb = {vorbisIO_read, vorbisIO_seek, vorbisIO_close, vorbisIO_tell};
	data_file_t *f;
	vorbis_info *vi;
	S16 *pcm;
	long cap, used, n;
	int bitstream;

	/* open */
	f = data_file_open(name);
	if (!f) return NULL;

	if (ov_open_callbacks(f, &vf, NULL, 0, cb) < 0) {
		data_file_close(f);
		return NULL;
	}

	vi = ov_info(&vf, -1);
	if (!vi || vi->channels != 1) {
		ov_clear(&vf); /* also closes f via vorbisIO_close */
		return NULL;
	}

	cap = 65536; /* bytes; grows as needed */
	used = 0;
	pcm = malloc(cap);
	if (!pcm) {
		ov_clear(&vf);
		return NULL;
	}

	while ((n = ov_read(&vf, (char *)pcm + used, (int)(cap - used), 0, 2, 1, &bitstream)) > 0) {
		used += n;
		if (used == cap) {
			S16 *grown;
			cap *= 2;
			grown = realloc(pcm, cap);
			if (!grown) {
				free(pcm);
				ov_clear(&vf);
				return NULL;
			}
			pcm = grown;
		}
	}

	ov_clear(&vf);

	/* alloc sound */
	s = malloc(sizeof(sound_t));
#ifdef DEBUG
	s->name = malloc(strlen(name) + 1);
	strncpy(s->name, name, strlen(name) + 1);
#endif

	s->buf = pcm;
	s->len = (U32)(used / (long)sizeof(S16));
	s->dispose = FALSE;

	return s;
}

/*
 *
 */
void
syssnd_free(sound_t *s)
{
	if (!s) return;
	if (s->buf) free(s->buf);
	s->buf = NULL;
	s->len = 0;
}

#endif /* ENABLE_SOUND */

/* eof */

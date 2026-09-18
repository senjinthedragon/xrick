/*
 * xrick/src/sounds.c
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

#include <stdlib.h>

#include "sounds.h"

#ifdef ENABLE_SOUND

sound_t *WAV_GAMEOVER;
sound_t *WAV_SBONUS2;
sound_t *WAV_BULLET;
sound_t *WAV_BOMBSHHT;
sound_t *WAV_EXPLODE;
sound_t *WAV_STICK;
sound_t *WAV_WALK;
sound_t *WAV_CRAWL;
sound_t *WAV_JUMP;
sound_t *WAV_PAD;
sound_t *WAV_BOX;
sound_t *WAV_BONUS;
sound_t *WAV_SBONUS1;
sound_t *WAV_DIE;
sound_t *WAV_ENTITY[10];

static sound_t *music_snd;

void
sounds_load(void)
{
	/*
	 * Cache sounds
	 *
	 * tune[0-5].ogg not cached
	 */
	WAV_GAMEOVER = syssnd_load("sounds/gameover.ogg");
	WAV_SBONUS2 = syssnd_load("sounds/sbonus2.ogg");
	WAV_BULLET = syssnd_load("sounds/bullet.ogg");
	WAV_BOMBSHHT = syssnd_load("sounds/bombshht.ogg");
	WAV_EXPLODE = syssnd_load("sounds/explode.ogg");
	WAV_STICK = syssnd_load("sounds/stick.ogg");
	WAV_WALK = syssnd_load("sounds/walk.ogg");
	WAV_CRAWL = syssnd_load("sounds/crawl.ogg");
	WAV_JUMP = syssnd_load("sounds/jump.ogg");
	WAV_PAD = syssnd_load("sounds/pad.ogg");
	WAV_BOX = syssnd_load("sounds/box.ogg");
	WAV_BONUS = syssnd_load("sounds/bonus.ogg");
	WAV_SBONUS1 = syssnd_load("sounds/sbonus1.ogg");
	WAV_DIE = syssnd_load("sounds/die.ogg");
	WAV_ENTITY[0] = syssnd_load("sounds/ent0.ogg");
	WAV_ENTITY[1] = syssnd_load("sounds/ent1.ogg");
	WAV_ENTITY[2] = syssnd_load("sounds/ent2.ogg");
	WAV_ENTITY[3] = syssnd_load("sounds/ent3.ogg");
	WAV_ENTITY[4] = syssnd_load("sounds/ent4.ogg");
	WAV_ENTITY[5] = syssnd_load("sounds/ent5.ogg");
	WAV_ENTITY[6] = syssnd_load("sounds/ent6.ogg");
	WAV_ENTITY[7] = syssnd_load("sounds/ent7.ogg");
	WAV_ENTITY[8] = syssnd_load("sounds/ent8.ogg");
}

void
sounds_free(void)
{
	syssnd_stopall();
	syssnd_free(WAV_GAMEOVER);
	syssnd_free(WAV_SBONUS2);
	syssnd_free(WAV_BULLET);
	syssnd_free(WAV_BOMBSHHT);
	syssnd_free(WAV_EXPLODE);
	syssnd_free(WAV_STICK);
	syssnd_free(WAV_WALK);
	syssnd_free(WAV_CRAWL);
	syssnd_free(WAV_JUMP);
	syssnd_free(WAV_PAD);
	syssnd_free(WAV_BOX);
	syssnd_free(WAV_BONUS);
	syssnd_free(WAV_SBONUS1);
	syssnd_free(WAV_DIE);
	syssnd_free(WAV_ENTITY[0]);
	syssnd_free(WAV_ENTITY[1]);
	syssnd_free(WAV_ENTITY[2]);
	syssnd_free(WAV_ENTITY[3]);
	syssnd_free(WAV_ENTITY[4]);
	syssnd_free(WAV_ENTITY[5]);
	syssnd_free(WAV_ENTITY[6]);
	syssnd_free(WAV_ENTITY[7]);
	syssnd_free(WAV_ENTITY[8]);
}

/*
 * sounds_setMusic
 *
 * sets the current background music.
 */
void
sounds_setMusic(char *name, U8 loop)
{
	if (music_snd)
		sounds_stopMusic();
	music_snd = syssnd_load(name);
	if (music_snd) {
		music_snd->dispose = TRUE; /* music is always "fire and forget" */
		syssnd_play(music_snd, loop);
	}
}


/*
 * sounds_stopMusic
 *
 * stops the current background music.
 */
void
sounds_stopMusic(void)
{
	syssnd_stopsound(music_snd);
	music_snd = NULL;
}

#endif /* ENABLE_SOUND */

/* eof */
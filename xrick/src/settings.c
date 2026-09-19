/*
 * xrick/src/settings.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Settings are a plain "name=number" text file. Loading fills the same
 * sysarg_args_* variables the command line does (so command-line options,
 * parsed afterwards, override the file for that run); saving reads the
 * live state back out of each subsystem.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "system.h"
#include "settings.h"
#include "sysarg.h"
#include "syskbd.h"
#include "sysjoy.h"
#include "syssnd.h"
#include "sysvid.h"

#define FILE_NAME "settings.ini"

/* loaded volume/mute, applied once audio is initialised */
static int loadedVolume = -1;
static int loadedMute = -1;
static int fileVersion = 1; /* files without a version key are from before v1.2 */

typedef struct {
	const char *name;
	int *value;
	int min, max;
} intSetting_t;

typedef struct {
	const char *name;
	U8 *value;
} keySetting_t;

/* keyboard bindings are U8 scancodes (0 = none) */
static const keySetting_t keySettings[] = {
    {"key_left", &syskbd_left},
    {"key_right", &syskbd_right},
    {"key_up", &syskbd_up},
    {"key_down", &syskbd_down},
    {"key_fire", &syskbd_fire},
    {"key_shoot", &syskbd_shoot},
    {"key_bomb", &syskbd_bomb},
    {"key_pause", &syskbd_pause},
};

static const intSetting_t intSettings[] = {
    {"fullscreen", &sysarg_args_fullscreen, 0, 1},
    {"zoom", &sysarg_args_zoom, 0, 4},
    {"upscale", &sysarg_args_upscale, 0, 2},
    {"crt", &sysarg_args_crt, 0, 3},
    {"aspect", &sysarg_args_aspect, 0, 1},
    {"bezel", &sysarg_args_bezel, 0, 2},
    {"controls", &sysarg_args_controls, 0, 1},
    {"speed_period", &sysarg_args_period, 0, 100},
    {"pad_jump", &sysjoy_btn_jump, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
    {"pad_fire", &sysjoy_btn_fire, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
    {"pad_shoot", &sysjoy_btn_shoot, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
    {"pad_bomb", &sysjoy_btn_bomb, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
    {"pad_pause", &sysjoy_btn_pause, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
    {"pad_menu", &sysjoy_btn_menu, 0, SDL_GAMEPAD_BUTTON_COUNT - 1},
};

#define COUNT(a) ((int)(sizeof(a) / sizeof((a)[0])))

static char *
filePath(void)
{
	char *dir = SDL_GetPrefPath("xrick", "xrick");
	char *path;
	size_t n;

	if (!dir) return NULL;
	n = strlen(dir) + strlen(FILE_NAME) + 1;
	path = (char *)malloc(n);
	if (path) snprintf(path, n, "%s%s", dir, FILE_NAME);
	SDL_free(dir);
	return path;
}

void
settings_load(void)
{
	char *path = filePath();
	FILE *f;
	char line[128];

	if (!path) return;
	f = fopen(path, "r");
	free(path);
	if (!f) return;

	while (fgets(line, sizeof line, f)) {
		char *eq = strchr(line, '=');
		int i, v;

		if (!eq) continue;
		*eq = '\0';
		v = atoi(eq + 1);

		for (i = 0; i < COUNT(intSettings); i++)
			if (!strcmp(line, intSettings[i].name) && v >= intSettings[i].min && v <= intSettings[i].max)
				*intSettings[i].value = v;
		for (i = 0; i < COUNT(keySettings); i++)
			if (!strcmp(line, keySettings[i].name) && v >= 0 && v <= 255)
				*keySettings[i].value = (U8)v;
		if (!strcmp(line, "version")) fileVersion = v;
		if (!strcmp(line, "volume") && v >= 0 && v <= SYSSND_MAXVOL) loadedVolume = v;
		if (!strcmp(line, "mute") && (v == 0 || v == 1)) loadedMute = v;
	}
	fclose(f);

	/* v1.2 reordered the upscaler/CRT/bezel choices (sharp, lottes and the
	 * SC1224 slot in ahead of the older ones): translate older numbers */
	if (fileVersion < 2) {
		if (sysarg_args_upscale == 1) sysarg_args_upscale = 2; /* fsr1 */
		if (sysarg_args_crt == 2) sysarg_args_crt = 3;	       /* royale */
		if (sysarg_args_bezel == 1) sysarg_args_bezel = 2;     /* 1084s */
	}
}

void
settings_apply(void)
{
	if (loadedVolume >= 0) syssnd_setVol((U8)loadedVolume);
	if (loadedMute >= 0) syssnd_setMute((U8)loadedMute);
}

void
settings_save(void)
{
	char *path = filePath();
	FILE *f;
	int i;

	if (!path) return;
	f = fopen(path, "w");
	free(path);
	if (!f) return;

	fprintf(f, "version=2\n");

	/* video/audio: read back from the live state */
	fprintf(f, "fullscreen=%d\n", sysvid_isFullscreen() ? 1 : 0);
	fprintf(f, "zoom=%d\n", sysvid_getWindowZoom());
	fprintf(f, "upscale=%d\n", sysvid_getUpscale());
	fprintf(f, "crt=%d\n", sysvid_getCrt());
	fprintf(f, "bezel=%d\n", sysvid_getBezel());
	fprintf(f, "aspect=%d\n", sysvid_getAspect());
	fprintf(f, "volume=%d\n", syssnd_getVol());
	fprintf(f, "mute=%d\n", syssnd_getMute() ? 1 : 0);

	/* the rest lives directly in the sysarg/kbd/joy variables */
	for (i = 0; i < COUNT(intSettings); i++) {
		const char *n = intSettings[i].name;
		if (!strcmp(n, "fullscreen") || !strcmp(n, "zoom") || !strcmp(n, "upscale") ||
		    !strcmp(n, "crt") || !strcmp(n, "aspect") || !strcmp(n, "bezel"))
			continue;
		fprintf(f, "%s=%d\n", n, *intSettings[i].value);
	}
	for (i = 0; i < COUNT(keySettings); i++)
		fprintf(f, "%s=%d\n", keySettings[i].name, *keySettings[i].value);

	fclose(f);
}

/* eof */

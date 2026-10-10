/*
 * xrick/src/menu.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * The in-game settings menu. Pages are lists of items; each item is
 * either a link to another page, an action, a value that left/right
 * cycles, or a key/gamepad binding that can be remapped. Every change is
 * applied immediately and saved (see settings.c).
 *
 * Shared by both rd1 and rd2 (sysarg_args_game == 2): video/gamepad/key
 * bindings are game-agnostic, but each game has its own sound engine and
 * cheat flags with no shared API, so AUDIO and the cheat rows dispatch on
 * sysarg_args_game; GAME SPEED and CONTROL SCHEME are rd1-only concepts
 * (rd2 runs its VBL loop at a fixed 50 Hz and has no modern-controls
 * combo-key path, port-rd2.md §7) and show N/A for rd2.
 */

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "system.h"
#include "menu.h"
#include "fb.h"
#include "control.h"
#include "settings.h"
#include "sysarg.h"
#include "sysevt.h"
#include "sysjoy.h"
#include "syskbd.h"
#include "syssnd.h"
#include "rd2_snd.h"
#include "rd2_sys.h"
#include "sysvid.h"
#include "sysvid_gpu.h"
#include "game.h"
#include "env.h"

#define GAME2 (sysarg_args_game == 2)

typedef enum {
	PG_MAIN,
	PG_VIDEO,
	PG_AUDIO,
	PG_GAME,
	PG_KEYS,
	PG_PAD,
	PG_CONFIRM,
	PG_COUNT
} page_t;

typedef enum {
	I_RESUME,
	I_GOTO_VIDEO,
	I_GOTO_AUDIO,
	I_GOTO_GAME,
	I_GOTO_KEYS,
	I_GOTO_PAD,
	I_QUIT,
	I_BACK,
	I_FULLSCREEN,
	I_ZOOM,
	I_ASPECT,
	I_UPSCALE,
	I_CRT,
	I_BEZEL,
	I_VOLUME,
	I_MUTE,
	I_CONTROLS,
	I_SPEED,
	I_TRAINER,
	I_INVINCIBLE,
	I_HIGHLIGHT,
	I_KEY_0, /* I_KEY_0 .. I_KEY_0 + KEY_COUNT - 1 */
	I_KEY_LAST = I_KEY_0 + 7,
	I_PAD_0, /* I_PAD_0 .. I_PAD_0 + PAD_COUNT - 1 */
	I_PAD_LAST = I_PAD_0 + 5,
	I_KEYS_RESET,
	I_PAD_RESET,
	I_YES,
	I_NO
} item_t;

#define KEY_COUNT 8
#define PAD_COUNT 6

typedef struct {
	const char *title;
	U8 parent; /* page to go back to; PG_MAIN's parent is itself (= close) */
	U8 count;
	U8 items[12];
} pagedef_t;

static const pagedef_t pages[PG_COUNT] = {
    {"SETTINGS", PG_MAIN, 7, {I_RESUME, I_GOTO_VIDEO, I_GOTO_AUDIO, I_GOTO_GAME, I_GOTO_KEYS, I_GOTO_PAD, I_QUIT}},
    {"VIDEO", PG_MAIN, 7, {I_FULLSCREEN, I_ZOOM, I_ASPECT, I_UPSCALE, I_CRT, I_BEZEL, I_BACK}},
    {"AUDIO", PG_MAIN, 3, {I_VOLUME, I_MUTE, I_BACK}},
    {"GAME", PG_MAIN, 6, {I_CONTROLS, I_SPEED, I_TRAINER, I_INVINCIBLE, I_HIGHLIGHT, I_BACK}},
    {"KEYBOARD CONTROLS", PG_MAIN, 10, {I_KEY_0 + 0, I_KEY_0 + 1, I_KEY_0 + 2, I_KEY_0 + 3, I_KEY_0 + 4, I_KEY_0 + 5, I_KEY_0 + 6, I_KEY_0 + 7, I_KEYS_RESET, I_BACK}},
    {"GAMEPAD CONTROLS", PG_MAIN, 8, {I_PAD_0 + 0, I_PAD_0 + 1, I_PAD_0 + 2, I_PAD_0 + 3, I_PAD_0 + 4, I_PAD_0 + 5, I_PAD_RESET, I_BACK}},
    {"QUIT THE GAME?", PG_MAIN, 2, {I_NO, I_YES}},
};

static U8 *const keyVar[KEY_COUNT] = {&syskbd_left, &syskbd_right, &syskbd_up, &syskbd_down, &syskbd_fire, &syskbd_shoot, &syskbd_bomb, &syskbd_pause};
static const char *const keyLabel[KEY_COUNT] = {"LEFT", "RIGHT", "UP / JUMP", "DOWN / CRAWL", "ACTION", "SHOOT (MODERN)", "BOMB (MODERN)", "PAUSE"};
static int *const padVar[PAD_COUNT] = {&sysjoy_btn_jump, &sysjoy_btn_fire, &sysjoy_btn_shoot, &sysjoy_btn_bomb, &sysjoy_btn_pause, &sysjoy_btn_menu};
static const char *const padLabel[PAD_COUNT] = {"JUMP", "ACTION", "SHOOT (MODERN)", "BOMB (MODERN)", "PAUSE", "MENU"};

static const char *const onOff[] = {"OFF", "ON"};
static const char *const upscaleNames[] = {"NONE", "SHARP", "FSR1"};
static const char *const crtNames[] = {"NONE", "EASYMODE", "LOTTES", "ROYALE"};
static const char *const aspectNames[] = {"4:3", "SQUARE"};
static const char *const bezelNames[] = {"NONE", "SC1224", "1084S"};
static const char *const controlNames[] = {"CLASSIC", "MODERN"};

static U8 active = FALSE;
static U8 page = PG_MAIN;
static U8 cursor[PG_COUNT];
static enum { NAV,
	      CAP_KEY,
	      CAP_PAD } mode = NAV;
static U8 capIndex; /* which binding is being captured */

#define PANEL_W 250
#define ROW_H 11
#define HDR_H 16
#define FTR_H 14

#define GAME_SPEED_DEFAULT GAME_PERIOD

U8
menu_active(void)
{
	return active;
}

/* --- rd1/rd2 dispatch ------------------------------------------------ */

static U8
audioGetVol(void)
{
	return GAME2 ? rd2_snd_getVol() : syssnd_getVol();
}

static void
audioVol(S8 d)
{
	if (GAME2) rd2_snd_vol(d); else syssnd_vol(d);
}

static U8
audioGetMute(void)
{
	return GAME2 ? rd2_snd_getMute() : syssnd_getMute();
}

static void
audioToggleMute(void)
{
	if (GAME2) rd2_snd_toggleMute(); else syssnd_toggleMute();
}

static int
cheatValue(int which)
{
	if (GAME2)
		return which == 1 ? rd2_cheat_trainer : which == 2 ? rd2_cheat_invincible
								     : rd2_cheat_highlight;
	return which == 1 ? env_trainer : which == 2 ? env_invicible
						       : env_highlight;
}

static void
cheatToggle(int which)
{
	if (GAME2) rd2_sys_toggleCheat((U8)which); else game_toggleCheat((U8)which);
}

/* --- helpers -------------------------------------------------------- */

static int
cycle(int v, int dir, int count)
{
	return (v + dir + count) % count;
}

static int
gamePeriod(void)
{
	return sysarg_args_period ? sysarg_args_period : GAME_SPEED_DEFAULT;
}

static void
keyName(int scancode, char *buf, size_t n)
{
	const char *name = scancode ? SDL_GetScancodeName((SDL_Scancode)scancode) : "";
	size_t i;

	if (!name || !*name) name = "NONE";
	for (i = 0; i + 1 < n && name[i]; i++)
		buf[i] = (char)toupper((unsigned char)name[i]);
	buf[i] = '\0';
}

static int
keyConflict(int i)
{
	int j;
	for (j = 0; j < KEY_COUNT; j++)
		if (j != i && *keyVar[j] == *keyVar[i]) return 1;
	return 0;
}

static int
padConflict(int i)
{
	int j;
	for (j = 0; j < PAD_COUNT; j++)
		if (j != i && *padVar[j] == *padVar[i]) return 1;
	return 0;
}

/* keys that must stay usable for the menu/game itself */
static int
keyReserved(int sc)
{
	return sc <= 0 || sc > 255 || sc == SDL_SCANCODE_ESCAPE ||
	       (sc >= SDL_SCANCODE_F1 && sc <= SDL_SCANCODE_F12);
}

/* --- open / close --------------------------------------------------- */

static void
openMenu(void)
{
	active = TRUE;
	page = PG_MAIN;
	cursor[PG_MAIN] = 0;
	mode = NAV;
	sysevt_resetInput();
#ifdef ENABLE_SOUND
	if (!GAME2) syssnd_pause(TRUE, FALSE);
#endif
}

static void
closeMenu(void)
{
	active = FALSE;
	mode = NAV;
	sysevt_resetInput();
#ifdef ENABLE_SOUND
	if (!GAME2 && !game_isPaused()) syssnd_pause(FALSE, FALSE);
#endif
}

static void
gotoPage(int p)
{
	page = (U8)p;
	if (p == PG_CONFIRM) cursor[p] = 0; /* default: NO */
}

static void
goBack(void)
{
	if (page == PG_MAIN)
		closeMenu();
	else
		page = pages[page].parent;
}

/* --- item values ---------------------------------------------------- */

/* left/right (or activate) on a value item */
static void
adjust(int it, int dir)
{
	switch (it) {
	case I_FULLSCREEN:
		sysvid_toggleFullscreen();
		break;
	case I_ZOOM:
		sysvid_zoom((S8)dir);
		break;
	case I_ASPECT:
		sysvid_setAspect(!sysvid_getAspect());
		break;
	case I_UPSCALE:
		sysvid_setUpscale(cycle(sysvid_getUpscale(), dir, 3));
		break;
	case I_CRT: {
		int v = sysvid_getCrt(), i;
		for (i = 0; i < 4; i++) { /* skip crt-royale if its mask failed to load */
			v = cycle(v, dir, 4);
			sysvid_setCrt(v);
			if (sysvid_getCrt() == v) break;
		}
		break;
	}
	case I_BEZEL:
		sysvid_setBezel(cycle(sysvid_getBezel(), dir, 3));
		break;
	case I_VOLUME:
		audioVol((S8)dir);
		break;
	case I_MUTE:
		audioToggleMute();
		break;
	case I_CONTROLS:
		sysarg_args_controls = sysarg_args_controls ? 0 : 1;
		sysevt_resetInput();
		break;
	case I_SPEED: {
		/* right = faster = shorter frame period; rd1 only, rd2's VBL loop is fixed 50 Hz */
		int p;
		if (GAME2) return;
		p = gamePeriod() - dir * 5;
		if (p < 25) p = 25;
		if (p > 100) p = 100;
		sysarg_args_period = p;
		game_period = (U8)p;
		break;
	}
	case I_TRAINER:
		cheatToggle(1);
		break;
	case I_INVINCIBLE:
		cheatToggle(2);
		break;
	case I_HIGHLIGHT:
		cheatToggle(3);
		break;
	default:
		return;
	}
	settings_save();
}

static void
activate(int it)
{
	switch (it) {
	case I_RESUME:
		closeMenu();
		return;
	case I_GOTO_VIDEO:
		gotoPage(PG_VIDEO);
		return;
	case I_GOTO_AUDIO:
		gotoPage(PG_AUDIO);
		return;
	case I_GOTO_GAME:
		gotoPage(PG_GAME);
		return;
	case I_GOTO_KEYS:
		gotoPage(PG_KEYS);
		return;
	case I_GOTO_PAD:
		gotoPage(PG_PAD);
		return;
	case I_QUIT:
		gotoPage(PG_CONFIRM);
		return;
	case I_BACK:
	case I_NO:
		goBack();
		return;
	case I_YES:
		closeMenu();
		control_status |= CONTROL_EXIT; /* same path as closing the window */
		control_last = CONTROL_EXIT;
		return;
	case I_KEYS_RESET:
		syskbd_resetDefaults();
		settings_save();
		return;
	case I_PAD_RESET:
		sysjoy_resetDefaults();
		settings_save();
		return;
	default:
		break;
	}

	if (it >= I_KEY_0 && it <= I_KEY_LAST) {
		capIndex = (U8)(it - I_KEY_0);
		mode = CAP_KEY;
	} else if (it >= I_PAD_0 && it <= I_PAD_LAST) {
		capIndex = (U8)(it - I_PAD_0);
		mode = CAP_PAD;
	} else {
		adjust(it, +1);
	}
}

/* --- input ---------------------------------------------------------- */

static void
moveCursor(int d)
{
	int n = pages[page].count;
	cursor[page] = (U8)((cursor[page] + d + n) % n);
}

static void
navigate(int dir_x, int dir_y, int select, int back)
{
	int it = pages[page].items[cursor[page]];

	if (dir_y) moveCursor(dir_y);
	if (dir_x) adjust(it, dir_x);
	if (select) activate(it);
	if (back) goBack();
}

static void
onKey(int sc, int repeat)
{
	if (mode == CAP_KEY) {
		if (repeat) return;
		if (sc == SDL_SCANCODE_ESCAPE) {
			mode = NAV;
		} else if (!keyReserved(sc)) {
			*keyVar[capIndex] = (U8)sc;
			mode = NAV;
			settings_save();
		}
		return;
	}
	if (mode == CAP_PAD) {
		if (sc == SDL_SCANCODE_ESCAPE) mode = NAV;
		return;
	}

	switch (sc) {
	case SDL_SCANCODE_ESCAPE:
		closeMenu();
		break;
	case SDL_SCANCODE_UP:
		navigate(0, -1, 0, 0);
		break;
	case SDL_SCANCODE_DOWN:
		navigate(0, +1, 0, 0);
		break;
	case SDL_SCANCODE_LEFT:
		navigate(-1, 0, 0, 0);
		break;
	case SDL_SCANCODE_RIGHT:
		navigate(+1, 0, 0, 0);
		break;
	case SDL_SCANCODE_RETURN:
	case SDL_SCANCODE_KP_ENTER:
	case SDL_SCANCODE_SPACE:
		if (!repeat) navigate(0, 0, 1, 0);
		break;
	case SDL_SCANCODE_BACKSPACE:
		if (!repeat) navigate(0, 0, 0, 1);
		break;
	}
}

static void
onButton(int button)
{
	if (mode == CAP_PAD) {
		/* movement stays on the d-pad */
		if (button >= SDL_GAMEPAD_BUTTON_DPAD_UP && button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT) return;
		*padVar[capIndex] = button;
		mode = NAV;
		settings_save();
		return;
	}
	if (mode == CAP_KEY) return;

	if (button == sysjoy_btn_menu) {
		closeMenu();
		return;
	}
	switch (button) {
	case SDL_GAMEPAD_BUTTON_DPAD_UP:
		navigate(0, -1, 0, 0);
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
		navigate(0, +1, 0, 0);
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
		navigate(-1, 0, 0, 0);
		break;
	case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
		navigate(+1, 0, 0, 0);
		break;
	case SDL_GAMEPAD_BUTTON_SOUTH:
		navigate(0, 0, 1, 0);
		break;
	case SDL_GAMEPAD_BUTTON_EAST:
		navigate(0, 0, 0, 1);
		break;
	}
}

static void
onAxis(int axis, int value)
{
	static int held[2]; /* -1/0/+1 per axis, so a held stick moves once */
	int idx, d;

	if (mode != NAV) return;
	if (axis == SDL_GAMEPAD_AXIS_LEFTX)
		idx = 0;
	else if (axis == SDL_GAMEPAD_AXIS_LEFTY)
		idx = 1;
	else
		return;

	d = value < -16000 ? -1 : value > 16000 ? 1
						 : 0;
	if (d == 0 && (value > -8000 && value < 8000)) {
		held[idx] = 0;
		return;
	}
	if (d == 0 || d == held[idx]) return;
	held[idx] = d;
	if (idx == 0)
		navigate(d, 0, 0, 0);
	else
		navigate(0, d, 0, 0);
}

U8
menu_handleEvent(const SDL_Event *ev)
{
	if (!active) {
		if (ev->type == SDL_EVENT_KEY_DOWN && !ev->key.repeat && ev->key.scancode == SDL_SCANCODE_ESCAPE) {
			openMenu();
			return TRUE;
		}
#ifdef ENABLE_JOYSTICK
		if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN && sysjoy_isActive(ev->gbutton.which) &&
		    ev->gbutton.button == sysjoy_btn_menu) {
			openMenu();
			return TRUE;
		}
#endif
		return FALSE;
	}

	switch (ev->type) {
	case SDL_EVENT_KEY_DOWN:
		onKey(ev->key.scancode, ev->key.repeat);
		return TRUE;
	case SDL_EVENT_KEY_UP:
		return TRUE;
#ifdef ENABLE_JOYSTICK
	case SDL_EVENT_GAMEPAD_BUTTON_UP:
		return TRUE;
	case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
		if (sysjoy_isActive(ev->gbutton.which)) onButton(ev->gbutton.button);
		return TRUE;
	case SDL_EVENT_GAMEPAD_AXIS_MOTION:
		if (sysjoy_isActive(ev->gaxis.which)) onAxis(ev->gaxis.axis, ev->gaxis.value);
		return TRUE;
#endif
	default:
		return FALSE; /* quit, hot-plugging, window events: not ours */
	}
}

/* --- drawing -------------------------------------------------------- */

static const U8 white[3] = {255, 255, 255};
static const U8 yellow[3] = {255, 220, 90};
static const U8 grey[3] = {150, 150, 160};
static const U8 red[3] = {255, 70, 70};

static void
text(int x, int y, const char *s, const U8 *c)
{
	sysvid_overlayText(x, y, s, c[0], c[1], c[2]);
}

static void
textRight(int xr, int y, const char *s, const U8 *c)
{
	text(xr - sysvid_overlayTextWidth(s), y, s, c);
}

static void
drawBar(int xr, int y, int value, int max)
{
	int i, segW = 5, x = xr - max * (segW + 1) + 1;

	for (i = 0; i < max; i++) {
		if (i < value)
			sysvid_overlayRect(x + i * (segW + 1), y, segW, 7, 255, 170, 30, 255);
		else
			sysvid_overlayRect(x + i * (segW + 1), y, segW, 7, 90, 90, 100, 200);
	}
}

static void
choiceText(int xr, int y, const char *value)
{
	char buf[40];
	snprintf(buf, sizeof buf, "< %s >", value);
	textRight(xr, y, buf, yellow);
}

static void
drawItem(int it, int xl, int xr, int y, int selected)
{
	char buf[40];
	const char *label = "";
	int i;

	switch (it) {
	case I_RESUME:
		label = "RESUME GAME";
		break;
	case I_GOTO_VIDEO:
		label = "VIDEO";
		break;
	case I_GOTO_AUDIO:
		label = "AUDIO";
		break;
	case I_GOTO_GAME:
		label = "GAME";
		break;
	case I_GOTO_KEYS:
		label = "KEYBOARD CONTROLS";
		break;
	case I_GOTO_PAD:
		label = "GAMEPAD CONTROLS";
		break;
	case I_QUIT:
		label = "QUIT GAME";
		break;
	case I_BACK:
		label = "BACK";
		break;
	case I_FULLSCREEN:
		label = "FULLSCREEN";
		break;
	case I_ZOOM:
		label = "WINDOW SCALE";
		break;
	case I_ASPECT:
		label = "ASPECT RATIO";
		break;
	case I_UPSCALE:
		label = "UPSCALER";
		break;
	case I_CRT:
		label = "CRT SHADER";
		break;
	case I_BEZEL:
		label = "BEZEL";
		break;
	case I_VOLUME:
		label = "VOLUME";
		break;
	case I_MUTE:
		label = "MUTE";
		break;
	case I_CONTROLS:
		label = "CONTROL SCHEME";
		break;
	case I_SPEED:
		label = "GAME SPEED";
		break;
	case I_TRAINER:
		label = "TRAINER (INF. AMMO)";
		break;
	case I_INVINCIBLE:
		label = "INVINCIBLE";
		break;
	case I_HIGHLIGHT:
		label = "HIGHLIGHT";
		break;
	case I_KEYS_RESET:
		label = "RESET TO DEFAULTS";
		break;
	case I_PAD_RESET:
		label = "RESET TO DEFAULTS";
		break;
	case I_YES:
		label = "YES, QUIT";
		break;
	case I_NO:
		label = "NO, KEEP PLAYING";
		break;
	default:
		break;
	}

	if (it >= I_KEY_0 && it <= I_KEY_LAST) {
		i = it - I_KEY_0;
		text(xl, y, keyLabel[i], keyConflict(i) ? red : white);
		if (mode == CAP_KEY && capIndex == i) {
			textRight(xr, y, "PRESS A KEY...", yellow);
		} else {
			keyName(*keyVar[i], buf, sizeof buf);
			textRight(xr, y, buf, keyConflict(i) ? red : yellow);
		}
		return;
	}
	if (it >= I_PAD_0 && it <= I_PAD_LAST) {
		i = it - I_PAD_0;
		text(xl, y, padLabel[i], padConflict(i) ? red : white);
		if (mode == CAP_PAD && capIndex == i)
			textRight(xr, y, "PRESS A BUTTON...", yellow);
		else
			textRight(xr, y, sysjoy_buttonName(*padVar[i]), padConflict(i) ? red : yellow);
		return;
	}

	text(xl, y, label, white);
	(void)selected;

	switch (it) {
	case I_FULLSCREEN:
		choiceText(xr, y, onOff[sysvid_isFullscreen() ? 1 : 0]);
		break;
	case I_ZOOM:
		if (sysvid_isFullscreen()) {
			textRight(xr, y, "N/A IN FULLSCREEN", grey);
		} else {
			snprintf(buf, sizeof buf, "%dX", sysvid_getWindowZoom());
			choiceText(xr, y, buf);
		}
		break;
	case I_ASPECT:
		choiceText(xr, y, aspectNames[sysvid_getAspect()]);
		break;
	case I_UPSCALE:
		choiceText(xr, y, upscaleNames[sysvid_getUpscale()]);
		break;
	case I_CRT:
		choiceText(xr, y, crtNames[sysvid_getCrt()]);
		break;
	case I_BEZEL:
		choiceText(xr, y, bezelNames[sysvid_getBezel()]);
		break;
	case I_VOLUME:
		drawBar(xr, y, audioGetVol(), SYSSND_MAXVOL);
		break;
	case I_MUTE:
		choiceText(xr, y, onOff[audioGetMute() ? 1 : 0]);
		break;
	case I_CONTROLS:
		choiceText(xr, y, controlNames[sysarg_args_controls ? 1 : 0]);
		break;
	case I_SPEED:
		if (GAME2) {
			textRight(xr, y, "N/A FOR RD2", grey);
		} else {
			snprintf(buf, sizeof buf, "%d%%", GAME_SPEED_DEFAULT * 100 / gamePeriod());
			choiceText(xr, y, buf);
		}
		break;
	case I_TRAINER:
		choiceText(xr, y, onOff[cheatValue(1) ? 1 : 0]);
		break;
	case I_INVINCIBLE:
		choiceText(xr, y, onOff[cheatValue(2) ? 1 : 0]);
		break;
	case I_HIGHLIGHT:
		choiceText(xr, y, onOff[cheatValue(3) ? 1 : 0]);
		break;
	}
}

void
menu_draw(void)
{
	const pagedef_t *pg = &pages[page];
	int w = PANEL_W;
	int h = HDR_H + pg->count * ROW_H + 6 + FTR_H;
	int x = (FB_WIDTH - w) / 2;
	int y = (FB_HEIGHT - h) / 2;
	int i;
	const char *hint;

	/* translucent panel, a thin border, and a title bar */
	sysvid_overlayRect(x, y, w, h, 8, 10, 24, 170);
	sysvid_overlayRect(x, y, w, 1, 255, 170, 30, 255);
	sysvid_overlayRect(x, y + h - 1, w, 1, 255, 170, 30, 255);
	sysvid_overlayRect(x, y, 1, h, 255, 170, 30, 255);
	sysvid_overlayRect(x + w - 1, y, 1, h, 255, 170, 30, 255);
	sysvid_overlayRect(x + 1, y + 1, w - 2, HDR_H - 4, 255, 150, 20, 90);
	text(x + (w - sysvid_overlayTextWidth(pg->title)) / 2, y + 4, pg->title, white);

	for (i = 0; i < pg->count; i++) {
		int ry = y + HDR_H + i * ROW_H;
		int sel = (i == cursor[page]);
		if (sel) sysvid_overlayRect(x + 3, ry - 2, w - 6, ROW_H - 1, 70, 120, 230, 150);
		drawItem(pg->items[i], x + 10, x + w - 10, ry, sel);
	}

	if (mode == CAP_KEY)
		hint = "PRESS THE NEW KEY  ESC CANCEL";
	else if (mode == CAP_PAD)
		hint = "PRESS THE NEW BUTTON  ESC CANCEL";
	else
		hint = "L/R CHANGE  ENTER SELECT  ESC CLOSE";
	textRight(x + w - 8, y + h - FTR_H + 3, hint, grey);
}

/* eof */

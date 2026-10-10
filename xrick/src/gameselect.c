/*
 * xrick/src/gameselect.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * The game select screen: a diskette carousel choosing RD1 or RD2, shown instead
 * of the usual splash (xrick.c) when launched with no -game argument. Runs before
 * either game engine boots, so it owns its own tiny render loop and draws its own
 * 320x200 RGBA frame directly (sysvid_updateRGBA, sysvid.c) -- the same resolution
 * and shader/bezel pipeline gameplay uses, so it's automatically period-correct
 * pixel size and respects whatever upscale/CRT/bezel the player has set, with no
 * separate "pixelization" effect needed.
 *
 * Everything animated here is computed live, every frame, from a handful of state
 * floats (bounce phase, carousel transition progress) -- old-school demo-effect
 * style, not a baked animation: the only static assets are the diskette label art
 * (see gameselect_embedded.h) and the diskette silhouette itself (drawn once at
 * startup into a small RGBA sprite per game, then just rotated/scaled/dimmed live
 * via blitRotScale() below, never re-rendered from scratch per frame).
 *
 * The disk is drawn strictly face-on: blitRotScale's "rotation" is in-plane only
 * (Z axis, screen-facing) -- used for the idle bounce's seesaw tilt. The left/right
 * carousel motion never actually rotates the sprite in a way that would reveal an
 * edge or back face (there isn't one to draw); it's just scale + position + a
 * brightness fade along a curve, which is enough to read as "swapping to the back"
 * without pretending to be a real 3D object.
 */

#include <math.h>
#include <string.h> /* memset, memcpy */
#include <stdlib.h> /* malloc, free */
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#include <SDL3/SDL.h> /* SDL_GetKeyboardState: Enter also confirms here, like the menu */

#include "system.h"
#include "sysvid.h"
#include "sysvid_overlay.h" /* sysvid_drawTextAlpha: the fading game-title label */
#include "sysevt.h"
#include "control.h"
#include "sysarg.h"
#include "menu.h"
#include "gameselect.h"
#include "gameselect_embedded.h"
#include "thirdparty/stb_image.h"

#include "rd1/pics.h" /* pic_splash: RD1's real title picture, decoded live below */
#include "syssnd.h"   /* rd1's engine: the carousel's own music/crossfade/scream */
#include "sounds.h"   /* SND_TRACK_ATTRACT, WAV_DIE */
#include "rd2_snd.h"  /* rd2's engine, same purpose */
#include "rd2_game.h" /* rd2_1a6aa: play_sound(id, d1), for the RD2 scream */

#define GS_W SYSVID_WIDTH  /* 320 */
#define GS_H SYSVID_HEIGHT /* 200 */
#define GAMESELECT_FRAME_MS 40 /* ~25fps, same pace as the rest of the game */

/* the composited diskette sprite (template + label), built once per game at
   gameselect_start() and only ever rotated/scaled/dimmed from there on */
#define DISK_W 72
#define DISK_H 72 /* a real 3.5" disk's case is near-square, not noticeably wider than tall */
/* label band within the disk sprite -- a real 3.5" disk's paper label, roughly */
#define LABEL_X 10
#define LABEL_Y 2
#define LABEL_W 54
#define LABEL_H 43

static U32 diskSprite[2][DISK_W * DISK_H]; /* [0] = RD1, [1] = RD2 */

static int current;        /* 0 = RD1 front, 1 = RD2 front */
static float transition;   /* 0 = settled on `current`; ramps to 1 during a carousel swap */
static int transitionDir;  /* +1 swapping to the right-hand disk, -1 to the left */
static float bouncePhase;  /* radians, free-running while idle */
static float titleAlpha;   /* 0..1, the bottom-center title label: in while idle, out mid-swap */
static float backFadeIn;   /* 0..1, the dim background disk fading in after it finishes exiting */
static U8 confirmed;       /* TRUE once Enter/fire chose a game */
static float confirmZoom;  /* 0..1, the post-confirm "flying at the viewer" wind-up */
static U8 oldFire, oldLeft, oldRight, oldEnter; /* edge-detect: act once per press, not per frame held */

/* the carousel's own music/scream, played through whichever game's real engine
   matches the disk currently front-and-center -- RD1 and RD2 each have their own
   separate emulated machine (different program images), so only one is ever live
   here at a time; swapping disks means tearing one down and bringing up the other,
   timed to happen in the silent middle of the crossfade below. */
static int audioIdx = -1;       /* 0 = RD1, 1 = RD2; -1 = no engine started yet */
static U8 audioSwapped;         /* one-shot per carousel swap: has the engine switched yet */

#define RGBA(r, g, b, a) ((U32)(U8)(r) | ((U32)(U8)(g) << 8) | ((U32)(U8)(b) << 16) | ((U32)(U8)(a) << 24))

static void putpx(U32 *buf, int w, int h, int x, int y, U32 c)
{
	if (x < 0 || x >= w || y < 0 || y >= h) return;
	buf[y * w + x] = c;
}

/*
 * drawDiskTemplate
 *
 * decodes the embedded diskette body art (gameselect_embedded.h) straight into
 * <buf> -- already exactly DISK_W x DISK_H, so no resampling needed. See that
 * header for what the art is and why it's already rotated/recoloured.
 */
static void drawDiskTemplate(U32 *buf)
{
	U8 *rgb;
	int w, h, comp;

	memset(buf, 0, (size_t)DISK_W * DISK_H * sizeof(U32));

	rgb = stbi_load_from_memory(gameselect_disk_body_png, (int)gameselect_disk_body_png_len,
		&w, &h, &comp, 4);
	if (!rgb || w != DISK_W || h != DISK_H)
	{
		if (rgb) stbi_image_free(rgb);
		return;
	}
	memcpy(buf, rgb, (size_t)DISK_W * DISK_H * sizeof(U32));
	stbi_image_free(rgb);
}

/*
 * compositeLabel
 *
 * nearest-neighbour stretches <src> (srcW x srcH RGB, opaque) into the sprite's
 * label band, overwriting the template's label-area pixels.
 */
static void compositeLabel(U32 *buf, const U8 *src, int srcW, int srcH, int srcComp)
{
	int x, y;

	for (y = 0; y < LABEL_H; y++)
	{
		int sy = y * srcH / LABEL_H;
		for (x = 0; x < LABEL_W; x++)
		{
			int sx = x * srcW / LABEL_W;
			const U8 *p = src + ((size_t)sy * (size_t)srcW + (size_t)sx) * (size_t)srcComp;
			putpx(buf, DISK_W, DISK_H, LABEL_X + x, LABEL_Y + y, RGBA(p[0], p[1], p[2], 0xFF));
		}
	}
}

/* RD1's fixed 16-colour ST palette (src/rd1/fb.c's RED/GREEN/BLUE, first 16 of 32 --
   the rest are sprite "highlight" variants pic_splash never indexes into) */
static const U8 rd1PalR[16] = { 0x00, 0xd8, 0xb0, 0xf8, 0x20, 0x00, 0x00, 0x20, 0x48, 0x48, 0x90, 0xd8, 0x48, 0x68, 0x90, 0xb0 };
static const U8 rd1PalG[16] = { 0x00, 0x00, 0x6c, 0x90, 0x24, 0x48, 0x6c, 0x48, 0x6c, 0x24, 0x48, 0x6c, 0x48, 0x6c, 0x90, 0xb4 };
static const U8 rd1PalB[16] = { 0x00, 0x00, 0x68, 0x68, 0x20, 0xb0, 0xd8, 0x00, 0x20, 0x00, 0x00, 0x00, 0x48, 0x68, 0x90, 0xb0 };

/*
 * decodeRD1Label
 *
 * unpacks pic_splash (320x200, 4 bits/pixel, src/rd1/dat_picsST.c) exactly like
 * img_paintPic (src/img.c) does, straight into an RGB24 buffer -- the real RD1
 * title picture, byte-exact, no screenshot. Crops the same title+character
 * region used to author the RD2 label PNG so the two line up.
 */
#define RD1_CROP_X 35
#define RD1_CROP_Y 5
#define RD1_CROP_W 240
#define RD1_CROP_H 190

static void decodeRD1Label(U8 *out /* RD1_CROP_W * RD1_CROP_H * 3 */)
{
	U8 full[320 * 200];
	U32 *pic = pic_splash;
	int pp = 0, row, col, k;

	for (row = 0; row < 200; row++)
	{
		for (col = 0; col < 320; col += 8)
		{
			U32 v = pic[pp++];
			for (k = 8; k--; v >>= 4)
				full[row * 320 + col + k] = (U8)(v & 0x0F);
		}
	}

	for (row = 0; row < RD1_CROP_H; row++)
	{
		for (col = 0; col < RD1_CROP_W; col++)
		{
			U8 idx = full[(RD1_CROP_Y + row) * 320 + (RD1_CROP_X + col)];
			U8 *d = out + ((size_t)row * (size_t)RD1_CROP_W + (size_t)col) * 3u;
			d[0] = rd1PalR[idx];
			d[1] = rd1PalG[idx];
			d[2] = rd1PalB[idx];
		}
	}
}

/*
 * blackenAboveLabel
 *
 * real disk labels commonly wrap slightly over the top edge of the case;
 * paints that same strip (directly above the label, same width) solid black
 * instead of leaving the diskette body's own plastic/rim visible there.
 */
static void blackenAboveLabel(U32 *buf)
{
	int x, y;

	for (y = 0; y < LABEL_Y; y++)
		for (x = LABEL_X; x < LABEL_X + LABEL_W; x++)
			putpx(buf, DISK_W, DISK_H, x, y, RGBA(0, 0, 0, 0xFF));
}

/*
 * audioStart / audioStop / audioSetFade
 *
 * bring up (or tear down) whichever game's real sound engine matches <idx>
 * (0 = RD1, 1 = RD2), and loop that game's own title music on it -- the exact
 * same engine, same program image, the real game itself uses; nothing here is
 * synthesized or a sample standing in for it. audioStart() reuses an engine
 * already left running by a previous carousel visit (xrick.c's sys_initSound
 * does the same isActive() check on the other side of the handoff once a disk
 * is confirmed) instead of always tearing down and rebuilding.
 */
#ifdef ENABLE_SOUND
static void audioStart(int idx)
{
	if (idx == 1) {
		if (!rd2_snd_isActive()) rd2_snd_init();
		rd2_snd_setFadeGain(0.0f);
		rd2_1a6aa(3, 0); /* RD2's own title/attract tune, same cue rd2_flow.c uses */
	} else {
		if (!syssnd_isActive()) syssnd_init();
		syssnd_setFadeGain(0.0f);
		sounds_setMusic(SND_TRACK_ATTRACT, 1); /* RD1's title tune, d1=1 loops */
	}
}

static void audioStop(int idx)
{
	if (idx == 1) rd2_snd_shutdown();
	else if (idx == 0) syssnd_shutdown();
}

static void audioSetFade(int idx, float gain)
{
	if (idx == 1) rd2_snd_setFadeGain(gain);
	else if (idx == 0) syssnd_setFadeGain(gain);
}

/* the real death "scream" -- WAV_DIE (track 19, d1=0) in RD1, play_sound id 0x0a in
   RD2 -- played once on confirm, over whichever engine is already running rather
   than a third, separate asset. Both are plain sound effects on their engine's own
   dedicated SFX voice, so they layer over the music instead of interrupting it,
   same as a real in-game death does. */
static void audioPlayScream(int idx)
{
	if (idx == 1) rd2_1a6aa(0x0a, 0);
	else if (idx == 0) syssnd_play(WAV_DIE);
}
#else
static void audioStart(int idx) { (void)idx; }
static void audioStop(int idx) { (void)idx; }
static void audioSetFade(int idx, float gain) { (void)idx; (void)gain; }
static void audioPlayScream(int idx) { (void)idx; }
#endif

/*
 * gameselect_start
 *
 * see gameselect.h
 */
void
gameselect_start(void)
{
	U8 *rd1rgb;
	U8 *rd2rgb;
	int rd2w, rd2h, rd2comp;

	current = sysarg_args_game == 2 ? 1 : 0;
	transition = 0.0f;
	transitionDir = 0;
	bouncePhase = 0.0f;
	titleAlpha = 0.0f;
	backFadeIn = 1.0f; /* already settled at startup -- only a post-swap reappearance fades in */
	confirmed = FALSE;
	confirmZoom = 0.0f;
	oldFire = oldLeft = oldRight = oldEnter = FALSE;

	/* this screen has no pause/settings of its own: Escape should quit the
	   program outright, not pop up the in-game menu (see gameselect_done) */
	menu_setEnabled(FALSE);

	audioSwapped = FALSE;
	audioIdx = current;
	audioStart(audioIdx);
	audioSetFade(audioIdx, 1.0f); /* full volume immediately -- nothing to fade from yet */

	drawDiskTemplate(diskSprite[0]);
	drawDiskTemplate(diskSprite[1]);

	rd1rgb = (U8 *)malloc((size_t)RD1_CROP_W * RD1_CROP_H * 3);
	if (rd1rgb)
	{
		decodeRD1Label(rd1rgb);
		compositeLabel(diskSprite[0], rd1rgb, RD1_CROP_W, RD1_CROP_H, 3);
		blackenAboveLabel(diskSprite[0]);
		free(rd1rgb);
	}

	rd2rgb = stbi_load_from_memory(gameselect_rd2_label_png, (int)gameselect_rd2_label_png_len,
		&rd2w, &rd2h, &rd2comp, 0);
	if (rd2rgb)
	{
		compositeLabel(diskSprite[1], rd2rgb, rd2w, rd2h, rd2comp);
		blackenAboveLabel(diskSprite[1]);
		stbi_image_free(rd2rgb);
	}
}

/*
 * blitRotScale
 *
 * the rotozoomer: every destination pixel in <dst> within range of the sprite's
 * (scaled) bounding box is mapped back through the inverse rotation+scale into
 * source space and sampled (nearest -- period-correct, no softening). <angle> is
 * in-plane (Z) only, matching how the disk must always face the viewer. <bright>
 * scales rgb (for the dimmed background disk); <alphaMul> scales the source
 * alpha (same knob, used for the carousel fade).
 */
static void blitRotScale(U32 *dst, int dw, int dh, const U32 *src, int sw, int sh,
	float cx, float cy, float angle, float scale, float bright, float alphaMul)
{
	float ca = cosf(angle), sa = sinf(angle);
	float halfExt = 0.5f * scale * sqrtf((float)(sw * sw + sh * sh)) + 1.0f;
	int x0 = (int)(cx - halfExt), x1 = (int)(cx + halfExt);
	int y0 = (int)(cy - halfExt), y1 = (int)(cy + halfExt);
	int x, y;

	if (scale <= 0.001f) return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= dw) x1 = dw - 1;
	if (y1 >= dh) y1 = dh - 1;

	for (y = y0; y <= y1; y++)
	{
		for (x = x0; x <= x1; x++)
		{
			float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
			float ux = (dx * ca + dy * sa) / scale;
			float uy = (-dx * sa + dy * ca) / scale;
			float sx = ux + (float)sw * 0.5f, sy = uy + (float)sh * 0.5f;
			int isx, isy;
			U32 sc;
			U8 sr, sg, sb, sa8;
			float a;
			U32 *dp;
			U8 dr, dg, db;

			if (sx < 0.0f || sx >= (float)sw || sy < 0.0f || sy >= (float)sh) continue;
			isx = (int)sx;
			isy = (int)sy;
			sc = src[isy * sw + isx];
			sa8 = (U8)(sc >> 24);
			if (sa8 == 0) continue;
			sr = (U8)sc;
			sg = (U8)(sc >> 8);
			sb = (U8)(sc >> 16);

			a = (float)sa8 / 255.0f * alphaMul;
			if (a <= 0.0f) continue;
			if (a > 1.0f) a = 1.0f;

			dp = &dst[y * dw + x];
			dr = (U8)*dp;
			dg = (U8)(*dp >> 8);
			db = (U8)(*dp >> 16);

			dr = (U8)((float)dr * (1.0f - a) + (float)sr * bright * a);
			dg = (U8)((float)dg * (1.0f - a) + (float)sg * bright * a);
			db = (U8)((float)db * (1.0f - a) + (float)sb * bright * a);
			*dp = RGBA(dr, dg, db, 0xFF);
		}
	}
}

/* shared with drawSpotlight: how far out (as a fraction of radius) the
   spotlight stays full strength before it starts falling off to black --
   the reflection fades on this same curve so it dims out in lockstep with
   the floor it's sitting on, instead of being hard-clipped at the ellipse's
   mathematical edge while the visible light there is already half gone */
#define SPOT_CORE 0.8f

/* how opaque the reflection is right at the object, and how much of the
   floor's own colour tints it (a reflection isn't a transparent "ghost" --
   it's floor-coloured, like it's bouncing off something, not a see-through
   copy floating over black) */
#define REFLECT_MAX_ALPHA 0.45f
#define REFLECT_TINT 0.35f

/*
 * blitReflection
 *
 * a cheap floor-reflection variant of blitRotScale. A true mirror image is
 * just the object's own sampling function evaluated at a vertically-flipped
 * screen position -- so this reuses blitRotScale's exact forward rotation
 * (<angle>, not negated) and only flips/squashes/shifts the *y coordinate
 * fed into it* (about <floorY>, the floor line, before sampling). An earlier
 * version negated the angle instead, hand-rolling the mirror inside the
 * rotated frame; that's wrong on two counts: it spins the tilt the wrong way
 * (a disk leaning right touched down its reflection's *left* corner instead
 * of its right -- the giveaway bug), and squashing inside that frame shears
 * the silhouette sideways whenever the disk isn't level. Reusing the same
 * forward formula at a transformed y avoids both: no separate handedness
 * logic to get backwards, no shear, since x is never touched by the flip.
 *
 * Three things beyond the geometry make it read as a reflection instead of a
 * floating translucent ghost: (1) opacity fades with distance from the
 * object, same as a real reflection dims the further it stretches from what's
 * casting it; (2) it's tinted by the floor's own colour; (3) its edge fades
 * out on the spotlight's own falloff curve (SPOT_CORE) instead of a hard cut
 * at the ellipse boundary.
 */
static void blitReflection(U32 *dst, int dw, int dh, const U32 *src, int sw, int sh,
	float cx, float cy, float floorY, float gap, float angle, float scale, float squashY,
	int spotCx, int spotCy, int spotRx, int spotRy, U8 tintR, U8 tintG, U8 tintB)
{
	float ca = cosf(angle), sa = sinf(angle); /* the object's own forward rotation --
		see function comment for why this must not be negated */
	float halfExt = 0.5f * scale * sqrtf((float)(sw * sw + sh * sh)) + 1.0f;
	float yCenter = floorY + gap + squashY * (floorY - cy);
	float yHalfExt = squashY * halfExt;
	int x0 = (int)(cx - halfExt), x1 = (int)(cx + halfExt);
	int y0 = (int)(yCenter - yHalfExt), y1 = (int)(yCenter + yHalfExt);
	int x, y;

	if (scale <= 0.001f || squashY <= 0.001f) return;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= dw) x1 = dw - 1;
	if (y1 >= dh) y1 = dh - 1;

	for (y = y0; y <= y1; y++)
	{
		float ey = ((float)y - (float)spotCy) / (float)spotRy;
		/* 0 right at the object, 1 by the time the reflection reaches the
		   bottom of the oval -- the far end of the floor it's cast on */
		float distT = ((float)y - (floorY + gap)) / (float)spotRy;
		float distFade;

		if (distT < 0.0f) distT = 0.0f;
		if (distT > 1.0f) distT = 1.0f;
		distFade = 1.0f - distT * distT * (3.0f - 2.0f * distT); /* smoothstep, 1..0 */

		for (x = x0; x <= x1; x++)
		{
			float ex = ((float)x - (float)spotCx) / (float)spotRx;
			float d = sqrtf(ex * ex + ey * ey);
			float edgeFade, t;
			float dx, dy, yPre, ux, uy, sx, sy;
			int isx, isy;
			U32 sc;
			U8 sr, sg, sb, sa8;
			float a;
			U32 *dp;
			U8 dr, dg, db;

			if (d > 1.0f) continue; /* outside the spotlight: no floor to reflect in */
			t = d < SPOT_CORE ? 0.0f : (d - SPOT_CORE) / (1.0f - SPOT_CORE);
			edgeFade = 1.0f - t * t * (3.0f - 2.0f * t);

			/* flip + squash the destination y about the floor line, in screen
			   space, before handing it to the object's own rotation formula */
			yPre = floorY - (((float)y + 0.5f - gap - floorY) / squashY);
			dx = (float)x + 0.5f - cx;
			dy = yPre - cy;
			ux = (dx * ca + dy * sa) / scale;
			uy = (-dx * sa + dy * ca) / scale;
			sx = ux + (float)sw * 0.5f;
			sy = uy + (float)sh * 0.5f;

			if (sx < 0.0f || sx >= (float)sw || sy < 0.0f || sy >= (float)sh) continue;
			isx = (int)sx;
			isy = (int)sy;
			sc = src[isy * sw + isx];
			sa8 = (U8)(sc >> 24);
			if (sa8 == 0) continue;
			sr = (U8)((float)(U8)sc * (1.0f - REFLECT_TINT) + (float)tintR * REFLECT_TINT);
			sg = (U8)((float)(U8)(sc >> 8) * (1.0f - REFLECT_TINT) + (float)tintG * REFLECT_TINT);
			sb = (U8)((float)(U8)(sc >> 16) * (1.0f - REFLECT_TINT) + (float)tintB * REFLECT_TINT);

			a = (float)sa8 / 255.0f * REFLECT_MAX_ALPHA * distFade * edgeFade;
			if (a <= 0.0f) continue;

			dp = &dst[y * dw + x];
			dr = (U8)*dp;
			dg = (U8)(*dp >> 8);
			db = (U8)(*dp >> 16);

			dr = (U8)((float)dr * (1.0f - a) + (float)sr * a);
			dg = (U8)((float)dg * (1.0f - a) + (float)sg * a);
			db = (U8)((float)db * (1.0f - a) + (float)sb * a);
			*dp = RGBA(dr, dg, db, 0xFF);
		}
	}
}

/* filled ellipse, full brightness out to 55% of each radius then a smooth
   falloff to black at the edge -- a soft-edged spotlight, not a flat disc */
static void drawSpotlight(U32 *fb, int cx, int cy, int rx, int ry, U32 color)
{
	int x, y;
	U8 cr = (U8)color, cg = (U8)(color >> 8), cb = (U8)(color >> 16);

	for (y = -ry; y <= ry; y++)
	{
		float fy = (float)y / (float)ry;

		for (x = -rx; x <= rx; x++)
		{
			float fx = (float)x / (float)rx;
			float d = sqrtf(fx * fx + fy * fy);
			float t, inten;

			if (d > 1.0f) continue;
			t = d < SPOT_CORE ? 0.0f : (d - SPOT_CORE) / (1.0f - SPOT_CORE);
			inten = 1.0f - t * t * (3.0f - 2.0f * t); /* smoothstep falloff */
			putpx(fb, GS_W, GS_H, cx + x, cy + y,
				RGBA((U8)((float)cr * inten), (U8)((float)cg * inten), (U8)((float)cb * inten), 0xFF));
		}
	}
}

/* a simple filled triangle, for the left/right hint arrows */
static void drawArrow(U32 *fb, int cx, int cy, int size, int pointRight, U32 color)
{
	int row;

	for (row = -size; row <= size; row++)
	{
		int w = size - (row < 0 ? -row : row);
		int x;

		for (x = 0; x <= w; x++)
			putpx(fb, GS_W, GS_H, cx + (pointRight ? x : -x), cy + row, color);
	}
}

#define SPOT_CX (GS_W / 2)
#define SPOT_CY (GS_H - 46)
#define SPOT_RX 118
#define SPOT_RY 30
/* resting position: the disk's bottom edge sits on the spotlight oval's own
   horizontal axis (its vertical center), so it reads as standing IN the light
   rather than in front of it -- not pinned to the screen bottom */
#define DISK_CY ((int)(SPOT_CY - (DISK_SCALE * DISK_H / 2.0f)))
#define DISK_SCALE 1.55f
#define SIDE_SCALE 0.85f
#define SIDE_OFFSET 108
#define TRANSITION_MS 320.0f
#define AUDIO_FADE_HALF 0.5f /* fraction of the swap (same clock as TRANSITION_MS)
                                 where the outgoing disk's music finishes fading
                                 out and the incoming disk's engine takes over,
                                 starting silent and fading back in by t=1 */
#define TITLE_FADE_MS 300.0f
#define BACK_FADE_MS 280.0f
#define CONFIRM_ZOOM_MS 260.0f  /* quick -- "briefly/rapidly", not a slow reveal */
#define CONFIRM_ZOOM_SCALE 6.0f /* overflows the screen at the end on purpose: a
                                    full-bleed zoom, not a framed, centered disk */
#define CONFIRM_ZOOM_DROP 70.0f /* drifts downward by this many px while it zooms,
                                    instead of growing in place */
#define TITLE_Y (GS_H - 14)
#define REFLECT_SQUASH 0.75f /* flattened vertically, like a real floor reflection */
#define REFLECT_GAP 16.0f /* extra px pushing the reflection down, away from the mirror
                              line, so a fast bounce never lets the two visibly touch/clip.
                              Needs to clear the disk's own diagonal-tilt protrusion (its
                              corner sticks down further than its half-height alone once
                              tilted), not just its flat resting edge. */

/*
 * gameselect_done
 *
 * see gameselect.h. advances the animation by one tick, draws and presents the
 * frame, and reports whether a choice has been confirmed. Called every frame on
 * both native (gameselect_run's loop) and web (xrick.c's own per-frame pump).
 */
U8
gameselect_done(void)
{
	static U32 frame[GS_W * GS_H];
	U8 fire, left, right, enter;
	int otherIdx = current ^ 1;
	float frontAngle, frontScale, frontCx, frontCy, frontBright, frontAlpha;
	float backAngle, backScale, backCx, backCy, backBright, backAlpha;

	/* RD2's title theme is two separate tracks chained by the real game itself,
	   not one: id 3 is a short ~6-chord sting that ends on its own, then id 0
	   (d1=1) is the actual loop -- rd2_flow.c's own rd2_178dc()/rd2_17c86() poll
	   [$1aa08] (0 once nothing is playing) every frame to chain the second one
	   in exactly this way; this is that same polling, so the carousel's copy
	   doesn't go silent after the sting. Kept unconditional here, ahead of the
	   confirmed-branch's early returns below, rather than further down with the
	   idle/transition logic: that placement stopped running the moment Enter
	   was pressed (the whole ~260ms confirm-zoom wind-up returns before ever
	   reaching it), and if the loop's own natural restart gap (same brief
	   [$1aa08]==0 blip this poll exists to catch) landed in that window with
	   nothing polling, rd2_178dc() would find it 0 once the real game started
	   and replay the sting after all -- found 2026-10-11, confirmed still
	   happening even though the loop was, in isolation, chaining correctly. */
	if (audioIdx == 1 && rd2_snd_rw(0x1aa08) == 0)
		rd2_1a6aa(0, 1);

	if (confirmed)
	{
		/* a quick "flying at the viewer" wind-up instead of an instant cut:
		   the chosen disk's label rushes toward the screen and everything
		   fades to black, landing on black right as the real game takes
		   over. No attempt to match the real title screen's exact framing
		   (the label is a decorative crop, not guaranteed pixel-identical
		   to it) -- the fade to black hides any mismatch instead. */
		float e;
		U32 *fp;
		size_t i;

		sysevt_poll(); /* still pump events -- e.g. the window's own close button */

		confirmZoom += (1000.0f / CONFIRM_ZOOM_MS) * ((float)GAMESELECT_FRAME_MS / 1000.0f);
		if (confirmZoom > 1.0f) confirmZoom = 1.0f;
		e = confirmZoom * confirmZoom * (3.0f - 2.0f * confirmZoom);

		memset(frame, 0, sizeof(frame));
		blitRotScale(frame, GS_W, GS_H, diskSprite[current], DISK_W, DISK_H,
			(float)(GS_W / 2), DISK_CY + CONFIRM_ZOOM_DROP * e, 0.0f,
			DISK_SCALE + (CONFIRM_ZOOM_SCALE - DISK_SCALE) * e, 1.0f, 1.0f);

		fp = frame;
		for (i = 0; i < (size_t)GS_W * GS_H; i++, fp++)
		{
			U8 r = (U8)*fp, g = (U8)(*fp >> 8), b = (U8)(*fp >> 16);
			r = (U8)((float)r * (1.0f - e));
			g = (U8)((float)g * (1.0f - e));
			b = (U8)((float)b * (1.0f - e));
			*fp = RGBA(r, g, b, 0xFF);
		}

		sysvid_updateRGBA(frame);

		if (confirmZoom < 1.0f)
			return FALSE;
		menu_setEnabled(TRUE); /* re-enable it for the game that's about to start */
		return TRUE;
	}

	sysevt_poll();

	/* Escape (or closing the window): quit immediately, no confirmation --
	   the menu is disabled here (gameselect_start), so this is the only
	   handler Escape reaches */
	if (control_status & CONTROL_EXIT) {
		sys_shutdown();
#ifdef __EMSCRIPTEN__
		emscripten_force_exit(0);
#else
		exit(0);
#endif
	}

	fire = (control_status & CONTROL_FIRE) ? TRUE : FALSE;
	left = (control_status & CONTROL_LEFT) ? TRUE : FALSE;
	right = (control_status & CONTROL_RIGHT) ? TRUE : FALSE;
	enter = SDL_GetKeyboardState(NULL)[SDL_SCANCODE_RETURN] ? TRUE : FALSE; /* confirms here too, like the menu */

	if (transition == 0.0f)
	{
		if (left && !oldLeft) { transitionDir = -1; transition = 0.0001f; }
		else if (right && !oldRight) { transitionDir = 1; transition = 0.0001f; }
	}
	oldLeft = left;
	oldRight = right;

	if (transition > 0.0f)
	{
		/* the audio crossfade shares this same clock but runs on its own schedule
		   within it (AUDIO_FADE_HALF): fade the outgoing disk's music out over the
		   first half, swap to the incoming disk's engine in the silent middle, fade
		   that one in over the second half -- using <t>, this frame's progress
		   before the end-of-swap wrap below, so the fade-in's last frame (t -> 1)
		   isn't lost to transition being reset to 0.0f in the same step. */
		float t = transition + (1000.0f / TRANSITION_MS) * ((float)GAMESELECT_FRAME_MS / 1000.0f);

		if (t < AUDIO_FADE_HALF)
			audioSetFade(audioIdx, 1.0f - t / AUDIO_FADE_HALF);
		else
		{
			float fadeIn = (t - AUDIO_FADE_HALF) / (1.0f - AUDIO_FADE_HALF);

			if (!audioSwapped)
			{
				audioStop(audioIdx);
				audioIdx = otherIdx; /* the disk becoming current -- see below */
				audioStart(audioIdx);
				audioSwapped = TRUE;
			}
			audioSetFade(audioIdx, fadeIn > 1.0f ? 1.0f : fadeIn);
		}

		transition = t;
		if (transition >= 1.0f)
		{
			U8 cameFromRight = transitionDir > 0;

			transition = 0.0f;
			transitionDir = 0;
			audioSwapped = FALSE;
			current = otherIdx;
			otherIdx = current ^ 1;
			/* idle always shows the dim background disk fixed on the right
			   (see below), so: a right-press's outgoing disk exits toward
			   the left -- a spot idle never shows -- and needs to fade back
			   in now that it rests there; a left-press's outgoing disk exits
			   toward the right, exactly where idle already shows it, so it
			   was visible there the whole time and needs no fade-in now. */
			backFadeIn = cameFromRight ? 0.0f : 1.0f;
			/* the disk arrives dead level (frontAngle is always 0.0f during a
			   swap) -- resume the idle hop from the one phase where its own
			   tilt is also exactly 0 (PI/2) instead of wherever bouncePhase
			   happened to drift to while the swap played out, which could be
			   any angle at all and made the disk visibly snap to it */
			bouncePhase = 1.5707963f;
		}
	}

	bouncePhase += 0.14f;
	if (bouncePhase > 6.2832f) bouncePhase -= 6.2832f;

	/* the title label: fades in while settled on `current` (idle bounce),
	   fades out the moment a swap starts -- current only flips to otherIdx
	   once the swap completes, so this naturally tracks the disk that's
	   actually front-and-center the whole time, not the one arriving */
	{
		float titleTarget = (transition == 0.0f) ? 1.0f : 0.0f;
		float titleStep = (1000.0f / TITLE_FADE_MS) * ((float)GAMESELECT_FRAME_MS / 1000.0f);
		if (titleAlpha < titleTarget) titleAlpha = (titleAlpha + titleStep < titleTarget) ? titleAlpha + titleStep : titleTarget;
		else if (titleAlpha > titleTarget) titleAlpha = (titleAlpha - titleStep > titleTarget) ? titleAlpha - titleStep : titleTarget;
	}

	if (transition > 0.0f)
	{
		/* ease in/out, classic 3-2t smoothstep */
		float t = transition;
		float e = t * t * (3.0f - 2.0f * t);

		frontCy = backCy = DISK_CY;
		frontAngle = backAngle = 0.0f;

		/* idle always shows the dim background disk fixed on the right. A
		   right press's outgoing disk heads for the left -- a spot idle never
		   shows -- so it fades out over the whole slide instead of just
		   shrinking/dimming and then snapping away; its incoming disk started
		   exactly where idle already had it, dim, on the right, so it's fully
		   visible throughout and simply slides in. */
		if (transitionDir > 0)
		{
			frontCx = (float)(GS_W / 2) - e * SIDE_OFFSET;
			frontScale = DISK_SCALE + (SIDE_SCALE - DISK_SCALE) * e;
			frontBright = 1.0f - 0.65f * e;
			frontAlpha = 1.0f - e;

			backCx = (float)(GS_W / 2) + (1.0f - e) * SIDE_OFFSET;
			backScale = SIDE_SCALE + (DISK_SCALE - SIDE_SCALE) * e;
			backBright = 0.35f + 0.65f * e;
			backAlpha = 1.0f;
		}
		else
		{
			/* a left press's outgoing disk heads for the right -- idle's own
			   fixed spot -- so it stays fully visible, no fade needed, same
			   as the mirrored case above. Its "incoming" disk is the one that
			   needs care: this is the disk idle already had sitting dim on
			   the right a moment ago, and it must relocate to the left before
			   sliding in from there -- so it fades out quickly right where it
			   already was (SWITCH_FRAC of the swap, snappy, no slide) and only
			   then jumps to the left and slides/fades in the usual way,
			   instead of popping straight to the left mid-fade. */
			const float SWITCH_FRAC = 0.15f;

			frontCx = (float)(GS_W / 2) + e * SIDE_OFFSET;
			frontScale = DISK_SCALE + (SIDE_SCALE - DISK_SCALE) * e;
			frontBright = 1.0f - 0.65f * e;
			frontAlpha = 1.0f;

			if (t < SWITCH_FRAC)
			{
				backCx = (float)(GS_W / 2) + SIDE_OFFSET;
				backScale = SIDE_SCALE;
				backBright = 0.35f;
				backAlpha = 1.0f - t / SWITCH_FRAC;
			}
			else
			{
				float t2 = (t - SWITCH_FRAC) / (1.0f - SWITCH_FRAC);
				float e2 = t2 * t2 * (3.0f - 2.0f * t2);

				backCx = (float)(GS_W / 2) - (1.0f - e2) * SIDE_OFFSET;
				backScale = SIDE_SCALE + (DISK_SCALE - SIDE_SCALE) * e2;
				backBright = 0.35f + 0.65f * e2;
				backAlpha = e2;
			}
		}
	}
	else
	{
		/* hopping foot to foot, not rolling in place: fully committed to one
		   corner (max tilt) exactly when grounded (bob == 0), leveling out
		   toward upright at the apex of each hop (bob at its peak) -- so the
		   tilt and the hop are 90 degrees out of phase, not in lockstep */
		/* bob follows a parabolic arc (constant "gravity") per hop instead of a
		   sine wave: launches fast off the planted foot, eases to a hang at the
		   apex, then eases back in for a fast landing on the other foot */
		float hopT = fmodf(bouncePhase, 3.14159265f) / 3.14159265f; /* 0..1 within this hop */
		float tilt = cosf(bouncePhase) * 0.22f; /* seesaw, Z-axis only */
		float bob = 4.0f * hopT * (1.0f - hopT) * 7.0f;
		float sway = cosf(bouncePhase) * 10.0f; /* weight shift toward the planted foot */

		frontCx = (float)(GS_W / 2) + sway;
		frontCy = DISK_CY - bob;
		frontScale = DISK_SCALE;
		frontAngle = tilt;
		frontBright = 1.0f;
		frontAlpha = 1.0f;

		/* the background disk fades in dim, rather than snapping straight to
		   0.35 brightness -- most noticeable right after a swap, when this is
		   the disk that just finished fading out on its way off-screen */
		if (backFadeIn < 1.0f)
		{
			backFadeIn += (1000.0f / BACK_FADE_MS) * ((float)GAMESELECT_FRAME_MS / 1000.0f);
			if (backFadeIn > 1.0f) backFadeIn = 1.0f;
		}

		backCx = (float)(GS_W / 2) + SIDE_OFFSET;
		backCy = DISK_CY;
		backScale = SIDE_SCALE;
		backAngle = 0.0f;
		backBright = 0.35f;
		backAlpha = backFadeIn;
	}

	if (((fire && !oldFire) || (enter && !oldEnter)) && transition == 0.0f)
	{
		confirmed = TRUE;
		sysarg_args_game = current == 1 ? 2 : 1; /* the confirm-zoom wind-up (above,
			next frame onward) is what actually reports TRUE and hands off */
		audioSetFade(audioIdx, 1.0f); /* idle already holds it at 1.0 -- just in case */
		audioPlayScream(audioIdx);    /* the same "waaaaa" a real death plays, layered
			over the music rather than stopping it, same as it does in-game. audioIdx
			already matches current/sysarg_args_game above, and is left running for
			xrick.c's sys_initSound() to hand off into the real game unchanged. */
	}
	oldFire = fire;
	oldEnter = enter;

	/* ---- draw ---- */
	memset(frame, 0, sizeof(frame));
	drawSpotlight(frame, SPOT_CX, SPOT_CY, SPOT_RX, SPOT_RY, RGBA(0xD8, 0x6C, 0x00, 0xFF));

	/* the front disk's own floor reflection, mirrored about the spotlight's
	   own horizontal axis (the same line its bottom edge rests on) -- only
	   while idle-bouncing: a carousel swap or the confirm-zoom already have
	   enough going on without reflecting those too */
	if (transition == 0.0f)
		blitReflection(frame, GS_W, GS_H, diskSprite[current], DISK_W, DISK_H,
			frontCx, frontCy, (float)SPOT_CY, REFLECT_GAP, frontAngle, frontScale, REFLECT_SQUASH,
			SPOT_CX, SPOT_CY, SPOT_RX, SPOT_RY, 0xD8, 0x6C, 0x00);

	/* background disk first (behind), then front disk on top */
	blitRotScale(frame, GS_W, GS_H, diskSprite[otherIdx], DISK_W, DISK_H,
		backCx, backCy, backAngle, backScale, backBright, backAlpha);
	blitRotScale(frame, GS_W, GS_H, diskSprite[current], DISK_W, DISK_H,
		frontCx, frontCy, frontAngle, frontScale, frontBright, frontAlpha);

	drawArrow(frame, SPOT_CX - 150, DISK_CY, 7, FALSE, RGBA(0xE0, 0x30, 0x30, 0xFF));
	drawArrow(frame, SPOT_CX + 150, DISK_CY, 7, TRUE, RGBA(0xE0, 0x30, 0x30, 0xFF));

	if (titleAlpha > 0.0f)
	{
		static const char *const gameTitle[2] = {"RICK DANGEROUS", "RICK DANGEROUS II"};
		const char *title = gameTitle[current];
		int tw = sysvid_overlayTextWidth(title);
		U8 a = (U8)(titleAlpha * 255.0f + 0.5f);

		sysvid_drawTextAlpha(frame, GS_W, GS_H, (GS_W - tw) / 2, TITLE_Y, title, 0xFF, 0xFF, 0xFF, a);
	}

	sysvid_updateRGBA(frame);

	return FALSE; /* the confirm-zoom wind-up (top of this function, from the
	                  next frame on) is what eventually reports TRUE */
}

/*
 * gameselect_run
 *
 * see gameselect.h
 */
void
gameselect_run(void)
{
	gameselect_start();
	while (!gameselect_done())
#ifdef __EMSCRIPTEN__
		/* yields to the browser (repaint, input, the works) and resumes here --
		   same trick rd2_sys_pump relies on for RD2's own non-callback main loop
		   to work on the web at all (build.sh's ASYNCIFY instruments whichever
		   functions the link-time advise pass finds reaching this call, this one
		   included, no manual list to update) */
		emscripten_sleep(GAMESELECT_FRAME_MS);
#else
		sys_sleep(GAMESELECT_FRAME_MS);
#endif
}

/* eof */

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

#include "system.h"
#include "sysvid.h"
#include "sysevt.h"
#include "control.h"
#include "sysarg.h"
#include "menu.h"
#include "gameselect.h"
#include "gameselect_embedded.h"
#include "thirdparty/stb_image.h"

#include "rd1/pics.h" /* pic_splash: RD1's real title picture, decoded live below */

#define GS_W SYSVID_WIDTH  /* 320 */
#define GS_H SYSVID_HEIGHT /* 200 */
#define GAMESELECT_FRAME_MS 40 /* ~25fps, same pace as the rest of the game */

/* the composited diskette sprite (template + label), built once per game at
   gameselect_start() and only ever rotated/scaled/dimmed from there on */
#define DISK_W 72
#define DISK_H 64
/* label band within the disk sprite -- a real 3.5" disk's paper label, roughly */
#define LABEL_X 7
#define LABEL_Y 9
#define LABEL_W 58
#define LABEL_H 30

static U32 diskSprite[2][DISK_W * DISK_H]; /* [0] = RD1, [1] = RD2 */

static int current;        /* 0 = RD1 front, 1 = RD2 front */
static float transition;   /* 0 = settled on `current`; ramps to 1 during a carousel swap */
static int transitionDir;  /* +1 swapping to the right-hand disk, -1 to the left */
static float bouncePhase;  /* radians, free-running while idle */
static U8 confirmed;       /* TRUE once Enter/fire chose a game */
static U8 oldFire, oldLeft, oldRight; /* edge-detect: act once per press, not per frame held */

#define RGBA(r, g, b, a) ((U32)(U8)(r) | ((U32)(U8)(g) << 8) | ((U32)(U8)(b) << 16) | ((U32)(U8)(a) << 24))

static void putpx(U32 *buf, int w, int h, int x, int y, U32 c)
{
	if (x < 0 || x >= w || y < 0 || y >= h) return;
	buf[y * w + x] = c;
}

/*
 * drawDiskTemplate
 *
 * a plain 3.5" diskette silhouette: light plastic body, a corner notch (top-left,
 * the classic cut corner), and a metal shutter band near the bottom. Transparent
 * (alpha 0) outside the silhouette, so blitRotScale only ever shows disk-shaped
 * pixels over whatever is behind it.
 */
static void drawDiskTemplate(U32 *buf)
{
	int x, y;
	const U32 body = RGBA(0xB4, 0xB6, 0xC2, 0xFF);
	const U32 bodyShade = RGBA(0x9A, 0x9C, 0xA8, 0xFF);
	const U32 shutter = RGBA(0xC8, 0xCA, 0xD2, 0xFF);
	const U32 shutterSlot = RGBA(0x30, 0x30, 0x34, 0xFF);
	const int notch = 10; /* top-left cut corner size */

	memset(buf, 0, (size_t)DISK_W * DISK_H * sizeof(U32));

	for (y = 0; y < DISK_H; y++)
	{
		for (x = 0; x < DISK_W; x++)
		{
			U32 c;

			if (x + y < notch) /* cut corner */
				continue;
			if (x < 3 || x >= DISK_W - 3 || y < 2)
				c = bodyShade; /* thin rim */
			else
				c = body;
			putpx(buf, DISK_W, DISK_H, x, y, c);
		}
	}

	/* metal shutter band, lower third, with a dark slot cut into it */
	for (y = DISK_H - 18; y < DISK_H - 4; y++)
	{
		for (x = 6; x < DISK_W - 6; x++)
		{
			U32 c = shutter;
			if (y >= DISK_H - 14 && y < DISK_H - 8 && x >= DISK_W / 2 - 10 && x < DISK_W / 2 + 10)
				c = shutterSlot;
			putpx(buf, DISK_W, DISK_H, x, y, c);
		}
	}
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
	confirmed = FALSE;
	oldFire = oldLeft = oldRight = FALSE;

	/* this screen has no pause/settings of its own: Escape should quit the
	   program outright, not pop up the in-game menu (see gameselect_done) */
	menu_setEnabled(FALSE);

	drawDiskTemplate(diskSprite[0]);
	drawDiskTemplate(diskSprite[1]);

	rd1rgb = (U8 *)malloc((size_t)RD1_CROP_W * RD1_CROP_H * 3);
	if (rd1rgb)
	{
		decodeRD1Label(rd1rgb);
		compositeLabel(diskSprite[0], rd1rgb, RD1_CROP_W, RD1_CROP_H, 3);
		free(rd1rgb);
	}

	rd2rgb = stbi_load_from_memory(gameselect_rd2_label_png, (int)gameselect_rd2_label_png_len,
		&rd2w, &rd2h, &rd2comp, 0);
	if (rd2rgb)
	{
		compositeLabel(diskSprite[1], rd2rgb, rd2w, rd2h, rd2comp);
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

/* filled ellipse, flat colour -- the spotlight */
static void drawSpotlight(U32 *fb, int cx, int cy, int rx, int ry, U32 color)
{
	int y;

	for (y = -ry; y <= ry; y++)
	{
		float f = 1.0f - ((float)(y * y) / (float)(ry * ry));
		int hw, x;

		if (f < 0.0f) continue;
		hw = (int)((float)rx * sqrtf(f));
		for (x = -hw; x <= hw; x++)
			putpx(fb, GS_W, GS_H, cx + x, cy + y, color);
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
	U8 fire, left, right;
	int otherIdx = current ^ 1;
	float frontAngle, frontScale, frontCx, frontCy, frontBright;
	float backAngle, backScale, backCx, backCy, backBright, backAlpha;

	if (confirmed)
		return TRUE;

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

	if (transition == 0.0f)
	{
		if (left && !oldLeft) { transitionDir = -1; transition = 0.0001f; }
		else if (right && !oldRight) { transitionDir = 1; transition = 0.0001f; }
	}
	oldLeft = left;
	oldRight = right;

	if (transition > 0.0f)
	{
		transition += (1000.0f / TRANSITION_MS) * ((float)GAMESELECT_FRAME_MS / 1000.0f);
		if (transition >= 1.0f)
		{
			transition = 0.0f;
			transitionDir = 0;
			current = otherIdx;
			otherIdx = current ^ 1;
		}
	}

	bouncePhase += 0.09f;
	if (bouncePhase > 6.2832f) bouncePhase -= 6.2832f;

	if (transition > 0.0f)
	{
		/* ease in/out, classic 3-2t smoothstep */
		float t = transition;
		float e = t * t * (3.0f - 2.0f * t);
		float sideSign = (float)transitionDir;

		frontCx = (float)(GS_W / 2) - sideSign * e * SIDE_OFFSET;
		frontCy = DISK_CY;
		frontScale = DISK_SCALE + (SIDE_SCALE - DISK_SCALE) * e;
		frontAngle = 0.0f;
		frontBright = 1.0f - 0.65f * e;

		backCx = (float)(GS_W / 2) + sideSign * (1.0f - e) * SIDE_OFFSET;
		backCy = DISK_CY;
		backScale = SIDE_SCALE + (DISK_SCALE - SIDE_SCALE) * e;
		backAngle = 0.0f;
		backBright = 0.35f + 0.65f * e;
		backAlpha = 1.0f;
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

		backCx = (float)(GS_W / 2) + SIDE_OFFSET;
		backCy = DISK_CY;
		backScale = SIDE_SCALE;
		backAngle = 0.0f;
		backBright = 0.35f;
		backAlpha = 1.0f;
	}

	if (fire && !oldFire && transition == 0.0f)
		confirmed = TRUE;
	oldFire = fire;

	if (confirmed)
		menu_setEnabled(TRUE); /* re-enable it for the game that's about to start */

	/* ---- draw ---- */
	memset(frame, 0, sizeof(frame));
	drawSpotlight(frame, SPOT_CX, SPOT_CY, SPOT_RX, SPOT_RY, RGBA(0x60, 0xA8, 0xE8, 0xFF));

	/* background disk first (behind), then front disk on top */
	blitRotScale(frame, GS_W, GS_H, diskSprite[otherIdx], DISK_W, DISK_H,
		backCx, backCy, backAngle, backScale, backBright, backAlpha);
	blitRotScale(frame, GS_W, GS_H, diskSprite[current], DISK_W, DISK_H,
		frontCx, frontCy, frontAngle, frontScale, frontBright, 1.0f);

	drawArrow(frame, SPOT_CX - 150, DISK_CY, 7, FALSE, RGBA(0xE0, 0x30, 0x30, 0xFF));
	drawArrow(frame, SPOT_CX + 150, DISK_CY, 7, TRUE, RGBA(0xE0, 0x30, 0x30, 0xFF));

	sysvid_updateRGBA(frame);

	if (confirmed)
		sysarg_args_game = current == 1 ? 2 : 1;
	return confirmed;
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

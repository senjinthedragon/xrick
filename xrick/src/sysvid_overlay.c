/*
 * xrick/src/sysvid_overlay.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * OSD messages and the overlay primitives the settings menu (menu.c)
 * draws itself with -- extracted out of sysvid_gpu.c (Phase 4, web menu
 * support) so sysvid_gl.c can share the exact same drawing code instead
 * of reimplementing it. Everything here is plain RGBA8 pixel blending
 * into a buffer the caller owns; no GPU/GL/web-specific code at all.
 */

#include <string.h> /* strlen, strncpy */

#include "system.h"
#include "sysvid_overlay.h"

static U32 *ovl_pixels;
static U16 ovl_w, ovl_h;

void
sysvid_overlay_init(U32 *pixels, U16 w, U16 h)
{
	ovl_pixels = pixels;
	ovl_w = w;
	ovl_h = h;
}

#define OSD_DURATION_MS 5000
#define OSD_SCALE 1
#define OSD_MARGIN 6
#define OSD_BG_ALPHA 110 /* 0-255 opacity of the backing box, blended over whatever's underneath -- ~43% opaque / 57% see-through */
#define OSD_MSG_MAX 32

static char osdMessage[OSD_MSG_MAX];
static U32 osdExpireAt;

/* tiny 5x7 bitmap font (upper case, digits, common punctuation) -- used by
 * the OSD and the settings menu; add more glyphs as needed */
typedef struct {
	char c;
	U8 rows[7];
} osdGlyph_t;
static const osdGlyph_t osdFont[] = {
    {'0', {0x0E, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0E}},
    {'4', {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02}},
    {'8', {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E}},
    {'A', {0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'B', {0x1E, 0x11, 0x11, 0x1E, 0x11, 0x11, 0x1E}},
    {'Z', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1F}},
    {'C', {0x0E, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0E}},
    {'D', {0x1E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1E}},
    {'E', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x1F}},
    {'F', {0x1F, 0x10, 0x10, 0x1E, 0x10, 0x10, 0x10}},
    {'G', {0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0E}},
    {'H', {0x11, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11}},
    {'I', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F}},
    {'M', {0x11, 0x1B, 0x15, 0x11, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}},
    {'O', {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'P', {0x1E, 0x11, 0x11, 0x1E, 0x10, 0x10, 0x10}},
    {'R', {0x1E, 0x11, 0x11, 0x1E, 0x14, 0x12, 0x11}},
    {'S', {0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E}},
    {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E}},
    {'Y', {0x11, 0x11, 0x0A, 0x04, 0x04, 0x04, 0x04}},
    {'1', {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x1F}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}},
    {'2', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F}},
    {'3', {0x1F, 0x02, 0x04, 0x02, 0x01, 0x11, 0x0E}},
    {'5', {0x1F, 0x10, 0x1E, 0x01, 0x01, 0x11, 0x0E}},
    {'6', {0x06, 0x08, 0x10, 0x1E, 0x11, 0x11, 0x0E}},
    {'7', {0x1F, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}},
    {'9', {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x02, 0x0C}},
    {'J', {0x07, 0x02, 0x02, 0x02, 0x02, 0x12, 0x0C}},
    {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'Q', {0x0E, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0D}},
    {'T', {0x1F, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0A, 0x04}},
    {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x1B, 0x11}},
    {'X', {0x11, 0x11, 0x0A, 0x04, 0x0A, 0x11, 0x11}},
    {'.', {0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x0C}},
    {',', {0x00, 0x00, 0x00, 0x00, 0x0C, 0x04, 0x08}},
    {'-', {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00}},
    {'+', {0x00, 0x04, 0x04, 0x1F, 0x04, 0x04, 0x00}},
    {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'%', {0x18, 0x19, 0x02, 0x04, 0x08, 0x13, 0x03}},
    {'&', {0x0C, 0x12, 0x14, 0x08, 0x15, 0x12, 0x0D}},
    {'(', {0x02, 0x04, 0x08, 0x08, 0x08, 0x04, 0x02}},
    {')', {0x08, 0x04, 0x02, 0x02, 0x02, 0x04, 0x08}},
    {'[', {0x0E, 0x08, 0x08, 0x08, 0x08, 0x08, 0x0E}},
    {']', {0x0E, 0x02, 0x02, 0x02, 0x02, 0x02, 0x0E}},
    {'<', {0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02}},
    {'>', {0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08}},
    {'=', {0x00, 0x00, 0x1F, 0x00, 0x1F, 0x00, 0x00}},
    {';', {0x00, 0x0C, 0x0C, 0x00, 0x0C, 0x04, 0x08}},
    {'\'', {0x04, 0x04, 0x08, 0x00, 0x00, 0x00, 0x00}},
    {'`', {0x08, 0x04, 0x02, 0x00, 0x00, 0x00, 0x00}},
    {'\\', {0x10, 0x10, 0x08, 0x04, 0x02, 0x01, 0x01}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1F}},
    {'?', {0x0E, 0x11, 0x01, 0x02, 0x04, 0x00, 0x04}},
    {'!', {0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04}},
    {'*', {0x00, 0x11, 0x0A, 0x1F, 0x0A, 0x11, 0x00}},
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}},
};
#define OSD_FONT_COUNT (sizeof(osdFont) / sizeof(osdFont[0]))


/*
 * sysvid_showOSD
 *
 * arms the on-screen message shown for OSD_DURATION_MS after this call.
 */
void
sysvid_showOSD(const char *msg)
{
	strncpy(osdMessage, msg, OSD_MSG_MAX - 1);
	osdMessage[OSD_MSG_MAX - 1] = '\0';
	osdExpireAt = sys_gettime() + OSD_DURATION_MS;
}


/*
 * sysvid_osdActive
 *
 * true while the OSD's timer is still counting down. sysevt_wait() checks
 * this to avoid blocking indefinitely on a static screen (menu, pause,
 * Hall of Fame) -- without it, the game loop simply wouldn't tick again
 * until the next real input event, so the OSD would never get a chance
 * to notice its timer expired and clear itself.
 */
U8
sysvid_osdActive(void)
{
	return sys_gettime() < osdExpireAt;
}


static const U8 *
osdFindGlyph(char c)
{
	U32 i;
	for (i = 0; i < OSD_FONT_COUNT; i++)
		if (osdFont[i].c == c) return osdFont[i].rows;
	return NULL; /* unknown char -- just skip it */
}


/*
 * sysvid_drawOSD
 *
 * stamps the current OSD message into the composited RGBA frame
 * (top-left corner), with a translucent dark backing box -- blended by
 * hand against whatever's already in the buffer, since the shader chain
 * downstream doesn't do alpha blending -- for legibility without fully
 * hiding the gameplay underneath. Text itself stays fully opaque.
 */
void
sysvid_drawOSD(void)
{
	int len = (int)strlen(osdMessage);
	int textW = len * 6 * OSD_SCALE - OSD_SCALE; /* 5px glyph + 1px gap, scaled */
	int textH = 7 * OSD_SCALE;
	int x0 = OSD_MARGIN;
	int y0 = OSD_MARGIN;
	int i, gx, gy;

	for (gy = -OSD_MARGIN / 2; gy < textH + OSD_MARGIN / 2; gy++) {
		int py = y0 + gy;
		if (py < 0 || py >= (int)ovl_h) continue;
		for (gx = -OSD_MARGIN / 2; gx < textW + OSD_MARGIN / 2; gx++) {
			int px = x0 + gx;
			U8 *p;
			if (px < 0 || px >= (int)ovl_w) continue;
			p = (U8 *)ovl_pixels + (py * ovl_w + px) * 4;
			/* blend toward black at OSD_BG_ALPHA/255 opacity */
			p[0] = (U8)((int)p[0] * (255 - OSD_BG_ALPHA) / 255);
			p[1] = (U8)((int)p[1] * (255 - OSD_BG_ALPHA) / 255);
			p[2] = (U8)((int)p[2] * (255 - OSD_BG_ALPHA) / 255);
			p[3] = 255;
		}
	}

	for (i = 0; i < len; i++) {
		const U8 *glyphRows = osdFindGlyph(osdMessage[i]);
		if (!glyphRows) continue;
		for (gy = 0; gy < 7; gy++) {
			U8 row = glyphRows[gy];
			for (gx = 0; gx < 5; gx++) {
				int sx, sy;
				if (!(row & (0x10 >> gx))) continue;
				for (sy = 0; sy < OSD_SCALE; sy++) {
					for (sx = 0; sx < OSD_SCALE; sx++) {
						int px = x0 + (i * 6 + gx) * OSD_SCALE + sx;
						int py = y0 + gy * OSD_SCALE + sy;
						U8 *p;
						if (px < 0 || px >= (int)ovl_w || py < 0 || py >= (int)ovl_h) continue;
						p = (U8 *)ovl_pixels + (py * ovl_w + px) * 4;
						p[0] = 255;
						p[1] = 255;
						p[2] = 255;
						p[3] = 255;
					}
				}
			}
		}
	}
}


/*
 * Overlay drawing, for the settings menu: alpha-blended rects and text
 * stamped straight into the composited frame (menu_draw() is called from
 * each backend's present function after the game frame is in place).
 */
void
sysvid_overlayRect(int x, int y, int w, int h, U8 r, U8 g, U8 b, U8 a)
{
	int px, py;

	for (py = y; py < y + h; py++) {
		if (py < 0 || py >= (int)ovl_h) continue;
		for (px = x; px < x + w; px++) {
			U8 *p;
			if (px < 0 || px >= (int)ovl_w) continue;
			p = (U8 *)ovl_pixels + (py * ovl_w + px) * 4;
			p[0] = (U8)(((int)p[0] * (255 - a) + (int)r * a) / 255);
			p[1] = (U8)(((int)p[1] * (255 - a) + (int)g * a) / 255);
			p[2] = (U8)(((int)p[2] * (255 - a) + (int)b * a) / 255);
			p[3] = 255;
		}
	}
}


int
sysvid_overlayTextWidth(const char *s)
{
	int len = (int)strlen(s);
	return len ? len * 6 - 1 : 0;
}


/*
 * sysvid_drawTextAlpha
 *
 * see sysvid_overlay.h. Same glyphs as sysvid_overlayText, but into any
 * caller-owned RGBA8 buffer (not just the registered overlay target) and
 * alpha-blended rather than a flat overwrite -- gameselect.c's own carousel
 * frame is never registered via sysvid_overlay_init (it has no overlay of
 * its own, it IS the frame), so it draws into its buffer directly this way.
 */
void
sysvid_drawTextAlpha(U32 *pixels, U16 w, U16 h, int x, int y, const char *s, U8 r, U8 g, U8 b, U8 alpha)
{
	int i, gx, gy;
	float a = (float)alpha / 255.0f;

	for (i = 0; s[i]; i++) {
		char c = s[i];
		const U8 *rows;
		if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
		rows = osdFindGlyph(c);
		if (!rows) continue;
		for (gy = 0; gy < 7; gy++) {
			for (gx = 0; gx < 5; gx++) {
				int px = x + i * 6 + gx, py = y + gy;
				U8 *p;
				if (!(rows[gy] & (0x10 >> gx))) continue;
				if (px < 0 || px >= (int)w || py < 0 || py >= (int)h) continue;
				p = (U8 *)pixels + ((size_t)py * w + (size_t)px) * 4;
				p[0] = (U8)((float)p[0] * (1.0f - a) + (float)r * a);
				p[1] = (U8)((float)p[1] * (1.0f - a) + (float)g * a);
				p[2] = (U8)((float)p[2] * (1.0f - a) + (float)b * a);
				p[3] = 255;
			}
		}
	}
}

void
sysvid_overlayText(int x, int y, const char *s, U8 r, U8 g, U8 b)
{
	sysvid_drawTextAlpha(ovl_pixels, ovl_w, ovl_h, x, y, s, r, g, b, 255);
}

/* eof */

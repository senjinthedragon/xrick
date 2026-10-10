/*
 * xrick/include/gameselect_embedded.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Diskette label art for the game select screen (see xrick/src/gameselect.c),
 * embedded directly into the executable. Decoded at runtime with stb_image
 * (see thirdparty/stb_image.h).
 *
 * RD1's label needs no asset here at all: gameselect.c decodes pic_splash
 * (src/rd1/dat_picsST.c), the real RD1 title picture, live from its own source
 * data and palette (src/rd1/fb.c) -- no screenshot, no embedded PNG.
 *
 * rd2_label.png is a crop of a clean screenshot of RD2's own title picture
 * (rd2_1793a, src/rd2/rd2_flow.c): that picture is stored tree/Huffman-coded
 * (rd2_1795c) and unpacked into raw ST bitplane memory at runtime, which isn't
 * worth decoding from scratch for one thumbnail -- captured instead with
 * upscale/CRT/bezel all off, so it's still pixel-exact, unfiltered source art,
 * not shader output.
 */

#ifndef _GAMESELECT_EMBEDDED_H
#define _GAMESELECT_EMBEDDED_H

extern const unsigned char gameselect_rd2_label_png[];
extern const unsigned long gameselect_rd2_label_png_len;

#endif

/* eof */

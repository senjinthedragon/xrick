/*
 * xrick/src/bezels_embedded.c
 *
 * Embeds bezel PNG assets (see xrick/src/bezels/) into the executable.
 * Requires a C23 compiler for #embed; only this file needs that dialect.
 */

#include "bezels_embedded.h"

const unsigned char bezel_commodore_1084s_png[] = {
#embed "bezels/commodore_1084s.png"
};
const unsigned long bezel_commodore_1084s_png_len = sizeof(bezel_commodore_1084s_png);

const unsigned char bezel_atari_sc1224_png[] = {
#embed "bezels/atari_sc1224.png"
};
const unsigned long bezel_atari_sc1224_png_len = sizeof(bezel_atari_sc1224_png);

/* eof */

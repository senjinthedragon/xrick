/*
 * xrick/src/gameselect_embedded.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Embeds the game select screen's diskette art (see xrick/src/gameselect/)
 * into the executable. Requires a C23 compiler for #embed; only this file
 * needs that dialect.
 */

#include "gameselect_embedded.h"

const unsigned char gameselect_rd2_label_png[] = {
#embed "gameselect/rd2_label.png"
};
const unsigned long gameselect_rd2_label_png_len = sizeof(gameselect_rd2_label_png);

const unsigned char gameselect_disk_body_png[] = {
#embed "gameselect/disk_body.png"
};
const unsigned long gameselect_disk_body_png_len = sizeof(gameselect_disk_body_png);

/* eof */

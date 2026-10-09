/*
 * xrick/src/masks_embedded.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Embeds phosphor mask PNG assets (see xrick/src/masks/) into the
 * executable. Requires a C23 compiler for #embed; only this file needs
 * that dialect.
 */

#include "masks_embedded.h"

const unsigned char mask_phosphor_slot_mip0_png[] = {
#embed "masks/phosphor_mask_slot_mip0.png"
};
const unsigned long mask_phosphor_slot_mip0_png_len = sizeof(mask_phosphor_slot_mip0_png);
const unsigned char mask_phosphor_slot_mip1_png[] = {
#embed "masks/phosphor_mask_slot_mip1.png"
};
const unsigned long mask_phosphor_slot_mip1_png_len = sizeof(mask_phosphor_slot_mip1_png);
const unsigned char mask_phosphor_slot_mip2_png[] = {
#embed "masks/phosphor_mask_slot_mip2.png"
};
const unsigned long mask_phosphor_slot_mip2_png_len = sizeof(mask_phosphor_slot_mip2_png);
const unsigned char mask_phosphor_slot_mip3_png[] = {
#embed "masks/phosphor_mask_slot_mip3.png"
};
const unsigned long mask_phosphor_slot_mip3_png_len = sizeof(mask_phosphor_slot_mip3_png);
const unsigned char mask_phosphor_slot_mip4_png[] = {
#embed "masks/phosphor_mask_slot_mip4.png"
};
const unsigned long mask_phosphor_slot_mip4_png_len = sizeof(mask_phosphor_slot_mip4_png);

/* eof */

/*
 * xrick/include/masks_embedded.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Phosphor mask textures for crt-royale, embedded via #embed. See
 * xrick/src/masks/ for how these were generated: each mask's full mip
 * chain (24/12/6/3/1px) is pre-rendered offline from crt-royale's own
 * bundled 64px "Tileable" LUTs using wrap-padded Lanczos resizing (tile
 * 3x3, resize, crop the center tile), so every level -- not just the
 * base -- is genuinely seamless when hardware-tiled. GPU-side
 * SDL_GenerateMipmapsForGPUTexture clamps at tile edges instead of
 * wrapping, which silently reintroduced the exact seam this was meant to
 * fix at every level above 0; these are uploaded directly instead (see
 * loadMipmappedPNGTexture in sysvid.c).
 */

#ifndef _MASKS_EMBEDDED_H
#define _MASKS_EMBEDDED_H

#define MASK_MIP_LEVELS 5

extern const unsigned char mask_phosphor_slot_mip0_png[];
extern const unsigned long mask_phosphor_slot_mip0_png_len;
extern const unsigned char mask_phosphor_slot_mip1_png[];
extern const unsigned long mask_phosphor_slot_mip1_png_len;
extern const unsigned char mask_phosphor_slot_mip2_png[];
extern const unsigned long mask_phosphor_slot_mip2_png_len;
extern const unsigned char mask_phosphor_slot_mip3_png[];
extern const unsigned long mask_phosphor_slot_mip3_png_len;
extern const unsigned char mask_phosphor_slot_mip4_png[];
extern const unsigned long mask_phosphor_slot_mip4_png_len;

#endif

/* eof */

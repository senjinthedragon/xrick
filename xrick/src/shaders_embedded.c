/*
 * xrick/src/shaders_embedded.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Embeds compiled SPIR-V shaders (built by the Makefile via glslc) into
 * the executable. Requires a C23 compiler for #embed; only this file
 * needs that dialect.
 */

#include "shaders_embedded.h"

const unsigned char shader_passthrough_vert_spv[] = {
#embed "../../build/shaders/passthrough.vert.spv"
};
const unsigned long shader_passthrough_vert_spv_len = sizeof(shader_passthrough_vert_spv);

const unsigned char shader_passthrough_frag_spv[] = {
#embed "../../build/shaders/passthrough.frag.spv"
};
const unsigned long shader_passthrough_frag_spv_len = sizeof(shader_passthrough_frag_spv);

const unsigned char shader_sharp_bilinear_frag_spv[] = {
#embed "../../build/shaders/sharp-bilinear.frag.spv"
};
const unsigned long shader_sharp_bilinear_frag_spv_len = sizeof(shader_sharp_bilinear_frag_spv);

const unsigned char shader_crt_lottes_frag_spv[] = {
#embed "../../build/shaders/crt-lottes.frag.spv"
};
const unsigned long shader_crt_lottes_frag_spv_len = sizeof(shader_crt_lottes_frag_spv);

const unsigned char shader_crt_easymode_frag_spv[] = {
#embed "../../build/shaders/crt-easymode.frag.spv"
};
const unsigned long shader_crt_easymode_frag_spv_len = sizeof(shader_crt_easymode_frag_spv);

const unsigned char shader_fsr_easu_frag_spv[] = {
#embed "../../build/shaders/fsr-easu.frag.spv"
};
const unsigned long shader_fsr_easu_frag_spv_len = sizeof(shader_fsr_easu_frag_spv);

const unsigned char shader_fsr_rcas_frag_spv[] = {
#embed "../../build/shaders/fsr-rcas.frag.spv"
};
const unsigned long shader_fsr_rcas_frag_spv_len = sizeof(shader_fsr_rcas_frag_spv);

const unsigned char shader_crt_easymode_post_frag_spv[] = {
#embed "../../build/shaders/crt-easymode-post.frag.spv"
};
const unsigned long shader_crt_easymode_post_frag_spv_len = sizeof(shader_crt_easymode_post_frag_spv);

const unsigned char shader_crt_royale_linearize_frag_spv[] = {
#embed "../../build/shaders/crt-royale-linearize.frag.spv"
};
const unsigned long shader_crt_royale_linearize_frag_spv_len = sizeof(shader_crt_royale_linearize_frag_spv);

const unsigned char shader_crt_royale_vscan_frag_spv[] = {
#embed "../../build/shaders/crt-royale-vscan.frag.spv"
};
const unsigned long shader_crt_royale_vscan_frag_spv_len = sizeof(shader_crt_royale_vscan_frag_spv);

const unsigned char shader_crt_royale_bloom_approx_frag_spv[] = {
#embed "../../build/shaders/crt-royale-bloom-approx.frag.spv"
};
const unsigned long shader_crt_royale_bloom_approx_frag_spv_len = sizeof(shader_crt_royale_bloom_approx_frag_spv);

const unsigned char shader_crt_royale_hscan_mask_frag_spv[] = {
#embed "../../build/shaders/crt-royale-hscan-mask.frag.spv"
};
const unsigned long shader_crt_royale_hscan_mask_frag_spv_len = sizeof(shader_crt_royale_hscan_mask_frag_spv);

const unsigned char shader_crt_royale_brightpass_frag_spv[] = {
#embed "../../build/shaders/crt-royale-brightpass.frag.spv"
};
const unsigned long shader_crt_royale_brightpass_frag_spv_len = sizeof(shader_crt_royale_brightpass_frag_spv);

const unsigned char shader_crt_royale_bloom_blur_frag_spv[] = {
#embed "../../build/shaders/crt-royale-bloom-blur.frag.spv"
};
const unsigned long shader_crt_royale_bloom_blur_frag_spv_len = sizeof(shader_crt_royale_bloom_blur_frag_spv);

const unsigned char shader_crt_royale_reconstitute_frag_spv[] = {
#embed "../../build/shaders/crt-royale-reconstitute.frag.spv"
};
const unsigned long shader_crt_royale_reconstitute_frag_spv_len = sizeof(shader_crt_royale_reconstitute_frag_spv);

const unsigned char shader_crt_curvature_frag_spv[] = {
#embed "../../build/shaders/crt-curvature.frag.spv"
};
const unsigned long shader_crt_curvature_frag_spv_len = sizeof(shader_crt_curvature_frag_spv);

const unsigned char shader_crt_final_encode_frag_spv[] = {
#embed "../../build/shaders/crt-final-encode.frag.spv"
};
const unsigned long shader_crt_final_encode_frag_spv_len = sizeof(shader_crt_final_encode_frag_spv);

/* eof */

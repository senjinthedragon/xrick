/*
 * xrick/src/shaders_embedded_msl.c
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Embeds Metal Shading Language shaders, transpiled from the SPIR-V in
 * shaders_embedded.c by SDL_shadercross (built by the Makefile, macOS only --
 * see XRICK_MSL_SHADERS there). Requires a C23 compiler for #embed, same as
 * shaders_embedded.c.
 *
 * This whole file is a no-op unless XRICK_MSL_SHADERS is defined: the
 * preprocessor skips every #embed below rather than expanding it, so the
 * .msl files it names never need to exist on a build that doesn't set it.
 */

#ifdef XRICK_MSL_SHADERS

#include "shaders_embedded_msl.h"

const unsigned char shader_passthrough_vert_msl[] = {
#embed "../../build/shaders/passthrough.vert.msl"
};
const unsigned long shader_passthrough_vert_msl_len = sizeof(shader_passthrough_vert_msl);

const unsigned char shader_passthrough_frag_msl[] = {
#embed "../../build/shaders/passthrough.frag.msl"
};
const unsigned long shader_passthrough_frag_msl_len = sizeof(shader_passthrough_frag_msl);

const unsigned char shader_sharp_bilinear_frag_msl[] = {
#embed "../../build/shaders/sharp-bilinear.frag.msl"
};
const unsigned long shader_sharp_bilinear_frag_msl_len = sizeof(shader_sharp_bilinear_frag_msl);

const unsigned char shader_crt_lottes_frag_msl[] = {
#embed "../../build/shaders/crt-lottes.frag.msl"
};
const unsigned long shader_crt_lottes_frag_msl_len = sizeof(shader_crt_lottes_frag_msl);

const unsigned char shader_crt_easymode_frag_msl[] = {
#embed "../../build/shaders/crt-easymode.frag.msl"
};
const unsigned long shader_crt_easymode_frag_msl_len = sizeof(shader_crt_easymode_frag_msl);

const unsigned char shader_fsr_easu_frag_msl[] = {
#embed "../../build/shaders/fsr-easu.frag.msl"
};
const unsigned long shader_fsr_easu_frag_msl_len = sizeof(shader_fsr_easu_frag_msl);

const unsigned char shader_fsr_rcas_frag_msl[] = {
#embed "../../build/shaders/fsr-rcas.frag.msl"
};
const unsigned long shader_fsr_rcas_frag_msl_len = sizeof(shader_fsr_rcas_frag_msl);

const unsigned char shader_crt_easymode_post_frag_msl[] = {
#embed "../../build/shaders/crt-easymode-post.frag.msl"
};
const unsigned long shader_crt_easymode_post_frag_msl_len = sizeof(shader_crt_easymode_post_frag_msl);

const unsigned char shader_crt_royale_linearize_frag_msl[] = {
#embed "../../build/shaders/crt-royale-linearize.frag.msl"
};
const unsigned long shader_crt_royale_linearize_frag_msl_len = sizeof(shader_crt_royale_linearize_frag_msl);

const unsigned char shader_crt_royale_vscan_frag_msl[] = {
#embed "../../build/shaders/crt-royale-vscan.frag.msl"
};
const unsigned long shader_crt_royale_vscan_frag_msl_len = sizeof(shader_crt_royale_vscan_frag_msl);

const unsigned char shader_crt_royale_bloom_approx_frag_msl[] = {
#embed "../../build/shaders/crt-royale-bloom-approx.frag.msl"
};
const unsigned long shader_crt_royale_bloom_approx_frag_msl_len = sizeof(shader_crt_royale_bloom_approx_frag_msl);

const unsigned char shader_crt_royale_hscan_mask_frag_msl[] = {
#embed "../../build/shaders/crt-royale-hscan-mask.frag.msl"
};
const unsigned long shader_crt_royale_hscan_mask_frag_msl_len = sizeof(shader_crt_royale_hscan_mask_frag_msl);

const unsigned char shader_crt_royale_brightpass_frag_msl[] = {
#embed "../../build/shaders/crt-royale-brightpass.frag.msl"
};
const unsigned long shader_crt_royale_brightpass_frag_msl_len = sizeof(shader_crt_royale_brightpass_frag_msl);

const unsigned char shader_crt_royale_bloom_blur_frag_msl[] = {
#embed "../../build/shaders/crt-royale-bloom-blur.frag.msl"
};
const unsigned long shader_crt_royale_bloom_blur_frag_msl_len = sizeof(shader_crt_royale_bloom_blur_frag_msl);

const unsigned char shader_crt_royale_reconstitute_frag_msl[] = {
#embed "../../build/shaders/crt-royale-reconstitute.frag.msl"
};
const unsigned long shader_crt_royale_reconstitute_frag_msl_len = sizeof(shader_crt_royale_reconstitute_frag_msl);

const unsigned char shader_crt_curvature_frag_msl[] = {
#embed "../../build/shaders/crt-curvature.frag.msl"
};
const unsigned long shader_crt_curvature_frag_msl_len = sizeof(shader_crt_curvature_frag_msl);

const unsigned char shader_crt_final_encode_frag_msl[] = {
#embed "../../build/shaders/crt-final-encode.frag.msl"
};
const unsigned long shader_crt_final_encode_frag_msl_len = sizeof(shader_crt_final_encode_frag_msl);

#endif /* XRICK_MSL_SHADERS */

/* eof */

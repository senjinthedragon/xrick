/*
 * xrick/include/shaders_embedded.h
 *
 * Compiled SPIR-V shaders, built by glslc from the GLSL sources under
 * xrick/src/shaders/, embedded directly into the executable via #embed.
 */

#ifndef _SHADERS_EMBEDDED_H
#define _SHADERS_EMBEDDED_H

extern const unsigned char shader_passthrough_vert_spv[];
extern const unsigned long shader_passthrough_vert_spv_len;

extern const unsigned char shader_passthrough_frag_spv[];
extern const unsigned long shader_passthrough_frag_spv_len;

extern const unsigned char shader_sharp_bilinear_frag_spv[];
extern const unsigned long shader_sharp_bilinear_frag_spv_len;
extern const unsigned char shader_crt_lottes_frag_spv[];
extern const unsigned long shader_crt_lottes_frag_spv_len;
extern const unsigned char shader_crt_easymode_frag_spv[];
extern const unsigned long shader_crt_easymode_frag_spv_len;

extern const unsigned char shader_fsr_easu_frag_spv[];
extern const unsigned long shader_fsr_easu_frag_spv_len;

extern const unsigned char shader_fsr_rcas_frag_spv[];
extern const unsigned long shader_fsr_rcas_frag_spv_len;

extern const unsigned char shader_crt_easymode_post_frag_spv[];
extern const unsigned long shader_crt_easymode_post_frag_spv_len;

extern const unsigned char shader_crt_royale_linearize_frag_spv[];
extern const unsigned long shader_crt_royale_linearize_frag_spv_len;

extern const unsigned char shader_crt_royale_vscan_frag_spv[];
extern const unsigned long shader_crt_royale_vscan_frag_spv_len;

extern const unsigned char shader_crt_royale_bloom_approx_frag_spv[];
extern const unsigned long shader_crt_royale_bloom_approx_frag_spv_len;

extern const unsigned char shader_crt_royale_hscan_mask_frag_spv[];
extern const unsigned long shader_crt_royale_hscan_mask_frag_spv_len;

extern const unsigned char shader_crt_royale_brightpass_frag_spv[];
extern const unsigned long shader_crt_royale_brightpass_frag_spv_len;

extern const unsigned char shader_crt_royale_bloom_blur_frag_spv[];
extern const unsigned long shader_crt_royale_bloom_blur_frag_spv_len;

extern const unsigned char shader_crt_royale_reconstitute_frag_spv[];
extern const unsigned long shader_crt_royale_reconstitute_frag_spv_len;

extern const unsigned char shader_crt_curvature_frag_spv[];
extern const unsigned long shader_crt_curvature_frag_spv_len;

extern const unsigned char shader_crt_final_encode_frag_spv[];
extern const unsigned long shader_crt_final_encode_frag_spv_len;

#endif

/* eof */

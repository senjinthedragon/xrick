/*
 * xrick/include/shaders_embedded_msl.h
 *
 * Part of the SDL3 port of xrick, by Senjin the Dragon.
 *
 * Metal Shading Language versions of shaders_embedded.h's shaders, transpiled
 * from the same SPIR-V by SDL_shadercross and embedded via #embed. Only built
 * when XRICK_MSL_SHADERS is defined (macOS, see the Makefile) -- everywhere
 * else these resolve to NULL/0 so sysvid_gpu.c's shader-creation calls don't
 * need platform #ifdefs of their own.
 */

#ifndef _SHADERS_EMBEDDED_MSL_H
#define _SHADERS_EMBEDDED_MSL_H

#ifdef XRICK_MSL_SHADERS

extern const unsigned char shader_passthrough_vert_msl[];
extern const unsigned long shader_passthrough_vert_msl_len;

extern const unsigned char shader_passthrough_frag_msl[];
extern const unsigned long shader_passthrough_frag_msl_len;

extern const unsigned char shader_sharp_bilinear_frag_msl[];
extern const unsigned long shader_sharp_bilinear_frag_msl_len;

extern const unsigned char shader_crt_lottes_frag_msl[];
extern const unsigned long shader_crt_lottes_frag_msl_len;

extern const unsigned char shader_crt_easymode_frag_msl[];
extern const unsigned long shader_crt_easymode_frag_msl_len;

extern const unsigned char shader_fsr_easu_frag_msl[];
extern const unsigned long shader_fsr_easu_frag_msl_len;

extern const unsigned char shader_fsr_rcas_frag_msl[];
extern const unsigned long shader_fsr_rcas_frag_msl_len;

extern const unsigned char shader_crt_easymode_post_frag_msl[];
extern const unsigned long shader_crt_easymode_post_frag_msl_len;

extern const unsigned char shader_crt_royale_linearize_frag_msl[];
extern const unsigned long shader_crt_royale_linearize_frag_msl_len;

extern const unsigned char shader_crt_royale_vscan_frag_msl[];
extern const unsigned long shader_crt_royale_vscan_frag_msl_len;

extern const unsigned char shader_crt_royale_bloom_approx_frag_msl[];
extern const unsigned long shader_crt_royale_bloom_approx_frag_msl_len;

extern const unsigned char shader_crt_royale_hscan_mask_frag_msl[];
extern const unsigned long shader_crt_royale_hscan_mask_frag_msl_len;

extern const unsigned char shader_crt_royale_brightpass_frag_msl[];
extern const unsigned long shader_crt_royale_brightpass_frag_msl_len;

extern const unsigned char shader_crt_royale_bloom_blur_frag_msl[];
extern const unsigned long shader_crt_royale_bloom_blur_frag_msl_len;

extern const unsigned char shader_crt_royale_reconstitute_frag_msl[];
extern const unsigned long shader_crt_royale_reconstitute_frag_msl_len;

extern const unsigned char shader_crt_curvature_frag_msl[];
extern const unsigned long shader_crt_curvature_frag_msl_len;

extern const unsigned char shader_crt_final_encode_frag_msl[];
extern const unsigned long shader_crt_final_encode_frag_msl_len;

#else

#define shader_passthrough_vert_msl NULL
#define shader_passthrough_vert_msl_len 0UL
#define shader_passthrough_frag_msl NULL
#define shader_passthrough_frag_msl_len 0UL
#define shader_sharp_bilinear_frag_msl NULL
#define shader_sharp_bilinear_frag_msl_len 0UL
#define shader_crt_lottes_frag_msl NULL
#define shader_crt_lottes_frag_msl_len 0UL
#define shader_crt_easymode_frag_msl NULL
#define shader_crt_easymode_frag_msl_len 0UL
#define shader_fsr_easu_frag_msl NULL
#define shader_fsr_easu_frag_msl_len 0UL
#define shader_fsr_rcas_frag_msl NULL
#define shader_fsr_rcas_frag_msl_len 0UL
#define shader_crt_easymode_post_frag_msl NULL
#define shader_crt_easymode_post_frag_msl_len 0UL
#define shader_crt_royale_linearize_frag_msl NULL
#define shader_crt_royale_linearize_frag_msl_len 0UL
#define shader_crt_royale_vscan_frag_msl NULL
#define shader_crt_royale_vscan_frag_msl_len 0UL
#define shader_crt_royale_bloom_approx_frag_msl NULL
#define shader_crt_royale_bloom_approx_frag_msl_len 0UL
#define shader_crt_royale_hscan_mask_frag_msl NULL
#define shader_crt_royale_hscan_mask_frag_msl_len 0UL
#define shader_crt_royale_brightpass_frag_msl NULL
#define shader_crt_royale_brightpass_frag_msl_len 0UL
#define shader_crt_royale_bloom_blur_frag_msl NULL
#define shader_crt_royale_bloom_blur_frag_msl_len 0UL
#define shader_crt_royale_reconstitute_frag_msl NULL
#define shader_crt_royale_reconstitute_frag_msl_len 0UL
#define shader_crt_curvature_frag_msl NULL
#define shader_crt_curvature_frag_msl_len 0UL
#define shader_crt_final_encode_frag_msl NULL
#define shader_crt_final_encode_frag_msl_len 0UL

#endif

#endif

/* eof */

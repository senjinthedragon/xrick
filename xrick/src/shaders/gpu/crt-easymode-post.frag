#version 450

/*
 * Variant of crt-easymode.frag for use AFTER an upscale pass (e.g. FSR1's
 * EASU+RCAS) instead of directly on xrick's raw 320x200 framebuffer.
 *
 * The original crt-easymode.slang assumes it's sampling the low-res source
 * directly, so it does its own Lanczos-ish resample/sharpen as part of the
 * shader. That's wrong once something else has already upscaled the image
 * -- resampling an already-upscaled image through a filter tuned for pixel
 * art just re-blurs it. This variant drops that resample step and samples
 * its input directly (already at the final output resolution); the
 * scanline/mask/gamma stages are otherwise identical and still correct,
 * since scanline period is driven by the *original* source row count
 * (which doesn't change), not the upscaled resolution.
 *
 * Converted from slang by Senjin the Dragon.
 */

layout(set = 2, binding = 0) uniform sampler2D Source;

layout(set = 3, binding = 0) uniform UBO
{
	vec2 OutputSize;
} ubo;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

/* xrick's framebuffer is always 320x200 -- still needed for scanline period */
const vec2 SourceSize = vec2(320.0, 200.0);

/* crt-easymode.slang's #pragma parameter defaults (mask/scanline/gamma
 * portion only -- SHARPNESS_H/V and DILATION don't apply, there's no
 * resample step here) */
const float MASK_STRENGTH          = 0.3;
const float MASK_DOT_WIDTH         = 1.0;
const float MASK_DOT_HEIGHT        = 1.0;
const float MASK_STAGGER           = 0.0;
const float MASK_SIZE              = 1.0;
const float SCANLINE_STRENGTH      = 1.0;
const float SCANLINE_BEAM_WIDTH_MIN= 1.5;
const float SCANLINE_BEAM_WIDTH_MAX= 1.5;
const float SCANLINE_BRIGHT_MIN    = 0.35;
const float SCANLINE_BRIGHT_MAX    = 0.65;
const float SCANLINE_CUTOFF        = 400.0;
const float GAMMA_INPUT            = 2.0;
const float GAMMA_OUTPUT           = 1.8;
const float BRIGHT_BOOST           = 1.2;

#define PI 3.141592653589

void main()
{
	vec3 col = texture(Source, vTexCoord).rgb;
	col = pow(col, vec3(GAMMA_INPUT));

	float luma        = dot(vec3(0.2126, 0.7152, 0.0722), col);
	float bright      = (max(col.r, max(col.g, col.b)) + luma) * 0.5;
	float scan_bright = clamp(bright, SCANLINE_BRIGHT_MIN, SCANLINE_BRIGHT_MAX);
	float scan_beam   = clamp(bright * SCANLINE_BEAM_WIDTH_MAX, SCANLINE_BEAM_WIDTH_MIN, SCANLINE_BEAM_WIDTH_MAX);
	float scan_weight = 1.0 - pow(cos(vTexCoord.y * 2.0 * PI * SourceSize.y) * 0.5 + 0.5, scan_beam) * SCANLINE_STRENGTH;

	float mask   = 1.0 - MASK_STRENGTH;
	vec2 mod_fac = floor(vTexCoord * ubo.OutputSize.xy * SourceSize / (SourceSize * vec2(MASK_SIZE, MASK_DOT_HEIGHT * MASK_SIZE)));
	int dot_no   = int(mod((mod_fac.x + mod(mod_fac.y, 2.0) * MASK_STAGGER) / MASK_DOT_WIDTH, 3.0));
	vec3 mask_weight;

	if      (dot_no == 0) mask_weight = vec3(1.0,  mask, mask);
	else if (dot_no == 1) mask_weight = vec3(mask, 1.0,  mask);
	else                  mask_weight = vec3(mask, mask, 1.0);

	if (SourceSize.y >= SCANLINE_CUTOFF)
		scan_weight = 1.0;

	vec3 col2 = col;
	col *= vec3(scan_weight);
	col  = mix(col, col2, scan_bright);
	col *= mask_weight;
	col  = pow(col, vec3(1.0 / GAMMA_OUTPUT));

	FragColor = vec4(col * BRIGHT_BOOST, 1.0);
}

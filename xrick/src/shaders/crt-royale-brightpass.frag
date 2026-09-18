#version 450

/*
 * Pass 5/6 of crt-royale: isolate the part of each phosphor's light that's
 * "too bright" to render sharply and should bloom into its neighbors
 * instead, based on comparing the masked scanlines against the cheap
 * bloom-approx estimate. Ported from crt-royale-brightpass.slang. All the
 * per-frame "runtime bloom sigma" math is replaced by constants computed
 * once offline for our fixed 3.0px triad size (get_min_sigma_to_blur_triad
 * / get_center_weight in bloom-functions.h -- see the project memory for
 * the derivation), since xrick's mask tile size never changes at runtime.
 */

layout(set = 2, binding = 0) uniform sampler2D scanlinesTex; /* MASKED_SCANLINES */
layout(set = 2, binding = 1) uniform sampler2D bloomApproxTex;

layout(set = 3, binding = 0) uniform UBO
{
	float maskAmplify; /* 1 / (selected mask type's average color) -- varies per mask, see sysvid.c */
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float AUTO_DIM_FACTOR = 0.5;
const float UNDIM_FACTOR = 1.0 / AUTO_DIM_FACTOR;
const float CENTER_WEIGHT = 0.06532138187320563; /* precomputed for triad_size=3.0 */
const float BLOOM_UNDERESTIMATE_LEVELS = 0.8;

void main()
{
	vec3 intensityDim = texture(scanlinesTex, uv).rgb;
	vec3 intensity = intensityDim * UNDIM_FACTOR * params.maskAmplify;

	vec3 blurApprox = texture(bloomApproxTex, uv).rgb;
	vec3 maxAreaContribApprox = max(vec3(0.0), blurApprox - CENTER_WEIGHT * intensity);
	vec3 areaContribUnderestimate = BLOOM_UNDERESTIMATE_LEVELS * maxAreaContribApprox;
	/* max() with a tiny epsilon avoids a 0/0 -> NaN for black background
	 * pixels (very common -- most of xrick's frame is black), which would
	 * otherwise survive `intensityDim * blurRatio` below (NaN * 0.0 is
	 * NaN, not 0.0) and then spread via the bloom blur's neighbor sum. */
	vec3 intensityUnderestimate = max(BLOOM_UNDERESTIMATE_LEVELS * intensity, vec3(1.0 / 65536.0));

	vec3 blurRatio = ((vec3(1.0) - areaContribUnderestimate) / intensityUnderestimate - vec3(1.0))
		/ (CENTER_WEIGHT - 1.0);
	blurRatio = clamp(blurRatio, 0.0, 1.0);

	outColor = vec4(intensityDim * blurRatio, 1.0);
}

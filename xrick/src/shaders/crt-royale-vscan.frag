#version 450

/*
 * Pass 2/6 of crt-royale: resample the linearized frame vertically to the
 * output's full height, simulating a CRT electron beam -- each output row
 * is lit by 3 nearby source scanlines through a color-dependent Gaussian
 * falloff (brighter colors -> wider beam), not a plain resize filter.
 * Ported from crt-royale-scanlines-vertical-interlacing.slang, with two
 * simplifications justified by the real default settings:
 *   - beam_generalized_gaussian is set to its documented "false" (plain
 *     Gaussian, not generalized) option, avoiding a full incomplete-gamma-
 *     function implementation for a subtle shape difference.
 *   - RGB misconvergence offsets, though beam_misconvergence=true, default
 *     to (0,0,0,0,0,0) in the real RetroArch runtime-parameter defaults
 *     (the nonzero (0.1,0.2)-style values only exist in the static
 *     HARDCODE_SETTINGS codepath, which crt-royale.slangp doesn't use) --
 *     so all three color channels sample identically here.
 */

layout(set = 3, binding = 0) uniform UBO
{
	vec2 outputSize; /* fb_width x this pass's output height (viewport height) */
} params;

layout(set = 2, binding = 0) uniform sampler2D srcTex; /* LINEARIZED, 320x200 */

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const vec2 SRC_SIZE = vec2(320.0, 200.0);
const float BEAM_MIN_SIGMA = 0.02;
const float BEAM_MAX_SIGMA = 0.3;
const float BEAM_SIGMA_RANGE = BEAM_MAX_SIGMA - BEAM_MIN_SIGMA;
const float BEAM_SPOT_POWER = 1.0 / 3.0;
const float LEVELS_AUTODIM = 0.5;
const float PI = 3.14159265359;
const float UNDER_HALF = 0.4995;

vec3 gaussianSigma(vec3 color)
{
	return vec3(BEAM_MIN_SIGMA) + BEAM_SIGMA_RANGE * pow(color, vec3(BEAM_SPOT_POWER));
}

/* scanline_gaussian_sampled_contrib: beam_antialias_level=1 (3x supersample) */
vec3 scanlineContrib(float dist, vec3 color, float pixelHeight)
{
	vec3 sigma = gaussianSigma(color);
	vec3 sigmaInv = 1.0 / sigma;
	vec3 innerDenomInv = 0.5 * sigmaInv * sigmaInv;
	vec3 outerDenomInv = sigmaInv / sqrt(2.0 * PI);
	float sampleOffset = pixelHeight / 3.0;
	float d2 = dist + sampleOffset;
	float d3 = abs(dist - sampleOffset);
	vec3 scale = color / 3.0 * outerDenomInv;
	vec3 w1 = exp(-(dist * dist) * innerDenomInv);
	vec3 w2 = exp(-(d2 * d2) * innerDenomInv);
	vec3 w3 = exp(-(d3 * d3) * innerDenomInv);
	return scale * (w1 + w2 + w3);
}

void main()
{
	float outputHeight = params.outputSize.y;
	float pixelHeight = SRC_SIZE.y / outputHeight; /* progressive: il_step_multiple.y == 1 */

	vec2 currTexel = uv * SRC_SIZE;
	vec2 prevTexel = floor(currTexel - vec2(UNDER_HALF)) + vec2(0.5);
	vec2 scanlineUv = prevTexel / SRC_SIZE;
	float dist = currTexel.y - prevTexel.y; /* in [0, 1) */

	vec2 vStep = vec2(0.0, 1.0 / SRC_SIZE.y);
	vec3 scanline2 = texture(srcTex, scanlineUv).rgb;
	vec3 scanline3 = texture(srcTex, scanlineUv + vStep).rgb;

	/* beam_num_scanlines == 3: two nearest lines, plus one more chosen by
	 * which side of the pair the sample falls closer to. */
	float distRound = (dist >= 0.5) ? 1.0 : 0.0;
	vec2 outsideOff = mix(-vStep, 2.0 * vStep, distRound);
	vec3 scanlineOutside = texture(srcTex, scanlineUv + outsideOff).rgb;

	vec3 c2 = scanlineContrib(dist, scanline2, pixelHeight);
	vec3 c3 = scanlineContrib(abs(1.0 - dist), scanline3, pixelHeight);
	float distOutside = mix(dist + 1.0, 2.0 - dist, distRound);
	vec3 cOutside = scanlineContrib(distOutside, scanlineOutside, pixelHeight);

	vec3 intensity = c2 + c3 + cOutside;
	outColor = vec4(intensity * LEVELS_AUTODIM, 1.0);
}

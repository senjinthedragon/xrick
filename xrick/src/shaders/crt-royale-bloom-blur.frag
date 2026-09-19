#version 450

/*
 * Pass 6a/6 of crt-royale (one of two, run twice -- vertical then
 * horizontal, via bloomHorizontal): a separable 17-tap Gaussian blur of
 * the brightpass, sigma precomputed offline for our fixed 3.0px triad
 * size (matches crt-royale-bloom-vertical.slang / bloom-horizontal-
 * reconstitute.slang's shared tex2DblurNfast(), which resolves to the
 * fixed 17-tap "blur17fast" kernel at this sigma with static parameters).
 * Ported as a plain discrete convolution rather than the original's
 * bilinear-pair sample-halving trick (blur-functions.h) -- same result,
 * more texture fetches; fine for our resolutions.
 *
 * Converted from slang by Senjin the Dragon.
 */

layout(set = 3, binding = 0) uniform UBO
{
	vec2 texelStep; /* (1/width, 0) for horizontal, (0, 1/height) for vertical */
} params;

layout(set = 2, binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

/* normalized 17-tap Gaussian weights for sigma=1.5609262985058092 (w0 center, w1..w8 symmetric) */
const float W0 = 0.2555804802272772;
const float W[8] = float[8](
	0.2081635753664239, 0.11246979640701465, 0.040310779186554895, 0.009584304220574675,
	0.0015116599309492884, 0.00015816184635019306, 1.0977500471997498e-05, 5.054280217849124e-07
);

void main()
{
	vec3 sum = texture(srcTex, uv).rgb * W0;
	for (int i = 0; i < 8; i++) {
		float k = float(i + 1);
		sum += texture(srcTex, uv + params.texelStep * k).rgb * W[i];
		sum += texture(srcTex, uv - params.texelStep * k).rgb * W[i];
	}
	outColor = vec4(sum, 1.0);
}

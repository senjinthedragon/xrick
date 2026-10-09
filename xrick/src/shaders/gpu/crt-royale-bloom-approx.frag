#version 450

/*
 * Pass 3/6 of crt-royale: a cheap, small, blurred copy of the linearized
 * frame (fixed absolute 320x240, per crt-royale.slangp's own scale_x2/
 * scale_y2) used later to estimate how much energy should bloom into
 * neighboring pixels. Ported from crt-royale-bloom-approx.slang, using its
 * "bilinear resize" codepath (bloom_approx_filter = 0) rather than the
 * default 4x4 true Gaussian resize: 320x200 -> 320x240 is nearly 1:1
 * scale, and the shader's own comments note bilinear approximates a 4x4
 * Gaussian resize "MUCH better...for the very small sigmas we're likely
 * to use at small output resolutions" like this one. Hardware bilinear
 * filtering (the sampler bound here) does the resize directly.
 *
 * Converted from slang by Senjin the Dragon.
 */

layout(set = 2, binding = 0) uniform sampler2D srcTex; /* LINEARIZED, 320x200 */

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = vec4(texture(srcTex, uv).rgb, 1.0);
}

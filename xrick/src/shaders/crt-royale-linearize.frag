#version 450

/*
 * Pass 1/6 of crt-royale: decode the game's output from "CRT gamma" into
 * linear light, so every later pass (scanline beam, bloom) works in
 * physically meaningful units. Ported from crt-royale's
 * crt-royale-first-pass-linearize-crt-gamma-bob-fields.slang -- the
 * "bob interlaced fields" half of that pass is dropped: xrick's source is
 * always a progressive 320x200 frame, and the original shader's own
 * interlace-bob math degenerates to a no-op for non-interlaced sources
 * (its is_interlaced() check requires >288 lines).
 */

layout(set = 2, binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float CRT_GAMMA = 2.5;

void main()
{
	vec3 c = texture(srcTex, uv).rgb;
	outColor = vec4(pow(c, vec3(CRT_GAMMA)), 1.0);
}

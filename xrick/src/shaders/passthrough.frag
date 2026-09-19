#version 450

/* Written by Senjin the Dragon for the SDL3 port of xrick. */

layout(set = 2, binding = 0) uniform sampler2D srcTex;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

void main()
{
	outColor = texture(srcTex, uv);
}

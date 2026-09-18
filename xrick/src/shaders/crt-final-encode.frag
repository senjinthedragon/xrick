#version 450

/*
 * The no-bezel "final blit": copies gpuFinalIntermediate to the
 * swapchain, exactly like passthrough.frag, except it also knows how to
 * finish crt-royale's deliberately-deferred clamp/gamma-encode (see
 * crt-royale-reconstitute.frag and crt-curvature.frag's applyGamma --
 * this is that same finishing step for when there's no bezel/curvature
 * pass to do it instead). Every other mode already writes clamped,
 * display-ready color into gpuFinalIntermediate, so this is a no-op copy
 * for them.
 */

layout(set = 2, binding = 0) uniform sampler2D srcTex;

layout(set = 3, binding = 0) uniform UBO
{
	float applyGamma;
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float LCD_GAMMA = 2.2;

void main()
{
	vec3 c = texture(srcTex, uv).rgb;
	if (params.applyGamma != 0.0)
		c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / LCD_GAMMA));
	outColor = vec4(c, 1.0);
}

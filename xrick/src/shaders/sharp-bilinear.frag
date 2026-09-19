#version 450

/*
 * Ported from sharp-bilinear.slang (RetroArch's libretro/slang-shaders repo,
 * pixel-art-scaling/shaders/sharp-bilinear.slang), by Themaister, public
 * domain.
 *
 * Does a bilinear stretch with a pre-applied integer nearest-neighbor scale,
 * giving a much sharper result than plain bilinear. The original picks one
 * scale from the vertical ratio; here each axis gets its own, since with
 * 4:3 aspect correction the horizontal and vertical ratios differ. Needs a
 * linear-filtering sampler bound to Source.
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

void main()
{
	vec2 sourceSize = vec2(textureSize(Source, 0));
	vec2 texel = vTexCoord * sourceSize;
	vec2 texel_floored = floor(texel);
	vec2 s = fract(texel);
	vec2 scale = max(floor(ubo.OutputSize / sourceSize + 0.01), vec2(1.0));
	vec2 region_range = 0.5 - 0.5 / scale;

	/* figure out where in the texel to sample to get correct pre-scaled
	 * bilinear, using the hardware interpolator instead of 4 manual samples */
	vec2 center_dist = s - 0.5;
	vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * scale + 0.5;

	vec2 mod_texel = texel_floored + f;

	FragColor = vec4(texture(Source, mod_texel / sourceSize).rgb, 1.0);
}

#version 450

/*
 * Ported from crt-easymode.slang (RetroArch's libretro/slang-shaders repo,
 * crt/shaders/crt-easymode.slang), by EasyMode, GPL.
 *
 * Original is parameterized via RetroArch's #pragma parameter system; here
 * the defaults are hardcoded as constants (this build has no in-game
 * shader-parameter UI). SourceSize is xrick's framebuffer, which never
 * changes; OutputSize (the actual render target size, used for scanline
 * period and mask alignment) does change with window size/zoom, so it's
 * pushed in per-frame via a uniform buffer.
 */

layout(set = 2, binding = 0) uniform sampler2D Source;

layout(set = 3, binding = 0) uniform UBO
{
	vec2 OutputSize;
} ubo;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

/* xrick's framebuffer is always 320x200 */
const vec2 SourceSize   = vec2(320.0, 200.0);
const vec2 SourceSizeInv= vec2(1.0 / 320.0, 1.0 / 200.0);

/* crt-easymode.slang's #pragma parameter defaults */
const float SHARPNESS_H            = 0.5;
const float SHARPNESS_V            = 1.0;
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
const float DILATION               = 1.0;

#define FIX(c) max(abs(c), 1e-5)
#define PI 3.141592653589
#define TEX2D(c) dilate(texture(Source, c))
#define ENABLE_LANCZOS 1

vec4 dilate(vec4 col)
{
	vec4 x = mix(vec4(1.0), col, DILATION);
	return col * x;
}

float curve_distance(float x, float sharp)
{
	float x_step = step(0.5, x);
	float curve = 0.5 - sqrt(0.25 - (x - x_step) * (x - x_step)) * sign(0.5 - x);
	return mix(x, curve, sharp);
}

mat4x4 get_color_matrix(vec2 co, vec2 dx)
{
	return mat4x4(TEX2D(co - dx), TEX2D(co), TEX2D(co + dx), TEX2D(co + 2.0 * dx));
}

vec3 filter_lanczos(vec4 coeffs, mat4x4 color_matrix)
{
	vec4 col        = color_matrix * coeffs;
	vec4 sample_min = min(color_matrix[1], color_matrix[2]);
	vec4 sample_max = max(color_matrix[1], color_matrix[2]);

	col = clamp(col, sample_min, sample_max);

	return col.rgb;
}

void main()
{
	vec2 dx     = vec2(SourceSizeInv.x, 0.0);
	vec2 dy     = vec2(0.0, SourceSizeInv.y);
	vec2 pix_co = vTexCoord * SourceSize - vec2(0.5, 0.5);
	vec2 tex_co = (floor(pix_co) + vec2(0.5, 0.5)) * SourceSizeInv;
	vec2 dist   = fract(pix_co);
	float curve_x;
	vec3 col, col2;

#if ENABLE_LANCZOS
	curve_x = curve_distance(dist.x, SHARPNESS_H * SHARPNESS_H);

	vec4 coeffs = PI * vec4(1.0 + curve_x, curve_x, 1.0 - curve_x, 2.0 - curve_x);

	coeffs = FIX(coeffs);
	coeffs = 2.0 * sin(coeffs) * sin(coeffs * 0.5) / (coeffs * coeffs);
	coeffs /= dot(coeffs, vec4(1.0));

	col  = filter_lanczos(coeffs, get_color_matrix(tex_co, dx));
	col2 = filter_lanczos(coeffs, get_color_matrix(tex_co + dy, dx));
#else
	curve_x = curve_distance(dist.x, SHARPNESS_H);

	col  = mix(TEX2D(tex_co).rgb,      TEX2D(tex_co + dx).rgb,      curve_x);
	col2 = mix(TEX2D(tex_co + dy).rgb, TEX2D(tex_co + dx + dy).rgb, curve_x);
#endif

	col = mix(col, col2, curve_distance(dist.y, SHARPNESS_V));
	col = pow(col, vec3(GAMMA_INPUT / (DILATION + 1.0)));

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

	col2 = col.rgb;
	col *= vec3(scan_weight);
	col  = mix(col, col2, scan_bright);
	col *= mask_weight;
	col  = pow(col, vec3(1.0 / GAMMA_OUTPUT));

	FragColor = vec4(col * BRIGHT_BOOST, 1.0);
}

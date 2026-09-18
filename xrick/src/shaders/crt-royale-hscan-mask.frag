#version 450

/*
 * Pass 4/6 of crt-royale: resample the vertically-beamed scanlines to the
 * output's full width (Quilez fast filter, crt-royale's default
 * beam_horiz_filter), then multiply by a tiled phosphor mask texture to
 * get the individual RGB triads. Ported from
 * crt-royale-scanlines-horizontal-apply-mask.slang. Simplifications:
 *   - halation_weight defaults to 0.0 in the real shader, so the halation
 *     blend this pass would otherwise do is skipped outright (multiplying
 *     by 0 and lerping toward it is a no-op regardless).
 *   - beam_horiz_linear_rgb_weight defaults to 1.0 (fully linear-space
 *     blending), which collapses get_interpolated_linear_color()'s
 *     gamma/linear mix down to a plain lerp -- so that's all this does.
 *   - The phosphor mask itself is a precomputed 24x24 tile (see
 *     bezels_embedded.h's sibling, phosphor_mask_24.png, generated once
 *     offline from crt-royale's real slot-mask LUT at the shader's own
 *     documented default triad size) sampled with hardware texture
 *     wrapping, replacing the original's two-pass runtime Lanczos-sinc
 *     mask resizer (crt-royale-mask-resize-{vertical,horizontal}.slang),
 *     which exists to support arbitrary/unknown viewport sizes across any
 *     RetroArch core -- xrick only ever has the one fixed source size.
 */

layout(set = 3, binding = 0) uniform UBO
{
	vec2 outputSize; /* full viewport size -- also this pass's output size */
} params;

layout(set = 2, binding = 0) uniform sampler2D srcTex;  /* VERTICAL_SCANLINES, 320 x outputSize.y */
layout(set = 2, binding = 1) uniform sampler2D maskTex; /* phosphor mask, tiled, 24x24 */

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float MASK_TILE_PX = 24.0;
const float UNDER_HALF = 0.4995;

void main()
{
	vec2 vscanSize = vec2(320.0, params.outputSize.y);
	vec2 texelInv = 1.0 / vscanSize;

	vec2 currTexel = uv * vscanSize;
	vec2 prevTexel = floor(currTexel - vec2(UNDER_HALF)) + vec2(0.5);
	float x = currTexel.x - prevTexel.x;

	/* Quilez smoothstep-family weight for the 2nd of 2 relevant taps */
	float w2 = x * x * x * (x * (x * 6.0 - 15.0) + 10.0);

	vec2 uv1 = vec2(prevTexel.x, currTexel.y) * texelInv;
	vec2 uv2 = uv1 + vec2(texelInv.x, 0.0);
	vec3 color1 = texture(srcTex, uv1).rgb;
	vec3 color2 = texture(srcTex, uv2).rgb;
	vec3 scanlineColor = mix(color1, color2, w2);

	/* forced small LOD bias, not texture()'s automatic derivative-based
	 * selection: the output/mask-tile pixel ratio is almost always very
	 * close to (but not exactly) 1:1, which is right where hardware LOD
	 * heuristics stay at mip 0 -- not minified enough to trigger a
	 * coarser level, but still enough of a mismatch for the tile's
	 * period to beat against the pixel grid as a slowly-varying moire
	 * (confirmed: switching to plain automatic texture() -- even with
	 * the mip chain below fixed to be genuinely seamless -- brought the
	 * moire straight back, so automatic LOD really does stay at level 0
	 * here in practice). A small fixed bias guarantees some prefiltering
	 * regardless of what the derivative heuristic would have picked.
	 * This only looks right because the mip chain it's biasing into is
	 * now hand-authored with wrap-aware (toroidal) resizing -- see
	 * masks_embedded.h/loadMipmappedPNGTexture -- rather than
	 * GPU-generated, which clamped at tile edges and reintroduced a
	 * seam at every level above 0 even after the base level was fixed. */
	vec2 maskUv = (uv * params.outputSize) / MASK_TILE_PX;
	vec3 maskSample = textureLod(maskTex, maskUv, 1.0).rgb;

	outColor = vec4(scanlineColor * maskSample, 1.0);
}

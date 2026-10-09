#version 450

/*
 * Pass 6b/6 (final) of crt-royale: recombine the blurred bloom with the
 * unblurred "dim pass" (everything the brightpass didn't already claim)
 * and undo the auto-dim/mask-dim compensation. Ported from
 * crt-royale-bloom-horizontal-reconstitute.slang, minus its
 * diffusion_weight blend toward a halation/diffusion blur -- that's a
 * 7.5%-weighted blend at crt-royale's own default, and we don't compute a
 * halation buffer at all (see crt-royale-hscan-mask.frag's note on why:
 * halation_weight defaults to 0.0, so it contributes nothing upstream of
 * this either).
 *
 * Deliberately NOT clamped or gamma-encoded here, unlike the rest of this
 * port's passes -- mask_amplify pushes bright (near-white) pixels well
 * above 1.0 before any clamping, and this pass's own gpuFinalIntermediate
 * output feeds into crt-curvature.frag's mip/anisotropic-filtered
 * averaging when a bezel is active. Clamping *before* that averaging
 * would silently throw away the true brightness of the peaks, so any
 * later blend against the phosphor mask's dark gaps comes out visibly
 * darker than correct -- worse wherever the curvature warp's local
 * minification is strongest (this was a real, confirmed bug: see project
 * memory). The clamp + gamma-encode now happen once, at the very end of
 * whichever pass actually reads this (crt-curvature.frag, or
 * crt-final-encode.frag when there's no bezel to warp against).
 *
 * Converted from slang by Senjin the Dragon.
 */

layout(set = 2, binding = 0) uniform sampler2D blurredBrightpassTex; /* fully blurred (v then h) BRIGHTPASS */
layout(set = 2, binding = 1) uniform sampler2D scanlinesTex;         /* MASKED_SCANLINES */
layout(set = 2, binding = 2) uniform sampler2D brightpassTex;        /* unblurred BRIGHTPASS */

layout(set = 3, binding = 0) uniform UBO
{
	float maskAmplify; /* 1 / (selected mask type's average color) -- varies per mask, see sysvid.c */
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float AUTO_DIM_FACTOR = 0.5;
const float UNDIM_FACTOR = 1.0 / AUTO_DIM_FACTOR;

void main()
{
	vec3 blurredBrightpass = texture(blurredBrightpassTex, uv).rgb;
	vec3 intensityDim = texture(scanlinesTex, uv).rgb;
	vec3 brightpass = texture(brightpassTex, uv).rgb;

	vec3 dimpass = intensityDim - brightpass;
	vec3 phosphorBloom = (dimpass + blurredBrightpass) * params.maskAmplify * UNDIM_FACTOR;

	outColor = vec4(phosphorBloom, 1.0);
}

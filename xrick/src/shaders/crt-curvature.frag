#version 450

/*
 * Screen curvature, applied only when a bezel is active (F12) -- a
 * physical CRT sits curved inside its bezel regardless of which upscale/
 * CRT filter is running, so this is deliberately decoupled from
 * crt-royale's own geometry pass rather than being part of it (the
 * real pass is ~2300 lines,
 * almost all of it a general-purpose antialiasing filter library and a
 * point-cloud eye-position optimizer we don't need for a fixed layout).
 *
 * What IS ported faithfully, from crt-royale's geometry-functions.h: the
 * actual ray-sphere intersection and sphere<->uv mapping math (Cg ->
 * GLSL, "sphere_xyz_to_uv"/"intersect_sphere"/border-dim-factor), using
 * crt-royale's own documented defaults (geom_radius=2.0,
 * geom_view_dist=2.0, border_size/darkness/compress). Dropped:
 * arbitrary tilt (geom_tilt_angle defaults to 0 anyway, so the
 * global-to-local rotation is just identity for us), the "alternate
 * sphere"/cylinder modes (mode 1, spherical, is the shader's own
 * default), and the point-cloud eye-position auto-centering refinement's
 * general iterative solver -- for our fixed, symmetric, untilted layout
 * there's only ever one answer, solved numerically offline instead (see
 * EYE_Z below).
 *
 * Antialiasing: a simple fixed 4-tap rotated-grid supersample around the
 * curved lookup, not the real pass's full configurable filter library --
 * enough to soften the curve's edges without porting ~1400 lines of
 * general-purpose AA code for a single hardcoded configuration.
 *
 * Converted from slang by Senjin the Dragon.
 */

layout(set = 2, binding = 0) uniform sampler2D srcTex;

layout(set = 3, binding = 0) uniform UBO
{
	float applyGamma; /* != 0: gamma-encode + clamp here (crt-royale's HDR-ish source); 0: srcTex is already display-ready */
} params;

layout(location = 0) in vec2 uv;
layout(location = 0) out vec4 outColor;

const float LCD_GAMMA = 2.2;

const float GEOM_RADIUS = 2.0;
const float GEOM_VIEW_DIST = 2.0;
/* the Commodore 1084S bezel's actual measured screen-cutout aspect ratio
 * (1112x866 px at the bezel PNG's own scale -- see bezels[] in sysvid.c),
 * not an assumed 4:3 -- normalize(vec2(1.284065, 1.0)) */
const vec2 GEOM_ASPECT = vec2(0.7889701653735128, 0.6144315081036226);
/* crt-royale's default is 0.015, which fades the outermost ~5 of the 200
 * rows to black and hides the status bar along the top; the bezel already
 * frames the picture, so no edge dimming */
const float BORDER_SIZE = 0.0;
const float BORDER_DARKNESS = 2.0;
const float BORDER_COMPRESS = 2.5;

/* eye_pos.z, solved numerically offline (crt-royale's own point-cloud
 * solver isn't needed for a fixed, symmetric, untilted layout). It's the
 * value that maps the middle of the flat screen's top and bottom edges
 * exactly onto the source image's top and bottom rows, so the status bar
 * along the top is never cropped. (Fitting the four corners exactly, as
 * crt-royale's own solver would, loses about 2.7 of the 200 rows at the
 * top and bottom centre instead; this costs only a few pixels of rounded
 * corner, like a real curved tube.) */
const float EYE_Z = 3.968595;

/* returns true and fills sphereUv on a valid intersection; false (sphereUv
 * undefined) if the view ray misses the sphere or grazes it edge-on. */
bool curveUv(vec2 flatUv, float eyeZ, out vec2 sphereUv)
{
	vec2 viewUv = (flatUv - 0.5) * GEOM_ASPECT;
	vec3 viewVec = vec3(viewUv.x, -viewUv.y, -GEOM_VIEW_DIST);
	vec3 eyePos = vec3(0.0, 0.0, eyeZ);

	/* intersect_sphere: quadratic formula, b_over_2 form */
	float a = dot(viewVec, viewVec);
	float bOver2 = dot(viewVec, eyePos);
	float c = dot(eyePos, eyePos) - GEOM_RADIUS * GEOM_RADIUS;
	float discriminant = bOver2 * bOver2 - a * c;
	if (discriminant <= 0.005) return false;
	float dist = c / (-bOver2 + sqrt(discriminant));

	vec3 pos = eyePos + viewVec * dist;

	/* sphere_xyz_to_uv: great-circle arc length from the image center */
	vec3 imageCenter = vec3(0.0, 0.0, GEOM_RADIUS);
	float cpLen = length(cross(pos, imageCenter));
	float dp = dot(pos, imageCenter);
	float angle = atan(cpLen, dp);
	float arcLen = angle * GEOM_RADIUS;
	vec2 squareUvUnit = normalize(vec2(pos.x, -pos.y));
	vec2 squareUv = arcLen * squareUvUnit;
	sphereUv = squareUv / GEOM_ASPECT + 0.5;
	return true;
}

float borderDimFactor(vec2 videoUv)
{
	vec2 edgeDists = min(videoUv, vec2(1.0) - videoUv) * GEOM_ASPECT;
	vec2 penetration = max(vec2(BORDER_SIZE) - edgeDists, vec2(0.0));
	float penetrationRatio = (BORDER_SIZE == 0.0) ? 0.0 : length(penetration) / BORDER_SIZE;
	float escapeRatio = max(1.0 - penetrationRatio, 0.0);
	return min(pow(escapeRatio, BORDER_DARKNESS) * max(1.0, BORDER_COMPRESS), 1.0);
}

vec3 sampleCurved(vec2 flatUv, float eyeZ)
{
	vec2 sUv;
	if (!curveUv(flatUv, eyeZ, sUv)) return vec3(0.0);
	if (sUv.x < 0.0 || sUv.x > 1.0 || sUv.y < 0.0 || sUv.y > 1.0) return vec3(0.0);
	return texture(srcTex, sUv).rgb * borderDimFactor(sUv);
}

void main()
{
	vec2 texel = 1.0 / vec2(textureSize(srcTex, 0));

	/* 4-tap rotated-grid supersample, +/- 1/4 texel */
	vec3 c = vec3(0.0);
	c += sampleCurved(uv + texel * vec2(-0.25, -0.25), EYE_Z);
	c += sampleCurved(uv + texel * vec2(0.25, -0.25), EYE_Z);
	c += sampleCurved(uv + texel * vec2(-0.25, 0.25), EYE_Z);
	c += sampleCurved(uv + texel * vec2(0.25, 0.25), EYE_Z);
	c *= 0.25;

	/* clamp + gamma-encode LAST, after averaging -- see
	 * crt-royale-reconstitute.frag's comment for why this can't happen
	 * any earlier for crt-royale's HDR-ish output specifically. */
	if (params.applyGamma != 0.0)
		c = pow(clamp(c, 0.0, 1.0), vec3(1.0 / LCD_GAMMA));

	outColor = vec4(c, 1.0);
}

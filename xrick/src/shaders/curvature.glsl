#if defined(VERTEX)

#if __VERSION__ >= 130
#define COMPAT_VARYING out
#define COMPAT_ATTRIBUTE in
#define COMPAT_TEXTURE texture
#else
#define COMPAT_VARYING varying
#define COMPAT_ATTRIBUTE attribute
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

COMPAT_ATTRIBUTE vec4 VertexCoord;
COMPAT_ATTRIBUTE vec4 COLOR;
COMPAT_ATTRIBUTE vec4 TexCoord;
COMPAT_VARYING vec4 COL0;
COMPAT_VARYING vec4 TEX0;

uniform mat4 MVPMatrix;
uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;

void main()
{
    gl_Position = VertexCoord.x * MVPMatrix[0] + VertexCoord.y * MVPMatrix[1] + VertexCoord.z * MVPMatrix[2] + VertexCoord.w * MVPMatrix[3];
    TEX0.xy = TexCoord.xy;
}

#elif defined(FRAGMENT)

/*
 * Ported from the SDL_GPU version (src/shaders/gpu/crt-curvature.frag) -- see that file
 * for the full provenance and derivation notes (crt-royale's geometry-functions.h
 * ray-sphere intersection math, adapted for this project's fixed, untilted, symmetric
 * bezel layout). Algorithm unchanged; srcTex/uv become Texture/TEX0 (this driver's own
 * uniform/varying names). applyGamma is always 0 here (unlike the GPU backend, this
 * backend never has a deferred-gamma source reaching this pass -- CRT_ROYALE, the only
 * mode that needs it, isn't ported to this backend yet), so that branch and its uniform
 * are dropped entirely rather than wired to always read 0.
 *
 * Only reached when a bezel is active (see sysvid_gl_present) -- a physical CRT sits
 * curved inside its bezel regardless of which upscale/CRT filter produced the picture.
 */

#if __VERSION__ >= 130
#define COMPAT_VARYING in
#define COMPAT_TEXTURE texture
out vec4 FragColor;
#else
#define COMPAT_VARYING varying
#define FragColor gl_FragColor
#define COMPAT_TEXTURE texture2D
#endif

#ifdef GL_ES
#ifdef GL_FRAGMENT_PRECISION_HIGH
precision highp float;
#else
precision mediump float;
#endif
#define COMPAT_PRECISION mediump
#else
#define COMPAT_PRECISION
#endif

uniform COMPAT_PRECISION int FrameDirection;
uniform COMPAT_PRECISION int FrameCount;
uniform COMPAT_PRECISION vec2 OutputSize;
uniform COMPAT_PRECISION vec2 TextureSize;
uniform COMPAT_PRECISION vec2 InputSize;
uniform sampler2D Texture;
COMPAT_VARYING vec4 TEX0;

const float GEOM_RADIUS = 2.0;
const float GEOM_VIEW_DIST = 2.0;
/* the curve is computed for a 4:3 screen whatever shape the bezel opening or
 * the chosen aspect ratio gives the picture -- the warp is only ever a few
 * percent, so it isn't worth tracking exactly */
const vec2 GEOM_ASPECT = vec2(0.8, 0.6);
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
 * along the top is never cropped. */
const float EYE_Z = 3.970051;

/* returns true and fills sphereUv on a valid intersection; false (sphereUv
 * undefined) if the view ray misses the sphere or grazes it edge-on. */
bool curveUv(vec2 flatUv, float eyeZ, out vec2 sphereUv)
{
	vec2 viewUv = (flatUv - 0.5) * GEOM_ASPECT;
	vec3 viewVec = vec3(viewUv.x, -viewUv.y, -GEOM_VIEW_DIST);
	vec3 eyePos = vec3(0.0, 0.0, eyeZ);

	float a = dot(viewVec, viewVec);
	float bOver2 = dot(viewVec, eyePos);
	float c = dot(eyePos, eyePos) - GEOM_RADIUS * GEOM_RADIUS;
	float discriminant = bOver2 * bOver2 - a * c;
	if (discriminant <= 0.005) return false;
	float dist = c / (-bOver2 + sqrt(discriminant));

	vec3 pos = eyePos + viewVec * dist;

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
	return COMPAT_TEXTURE(Texture, sUv).rgb * borderDimFactor(sUv);
}

void main()
{
	vec2 texel = 1.0 / TextureSize;

	/* 4-tap rotated-grid supersample, +/- 1/4 texel */
	vec3 c = vec3(0.0);
	c += sampleCurved(TEX0.xy + texel * vec2(-0.25, -0.25), EYE_Z);
	c += sampleCurved(TEX0.xy + texel * vec2(0.25, -0.25), EYE_Z);
	c += sampleCurved(TEX0.xy + texel * vec2(-0.25, 0.25), EYE_Z);
	c += sampleCurved(TEX0.xy + texel * vec2(0.25, 0.25), EYE_Z);
	c *= 0.25;

	FragColor = vec4(c, 1.0);
}
#endif

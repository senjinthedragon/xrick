#version 450

/*
 * Ported from AMD FidelityFX Super Resolution 1.0 (FSR1), EASU pass (edge
 * adaptive spatial upsampling), via RetroArch's libretro/slang-shaders port
 * (edge-smoothing/fsr/shaders/fsr-pass0.slang + ffx_a.h + ffx_fsr1.h).
 * FSR1 itself is MIT-licensed (AMD FidelityFX SDK); the RetroArch port is
 * Unlicense. The 12-tap filter logic below is that port's, verbatim aside
 * from variable renames; only the small subset of AMD's cross-platform "A"
 * helper macros this pass actually uses are reproduced here, rather than
 * pulling in the full multi-backend ffx_a.h.
 */

layout(set = 2, binding = 0) uniform sampler2D Source;

layout(set = 3, binding = 0) uniform UBO {
	vec2 OutputSize;
} ubo;

layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;

/* xrick's framebuffer is always 320x200 */
const vec2 SourceSize = vec2(320.0, 200.0);

/* ---- minimal subset of AMD's ffx_a.h "A" helpers this pass needs ---- */
float AF1_x(float a) { return a; }
vec2  AF2_x(float a) { return vec2(a, a); }
vec3  AF3_x(float a) { return vec3(a, a, a); }
#define AF1_(a) AF1_x(float(a))
#define AF2_(a) AF2_x(float(a))
#define AF3_(a) AF3_x(float(a))
#define AF1_AU1(x) uintBitsToFloat(uint(x))
#define AU1_AF1(x) floatBitsToUint(float(x))
float APrxLoRcpF1(float a) { return AF1_AU1(0x7ef07ebbu - AU1_AF1(a)); }
float APrxLoRsqF1(float a) { return AF1_AU1(0x5f347d74u - (AU1_AF1(a) >> 1u)); }
float ASatF1(float x) { return clamp(x, 0.0, 1.0); }

void main()
{
	/* FsrEasuCon, simplified: this pass never dynamic-resolution-scales its
	 * input (inputViewport == inputSize == SourceSize), so only the two
	 * vec2s actually read further down (con0.xy/.zw) are computed */
	vec2 con0xy = SourceSize / ubo.OutputSize;
	vec2 con0zw = 0.5 * SourceSize / ubo.OutputSize - vec2(0.5);

	uvec2 gxy = uvec2(vTexCoord.xy * ubo.OutputSize.xy);

	vec2 pp = vec2(gxy) * con0xy + con0zw;
	vec2 fp = floor(pp);
	pp -= fp;

	/* 12-tap kernel layout relative to 'fp':
	 *    b c          (0,-1) (1,-1)
	 *  e f g h   (-1,0) (0,0) (1,0) (2,0)
	 *  i j k l   (-1,1) (0,1) (1,1) (2,1)
	 *    n o          (0, 2) (1, 2)
	 */
	ivec2 sp = ivec2(fp);
	vec3 b = texelFetch(Source, sp + ivec2( 0,-1), 0).rgb;
	vec3 c = texelFetch(Source, sp + ivec2( 1,-1), 0).rgb;
	vec3 e = texelFetch(Source, sp + ivec2(-1, 0), 0).rgb;
	vec3 f = texelFetch(Source, sp + ivec2( 0, 0), 0).rgb;
	vec3 g = texelFetch(Source, sp + ivec2( 1, 0), 0).rgb;
	vec3 h = texelFetch(Source, sp + ivec2( 2, 0), 0).rgb;
	vec3 i = texelFetch(Source, sp + ivec2(-1, 1), 0).rgb;
	vec3 j = texelFetch(Source, sp + ivec2( 0, 1), 0).rgb;
	vec3 k = texelFetch(Source, sp + ivec2( 1, 1), 0).rgb;
	vec3 l = texelFetch(Source, sp + ivec2( 2, 1), 0).rgb;
	vec3 n = texelFetch(Source, sp + ivec2( 0, 2), 0).rgb;
	vec3 o = texelFetch(Source, sp + ivec2( 1, 2), 0).rgb;

	/* approximate luma (luma times 2, in 2 FMA/MAD) */
	float bL = b.b * 0.5 + (b.r * 0.5 + b.g);
	float cL = c.b * 0.5 + (c.r * 0.5 + c.g);
	float eL = e.b * 0.5 + (e.r * 0.5 + e.g);
	float fL = f.b * 0.5 + (f.r * 0.5 + f.g);
	float gL = g.b * 0.5 + (g.r * 0.5 + g.g);
	float hL = h.b * 0.5 + (h.r * 0.5 + h.g);
	float iL = i.b * 0.5 + (i.r * 0.5 + i.g);
	float jL = j.b * 0.5 + (j.r * 0.5 + j.g);
	float kL = k.b * 0.5 + (k.r * 0.5 + k.g);
	float lL = l.b * 0.5 + (l.r * 0.5 + l.g);
	float nL = n.b * 0.5 + (n.r * 0.5 + n.g);
	float oL = o.b * 0.5 + (o.r * 0.5 + o.g);

	vec2 dir = vec2(0.0);
	float len = 0.0;

	/* quadrant s */
	{
		float w = (1.0 - pp.x) * (1.0 - pp.y);
		float dc = gL - fL; float cb = fL - eL;
		float lenX = max(abs(dc), abs(cb));
		lenX = APrxLoRcpF1(lenX);
		float dirX = gL - eL;
		dir.x += dirX * w;
		lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
		float ec = jL - fL; float ca = fL - bL;
		float lenY = max(abs(ec), abs(ca));
		lenY = APrxLoRcpF1(lenY);
		float dirY = jL - bL;
		dir.y += dirY * w;
		lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
	}
	/* quadrant t */
	{
		float w = pp.x * (1.0 - pp.y);
		float dc = hL - gL; float cb = gL - fL;
		float lenX = max(abs(dc), abs(cb));
		lenX = APrxLoRcpF1(lenX);
		float dirX = hL - fL;
		dir.x += dirX * w;
		lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
		float ec = kL - gL; float ca = gL - cL;
		float lenY = max(abs(ec), abs(ca));
		lenY = APrxLoRcpF1(lenY);
		float dirY = kL - cL;
		dir.y += dirY * w;
		lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
	}
	/* quadrant u */
	{
		float w = (1.0 - pp.x) * pp.y;
		float dc = kL - jL; float cb = jL - iL;
		float lenX = max(abs(dc), abs(cb));
		lenX = APrxLoRcpF1(lenX);
		float dirX = kL - iL;
		dir.x += dirX * w;
		lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
		float ec = nL - jL; float ca = jL - fL;
		float lenY = max(abs(ec), abs(ca));
		lenY = APrxLoRcpF1(lenY);
		float dirY = nL - fL;
		dir.y += dirY * w;
		lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
	}
	/* quadrant v */
	{
		float w = pp.x * pp.y;
		float dc = lL - kL; float cb = kL - jL;
		float lenX = max(abs(dc), abs(cb));
		lenX = APrxLoRcpF1(lenX);
		float dirX = lL - jL;
		dir.x += dirX * w;
		lenX = ASatF1(abs(dirX) * lenX); lenX *= lenX; len += lenX * w;
		float ec = oL - kL; float ca = kL - gL;
		float lenY = max(abs(ec), abs(ca));
		lenY = APrxLoRcpF1(lenY);
		float dirY = oL - gL;
		dir.y += dirY * w;
		lenY = ASatF1(abs(dirY) * lenY); lenY *= lenY; len += lenY * w;
	}

	vec2 dir2 = dir * dir;
	float dirR = dir2.x + dir2.y;
	bool zro = dirR < (1.0 / 32768.0);
	dirR = APrxLoRsqF1(dirR);
	dirR = zro ? 1.0 : dirR;
	dir.x = zro ? 1.0 : dir.x;
	dir *= vec2(dirR);

	len = len * 0.5;
	len *= len;
	float stretch = (dir.x * dir.x + dir.y * dir.y) * APrxLoRcpF1(max(abs(dir.x), abs(dir.y)));
	vec2 len2 = vec2(1.0 + (stretch - 1.0) * len, 1.0 + (-0.5) * len);
	float lob = 0.5 + ((1.0 / 4.0 - 0.04) - 0.5) * len;
	float clp = APrxLoRcpF1(lob);

	vec3 min4 = min(min(f, g), min(j, k));
	vec3 max4 = max(max(f, g), max(j, k));

	vec3 aC = vec3(0.0);
	float aW = 0.0;

#define FSR_EASU_TAP(OFF_X, OFF_Y, COLOR) { \
		vec2 v; \
		v.x = ((OFF_X) - pp.x) * dir.x + ((OFF_Y) - pp.y) * dir.y; \
		v.y = ((OFF_X) - pp.x) * (-dir.y) + ((OFF_Y) - pp.y) * dir.x; \
		v *= len2; \
		float d2 = min(v.x * v.x + v.y * v.y, clp); \
		float wB = (2.0 / 5.0) * d2 - 1.0; \
		float wA = lob * d2 - 1.0; \
		wB *= wB; wA *= wA; \
		wB = (25.0 / 16.0) * wB - (25.0 / 16.0 - 1.0); \
		float w = wB * wA; \
		aC += (COLOR) * w; aW += w; }

	FSR_EASU_TAP( 0.0, -1.0, b)
	FSR_EASU_TAP( 1.0, -1.0, c)
	FSR_EASU_TAP(-1.0,  1.0, i)
	FSR_EASU_TAP( 0.0,  1.0, j)
	FSR_EASU_TAP( 0.0,  0.0, f)
	FSR_EASU_TAP(-1.0,  0.0, e)
	FSR_EASU_TAP( 1.0,  1.0, k)
	FSR_EASU_TAP( 2.0,  1.0, l)
	FSR_EASU_TAP( 2.0,  0.0, h)
	FSR_EASU_TAP( 1.0,  0.0, g)
	FSR_EASU_TAP( 1.0,  2.0, o)
	FSR_EASU_TAP( 0.0,  2.0, n)

#undef FSR_EASU_TAP

	vec3 pix = min(max4, max(min4, aC * (1.0 / aW)));
	FragColor = vec4(pix, 1.0);
}

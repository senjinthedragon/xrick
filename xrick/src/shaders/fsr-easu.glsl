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
 * Ported from the SDL_GPU version (src/shaders/gpu/fsr-easu.frag) -- see that file for
 * the full provenance (AMD FidelityFX Super Resolution 1.0, via RetroArch's
 * libretro/slang-shaders port). Algorithm unchanged; Source/ubo.OutputSize become
 * Texture/OutputSize (this driver's own uniform names), and SourceSize (hardcoded
 * 320x200 there since it's reading sysvid_gpu.c's own fixed-size texture) becomes the
 * InputSize uniform, since this pass instead reads whatever pass came before it.
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

/* ---- minimal subset of AMD's ffx_a.h "A" helpers this pass needs ---- */
#define AF1_AU1(x) uintBitsToFloat(uint(x))
#define AU1_AF1(x) floatBitsToUint(float(x))
float APrxLoRcpF1(float a) { return AF1_AU1(0x7ef07ebbu - AU1_AF1(a)); }
float APrxLoRsqF1(float a) { return AF1_AU1(0x5f347d74u - (AU1_AF1(a) >> 1u)); }
float ASatF1(float x) { return clamp(x, 0.0, 1.0); }

void main()
{
	vec2 SourceSize = InputSize;

	/* FsrEasuCon, simplified: this pass never dynamic-resolution-scales its
	 * input (inputViewport == inputSize == SourceSize), so only the two
	 * vec2s actually read further down (con0.xy/.zw) are computed */
	vec2 con0xy = SourceSize / OutputSize;
	vec2 con0zw = 0.5 * SourceSize / OutputSize - vec2(0.5);

	uvec2 gxy = uvec2(TEX0.xy * OutputSize.xy);

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
	vec3 b = texelFetch(Texture, sp + ivec2( 0,-1), 0).rgb;
	vec3 c = texelFetch(Texture, sp + ivec2( 1,-1), 0).rgb;
	vec3 e = texelFetch(Texture, sp + ivec2(-1, 0), 0).rgb;
	vec3 f = texelFetch(Texture, sp + ivec2( 0, 0), 0).rgb;
	vec3 g = texelFetch(Texture, sp + ivec2( 1, 0), 0).rgb;
	vec3 h = texelFetch(Texture, sp + ivec2( 2, 0), 0).rgb;
	vec3 i = texelFetch(Texture, sp + ivec2(-1, 1), 0).rgb;
	vec3 j = texelFetch(Texture, sp + ivec2( 0, 1), 0).rgb;
	vec3 k = texelFetch(Texture, sp + ivec2( 1, 1), 0).rgb;
	vec3 l = texelFetch(Texture, sp + ivec2( 2, 1), 0).rgb;
	vec3 n = texelFetch(Texture, sp + ivec2( 0, 2), 0).rgb;
	vec3 o = texelFetch(Texture, sp + ivec2( 1, 2), 0).rgb;

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
#endif

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
 * Ported from the SDL_GPU version (src/shaders/gpu/fsr-rcas.frag) -- see that file for
 * the full provenance (AMD FidelityFX Super Resolution 1.0, via RetroArch's
 * libretro/slang-shaders port). Algorithm unchanged; Source/ubo.OutputSize/
 * ubo.FrameCount become Texture/OutputSize/FrameCount (this driver's own uniform
 * names). Reads the EASU pass's output, which is already at OutputSize, so (unlike
 * EASU) this pass needs no separate "source size" concept.
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

/* fsr.slangp defaults */
const float FSR_SHARPENING = 0.3;
const float FSR_FILMGRAIN  = 0.3;
const float FSR_GRAINCOLOR = 1.0; /* 1.0 = RGB noise, 0.0 = monochrome */
const float FSR_GRAINPDF   = 0.3;

#define AF1_AU1(x) uintBitsToFloat(uint(x))
#define AU1_AF1(x) floatBitsToUint(float(x))
float APrxMedRcpF1(float a) { float b = AF1_AU1(0x7ef19fffu - AU1_AF1(a)); return b * (-b * a + 2.0); }
float ASatF1(float x) { return clamp(x, 0.0, 1.0); }
float AMax3F1(float x, float y, float z) { return max(x, max(y, z)); }
float AMin3F1(float x, float y, float z) { return min(x, min(y, z)); }

#define FSR_RCAS_LIMIT (0.25 - (1.0 / 16.0))

vec3 FsrRcasLoadF(ivec2 p) { return texelFetch(Texture, p, 0).rgb; }

/* conX holds the linear sharpness scale from FsrRcasCon(sharpness) */
void FsrRcasF(out float pixR, out float pixG, out float pixB, ivec2 ip, float conX)
{
	/*    b
	 *  d e f
	 *    h
	 */
	ivec2 sp = ip;
	vec3 b = FsrRcasLoadF(sp + ivec2( 0, -1));
	vec3 d = FsrRcasLoadF(sp + ivec2(-1,  0));
	vec3 e = FsrRcasLoadF(sp);
	vec3 f = FsrRcasLoadF(sp + ivec2( 1,  0));
	vec3 h = FsrRcasLoadF(sp + ivec2( 0,  1));

	float bR=b.r, bG=b.g, bB=b.b;
	float dR=d.r, dG=d.g, dB=d.b;
	float eR=e.r, eG=e.g, eB=e.b;
	float fR=f.r, fG=f.g, fB=f.b;
	float hR=h.r, hG=h.g, hB=h.b;

	float bL = bB * 0.5 + (bR * 0.5 + bG);
	float dL = dB * 0.5 + (dR * 0.5 + dG);
	float eL = eB * 0.5 + (eR * 0.5 + eG);
	float fL = fB * 0.5 + (fR * 0.5 + fG);
	float hL = hB * 0.5 + (hR * 0.5 + hG);

	float nz = 0.25 * bL + 0.25 * dL + 0.25 * fL + 0.25 * hL - eL;
	nz = ASatF1(abs(nz) * APrxMedRcpF1(AMax3F1(AMax3F1(bL, dL, eL), fL, hL) - AMin3F1(AMin3F1(bL, dL, eL), fL, hL)));
	nz = -0.5 * nz + 1.0;

	float mn4R = min(AMin3F1(bR, dR, fR), hR);
	float mn4G = min(AMin3F1(bG, dG, fG), hG);
	float mn4B = min(AMin3F1(bB, dB, fB), hB);
	float mx4R = max(AMax3F1(bR, dR, fR), hR);
	float mx4G = max(AMax3F1(bG, dG, fG), hG);
	float mx4B = max(AMax3F1(bB, dB, fB), hB);

	vec2 peakC = vec2(1.0, -1.0 * 4.0);

	float hitMinR = min(mn4R, eR) / (4.0 * mx4R);
	float hitMinG = min(mn4G, eG) / (4.0 * mx4G);
	float hitMinB = min(mn4B, eB) / (4.0 * mx4B);
	float hitMaxR = (peakC.x - max(mx4R, eR)) / (4.0 * mn4R + peakC.y);
	float hitMaxG = (peakC.x - max(mx4G, eG)) / (4.0 * mn4G + peakC.y);
	float hitMaxB = (peakC.x - max(mx4B, eB)) / (4.0 * mn4B + peakC.y);
	float lobeR = max(-hitMinR, hitMaxR);
	float lobeG = max(-hitMinG, hitMaxG);
	float lobeB = max(-hitMinB, hitMaxB);
	float lobe = max(-FSR_RCAS_LIMIT, min(AMax3F1(lobeR, lobeG, lobeB), 0.0)) * conX;

	float rcpL = APrxMedRcpF1(4.0 * lobe + 1.0);
	pixR = (lobe*bR + lobe*dR + lobe*hR + lobe*fR + eR) * rcpL;
	pixG = (lobe*bG + lobe*dG + lobe*hG + lobe*fG + eG) * rcpL;
	pixB = (lobe*bB + lobe*dB + lobe*hB + lobe*fB + eB) * rcpL;
}

/* FSR - [LFGA] LINEAR FILM GRAIN APPLICATOR */
void FsrLfgaF(inout vec3 c, vec3 t, float a) { c += (t * a) * min(vec3(1.0) - c, c); }

float prng(vec2 uv, float time)
{
	return fract(sin(dot(uv + fract(time), vec2(12.9898, 78.233))) * 43758.5453);
}

float pdf(float noise, float shape)
{
	float orig = noise * 2.0 - 1.0;
	noise = pow(abs(orig), shape);
	noise *= sign(orig);
	noise -= sign(orig);
	return noise * 0.5;
}

void main()
{
	/* FsrRcasCon(con, FSR_SHARPENING): stops -> linear scale */
	float conX = exp2(-FSR_SHARPENING);

	ivec2 gxy = ivec2(TEX0.xy * OutputSize.xy);
	vec3 col = vec3(0.0);
	FsrRcasF(col.r, col.g, col.b, gxy, conX);

	if (FSR_FILMGRAIN > 0.0) {
		if (FSR_GRAINCOLOR == 0.0) {
			float noise = pdf(prng(TEX0.xy, float(FrameCount) * 0.11), FSR_GRAINPDF);
			FsrLfgaF(col, vec3(noise), FSR_FILMGRAIN);
		} else {
			vec3 rgbNoise = vec3(
				pdf(prng(TEX0.xy, float(FrameCount) * 0.11), FSR_GRAINPDF),
				pdf(prng(TEX0.xy, float(FrameCount) * 0.13), FSR_GRAINPDF),
				pdf(prng(TEX0.xy, float(FrameCount) * 0.17), FSR_GRAINPDF)
			);
			FsrLfgaF(col, rgbNoise, FSR_FILMGRAIN);
		}
	}

	FragColor = vec4(col, 1.0);
}
#endif

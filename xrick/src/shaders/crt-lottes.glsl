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
 * Ported from the SDL_GPU version (src/shaders/gpu/crt-lottes.frag) -- see that file
 * for the full provenance (crt-lottes.slang, RetroArch's libretro/slang-shaders repo,
 * "PUBLIC DOMAIN CRT STYLED SCAN-LINE SHADER" by Timothy Lottes). Algorithm unchanged
 * (screen-warp already left disabled there, same reasoning applies here: this backend
 * has no curvature/bezel warp pass yet either); Source/ubo.OutputSize become Texture/
 * OutputSize (this driver's own uniform names), and the hardcoded 320x200 SourceSize
 * becomes the InputSize uniform.
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

const float HARD_SCAN = -8.0;
const float HARD_PIX = -3.0;
const float MASK_DARK = 0.5;
const float MASK_LIGHT = 1.5;
const float SCALE_IN_LINEAR_GAMMA = 1.0;
const float SHADOW_MASK = 3.0;
const float BRIGHT_BOOST = 1.0;
const float HARD_BLOOM_SCAN = -2.0;
const float HARD_BLOOM_PIX = -1.5;
const float BLOOM_AMOUNT = 0.15;
const float SHAPE = 2.0;

// sRGB to linear (and back), skipped when SCALE_IN_LINEAR_GAMMA is 0.
float ToLinear1(float c)
{
	if (SCALE_IN_LINEAR_GAMMA == 0.0)
		return c;
	return (c <= 0.04045) ? c / 12.92 : pow((c + 0.055) / 1.055, 2.4);
}

vec3 ToLinear(vec3 c)
{
	return vec3(ToLinear1(c.r), ToLinear1(c.g), ToLinear1(c.b));
}

float ToSrgb1(float c)
{
	if (SCALE_IN_LINEAR_GAMMA == 0.0)
		return c;
	return (c < 0.0031308) ? c * 12.92 : 1.055 * pow(c, 0.41666) - 0.055;
}

vec3 ToSrgb(vec3 c)
{
	return vec3(ToSrgb1(c.r), ToSrgb1(c.g), ToSrgb1(c.b));
}

vec2 LottesSourceSize;

// Nearest emulated sample given floating point position and texel offset.
// Also zero's off screen.
vec3 Fetch(vec2 pos,vec2 off){
  pos=(floor(pos*LottesSourceSize+off)+vec2(0.5,0.5))/LottesSourceSize;
  return ToLinear(BRIGHT_BOOST * COMPAT_TEXTURE(Texture, pos.xy).rgb);
}

// Distance in emulated pixels to nearest texel.
vec2 Dist(vec2 pos)
{
    pos = pos*LottesSourceSize;

    return -((pos - floor(pos)) - vec2(0.5));
}

// 1D Gaussian.
float Gaus(float pos, float scale)
{
    return exp2(scale*pow(abs(pos), SHAPE));
}

// 3-tap Gaussian filter along horz line.
vec3 Horz3(vec2 pos, float off)
{
    vec3 b    = Fetch(pos, vec2(-1.0, off));
    vec3 c    = Fetch(pos, vec2( 0.0, off));
    vec3 d    = Fetch(pos, vec2( 1.0, off));
    float dst = Dist(pos).x;

    // Convert distance to weight.
    float scale = HARD_PIX;
    float wb = Gaus(dst-1.0,scale);
    float wc = Gaus(dst+0.0,scale);
    float wd = Gaus(dst+1.0,scale);

    // Return filtered sample.
    return (b*wb+c*wc+d*wd)/(wb+wc+wd);
}

// 5-tap Gaussian filter along horz line.
vec3 Horz5(vec2 pos,float off){
    vec3 a = Fetch(pos,vec2(-2.0, off));
    vec3 b = Fetch(pos,vec2(-1.0, off));
    vec3 c = Fetch(pos,vec2( 0.0, off));
    vec3 d = Fetch(pos,vec2( 1.0, off));
    vec3 e = Fetch(pos,vec2( 2.0, off));

    float dst = Dist(pos).x;
    // Convert distance to weight.
    float scale = HARD_PIX;
    float wa = Gaus(dst - 2.0, scale);
    float wb = Gaus(dst - 1.0, scale);
    float wc = Gaus(dst + 0.0, scale);
    float wd = Gaus(dst + 1.0, scale);
    float we = Gaus(dst + 2.0, scale);

    // Return filtered sample.
    return (a*wa+b*wb+c*wc+d*wd+e*we)/(wa+wb+wc+wd+we);
}

// 7-tap Gaussian filter along horz line.
vec3 Horz7(vec2 pos,float off)
{
    vec3 a = Fetch(pos, vec2(-3.0, off));
    vec3 b = Fetch(pos, vec2(-2.0, off));
    vec3 c = Fetch(pos, vec2(-1.0, off));
    vec3 d = Fetch(pos, vec2( 0.0, off));
    vec3 e = Fetch(pos, vec2( 1.0, off));
    vec3 f = Fetch(pos, vec2( 2.0, off));
    vec3 g = Fetch(pos, vec2( 3.0, off));

    float dst = Dist(pos).x;
    // Convert distance to weight.
    float scale = HARD_BLOOM_PIX;
    float wa = Gaus(dst - 3.0, scale);
    float wb = Gaus(dst - 2.0, scale);
    float wc = Gaus(dst - 1.0, scale);
    float wd = Gaus(dst + 0.0, scale);
    float we = Gaus(dst + 1.0, scale);
    float wf = Gaus(dst + 2.0, scale);
    float wg = Gaus(dst + 3.0, scale);

    // Return filtered sample.
    return (a*wa+b*wb+c*wc+d*wd+e*we+f*wf+g*wg)/(wa+wb+wc+wd+we+wf+wg);
}

// Return scanline weight.
float Scan(vec2 pos, float off)
{
    float dst = Dist(pos).y;

    return Gaus(dst + off, HARD_SCAN);
}

// Return scanline weight for bloom.
float BloomScan(vec2 pos, float off)
{
    float dst = Dist(pos).y;

    return Gaus(dst + off, HARD_BLOOM_SCAN);
}

// Allow nearest three lines to effect pixel.
vec3 Tri(vec2 pos)
{
    vec3 a = Horz3(pos,-1.0);
    vec3 b = Horz5(pos, 0.0);
    vec3 c = Horz3(pos, 1.0);

    float wa = Scan(pos,-1.0);
    float wb = Scan(pos, 0.0);
    float wc = Scan(pos, 1.0);

    return a*wa + b*wb + c*wc;
}

// Small bloom.
vec3 Bloom(vec2 pos)
{
    vec3 a = Horz5(pos,-2.0);
    vec3 b = Horz7(pos,-1.0);
    vec3 c = Horz7(pos, 0.0);
    vec3 d = Horz7(pos, 1.0);
    vec3 e = Horz5(pos, 2.0);

    float wa = BloomScan(pos,-2.0);
    float wb = BloomScan(pos,-1.0);
    float wc = BloomScan(pos, 0.0);
    float wd = BloomScan(pos, 1.0);
    float we = BloomScan(pos, 2.0);

    return a*wa+b*wb+c*wc+d*wd+e*we;
}

// Shadow mask.
vec3 Mask(vec2 pos)
{
    vec3 mask = vec3(MASK_DARK, MASK_DARK, MASK_DARK);

    // Very compressed TV style shadow mask.
    if (SHADOW_MASK == 1.0)
    {
        float line = MASK_LIGHT;
        float odd = 0.0;

        if (fract(pos.x*0.166666666) < 0.5) odd = 1.0;
        if (fract((pos.y + odd) * 0.5) < 0.5) line = MASK_DARK;

        pos.x = fract(pos.x*0.333333333);

        if      (pos.x < 0.333) mask.r = MASK_LIGHT;
        else if (pos.x < 0.666) mask.g = MASK_LIGHT;
        else                    mask.b = MASK_LIGHT;
        mask*=line;
    }

    // Aperture-grille.
    else if (SHADOW_MASK == 2.0)
    {
        pos.x = fract(pos.x*0.333333333);

        if      (pos.x < 0.333) mask.r = MASK_LIGHT;
        else if (pos.x < 0.666) mask.g = MASK_LIGHT;
        else                    mask.b = MASK_LIGHT;
    }

    // Stretched VGA style shadow mask (same as prior shaders).
    else if (SHADOW_MASK == 3.0)
    {
        pos.x += pos.y*3.0;
        pos.x  = fract(pos.x*0.166666666);

        if      (pos.x < 0.333) mask.r = MASK_LIGHT;
        else if (pos.x < 0.666) mask.g = MASK_LIGHT;
        else                    mask.b = MASK_LIGHT;
    }

    // VGA style shadow mask.
    else if (SHADOW_MASK == 4.0)
    {
        pos.xy  = floor(pos.xy*vec2(1.0, 0.5));
        pos.x  += pos.y*3.0;
        pos.x   = fract(pos.x*0.166666666);

        if      (pos.x < 0.333) mask.r = MASK_LIGHT;
        else if (pos.x < 0.666) mask.g = MASK_LIGHT;
        else                    mask.b = MASK_LIGHT;
    }

    return mask;
}

void main()
{
	LottesSourceSize = InputSize;

	vec2 pos = TEX0.xy;
	vec3 outColor = Tri(pos);

	// small bloom
	outColor += Bloom(pos) * BLOOM_AMOUNT;

	if (SHADOW_MASK > 0.0)
		outColor *= Mask(TEX0.xy * OutputSize * 1.000001);

	FragColor = vec4(ToSrgb(outColor), 1.0);
}
#endif

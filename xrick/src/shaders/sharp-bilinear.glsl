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
 * Ported from the SDL_GPU version (src/shaders/gpu/sharp-bilinear.frag), itself
 * ported from sharp-bilinear.slang (RetroArch's libretro/slang-shaders repo,
 * pixel-art-scaling/shaders/sharp-bilinear.slang), by Themaister, public domain.
 *
 * Does a bilinear stretch with a pre-applied integer nearest-neighbor scale,
 * giving a much sharper result than plain bilinear. The original picks one
 * scale from the vertical ratio; here each axis gets its own, since with 4:3
 * aspect correction the horizontal and vertical ratios differ. Needs a
 * linear-filtering sampler bound to Texture (this chain's filter_linear = 1).
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

void main()
{
    vec2 sourceSize = TextureSize;
    vec2 texel = TEX0.xy * sourceSize;
    vec2 texel_floored = floor(texel);
    vec2 s = fract(texel);
    vec2 scale = max(floor(OutputSize / sourceSize + 0.01), vec2(1.0));
    vec2 region_range = 0.5 - 0.5 / scale;

    vec2 center_dist = s - 0.5;
    vec2 f = (center_dist - clamp(center_dist, -region_range, region_range)) * scale + 0.5;

    vec2 mod_texel = texel_floored + f;

    FragColor = vec4(COMPAT_TEXTURE(Texture, mod_texel / sourceSize).rgb, 1.0);
}
#endif

/*
 * xrick/src/stb_image_impl.c
 *
 * Single translation unit that provides the stb_image implementation.
 * Only PNG decoding is needed (bezel assets), so the other format
 * decoders are compiled out to keep this small.
 */

#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_NO_STDIO

#include "thirdparty/stb_image.h"

/* eof */

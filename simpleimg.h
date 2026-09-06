/*
 *  Decoders for five simple bitmap formats: PCX, TGA, SGI/RGB, PNM
 *  (PBM/PGM/PPM) and XBM.
 *
 *  These are the only formats in this tree with no third-party decoder behind
 *  them. Each is a header plus either raw samples or a byte-oriented run-length
 *  scheme, so pulling in a library for them would cost more than it saves; the
 *  implementations live in simpleimg.c and are kept free of any GPAC or
 *  emscripten dependency so they can be compiled and tested natively.
 *
 *  Every decoder produces the same thing: a tightly packed 24-bit RGB buffer,
 *  top row first. Greyscale and bilevel sources are replicated across the three
 *  channels and alpha is dropped, because neither a GREYSCALE nor an RGBA pid
 *  has an adaptation path to writegen in this build.
 */

#ifndef _SIMPLEIMG_H_
#define _SIMPLEIMG_H_

#include <stddef.h>

typedef enum
{
	SIMPLEIMG_PCX = 0,
	SIMPLEIMG_TGA,
	SIMPLEIMG_SGI,
	SIMPLEIMG_PNM,
	SIMPLEIMG_XBM
} SimpleImgFormat;

/* On success returns 0, stores a malloc()'d RGB buffer in *out (released with
 * simpleimg_free) and the image size in *width and *height. */
int simpleimg_decode(SimpleImgFormat format, const unsigned char *data, size_t size,
                     unsigned char **out, unsigned int *width, unsigned int *height);

void simpleimg_free(unsigned char *buffer);

/* Maps a file extension (without the dot, any case) to a format. Returns 1 and
 * fills *format on success, 0 if the extension is not one of ours. */
int simpleimg_format_from_ext(const char *ext, SimpleImgFormat *format);

#define SIMPLEIMG_OK              0
#define SIMPLEIMG_ERR_MEMORY     -1
#define SIMPLEIMG_ERR_BITSTREAM  -2   /* truncated, or not the format claimed */
#define SIMPLEIMG_ERR_VARIANT    -3   /* a variant of the format not handled here */

#endif

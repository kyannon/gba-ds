/* A small DEFLATE/zlib decoder, for the fonts embedded compressed.
 *
 * src/fonts_data.c holds each font as a zlib stream (tools/embed_fonts.py),
 * which is about half the size; src/textfont.c decodes one the first time it
 * draws with it. zlib itself is only linked as mGBA's static library, whose
 * headers the three builds don't put on the include path, so the decoder is
 * here: no dependency, the same code on ROCKNIX, Android and wasm.
 *
 * Only what zlib's "compress2" makes for a whole buffer: any block type
 * (stored, fixed, dynamic), no preset dictionary, and the Adler-32 checked
 * at the end. Writes at most dstLen bytes and fails rather than overrun.
 */
#ifndef GBADS_INFLATE_H
#define GBADS_INFLATE_H

#include <stdbool.h>
#include <stddef.h>

/* Decodes the zlib stream in[0..inLen) into out, which must hold exactly
 * dstLen bytes (what the encoder compressed). False on a bad stream, a
 * wrong length or a bad Adler-32. */
bool zlib_inflate(const unsigned char* in, size_t inLen, unsigned char* out, size_t dstLen);

#endif

/* UI text in Jost (TrueType, via stb_truetype): measuring and rasterizing
 * UTF-8 strings, from Jost and, for what Jost lacks, fallback faces. ui.c turns
 * the results into textures. */
#ifndef GBADS_TEXTFONT_H
#define GBADS_TEXTFONT_H

#include <stdbool.h>
#include <stdint.h>

enum { TF_BOLD, TF_BODY, TF_COUNT }; /* Jost Bold Italic, Jost Medium Italic */

/* Faces to fall back on, after Jost, for characters Jost lacks (a script's font;
 * ui.c sets them when the language changes). A face is the TTF's bytes, which must
 * stay valid. At most three per style. */
void tf_fallback_clear(void);
bool tf_fallback_add(int font, const unsigned char* ttf);

/* Advance width in pixels of text whose capital letters are capPx tall. */
int tf_width(int font, int capPx, const char* text);

/* Rasterized text: 8-bit coverage, w * h. The baseline is `baseline` pixels
 * from the top and the text starts `left` pixels from the left edge (italic
 * and overhanging glyphs can reach a little either side of the advance). */
typedef struct {
	uint8_t* alpha;
	int w, h, baseline, left;
} TfBitmap;
bool tf_render(int font, int capPx, const char* text, TfBitmap* out);
void tf_free(TfBitmap* b);

#endif

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
/* The embedded script faces for a language's script (SCRIPT_*, i18n.h): its own first, the others after. */
void tf_fallback_for_script(int script);

/* Advance width in pixels of text whose capital letters are capPx tall. */
int tf_width(int font, int capPx, const char* text);

/* Whether any face in the style's chain has a glyph for a code point (false
 * means it would draw as a box). The tests use it on sample game names. */
bool tf_has_codepoint(int font, unsigned cp);

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

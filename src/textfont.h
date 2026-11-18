/* UI text in Jost (TrueType, via stb_truetype): measuring and rasterizing
 * UTF-8 strings. ui.c turns the results into textures. The Chinese interfaces
 * draw in Noto Sans SC and Noto Sans TC instead (ui.c picks the font). */
#ifndef GBADS_TEXTFONT_H
#define GBADS_TEXTFONT_H

#include <stdbool.h>
#include <stdint.h>

/* Jost Bold Italic, Jost Medium Italic, and the Chinese interfaces' Noto
 * Sans SC (Simplified) and Noto Sans TC (Traditional), bold and regular */
enum { TF_BOLD, TF_BODY, TF_ZH_BOLD, TF_ZH_BODY, TF_ZT_BOLD, TF_ZT_BODY, TF_COUNT };

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

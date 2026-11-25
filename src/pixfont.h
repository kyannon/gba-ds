/* GBA DS's pixel font: Jost's forms drawn one pixel thick, proportional, with
 * lower case, in two sizes. The glyphs are drawn in tools/make_pixfont.py
 * (which writes pixfont.c). Printable ASCII only; the quick bar uses it to draw
 * at the GBA's own resolution (game.c, cv_label / cv_message). */
#ifndef PIXFONT_H
#define PIXFONT_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
	uint8_t w;       /* 0: no such glyph */
	uint8_t row[9];  /* top to bottom; bit w - 1 - x is the pixel at x */
} PixGlyph;

typedef struct {
	int cap, xh, desc, height, space; /* rows: capitals, lower case without ascenders, below the baseline, all; a space's width */
	PixGlyph g[95];                   /* ' ' to '~' */
} PixFont;

extern const PixFont pixfont_small, pixfont_large;

/* whether the font has every character of an ASCII string (spaces always) */
static inline bool pixfont_has(const PixFont* f, const char* s) {
	for (; *s; ++s) {
		unsigned char c = (unsigned char) *s;
		if (c == ' ') continue;
		if (c < 33 || c > 126 || !f->g[c - 32].w) return false;
	}
	return true;
}
/* the width of a string in pixels (a pixel between letters) */
static inline int pixfont_width(const PixFont* f, const char* s) {
	int w = 0;
	for (; *s; ++s) {
		unsigned char c = (unsigned char) *s;
		w += (c == ' ' ? f->space : f->g[c - 32].w + 1);
	}
	return w > 0 && s[-1] != ' ' ? w - 1 : w;
}

#endif

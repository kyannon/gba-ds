#include "textfont.h"
#include "fonts_data.h"

#pragma GCC diagnostic ignored "-Wunused-function" /* stb_truetype has many we don't call */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#define STBTT_STATIC
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#pragma GCC diagnostic pop

#include <math.h>
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

static stbtt_fontinfo fonts[TF_COUNT];
static int capUnits[TF_COUNT]; /* height of "H" in font units */
static bool ready[TF_COUNT];
static pthread_once_t once = PTHREAD_ONCE_INIT;

/* the embedded font each of TF_* is */
static const int fontData[TF_COUNT] = {
	[TF_BOLD] = EMBEDDED_JOST_BOLD_ITALIC,
	[TF_BODY] = EMBEDDED_JOST_MEDIUM_ITALIC,
	[TF_ZH_BOLD] = EMBEDDED_NOTO_SANS_SC_BOLD,
	[TF_ZH_BODY] = EMBEDDED_NOTO_SANS_SC_REGULAR,
	[TF_ZT_BOLD] = EMBEDDED_NOTO_SANS_TC_BOLD,
	[TF_ZT_BODY] = EMBEDDED_NOTO_SANS_TC_REGULAR,
};

static void init(void) {
	for (int f = 0; f < TF_COUNT; ++f) {
		const EmbeddedFont* e = &embeddedFonts[fontData[f]];
		if (!stbtt_InitFont(&fonts[f], e->ttf, stbtt_GetFontOffsetForIndex(e->ttf, 0))) continue;
		int x0, y0, x1, y1;
		if (!stbtt_GetCodepointBox(&fonts[f], 'H', &x0, &y0, &x1, &y1) || y1 <= 0) continue;
		capUnits[f] = y1;
		ready[f] = true;
	}
}

/* Next code point from UTF-8 (invalid bytes come out as themselves). */
static int next_cp(const char** s) {
	const unsigned char* p = (const unsigned char*) *s;
	int c = *p++;
	int extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
	if (extra) {
		c &= 0x3F >> extra;
		for (int i = 0; i < extra && (*p & 0xC0) == 0x80; ++i) c = (c << 6) | (*p++ & 0x3F);
	}
	*s = (const char*) p;
	return c;
}

static float scale_for(int font, int capPx) {
	return (float) capPx / (float) capUnits[font];
}

int tf_width(int font, int capPx, const char* text) {
	pthread_once(&once, init);
	if (font < 0 || font >= TF_COUNT || !ready[font] || !text) return 0;
	stbtt_fontinfo* fi = &fonts[font];
	float sc = scale_for(font, capPx), x = 0;
	int prev = 0;
	for (const char* p = text; *p;) {
		int cp = next_cp(&p);
		int g = stbtt_FindGlyphIndex(fi, cp);
		if (prev) x += sc * stbtt_GetGlyphKernAdvance(fi, prev, g);
		int adv, lsb;
		stbtt_GetGlyphHMetrics(fi, g, &adv, &lsb);
		x += sc * adv;
		prev = g;
	}
	return (int) ceilf(x);
}

bool tf_render(int font, int capPx, const char* text, TfBitmap* out) {
	pthread_once(&once, init);
	memset(out, 0, sizeof(*out));
	if (font < 0 || font >= TF_COUNT || !ready[font] || !text || capPx <= 0) return false;
	stbtt_fontinfo* fi = &fonts[font];
	float sc = scale_for(font, capPx);
	int ascent, descent, gap;
	stbtt_GetFontVMetrics(fi, &ascent, &descent, &gap);
	int pad = capPx / 2 + 2; /* room for italic overhang either side */
	int base = (int) ceilf(ascent * sc) + 1;
	int h = base + (int) ceilf(-descent * sc) + 2;
	int w = tf_width(font, capPx, text) + pad * 2;
	if (w <= pad * 2) w = pad * 2 + 1;
	uint8_t* a = calloc((size_t) w * h, 1);
	if (!a) return false;
	float x = (float) pad;
	int prev = 0;
	for (const char* p = text; *p;) {
		int cp = next_cp(&p);
		int g = stbtt_FindGlyphIndex(fi, cp);
		if (prev) x += sc * stbtt_GetGlyphKernAdvance(fi, prev, g);
		int adv, lsb;
		stbtt_GetGlyphHMetrics(fi, g, &adv, &lsb);
		int ix0, iy0, ix1, iy1;
		float sx = x - floorf(x);
		stbtt_GetGlyphBitmapBoxSubpixel(fi, g, sc, sc, sx, 0, &ix0, &iy0, &ix1, &iy1);
		int gw = ix1 - ix0, gh = iy1 - iy0;
		if (gw > 0 && gh > 0) {
			uint8_t* gb = malloc((size_t) gw * gh);
			if (gb) {
				stbtt_MakeGlyphBitmapSubpixel(fi, gb, gw, gh, gw, sc, sc, sx, 0, g);
				int ox = (int) floorf(x) + ix0, oy = base + iy0;
				for (int yy = 0; yy < gh; ++yy) {
					int ty = oy + yy;
					if (ty < 0 || ty >= h) continue;
					for (int xx = 0; xx < gw; ++xx) {
						int tx = ox + xx;
						if (tx < 0 || tx >= w) continue;
						int v = a[ty * w + tx] + gb[yy * gw + xx]; /* glyphs may touch */
						a[ty * w + tx] = (uint8_t) (v > 255 ? 255 : v);
					}
				}
				free(gb);
			}
		}
		x += sc * adv;
		prev = g;
	}
	out->alpha = a;
	out->w = w;
	out->h = h;
	out->baseline = base;
	out->left = pad;
	return true;
}

void tf_free(TfBitmap* b) {
	free(b->alpha);
	b->alpha = NULL;
}

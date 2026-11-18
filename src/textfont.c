#include "textfont.h"
#include "fonts_data.h"
#include "inflate.h"

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
static unsigned char* heap[TF_COUNT]; /* a decoded font, kept for the session */
static pthread_mutex_t fontLock = PTHREAD_MUTEX_INITIALIZER;

/* the embedded font each of TF_* is */
static const int fontData[TF_COUNT] = {
	[TF_BOLD] = EMBEDDED_JOST_BOLD_ITALIC,
	[TF_BODY] = EMBEDDED_JOST_MEDIUM_ITALIC,
	[TF_ZH_BOLD] = EMBEDDED_NOTO_SANS_SC_BOLD,
	[TF_ZH_BODY] = EMBEDDED_NOTO_SANS_SC_REGULAR,
	[TF_ZT_BOLD] = EMBEDDED_NOTO_SANS_TC_BOLD,
	[TF_ZT_BODY] = EMBEDDED_NOTO_SANS_TC_REGULAR,
};

/* Decodes and parses a face the first time it is drawn with: English's two
 * are 200 KB together, and a Chinese one is only decoded if the language uses
 * it (the bold ones carry a whole character set, about 2 MB and 4.7 MB). */
static bool face(int f) {
	if (f < 0 || f >= TF_COUNT) return false;
	if (ready[f]) return true;
	pthread_mutex_lock(&fontLock);
	if (!ready[f]) {
		const EmbeddedFont* e = &embeddedFonts[fontData[f]];
		const unsigned char* ttf = e->ttf;
		if (e->raw) {
			unsigned char* buf = malloc(e->raw);
			if (buf && zlib_inflate(e->ttf, e->size, buf, e->raw)) {
				heap[f] = buf;
				ttf = buf;
			} else {
				free(buf);
			}
		}
		int x0, y0, x1, y1;
		if (ttf && stbtt_InitFont(&fonts[f], ttf, stbtt_GetFontOffsetForIndex(ttf, 0)) &&
		    stbtt_GetCodepointBox(&fonts[f], 'H', &x0, &y0, &x1, &y1) && y1 > 0) {
			capUnits[f] = y1;
			ready[f] = true;
		}
	}
	pthread_mutex_unlock(&fontLock);
	return ready[f];
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

bool tf_has_codepoint(int font, unsigned cp) {
	if (!face(font)) return false;
	return stbtt_FindGlyphIndex(&fonts[font], (int) cp) != 0;
}

int tf_width(int font, int capPx, const char* text) {
	if (!text || !face(font)) return 0;
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
	memset(out, 0, sizeof(*out));
	if (!text || capPx <= 0 || !face(font)) return false;
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

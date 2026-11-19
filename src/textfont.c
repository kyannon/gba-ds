#include "textfont.h"
#include "fonts_data.h"
#include "i18n.h" /* SCRIPT_* */

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

/* Each style (bold, body) is a chain of faces: Jost first, then the fallback
 * faces the language's script needs (tf_fallback_*). Every character is drawn
 * from the first face that has it, so Latin text (game titles, numbers) stays
 * in Jost whatever the interface's language, and a script Jost lacks comes
 * from its own font. A face is scaled by its own capital height, so the
 * letters of two faces match; text sits on one baseline. */
#define CHAIN_MAX 6 /* Jost, the language's own face, then the last resorts (ui.c ui_language_changed) */
typedef struct {
	stbtt_fontinfo fi;
	int capUnits; /* height of "H" in font units */
} Face;
static Face chain[TF_COUNT][CHAIN_MAX];
static int chainLen[TF_COUNT];
static bool ready[TF_COUNT];
static pthread_once_t once = PTHREAD_ONCE_INIT;

static bool face_init(Face* f, const unsigned char* ttf) {
	int off = stbtt_GetFontOffsetForIndex(ttf, 0);
	if (off < 0 || !stbtt_InitFont(&f->fi, ttf, off)) return false;
	int x0, y0, x1, y1;
	if (!stbtt_GetCodepointBox(&f->fi, 'H', &x0, &y0, &x1, &y1) || y1 <= 0) return false;
	f->capUnits = y1;
	return true;
}

static void init(void) {
	for (int f = 0; f < TF_COUNT; ++f) {
		const EmbeddedFont* e = &embeddedFonts[f == TF_BOLD ? EMBEDDED_JOST_BOLD_ITALIC : EMBEDDED_JOST_MEDIUM_ITALIC];
		if (!face_init(&chain[f][0], e->ttf)) continue;
		chainLen[f] = 1;
		ready[f] = true;
	}
}

void tf_fallback_clear(void) {
	pthread_once(&once, init);
	for (int f = 0; f < TF_COUNT; ++f)
		if (chainLen[f] > 1) chainLen[f] = 1;
}

bool tf_fallback_add(int font, const unsigned char* ttf) {
	pthread_once(&once, init);
	if (font < 0 || font >= TF_COUNT || !ready[font] || chainLen[font] >= CHAIN_MAX) return false;
	if (!face_init(&chain[font][chainLen[font]], ttf)) return false;
	++chainLen[font];
	return true;
}

/* The face a character comes from and its glyph (Jost's .notdef when none has it). */
static int pick(int font, int cp, int* glyph) {
	for (int k = 0; k < chainLen[font]; ++k) {
		int g = stbtt_FindGlyphIndex(&chain[font][k].fi, cp);
		if (g) {
			*glyph = g;
			return k;
		}
	}
	*glyph = 0;
	return 0;
}

/* The language's own script face goes first (Japanese shapes for the kanji,
 * Traditional's forms, Korean). After it come the others as last resorts, in
 * every language, because a game's or folder's name is the player's own and
 * can be in any script (and the Language list shows each language's own
 * name): Simplified Chinese, Traditional, Japanese (kana, and kanji Chinese
 * lacks), Korean (Hangul). The Chinese bold faces carry whole character sets
 * (tools/make_zh_font.py), the others the characters the catalogues use. */
void tf_fallback_for_script(int script) {
	static const int order[] = { EMBEDDED_NOTO_SC_BOLD, EMBEDDED_NOTO_TC_BOLD, EMBEDDED_NOTO_JP_BOLD, EMBEDDED_NOTO_KR_BOLD };
	int first = script == SCRIPT_HAN_TC ? EMBEDDED_NOTO_TC_BOLD : script == SCRIPT_JP ? EMBEDDED_NOTO_JP_BOLD
	          : script == SCRIPT_KO ? EMBEDDED_NOTO_KR_BOLD : -1;
	tf_fallback_clear();
	if (first >= 0) { /* (each _REGULAR face follows its _BOLD one in the enum) */
		tf_fallback_add(TF_BOLD, embeddedFonts[first].ttf);
		tf_fallback_add(TF_BODY, embeddedFonts[first + 1].ttf);
	}
	for (size_t i = 0; i < sizeof(order) / sizeof(order[0]); ++i) {
		if (order[i] == first) continue;
		tf_fallback_add(TF_BOLD, embeddedFonts[order[i]].ttf);
		tf_fallback_add(TF_BODY, embeddedFonts[order[i] + 1].ttf);
	}
}

bool tf_has_codepoint(int font, unsigned cp) {
	pthread_once(&once, init);
	if (font < 0 || font >= TF_COUNT || !ready[font]) return false;
	int g;
	pick(font, (int) cp, &g);
	return g != 0;
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

static float scale_for(const Face* f, int capPx) {
	return (float) capPx / (float) f->capUnits;
}

int tf_width(int font, int capPx, const char* text) {
	pthread_once(&once, init);
	if (font < 0 || font >= TF_COUNT || !ready[font] || !text) return 0;
	float x = 0;
	int prev = 0, prevFace = -1;
	for (const char* p = text; *p;) {
		int cp = next_cp(&p), g;
		int k = pick(font, cp, &g);
		const Face* f = &chain[font][k];
		float sc = scale_for(f, capPx);
		if (prev && k == prevFace) x += sc * stbtt_GetGlyphKernAdvance(&f->fi, prev, g);
		int adv, lsb;
		stbtt_GetGlyphHMetrics(&f->fi, g, &adv, &lsb);
		x += sc * adv;
		prev = g, prevFace = k;
	}
	return (int) ceilf(x);
}

bool tf_render(int font, int capPx, const char* text, TfBitmap* out) {
	pthread_once(&once, init);
	memset(out, 0, sizeof(*out));
	if (font < 0 || font >= TF_COUNT || !ready[font] || !text || capPx <= 0) return false;
	/* the rows the text needs: Jost's, or more where a fallback face reaches further */
	float asc = 0, desc = 0;
	{
		bool used[CHAIN_MAX] = { true };
		for (const char* p = text; *p;) {
			int g;
			used[pick(font, next_cp(&p), &g)] = true;
		}
		for (int k = 0; k < chainLen[font]; ++k) {
			if (!used[k]) continue;
			int a, d, gap;
			stbtt_GetFontVMetrics(&chain[font][k].fi, &a, &d, &gap);
			float sc = scale_for(&chain[font][k], capPx);
			if (a * sc > asc) asc = a * sc;
			if (-d * sc > desc) desc = -d * sc;
		}
	}
	int pad = capPx / 2 + 2; /* room for italic overhang either side */
	int base = (int) ceilf(asc) + 1;
	int h = base + (int) ceilf(desc) + 2;
	int w = tf_width(font, capPx, text) + pad * 2;
	if (w <= pad * 2) w = pad * 2 + 1;
	uint8_t* a = calloc((size_t) w * h, 1);
	if (!a) return false;
	float x = (float) pad;
	int prev = 0, prevFace = -1;
	for (const char* p = text; *p;) {
		int cp = next_cp(&p), g;
		int k = pick(font, cp, &g);
		const Face* f = &chain[font][k];
		const stbtt_fontinfo* fi = &f->fi;
		float sc = scale_for(f, capPx);
		if (prev && k == prevFace) x += sc * stbtt_GetGlyphKernAdvance(fi, prev, g);
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
		prev = g, prevFace = k;
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

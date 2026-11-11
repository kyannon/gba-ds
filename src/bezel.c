/* GBA DS's own bezel round the picture (Overlays: Generated), drawn on the
 * device for whatever screen and picture it's asked for. The design is
 * tools/make_overlays.py's (the reference: it draws the same at any size,
 * for checking this against): in GBA pixels (u, one GBA pixel at the
 * picture's scale), the outer colour all over, a bezel in the inner colour
 * 12u round the picture with 3u corners (out to the screen's edge where
 * less than 2u of the outer colour would be left), the GBA DS logo (black
 * outline, dimmed letters) in the bezel by the hinge: below the picture on
 * the top screen, above it on the bottom one, 56% of the bezel's band tall
 * (smaller where the band is cut off; left out under 9 screen pixels), and
 * the picture itself transparent.
 *
 * Made once per change (game.c keeps the texture): a screen's worth of
 * pixels and the logo scaled down, a few milliseconds; drawing it is then
 * one textured quad a frame, as any overlay. */
#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "bezel.h"

SDL_Surface* image_load_mem(const unsigned char* data, size_t size); /* image.c */

#define GBA_PW 240
#define GBA_PH 160
#define MARGIN 12.0f
#define RADIUS 3.0f
#define SLIVER 2.0f
#define LOGO_H 0.56f
#define MIN_LOGO 9.0f

/* the logo as the bezel draws it (black outline, dimmed letters), cropped:
 * tools/make_overlays.py --embed (from assets/brand/gba-ds-logo.png) */
extern const unsigned char bezelLogoPng[];
extern const size_t bezelLogoPngSize;

/* The logo, straight RGBA; decoded once. */
static uint8_t* logoPx;
static int logoW, logoH;

static bool logo_ready(void) {
	if (logoPx) return true;
	SDL_Surface* s = image_load_mem(bezelLogoPng, bezelLogoPngSize);
	if (!s) return false;
	logoW = s->w, logoH = s->h;
	logoPx = malloc((size_t) logoW * logoH * 4);
	if (logoPx)
		for (int y = 0; y < logoH; ++y) memcpy(logoPx + (size_t) y * logoW * 4, (uint8_t*) s->pixels + y * s->pitch, (size_t) logoW * 4);
	SDL_FreeSurface(s);
	return logoPx != NULL;
}

/* The logo at tw x th (area average, premultiplied), composited over the
 * opaque pixels at (dx, dy). */
static void draw_logo(uint8_t* px, int pitch, int sw, int sh, int dx, int dy, int tw, int th) {
	if (tw < 1 || th < 1 || !logo_ready()) return;
	float* mid = calloc((size_t) tw * logoH * 4, sizeof(float)); /* scaled across, full height */
	if (!mid) return;
	float sx = (float) logoW / tw, sy = (float) logoH / th;
	for (int y = 0; y < logoH; ++y) {
		const uint8_t* row = logoPx + (size_t) y * logoW * 4;
		for (int x = 0; x < tw; ++x) {
			float a0 = x * sx, a1 = (x + 1) * sx, acc[4] = { 0 };
			for (int i = (int) a0; i < (int) ceilf(a1) && i < logoW; ++i) {
				float w = fminf(a1, i + 1) - fmaxf(a0, i);
				float al = row[i * 4 + 3] / 255.0f;
				acc[0] += w * row[i * 4] * al, acc[1] += w * row[i * 4 + 1] * al, acc[2] += w * row[i * 4 + 2] * al, acc[3] += w * al;
			}
			float* m = mid + ((size_t) y * tw + x) * 4;
			for (int c = 0; c < 4; ++c) m[c] = acc[c] / sx;
		}
	}
	for (int y = 0; y < th; ++y) {
		int oy = dy + y;
		if (oy < 0 || oy >= sh) continue;
		float b0 = y * sy, b1 = (y + 1) * sy;
		for (int x = 0; x < tw; ++x) {
			int ox = dx + x;
			if (ox < 0 || ox >= sw) continue;
			float acc[4] = { 0 };
			for (int j = (int) b0; j < (int) ceilf(b1) && j < logoH; ++j) {
				float w = fminf(b1, j + 1) - fmaxf(b0, j);
				const float* m = mid + ((size_t) j * tw + x) * 4;
				for (int c = 0; c < 4; ++c) acc[c] += w * m[c];
			}
			float al = acc[3] / sy;
			uint8_t* o = px + oy * pitch + ox * 4;
			for (int c = 0; c < 3; ++c) {
				float v = acc[c] / sy + o[c] * (1 - al); /* (premultiplied over opaque) */
				o[c] = (uint8_t) (v < 0 ? 0 : v > 255 ? 255 : v + 0.5f);
			}
		}
	}
	free(mid);
}

SDL_Surface* bezel_surface(int sw, int sh, SDL_Rect pic, bool bottom, uint32_t inner, uint32_t outer, bool logo) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_RGBA32);
	if (!s) return NULL;
	uint8_t* px = s->pixels;
	int pitch = s->pitch;
	float u = fminf((float) pic.w / GBA_PW, (float) pic.h / GBA_PH);
	float m = MARGIN * u, rad = RADIUS * u, sliver = SLIVER * u;
	/* the bezel: the picture plus the margin, out to the edge where only a sliver of the outer colour would be left */
	float bx0 = pic.x - m, by0 = pic.y - m, bx1 = pic.x + pic.w + m, by1 = pic.y + pic.h + m;
	if (bx0 < sliver) bx0 = -rad * 2;
	if (by0 < sliver) by0 = -rad * 2;
	if (bx1 > sw - sliver) bx1 = sw + rad * 2;
	if (by1 > sh - sliver) by1 = sh + rad * 2;
	float cx = (bx0 + bx1) / 2, cy = (by0 + by1) / 2, hx = (bx1 - bx0) / 2 - rad, hy = (by1 - by0) / 2 - rad;
	uint8_t ic[3] = { inner >> 16, inner >> 8, inner }, oc[3] = { outer >> 16, outer >> 8, outer };
	for (int y = 0; y < sh; ++y) {
		uint8_t* row = px + y * pitch;
		float qy = fabsf(y + 0.5f - cy) - hy;
		for (int x = 0; x < sw; ++x) {
			/* coverage of the rounded rectangle at the pixel's centre */
			float qx = fabsf(x + 0.5f - cx) - hx;
			float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
			float d = (ox > 0 || oy > 0 ? sqrtf(ox * ox + oy * oy) : fmaxf(qx, qy)) - rad;
			float a = 0.5f - d;
			a = a < 0 ? 0 : a > 1 ? 1 : a;
			uint8_t* p = row + x * 4;
			for (int c = 0; c < 3; ++c) p[c] = (uint8_t) (oc[c] + (ic[c] - oc[c]) * a + 0.5f);
			p[3] = 255;
		}
	}
	/* the logo, in the band between the picture and the bezel's edge by the hinge */
	if (logo) {
		float t0 = bottom ? fmaxf(0, by0) : pic.y + pic.h, t1 = bottom ? pic.y : fminf(sh, by1);
		float band = t1 - t0, h = fminf(LOGO_H * MARGIN * u, 0.8f * band);
		if (band > 0 && h >= MIN_LOGO && logo_ready()) {
			int th = (int) lroundf(h), tw = (int) lroundf(logoW * h / logoH);
			draw_logo(px, pitch, sw, sh, (int) lroundf(pic.x + pic.w / 2.0f - tw / 2.0f), (int) lroundf((t0 + t1) / 2 - th / 2.0f), tw, th);
		}
	}
	/* the picture: fully transparent */
	for (int y = SDL_max(0, pic.y); y < SDL_min(sh, pic.y + pic.h); ++y)
		memset(px + y * pitch + SDL_max(0, pic.x) * 4, 0, (size_t) (SDL_min(sw, pic.x + pic.w) - SDL_max(0, pic.x)) * 4);
	return s;
}

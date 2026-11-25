/* The second screen's picture, the parts that are plain pixels in and out
 * (no SDL, no game.c state): moved unchanged out of game.c for the libretro
 * core (discussion #75). */
#include "second.h"
#include <stdlib.h>
#include <string.h>

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

static int cmp_u32(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
	return x < y ? -1 : x > y;
}

/* Builds the still map for "Map Animations: Off" into out (w * h), and marks
 * the pixels that animate in mask. Each animated spot (pixels that change,
 * grouped when they're within a few pixels of each other) is taken from the
 * frame where it is most visible: its brightest state (a marker that blinks
 * on and off, a pulsing glow at its peak), with STILL_RED its reddest if it
 * has red in it, or with STILL_COVER the state that
 * covers the most pixels unlike the spot's surroundings, ties going to the
 * one with the most contrast. */
void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
	int n = r->nFrames, w = r->w, h = r->h, wh = w * h;
	memcpy(out, r->frames[0], (size_t) wh * 4);
	memset(mask, 0, (size_t) wh);
	if (n < 2 || pick == STILL_FIRST) return;
	for (int i = 0; i < wh; ++i) {
		uint32_t p = r->frames[0][i] & 0xFFFFFF;
		for (int f = 1; f < n && !mask[i]; ++f) mask[i] = (r->frames[f][i] & 0xFFFFFF) != p;
	}
	int* label = calloc((size_t) wh, sizeof(int));
	int* queue = malloc((size_t) wh * sizeof(int));
	if (!label || !queue) {
		free(label);
		free(queue);
		return;
	}
	static uint32_t sample[4096];
	const int R = 3; /* animated pixels this close belong to the same spot */
	int spots = 0;
	for (int seed = 0; seed < wh; ++seed) {
		if (!mask[seed] || label[seed]) continue;
		/* flood-fill one spot; list its pixels in queue[0..len) */
		int id = ++spots, len = 0, head = 0;
		int x0 = w, y0 = h, x1 = 0, y1 = 0;
		label[seed] = id;
		queue[len++] = seed;
		while (head < len) {
			int p = queue[head++], px = p % w, py = p / w;
			x0 = imin(x0, px), x1 = imax(x1, px), y0 = imin(y0, py), y1 = imax(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = imax(x0 - 2, 0), y0 = imax(y0 - 2, 0), x1 = imin(x1 + 2, w - 1), y1 = imin(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) (sizeof(sample) / sizeof(sample[0])); ++x) {
					if (!mask[y * w + x]) sample[ns++] = r->frames[0][y * w + x] & 0xFFFFFF;
				}
			}
			qsort(sample, ns, sizeof(sample[0]), cmp_u32);
			for (int i = 0, run = 0, top = 0; i < ns; ++i) {
				run = i && sample[i] == sample[i - 1] ? run + 1 : 1;
				if (run > top) top = run, bg = sample[i];
			}
		}
		int best = 0;
		long bestA = -1, bestB = -1;
		/* STILL_RED: a spot with real red in any frame is judged by how red it
		 * is (red above the mean of green and blue, summed); else brightest */
		bool red = false;
		for (int f = 0; f < n && pick == STILL_RED && !red; ++f) {
			for (int i = 0; i < len && !red; ++i) {
				uint32_t p = r->frames[f][queue[i]];
				int pr = p & 0xFF, pg = (p >> 8) & 0xFF, pb = (p >> 16) & 0xFF;
				red = pr - (pg > pb ? pg : pb) > 80;
			}
		}
		for (int f = 0; f < n; ++f) {
			const uint32_t* fr = r->frames[f];
			long a = 0, b = 0;
			for (int i = 0; i < len; ++i) {
				uint32_t p = fr[queue[i]];
				if (pick == STILL_COVER) a += (p & 0xFFFFFF) != bg;
				else if (red) a += imax(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
				else a += (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			}
			if (pick == STILL_COVER) {
				for (int y = y0; y <= y1; ++y) {
					for (int x = x0; x <= x1; ++x) {
						uint32_t p = fr[y * w + x];
						if (x < x1) {
							uint32_t q = fr[y * w + x + 1];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
						if (y < y1) {
							uint32_t q = fr[(y + 1) * w + x];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
					}
				}
			}
			if (a > bestA || (a == bestA && b > bestB)) best = f, bestA = a, bestB = b;
		}
		for (int i = 0; i < len; ++i) out[queue[i]] = r->frames[best][queue[i]];
	}
	free(label);
	free(queue);
}

/* A turn that waits (GEN-14): the picture at fade level (0 black .. 256 as
 * it is), in a buffer of its own kept between calls; px itself if there's
 * no room for one. (Was inline in game.c's upload_map_frame.) */
const uint32_t* second_fade(const uint32_t* px, size_t n, int level) {
	static uint32_t* fadeBuf;
	static size_t fadeN;
	if (n > fadeN) fadeBuf = realloc(fadeBuf, n * 4), fadeN = fadeBuf ? n : 0;
	if (!fadeBuf) return px;
	for (size_t i = 0; i < n; ++i) {
		uint32_t c = px[i];
		uint32_t r = (c & 0xFF) * (uint32_t) level >> 8, g = ((c >> 8) & 0xFF) * (uint32_t) level >> 8, b = ((c >> 16) & 0xFF) * (uint32_t) level >> 8;
		fadeBuf[i] = (c & 0xFF000000u) | r | g << 8 | b << 16;
	}
	return fadeBuf;
}

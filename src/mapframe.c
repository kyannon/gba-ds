#include "mapframe.h"

#include <SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SDL_Surface* image_load(const char* path); /* image.c */

void mapframe_init(MapFrame* f, const MapProfile* prof, int mw, int mh) {
	memset(f, 0, sizeof(*f));
	f->prof = prof;
	f->mw = mw;
	f->mh = mh;
}

void mapframe_free(MapFrame* f) {
	free(f->base);
	free(f->canvas);
	free(f->atlas);
	memset(f, 0, sizeof(*f));
}

bool mapframe_load_atlas(MapFrame* f, const char* path) {
	if (f->atlas || !f->prof || !f->prof->frame.t) return f->atlas != NULL;
	SDL_Surface* s = image_load(path);
	if (!s) return false;
	f->aw = s->w;
	f->atlas = malloc((size_t) s->w * s->h * 4);
	/* RGBA32 bytes are R,G,B,A: the same layout as mGBA's colours, alpha on top */
	for (int y = 0; y < s->h; ++y) memcpy(&f->atlas[y * s->w], (uint8_t*) s->pixels + y * s->pitch, s->w * 4);
	SDL_FreeSurface(s);
	f->baseDirty = true;
	return true;
}

void mapframe_set_tile(MapFrame* f, const uint32_t* tile, int k, int tx, int ty) {
	if (k <= 0 || k > 16) return;
	if (f->tileOk && f->tk == k && f->tx == tx && f->ty == ty && !memcmp(f->tile, tile, k * k * 4)) return;
	memcpy(f->tile, tile, k * k * 4);
	f->tk = k;
	f->tx = tx;
	f->ty = ty;
	f->tileOk = true;
	f->baseDirty = true;
}

bool mapframe_ready(const MapFrame* f) {
	if (!f->prof) return false;
	if (f->prof->gridBackground) return f->tileOk;
	return f->atlas != NULL;
}

bool mapframe_layout(MapFrame* f, int sw, int sh, const char* mode) {
	if (f->sw == sw && f->sh == sh && !strcmp(f->mode, mode) && f->base) return false;
	f->sw = sw;
	f->sh = sh;
	snprintf(f->mode, sizeof(f->mode), "%s", mode);
	if (!strcmp(mode, "integer")) {
		int k = SDL_max(1, SDL_min(sw / f->mw, sh / f->mh));
		f->cw = sw / k;
		f->ch = sh / k;
	} else if (!strcmp(mode, "fit")) {
		if ((double) sw / f->mw <= (double) sh / f->mh) { /* width-limited */
			f->cw = f->mw;
			f->ch = (int) ((double) sh * f->mw / sw + 0.5);
		} else {
			f->ch = f->mh;
			f->cw = (int) ((double) sw * f->mh / sh + 0.5);
		}
	} else { /* stretch: no surround */
		f->cw = f->mw;
		f->ch = f->mh;
	}
	f->cw = SDL_max(f->cw, f->mw);
	f->ch = SDL_max(f->ch, f->mh);
	f->mx = (f->cw - f->mw) / 2;
	f->my = (f->ch - f->mh) / 2;
	free(f->base);
	free(f->canvas);
	f->base = calloc((size_t) f->cw * f->ch, 4);
	f->canvas = calloc((size_t) f->cw * f->ch, 4);
	f->baseDirty = true;
	return true;
}

static uint32_t rgb_to_native(uint32_t rgb) {
	return 0xFF000000u | ((rgb & 0xFF) << 16) | (rgb & 0xFF00) | ((rgb >> 16) & 0xFF);
}

/* Copies one atlas piece to the canvas, clipped, skipping transparent pixels. */
static void piece(MapFrame* f, int ax, int ay, int w, int h, int dx, int dy) {
	for (int y = 0; y < h; ++y) {
		int cy = dy + y;
		if (cy < 0 || cy >= f->ch) continue;
		for (int x = 0; x < w; ++x) {
			int cx = dx + x;
			if (cx < 0 || cx >= f->cw) continue;
			uint32_t c = f->atlas[(ay + y) * f->aw + ax + x];
			if (c >> 24) f->base[cy * f->cw + cx] = c;
		}
	}
}

static void build_base(MapFrame* f) {
	const MapProfile* p = f->prof;
	int n = f->cw * f->ch;
	if (p->gridBackground && f->tileOk) {
		/* continue the map's grid: canvas (x,y) is map (x-mx, y-my) */
		int k = f->tk;
		for (int y = 0; y < f->ch; ++y) {
			int ty = (((y - f->my) - f->ty) % k + k) % k;
			for (int x = 0; x < f->cw; ++x) {
				int tx = (((x - f->mx) - f->tx) % k + k) % k;
				f->base[y * f->cw + x] = f->tile[ty * k + tx];
			}
		}
	} else {
		uint32_t bg = rgb_to_native(p->bgColor);
		for (int i = 0; i < n; ++i) f->base[i] = bg;
	}
	if (f->atlas && strcmp(f->mode, "stretch")) {
		int t = p->frame.t, pw = p->frame.p ? p->frame.p : t;
		int x0 = f->mx - t, y0 = f->my - t, x1 = f->mx + f->mw, y1 = f->my + f->mh;
		bool ring = !strcmp(f->mode, "integer");
		/* horizontal edges: around the map (ring) or across the whole width (strips) */
		int hx0 = ring ? f->mx : 0, hx1 = ring ? x1 : f->cw;
		for (int x = hx0; x < hx1; x += pw) {
			piece(f, t, 0, SDL_min(pw, hx1 - x), t, x, y0);
			piece(f, t, t + pw, SDL_min(pw, hx1 - x), t, x, y1);
		}
		if (ring) {
			for (int y = f->my; y < y1; y += pw) {
				piece(f, 0, t, t, SDL_min(pw, y1 - y), x0, y);
				piece(f, t + pw, t, t, SDL_min(pw, y1 - y), x1, y);
			}
			piece(f, 0, 0, t, t, x0, y0);
			piece(f, t + pw, 0, t, t, x1, y0);
			piece(f, 0, t + pw, t, t, x0, y1);
			piece(f, t + pw, t + pw, t, t, x1, y1);
		}
	}
	f->baseDirty = false;
}

const uint32_t* mapframe_compose(MapFrame* f, const uint32_t* map) {
	if (f->baseDirty) build_base(f);
	memcpy(f->canvas, f->base, (size_t) f->cw * f->ch * 4);
	for (int y = 0; y < f->mh; ++y) memcpy(&f->canvas[(f->my + y) * f->cw + f->mx], &map[y * f->mw], f->mw * 4);
	return f->canvas;
}

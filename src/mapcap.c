#include "mapcap.h"
#include "profiles.h"
#ifdef __ANDROID__
#include "android.h"
#endif

#include <pthread.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <string.h>

struct MapCapture {
	const MapProfile* prof;
	Emu shadow;
	size_t stateSize;
	size_t cropPixels;
	int cx, cy, cw, ch; /* active crop */
	bool tight;
	bool clean; /* map_crop: also erase button-prompt lettering (profile glyphs) */
	int failures; /* consecutive failed captures (guarded by lock) */
	/* profile.rate: the label copied from the pause screen (worker thread only) */
	uint32_t rateBuf[256 * 16];
	bool rateMask[256 * 16];
	bool rateValid;
	uint32_t* rateRef; /* map (frame 0) when the label was last refreshed */
	int captures;
	uint32_t tile[16 * 16]; /* empty grid cell used to paint over masks */
	bool tileValid;

	/* scratch buffers used by the capture routine (worker thread only) */
	uint32_t* work[MAP_MAX_FRAMES];
	uint32_t* prev;
	uint32_t* under;   /* full-screen render of underLayers (profile.underRect) */
	void* underState;
	bool underValid;
	int workFrames;
	int workStep;     /* emulated frames between work[] frames */
	volatile bool still; /* "Map Animations: Off": record the whole loop (animCycle) */

	/* async machinery */
	pthread_t thread;
	bool noThread; /* no threads (the web build): captures run in mapcap_request */
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool running;
	bool pending; /* a snapshot is waiting for the worker */
	bool busy;
	volatile bool cancel; /* mapcap_cancel: the capture running is called off */
	void* snapshot;

	uint32_t* published[MAP_MAX_FRAMES];
	uint32_t pubTile[16 * 16];
	bool pubTileValid;
	int pubFrames;
	int pubStep;
	uint32_t serial;
	const char* lastError;
};

static int probe_count(const MapProfile* p) {
	int n = 0;
	while (n < 8 && (p->probes[n].x || p->probes[n].y || p->probes[n].rgb)) {
		++n;
	}
	return n;
}

static bool color_near(uint32_t c, uint32_t rgb, int tol) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	int er = (rgb >> 16) & 0xFF, eg = (rgb >> 8) & 0xFF, eb = rgb & 0xFF;
	return abs(r - er) <= tol && abs(g - eg) <= tol && abs(b - eb) <= tol;
}

static bool screen_matches(const MapProfile* p, const uint32_t* fb) {
	int n = probe_count(p);
	int need = p->minProbes ? p->minProbes : n;
	int hit = 0;
	for (int i = 0; i < n; ++i) {
		const Probe* pr = &p->probes[i];
		if (color_near(fb[pr->y * GBA_W + pr->x], pr->rgb, p->tolerance)) {
			++hit;
		}
	}
	if (hit < need) {
		return false;
	}
	if (p->minForeground > 0) {
		int fg = 0;
		for (int y = p->crop_y; y < p->crop_y + p->crop_h && fg < p->minForeground; ++y) {
			for (int x = p->crop_x; x < p->crop_x + p->crop_w; ++x) {
				if (!color_near(fb[y * GBA_W + x], p->bgColor, p->tolerance) && ++fg >= p->minForeground) {
					break;
				}
			}
		}
		if (fg < p->minForeground) {
			return false;
		}
	}
	return true;
}

uint32_t (*memprobe_virtual)(Emu* e, uint32_t addr);

bool memprobes_match(const MemProbe* list, int n, Emu* e) {
	for (int i = 0; i < n && list[i].addr; ++i) {
		const MemProbe* m = &list[i];
		uint32_t v;
		if (m->addr >= 0xF0000000u) v = memprobe_virtual ? memprobe_virtual(e, m->addr) : 0;
		else switch (m->size) {
		case 2: v = emu_read16(e, m->addr); break;
		case 4: v = emu_read32(e, m->addr); break;
		default: v = emu_read8(e, m->addr); break;
		}
		uint32_t mask = m->mask ? m->mask : 0xFFFFFFFF;
		if (m->range ? (v & mask) < m->value || (v & mask) > m->alt
		             : (v & mask) != (m->value & mask) && !(m->hasAlt && (v & mask) == (m->alt & mask))) {
			return false;
		}
	}
	return true;
}

/* The most common grid cell on screen is an empty one. */
static void find_tile(MapCapture* mc, const uint32_t* fb) {
	const MapProfile* p = mc->prof;
	int k = p->gridCell;
	if (k <= 0 || k > 16) return;
	enum { MAXC = 30 * 20 };
	static uint64_t hash[MAXC];
	static int cellX[MAXC], cellY[MAXC];
	int n = 0;
	for (int y = mc->cy + ((p->gridY - mc->cy) % k + k) % k; y + k <= mc->cy + mc->ch && n < MAXC; y += k) {
		for (int x = mc->cx + ((p->gridX - mc->cx) % k + k) % k; x + k <= mc->cx + mc->cw && n < MAXC; x += k) {
			uint64_t h = 1469598103934665603ull;
			for (int j = 0; j < k; ++j) {
				for (int i = 0; i < k; ++i) h = (h ^ fb[(y + j) * GBA_W + x + i]) * 1099511628211ull;
			}
			hash[n] = h;
			cellX[n] = x;
			cellY[n] = y;
			++n;
		}
	}
	int best = -1, bestCount = 0;
	for (int i = 0; i < n; ++i) {
		int c = 0;
		for (int j = 0; j < n; ++j) c += hash[j] == hash[i];
		if (c > bestCount) bestCount = c, best = i;
	}
	if (best < 0 || bestCount < 4) return;
	for (int j = 0; j < k; ++j) {
		for (int i = 0; i < k; ++i) mc->tile[j * k + i] = fb[(cellY[best] + j) * GBA_W + cellX[best] + i];
	}
	mc->tileValid = true;
}

static bool is_arrow(int key, uint32_t c) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	if (key == 1) return r > 140 && b > 140 && g < 110;          /* magenta */
	if (key == 2) return r > 200 && b < 110 && (g > 150 || g < 110); /* yellow, orange, red */
	return false;
}

/* Removes scroll arrows drawn over the map (crop coordinates in out). */
static void remove_arrows(MapCapture* mc, uint32_t* out) {
	const MapProfile* p = mc->prof;
	int k = p->gridCell > 0 ? p->gridCell : 8;
	for (int a = 0; a < 2 && p->arrows[a].w; ++a) {
		for (int y = p->arrows[a].y; y < p->arrows[a].y + p->arrows[a].h; ++y) {
			if (y < mc->cy || y >= mc->cy + mc->ch) continue;
			uint32_t* row = &out[(y - mc->cy) * mc->cw];
			for (int x = p->arrows[a].x; x < p->arrows[a].x + p->arrows[a].w; ++x) {
				int u = x - mc->cx;
				if (u < 0 || u >= mc->cw || !is_arrow(p->arrowKey, row[u])) continue;
				/* majority vote among the same pixel 1-3 grid cells to either side,
				 * so one-off details (door dividers) don't get copied */
				uint32_t cands[6];
				int nc = 0;
				for (int d = 1; d <= 3; ++d) {
					int c1 = u - d * k, c2 = u + d * k;
					if (c1 >= 0 && !is_arrow(p->arrowKey, row[c1])) cands[nc++] = row[c1];
					if (c2 < mc->cw && !is_arrow(p->arrowKey, row[c2])) cands[nc++] = row[c2];
				}
				uint32_t rep = 0;
				bool found = false;
				int bestN = 0;
				for (int i = 0; i < nc; ++i) {
					int n = 0;
					for (int j = 0; j < nc; ++j) n += cands[j] == cands[i];
					if (n > bestN) bestN = n, rep = cands[i], found = true;
				}
				if (!found && mc->tileValid) {
					rep = mc->tile[(((y - p->gridY) % k + k) % k) * k + ((x - p->gridX) % k + k) % k];
					found = true;
				}
				if (found) row[u] = rep;
			}
		}
	}
}

/* Erases button-prompt letters (profile glyphs): pixels of the letter's colours
 * are replaced, outside in, by the most common colour among their already-clean
 * 8 neighbours, so the letter takes on the button face around it. */
#define IMAX(a, b) ((a) > (b) ? (a) : (b))
#define IMIN(a, b) ((a) < (b) ? (a) : (b))

static bool in_grid_glyph(const MapProfile* p, int x, int y) {
	for (int g = 0; g < 10 && p->glyphs[g].w; ++g) {
		if (p->glyphs[g].grid && x >= p->glyphs[g].x && x < p->glyphs[g].x + p->glyphs[g].w &&
		    y >= p->glyphs[g].y && y < p->glyphs[g].y + p->glyphs[g].h) {
			return true;
		}
	}
	return false;
}

/* Rebuilds a patch of the map grid: each pixel becomes the most common value
 * of the same pixel 1-3 cells above and to either side (never below: the
 * prompts sit just above the frame), skipping spots inside any grid rect. */
static void rebuild_grid(MapCapture* mc, uint32_t* out, int x0, int y0, int x1, int y1) {
	const MapProfile* p = mc->prof;
	int k = p->gridCell > 0 ? p->gridCell : 8;
	for (int y = y0; y < y1; ++y) {
		for (int x = x0; x < x1; ++x) {
			uint32_t cands[9];
			int nc = 0;
			static const int dirs[3][2] = { { 0, -1 }, { -1, 0 }, { 1, 0 } };
			for (int d = 0; d < 3; ++d) {
				for (int s = 1; s <= 3; ++s) {
					int u = x + dirs[d][0] * s * k, v = y + dirs[d][1] * s * k;
					if (u < mc->cx || v < mc->cy || u >= mc->cx + mc->cw || v >= mc->cy + mc->ch) continue;
					if (in_grid_glyph(p, u, v)) continue;
					cands[nc++] = out[(v - mc->cy) * mc->cw + (u - mc->cx)];
				}
			}
			if (!nc) continue;
			uint32_t rep = cands[0];
			int bestN = 0;
			for (int i = 0; i < nc; ++i) {
				int n = 0;
				for (int j = 0; j < nc; ++j) n += cands[j] == cands[i];
				if (n > bestN) bestN = n, rep = cands[i];
			}
			out[(y - mc->cy) * mc->cw + (x - mc->cx)] = rep;
		}
	}
}

static bool is_key(const uint32_t* keys, int nk, uint32_t c) {
	for (int i = 0; i < nk; ++i) {
		if ((c & 0xFFFFFF) == keys[i]) return true;
	}
	return false;
}

static void remove_glyphs(MapCapture* mc, uint32_t* out) {
	const MapProfile* p = mc->prof;
	for (int g = 0; g < 10 && p->glyphs[g].w; ++g) {
		/* framebuffer pixels are 0xAABBGGRR */
		uint32_t keys[8];
		int nk = p->glyphs[g].n > 0 ? IMIN(p->glyphs[g].n, 8) : 1;
		for (int i = 0; i < nk; ++i) {
			uint32_t rgb = p->glyphs[g].rgb[i];
			keys[i] = ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16);
		}
#define IS_KEY(c) is_key(keys, nk, c)
		int x0 = IMAX(p->glyphs[g].x, mc->cx), x1 = IMIN(p->glyphs[g].x + p->glyphs[g].w, mc->cx + mc->cw);
		int y0 = IMAX(p->glyphs[g].y, mc->cy), y1 = IMIN(p->glyphs[g].y + p->glyphs[g].h, mc->cy + mc->ch);
		if (x0 >= x1 || y0 >= y1) continue;
		if (p->glyphs[g].grid) {
			rebuild_grid(mc, out, x0, y0, x1, y1);
			continue;
		}
		if (p->glyphs[g].n < 0) {
			for (int y = y0; y < y1; ++y) {
				for (int x = x0; x < x1; ++x) {
					uint32_t* px = &out[(y - mc->cy) * mc->cw + (x - mc->cx)];
					*px = (*px & 0xFF000000) | keys[0];
				}
			}
			continue;
		}
		if (p->glyphs[g].flat) {
			/* a flat plate: paint the letters in the plate's main colour */
			uint32_t cols[64];
			int counts[64], ncols = 0;
			for (int y = y0; y < y1; ++y) {
				for (int x = x0; x < x1; ++x) {
					uint32_t c = out[(y - mc->cy) * mc->cw + (x - mc->cx)];
					if (IS_KEY(c)) continue;
					int i = 0;
					while (i < ncols && cols[i] != c) ++i;
					if (i == ncols) {
						if (ncols == 64) continue;
						cols[ncols] = c, counts[ncols++] = 0;
					}
					++counts[i];
				}
			}
			if (!ncols) continue;
			int best = 0;
			for (int i = 1; i < ncols; ++i) {
				if (counts[i] > counts[best]) best = i;
			}
			for (int y = y0; y < y1; ++y) {
				for (int x = x0; x < x1; ++x) {
					uint32_t* px = &out[(y - mc->cy) * mc->cw + (x - mc->cx)];
					if (IS_KEY(*px)) *px = cols[best];
				}
			}
			continue;
		}
		bool changed = true;
		for (int pass = 0; pass < 16 && changed; ++pass) {
			changed = false;
			for (int y = y0; y < y1; ++y) {
				for (int x = x0; x < x1; ++x) {
					uint32_t* px = &out[(y - mc->cy) * mc->cw + (x - mc->cx)];
					if (!IS_KEY(*px)) continue;
					uint32_t cands[8];
					int nc = 0;
					for (int dy = -1; dy <= 1; ++dy) {
						for (int dx = -1; dx <= 1; ++dx) {
							int u = x + dx - mc->cx, v = y + dy - mc->cy;
							if ((!dx && !dy) || u < 0 || v < 0 || u >= mc->cw || v >= mc->ch) continue;
							uint32_t c = out[v * mc->cw + u];
							if (!IS_KEY(c)) cands[nc++] = c;
						}
					}
					if (nc < 2) continue; /* deep inside the letter: wait for the next pass */
					uint32_t rep = cands[0];
					int bestN = 0;
					for (int i = 0; i < nc; ++i) {
						int n = 0;
						for (int j = 0; j < nc; ++j) n += cands[j] == cands[i];
						if (n > bestN) bestN = n, rep = cands[i];
					}
					*px = rep;
					changed = true;
				}
			}
		}
	}
}

static uint32_t to_fb(uint32_t rgb) { /* 0xRRGGBB -> framebuffer 0xBBGGRR */
	return ((rgb >> 16) & 0xFF) | (rgb & 0xFF00) | ((rgb & 0xFF) << 16);
}

static void recolor(MapCapture* mc, uint32_t* out) {
	const MapProfile* p = mc->prof;
	for (int r = 0; r < 6 && p->recolor[r].w; ++r) {
		int x0 = IMAX(p->recolor[r].x, mc->cx), x1 = IMIN(p->recolor[r].x + p->recolor[r].w, mc->cx + mc->cw);
		int y0 = IMAX(p->recolor[r].y, mc->cy), y1 = IMIN(p->recolor[r].y + p->recolor[r].h, mc->cy + mc->ch);
		int n = IMIN(p->recolor[r].n, 16);
		for (int y = y0; y < y1; ++y) {
			for (int x = x0; x < x1; ++x) {
				uint32_t* px = &out[(y - mc->cy) * mc->cw + (x - mc->cx)];
				for (int i = 0; i < n; ++i) {
					if ((*px & 0xFFFFFF) == to_fb(p->recolor[r].from[i])) {
						*px = (*px & 0xFF000000) | to_fb(p->recolor[r].to[i]);
						break;
					}
				}
			}
		}
	}
}

static void paint_tab(MapCapture* mc, uint32_t* out) {
	const MapProfile* p = mc->prof;
	int h = p->tab.base - p->tab.top;
	int xa = IMIN(p->tab.x0 - h, p->tab.n0), xb = IMAX(p->tab.x1 + h, p->tab.n1);
	for (int x = xa; x <= xb; ++x) {
		int t;
		if (x >= p->tab.x0 && x <= p->tab.x1) t = p->tab.top;
		else if (x < p->tab.x0 && x >= p->tab.x0 - h) t = p->tab.top + (p->tab.x0 - x);
		else if (x > p->tab.x1 && x <= p->tab.x1 + h) t = p->tab.top + (x - p->tab.x1);
		else t = p->tab.base;
		if (t >= p->tab.base && (x < p->tab.n0 || x > p->tab.n1)) continue;
		if (x < mc->cx || x >= mc->cx + mc->cw) continue;
		for (int y = t; y <= p->tab.bottom; ++y) {
			if (y < mc->cy || y >= mc->cy + mc->ch) continue;
			int d = IMIN(y - t, p->tab.n - 1);
			uint32_t* px = &out[(y - mc->cy) * mc->cw + (x - mc->cx)];
			*px = (*px & 0xFF000000) | to_fb(p->tab.colors[d]);
		}
	}
}

static void patch_under(MapCapture* mc, uint32_t* out) {
	const MapProfile* p = mc->prof;
	for (int r = 0; r < 2 && p->underRect[r].x1; ++r) {
		int x0 = IMAX(p->underRect[r].x0, mc->cx), x1 = IMIN(p->underRect[r].x1, mc->cx + mc->cw - 1);
		int y0 = IMAX(p->underRect[r].y0, mc->cy), y1 = IMIN(p->underRect[r].y1, mc->cy + mc->ch - 1);
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) out[(y - mc->cy) * mc->cw + (x - mc->cx)] = mc->under[y * GBA_W + x];
		}
	}
}

/* Renders the current frame with only the profile's underLayers, then puts
 * the core back exactly as it was (layer switches apply on the state reload). */
static void grab_under(MapCapture* mc, Emu* s) {
	const MapProfile* p = mc->prof;
	if (!emu_state_save(s, mc->underState)) return;
	SpriteRule rules[8];
	int n = 0, base;
	while (n < 6 && p->captureHide[n].x1) rules[n] = p->captureHide[n], ++n;
	base = n;
	for (int i = 0; i < 2 && p->underHide[i].x1; ++i) rules[n++] = p->underHide[i];
	if (n > base) emu_hide_sprites(s, rules, n);
	emu_layers(s, p->underLayers);
	emu_state_load(s, mc->underState);
	emu_frame(s, 0);
	memcpy(mc->under, s->fb, GBA_W * GBA_H * sizeof(uint32_t));
	emu_layers(s, 0x1F);
	if (n > base) emu_hide_sprites(s, rules, base);
	emu_state_load(s, mc->underState);
	mc->underValid = true;
}

static void crop(MapCapture* mc, const uint32_t* fb, uint32_t* out) {
	const MapProfile* p = mc->prof;
	for (int y = 0; y < mc->ch; ++y) {
		memcpy(&out[y * mc->cw], &fb[(mc->cy + y) * GBA_W + mc->cx], mc->cw * sizeof(uint32_t));
	}
	if (mc->clean && mc->underValid) patch_under(mc, out);
	if (mc->clean && p->glyphs[0].w) remove_glyphs(mc, out);
	if (mc->clean && p->recolor[0].w) recolor(mc, out);
	if (mc->clean && p->tab.n) paint_tab(mc, out);
	if (!mc->tight || !p->masks[0].w) return;
	if (!mc->tileValid) find_tile(mc, fb);
	if (p->arrows[0].w) remove_arrows(mc, out);
	if (!mc->tileValid) return;
	int k = p->gridCell;
	for (int m = 0; m < 8 && p->masks[m].w; ++m) {
		for (int y = p->masks[m].y; y < p->masks[m].y + p->masks[m].h; ++y) {
			for (int x = p->masks[m].x; x < p->masks[m].x + p->masks[m].w; ++x) {
				if (x < mc->cx || y < mc->cy || x >= mc->cx + mc->cw || y >= mc->cy + mc->ch) continue;
				int ty = ((y - p->gridY) % k + k) % k, tx = ((x - p->gridX) % k + k) % k;
				out[(y - mc->cy) * mc->cw + (x - mc->cx)] = mc->tile[ty * k + tx];
			}
		}
	}
}

static int count_diff(const uint32_t* a, const uint32_t* b, size_t n, int limit) {
	int d = 0;
	for (size_t i = 0; i < n; ++i) {
		if (a[i] != b[i] && ++d > limit) {
			break;
		}
	}
	return d;
}

static bool map_up(const MapProfile* p, Emu* s) {
	return memprobes_match(p->mem, 4, s) && screen_matches(p, s->fb);
}

/* Runs the shadow core from `state`; returns NULL on success or a reason.
 * Fills mc->work[0..workFrames). */
static bool grab_rate(MapCapture* mc, const void* state);

/* Summed brightness of a frame (fast, for comparing fade levels). */
static long frame_luma(const uint32_t* px, size_t n) {
	long sum = 0;
	for (size_t i = 0; i < n; i += 3) sum += (px[i] & 0xFF) + ((px[i] >> 8) & 0xFF) * 2 + ((px[i] >> 16) & 0xFF);
	return sum + 1;
}
static void place_rate(MapCapture* mc);

static const char* do_capture_(MapCapture* mc, const void* state);
/* A capture, timed for GBADS_DEBUG: how long, how many of the shadow
 * core's frames */
static const char* do_capture(MapCapture* mc, const void* state) {
	static int dbg = -1;
	if (dbg < 0) dbg = getenv("GBADS_DEBUG") != NULL;
	struct timespec t0, t1;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	unsigned long f0 = mc->shadow.frames;
	const char* err = do_capture_(mc, state);
	clock_gettime(CLOCK_MONOTONIC, &t1);
	if (dbg) fprintf(stderr, "capture: %.0f ms, %lu frames%s%s\n", (t1.tv_sec - t0.tv_sec) * 1e3 + (t1.tv_nsec - t0.tv_nsec) / 1e6, mc->shadow.frames - f0,
	                 mc->cancel ? ", called off" : err ? ": " : "", mc->cancel ? "" : err ? err : "");
	return err;
}
static const char* do_capture_(MapCapture* mc, const void* state) {
	const MapProfile* p = mc->prof;
	Emu* s = &mc->shadow;
	size_t n = mc->cropPixels;
	mc->workFrames = 0;
	mc->tileValid = false;
	mc->underValid = false;
	if (p->composePages) {
		int n = MAP_MAX_FRAMES, step = 1;
		const char* err = p->composePages(s, state, mc->work, &n, &step, mc->cw, mc->ch);
		mc->workFrames = err ? 0 : n < 1 ? 1 : n > MAP_MAX_FRAMES ? MAP_MAX_FRAMES : n;
		mc->workStep = step < 1 ? 1 : step;
		return err;
	}
	if (p->compose) {
		const char* err = p->compose(s, state, mc->work[0], mc->cw, mc->ch);
		mc->workFrames = err ? 0 : 1;
		mc->workStep = 1;
		return err;
	}
	if (!emu_state_load(s, state)) {
		return "state load failed";
	}
	/* If the screen already passes as "the map" before we press anything,
	 * the probes can't tell it apart from whatever is showing (or the player
	 * has the map open themselves). Either way, don't trust a capture. */
	emu_frame(s, 0);
	if (map_up(p, s)) {
		return "screen already looks like the map";
	}
	for (int i = 0; i < 8 && p->open[i].frames; ++i) {
		for (int f = 0; f < p->open[i].frames; ++f) {
			emu_frame(s, p->open[i].keys);
		}
	}
	if (p->open2[0].frames) {
		int limit = p->openWithin ? p->openWithin : p->settleMax;
		int g = 0;
		while (g < limit && !memprobes_match(p->mem, 4, s)) emu_frame(s, 0), ++g;
		if (g >= limit) return "map button did nothing";
		if (memprobes_match(p->open2If, 2, s)) {
			for (int f = 0; f < p->open2Delay; ++f) emu_frame(s, 0);
			for (int i = 0; i < 4 && p->open2[i].frames; ++i) {
				for (int f = 0; f < p->open2[i].frames; ++f) emu_frame(s, p->open2[i].keys);
			}
		}
	}
	int stable = 0;
	bool havePrev = false;
	bool everUp = false;
	int f, presses = 1, base = 0;
	for (f = 0; f < p->settleMax; ++f) {
		if (p->captureLayers) emu_write16(s, 0x05000000, p->captureBack); /* black (or the profile's colour) behind the layers shown */
		emu_frame(s, 0);
		if (!everUp && p->openWithin && f - base >= p->openWithin) {
			if (presses < p->openTries) {
				/* ignored for now: press it again */
				for (int i = 0; i < 8 && p->open[i].frames; ++i) {
					for (int g = 0; g < p->open[i].frames; ++g) emu_frame(s, p->open[i].keys);
				}
				++presses, base = f;
				continue;
			}
			return "map button did nothing"; /* quick: no map available here */
		}
		if (!map_up(p, s)) {
			stable = 0;
			havePrev = false;
			continue;
		}
		everUp = true;
		crop(mc, s->fb, mc->work[0]);
		if (havePrev && count_diff(mc->prev, mc->work[0], n, p->maxDiff) <= p->maxDiff) {
			if (++stable >= p->stableFrames) {
				break;
			}
		} else {
			stable = 0;
		}
		memcpy(mc->prev, mc->work[0], n * sizeof(uint32_t));
		havePrev = true;
	}
	if (f >= p->settleMax) {
		return everUp ? "map screen never settled" : "map screen did not appear";
	}
	if (mc->clean && p->underRect[0].x1) {
		grab_under(mc, s);
		if (mc->underValid) patch_under(mc, mc->work[0]);
	}
	mc->workFrames = 1;
	int want = p->animFrames > MAP_MAX_FRAMES ? MAP_MAX_FRAMES : p->animFrames;
	int step = p->animStep;
	if (mc->still && p->animCycle > want * step) {
		want = MAP_MAX_FRAMES;
		step = (p->animCycle + want - 1) / want;
	}
	mc->workStep = step;
	bool animated = false;
	for (int i = 1; i < want; ++i) {
		for (int k = 0; k < step; ++k) {
			emu_frame(s, 0);
		}
		if (!map_up(p, s)) {
			break;
		}
		crop(mc, s->fb, mc->work[i]);
		if (memcmp(mc->work[i], mc->work[0], n * sizeof(uint32_t))) {
			animated = true;
		}
		mc->workFrames = i + 1;
	}
	if (!animated) {
		mc->workFrames = 1;
	}
	/* A fade still in progress isn't animation: drop leading frames whose
	 * brightness differs from the last one by more than 3% of full scale
	 * (a blinking marker changes far less). */
	if (mc->workFrames > 1) {
		long last = frame_luma(mc->work[mc->workFrames - 1], n);
		long full = (long) ((n + 2) / 3) * 1020; /* every sampled pixel white */
		int skip = 0;
		while (skip < mc->workFrames - 1 && labs(frame_luma(mc->work[skip], n) - last) * 100 > full * 3) ++skip;
		if (skip) {
			for (int i = skip; i < mc->workFrames; ++i) memcpy(mc->work[i - skip], mc->work[i], n * sizeof(uint32_t));
			mc->workFrames -= skip;
		}
	}
	if (p->rate.w) {
		/* refresh the label when the map gained pixels (a new room, or the
		 * marker moved to another one), and now and then regardless */
		int grown = 0;
		if (mc->rateValid) {
			for (size_t i = 0; i < n && grown < 4; ++i) {
				grown += (mc->rateRef[i] & 0xFFFFFF) != (mc->work[0][i] & 0xFFFFFF) &&
				         (mc->rateRef[i] & 0xFFFFFF) == (to_fb(p->bgColor) & 0xFFFFFF);
			}
		}
		if (!mc->rateValid || grown >= 4 || ++mc->captures % 8 == 0) {
			if (grab_rate(mc, state)) {
				mc->rateValid = true;
				memcpy(mc->rateRef, mc->work[0], n * sizeof(uint32_t));
			}
		}
		if (mc->rateValid) place_rate(mc);
	}
	return NULL;
}

/* Opens the pause screen in the shadow core and copies the rate label. */
static bool grab_rate(MapCapture* mc, const void* state) {
	const MapProfile* p = mc->prof;
	Emu* s = &mc->shadow;
	int w = p->rate.w, h = p->rate.h;
	if ((w + 2) * (h + 2) > 256 * 16 || !emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	for (int i = 0; i < 4 && p->rate.open[i].frames; ++i) {
		for (int f = 0; f < p->rate.open[i].frames; ++f) emu_frame(s, p->rate.open[i].keys);
	}
	for (int f = 0; f < p->rate.wait; ++f) emu_frame(s, 0);
	uint32_t text = to_fb(p->rate.text);
	/* the digits, plus a 1-pixel outline in the map's background colour */
	int count = 0;
	uint32_t bg = 0xFF000000 | to_fb(p->bgColor);
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			mc->rateMask[y * w + x] = false;
			if ((s->fb[(p->rate.y + y) * GBA_W + p->rate.x + x] & 0xFFFFFF) == text) ++count;
		}
	}
	if (count < 12) return false; /* the pause screen didn't come up */
	/* stored with a 1px border all round for the outline */
	int bw = w + 2, bh = h + 2;
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			bool isText = false, nearText = false;
			for (int dy = -1; dy <= 1; ++dy) {
				for (int dx = -1; dx <= 1; ++dx) {
					int u = x - 1 + dx, v = y - 1 + dy;
					if (u < 0 || v < 0 || u >= w || v >= h) continue;
					bool t = (s->fb[(p->rate.y + v) * GBA_W + p->rate.x + u] & 0xFFFFFF) == text;
					if (t && !dx && !dy) isText = true;
					if (t) nearText = true;
				}
			}
			mc->rateBuf[y * bw + x] = isText ? (0xFF000000 | text) : bg;
			mc->rateMask[y * bw + x] = nearText;
		}
	}
	return true;
}

/* Puts the rate label into every captured frame, in the first corner that
 * has no rooms (or the emptiest one). */
static void place_rate(MapCapture* mc) {
	const MapProfile* p = mc->prof;
	int w = p->rate.w + 2, h = p->rate.h + 2; /* with the outline */
	if (w + 2 > mc->cw || h + 4 > mc->ch) return;
	int ys[2] = { 1, mc->ch - h - 1 };
	int x0 = mc->cw - w - 1;
	uint32_t bg = to_fb(p->bgColor) & 0xFFFFFF;
	int best = 0, bestCount = 1 << 30;
	for (int c = 0; c < 2; ++c) {
		int count = 0;
		for (int y = ys[c]; y < ys[c] + h; ++y) {
			for (int x = x0; x < x0 + w; ++x) {
				if ((mc->work[0][y * mc->cw + x] & 0xFFFFFF) != bg) ++count;
			}
		}
		if (count < bestCount) bestCount = count, best = c;
		if (!count) break;
	}
	int y0 = ys[best];
	for (int i = 0; i < mc->workFrames; ++i) {
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				if (mc->rateMask[y * w + x]) mc->work[i][(y0 + y) * mc->cw + x0 + x] = mc->rateBuf[y * w + x];
			}
		}
	}
}

static void publish(MapCapture* mc, const char* err);

static void* worker(void* arg) {
	MapCapture* mc = arg;
	/* the live game (and its audio) gets the CPU first */
#ifndef __wasi__
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 10);
#endif
#ifdef __ANDROID__
	android_pin_worker_thread("map capture");
#endif
	pthread_mutex_lock(&mc->lock);
	while (mc->running) {
		if (!mc->pending) {
			pthread_cond_wait(&mc->cond, &mc->lock);
			continue;
		}
		mc->pending = false;
		mc->busy = true;
		pthread_mutex_unlock(&mc->lock);

		const char* err = do_capture(mc, mc->snapshot);

		pthread_mutex_lock(&mc->lock);
		/* (called off: a newer one's wanted; neither a result nor a failure) */
		if (!mc->cancel) publish(mc, err);
		mc->cancel = false;
		mc->busy = false;
	}
	pthread_mutex_unlock(&mc->lock);
	return NULL;
}

/* A capture's result, for mapcap_poll (lock held). */
static void publish(MapCapture* mc, const char* err) {
	size_t n = mc->cropPixels;
	mc->lastError = err;
	mc->failures = err ? mc->failures + 1 : 0;
	if (err) return;
	for (int i = 0; i < mc->workFrames; ++i) {
		memcpy(mc->published[i], mc->work[i], n * sizeof(uint32_t));
	}
	mc->pubFrames = mc->workFrames;
	mc->pubStep = mc->workStep;
	if (mc->tileValid) {
		memcpy(mc->pubTile, mc->tile, sizeof(mc->pubTile));
		mc->pubTileValid = true;
	}
	++mc->serial;
}

MapCapture* mapcap_create(const char* romPath, const char* gameCode, bool tight) {
	const MapProfile* p = profile_find(gameCode);
	if (!p || p->logoOnly) {
		return NULL;
	}
	MapCapture* mc = calloc(1, sizeof(*mc));
	mc->prof = p;
	memprobe_virtual = p->virt;
	if (!emu_open(&mc->shadow, romPath, NULL, false)) {
		free(mc);
		return NULL;
	}
	mc->shadow.cancel = &mc->cancel;
	mc->stateSize = emu_state_size(&mc->shadow);
	mc->snapshot = malloc(mc->stateSize);
	mc->tight = tight && p->tight_w > 0;
	mc->clean = tight;
	if ((mc->clean || p->captureLayers) && p->captureHide[0].x1) {
		int n = 0;
		while (n < 6 && p->captureHide[n].x1) ++n;
		emu_hide_sprites(&mc->shadow, p->captureHide, n);
	}
	if (p->captureLayers) emu_layers(&mc->shadow, (p->captureLayers & 0xF) | 0x10);
	if (mc->tight) {
		mc->cx = p->tight_x, mc->cy = p->tight_y, mc->cw = p->tight_w, mc->ch = p->tight_h;
	} else {
		mc->cx = p->crop_x, mc->cy = p->crop_y, mc->cw = p->crop_w, mc->ch = p->crop_h;
	}
	mc->cropPixels = (size_t) mc->cw * mc->ch;
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		mc->work[i] = calloc(mc->cropPixels, sizeof(uint32_t));
		mc->published[i] = calloc(mc->cropPixels, sizeof(uint32_t));
	}
	mc->prev = calloc(mc->cropPixels, sizeof(uint32_t));
	if (mc->clean && p->underRect[0].x1) {
		mc->under = calloc((size_t) GBA_W * GBA_H, sizeof(uint32_t));
		mc->underState = malloc(mc->stateSize);
	}
	mc->rateRef = calloc(mc->cropPixels, sizeof(uint32_t));
	pthread_mutex_init(&mc->lock, NULL);
	pthread_cond_init(&mc->cond, NULL);
	return mc;
}

/* GBADS_SYNC=1 (regression tests): captures run in mapcap_request, the
 * game waiting for them, as in the web build, so every run gives the same
 * pictures at the same frames. */
static void start_thread(MapCapture* mc) {
	static int sync = -1;
	if (sync < 0) sync = getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC"));
	if (sync) mc->noThread = true;
	if (!mc->running && !mc->noThread) {
		mc->running = true;
		if (pthread_create(&mc->thread, NULL, worker, mc) != 0) mc->running = false, mc->noThread = true;
	}
}

void mapcap_destroy(MapCapture* mc) {
	if (!mc) {
		return;
	}
	if (mc->running) {
		pthread_mutex_lock(&mc->lock);
		mc->running = false;
		pthread_cond_signal(&mc->cond);
		pthread_mutex_unlock(&mc->lock);
		pthread_join(mc->thread, NULL);
	}
	emu_close(&mc->shadow);
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mc->work[i]);
		free(mc->published[i]);
	}
	free(mc->prev);
	free(mc->under);
	free(mc->underState);
	free(mc->rateRef);
	free(mc->snapshot);
	pthread_mutex_destroy(&mc->lock);
	pthread_cond_destroy(&mc->cond);
	free(mc);
}

const char* mapcap_title(MapCapture* mc) {
	return mc->prof->title;
}

void mapcap_size(MapCapture* mc, int* w, int* h) {
	*w = mc->cw;
	*h = mc->ch;
}

MapResult mapcap_capture_sync(MapCapture* mc, Emu* live) {
	MapResult r;
	memset(&r, 0, sizeof(r));
	if (!emu_state_save(live, mc->snapshot)) {
		r.why = "state save failed";
		return r;
	}
	r.why = do_capture(mc, mc->snapshot);
	r.ok = !r.why;
	r.w = mc->cw;
	r.h = mc->ch;
	r.nFrames = mc->workFrames;
	r.frameStep = mc->workStep;
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		r.frames[i] = mc->work[i];
	}
	r.pixels = r.frames[0];
	return r;
}

bool mapcap_request(MapCapture* mc, Emu* live) {
	start_thread(mc);
	if (mc->noThread) {
		/* no threads: the capture now, the game waiting for it */
		if (!emu_state_save(live, mc->snapshot)) return false;
		publish(mc, do_capture(mc, mc->snapshot));
		return true;
	}
	pthread_mutex_lock(&mc->lock);
	if (mc->busy || mc->pending) {
		pthread_mutex_unlock(&mc->lock);
		return false;
	}
	/* The worker only reads the snapshot after `pending` is set. */
	bool ok = emu_state_save(live, mc->snapshot);
	if (ok) {
		mc->pending = true;
		pthread_cond_signal(&mc->cond);
	}
	pthread_mutex_unlock(&mc->lock);
	return ok;
}

void mapcap_set_still(MapCapture* mc, bool still) {
	mc->still = still;
}

const MapProfile* mapcap_profile(MapCapture* mc) {
	return mc->prof;
}

void mapcap_cancel(MapCapture* mc) {
	pthread_mutex_lock(&mc->lock);
	if (mc->busy) mc->cancel = true;
	pthread_mutex_unlock(&mc->lock);
}

bool mapcap_busy(MapCapture* mc) {
	pthread_mutex_lock(&mc->lock);
	bool b = mc->busy || mc->pending;
	pthread_mutex_unlock(&mc->lock);
	return b;
}

bool mapcap_poll(MapCapture* mc, uint32_t lastSerial, MapResult* out) {
	size_t n = mc->cropPixels;
	pthread_mutex_lock(&mc->lock);
	bool fresh = mc->serial != lastSerial && mc->serial != 0;
	if (fresh) {
		out->ok = true;
		out->why = NULL;
		out->w = mc->cw;
		out->h = mc->ch;
		out->nFrames = mc->pubFrames;
		out->frameStep = mc->pubStep;
		for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
			if (!out->frames[i]) {
				out->frames[i] = malloc(n * sizeof(uint32_t));
			}
			if (i < mc->pubFrames) {
				memcpy(out->frames[i], mc->published[i], n * sizeof(uint32_t));
			}
		}
		out->pixels = out->frames[0];
		out->serial = mc->serial;
		int k = mc->prof->gridCell;
		out->hasTile = mc->pubTileValid && k > 0 && k <= 16;
		if (out->hasTile) {
			out->tileK = k;
			out->tileX = ((mc->prof->gridX - mc->cx) % k + k) % k;
			out->tileY = ((mc->prof->gridY - mc->cy) % k + k) % k;
			memcpy(out->tile, mc->pubTile, sizeof(out->tile));
		}
	}
	pthread_mutex_unlock(&mc->lock);
	return fresh;
}

int mapcap_failures(MapCapture* mc, const char** why) {
	pthread_mutex_lock(&mc->lock);
	int f = mc->failures;
	if (why) *why = mc->lastError;
	pthread_mutex_unlock(&mc->lock);
	return f;
}

bool mapcap_in_menu(MapCapture* mc, Emu* live) {
	return mc->prof->menu[0].addr && memprobes_match(mc->prof->menu, 2, live);
}

bool mapcap_live_ok(MapCapture* mc, Emu* live) {
	return memprobes_match(mc->prof->live, 4, live);
}

uint32_t mapcap_room_key(MapCapture* mc, Emu* live) {
	uint32_t key = 2166136261u;
	bool any = false;
	for (int i = 0; i < 4 && mc->prof->room[i].addr; ++i) {
		const MemProbe* m = &mc->prof->room[i];
		uint32_t v = m->addr >= 0xF0000000u ? (memprobe_virtual ? memprobe_virtual(live, m->addr) : 0)
		           : m->size == 4 ? emu_read32(live, m->addr) : m->size == 2 ? emu_read16(live, m->addr) : emu_read8(live, m->addr);
		if (m->mask) {
			v &= m->mask;
		}
		key = (key ^ v) * 16777619u;
		any = true;
	}
	return any ? key : 0;
}

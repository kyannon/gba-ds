#include "logo.h"
#include "emu.h"
#include "profiles.h"
#ifdef __ANDROID__
#include "android.h"
#endif

#include <mgba/flags.h>
#include <mgba/core/core.h>

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_WRITE_IMPLEMENTATION
#define STBIW_WINDOWS_UTF8 0
#include "stb_image_write.h"
#pragma GCC diagnostic pop

enum { NLAYERS = 5 }; /* bg0-3, obj */
static volatile bool cancelAll;

static void layers(Emu* e, int only) {
	for (int i = 0; i < NLAYERS; ++i) e->core->enableVideoLayer(e->core, i, only == -2 || i == only);
}

/* several layers at once (bit i: BG i, bit 4: sprites) */
static void layer_set(Emu* e, unsigned mask) {
	for (int i = 0; i < NLAYERS; ++i) e->core->enableVideoLayer(e->core, i, (mask >> i) & 1);
}

bool logo_extract(const char* romPath, const char* gameCode, const char* outPng) {
	const MapProfile* p = profile_find(gameCode);
	if (!p || (!p->logo.wait && !p->logo.steps[0].frames)) return false;
	Emu e;
	if (!emu_open(&e, romPath, NULL, false)) return false;

	for (int i = 0; i < 24 && p->logo.steps[i].frames && !cancelAll; ++i) {
		for (int f = 0; f < p->logo.steps[i].frames && !cancelAll; ++f) emu_frame(&e, p->logo.steps[i].keys);
	}
	for (int f = 0; f < p->logo.wait && !cancelAll; ++f) emu_frame(&e, 0);
	if (p->logo.pressStart) {
		for (int f = 0; f < 4; ++f) emu_frame(&e, K_START);
		for (int f = 0; f < p->logo.wait2 && !cancelAll; ++f) emu_frame(&e, 0);
	}
	if (cancelAll) {
		emu_close(&e);
		return false;
	}

	size_t sz = emu_state_size(&e);
	void* st = malloc(sz);
	uint32_t* back = malloc(GBA_W * GBA_H * 4);
	uint32_t* fg = malloc(GBA_W * GBA_H * 4);
	uint8_t* opaque = calloc(GBA_W * GBA_H, 1);
	emu_state_save(&e, st);
	/* Layer switches only take effect when the display registers are
	 * written; loading the state after setting them rewrites the registers
	 * (some titles, e.g. Circle of the Moon JP, never rewrite them). */
	layers(&e, -1); /* backdrop only */
	emu_state_load(&e, st);
	emu_frame(&e, 0);
	memcpy(back, e.fb, GBA_W * GBA_H * 4);
	if (p->logo.mask) layers(&e, -2); /* a mask cuts the logo out of the whole picture */
	else if (p->logo.layers) layer_set(&e, p->logo.layers); /* a logo over several layers (FFTA: its shadow) */
	else layers(&e, p->logo.layer);
	emu_state_load(&e, st);
	emu_frame(&e, 0);
	memcpy(fg, e.fb, GBA_W * GBA_H * 4);
	if (!p->logo.mask && p->logo.layers && p->logo.objMaxY > 0 && p->logo.objMaxY < GBA_H) {
		/* below objMaxY, the BG layers without the sprites (Summon Night:
		 * its PRESS START BUTTON is a sprite across the sword's hilt) */
		layer_set(&e, p->logo.layers & ~0x10u);
		emu_state_load(&e, st);
		emu_frame(&e, 0);
		memcpy(fg + p->logo.objMaxY * GBA_W, e.fb + p->logo.objMaxY * GBA_W, (size_t) (GBA_H - p->logo.objMaxY) * GBA_W * 4);
	}
	layers(&e, -2);
	emu_close(&e);
	free(st);

	if (getenv("GBADS_LOGO_DEBUG")) {
		char dbg[1200];
		snprintf(dbg, sizeof(dbg), "%s.fg.png", outPng);
		stbi_write_png(dbg, GBA_W, GBA_H, 4, fg, GBA_W * 4);
		snprintf(dbg, sizeof(dbg), "%s.back.png", outPng);
		stbi_write_png(dbg, GBA_W, GBA_H, 4, back, GBA_W * 4);
	}
	uint8_t* inMask = NULL;
	if (p->logo.mask) {
		const LogoMask* m = p->logo.mask;
		inMask = calloc(GBA_W * GBA_H, 1);
		int pos = 0;
		for (int r = 0; r < m->nRuns; ++r) {
			for (int k = 0; k < m->runs[r] && pos < m->w * m->h; ++k, ++pos) {
				int x = m->x + pos % m->w, y = m->y + pos / m->w;
				if ((r & 1) && x < GBA_W && y < GBA_H) inMask[y * GBA_W + x] = 1;
			}
		}
	}
	/* opaque mask and bounding box above maxY */
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1, count = 0;
	int maxY = p->logo.maxY ? p->logo.maxY : GBA_H;
	for (int y = 0; y < maxY; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = fg[y * GBA_W + x];
			bool clear = inMask ? !inMask[y * GBA_W + x] : c == back[y * GBA_W + x];
			if (p->logo.blackIsClear && (c & 0xFF) < 24 && ((c >> 8) & 0xFF) < 24 && ((c >> 16) & 0xFF) < 24) clear = true;
			if (clear) continue;
			opaque[y * GBA_W + x] = 1;
			++count;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	}
	bool ok = false;
	if (count >= 400) {
		int w = x1 - x0 + 1, h = y1 - y0 + 1;
		uint8_t* rgba = malloc((size_t) w * h * 4);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int i = (y0 + y) * GBA_W + x0 + x;
				uint32_t c = fg[i];
				uint8_t* o = &rgba[(y * w + x) * 4];
				o[0] = c & 0xFF;
				o[1] = (c >> 8) & 0xFF;
				o[2] = (c >> 16) & 0xFF;
				o[3] = opaque[i] ? 255 : 0;
			}
		}
		char tmp[1100];
		snprintf(tmp, sizeof(tmp), "%s.tmp", outPng);
		ok = stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, outPng) == 0;
		free(rgba);
		fprintf(stderr, "logo: %s %dx%d -> %s\n", gameCode, w, h, ok ? outPng : "write failed");
	} else {
		fprintf(stderr, "logo: %s not found on the title screen (%d px)\n", gameCode, count);
	}
	free(back);
	free(fg);
	free(opaque);
	free(inMask);
	return ok;
}

/* Renders BG `layer` alone after playing `steps` from power-on; returns the
 * frame and a mask of non-transparent pixels. */
static bool render_layer(const char* romPath, const MacroStep* steps, int nSteps, int layer, uint32_t* out,
                         uint8_t* opaque) {
	Emu e;
	if (!emu_open(&e, romPath, NULL, false)) return false;
	for (int s = 0; s < nSteps && steps[s].frames; ++s) {
		for (int f = 0; f < steps[s].frames && !cancelAll; ++f) emu_frame(&e, steps[s].keys);
	}
	if (cancelAll) {
		emu_close(&e);
		return false;
	}
	size_t sz = emu_state_size(&e);
	void* st = malloc(sz);
	uint32_t* back = malloc(GBA_W * GBA_H * 4);
	emu_state_save(&e, st);
	layers(&e, -1); /* (state reloaded after switching layers: see logo_extract) */
	emu_state_load(&e, st);
	emu_frame(&e, 0);
	memcpy(back, e.fb, GBA_W * GBA_H * 4);
	layers(&e, layer);
	emu_state_load(&e, st);
	emu_frame(&e, 0);
	memcpy(out, e.fb, GBA_W * GBA_H * 4);
	layers(&e, -2);
	emu_close(&e);
	for (int i = 0; i < GBA_W * GBA_H; ++i) opaque[i] = out[i] != back[i];
	free(st);
	free(back);
	return true;
}

/* Index of the most common w x h block among n candidates starting at
 * (x + i*dx, y + i*dy). */
static int mode_block(const uint32_t* px, const uint8_t* op, int x, int y, int dx, int dy, int n, int w, int h) {
	uint64_t hashes[256];
	if (n > 256) n = 256;
	for (int i = 0; i < n; ++i) {
		uint64_t hsh = 1469598103934665603ull;
		for (int yy = 0; yy < h; ++yy) {
			for (int xx = 0; xx < w; ++xx) {
				int k = (y + i * dy + yy) * GBA_W + x + i * dx + xx;
				hsh = (hsh ^ (op[k] ? px[k] : 0xFFFFFFFFu)) * 1099511628211ull;
			}
		}
		hashes[i] = hsh;
	}
	int best = 0, bestN = 0;
	for (int i = 0; i < n; ++i) {
		int c = 0;
		for (int j = 0; j < n; ++j) c += hashes[j] == hashes[i];
		if (c > bestN) bestN = c, best = i;
	}
	return best;
}

static void blit(uint8_t* atlas, int aw, int ax, int ay, const uint32_t* px, const uint8_t* op, int sx, int sy, int w,
                 int h) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int k = (sy + y) * GBA_W + sx + x;
			uint8_t* o = &atlas[((ay + y) * aw + ax + x) * 4];
			uint32_t c = px[k];
			o[0] = c & 0xFF;
			o[1] = (c >> 8) & 0xFF;
			o[2] = (c >> 16) & 0xFF;
			o[3] = op[k] ? 255 : 0;
		}
	}
}

bool frame_extract(const char* romPath, const char* gameCode, const char* outPng) {
	const MapProfile* p = profile_find(gameCode);
	if (!p || !p->frame.t) return false;
	int t = p->frame.t, pw = p->frame.p ? p->frame.p : t;
	int x0 = p->frame.x0, y0 = p->frame.y0, x1 = p->frame.x1, y1 = p->frame.y1;
	uint32_t* px = malloc(GBA_W * GBA_H * 4);
	uint8_t* op = malloc(GBA_W * GBA_H);
	bool ok = render_layer(romPath, p->frame.steps, 24, p->frame.layer, px, op);
	if (ok && p->frame.minLuma) {
		for (int i = 0; i < GBA_W * GBA_H; ++i) {
			uint32_t c = px[i];
			int luma = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) / 4;
			if (luma < p->frame.minLuma) op[i] = 0;
		}
	}
	if (ok) {
		/* sanity: the corners must be there */
		int n = 0;
		for (int y = 0; y < t; ++y)
			for (int x = 0; x < t; ++x) n += op[(y0 + y) * GBA_W + x0 + x] + op[(y1 + y) * GBA_W + x1 + x];
		ok = n >= t; /* at least a few corner pixels */
	}
	if (ok) {
		int aw = 2 * t + pw;
		uint8_t* atlas = calloc((size_t) aw * aw, 4);
		int nh = (x1 - x0 - t) / pw, nv = (y1 - y0 - t) / pw;
		int top = mode_block(px, op, x0 + t, y0, pw, 0, nh, pw, t);
		int bot = mode_block(px, op, x0 + t, y1, pw, 0, nh, pw, t);
		int lft = mode_block(px, op, x0, y0 + t, 0, pw, nv, t, pw);
		int rgt = mode_block(px, op, x1, y0 + t, 0, pw, nv, t, pw);
		blit(atlas, aw, 0, 0, px, op, x0, y0, t, t);
		blit(atlas, aw, t + pw, 0, px, op, x1, y0, t, t);
		blit(atlas, aw, 0, t + pw, px, op, x0, y1, t, t);
		blit(atlas, aw, t + pw, t + pw, px, op, x1, y1, t, t);
		blit(atlas, aw, t, 0, px, op, x0 + t + top * pw, y0, pw, t);
		blit(atlas, aw, t, t + pw, px, op, x0 + t + bot * pw, y1, pw, t);
		blit(atlas, aw, 0, t, px, op, x0, y0 + t + lft * pw, t, pw);
		blit(atlas, aw, t + pw, t, px, op, x1, y0 + t + rgt * pw, t, pw);
		char tmp[1100];
		snprintf(tmp, sizeof(tmp), "%s.tmp", outPng);
		ok = stbi_write_png(tmp, aw, aw, 4, atlas, aw * 4) && rename(tmp, outPng) == 0;
		free(atlas);
	}
	fprintf(stderr, "frame: %s %s\n", gameCode, ok ? outPng : "not found");
	free(px);
	free(op);
	return ok;
}

/* ------------------------------------------------------------ queue */

typedef struct Job {
	char rom[1024], code[5], out[1024];
	bool frame;
	struct Job* next;
} Job;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;
static Job* queue;
static bool running, stopping;
static pthread_t thread;
static volatile uint32_t generation;
static int pending, finished; /* jobs queued or running / done since the queue last went idle */

static bool exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static void do_job(Job* j) {
	if (!exists(j->out)) {
		if (j->frame) frame_extract(j->rom, j->code, j->out);
		else logo_extract(j->rom, j->code, j->out);
	}
	free(j);
	__atomic_add_fetch(&generation, 1, __ATOMIC_SEQ_CST);
}

static void* worker(void* arg) {
	(void) arg;
	/* background work: let the game (and its audio) have the CPU first */
#ifndef __wasi__
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 19);
#endif
#ifdef __ANDROID__
	android_pin_worker_thread("logo extraction");
#endif
	pthread_mutex_lock(&qlock);
	while (!stopping) {
		Job* j = queue;
		if (!j) {
			pthread_cond_wait(&qcond, &qlock);
			continue;
		}
		queue = j->next;
		pthread_mutex_unlock(&qlock);
		do_job(j);
		pthread_mutex_lock(&qlock);
		--pending;
		++finished;
	}
	pthread_mutex_unlock(&qlock);
	return NULL;
}

static void enqueue(const char* romPath, const char* gameCode, const char* outPng, bool frame) {
	pthread_mutex_lock(&qlock);
	for (Job* j = queue; j; j = j->next) {
		if (!strcmp(j->out, outPng)) {
			pthread_mutex_unlock(&qlock);
			return;
		}
	}
	Job* j = calloc(1, sizeof(*j));
	snprintf(j->rom, sizeof(j->rom), "%s", romPath);
	snprintf(j->code, sizeof(j->code), "%.4s", gameCode);
	snprintf(j->out, sizeof(j->out), "%s", outPng);
	j->frame = frame;
	if (!pending) finished = 0;
	++pending;
	Job** tail = &queue;
	while (*tail) tail = &(*tail)->next;
	*tail = j;
	/* (GBADS_SYNC=1, regression tests: inline too, like the web build) */
	if (!running && !(getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC")))) running = pthread_create(&thread, NULL, worker, NULL) == 0;
	if (!running) {
		/* no threads (the web build), or GBADS_SYNC: the work now */
		while (queue) {
			Job* q = queue;
			queue = q->next;
			do_job(q);
			--pending;
			++finished;
		}
		pthread_mutex_unlock(&qlock);
		return;
	}
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

void logo_request(const char* romPath, const char* gameCode, const char* outPng) {
	if (exists(outPng)) return;
	const MapProfile* p = profile_find(gameCode);
	if (!p || (!p->logo.wait && !p->logo.steps[0].frames)) return;
	enqueue(romPath, gameCode, outPng, false);
}

void frame_request(const char* romPath, const char* gameCode, const char* outPng) {
	if (exists(outPng)) return;
	const MapProfile* p = profile_find(gameCode);
	if (!p || !p->frame.t) return;
	enqueue(romPath, gameCode, outPng, true);
}

void logo_progress(int* done, int* total) {
	pthread_mutex_lock(&qlock);
	*done = finished;
	*total = finished + pending;
	pthread_mutex_unlock(&qlock);
}

uint32_t logo_generation(void) {
	return __atomic_load_n(&generation, __ATOMIC_SEQ_CST);
}

void logo_shutdown(void) {
	cancelAll = true;
	pthread_mutex_lock(&qlock);
	stopping = true;
	while (queue) {
		Job* j = queue;
		queue = j->next;
		free(j);
	}
	pending = 0;
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
	if (running) pthread_join(thread, NULL);
	running = false;
}

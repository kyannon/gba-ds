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
/* The queue's job running is called off (logo_yield: a game is starting
 * whose own logo is ready): only the worker's extraction stops, not one a
 * game's start runs itself (ui_game_prepare). */
static volatile bool cancelJob;
#ifdef __wasi__
static bool onWorker; /* (no threads there) */
#else
static __thread bool onWorker;
#endif
static bool stopped(void) {
	return cancelAll || (onWorker && cancelJob);
}

static void layers(Emu* e, int only) {
	for (int i = 0; i < NLAYERS; ++i) e->core->enableVideoLayer(e->core, i, only == -2 || i == only);
}

/* several layers at once (bit i: BG i, bit 4: sprites) */
static void layer_set(Emu* e, unsigned mask) {
	for (int i = 0; i < NLAYERS; ++i) e->core->enableVideoLayer(e->core, i, (mask >> i) & 1);
}

/* the profile's sprites to leave out, for the render that follows */
static void logo_hide(Emu* e, const LogoSpec* L) {
	int n = 0;
	while (n < 2 && (L->hide[n].x1 > L->hide[n].x0 || L->hide[n].y1 > L->hide[n].y0)) ++n;
	emu_hide_sprites(e, n ? L->hide : NULL, n);
}

#ifndef GBADS_VERSION
#define GBADS_VERSION "dev"
#endif
static void failed_path(const char* outPng, char* path, size_t n) {
	snprintf(path, n, "%s.none", outPng);
}
bool logo_failed(const char* outPng) {
	char path[1100], line[64] = "";
	failed_path(outPng, path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f) return false;
	if (!fgets(line, sizeof(line), f)) line[0] = 0;
	fclose(f);
	line[strcspn(line, "\r\n")] = 0;
	return !strcmp(line, GBADS_VERSION);
}
static void failed_mark(const char* outPng, bool failed) {
	char path[1100];
	failed_path(outPng, path, sizeof(path));
	if (!failed) {
		unlink(path);
		return;
	}
	FILE* f = fopen(path, "w");
	if (!f) return;
	fprintf(f, "%s\n", GBADS_VERSION);
	fclose(f);
}

/* want: the ROM's CRC32 as played must be this known mod's, or (0) not a known
 * mod's (GEN-20); NULL: any ROM */
static bool extract(const char* romPath, const char* gameCode, const char* outPng, const uint32_t* want) {
	const MapProfile* p = profile_find(gameCode);
	if (!p) return false;
	Emu e;
	/* a game whose logos are per ROM file: the file's own logo, whatever
	 * patch is beside it or switched on (a patched game shows its base's) */
	if (p->logoPerRom) emu_open_unpatched(true);
	bool opened = emu_open(&e, romPath, NULL, false);
	if (p->logoPerRom) emu_open_unpatched(false);
	if (!opened) return false;
	/* this ROM file's recipe (a romhack's own, by its CRC32), and for a game
	 * whose logos are per ROM file, never a patched copy's (a patched game
	 * is the base game: it uses the base's logo, from the unpatched file) */
	const LogoSpec* L = profile_logo_spec(p, e.fileCrc);
	if ((!L->wait && !L->steps[0].frames) || (p->logoPerRom && e.romCrc != e.fileCrc)) {
		emu_close(&e);
		return false;
	}
	if (want && !p->logoPerRom && (*want ? e.romCrc != *want : profile_known_mod(gameCode, e.romCrc))) {
		fprintf(stderr, "logo: %s is CRC32 %08x as played, not the ROM %s is for\n", romPath, e.romCrc, outPng);
		emu_close(&e);
		return false;
	}
	/* the profile's patches for this copy only, all or none (another
	 * revision of the ROM is left alone) */
	bool patchOk = true;
	for (int k = 0; k < 2 && L->patch[k].addr; ++k) {
		const RomPatch* r = &L->patch[k];
		for (int i = 0; i < r->len; ++i) patchOk &= emu_read8(&e, r->addr + i) == (r->orig ? r->orig[i] : 0xFF);
	}
	for (int k = 0; k < 2 && patchOk && L->patch[k].addr; ++k) {
		const RomPatch* r = &L->patch[k];
		for (int i = 0; i < r->len; ++i) e.core->rawWrite8(e.core, r->addr + i, -1, r->bytes[i]);
	}

	for (int i = 0; i < 24 && L->steps[i].frames && !stopped(); ++i) {
		for (int f = 0; f < L->steps[i].frames && !stopped(); ++f) emu_frame(&e, L->steps[i].keys);
	}
	for (int f = 0; f < L->wait && !stopped(); ++f) emu_frame(&e, 0);
	if (L->pressStart) {
		for (int f = 0; f < 4; ++f) emu_frame(&e, K_START);
		for (int f = 0; f < L->wait2 && !stopped(); ++f) emu_frame(&e, 0);
	}
	if (stopped()) {
		emu_close(&e);
		return false;
	}

	size_t sz = emu_state_size(&e);
	void* st = malloc(sz);
	uint32_t* back = malloc(GBA_W * GBA_H * 4);
	uint32_t* fg = malloc(GBA_W * GBA_H * 4);
	uint8_t* opaque = calloc(GBA_W * GBA_H, 1);
	if (!st || !back || !fg || !opaque) {
		emu_close(&e);
		free(st), free(back), free(fg), free(opaque);
		return false;
	}
	emu_state_save(&e, st);
	/* Layer switches only take effect when the display registers are
	 * written; loading the state after setting them rewrites the registers
	 * (some titles, e.g. Circle of the Moon JP, never rewrite them). */
	layers(&e, -1); /* backdrop only */
	emu_state_load(&e, st);
	emu_frame(&e, 0);
	memcpy(back, e.fb, GBA_W * GBA_H * 4);
	if (L->mask) layers(&e, -2); /* a mask cuts the logo out of the whole picture */
	else if (L->layers) layer_set(&e, L->layers); /* a logo over several layers (FFTA: its shadow) */
	else layers(&e, L->layer);
	emu_state_load(&e, st);
	logo_hide(&e, L);
	emu_frame(&e, 0);
	memcpy(fg, e.fb, GBA_W * GBA_H * 4);
	if (!L->mask && L->layers && L->objMaxY > 0 && L->objMaxY < GBA_H) {
		/* below objMaxY, the BG layers without the sprites (Summon Night:
		 * its PRESS START BUTTON is a sprite across the sword's hilt) */
		layer_set(&e, L->layers & ~0x10u);
		emu_state_load(&e, st);
		emu_frame(&e, 0);
		memcpy(fg + L->objMaxY * GBA_W, e.fb + L->objMaxY * GBA_W, (size_t) (GBA_H - L->objMaxY) * GBA_W * 4);
	}
	if (!L->mask && L->layers && L->lowFrom > 0 && L->lowFrom < GBA_H) {
		/* from lowFrom down, only lowLayers (Sonic Pinball Party: its
		 * badge's lowest rows beside the copyright line's) */
		layer_set(&e, L->lowLayers);
		emu_state_load(&e, st);
		logo_hide(&e, L);
		emu_frame(&e, 0);
		memcpy(fg + L->lowFrom * GBA_W, e.fb + L->lowFrom * GBA_W, (size_t) (GBA_H - L->lowFrom) * GBA_W * 4);
	}
	emu_hide_sprites(&e, NULL, 0);
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
	if (L->mask) {
		const LogoMask* m = L->mask;
		inMask = calloc(GBA_W * GBA_H, 1);
		int pos = 0;
		for (int r = 0; r < m->nRuns; ++r) {
			for (int k = 0; k < m->runs[r] && pos < m->w * m->h; ++k, ++pos) {
				int x = m->x + pos % m->w, y = m->y + pos / m->w;
				if ((r & 1) && x < GBA_W && y < GBA_H) inMask[y * GBA_W + x] = 1;
			}
		}
		for (int i = 0; i < m->nPatch; ++i) {
			int at = m->patchAt[i];
			if (at >= GBA_W * GBA_H) continue;
			uint32_t c = m->patchRgb[i];
			inMask[at] = 1;
			fg[at] = 0xFF000000u | (c & 0xFF) << 16 | (c & 0xFF00) | (c >> 16 & 0xFF);
		}
	}
	/* opaque mask and bounding box above maxY */
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1, count = 0;
	int maxY = L->maxY ? L->maxY : GBA_H;
	int maxX = L->maxX ? L->maxX : GBA_W;
	for (int y = L->minY; y < maxY; ++y) {
		for (int x = L->minX; x < maxX; ++x) {
			uint32_t c = fg[y * GBA_W + x];
			bool clear = inMask ? !inMask[y * GBA_W + x] : c == back[y * GBA_W + x];
			if (L->blackIsClear && (c & 0xFF) < 24 && ((c >> 8) & 0xFF) < 24 && ((c >> 16) & 0xFF) < 24) clear = true;
			if (!clear) opaque[y * GBA_W + x] = 1;
		}
	}
	if (L->lightEdgeClear) {
		/* the layer's light background (Pokemon Pinball's white, striped):
		 * light grey to white pixels reached from the picture's edges
		 * through others like them, not the logo's own white inside it */
		int* stack = malloc(sizeof(int) * GBA_W * GBA_H);
		uint8_t* seen = calloc(GBA_W * GBA_H, 1);
		int n = 0;
		for (int y = 0; stack && seen && y < maxY; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				if (x == 0 || y == 0 || x == GBA_W - 1 || y == maxY - 1) stack[n++] = y * GBA_W + x, seen[y * GBA_W + x] = 1;
			}
		}
		while (n) {
			int i = stack[--n], x = i % GBA_W, y = i / GBA_W;
			uint32_t c = fg[i];
			int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			int hi = r > g ? (r > b ? r : b) : (g > b ? g : b), lo = r < g ? (r < b ? r : b) : (g < b ? g : b);
			if (opaque[i] && !(lo >= 190 && hi - lo <= 40)) continue;
			opaque[i] = 0;
			const int nb[4] = { x > 0 ? i - 1 : -1, x < GBA_W - 1 ? i + 1 : -1, y > 0 ? i - GBA_W : -1, y < maxY - 1 ? i + GBA_W : -1 };
			for (int k = 0; k < 4; ++k) {
				if (nb[k] >= 0 && !seen[nb[k]]) seen[nb[k]] = 1, stack[n++] = nb[k];
			}
		}
		free(stack);
		free(seen);
		/* and its white outline back: two pixels around what's left */
		for (int pass = 0; pass < 2; ++pass) {
			memcpy(back, opaque, GBA_W * GBA_H); /* (back's done with: a scratch copy) */
			const uint8_t* was = (const uint8_t*) back;
			for (int y = 0; y < maxY; ++y) {
				for (int x = 0; x < GBA_W; ++x) {
					int i = y * GBA_W + x;
					if (was[i]) continue;
					if ((x > 0 && was[i - 1]) || (x < GBA_W - 1 && was[i + 1]) || (y > 0 && was[i - GBA_W]) || (y < maxY - 1 && was[i + GBA_W])) opaque[i] = 1;
				}
			}
		}
	}
	if (L->edgeClear[0]) {
		int* stack = malloc(sizeof(int) * GBA_W * GBA_H);
		uint8_t* seen = calloc(GBA_W * GBA_H, 1);
		int n = 0;
		/* (from outside the logo, and from the picture's edges: a layer
		 * that fills the screen with the background, Battle Network
		 * Rockman EXE's blue) */
		for (int i = 0; stack && seen && i < GBA_W * maxY; ++i) {
			int x = i % GBA_W, y = i / GBA_W;
			if (!opaque[i] || x == 0 || x == GBA_W - 1 || y == 0 || y == maxY - 1) stack[n++] = i, seen[i] = 1;
		}
		while (stack && seen && n) {
			int i = stack[--n], x = i % GBA_W, y = i / GBA_W;
			if (opaque[i]) {
				uint32_t c = fg[i];
				int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
				bool clearIt = false;
				for (int k = 0; k < 8 && L->edgeClear[k] && !clearIt; ++k) {
					uint32_t e = L->edgeClear[k];
					clearIt = abs(r - (int) (e >> 16 & 0xFF)) + abs(g - (int) (e >> 8 & 0xFF)) + abs(b - (int) (e & 0xFF)) < 20;
				}
				if (!clearIt) continue;
				opaque[i] = 0;
			}
			const int nb[4] = { x > 0 ? i - 1 : -1, x < GBA_W - 1 ? i + 1 : -1, y > 0 ? i - GBA_W : -1, y < maxY - 1 ? i + GBA_W : -1 };
			for (int k = 0; k < 4; ++k) {
				if (nb[k] >= 0 && !seen[nb[k]]) seen[nb[k]] = 1, stack[n++] = nb[k];
			}
		}
		free(stack);
		free(seen);
		/* (and what's left alone: at most one opaque neighbour) */
		memcpy(back, opaque, GBA_W * GBA_H); /* (back's done with: a scratch copy) */
		const uint8_t* was = (const uint8_t*) back;
		for (int y = 0; y < maxY; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				int i = y * GBA_W + x;
				if (!was[i]) continue;
				int nb = (x > 0 && was[i - 1]) + (x < GBA_W - 1 && was[i + 1]) + (y > 0 && was[i - GBA_W]) + (y < maxY - 1 && was[i + GBA_W]);
				if (nb <= 1) opaque[i] = 0;
			}
		}
		/* (and specks: pieces under 16 pixels, 8-connected) */
		int* q = malloc(sizeof(int) * GBA_W * GBA_H);
		uint8_t* done = calloc(GBA_W * GBA_H, 1);
		for (int s0 = 0; q && done && s0 < GBA_W * maxY; ++s0) {
			if (!opaque[s0] || done[s0]) continue;
			int head = 0, tail = 0;
			q[tail++] = s0;
			done[s0] = 1;
			while (head < tail) {
				int i = q[head++], x = i % GBA_W, y = i / GBA_W;
				for (int dy = -1; dy <= 1; ++dy) {
					for (int dx = -1; dx <= 1; ++dx) {
						int nx = x + dx, ny = y + dy, j = ny * GBA_W + nx;
						if (nx < 0 || nx >= GBA_W || ny < 0 || ny >= maxY || !opaque[j] || done[j]) continue;
						done[j] = 1;
						q[tail++] = j;
					}
				}
			}
			if (tail < 16) {
				for (int k = 0; k < tail; ++k) opaque[q[k]] = 0;
			}
		}
		free(q);
		free(done);
	}
	if (L->shadow[0] && L->shadowReach > 0) {
		/* (logo.shadow: row by row, each shadow pixel kept or not) */
		int reach = L->shadowReach;
		uint8_t* shade = calloc(GBA_W * GBA_H, 1); /* 1 shadow, 2 shadow kept */
		for (int i = 0; shade && i < GBA_W * maxY; ++i) {
			if (!opaque[i]) continue;
			uint32_t c = fg[i];
			int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			for (int k = 0; k < 2 && L->shadow[k]; ++k) {
				uint32_t e = L->shadow[k];
				if (abs(r - (int) (e >> 16 & 0xFF)) + abs(g - (int) (e >> 8 & 0xFF)) + abs(b - (int) (e & 0xFF)) < 12) shade[i] = 1;
			}
		}
		for (int y = 0; shade && y < maxY; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				int i = y * GBA_W + x;
				if (shade[i] != 1) continue;
				bool keep = false;
				for (int dy = 1; dy <= reach && !keep; ++dy) {
					for (int dx = 0; dx <= reach && !keep; ++dx) {
						int X = x - dx, Y = y - dy, j = Y * GBA_W + X;
						if (X >= 0 && Y >= 0 && opaque[j] && shade[j] != 1) keep = true;
					}
				}
				if (keep) shade[i] = 2;
				else opaque[i] = 0;
			}
		}
		free(shade);
	}
	for (int y = 0; y < maxY; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			if (!opaque[y * GBA_W + x]) continue;
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
		for (int y = 0; rgba && y < h; ++y) {
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
		ok = rgba && stbi_write_png(tmp, w, h, 4, rgba, w * 4) && rename(tmp, outPng) == 0;
		free(rgba);
		fprintf(stderr, "logo: %s %dx%d -> %s\n", gameCode, w, h, ok ? outPng : "write failed");
		if (ok) failed_mark(outPng, false);
	} else {
		fprintf(stderr, "logo: %s not found on the title screen (%d px)\n", gameCode, count);
		failed_mark(outPng, true); /* (not again at every start: logo_failed) */
	}
	free(back);
	free(fg);
	free(opaque);
	free(inMask);
	return ok;
}

bool logo_extract(const char* romPath, const char* gameCode, const char* outPng) {
	return extract(romPath, gameCode, outPng, NULL);
}

/* ------------------------------------------------------------ queue */

typedef struct Job {
	char rom[1024], code[5], out[1024];
	uint32_t crc; /* the known mod it's for, or 0 (GEN-20) */
	bool urgent; /* runs while the queue's paused: the game being started or played */
	struct Job* next;
} Job;

static pthread_mutex_t qlock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qcond = PTHREAD_COND_INITIALIZER;
static Job* queue;
static bool running, stopping;
static pthread_t thread;
static volatile uint32_t generation;
static int pending, finished; /* jobs queued or running / done since the queue last went idle */
static bool paused; /* a game's playing: only urgent jobs run */
static char current[5]; /* the code of the job running, "" if none */

static bool exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

/* False: called off (logo_yield), the job kept for later. */
static bool do_job(Job* j) {
	if (!exists(j->out)) {
		extract(j->rom, j->code, j->out, &j->crc);
		if (onWorker && cancelJob && !exists(j->out)) return false;
	}
	free(j);
	__atomic_add_fetch(&generation, 1, __ATOMIC_SEQ_CST);
	return true;
}

/* (qlock held) the first job, or while paused the first urgent one; NULL if none */
static Job* take_job(void) {
	Job** at = &queue;
	while (*at && paused && !(*at)->urgent) at = &(*at)->next;
	Job* j = *at;
	if (j) *at = j->next;
	return j;
}

/* (qlock held) a job taken from the queue, done with the lock let go meanwhile.
 * One called off for a game starting (only on the worker: logo_request) goes
 * back in the queue, after the urgent ones, to be made once the list is back. */
static void run_job(Job* j) {
	snprintf(current, sizeof(current), "%s", j->code);
	cancelJob = false;
	pthread_mutex_unlock(&qlock);
	bool done = do_job(j);
	pthread_mutex_lock(&qlock);
	current[0] = 0;
	cancelJob = false;
	if (done) {
		--pending;
		++finished;
	} else {
		fprintf(stderr, "logo: %s put off (a game is starting)\n", j->code);
		j->urgent = false;
		Job** tail = &queue;
		while (*tail && (*tail)->urgent) tail = &(*tail)->next;
		j->next = *tail;
		*tail = j;
	}
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
	onWorker = true;
	pthread_mutex_lock(&qlock);
	while (!stopping) {
		Job* j = take_job();
		if (!j) {
			pthread_cond_wait(&qcond, &qlock);
			continue;
		}
		run_job(j);
		pthread_cond_broadcast(&qcond);
	}
	pthread_mutex_unlock(&qlock);
	return NULL;
}

/* Outputs already tried this run. A job that couldn't make its file (the
 * logo not found on the title screen, or the folder not writable) isn't
 * queued again until the next start: each finished job makes the game list
 * reload its logos, which asked for the missing one again, so one failure
 * looped for ever (and "Preparing games" counted up without end). */
static char (*tried)[2100];
static int nTried, capTried;

static bool tried_before(const char* out) {
	for (int i = 0; i < nTried; ++i) {
		if (!strcmp(tried[i], out)) return true;
	}
	if (nTried == capTried) {
		int cap = capTried ? capTried * 2 : 32;
		char (*t)[2100] = realloc(tried, sizeof(*tried) * cap);
		if (!t) return false;
		tried = t;
		capTried = cap;
	}
	snprintf(tried[nTried++], sizeof(*tried), "%s", out);
	return false;
}

static void enqueue(const char* romPath, const char* gameCode, uint32_t crc, const char* outPng) {
	pthread_mutex_lock(&qlock);
	for (Job* j = queue; j; j = j->next) {
		if (!strcmp(j->out, outPng) && !strcmp(j->rom, romPath)) {
			pthread_mutex_unlock(&qlock);
			return;
		}
	}
	/* (per ROM: one refused for being another version leaves the file to the
	 * others, GEN-20; and apart while a game's played, as its own patches
	 * apply there) */
	char key[2100];
	snprintf(key, sizeof(key), "%s%s|%s", paused ? "game:" : "", outPng, romPath);
	if (logo_failed(outPng) || tried_before(key)) {
		pthread_mutex_unlock(&qlock);
		return;
	}
	Job* j = calloc(1, sizeof(*j));
	if (!j) {
		pthread_mutex_unlock(&qlock);
		return;
	}
	snprintf(j->rom, sizeof(j->rom), "%s", romPath);
	snprintf(j->code, sizeof(j->code), "%.4s", gameCode);
	snprintf(j->out, sizeof(j->out), "%s", outPng);
	j->crc = crc;
	j->urgent = paused; /* (asked for by the game being played: its own logo) */
	if (!pending) finished = 0;
	++pending;
	Job** tail = &queue;
	if (j->urgent) {
		while (*tail && (*tail)->urgent) tail = &(*tail)->next; /* (after the other urgent ones) */
		j->next = *tail;
	} else {
		while (*tail) tail = &(*tail)->next;
	}
	*tail = j;
	/* (emu_sync_work: the regression tests, a core's choice: inline too, like the web build) */
	if (!running && !emu_sync_work()) running = pthread_create(&thread, NULL, worker, NULL) == 0;
	if (!running) {
		/* no threads (the web build), or emu_sync_work: the work now */
#ifdef __wasi__
		/* (the web build: only a game's own logo, asked for while it's
		 * played; the rest one a frame, logo_step, so the game list shows
		 * at once and the logos fill in, as a thread's would) */
		if (!j->urgent) {
			pthread_mutex_unlock(&qlock);
			return;
		}
		for (Job** at = &queue; *at;) {
			Job* q = *at;
			if (!q->urgent) {
				at = &q->next;
				continue;
			}
			*at = q->next;
			do_job(q);
			--pending;
			++finished;
		}
#else
		while (queue) {
			Job* q = queue;
			queue = q->next;
			do_job(q);
			--pending;
			++finished;
		}
#endif
		pthread_mutex_unlock(&qlock);
		return;
	}
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
}

/* The web build (no threads): the next job, as the worker would take it (while
 * a game's played, only urgent ones), once a frame: the game list and the
 * game call it before each frame they draw. Elsewhere a thread does them. */
#ifdef __wasi__
void logo_step(void) {
	pthread_mutex_lock(&qlock);
	Job* j = take_job();
	if (j) run_job(j);
	pthread_mutex_unlock(&qlock);
}
#endif

void logo_request(const char* romPath, const char* gameCode, uint32_t crc, const char* outPng) {
	if (exists(outPng)) return;
	const MapProfile* p = profile_find(gameCode);
	if (!p || (!p->logo.wait && !p->logo.steps[0].frames)) return;
	enqueue(romPath, gameCode, crc, outPng);
}

void logo_front(const char* gameCode, bool urgent) {
	pthread_mutex_lock(&qlock);
	Job *mine = NULL, **mineTail = &mine;
	for (Job** at = &queue; *at;) {
		Job* j = *at;
		if (!strncmp(j->code, gameCode, 4)) {
			*at = j->next;
			j->next = NULL;
			j->urgent |= urgent;
			*mineTail = j, mineTail = &j->next;
		} else {
			at = &j->next;
		}
	}
	if (mine) {
		*mineTail = queue;
		queue = mine;
		pthread_cond_signal(&qcond);
	}
	pthread_mutex_unlock(&qlock);
}

bool logo_busy(const char* gameCode) {
	pthread_mutex_lock(&qlock);
	bool busy = (gameCode && !strncmp(current, gameCode, 4)) || (!gameCode && current[0]);
	for (Job* j = queue; j && gameCode && !busy; j = j->next) busy = !strncmp(j->code, gameCode, 4);
	pthread_mutex_unlock(&qlock);
	return busy;
}

void logo_yield(const char* gameCode) {
	pthread_mutex_lock(&qlock);
	if (current[0] && strncmp(current, gameCode, 4)) cancelJob = true;
	pthread_mutex_unlock(&qlock);
}
void logo_pause(bool on) {
	pthread_mutex_lock(&qlock);
	paused = on;
	pthread_cond_signal(&qcond);
	pthread_mutex_unlock(&qlock);
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

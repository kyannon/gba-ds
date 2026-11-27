#include "climb.h"
#include "emu.h"
#ifdef __ANDROID__
#include "android.h"
#endif

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* The copy runs from the game's state before the frame before, its camera a
 * screen higher, so the sprites the game builds one frame ahead are its own.
 * Its maps are filled for those rows at once: the game streams one a frame. */

#ifdef __wasi__
#define WEB_COPY C.web
#define WEB_BROKEN C.broken
#else
#define WEB_COPY false
#define WEB_BROKEN false
#endif

static bool paceFresh; /* set by climb_open: a new game starts the copy at every frame, not at the last one's pace (climb_pace) */

/* A picture the copy drew, with the game's frame it is of and the scroll it was drawn from */
typedef struct {
	uint32_t px[GBA_W * GBA_H];
	unsigned seq, epoch;
	bool ok;
	uint16_t regs[6]; /* the copy's BG1-3 scroll (x, y each) as its frame ended */
} Pic;

/* What the copy is asked for: the game's frame `seq` from its state before the frame before */
typedef struct {
	unsigned seq, epoch;
	int slot;
	uint32_t keysPrev, keysNow;
	int32_t cam;
	uint16_t oam[512];
} Job;

static struct {
	Emu e;
	const MapProfile* p;
	bool open;
	void* st[3];
	uint32_t keys[3];
	int last; /* the slot saved last: the game's state before the frame before */
	bool have;
	uint32_t nowKeys, prevKeys;
	int32_t gameCam; /* the game's camera row (the first word to shift) as the frames start */
	uint16_t gameOam[512]; /* the game's shadow sprite table, the one for the copy's first frame */
	int job; /* the slot the copy's frames start from */
	const uint8_t* rom; /* its ROM, for reading the level's block tables */
	size_t romSize;
	bool up, havePic;
	bool skip;  /* this frame the copy isn't run: the last picture is moved with the camera */
	int gap;    /* frames since the copy last ran: the picture is moved with the camera until it runs again */
	int picX, picY; /* the game's camera (whole pixels) when `pic` was drawn */
	uint32_t pic[GBA_W * GBA_H], moved[GBA_W * GBA_H];
	/* The copy on its own, a frame or two behind (the default): jobs go to the worker, pictures come back whenever
	 * they're done, and the last one is moved with the camera to this frame. */
	bool async, syncStep;
	size_t stateSize;
#ifdef __wasi__
	bool web, broken; /* the copy is in the web player's climb worker: jobs leave for the page (web_climb_job) and pictures come back (web_climb_deliver) */
	bool debug;
	char romPath[512], code[8];
#endif
	Job req;
	bool reqPending;
	bool slotBusy[3]; /* a state the worker hasn't loaded yet */
	Pic pics[3];      /* the one in use, the newest finished one, the one being drawn */
	int held, ready, wr;
	unsigned seq, epoch, baseSeq;
	bool baseOk;
	unsigned posted, saved, taken, lagSum, lagMax; /* (what the worker did, for the log) */
	bool slow; /* the copy's last picture took over most of a frame: no state kept for the frame after a job's */
	uint16_t gameRegs[32][6]; /* the game's BG scroll after each of its last frames */
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
} C = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };

/* the entry of a layer's map at world pixel (x, y): its block of 32x32 entries
 * from the layer's table (rows of four pointers, 256 pixels each way) */
static uint32_t rom_read32(Emu* e, uint32_t a) {
	uint32_t o = a - 0x08000000;
	if (a >= 0x08000000 && a < 0x0A000000 && o + 4 <= C.romSize) return C.rom[o] | C.rom[o + 1] << 8 | C.rom[o + 2] << 16 | (uint32_t) C.rom[o + 3] << 24;
	return emu_read32(e, a);
}

static uint16_t rom_read16(Emu* e, uint32_t a) {
	uint32_t o = a - 0x08000000;
	if (a >= 0x08000000 && a < 0x0A000000 && o + 2 <= C.romSize) return C.rom[o] | C.rom[o + 1] << 8;
	return emu_read16(e, a);
}

static uint16_t block_entry(Emu* e, uint32_t table, int x, int y) {
	if (x < 0 || y < 0) return 0;
	uint32_t blk = rom_read32(e, table + (uint32_t) (y >> 8) * 16 + (uint32_t) (x >> 8) * 4);
	if (blk < 0x02000000) return 0;
	return rom_read16(e, blk + ((y & 0xF8) << 3) + ((x & 0xF8) >> 2));
}

/* The layer's whole 32x32 map for the rows the game would have in it at its
 * position now: 16 rows above its top to 240 below, as its streaming keeps them;
 * a column to its left of the screen, the 31 the screen shows from there on (a
 * scroll that isn't a whole tile shows one more column than 240 pixels' 30) */
static void fill_layer(Emu* e, const typeof(((MapProfile*) 0)->climb.layer[0])* L) {
	int x = (int16_t) emu_read16(e, L->x + 2), y = (int16_t) emu_read16(e, L->y + 2);
	uint32_t table = emu_read32(e, L->table);
	int t0 = (y - 16) >> 3, c0 = (x >> 3) - 1;
	for (int r = 0; r < 32; ++r) {
		uint16_t row[32];
		for (int i = 0; i < 32; ++i) row[(c0 + i) & 31] = block_entry(e, table, (c0 + i) * 8, (t0 + r) * 8);
		emu_vram_write(e, L->map + ((t0 + r) & 31) * 64, row, sizeof(row));
	}
}

/* whether the game streams this layer from its table: the map in VRAM holds what the table says for the
 * rows and columns on the screen. A layer the game fills some other way (a picture loaded once, a pattern
 * drawn by code) is left as the game has it: the table's rows would replace one picture with another. */
static bool layer_from_table(Emu* e, const typeof(((MapProfile*) 0)->climb.layer[0])* L) {
	int x = (int16_t) emu_read16(e, L->x + 2), y = (int16_t) emu_read16(e, L->y + 2);
	uint32_t table = emu_read32(e, L->table);
	const uint16_t* map = emu_vram(e) + L->map / 2;
	int n = 0, bad = 0;
	for (int ty = y >> 3; ty <= (y + GBA_H - 1) >> 3; ++ty)
		for (int tx = x >> 3; tx <= (x + GBA_W - 1) >> 3; ++tx, ++n)
			bad += block_entry(e, table, tx * 8, ty * 8) != map[(ty & 31) * 32 + (tx & 31)];
	return bad * 10 <= n;
}

/* The copy's sprites that are the game's too, fixed to the screen: the same
 * attributes in the same place of the table as on the game's screen (a world
 * object is a screen lower there; the slot too, as two pegs a screen apart
 * look alike) */
static void hide_fixed(void) {
	uint32_t o = C.p->climb.oam;
	static uint16_t out[128][3];
	int n = 0;
	for (int j = 0; j < 128; ++j) {
		uint16_t b0 = emu_read16(&C.e, o + j * 8), b1 = emu_read16(&C.e, o + j * 8 + 2), b2 = emu_read16(&C.e, o + j * 8 + 4);
		if ((b0 & 0x0300) == 0x0200 || (b0 >> 14) == 3) continue;
		if (C.gameOam[j * 4] == b0 && C.gameOam[j * 4 + 1] == b1 && C.gameOam[j * 4 + 2] == b2)
			out[n][0] = b0, out[n][1] = b1, out[n][2] = b2, ++n;
	}
	emu_hide_exact(&C.e, out, n);
}

/* patch words in RAM (code the game copies there at a level's start) are put in each state before it's loaded, only where the word
 * is as the profile found it (a write after the load would change code a recompiler has compiled, and each load throw it away) */
static bool patch_in_ram(uint32_t addr) { return addr >= 0x02000000 && addr < 0x08000000; }

static void release_slot(int slot) {
	pthread_mutex_lock(&C.lock);
	C.slotBusy[slot] = false;
	pthread_cond_broadcast(&C.cond);
	pthread_mutex_unlock(&C.lock);
}

/* the copy's two frames from the state in slot C.job; false if there's no picture of them */
static bool climb_frames(void) {
	const MapProfile* p = C.p;
#ifndef CLIMB_TEST
	for (int i = 0; i < 24 && p->climb.patch[i].addr; ++i)
		if (patch_in_ram(p->climb.patch[i].addr)) emu_state_patch32(&C.e, C.st[C.job], p->climb.patch[i].addr, p->climb.patch[i].orig, p->climb.patch[i].word);
#endif
	bool loaded = emu_state_load(&C.e, C.st[C.job]);
	if (C.async) release_slot(C.job);
	if (!loaded) return false;
	bool fill[3] = { false };
	for (int i = 0; i < 3 && p->climb.layer[i].table; ++i) fill[i] = layer_from_table(&C.e, &p->climb.layer[i]);
#ifndef CLIMB_TEST /* (the scout tool's check that the copy without the shift is the game's own picture) */
	for (int i = 0; i < 8 && p->climb.shift[i]; ++i)
		emu_write32(&C.e, p->climb.shift[i], emu_read32(&C.e, p->climb.shift[i]) - ((uint32_t) GBA_H << 16));
#endif
	for (int i = 0; i < 3 && p->climb.layer[i].table; ++i)
		if (fill[i]) fill_layer(&C.e, &p->climb.layer[i]);
	emu_frame_blind(&C.e, (C.async ? C.prevKeys : C.keys[C.job]) & 0x3FF);
#ifndef CLIMB_TEST
	if (p->climb.oam) hide_fixed();
#endif
	emu_frame(&C.e, C.nowKeys & 0x3FF);
	/* a screen above the game's, give or take its steps: else the camera code wasn't what ran
	 * (not a level the patches are for) and there's no picture */
#ifndef CLIMB_TEST
	int32_t off = (int32_t) emu_read32(&C.e, p->climb.shift[0]) + ((int32_t) GBA_H << 16) - C.gameCam;
	if (abs(off) > (24 << 16)) return false;
#endif
	memcpy(C.async ? C.pics[C.wr].px : C.pic, C.e.fb, sizeof(C.pic));
	return true;
}

static void* worker(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	android_pin_worker_thread("climb copy");
#endif
	pthread_mutex_lock(&C.lock);
	for (;;) {
		while (!C.busy && !C.quit) pthread_cond_wait(&C.cond, &C.lock);
		if (C.quit) break;
		pthread_mutex_unlock(&C.lock);
		C.up = climb_frames();
		pthread_mutex_lock(&C.lock);
		C.busy = false;
		pthread_cond_broadcast(&C.cond);
	}
	pthread_mutex_unlock(&C.lock);
	return NULL;
}

/* the copy's scroll after its frame, to check its picture against the game's */
static void copy_regs(uint16_t* r) {
	for (int i = 0; i < 3; ++i) r[2 * i] = emu_io16(&C.e, 0x14 + 4 * i) & 0x1FF, r[2 * i + 1] = emu_io16(&C.e, 0x16 + 4 * i) & 0x1FF;
}

static int free_pic(void) {
	for (int i = 0; i < 3; ++i)
		if (i != C.held && i != C.ready) return i;
	return 0;
}

static void* worker_async(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	android_pin_worker_thread("climb copy");
#endif
	pthread_mutex_lock(&C.lock);
	for (;;) {
		while (!C.reqPending && !C.quit) pthread_cond_wait(&C.cond, &C.lock);
		if (C.quit) break;
		Job* j = &C.req;
		C.reqPending = false;
		C.busy = true;
		C.job = j->slot, C.prevKeys = j->keysPrev, C.nowKeys = j->keysNow, C.gameCam = j->cam;
		memcpy(C.gameOam, j->oam, sizeof(C.gameOam));
		unsigned seq = j->seq, epoch = j->epoch;
		C.wr = free_pic();
		pthread_mutex_unlock(&C.lock);
		struct timespec t0, t1;
		clock_gettime(CLOCK_MONOTONIC, &t0);
		bool up = climb_frames();
		clock_gettime(CLOCK_MONOTONIC, &t1);
		pthread_mutex_lock(&C.lock);
		C.slow = (t1.tv_sec - t0.tv_sec) * 1000.0 + (t1.tv_nsec - t0.tv_nsec) / 1e6 > 12;
		Pic* pic = &C.pics[C.wr];
		pic->seq = seq, pic->epoch = epoch, pic->ok = up;
		if (up) copy_regs(pic->regs);
		C.ready = C.wr; /* (an older one not taken yet is dropped) */
		C.busy = false;
		pthread_cond_broadcast(&C.cond);
	}
	pthread_mutex_unlock(&C.lock);
	return NULL;
}

static void work_start(void) {
	pthread_mutex_lock(&C.lock);
	if (!C.started) {
		C.quit = false;
		C.started = pthread_create(&C.thread, NULL, worker, NULL) == 0;
	}
	bool ok = C.started;
	if (ok) C.busy = true, pthread_cond_broadcast(&C.cond);
	pthread_mutex_unlock(&C.lock);
	if (!ok) C.up = climb_frames(); /* (no threads: here, now) */
}

static void work_wait(void) {
	pthread_mutex_lock(&C.lock);
	while (C.busy) pthread_cond_wait(&C.cond, &C.lock);
	pthread_mutex_unlock(&C.lock);
}

void climb_close(void) {
	pthread_mutex_lock(&C.lock);
	bool started = C.started;
	C.quit = true;
	pthread_cond_broadcast(&C.cond);
	pthread_mutex_unlock(&C.lock);
	if (started) pthread_join(C.thread, NULL);
	C.started = false;
	if (C.async && C.taken && getenv("GBADS_DEBUG"))
		fprintf(stderr, "climb worker: %u of %u frames asked for (%u states handed over), %u pictures used, a picture %.2f frames behind on average (at most %u)\n", C.posted, C.seq, C.saved, C.taken, (double) C.lagSum / C.taken, C.lagMax);
	if (C.open && !WEB_COPY) emu_close(&C.e);
	for (int i = 0; i < 3; ++i) free(C.st[i]), C.st[i] = NULL;
	C.open = C.have = C.up = C.havePic = C.async = C.baseOk = false;
#ifdef __wasi__
	C.web = C.broken = false;
#endif
}

bool climb_is_open(void) { return C.open; }

/* whether the ROM in `e` has the code the profile's patches are for (another dump's hasn't) */
static bool patches_match(const MapProfile* p, Emu* e) {
	for (int i = 0; i < 24 && p->climb.patch[i].addr; ++i)
		if (!patch_in_ram(p->climb.patch[i].addr) && emu_read32(e, p->climb.patch[i].addr) != p->climb.patch[i].orig) return false;
	return true;
}

/* the copy's machine, patched; false (no copy) when the ROM isn't the one the profile's patches are for: the game
 * then on one screen with the logo */
static bool open_copy(const char* rom, const MapProfile* p) {
	if (!emu_open(&C.e, rom, NULL, false)) return false;
	if (!patches_match(p, &C.e)) {
		emu_close(&C.e);
		fprintf(stderr, "climb: not this ROM's camera code, no copy\n");
		return false;
	}
#ifndef CLIMB_TEST
	for (int i = 0; i < 24 && p->climb.patch[i].addr; ++i)
		if (!patch_in_ram(p->climb.patch[i].addr)) emu_write32(&C.e, p->climb.patch[i].addr, p->climb.patch[i].word);
#endif
	emu_layers(&C.e, 0x1E); /* (not BG0: the HUD stays on the game's own screen) */
	C.p = p;
	C.rom = emu_memory(&C.e, 8, &C.romSize);
	if (!C.rom) C.romSize = 0;
	return true;
}

/* a thread of its own is worth it only beside the game's: with one core they'd take turns, and the copy in step
 * can drop to every other or third frame when the game's run late (climb_pace) */
static bool second_core(void) {
#ifdef __wasi__
	return false;
#else
	return sysconf(_SC_NPROCESSORS_ONLN) > 1;
#endif
}

bool climb_open(const char* rom, const MapProfile* p, Emu* game, bool worker) {
	climb_close();
	if (!p || !p->climb.on[0].addr) return false;
	size_t sz = emu_state_size(game);
	C.stateSize = sz;
	bool web = false;
#ifdef __wasi__
	web = worker && getenv("GBADS_CLIMB_OFFLOAD"); /* (the web player has a climb worker: another copy of the app) */
#endif
	for (int i = 0; i < (worker ? 3 : 2); ++i) C.st[i] = malloc(sz);
	bool ok = C.st[0] && C.st[1] && (!worker || C.st[2]);
	if (ok && web) {
		ok = patches_match(p, game); /* (this machine has no copy of its own to try) */
		if (!ok) fprintf(stderr, "climb: not this ROM's camera code, no copy\n");
		C.p = p;
	} else if (ok) {
		ok = open_copy(rom, p);
	}
	if (!ok) {
		for (int i = 0; i < 3; ++i) free(C.st[i]), C.st[i] = NULL;
		return false;
	}
	C.held = C.ready = -1;
	C.seq = C.epoch = 0;
	paceFresh = true;
#ifdef __wasi__
	C.debug = getenv("GBADS_DEBUG") != NULL;
	snprintf(C.romPath, sizeof(C.romPath), "%s", rom);
	snprintf(C.code, sizeof(C.code), "%s", game->gameCode);
	C.web = C.async = web;
#endif
	if (worker && !web && second_core()) { /* (else, or if the thread can't start, the copy runs in step) */
		pthread_mutex_lock(&C.lock);
		C.quit = false;
		C.started = pthread_create(&C.thread, NULL, worker_async, NULL) == 0;
		pthread_mutex_unlock(&C.lock);
		C.async = C.started;
		C.syncStep = C.async && emu_sync_work(); /* (exact runs, emu_sync_work: each picture waited for, none a frame late) */
	}
	C.open = true;
	return true;
}

/* How many of the game's frames each picture of the copy covers: 1 while the game keeps near 60 a second;
 * while its frames average over 18.5 ms, 2, then 3 (the copy runs that often, its picture moved with the
 * camera between), then none if that doesn't help. A faster pace is tried again after 30 quiet seconds.
 * GBADS_CLIMB_PACE=1|2|3|4 holds it (for the tests). */
static int climb_pace(void) {
	static double last, avg = 16.7;
	static int level, wait = 120, calm;
	const char* hold = getenv("GBADS_CLIMB_PACE");
	if (hold) return atoi(hold);
	if (paceFresh) last = 0, avg = 16.7, level = 0, wait = 120, calm = 0, paceFresh = false;
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	double now = t.tv_sec * 1000.0 + t.tv_nsec / 1e6, dt = now - last;
	last = now;
	if (dt > 0 && dt < 250) avg += (dt - avg) * 0.03; /* (a pause is no lateness) */
	if (wait > 0) --wait;
	bool late = avg > 18.5;
	int was = level;
	if (late && !wait && level < 3) ++level, wait = 90, calm = 0;
	else if (!late && level && ++calm >= 1800) --level, wait = 90, calm = 0;
	if (level != was && getenv("GBADS_DEBUG")) fprintf(stderr, "climb copy: %s (frames take %.1f ms)\n", level == 0 ? "every frame" : level == 1 ? "every other frame" : level == 2 ? "every third frame" : "stopped", avg);
	return level + 1;
}

/* where the game's first background is scrolled to (its registers, which the picture of a frame is drawn from) */
static void game_camera(Emu* game, int* x, int* y) {
	*x = emu_io16(game, 0x14) & 0x1FF;
	*y = emu_io16(game, 0x16) & 0x1FF;
}

/* `pic` moved the way the camera has moved since it was drawn, its new edge a copy of the old one; false if
 * the camera jumped (not a step) */
static bool move_picture(Emu* game, const uint32_t* base) {
	int x, y;
	game_camera(game, &x, &y);
	int dx = ((x - C.picX + 256) & 0x1FF) - 256, dy = ((y - C.picY + 256) & 0x1FF) - 256;
	if (abs(dx) > 16 || abs(dy) > 16) return false;
	for (int row = 0; row < GBA_H; ++row) {
		int sy = row + dy < 0 ? 0 : row + dy >= GBA_H ? GBA_H - 1 : row + dy;
		for (int col = 0; col < GBA_W; ++col) {
			int sx = col + dx < 0 ? 0 : col + dx >= GBA_W ? GBA_W - 1 : col + dx;
			C.moved[row * GBA_W + col] = base[sy * GBA_W + sx];
		}
	}
	return true;
}

/* The game's frame is about to run. If the copy is free, it's asked for this frame from the state saved before the frame
 * before, and this frame's state is saved for the next (a state is the dearest thing handed over: a copy that takes
 * longer than a frame gets one for each picture it can draw, not for each frame the game runs). */
static void begin_async(Emu* game, uint32_t keys) {
	int next = (C.last + 1) % 3;
	pthread_mutex_lock(&C.lock);
	while (C.slotBusy[next] && !WEB_COPY) pthread_cond_wait(&C.cond, &C.lock);
	bool free = !C.busy && !C.reqPending, posted = false;
	if (C.have && free) {
		Job* j = &C.req;
		j->seq = C.seq, j->epoch = C.epoch, j->slot = C.last, j->keysPrev = C.keys[C.last], j->keysNow = keys;
		j->cam = (int32_t) emu_read32(game, C.p->climb.shift[0]);
		for (int i = 0; i < 512; ++i) j->oam[i] = emu_read16(game, C.p->climb.oam + i * 2);
		C.slotBusy[C.last] = !WEB_COPY;
		C.reqPending = true;
		++C.posted;
		posted = true;
		pthread_cond_broadcast(&C.cond);
	}
	bool slow = C.slow; /* (the worker sets it under the lock) */
	pthread_mutex_unlock(&C.lock);
	C.have = false;
	if (free && !(posted && slow) && emu_state_save(game, C.st[next])) C.keys[next] = keys, C.last = next, C.have = true, ++C.saved;
}

/* After the game's frame: its scroll noted; the newest picture the copy has finished, if it drew the scroll the game
 * had for its frame, becomes the one that's moved; it's moved to the game's scroll now (the game being ahead of it
 * by the frames the copy took) */
static void end_async(Emu* game) {
	unsigned n = C.seq++;
#ifdef __wasi__
	if (C.debug && n % 600 == 599) /* (a thread's are told at the end; a page's worker has no end) */
		fprintf(stderr, "climb worker: %u of %u frames asked for (%u states handed over), %u pictures used, a picture %.2f frames behind on average (at most %u)\n", C.posted, C.seq, C.saved, C.taken, C.taken ? (double) C.lagSum / C.taken : 0.0, C.lagMax);
#endif
	uint16_t* r = C.gameRegs[n & 31];
	for (int i = 0; i < 3; ++i) r[2 * i] = emu_io16(game, 0x14 + 4 * i) & 0x1FF, r[2 * i + 1] = emu_io16(game, 0x16 + 4 * i) & 0x1FF;
	pthread_mutex_lock(&C.lock);
	if (C.syncStep) {
		while (C.busy || C.reqPending) pthread_cond_wait(&C.cond, &C.lock);
	}
	if (C.ready >= 0) {
		Pic* p = &C.pics[C.ready];
		bool ok = p->ok && p->epoch == C.epoch && n - p->seq < 32;
		for (int i = 0; ok && i < 3 && C.p->climb.layer[i].table; ++i) {
			const uint16_t* g = C.gameRegs[p->seq & 31];
			ok = p->regs[2 * i] == g[2 * i] && p->regs[2 * i + 1] == ((g[2 * i + 1] - GBA_H) & 0x1FF);
		}
		if (ok) {
			C.held = C.ready;
			C.baseSeq = p->seq;
			C.baseOk = true;
			++C.taken, C.lagSum += n - p->seq, C.lagMax = n - p->seq > C.lagMax ? n - p->seq : C.lagMax;
		}
		C.ready = -1;
	}
	pthread_mutex_unlock(&C.lock);
	C.havePic = false;
	if (!C.baseOk || n - C.baseSeq >= 32) return;
	C.picX = C.gameRegs[C.baseSeq & 31][0], C.picY = C.gameRegs[C.baseSeq & 31][1];
	C.havePic = move_picture(game, C.pics[C.held].px);
}

void climb_begin(Emu* game, uint32_t keys, bool run) {
	if (!C.open || WEB_BROKEN) return;
	C.up = false;
	if (!run) {
		C.have = false;
		C.havePic = false;
		if (C.async) {
			pthread_mutex_lock(&C.lock);
			++C.epoch;
			C.baseOk = false;
			pthread_mutex_unlock(&C.lock);
		}
		return;
	}
	if (C.async) {
		begin_async(game, keys);
		return;
	}
	int pace = climb_pace();
	if (pace > 3) { /* (the game on its own screen, as with no copy) */
		C.have = C.havePic = false;
		return;
	}
	C.skip = C.have && C.havePic && C.gap + 1 < pace;
	if (C.have && !C.skip) {
		C.gameCam = (int32_t) emu_read32(game, C.p->climb.shift[0]);
		for (int i = 0; i < 512; ++i) C.gameOam[i] = emu_read16(game, C.p->climb.oam + i * 2);
		C.job = C.last, C.nowKeys = keys;
		work_start();
	}
	int slot = C.have ? 1 - C.last : 0;
	if (emu_state_save(game, C.st[slot])) C.keys[slot] = keys, C.last = slot, C.have = true;
	else C.have = false;
}

/* the copy's picture is the game's of this frame a screen higher only if each layer was drawn from the
 * scroll it should: the game's own registers after its frame, the rows lowered. A frame in which code
 * wrote the camera's row some other way (a pan that ends on the real row) is drawn from another one. */
static bool scroll_as_game_has(Emu* game) {
#ifdef CLIMB_TEST
	const int shift = 0;
#else
	const int shift = GBA_H;
#endif
	for (int i = 0; i < 3 && C.p->climb.layer[i].table; ++i) {
		int h = 0x14 + 4 * i, v = 0x16 + 4 * i;
		if (((emu_io16(&C.e, v) ^ (emu_io16(game, v) - shift)) & 0x1FF) || ((emu_io16(&C.e, h) ^ emu_io16(game, h)) & 0x1FF)) return false;
	}
	return true;
}

void climb_end(Emu* game) {
	if (!C.open || WEB_BROKEN) return;
	if (C.async) {
		end_async(game);
		return;
	}
	work_wait();
	if (C.skip) {
		++C.gap;
		if (!move_picture(game, C.pic)) C.havePic = false;
		return;
	}
	C.havePic = C.up && scroll_as_game_has(game);
	C.gap = 0;
	if (C.havePic) game_camera(game, &C.picX, &C.picY);
}

/* the copy is on its own and has drawn nothing for this level yet: a picture is on its way */
bool climb_waiting(void) { return C.open && C.async && !C.baseOk && !WEB_BROKEN; }

const uint32_t* climb_picture(void) {
	if (!C.open || !C.havePic || WEB_BROKEN) return NULL;
	return C.gap || C.async ? C.moved : C.pic;
}

void climb_reset(void) {
	C.have = C.havePic = false;
	if (!C.async) return;
	pthread_mutex_lock(&C.lock);
	++C.epoch;
	C.baseOk = false;
	pthread_mutex_unlock(&C.lock);
}

#ifdef __wasi__
/* ---- the web player's climb worker: a second copy of the app (GBADS_CLIMB_SERVE=CODE, the ROM last) that does
 * the copy's frames while the game goes on, as a thread does elsewhere. The page carries the jobs and the pictures
 * (web/player/worker.js, player.js). */
extern void web_sleep(unsigned ms) __attribute__((import_module("env"), import_name("web_sleep")));

/* The game's side: a job to hand over (0: none), and in info the state (pointer, size), the frame, the epoch, the
 * keys before and now, the camera, the sprite table (pointer), and what the worker starts with: the ROM's path
 * and the game's code. */
__attribute__((export_name("web_climb_job"))) uint32_t web_climb_job(uint32_t* info) {
	if (!C.web || !C.reqPending) return 0;
	Job* j = &C.req;
	C.reqPending = false;
	C.busy = true;
	info[0] = (uint32_t) (uintptr_t) C.st[j->slot], info[1] = (uint32_t) C.stateSize;
	info[2] = j->seq, info[3] = j->epoch, info[4] = j->keysPrev, info[5] = j->keysNow, info[6] = (uint32_t) j->cam;
	info[7] = (uint32_t) (uintptr_t) j->oam;
	info[8] = (uint32_t) (uintptr_t) C.romPath, info[9] = (uint32_t) (uintptr_t) C.code;
	return j->seq + 1;
}

/* Where a picture goes (the pixels, GBA_W x GBA_H words), for web_climb_deliver */
__attribute__((export_name("web_climb_inbox"))) void* web_climb_inbox(void) {
	if (!C.web) return NULL;
	C.wr = free_pic();
	return C.pics[C.wr].px;
}

/* The picture of frame `seq` is in the inbox: ok if the worker drew one, and the scroll it was drawn from, the six
 * numbers two to a word. A job the game has since dropped (a reset, the epoch) is let go. */
__attribute__((export_name("web_climb_deliver"))) void web_climb_deliver(uint32_t seq, uint32_t epoch, int ok, uint32_t r01, uint32_t r23, uint32_t r45) {
	if (!C.web) return;
	Pic* p = &C.pics[C.wr];
	p->seq = seq, p->epoch = epoch, p->ok = ok;
	p->regs[0] = (uint16_t) r01, p->regs[1] = (uint16_t) (r01 >> 16), p->regs[2] = (uint16_t) r23, p->regs[3] = (uint16_t) (r23 >> 16);
	p->regs[4] = (uint16_t) r45, p->regs[5] = (uint16_t) (r45 >> 16);
	C.ready = C.wr;
	C.busy = false;
}

/* The worker failed or the page has none: the game on one screen from here on */
__attribute__((export_name("web_climb_offload"))) void web_climb_offload(int on) {
	if (!on && C.web) C.broken = true, C.busy = C.reqPending = C.havePic = false;
}

/* The worker's side */
static Job serveJob;
static bool servePending, serveDone;
static int serveOk;

__attribute__((export_name("web_serve_climb_snapshot"))) void* web_serve_climb_snapshot(void) { return C.st[0]; }
__attribute__((export_name("web_serve_climb_snapshot_size"))) uint32_t web_serve_climb_snapshot_size(void) { return C.st[0] ? (uint32_t) C.stateSize : 0; }
/* where the page puts the job's fields (a Job: seq, epoch, slot, keys before, keys now, camera, the sprite table) */
__attribute__((export_name("web_serve_climb_info"))) void* web_serve_climb_info(void) { return &serveJob; }
__attribute__((export_name("web_serve_climb_go"))) void web_serve_climb_go(void) { servePending = true; }
/* The job finished since the last call (0: none); in info the picture's pointer, the frame, the epoch, ok and the
 * scroll as web_climb_deliver has it */
__attribute__((export_name("web_serve_climb_result"))) uint32_t web_serve_climb_result(uint32_t* info) {
	if (!serveDone) return 0;
	serveDone = false;
	const Pic* p = &C.pics[0];
	info[0] = (uint32_t) (uintptr_t) p->px, info[1] = p->seq, info[2] = p->epoch, info[3] = (uint32_t) p->ok;
	info[4] = p->regs[0] | (uint32_t) p->regs[1] << 16, info[5] = p->regs[2] | (uint32_t) p->regs[3] << 16, info[6] = p->regs[4] | (uint32_t) p->regs[5] << 16;
	return 1;
}

int climb_web_serve(const char* rom, const char* code) {
	const MapProfile* p = profile_find(code);
	if (!p || !p->climb.on[0].addr || !open_copy(rom, p)) {
		fprintf(stderr, "climb worker: no copy for %s (%s)\n", code, rom);
		return 1;
	}
	C.stateSize = emu_state_size(&C.e);
	C.st[0] = malloc(C.stateSize);
	if (!C.st[0]) return 1;
	C.async = C.open = true; /* (climb_frames: the picture into pics[wr], the keys from the job) */
	C.wr = 0;
	fprintf(stderr, "climb worker: %s\n", p->title);
	for (;;) {
		if (servePending) {
			servePending = false;
			C.job = 0, C.prevKeys = serveJob.keysPrev, C.nowKeys = serveJob.keysNow, C.gameCam = serveJob.cam;
			memcpy(C.gameOam, serveJob.oam, sizeof(C.gameOam));
			bool up = climb_frames();
			Pic* pic = &C.pics[0];
			pic->seq = serveJob.seq, pic->epoch = serveJob.epoch, pic->ok = up;
			if (up) copy_regs(pic->regs);
			serveDone = true;
		}
		web_sleep(servePending ? 0 : 2);
	}
}
#endif

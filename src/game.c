/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#include "i18n.h"
#ifdef __wasi__
extern void web_sleep(unsigned ms) __attribute__((import_module("env"), import_name("web_sleep")));
#endif
#include "android.h"
#include "brand.h"
#include "emu.h"
#include "mapcap.h"
#include "hotkeys.h"
#include "paths.h"
#include "ui.h"
#include "bezel.h"
#include "ra.h"
#include "textfont.h"
#include "pixfont.h"
#include "logo.h"
#include <ctype.h>
#include <dirent.h>
#include <strings.h>
#include <math.h>
#include "profiles.h"
#include "patch.h"
#include "cheats.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <limits.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#ifdef __ANDROID__
#include <sys/system_properties.h>
#endif
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Times the game thread was taken off its core (involuntary context
 * switches): another thread got it, so the frame ran late through no work
 * of ours. Linux only; 0 elsewhere. */
static long thread_preemptions(void) {
#if defined(__linux__) && defined(RUSAGE_THREAD)
	struct rusage ru;
	if (getrusage(RUSAGE_THREAD, &ru) == 0) return ru.ru_nivcsw;
#endif
	return 0;
}
static double thread_cpu_ms(void) {
	struct timespec t;
#ifdef __wasi__
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
#else
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
#endif
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
/* CLOCK_MONOTONIC in seconds: the time the kernel log and the journal's
 * monotonic stamps use, so a stall can be matched with what the system did
 * (launch.sh logs both on ROCKNIX) */
static double mono_seconds(void) {
	struct timespec t;
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
	return t.tv_sec + t.tv_nsec / 1e9;
}
static int cpu_mhz(int cpu) {
#ifdef __linux__
	if (cpu < 0) return 0;
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
	FILE* f = fopen(path, "r");
	long k = 0;
	if (f) {
		if (fscanf(f, "%ld", &k) != 1) k = 0;
		fclose(f);
	}
	return (int) (k / 1000);
#else
	(void) cpu;
	return 0;
#endif
}
static int current_cpu(void) {
#if defined(__linux__) && defined(SYS_getcpu)
	unsigned cpu = 0;
	return syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 ? (int) cpu : -1;
#else
	return -1;
#endif
}

#define GBA_FPS (16777216.0 / 280896.0)
#define FF_MAX_RUNS 30 /* fast-forward at Max: at most this many frames a display frame */

static Config* cfg;
static Emu emu;

/* ------------------------------------------------- frame buffer (option)
 * With frame_buffer on, the next frame is emulated on this thread while the
 * game loop draws and shows the one before: a frame that runs long (the
 * game thread kept off the CPU for a few ms, common on Android) no longer
 * misses the screen's refresh, at the cost of one frame of input delay.
 * The worker only runs emu_frame; everything else that touches the core
 * (sound, RetroAchievements, map, probes, states, cheats, the menu) stays
 * on the game loop and runs while the worker is idle: the loop waits for
 * it at the top of each iteration and starts it again just before drawing. */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	uint32_t keys;
} ahead = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
static bool aheadInFlight; /* the worker is emulating the next frame */
static bool aheadReady;    /* it has: that frame is emulated but not yet used */
static uint32_t aheadFb[GBA_W * GBA_H]; /* the frame on screen, while the worker writes the next */

/* Bits above the GBA keys, for run_frame: the SNES layout's camera pan
 * (profile controls.snes) left / right while a shoulder is held. */
#define KX_PAN_L (1u << 20)
#define KX_PAN_R (1u << 21)
static bool debug; /* GBADS_DEBUG (declared again below) */
static const struct MapProfile* ctlProf; /* NULL: no alternate layout for this game */

/* One frame of the game with these keys (on the worker too, while the main
 * thread leaves the core alone). */
static uint32_t frameKeys; /* the GBA keys of the last frame run */
/* Bug reports (the quick bar's bug button): the keys of every frame run
 * and a state a second, kept for the last ~11 seconds, so a report can
 * hold the game from 10 seconds before and every key since. bugFrame
 * counts the frames run on the game's core (run_frame, on the worker
 * too); the states are taken between frames (bug_tick). Loading a state,
 * rewinding or restarting starts the log again (bug_reset). */
#define BUG_LOG 2048    /* frames of keys kept (a power of two) */
#define BUG_SNAPS 12    /* states kept */
#define BUG_EVERY 60    /* frames between them */
#define BUG_BACK 600    /* how far back a report goes: 10 seconds */
static uint64_t bugFrame, bugStart;
static uint16_t bugKeys[BUG_LOG];
static uint8_t bugPoke[BUG_LOG]; /* the SNES layout's camera pan written that frame (run_frame), 0 none */
static struct {
	void* st;
	uint64_t frame;
	bool have;
} bugSnap[BUG_SNAPS];
static uint64_t frameCount;
static bool tallOn;        /* the table's up: the screens show it */
/* profile tall.live: two more copies of the game drawing the table's rows
 * the game's camera doesn't (update_tall). twins[0] is held at the top of
 * the lower view (row tall.rows - 320), twins[1] 160 rows below the view's
 * top (the game's view, at most rows - 320). Every frame each copy is
 * taken from the game's state from before the frame before (run_frame
 * keeps the last two), held at its row and run that frame with the game's
 * keys, then this frame, whose picture it shows: its logic is always the
 * game's, but its picture is of its own rows, with its own sprites and
 * scroll for them. (A copy left running on its own fell behind the game:
 * the game counts some things only when they're on its own screen, such as
 * the bumper hits that uncover catch mode's portrait.) The copies' camera
 * stores are no-ops: they're moved by setting their camera. The copies run
 * on two worker threads, the second frame while the game runs its own. */
#define TWINS 2
typedef struct {
	Emu e;
	bool need;    /* its picture is on a screen next frame (twins[0]: the lower view) */
	bool up;      /* it ran this frame: pic is this frame's */
	int row;      /* where it's held for this frame's picture: its view's row less the shake */
	int panStep;  /* how far its own pans moved its view in its last first frame */
	int hidden;   /* sprites left out this frame */
	bool havePic;
	int picRow;   /* the table row on pic's first line */
	uint32_t pic[GBA_W * GBA_H];
	/* (profile tall.live.carry) its camera's row, walked to where it's held;
	 * what it keeps of its own from its last first frame (carried): its BG
	 * tiles, vars and queued copies into them */
	int cam;
	bool carried;
	uint64_t seq; /* the run_frame its first frame last ran in */
	uint8_t* ring;
	uint8_t vars[4][32];
	uint32_t q[64][4]; /* (entries of up to 16 bytes) */
	int nq;
	uint16_t oam[512];   /* its sprite table as it last drew, and */
	uint8_t shadow[1024]; /* the game's shadow of it, as the copy left it: its own sprites, for a frame whose logic runs long */
} Twin;
static Twin twins[TWINS];
static bool twinOpen;
static uint64_t twinSeq; /* run_frames; jumps on a load or rewind (bug_reset) */
static bool twinNoObj; /* (carry) the copies draw no sprites (profile carry.noObj, in the game) */
static bool twinRun;       /* the copies are to run (a table they know, its sweep over, a ball) */
static int twinArmed;      /* frames on a table they know, with no sweep of the table going on */
static void* twinStates[2]; /* the game's state from before each of the last two frames */
static uint32_t twinKeys[2];
static int twinHave, twinCur; /* how many of them there are; the latest */
static const MapProfile* twinProf;
static bool tallTrace; /* GBADS_TALL_TRACE: a line a frame about the live view */
static int twinShakeNow, twinShakeNext; /* the game's screen shake in this frame's picture / the next's */
static bool tallMsg, tallGameTop; /* (for the log: PP-11's screen, the top screen's picture) */
static bool tallTopLogo; /* PP-11's screen up with no table rows above it: the logo on the top screen */
static bool tallFixedBot; /* PP-10: the game's banners on the bottom screen (the ball's there) */
static int tallFixedHold; /* frames the banners stay where they went with none shown */
static uint32_t lastTop[GBA_W * GBA_H]; /* the top screen before PP-11's screen came up */
static bool haveLastTop;

/* a worker thread for each copy: runs fn(arg) when asked */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	void (*fn)(void*);
	void* arg;
} twinWork[TWINS] = { { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER },
                      { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER } };

static void* twin_worker(void* arg) {
	typeof(twinWork[0])* w = arg;
	pthread_mutex_lock(&w->lock);
	for (;;) {
		while (!w->busy && !w->quit) pthread_cond_wait(&w->cond, &w->lock);
		if (w->quit) break;
		void (*fn)(void*) = w->fn;
		void* a = w->arg;
		pthread_mutex_unlock(&w->lock);
		fn(a);
		pthread_mutex_lock(&w->lock);
		w->busy = false;
		pthread_cond_broadcast(&w->cond);
	}
	pthread_mutex_unlock(&w->lock);
	return NULL;
}
/* fn(arg) on worker i; no thread (the web build): here, now */
static void twin_work_start(int i, void (*fn)(void*), void* arg) {
	typeof(twinWork[0])* w = &twinWork[i];
	pthread_mutex_lock(&w->lock);
	if (!w->started) {
		w->quit = false;
		w->started = pthread_create(&w->thread, NULL, twin_worker, w) == 0;
	}
	bool ok = w->started;
	if (ok) w->fn = fn, w->arg = arg, w->busy = true, pthread_cond_broadcast(&w->cond);
	pthread_mutex_unlock(&w->lock);
	if (!ok) fn(arg);
}
static void twin_work_wait(int i) {
	typeof(twinWork[0])* w = &twinWork[i];
	pthread_mutex_lock(&w->lock);
	while (w->busy) pthread_cond_wait(&w->cond, &w->lock);
	pthread_mutex_unlock(&w->lock);
}
static void twin_work_stop(void) {
	for (int i = 0; i < TWINS; ++i) {
		typeof(twinWork[0])* w = &twinWork[i];
		pthread_mutex_lock(&w->lock);
		bool started = w->started;
		w->quit = true;
		pthread_cond_broadcast(&w->cond);
		pthread_mutex_unlock(&w->lock);
		if (started) pthread_join(w->thread, NULL);
		w->started = false;
	}
}

static bool twin_ball(Emu* e, uint32_t S, int32_t* x, int32_t* y) {
	uint32_t b = emu_read32(e, S + twinProf->tall.live.ball);
	if (b < 0x02000000 || b >= 0x02040000) return false;
	*x = (int32_t) emu_read32(e, b + 0x34), *y = (int32_t) emu_read32(e, b + 0x38);
	return true;
}

/* The table's rows: all of them, or (carry) the visible ones of the table
 * up now; the view's top row at its highest; the screen shake */
static bool tall_carry(const MapProfile* p) { return p && p->tall.live.carry.cam; }
static int tall_rows_of(const MapProfile* p, Emu* e) {
	const typeof(p->tall.live.carry)* C = &p->tall.live.carry;
	if (!C->cam || !C->rowsAt) return p->tall.rows;
	int h = emu_read16(e, C->rowsAt) - C->bottomLess;
	return h > 2 * GBA_H + C->top && h < 1024 ? h : 2 * GBA_H + C->top;
}
static int tall_top_of(const MapProfile* p) { return p->tall.live.carry.cam ? p->tall.live.carry.top : 0; }
/* the view's row as the game keeps it (the next frame's BG VOFS; carry:
 * less viewOff, Muppet Pinball Mayhem's camera being 16 rows below it) */
static int tall_view(const MapProfile* p, Emu* e) {
	return (int16_t) emu_read16(e, p->tall.live.view) - p->tall.live.carry.viewOff;
}
static int tall_shake(const MapProfile* p, Emu* e) {
	const typeof(p->tall.live)* L = &p->tall.live;
	if (L->carry.cam) return L->carry.shakeAt ? (int16_t) emu_read16(e, L->carry.shakeAt) : 0;
	uint32_t S = emu_read32(e, L->state);
	return S >= 0x02000000 && S < 0x02040000 ? (int8_t) emu_read8(e, S + L->shake) : 0;
}

/* A copy just taken from the game: its scroll and table tiles set for
 * camera row `row` at once, as if it had scrolled there (profile
 * tall.live.jump). */
static void twin_jump(Emu* e, uint32_t T, int row) {
	const typeof(twinProf->tall.live.jump)* J = &twinProf->tall.live.jump;
	if (!J->ring) return;
	int nt = row >> 3;
	size_t ewSize;
	const uint8_t* ew = emu_memory(e, 2, &ewSize); /* (EWRAM, 0x02000000) */
	for (int R = SDL_max(nt, 0); R < nt + J->slots && R < J->tileRows; ++R) {
		uint32_t src = R < J->split ? J->src[0] + R * 0x400 : J->src[1] + (R - J->split) * 0x400;
		uint32_t dst = J->ring + ((R + J->slotOff) % J->slots) * 0x400;
		if (ew && src - 0x02000000 + 0x400 <= ewSize) emu_vram_write(e, dst - 0x06000000, ew + (src - 0x02000000), 0x400);
	}
	emu_write16(e, T + J->last, (uint16_t) nt);
	for (int i = 0; i < 4; ++i) if (J->scroll[i]) emu_write16(e, T + J->scroll[i], (uint16_t) row);
	for (int i = 0; i < 3; ++i) if (J->scrollAt[i]) emu_write16(e, J->scrollAt[i], (uint16_t) row);
	for (int bg = 0; bg < 4; ++bg) if ((J->scrollVofs >> bg) & 1) emu_write16(e, 0x04000012 + bg * 4, (uint16_t) row);
	if (J->plus16) emu_write16(e, J->plus16, (uint16_t) (J->plus16Base + row + 16));
}

/* A sprite's height from its attribute 0 and 1 (double for an affine one
 * drawn double size). */
static int obj_height(uint16_t a0, uint16_t a1) {
	static const uint8_t h[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
	int s = a0 >> 14;
	int v = s < 3 ? h[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
static int obj_width(uint16_t a0, uint16_t a1) {
	static const uint8_t w[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
	int s = a0 >> 14;
	int v = s < 3 ? w[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
/* the game's sprites at the same place on the screen whatever the camera,
 * this frame (found against twins[0]; drawn by the HUD split, to be laid
 * over the top screen) */
static uint16_t tallFixed[128][3];
static int nTallFixed;
/* (carry) the rows where the score bar's layers drew something over the
 * table last frame (a box, a banner; -1: none): only the fixed sprites
 * there go to the top screen with it */
static int tallBandY0 = -1, tallBandY1 = -1;
static uint16_t tallBandFixed[128][3];
static int nTallBandFixed;

static uint8_t gameOam[1024]; /* the game's shadow sprite table and view before this frame (run_frame) */
static int gameView;
static void game_oam_take(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	for (int i = 0; i < 1024; i += 2) { uint16_t v = emu_read16(&emu, L->oam + i); gameOam[i] = v & 0xFF, gameOam[i + 1] = v >> 8; }
	gameView = tall_view(twinProf, &emu);
}
#define GAME_OAM16(off) ((uint16_t) (gameOam[(off)] | gameOam[(off) + 1] << 8))

/* The sprites each copy leaves out this frame, from the shadow sprite
 * tables (what this frame shows) and the views each shows them with
 * (PP-10). A copy's sprite alike one of the game's sprites on the game's
 * screen (attributes 1 and 2, the high byte of 0) is either the same thing
 * on the table, seen from the copy's camera: kept if that puts it on the
 * copy's screen, left out if it's only there because the hardware wraps its
 * position round (a message's letters); or at the same place on the screen
 * whatever the camera (a banner, the Mart's items): left out, the game's
 * picture shows it. */
static void twin_hide(Twin* tw) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	uint32_t o = L->oam;
	int gs = gameView, cs = tall_view(twinProf, &tw->e);
	static uint16_t gA[128][4]; /* the game's sprites on its screen: attributes 0-2, y on its screen + 256 */
	int ng = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = GAME_OAM16(i * 8);
		if ((a0 & 0x0300) == 0x0200 || (a0 >> 14) == 3) continue;
		uint16_t a1 = GAME_OAM16(i * 8 + 2);
		int y = a0 & 0xFF, h = obj_height(a0, a1);
		if (y >= GBA_H) y -= 256;
		if (y >= GBA_H || y + h <= 0) continue; /* (off its screen) */
		gA[ng][0] = a0, gA[ng][1] = a1, gA[ng][2] = GAME_OAM16(i * 8 + 4), gA[ng][3] = (uint16_t) (y + 256), ++ng;
	}
	static uint16_t out[128][3];
	static bool gFixed[128];
	memset(gFixed, 0, sizeof(gFixed));
	int n = 0;
	for (int j = 0; j < 128 && n < 128; ++j) {
		uint16_t b0 = emu_read16(&tw->e, o + j * 8);
		if ((b0 & 0x0300) == 0x0200 || (b0 >> 14) == 3) continue;
		uint16_t b1 = emu_read16(&tw->e, o + j * 8 + 2), b2 = emu_read16(&tw->e, o + j * 8 + 4);
		bool table = false, fixed = false;
		int h = obj_height(b0, b1);
		for (int i = 0; i < ng && !table; ++i) {
			if (gA[i][1] != b1 || gA[i][2] != b2 || (gA[i][0] & 0xFF00) != (b0 & 0xFF00)) continue;
			int yt = (int) gA[i][3] - 256 + gs - cs; /* where the table puts it on the copy's screen */
			if (((yt + 256) & 0xFF) == (b0 & 0xFF)) {
				if (yt < GBA_H && yt + h > 0) table = true;
				else fixed = true; /* (wrapped round) */
			} else if ((gA[i][0] & 0xFF) == (b0 & 0xFF)) {
				fixed = gFixed[i] = true;
			}
		}
		/* (and those found fixed to the screen against twins[0], for one at
		 * the game's own camera, which can't tell) */
		for (int i = 0; i < nTallFixed && tw != &twins[0] && !fixed; ++i) {
			fixed = tallFixed[i][0] == b0 && tallFixed[i][1] == b1 && tallFixed[i][2] == b2;
			if (fixed) table = false;
		}
		if (fixed && !table) out[n][0] = b0, out[n][1] = b1, out[n][2] = b2, ++n;
	}
	emu_hide_exact(&tw->e, out, n);
	tw->hidden = n;
	if (tw == &twins[0]) {
		nTallFixed = 0;
		for (int i = 0; i < ng; ++i) {
			if (!gFixed[i]) continue;
			tallFixed[nTallFixed][0] = gA[i][0], tallFixed[nTallFixed][1] = gA[i][1], tallFixed[nTallFixed][2] = gA[i][2];
			++nTallFixed;
		}
	}
}

/* A copy's camera held so that its view (less the shake) is `target`: its
 * view is its camera's row plus the shake and its own pans' offsets, which
 * depend on where its camera is, so its camera is held that much higher
 * (as they are now, and as they moved last frame) */
static uint32_t twin_hold(Twin* tw, int target, int step) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	uint32_t T = emu_read32(&tw->e, L->state);
	if (T < 0x02000000 || T >= 0x02040000) return 0;
	int shake = (int8_t) emu_read8(&tw->e, T + L->shake);
	int own = (int16_t) emu_read16(&tw->e, L->view) - (int16_t) emu_read16(&tw->e, T + L->camRow) - shake;
	int row = target - own - step;
	emu_write32(&tw->e, T + L->cam, (uint32_t) row << 8);
	emu_write16(&tw->e, T + L->camRow, (uint16_t) row);
	return T;
}

/* (carry) the game's queue of copies at the next vblank: its entries into
 * the copy's BG tiles left out, the copy's own put in. Entries of qStride
 * bytes (12 by default: Sonic Pinball Party's destination, source, size;
 * Muppet Pinball Mayhem's 16: source, destination, size, kind), the count a
 * byte (or a word: qWide), from slot 0 (or a ring of qSlots from the start
 * index at qIndex) */
#define CARRY_Q 64
static int carry_q_stride(const typeof(twinProf->tall.live.carry)* C) { return C->qStride ? C->qStride : 12; }
static int carry_q_count(const typeof(twinProf->tall.live.carry)* C, Emu* e) {
	int n = C->qWide ? (int) emu_read32(e, C->qCount) : emu_read8(e, C->qCount);
	return n < 0 ? 0 : n > CARRY_Q ? CARRY_Q : n;
}
static void carry_q_set_count(const typeof(twinProf->tall.live.carry)* C, Emu* e, int n) {
	if (C->qWide) emu_write32(e, C->qCount, (uint32_t) n);
	else emu_write8(e, C->qCount, (uint8_t) n);
}
/* the address of the queue's i-th entry, from its start */
static uint32_t carry_q_at(const typeof(twinProf->tall.live.carry)* C, Emu* e, int i) {
	int slot = i;
	if (C->qIndex) {
		int slots = C->qSlots ? C->qSlots : CARRY_Q;
		slot = (int) ((emu_read32(e, C->qIndex) + (uint32_t) i) % (uint32_t) slots);
	}
	return C->qEntries + (uint32_t) (slot * carry_q_stride(C));
}
static bool carry_ring(const typeof(twinProf->tall.live.carry)* C, uint32_t d) {
	return d >= 0x06000000 + C->ringLo && d < 0x06000000 + C->ringHi;
}
static bool carry_q_ring(const typeof(twinProf->tall.live.carry)* C, Emu* e, uint32_t at) {
	return carry_ring(C, emu_read32(e, at + (uint32_t) C->qDst));
}
/* (carry) the game's queue holds more than a row's copy into the ring (the
 * whole table's tiles loaded afresh) */
static bool carry_reload(Emu* e) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	if (!C->qCount) return false;
	int n = carry_q_count(C, e);
	uint32_t big = C->qReload ? (uint32_t) C->qReload : 0x400;
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (carry_q_ring(C, e, at) && emu_read32(e, at + (uint32_t) (C->qSize ? C->qSize : 8)) > big) return true;
	}
	return false;
}
static void carry_put(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	Emu* e = &tw->e;
	emu_vram_write(e, C->ringLo, tw->ring, C->ringHi - C->ringLo);
	for (int k = 0; k < 4 && C->vars[k].addr; ++k)
		for (int j = 0; j < C->vars[k].n; ++j) emu_write8(e, C->vars[k].addr + j, tw->vars[k][j]);
	if (!C->qCount) return;
	int n = carry_q_count(C, e), m = 0, st = carry_q_stride(C);
	/* (the game's other entries kept, in order, then the copy's own; read
	 * them all first: they're written back from the same start) */
	static uint32_t keep[2][CARRY_Q][4];
	uint32_t (*kp)[4] = keep[tw == &twins[0] ? 0 : 1];
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (carry_q_ring(C, e, at)) continue;
		for (int j = 0; j < st / 4; ++j) kp[m][j] = emu_read32(e, at + (uint32_t) j * 4);
		++m;
	}
	for (int i = 0; i < m; ++i)
		for (int j = 0; j < st / 4; ++j) emu_write32(e, carry_q_at(C, e, i) + (uint32_t) j * 4, kp[i][j]);
	for (int i = 0; i < tw->nq && m < CARRY_Q; ++i, ++m)
		for (int j = 0; j < st / 4; ++j) emu_write32(e, carry_q_at(C, e, m) + (uint32_t) j * 4, tw->q[i][j]);
	carry_q_set_count(C, e, m);
}
static void carry_take(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	Emu* e = &tw->e;
	memcpy(tw->ring, (const uint8_t*) emu_vram(e) + C->ringLo, C->ringHi - C->ringLo);
	for (int k = 0; k < 4 && C->vars[k].addr; ++k)
		for (int j = 0; j < C->vars[k].n; ++j) tw->vars[k][j] = emu_read8(e, C->vars[k].addr + j);
	tw->nq = 0;
	int n = C->qCount ? carry_q_count(C, e) : 0, st = carry_q_stride(C);
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (!carry_q_ring(C, e, at)) continue;
		for (int j = 0; j < st / 4; ++j) tw->q[tw->nq][j] = emu_read32(e, at + (uint32_t) j * 4);
		++tw->nq;
	}
	tw->carried = true;
}
/* (carry) the copy's first frame: taken from the game, its own tiles kept,
 * its camera walked toward its row (up to 16 rows a frame: the game
 * streams two tile rows a frame at most) */
static void twin_first_carry(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	/* (the layers before the state: a change takes effect as the display
	 * registers are written, which loading a state does) */
	emu_layers(&tw->e, (twinNoObj ? 0x0F : 0x1F) & ~twinProf->tall.hudBg);
	if (!emu_state_load(&tw->e, twinStates[twinCur])) return;
	if (tw->seq + 1 != twinSeq) tw->carried = false; /* (not run last frame, or a load since: its tiles are stale) */
	if (tw->carried && carry_reload(&tw->e)) {
		/* the game is loading its table's tiles afresh (a table or a ball
		 * starting): what the copy kept is from before; it starts again
		 * from the game's */
		tw->carried = false;
		if (tallTrace) fprintf(stderr, "copy %d: the table's tiles reloaded, starting again\n", (int) (tw - twins));
	}
	tw->seq = twinSeq;
	if (C->ready.addr && !memprobes_match(&C->ready, 1, &tw->e)) tw->carried = false;
	if (tw->carried) carry_put(tw);
	/* (its own shadow sprite table: a frame whose logic runs long leaves
	 * part of it as it was, which would be the game's, placed for the
	 * game's view, and copied to the sprite table at the vblank all the
	 * same) */
	if (tw->carried) for (int i = 0; i < 1024; i += 4) emu_write32(&tw->e, twinProf->tall.live.oam + i, (uint32_t) tw->shadow[i] | tw->shadow[i + 1] << 8 | tw->shadow[i + 2] << 16 | (uint32_t) tw->shadow[i + 3] << 24);
	/* (from where its view is, not where its camera was put: the game
	 * moves the view only when its camera code runs, which it doesn't in
	 * a table's intro, and streams at most two tile rows a frame: a view
	 * jumping further leaves its tiles behind for good) */
	int at = tall_view(twinProf, &tw->e) - tall_shake(twinProf, &tw->e) + C->camOff;
	int want = tw->row + C->camOff;
	tw->cam = want > at ? SDL_min(at + 16, want) : SDL_max(at - 16, want);

	emu_write16(&tw->e, C->cam, (uint16_t) tw->cam);
	bool fresh = !tw->carried;
	/* (not drawn: only its second frame's picture is shown) */
	emu_frame_blind(&tw->e, twinKeys[twinCur] & 0x3FF);
	carry_take(tw);
	/* (starting again: its own sprites are the ones it just drew, not
	 * what it kept from when it last ran, on another table or before a
	 * state was loaded) */
	if (fresh) emu_oam_read(&tw->e, tw->oam);
	tw->up = true;
}

/* Before the game's frame: the frame before again, from the game's state
 * then, held at its row (which gives it this frame's sprites and scroll) */
static void twin_first(void* arg) {
	Twin* tw = arg;
	if (tall_carry(twinProf)) {
		tw->up = false;
		twin_first_carry(tw);
		return;
	}
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	tw->up = false;
	/* (its pans' step is guessed from last frame's; when the game's view
	 * starts or stops moving (the ball let go from a trap) the guess is a
	 * frame out, the copy's picture a few rows off its place and the rows
	 * between the screens wrong: then the frame is run again with the step
	 * it really took) */
	for (int again = 0; again < 2; ++again) {
		if (!emu_state_load(&tw->e, twinStates[twinCur])) return;
		uint32_t T = twin_hold(tw, tw->row, tw->panStep);
		if (!T) return;
		twin_jump(&tw->e, T, tw->row + (int8_t) emu_read8(&tw->e, T + L->shake));
		/* not drawn: its picture is never shown (twin_second's is), and
		 * drawing is about half of a frame's work, on the frame's critical
		 * path (twins[1]'s runs before the game's frame, on its thread) */
		emu_frame_blind(&tw->e, twinKeys[twinCur] & 0x3FF);
		/* (how far its pans moved it: put right) */
		int at = (int16_t) emu_read16(&tw->e, L->view) - (int8_t) emu_read8(&tw->e, T + L->shake);
		int miss = at - tw->row;
		tw->panStep += miss;
		if (tw->panStep > 8 || tw->panStep < -8) tw->panStep = 0;
		if (!miss) break;
		if (tallTrace || debug) fprintf(stderr, "copy %d: %d row(s) off, run again\n", (int) (tw - twins), miss);
	}
	tw->up = true;
}
/* With the game's frame: this frame, whose picture it shows */
static uint32_t twinFrameKeys;
static void twin_second(void* arg) {
	Twin* tw = arg;
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	/* (carry: its own sprites as it last drew them, not the game's. The
	 * game copies its sprites to the sprite table at a vblank only once
	 * its frame's logic is done; after a frame whose logic ran long it
	 * shows its last frame's sprites again, and so would a copy, but the
	 * last ones a copy has (from the game's state) are the game's, placed
	 * for the game's view) */
	if (tall_carry(twinProf) && tw->carried) emu_oam_write(&tw->e, tw->oam);
	emu_frame(&tw->e, twinFrameKeys & 0x3FF);
	memcpy(tw->pic, tw->e.fb, sizeof(tw->pic));
	if (tall_carry(twinProf)) {
		emu_oam_read(&tw->e, tw->oam);
		for (int i = 0; i < 1024; ++i) tw->shadow[i] = emu_read8(&tw->e, twinProf->tall.live.oam + i);
	}
	tw->picRow = emu_io16(&tw->e, 0x10 + 4 * L->viewBg + 2) & 0x1FF;
	if (tw->picRow >= tall_rows_of(twinProf, &tw->e)) tw->picRow -= 512;
	/* (carry: a copy still walking to its row isn't shown) */
	int off = abs(tw->picRow - tall_shake(twinProf, &tw->e) - tw->row);
	tw->havePic = !tall_carry(twinProf) || off == 0 || (tw->havePic && off <= 16);
}

/* After this frame of the game: whether the copies run next frame, and
 * where they're held for its picture: twins[0] at rows - 320, twins[1] 160
 * rows below the game's view (its pans included; less the shake k, which
 * the copies add alike), or below rows - 320 once the game's is further
 * down. The game itself is never changed. */
static void twin_step(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	if (tall_carry(twinProf)) {
		/* (the same, for a game whose camera is a row and whose table's
		 * height is known from the game) */
		int k = tall_shake(twinProf, &emu);
		twinShakeNow = twinShakeNext, twinShakeNext = k;
		int top = tall_top_of(twinProf), vmax = tall_rows_of(twinProf, &emu) - 2 * GBA_H;
		int u = tall_view(twinProf, &emu) - k;
		twins[0].row = vmax, twins[1].row = SDL_max(SDL_min(u, vmax), top) + GBA_H;
		/* (both always: a copy walking to its row from the game's isn't
		 * shown for a few frames, so the top screen's copy is kept at its
		 * row while the game itself is there too) */
		twins[0].need = twins[1].need = true;
		/* (and for two frames after: the game's first frame after the sweep
		 * still shows the sprites where the sweep moved them) */
		static int noObjHold;
		if (L->carry.noObj.addr && memprobes_match(&L->carry.noObj, 1, &emu)) noObjHold = 3;
		twinNoObj = noObjHold > 0;
		if (noObjHold > 0) --noObjHold;
		twinRun = true;
		return;
	}
	uint32_t S = emu_read32(&emu, L->state);
	twinRun = false;
	/* (states saved by v1.3.2's earlier test builds have the camera limit
	 * those lowered to 186 for the table: put back) */
	if (emu_read16(&emu, L->bound) == 186) emu_write16(&emu, L->bound, L->boundFrom);
	if (S < 0x02000000 || S >= 0x02040000 || emu_read16(&emu, L->bound) != L->boundFrom) {
		twinArmed = 0; /* not on a table we know */
		return;
	}
	twinArmed = memprobes_match(L->idle, 2, &emu) ? twinArmed + 1 : 0;
	int32_t ax, ay;
	/* (no ball: the same. A couple of frames after the sweep: the copies are
	 * taken from the game every frame, so there's nothing to wait for) */
	if (twinArmed < 2 || !twin_ball(&emu, S, &ax, &ay)) return;
	int k = (int8_t) emu_read8(&emu, S + L->shake);
	twinShakeNow = twinShakeNext, twinShakeNext = k;
	int u = (int16_t) emu_read16(&emu, L->view) - k, vmax = twinProf->tall.rows - 2 * GBA_H;
	int want[TWINS] = { vmax, SDL_max(SDL_min(u, vmax), 0) + GBA_H };
	/* (PP-11: the game's whole screen is on the bottom one; the top screen
	 * shows the rows just above it, from twins[0] held there, when the
	 * table has them) */
	bool above = tallMsg && u - GBA_H >= 0;
	if (above) want[0] = u - GBA_H;
	for (int i = 0; i < TWINS; ++i) {
		if (twins[i].row != want[i] && !twins[i].up) twins[i].panStep = 0;
		twins[i].row = want[i];
	}
	/* (in the table's upper part the game itself is on the top screen:
	 * twins[0] isn't needed, and since the copies are taken from the game
	 * every frame, it's back at once when it is) */
	twins[0].need = u > vmax || above, twins[1].need = true;
	twinRun = true;
}

static void twin_close(void) {
	twin_work_stop();
	for (int i = 0; i < TWINS; ++i) {
		if (twinOpen) emu_close(&twins[i].e);
		twins[i].up = twins[i].havePic = false;
		twins[i].carried = false;
		free(twins[i].ring), twins[i].ring = NULL;
	}
	twinOpen = twinRun = false;
	for (int i = 0; i < 2; ++i) free(twinStates[i]), twinStates[i] = NULL;
	twinHave = 0;
}

static void twin_open(const char* rom, const MapProfile* p) {
	twin_close();
	if (!p || (!p->tall.live.state && !tall_carry(p))) return;
	if (tall_carry(p)) {
		for (int i = 0; i < TWINS; ++i) {
			twins[i].ring = malloc(p->tall.live.carry.ringHi - p->tall.live.carry.ringLo);
			if (!twins[i].ring) return;
		}
	}
	size_t sz = emu_state_size(&emu);
	twinStates[0] = malloc(sz), twinStates[1] = malloc(sz);
	int opened = 0;
	while (twinStates[0] && twinStates[1] && opened < TWINS && emu_open(&twins[opened].e, rom, NULL, false)) ++opened;
	if (opened < TWINS) {
		for (int i = 0; i < opened; ++i) emu_close(&twins[i].e);
		for (int i = 0; i < 2; ++i) free(twinStates[i]), twinStates[i] = NULL;
		return;
	}
	/* The camera's stores (a dump of its own: its Rev's set, by CRC32) are Thumb
	 * STR / STRH with an immediate offset (top bits 6 / 8). A ROM whose code is
	 * elsewhere (an untried dump of the release) has something else there: no
	 * copies (the logo on the other screen), not a copy with its code broken. */
	const uint32_t* nops = p->tall.live.rev.crc && p->tall.live.rev.crc == emu.romCrc ? p->tall.live.rev.nop : p->tall.live.nop;
	for (int i = 0; i < 4 && nops[i]; ++i) {
		unsigned op = emu_read16(&twins[0].e, nops[i]) >> 12;
		if (op == 6 || op == 8) continue;
		fprintf(stderr, "live view: not this ROM's camera store at %08x (%04x), copies off\n", nops[i], emu_read16(&twins[0].e, nops[i]));
		for (int j = 0; j < opened; ++j) emu_close(&twins[j].e);
		for (int j = 0; j < 2; ++j) free(twinStates[j]), twinStates[j] = NULL;
		return;
	}
	for (int k = 0; k < TWINS; ++k) {
		for (int i = 0; i < 4 && nops[i]; ++i) emu_write16(&twins[k].e, nops[i], 0x46C0); /* mov r8, r8 */
		for (int i = 0; i < 16 && p->tall.live.carry.patch[i].addr; ++i) emu_write16(&twins[k].e, p->tall.live.carry.patch[i].addr, p->tall.live.carry.patch[i].v);
		emu_layers(&twins[k].e, 0x1F & ~p->tall.hudBg); /* (its score bar's layers: fixed to the screen, the game's are shown) */
	}
	twinProf = p;
	twinOpen = true;
	tallTrace = getenv("GBADS_TALL_TRACE") != NULL;
	fprintf(stderr, "live view: two more copies of the game for the table's other rows\n");
}

/* a profile's hideSprites switched by its hideWhen (NULL: none, or always on) */
static const MapProfile* hideWhenProf;
static int hideWhenN;
static bool hideWhenOn;

static void run_frame(uint32_t keys) {
	frameKeys = keys;
	unsigned bugAt = bugFrame & (BUG_LOG - 1);
	bugKeys[bugAt] = keys & 0x3FF;
	bugPoke[bugAt] = 0;
	uint32_t want = keys & (KX_PAN_L | KX_PAN_R);
	if (want && ctlProf && ctlProf->controls.snes.pan) {
		const typeof(ctlProf->controls.snes)* sn = &ctlProf->controls.snes;
		if (memprobes_match(sn->when, 3, &emu)) {
			emu_write8(&emu, sn->pan, want & KX_PAN_L ? 2 : 1);
			bugPoke[bugAt] = want & KX_PAN_L ? 2 : 1;
			static bool told;
			if (!told) told = true, fprintf(stderr, "camera pan: first pan (%s, game mode %02x)\n", want & KX_PAN_L ? "left" : "right", emu_read8(&emu, sn->when[0].addr));
		} else {
			static bool told; /* (for reports: once) */
			if (!told) told = true, fprintf(stderr, "camera pan: not now (game mode %02x, paused %02x)\n", emu_read8(&emu, sn->when[0].addr), emu_read8(&emu, sn->when[2].addr));
		}
	}
	bool live = twinOpen && tallOn;
	bool run = live && twinRun && twinHave > 0;
	++twinSeq;
	if (!run) for (int i = 0; i < TWINS; ++i) twins[i].up = false;
	nTallFixed = 0;
	if (run) {
		/* the copies' first frame (the game's frame before again, at their
		 * own cameras), then which of their sprites to leave out, and the
		 * game's fixed to the screen, for the HUD split to draw */
		game_oam_take();
		for (int i = 0; i < TWINS; ++i) twins[i].up = false;
		if (twins[0].need) twin_work_start(0, twin_first, &twins[0]);
		twin_first(&twins[1]);
		if (twins[0].need) twin_work_wait(0);
		if (!twinProf->tall.live.carry.keepObj)
			for (int i = 0; i < TWINS; ++i) if (twins[i].up) twin_hide(&twins[i]);
		/* (only while a copy's rows are on the top screen this frame) */
		bool topCopy = tall_view(twinProf, &emu) - tall_shake(twinProf, &emu) > tall_rows_of(twinProf, &emu) - 2 * GBA_H;
		emu_hud_keep_exact(tallFixed, topCopy ? nTallFixed : 0);
		/* (carry: and left out of the game's picture, which can be on the
		 * bottom screen) */
		if (tall_carry(twinProf)) {
			nTallBandFixed = 0;
			/* (whichever is on the top screen: the game's own picture is
			 * laid over by the boxes too, which would cover a sprite drawn
			 * on them, such as the pause menu's arrow) */
			for (int i = 0; i < nTallFixed; ++i) {
				int y = tallFixed[i][0] & 0xFF, h = obj_height(tallFixed[i][0], tallFixed[i][1]);
				if (y >= GBA_H) y -= 256;
				if (tallBandY0 >= 0 && y < tallBandY1 + 1 && y + h > tallBandY0) memcpy(tallBandFixed[nTallBandFixed++], tallFixed[i], sizeof(tallFixed[i]));
			}
			emu_hud_keep_exact(tallBandFixed, nTallBandFixed);
			emu_hide_exact(&emu, tallBandFixed, nTallBandFixed);
		}
	} else if (live) {
		emu_hud_keep_exact(tallFixed, 0);
		if (tall_carry(twinProf)) emu_hide_exact(&emu, tallFixed, 0);
	}
	if (live) {
		/* the game's state from before this frame, for next frame's copies */
		twinCur ^= 1;
		if (emu_state_save(&emu, twinStates[twinCur])) twinKeys[twinCur] = keys, twinHave = 1;
		else twinHave = 0;
	}
	/* the copies' second frame (this one), alongside the game's */
	twinFrameKeys = keys;
	for (int i = 0; i < TWINS; ++i) if (twins[i].up) twin_work_start(i, twin_second, &twins[i]);
	emu_frame(&emu, keys & 0x3FF);
	++bugFrame;
	for (int i = 0; i < TWINS; ++i) if (twins[i].up) twin_work_wait(i);
	if (live) twin_step();
}

/* Start the bug log again from here (the frames before belong to another moment). */
static void bug_reset(void) {
	bugStart = bugFrame;
	/* (the live view's copies: what they kept is from before; profile tall) */
	twinSeq += 2;
	for (int i = 0; i < BUG_SNAPS; ++i) bugSnap[i].have = false;
}

/* Between frames (the worker idle): a state a second for the bug log. */
static void bug_tick(void) {
	if (!emu.core) return;
	int oldest = 0;
	uint64_t newest = 0;
	bool any = false;
	for (int i = 0; i < BUG_SNAPS; ++i) {
		if (!bugSnap[i].have) {
			oldest = i;
			continue;
		}
		if (!any || bugSnap[i].frame > newest) newest = bugSnap[i].frame;
		any = true;
		if (bugSnap[oldest].have && bugSnap[i].frame < bugSnap[oldest].frame) oldest = i;
	}
	if (any && bugFrame < newest + BUG_EVERY) return;
	if (any && bugFrame == newest) return;
	if (!bugSnap[oldest].st) bugSnap[oldest].st = malloc(emu_state_size(&emu));
	if (!bugSnap[oldest].st) return;
	bugSnap[oldest].have = emu_state_save(&emu, bugSnap[oldest].st);
	bugSnap[oldest].frame = bugFrame;
}

static void* ahead_worker(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	int nice = android_raise_thread();
#else
	int nice = 0;
#endif
	fprintf(stderr, "frame buffer: emulation thread started%s\n", nice ? " (priority raised)" : "");
	pthread_mutex_lock(&ahead.lock);
	for (;;) {
		while (!ahead.busy && !ahead.quit) pthread_cond_wait(&ahead.cond, &ahead.lock);
		if (ahead.quit) break;
		uint32_t keys = ahead.keys;
		pthread_mutex_unlock(&ahead.lock);
		run_frame(keys);
		pthread_mutex_lock(&ahead.lock);
		ahead.busy = false;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return NULL;
}

/* false: no thread (emulate inline instead) */
static bool ahead_start(uint32_t keys) {
	pthread_mutex_lock(&ahead.lock);
	if (!ahead.started) {
		ahead.quit = false;
		ahead.started = pthread_create(&ahead.thread, NULL, ahead_worker, NULL) == 0;
	}
	bool ok = ahead.started;
	if (ok) {
		ahead.keys = keys;
		ahead.busy = true;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return ok;
}

/* Wait for the frame being emulated, if any (then the core is ours again). */
static void ahead_sync(void) {
	if (!aheadInFlight) return;
	pthread_mutex_lock(&ahead.lock);
	while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
	pthread_mutex_unlock(&ahead.lock);
	aheadInFlight = false;
	aheadReady = true;
}

static void ahead_stop(void) {
	ahead_sync();
	aheadReady = false;
	pthread_mutex_lock(&ahead.lock);
	bool started = ahead.started;
	ahead.quit = true;
	pthread_cond_broadcast(&ahead.cond);
	pthread_mutex_unlock(&ahead.lock);
	if (started) pthread_join(ahead.thread, NULL);
	ahead.started = false;
}
static MapCapture* mc;
static const MapProfile* pickProf; /* a compilation's profile (pick), or NULL */
static char subCode[5];          /* the profile in use: the game's code, or what pickProf picked */
/* what the ROM is, by its CRC32 (the log, bug reports) */
static const char* dump_text(char* buf, size_t n) {
	const char* name;
	switch (profile_dump(emu.gameCode, emu.romCrc, &name)) {
	case DUMP_TESTED: snprintf(buf, n, "CRC32 %08x, the dump GBA DS is tested on (%s)", emu.romCrc, name); break;
	case DUMP_OFFICIAL: snprintf(buf, n, "CRC32 %08x, an official dump GBA DS isn't tested on (%s)", emu.romCrc, name); break;
	case DUMP_MOD_TESTED: snprintf(buf, n, "CRC32 %08x, a mod GBA DS is tested on (%s)", emu.romCrc, name); break;
	case DUMP_MOD: snprintf(buf, n, "CRC32 %08x, a known mod GBA DS isn't tested on (%s)", emu.romCrc, name); break;
	case DUMP_MODIFIED:
		snprintf(buf, n, "CRC32 %08x, not a known dump of %.4s: modified (a patch or hack), trimmed or a bad dump", emu.romCrc,
		         emu.gameCode);
		break;
	default: snprintf(buf, n, "CRC32 %08x, not checked (no list of %.4s's dumps)", emu.romCrc, emu.gameCode); break;
	}
	/* a patch file beside the ROM (GEN-9): which, and what the file was before it */
	size_t l = strlen(buf);
	if (emu.patch == PATCH_APPLIED || emu.patch == PATCH_PARTIAL) {
		const char* fname = NULL;
		DumpKind fk = profile_dump(emu.gameCode, emu.fileCrc, &fname);
		snprintf(buf + l, n - l, "; patched as it started with %s, the ROM file itself CRC32 %08x (%s)", emu.patchName,
		         emu.fileCrc,
		         fk == DUMP_TESTED     ? "the tested dump"
		         : fk == DUMP_OFFICIAL ? "an official dump"
		         : fk == DUMP_MODIFIED ? "not a known dump"
		                               : "a known mod");
		l = strlen(buf);
		if (emu.patch == PATCH_PARTIAL)
			snprintf(buf + l, n - l, "; not applied: %s (made for another ROM, or for the ROM before the patches above it, or can't be read)",
			         emu.patchSkipped);
	} else if (emu.patch == PATCH_WRONG_ROM) {
		snprintf(buf + l, n - l, "; %s beside it was made for another ROM, not applied", emu.patchName);
	} else if (emu.patch == PATCH_UNREADABLE) {
		snprintf(buf + l, n - l, "; %s beside it can't be read, not applied", emu.patchName);
	}
	return buf;
}
static GamePaths gp;

static SDL_Texture* gameTex[2];
static SDL_Texture* mapTex[2];
/* profile.combat: in a fight, the second screen is the map's background
 * with the logo small and dimmed (combatTex, mapW x mapH, made once) */
static const MapProfile* combatProf;
static SDL_Texture* combatTex[2];
static bool combatOn, combatMade;
static uint32_t* combatPx; /* its pixels */
static bool combatInGame; /* (hud.logoUnder) the HUD's first probe matches: in a chapter */
static SDL_Texture* hudTex[2]; /* the HUD's second screen (HUD_CW x HUD_CH) */
static SDL_Texture* logoTex[2];
/* the logo centred in a GBA-sized picture, so it's scaled (and gridded)
 * like the game; NULL for a user's high-resolution logo */
static SDL_Texture* logoCanvasTex[2];
/* user overlay PNGs: [0] top screen, [1] bottom (load_overlay) */
static SDL_Texture* overlayTex[2][2];
static int overlayW[2], overlayH[2];
static char overlayMode[2][32];
/* GBA DS's own bezel (draw_bezel, bezel.c): [0] top screen, [1] bottom,
 * made again when anything it's drawn from changes */
static SDL_Texture* bezelTex[2][2];
static int bezelW[2], bezelH[2];
static char bezelKey[2][96];
static int logoW, logoH;
static bool logoPixel;
static int logoSub = 1; /* logoTex's pixels per GBA pixel (LOGO_SUB: a scraped logo at full resolution) */
static bool logoProvisional; /* a scraped logo sized before the extracted one existed (GEN-11) */
static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
/* "Map Animations: Off" shows this composed still instead of mapRes.frames[] */
#define STILL_IDX MAP_MAX_FRAMES
static uint32_t* stillPx;        /* mapW * mapH */
static int stillW, stillH;
static bool stillValid;
/* The map button did nothing at the last attempt (A Link to the Past indoors
 * without a map): keep showing the last map, dimmed. */
static bool mapInactive;
static bool logoOnly; /* the game has a profile but no map: logo on the second screen */
static const MapProfile* snapProf; /* logo-only game with screens copied to the second screen */
static const MapProfile* tallProf; /* logo-only game with a table over both screens (profile.tall) */
static uint32_t tallTop[GBA_W * GBA_H], tallBot[GBA_W * GBA_H];
static bool tallLiveAtStart; /* pinball_live as this game started with it: a change counts only after a new start */
static bool tallLive;      /* the live view: the table over both screens (update_tall) */
/* Mario Kart: the race HUD on the second screen (profile .hud) */
static const MapProfile* hudProf;
static uint32_t hudPx[GBA_W * GBA_H];
static bool hudOn; /* the frame being emulated is split */
/* the second screen's picture: the HUD at 2x over the backdrop, its map
 * larger; the backdrop, made from the grabbed layers as each race starts */
#define HUD_CW (GBA_W * 2)
#define HUD_CH (GBA_H * 2)
static uint32_t hudCanvas[HUD_CW * HUD_CH], hudBack[HUD_CW * HUD_CH], hudGrab[GBA_W * GBA_H];
static bool hudBackValid;
static int hudFrames; /* split frames since the race (re)started */
static int mapX0, mapY0, mapX1, mapY1; /* the map's box on the HUD, measured over the first frames */
static bool mapFound;
static SpriteRule hudWorldObjs[4]; /* hud.world.objs, the same way */
static SpriteRule hudObjs[12]; /* hudProf's hud.objs, as used (see romPatch) */
static int hudFadeEff, hudFadeIn; /* the last full fade (2: white, 3: black); frames to fade the next backdrop in */
static long hudPalRef[8]; /* bg_pal_levels() as the backdrop was taken */
static unsigned hudKeep; /* BG layers kept on the game screen (FFTA's spells) */
static bool hudWorld; /* FFTA's world map (hud.world) */
static bool hudScene; /* the profile's other scene (Mario Kart's podium): logo backdrop, its own sprites */
static bool hudCard; /* hud.card: the title card's sprites are what's split off */
static int cardScore = -1; /* hud.card: how far off centre the best take so far is (-1: none) */
/* hud.card.hideTillTake: this card's take is made (the HUD over it from
 * then), and how long its start has been seen / not seen */
static bool cardTaken;
static int cardPreFor, cardAway = 1000;
static uint32_t cardBest[GBA_W * GBA_H];
static int sideScore = -1; /* hud.card.side: how far off centre its best take is (-1: none) */
static uint32_t sideBest[GBA_W * GBA_H];
static bool hudAlt; /* the HUD on its other layer (hud.alt: SMA2's boss rooms) */
static bool hudOffLatch; /* hud.offAfter has matched: off till on[0] stops matching */
static uint32_t hudLogoBack[HUD_CW * HUD_CH];
static bool hudLogoDone;
static bool hudShown; /* the second screen shows the HUD picture (hudTex) */
/* the profile's logoIf probes match: the logo, the map picture and HUD kept */
static bool logoForced;
/* logoIf just stopped matching: the logo still, until the second screen has
 * a new picture to show (a HUD picture drawn, a map picture taken), at
 * most a second; not the last picture from before for a frame (SM4-11) */
static int logoHolding;
/* profile hud.overMap: this frame's HUD is drawn into the map picture;
 * the map picture needs uploading again (the HUD changed) */
static bool overOn, overDirty;
static bool overAlone; /* a piece that goes alone is in the map picture (shown even in a fight) */
/* the HUD's pieces as last seen (hud.overMap): the pixels inside each
 * move's box, cut to what's there (0: nothing); latched pieces wait for
 * a new value at their latch address, then for the piece to hold still */
typedef struct {
	bool valid;
	int w, h;
	uint32_t px[GBA_W * GBA_H];
	uint32_t key;
	int cw, ch, cx, cy, still; /* a latched piece's size and place (a settling one's size), and for how long */
	uint32_t cand[GBA_W * GBA_H]; /* a settling piece's pixels */
	int gone; /* frames missing while the HUD's on (HudMove.dropAfter) */
	bool stale; /* a state was loaded: taken again (latched or not) as soon as it holds still */
	int sx, sy; /* where it was on the game screen (its top left) */
	bool atBottom; /* it reached the game screen's bottom edge */
} Piece;
static Piece pieces[4];
static void upload_map_frame(int idx);
static const uint32_t* map_px(int idx);
static const uint32_t* hud_over_map(const uint32_t* px);
static void make_combat_canvas(void);
static bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha);
static void logo_refresh(void);
static bool cardLogoMade; /* card_logo's picture is made */
static uint32_t hudLogoBelow[HUD_CW * HUD_CH]; /* profile hud.logoBelow: dark grey, the logo below the HUD */
/* profile hud.mapTitleRows: those rows of the last map picture, held over
 * the HUD (mapTitleN rows; 0: none taken yet) */
static uint32_t mapTitle[GBA_W * 32];
static int mapTitleN;
static bool hudLogoBelowDone;
static int snapShow, snapRevert;   /* consecutive frames matching show / a revert screen */
static uint32_t snapShown[GBA_W * GBA_H]; /* the copy on the second screen (kept with save states) */

static uint64_t mapAnimStart;
/* the contextual toggle (GEN-14, issue #37): where the pages' count starts
 * (0 with the toggle off: then nothing here is used), the picture's
 * brightness while a turn that waits fades (256: as it is), and whether
 * the capture under way is for a new subject (a room, a unit, a loaded
 * state: the pages start again at the first) */
static int pageOffset;
static int fadeLevel = 256;
static bool fadeUpload; /* the picture is uploaded again this frame for the fade, one picture or not */
static bool capSubject;
/* ONPRESS views (GEN-14): the other view is up (viewAlt), a capture of the view wanted is to be asked for now (pressCap), how many captures have come in (capDone), and which one the press waits for (tgCapWant; 0: not asked for yet) */
static bool viewAlt, pressCap, viewWait;
static unsigned capDone, tgCapWant;
/* which of the second screen's pictures shows now: the map's frames turn
 * every frameStep frames from mapAnimStart (a marker's blinking, or pages:
 * mapRes.pages); never out of range, even with mapAnimStart ahead of
 * frameCount (it once came from the game played before: frames[-1]).
 * The one place that says which page is up (the toggle, GEN-14, changes
 * it from here). */
static int second_page(void) {
	if (mapRes.nFrames < 1 || frameCount < mapAnimStart) return 0;
	const MapProfile* hp = mc ? mapcap_profile(mc) : NULL;
	if (hp && hp->holdPage) {
		int hold = hp->holdPage(&emu); /* (a page kept up: Pokémon's Party in a battle) */
		if (hold >= 0 && hold < mapRes.nFrames) return hold;
	}
	if (hp && hp->views) return cfg->toggle ? pageOffset % mapRes.nFrames : 0; /* (views: the first until the toggle turns to another; no timer) */
	bool by_hand = cfg->toggle && mapRes.pages;
	if (by_hand && !cfg->autoRotate) return pageOffset % mapRes.nFrames; /* (auto-rotate off: a page stays until you press) */
	return (int) ((((frameCount - mapAnimStart) / (uint64_t) SDL_max(1, mapRes.frameStep)) + (uint64_t) (by_hand ? pageOffset : 0)) % (uint64_t) mapRes.nFrames);
}
static bool singleShowsMap;
static double fps, speedPct;
static Uint64 emuTicks, drawTicks; /* time spent emulating / drawing, for the 5 s report */

/* Slow frames (work over a frame's time): the worst each second, broken down,
 * logged on Android (where frame time is tight) or with GBADS_DEBUG. */
typedef struct {
	double total, input, emulate, ra, map, draw, top, bottom;
	int redraws;

	unsigned long long frame;
	long preempted; /* times the thread was put off its core during the frame */
	int cpu, mhz;   /* where it ended, and that core's clock then */
	double cpuMs;   /* time the thread actually ran (the rest it waited) */
	double waitEmu, waitDraw; /* of the waiting: during emulation, during drawing */
	double audio;   /* of emulating: handing the sound to SDL (its lock is shared with the audio thread) */
} SlowFrame;
static SlowFrame slowWorst;
static int slowCount;

/* Stalls (issue #50): a loop iteration over STALL_MS from top to top, any
 * build, no GBADS_DEBUG needed. Each part of the iteration is timed, the
 * wait before the frame and the sleep after it included (slow frames above
 * only see the work in between), so one line in a player's log says where
 * the time went. At most STALL_LINES lines per 5 s; the 5 s report counts
 * them all. */
#define STALL_MS 100
#define STALL_LINES 4
typedef struct {
	Uint64 top, synced, work, emu0, emu1, draw0, draw1, sleep0, end;
	double sleepWant;  /* s */
	double audio;      /* ms handing sound to SDL */
	double presentTop, presentBottom;
	double cpuMs;      /* the thread's CPU time at the top */
	long preempt;      /* its involuntary switches at the top */
	int queueMs;       /* the sound queue at the top */
	unsigned long long frame;
	int kept;          /* frames the keeper ran during the present */
	bool valid;
} LoopMarks;
static LoopMarks loopCur;
static int stallCount, stallLogged;
static unsigned keptTotal; /* frames the keeper ran (the 5 s report) */
static double stallWorst;

static void slow_frame_note(SlowFrame f) {
	++slowCount;
	if (f.total > slowWorst.total) slowWorst = f;
}

static void slow_frame_report(bool debug) {
#ifndef __ANDROID__
	if (!debug) {
		slowCount = 0;
		slowWorst.total = 0;
		return;
	}
#endif
	if (!slowCount) return;
	const SlowFrame* w = &slowWorst;
	fprintf(stderr,
	        "slow frames: %d in the last second; worst (frame %llu) %.1f ms: input/menu %.1f, emulating %.1f (sound hand-off %.1f), "
	        "RetroAchievements %.1f, map %.1f, drawing %.1f (top screen %s %.1f, bottom %s %.1f); "
	        "ran %.1f ms of it (waited %.1f: %.1f while emulating, %.1f while drawing), preempted %ld time(s), on cpu %d at %d MHz\n",
	        slowCount, w->frame, w->total, w->input, w->emulate, w->audio, w->ra, w->map, w->draw,
	        w->redraws & 1 ? "redrawn" : "kept", w->top, w->redraws & 2 ? "redrawn" : "kept", w->bottom, w->cpuMs, w->total - w->cpuMs, w->waitEmu, w->waitDraw, w->preempted, w->cpu, w->mhz);
	slowCount = 0;
	slowWorst.total = 0;
}
static bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_CHEATS, M_PATCHES, M_ACH, M_SHOT, M_SWAP, M_SCALE, M_LCD, M_OVERLAY, M_FPS, M_SAVE, M_LOAD, M_RESTART, M_LIBRARY, M_EXIT, M_FRAMEBUF, M_LOGO, M_MATCH, M_REWIND, M_AUTOSAVE, M_AUTOLOAD, M_BUG, M_SLOT, M_SHAPES, M_FF, M_COLOR, M_GHOST, M_BUTTONS, M_PIXLOGO, M_PLACE };
static const char* menuLabels[] = {
	/* title case: drawn in Futura from the built-in images (tools/make_brand.py) */
	[M_RESUME] = N_("Resume"), [M_SWAP] = N_("Swap Screens"), [M_SAVE] = N_("Save State"), [M_SCALE] = "", [M_LCD] = "", [M_FPS] = "", [M_FRAMEBUF] = "", [M_OVERLAY] = "", [M_LOGO] = "", [M_MATCH] = "", [M_REWIND] = "", [M_RESTART] = N_("Restart Game"),
	[M_LOAD] = N_("Load State"), [M_CHEATS] = "Cheats", [M_PATCHES] = "Patches", [M_BUTTONS] = N_("Buttons"), [M_ACH] = "Achievements", [M_SHOT] = N_("Take Screenshot"), [M_LIBRARY] = N_("Close Game"), [M_EXIT] = N_("Close GBA DS"),
	[M_AUTOSAVE] = "", [M_AUTOLOAD] = "", [M_BUG] = N_("Submit a Bug Report"), [M_SLOT] = "", [M_FF] = "", [M_COLOR] = "", [M_GHOST] = "", [M_PIXLOGO] = "", [M_PLACE] = "",
};
static int menuItems[12], nMenuItems;
/* Some buttons are half as wide, two to a row (Save State | Load State):
 * each item's row and side (-1: the whole row, 0 left, 1 right). */
static int itemRow[12], itemSide[12], nMenuRows;
static bool half_item(int item) {
	return item == M_RESUME || item == M_RESTART || item == M_SAVE || item == M_LOAD || item == M_AUTOSAVE || item == M_AUTOLOAD || item == M_REWIND || item == M_FF ||
	       item == M_COLOR || item == M_GHOST || item == M_CHEATS || item == M_PATCHES || item == M_SHOT || item == M_BUG;
}
/* Close Game and Close GBA DS are double height: two rows tall, side by side
 * when both are there, else the one across the whole row */
static bool tall_item(int item) {
	return item == M_LIBRARY || item == M_EXIT;
}
static int item_span(int item) {
	return tall_item(item) ? 2 : 1;
}
/* rows, sides and the row count for a page's items (two half items in a row share it; a tall one takes two rows) */
static int layout_rows(const int* items, int n, int* row, int* side) {
	int rows = 0;
	for (int i = 0; i < n; ++i) {
		bool both = i + 1 < n && ((half_item(items[i]) && half_item(items[i + 1])) || (tall_item(items[i]) && tall_item(items[i + 1])));
		if (both) {
			row[i] = row[i + 1] = rows;
			rows += item_span(items[i]);
			side[i] = 0, side[i + 1] = 1;
			++i;
		} else {
			row[i] = rows;
			rows += item_span(items[i]);
			side[i] = -1;
		}
	}
	return rows;
}
/* Six pages, switched with L1/R1 or the tabs: Game holds buttons
 * (actions); the other five are rows of settings (docs/MENUS.md), the same
 * categories as the game list's Settings, each changed for this game (A) or
 * for all games (Y), kept in states/<game>.cfg or states/prefs.cfg. */
#define N_PAGES 6
enum { PG_GAME, PG_DISPLAY, PG_SCREENS, PG_CONTROLS, PG_SPEED, PG_MAP };
#define ICON_SYSTEM 100 /* a toolbox: Settings > System (not a page) */
static int pages[N_PAGES][12], pageLen[N_PAGES], curPage;
#define ROW_MAX 16 /* settings on a page */
#define ROW_VIS 8  /* most shown at once: more scroll */
static const char* rowKeys[N_PAGES][ROW_MAX]; /* a settings page's rows: the settings' keys, or "@name" for a button that opens something */
static int rowN[N_PAGES];
static int rowSel;     /* 0: the This game / All games bar; 1...: the rows */
static int rowTop;     /* the first row shown (0-based; the rows before it are scrolled off) */
static bool scopeAll;  /* the bar: changes go to all games */
static int rowPressed = -1;
static int rowDragY;       /* where a finger last moved the list from */
static bool rowDragged;    /* the finger scrolled the list: not a tap */
static bool page_is_rows(int p) {
	return p >= PG_DISPLAY;
}
/* Buttons still held when the menu closes (or after a HOME combo) are kept
 * from the game until released, so e.g. the A that picked "Save state"
 * doesn't make Samus jump. */
static bool suppress[B_COUNT];
#define HELD(b) (btnNow[b] && !suppress[b])
/* the hotkeys (hotkeys.c): the hotkey button, each action's button or -1 */
static int hkMod = B_HOME;
static int hkBtn[HK_COUNT];

static void suppress_held(void) {
	for (int b = 0; b < B_COUNT; ++b) suppress[b] = btnNow[b];
}
static const char* pageNames[N_PAGES] = { "Game", N_("Display"), "Screens", "Controls", N_("Speed"), N_("Map")};
/* Restart Game, Close Game and Close GBA DS ask first: chosen once, the
 * button says "Are You Sure?" (and the note what happens); chosen again,
 * it's done. Another page, another button or closing the menu calls it off. */
static int armedItem = -1;
static void rebuild_textures(void);
static void commit_current(const char* key);
static bool confirm_item(int item) {
	return item == M_RESTART || item == M_LIBRARY || item == M_EXIT;
}
static bool menuOpen;
static int menuSel;
static int menuPressed = -1;      /* item under a finger that went down in the menu */
static bool ignorePointerUp;      /* the tap that opened the menu */
static char menuNote[64];
static bool pacedFallback; /* Match Display Rate fell back to the GBA's rate (pacing_update) */

#define C_BTN ui_accent_color(AC_BTN)
#define C_BTN_SEL ui_accent_color(AC_BTN_SEL)
#define C_BTN_EDGE ui_accent_color(AC_EDGE)
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIS = { 100, 100, 120, 255 };
#define C_GOLD ui_accent_color(AC_HI)

static Cheats cheats;
static SDL_Rect tab_rect(int scr, int t);
static SDL_Rect ach_area(int scr);

/* Every label a pause-menu button can show, for sizing the text so the
 * widest fits (item_label gives the current one). */
static const char* const allLabels[] = {
	N_("Resume"), N_("Restart Game"), N_("Are You Sure?"), N_("Save State"), N_("Load State"), "Cheats", "Patches", "Achievements",
	N_("Take Screenshot"), N_("Close Game"), N_("Close GBA DS"), N_("Submit a Bug Report"),
	"\u2039   Slot 0: Sep 30, 12:59 PM   \u203A", /* (the slot row, with its arrows) */
};
/* the labels of the half-width buttons, which fit in half the width */
static const char* const halfLabels[] = {
	N_("Save State"), N_("Load State"), "Save State 0", "Load State 0", N_("Resume"), N_("Restart Game"), N_("Are You Sure?"),
	"Cheats", "Patches", N_("Take Screenshot"), N_("Submit a Bug Report"),
};

/* The pause menu's layout: buttons across the whole screen (a small margin
 * at the edges), all the same size on every page, as tall as the page with
 * the most buttons allows (with the page tabs above and a line for notes
 * below), and the text as big as the button height and the widest label
 * allow. */
typedef struct {
	int margin, w, h, gap; /* buttons: x = margin, width w, height h, gap between them */
	int tabY, top;         /* the tab row's y; the first button's y */
	int cap, tabCap, noteCap, pad, rad;
	int prompt;            /* L1 / R1 prompt width, beside the tabs */
} MenuGeom;

static MenuGeom menu_geom(int scr) {
	static MenuGeom cache;
	static int cw = -1, ch = -1, cn = -1;
	SDL_Rect r = screens[scr].rect;
	int n = 1;
	for (int p = 0; p < N_PAGES; ++p) {
		int row[12], side[12];
		n = SDL_max(n, page_is_rows(p) ? SDL_min(rowN[p], ROW_VIS) + 1 : SDL_min(layout_rows(pages[p], pageLen[p], row, side), ROW_VIS));
	}
	if (r.w == cw && r.h == ch && n == cn) return cache;
	MenuGeom g;
	float px = ui_px(scr);
	g.margin = px * 4;
	g.w = r.w - g.margin * 2;
	/* margin, tab row (h), gap (h * 0.4), n rows (h) with gaps (h * 0.2),
	 * the note line (h * 0.7), margin */
	g.h = (int) ((r.h - g.margin * 2) / (1.0 + 0.4 + n + 0.2 * (n - 1) + 0.7));
	g.gap = g.h / 5;
	g.tabY = g.margin;
	g.top = g.tabY + g.h + g.h * 2 / 5;
	g.pad = g.h / 3;
	g.rad = SDL_max(2, g.h / 5);
	/* text: capitals at most 40% of the button, and the widest label fits */
	int ref = 100, widest = 1;
	for (size_t i = 0; i < sizeof(allLabels) / sizeof(*allLabels); ++i)
		widest = SDL_max(widest, ui_label_width(scr, ref, allLabels[i]));
	g.cap = SDL_min(g.h * 2 / 5, (g.w - g.pad * 2) * ref / widest);
	widest = 1;
	for (size_t i = 0; i < sizeof(halfLabels) / sizeof(*halfLabels); ++i)
		widest = SDL_max(widest, ui_label_width(scr, ref, halfLabels[i]));
	for (int k = 0; k < 2; ++k) { /* (the slot buttons: "Save State 0", "Load State 0" in the language's words) */
		char b[96];
		snprintf(b, sizeof(b), k ? tr("Load State %d") : tr("Save State %d"), 0);
		widest = SDL_max(widest, ui_label_width(scr, ref, b));
	}
	g.cap = SDL_min(g.cap, ((g.w - g.gap) / 2 - g.pad * 2) * ref / widest);
	g.noteCap = g.cap * 4 / 5;
	/* the tabs share the row between the L1 / R1 prompts, each as wide as its name needs */
	g.prompt = g.h * 9 / 10;
	int tabsW = g.w - (g.prompt + g.gap) * 2 - g.gap * (N_PAGES - 1), names = 0;
	for (int t = 0; t < N_PAGES; ++t) names += ui_label_width(scr, ref, pageNames[t]);
	/* each tab: its name plus pad on both sides */
	g.tabCap = SDL_min(g.cap, (tabsW - g.pad * 2 * N_PAGES) * ref / SDL_max(1, names));
	g.rad = SDL_max(2, g.h / 5);
	cache = g, cw = r.w, ch = r.h, cn = n;
	return g;
}


/* The pause menu's save slot (0-9): Save State and Load State use it.
 * At start, the slot saved to last (or 0). */
static int stateSlot;
static void slot_pick_newest(void) {
	time_t best = 0;
	stateSlot = 0;
	for (int i = 0; i < 10; ++i) {
		struct stat st;
		if (stat(gp.slot[i], &st) == 0 && st.st_mtime > best) best = st.st_mtime, stateSlot = i;
	}
}
/* What's in a slot shared with RetroArch (issue #39), looked at again
 * only when the file changes */
static StateFileKind slot_kind(const struct stat* st, bool* fromGbads) {
	static struct {
		char path[1024];
		time_t time;
		off_t size;
		StateFileKind kind;
		bool gbads;
	} seen[10];
	int i = stateSlot;
	if (strcmp(seen[i].path, gp.slot[i]) || seen[i].time != st->st_mtime || seen[i].size != st->st_size) {
		snprintf(seen[i].path, sizeof(seen[i].path), "%s", gp.slot[i]);
		seen[i].time = st->st_mtime, seen[i].size = st->st_size;
		seen[i].kind = emu_state_file_kind(&emu, gp.slot[i], &seen[i].gbads);
	}
	*fromGbads = seen[i].gbads;
	return seen[i].kind;
}
/* "Slot 3: Oct 31, 1:45 PM" (when it was saved), or "Slot 3: Empty";
 * shared with RetroArch, also who saved it: "Slot 3: Oct 31, 1:45 PM, RetroArch",
 * "Slot 3: Another emulator's" */
static const char* slot_label(void) {
	static char buf[80];
	struct stat st;
	if (stat(gp.slot[stateSlot], &st) != 0) {
		snprintf(buf, sizeof(buf), tr("Slot %d: Empty"), stateSlot);
		return buf;
	}
	const char* by = "";
	if (gp.raDir[0]) {
		bool fromGbads;
		StateFileKind k = slot_kind(&st, &fromGbads);
		if (k == STATE_FILE_OTHER_CORE || k == STATE_FILE_UNREADABLE) {
			snprintf(buf, sizeof(buf), tr("Slot %d: %s"), stateSlot, k == STATE_FILE_OTHER_CORE ? tr("Another emulator's"): tr("Can't be read"));
			return buf;
		}
		if (!fromGbads) by = N_(", RetroArch");
	}
	struct tm t, now;
	time_t n = time(NULL);
	localtime_r(&st.st_mtime, &t);
	localtime_r(&n, &now);
	static const char* const mon[12] = { N_("Jan"), N_("Feb"), N_("Mar"), N_("Apr"), N_("May"), N_("Jun"), N_("Jul"), N_("Aug"), N_("Sep"), N_("Oct"), N_("Nov"), N_("Dec")};
	if (t.tm_year != now.tm_year) snprintf(buf, sizeof(buf), tr("Slot %d: %s %d, %d%s"), stateSlot, mon[t.tm_mon], t.tm_mday, t.tm_year + 1900, by);
	else snprintf(buf, sizeof(buf), tr("Slot %d: %s %d, %d:%02d %s%s"), stateSlot, mon[t.tm_mon], t.tm_mday, (t.tm_hour + 11) % 12 + 1, t.tm_min, t.tm_hour < 12 ? tr("AM"): tr("PM"), by);
	return buf;
}
static void slot_step(int d) {
	stateSlot = (stateSlot + d + 10) % 10;
	menuNote[0] = 0;
}

static bool item_enabled(int item) {
	if (item == M_CHEATS) return cheats.n > 0;
	if (item == M_SHAPES) return ui_screens_differ(); /* (two screens of a shape: nothing to match) */
	if (item == M_PLACE) return nScreens == 2;         /* (one screen: no gap) */
	if (item == M_FRAMEBUF) return !tallProf;          /* (always on in the pinball games' two-screen view) */
	return item != M_LOAD || file_exists(gp.slot[stateSlot]);
}

/* ---------------------------------------------------------------- cheats screen
 *
 * "Cheats" on the Game page opens a list of the game's cheats with an On/Off
 * pill each: D-pad / A to toggle, L/R a page at a time, B (or the Back box)
 * back to the menu. Touch: tap a cheat to toggle it, drag to scroll. */
static bool cheatsOpen;
/* "Achievements" (RetroAchievements signed in): the list from ra.c, under
 * the same Back / title boxes. D-pad scrolls, L/R a page, B back. */
static bool achOpen;
static int achPressY, achPressScroll;
static bool achPressed;
static int cheatSel;                 /* row index */
static int cheatScroll;              /* pixels */
static int cheatRows[CHEAT_MAX_FILES + 1024], nCheatRows; /* >= 0 cheat index, < 0 file header (-1 - file) */
static int cheatPressRow = -1, cheatPressY, cheatPressScroll;
static bool cheatDragging;

static void build_cheat_rows(void) {
	nCheatRows = 0;
	for (int f = 0; f < cheats.nFiles; ++f) {
		if (cheats.nFiles > 1) cheatRows[nCheatRows++] = -1 - f;
		for (int i = 0; i < cheats.n && nCheatRows < (int) SDL_arraysize(cheatRows); ++i) {
			if (cheats.items[i].file == f) cheatRows[nCheatRows++] = i;
		}
	}
}

typedef struct {
	float px;
	int tpx, top, rowH, gap, x, w, listH, footY;
} CheatGeom;

static CheatGeom cheat_geom(int scr) {
	CheatGeom g;
	SDL_Rect r = screens[scr].rect;
	g.px = ui_px(scr);
	g.tpx = SDL_max(2, g.px * 3 / 4);
	g.top = g.px * 26;
	g.rowH = g.tpx * 7 + g.px * 5;
	g.gap = g.px * 2;
	g.x = g.px * 4; /* the pause menu's margin */
	g.w = r.w - g.px * 8;
	g.footY = r.h - g.tpx * 9 * 2 - g.px * 5;
	g.listH = g.footY - g.px * 6 - g.top;
	return g;
}

static int cheat_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nCheatRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void cheat_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = cheatSel * (g.rowH + g.gap);
	if (y < cheatScroll) cheatScroll = y;
	if (y + g.rowH > cheatScroll + g.listH) cheatScroll = y + g.rowH - g.listH;
	cheatScroll = SDL_clamp(cheatScroll, 0, cheat_max_scroll(&g));
}

static void cheat_move(int dir) {
	int i = cheatSel;
	for (int k = 0; k < nCheatRows; ++k) {
		i += dir;
		if (i < 0 || i >= nCheatRows) return;
		if (cheatRows[i] >= 0) {
			cheatSel = i;
			cheat_keep_visible(ui_touch_screen());
			return;
		}
	}
}

static void open_cheats(void) {
	build_cheat_rows();
	cheatsOpen = true;
	cheatSel = 0;
	cheatScroll = 0;
	if (nCheatRows && cheatRows[0] < 0) cheat_move(1);
	cheatPressRow = -1;
}

static void toggle_cheat(int row) {
	if (row < 0 || row >= nCheatRows || cheatRows[row] < 0) return;
	Cheat* c = &cheats.items[cheatRows[row]];
	c->on = !c->on;
	cheats_apply(&cheats, &emu);
	cheats_save(&cheats);
}

/* row under screen-local y, or -1 */
static int cheat_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + cheatScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nCheatRows) return -1;
	return row;
}

/* fits s into maxChars, ending in "..." when cut */
/* s in Jost, cut short with "..." if it's wider than maxW. */
static void fit_text(char* out, size_t size, int font, int capPx, const char* s, int maxW) {
	snprintf(out, size, "%s", s);
	if (ui_font_width(font, capPx, out) <= maxW) return;
	size_t n = strlen(out);
	while (n > 0) {
		do --n; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80); /* whole UTF-8 characters */
		snprintf(out, size, "%.*s...", (int) n, s);
		if (ui_font_width(font, capPx, out) <= maxW) return;
	}
}

static void draw_cheats(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = menu_geom(scr).rad, cap = menu_geom(scr).cap;
	/* top: Back box (where the Game tab is) and the title */
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Cheats", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5; /* Jost capitals, about the pixel font's height */
	int pillW = ui_label_width(scr, tcap, "Off") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nCheatRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - cheatScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		char line[300];
		if (cheatRows[i] < 0) {
			const CheatFile* f = &cheats.files[-1 - cheatRows[i]];
			fit_text(line, sizeof(line), TF_BOLD, tcap, f->label, g.w - px * 4);
			ui_font_text(scr, TF_BOLD, g.x + px * 2, y + g.rowH / 2, tcap, line, C_GOLD, -1);
			continue;
		}
		const Cheat* c = &cheats.items[cheatRows[i]];
		bool sel = i == cheatSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		ui_key(scr, row, rad, sel ? KEY_SEL : KEY_PLAIN);
		fit_text(line, sizeof(line), TF_BODY, tcap, c->desc, nameW);
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, C_TEXT, -1);
		SDL_Rect pill = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		ui_fill_round(scr, pill, c->on ? (SDL_Color) { 64, 168, 112, 255 } : (SDL_Color) { 60, 56, 84, 255 }, px * 2);
		ui_label(scr, pill.x + pill.w / 2, pill.y + pill.h / 2, tcap, c->on ? "On" : "Off",
		         c->on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	/* scroll bar */
	int maxS = cheat_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * cheatScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: the selected cheat's whole name when the row had to cut it */
	if (cheatSel >= 0 && cheatSel < nCheatRows && cheatRows[cheatSel] >= 0 &&
	    ui_font_width(TF_BODY, tcap, cheats.items[cheatRows[cheatSel]].desc) > nameW) {
		ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, cheats.items[cheatRows[cheatSel]].desc,
		             C_GOLD);
	}
	char count[48];
	snprintf(count, sizeof(count), tr("%d on"), cheats_on(&cheats));
	ui_font_text(scr, TF_BODY, r.w - px * 4, r.h - g.tpx * 5, tcap, count, C_DIS, 1);
}

/* ---------------------------------------------------------------- patches screen (GEN-9)
 *
 * "Patches" on the Extras page: the game's patches folder's files with an
 * On/Off pill each, the file beside the ROM (always on, applied first), and
 * two switches for the patched game's own battery save and states. The ones
 * on come first, numbered in the order they're applied; X (or a tap on the
 * number) picks one up to move it. Switching a patch asks first: A again
 * restarts the game with the change, Y keeps it for later, B calls it off;
 * while the choice isn't what the game started with, a "Restart" row at the
 * top does it. Each patch is tried on the ROM as the list opens, and the
 * ones on are tried together, in order, so the list can say which can't
 * follow which (a UPS, BPS or xdelta is made for the unchanged ROM) and which
 * change the same bytes. */
static const char* romPath; /* (the game's ROM; set by game_run) */
static PatchChoice patchChoice;  /* as chosen (saved as it changes) */
static PatchChoice patchStarted; /* as the game started */
static char patchDir[1024];
static bool patchReload; /* the game restarted for a patch switched on or off: no auto-load */
static bool patchReloadNext;
/* (GEN-9) a change that can't upset the saves: the moment saved here before
 * the game reopens with it, and loaded straight back, so it carries on */
static bool patchHandoffNext;
static char patchHandoff[1100];
uint32_t doCrc32(const void* buf, size_t size); /* (libmgba's: its header wants zlib.h) */
enum { PR_RESTART, PR_SAVE, PR_STATES, PR_HEADER, PR_SUMMARY, PR_BESIDE, PR_PATCH, PR_EMPTY };
/* what each file does to the ROM on its own (worked out as the list opens) */
typedef struct {
	uint32_t from, to; /* [from, to) */
} ByteRange;
typedef struct {
	char file[256];
	char note[300];
	bool usable;       /* applies to this ROM on its own */
	ByteRange* ranges; /* (usable) the bytes it changes */
	int nRanges;
	/* a patch GBA DS knows (docs/patches.md): the ROMs it's made for, and
	 * what to say when it goes on one of them (an optional patch: on
	 * another patch's result, found when they're tried together) */
	const KnownPatch* known;
	uint32_t bases[8];
	int nBases;
	bool baseOk; /* made for this ROM (or for any of its release's) */
	char okNote[300];
} PatchInfo;
#define PATCH_ROWS 70
static PatchInfo patchInfo[PATCH_ROWS];
static int nPatchInfo;
static bool patchBesideAt0; /* patchInfo[0] is the file beside the ROM */
typedef struct {
	int kind;
	char file[256]; /* (PR_PATCH, PR_BESIDE) its file name */
	char label[300];
	char note[600]; /* the footer's text when it's selected */
	bool usable;    /* (PR_PATCH) can be switched on */
	int order;      /* (PR_PATCH) its place among the ones on, from 0, or -1 */
	int chain;      /* (on) 0 applies, 1 applies but shares bytes with one above, 2 fails where it is */
} PatchRow;
static PatchRow patchRows[PATCH_ROWS + 8];
static int nPatchRows, patchSel, patchScroll, patchArmed = -1;
static bool patchesOpen, patchMoving;
static char patchNote[300];
static int patchPressRow = -1, patchPressY, patchPressScroll;
static bool patchDragging;

static PatchRow* patch_row_add(int kind, const char* file, const char* label, const char* note, bool usable) {
	if (nPatchRows >= PATCH_ROWS + 8) return NULL;
	PatchRow* r = &patchRows[nPatchRows++];
	memset(r, 0, sizeof(*r));
	r->kind = kind;
	r->order = -1;
	snprintf(r->file, sizeof(r->file), "%s", file ? file : "");
	snprintf(r->label, sizeof(r->label), "%s", label);
	snprintf(r->note, sizeof(r->note), "%s", note ? note : "");
	r->usable = usable;
	return r;
}

static int patch_name_cmp(const void* a, const void* b) {
	return strcasecmp((const char*) a, (const char*) b);
}

static uint8_t* patch_file_read(const char* path, size_t* n) {
	*n = 0;
	uint8_t* p = NULL;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	rewind(f);
	if (len > 0 && len <= 64L * 1024 * 1024 && (p = malloc((size_t) len)) && fread(p, 1, (size_t) len, f) == (size_t) len)
		*n = (size_t) len;
	else {
		free(p);
		p = NULL;
	}
	fclose(f);
	return p;
}

/* the bytes out changes from rom (gaps under 16 bytes joined; past rom's end, all) */
static void patch_ranges(const uint8_t* rom, size_t romN, const uint8_t* out, size_t outN, PatchInfo* pi) {
	int cap = 0;
	size_t n = romN < outN ? romN : outN;
	for (size_t i = 0; i < n;) {
		if (rom[i] == out[i]) {
			++i;
			continue;
		}
		size_t j = i + 1, same = 0;
		while (j < n && same < 16) {
			same = rom[j] == out[j] ? same + 1 : 0;
			++j;
		}
		ByteRange br = { (uint32_t) i, (uint32_t) (j - same) };
		if (pi->nRanges == cap) {
			cap = cap ? cap * 2 : 64;
			ByteRange* nr = realloc(pi->ranges, sizeof(ByteRange) * (size_t) cap);
			if (!nr) return;
			pi->ranges = nr;
		}
		pi->ranges[pi->nRanges++] = br;
		i = j;
	}
	if (outN > romN) {
		ByteRange* nr = realloc(pi->ranges, sizeof(ByteRange) * (size_t) (pi->nRanges + 1));
		if (!nr) return;
		pi->ranges = nr;
		pi->ranges[pi->nRanges++] = (ByteRange) { (uint32_t) romN, (uint32_t) outN };
	}
}

static bool patch_overlap(const PatchInfo* a, const PatchInfo* b) {
	int i = 0, j = 0;
	while (i < a->nRanges && j < b->nRanges) {
		if (a->ranges[i].to <= b->ranges[j].from) ++i;
		else if (b->ranges[j].to <= a->ranges[i].from) ++j;
		else return true;
	}
	return false;
}

/* what a ROM the patches made is ("Tested romhack: ..." / "Untested ...") */
static void patch_result_text(const uint8_t* out, size_t outN, char* note, size_t size) {
	const char* name = NULL;
	switch (profile_dump(emu.gameCode, doCrc32(out, outN), &name)) {
	case DUMP_MOD_TESTED: snprintf(note, size, tr("Tested romhack: %s"), name); break;
	case DUMP_MOD: snprintf(note, size, tr("Known romhack: %s, untested"), name); break;
	case DUMP_TESTED:
	case DUMP_OFFICIAL: snprintf(note, size, tr("Turns this ROM into another official release: %s"), name ? name : ""); break;
	default: snprintf(note, size, tr("Untested: the second screen may not work with it")); break;
	}
}

/* a known patch's note when it goes on the ROM it's made for */
static void patch_known_text(const KnownPatch* k, const uint8_t* out, size_t outN, char* note, size_t size) {
	const char* name = NULL;
	DumpKind d = out ? profile_dump(emu.gameCode, doCrc32(out, outN), &name) : DUMP_MODIFIED;
	if (d == DUMP_MOD_TESTED) snprintf(note, size, tr("Known %s patch, tested: %s"), k->kind, k->name);
	else snprintf(note, size, tr("Known %s patch, untested: %s. The second screen may not work with it"), k->kind, k->name);
}

/* what a patch does to this ROM, on its own */
static void patch_describe(const uint8_t* rom, size_t romN, uint32_t romCrc, const char* path, PatchInfo* pi) {
	size_t pn = 0;
	uint8_t* p = patch_file_read(path, &pn);
	uint8_t* out = NULL;
	size_t outN = 0;
	int r = pn && rom ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
	/* known (docs/patches.md)? Made for this ROM? */
	pi->known = NULL;
	pi->nBases = 0;
	pi->baseOk = false;
	const KnownPatch* k;
	for (int i = 0; pn && (k = profile_patch(p, pn, i)); ++i) {
		if (strncmp(k->code, emu.gameCode, 4) && k->code[0]) continue; /* (another game's, with the same file) */
		if (!pi->known) pi->known = k;
		if (!k->base || k->base == romCrc) pi->baseOk = true, pi->known = k;
		else if (pi->nBases < 8) pi->bases[pi->nBases++] = k->base;
	}
	bool compressed = pn && patch_xdelta_compressed(p, pn);
	free(p);
	pi->usable = r == PATCH_APPLIED;
	if (pi->known && r == PATCH_APPLIED) patch_known_text(pi->known, out, outN, pi->okNote, sizeof(pi->okNote));
	if (r == PATCH_WRONG_ROM && pi->known) {
		snprintf(pi->note, sizeof(pi->note), tr("Known %s patch: %s, made for %s: it can't be switched on for this ROM"), pi->known->kind,
		         pi->known->name, pi->known->baseName);
	} else if (r == PATCH_WRONG_ROM) {
		snprintf(pi->note, sizeof(pi->note), tr("Made for another version of the game: it can't be switched on for this ROM"));
	} else if (r != PATCH_APPLIED && compressed) {
		snprintf(pi->note, sizeof(pi->note),
		         tr("An xdelta made with compression, which GBA DS can't read: patch the ROM with it beforehand, or make it again without (xdelta3 -S none)"));
	} else if (r != PATCH_APPLIED) {
		snprintf(pi->note, sizeof(pi->note), tr("Can't be read: not a patch, or damaged"));
	} else if (pi->known && !pi->baseOk) {
		/* an IPS can't say which ROM it's for: the list knows */
		snprintf(pi->note, sizeof(pi->note), tr("Known %s patch: %s, made for %s. This ROM is another version: it may break the game"),
		         pi->known->kind, pi->known->name, pi->known->baseName);
	} else if (pi->known) {
		snprintf(pi->note, sizeof(pi->note), "%s", pi->okNote);
	} else {
		patch_result_text(out, outN, pi->note, sizeof(pi->note));
	}
	if (r == PATCH_APPLIED) patch_ranges(rom, romN, out, outN, pi);
	free(out);
}

static PatchInfo* patch_info(const char* file) {
	for (int i = patchBesideAt0 ? 1 : 0; i < nPatchInfo; ++i)
		if (!strcmp(patchInfo[i].file, file)) return &patchInfo[i];
	return NULL;
}

static void patch_title_type(const char* file, char* out, size_t size) {
	char title[256], type[8] = "";
	patch_title(file, title, sizeof(title));
	const char* ext = strrchr(file, '.');
	if (ext && (!strcasecmp(ext, ".xdelta") || !strcasecmp(ext, ".vcdiff"))) snprintf(type, sizeof(type), "xdelta");
	else
		for (int k = 0; ext && ext[k + 1] && k < 3; ++k) type[k] = (char) toupper((unsigned char) ext[k + 1]);
	snprintf(out, size, "%s (%s)", title, type);
}

/* The rows, from patchInfo and the choice; the selection kept on the same
 * row. With more than one patch to apply, they're tried together. */
static void patch_rebuild(void) {
	int selKind = patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].kind : PR_SAVE;
	char selFile[256];
	snprintf(selFile, sizeof(selFile), "%s", patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].file : "");
	nPatchRows = 0;
	if (!patch_choice_same(&patchChoice, &patchStarted))
		patch_row_add(PR_RESTART, NULL, "Patches changed since the game started",
		              "The patches aren't as the game started: restart it to play with them (or they're used from the next start)", true);
	patch_row_add(PR_SAVE, NULL, "Separate battery save",
	              "With a patch on, the game keeps its own battery save (\"<game> + <patch>\"): off, it uses the original's", true);
	patch_row_add(PR_STATES, NULL, "Separate save states",
	              "With a patch on, the game keeps its own save states: off, it uses the original's (a state can come out garbled in the other)", true);
	char folder[300];
	const char* slash = strrchr(patchDir, '/');
	snprintf(folder, sizeof(folder), tr("In patches/%s"), slash ? slash + 1 : patchDir);
	patch_row_add(PR_HEADER, NULL, folder, NULL, false);
	int summaryAt = nPatchRows;
	int nApply = (patchBesideAt0 ? 1 : 0) + patchChoice.nOn;
	if (nApply > 1) patch_row_add(PR_SUMMARY, NULL, "", NULL, false);
	if (patchBesideAt0) {
		PatchInfo* pi = &patchInfo[0];
		char label[300], full[600];
		snprintf(label, sizeof(label), tr("%s (beside the ROM)"), pi->file);
		snprintf(full, sizeof(full), tr("Always on, applied first (RetroArch's way: a patch named as the ROM). %s"), pi->note);
		patch_row_add(PR_BESIDE, pi->file, label, full, false);
	}
	/* the ones on, in order */
	for (int i = 0; i < patchChoice.nOn; ++i) {
		PatchInfo* pi = patch_info(patchChoice.on[i]);
		char label[300], t[280];
		patch_title_type(patchChoice.on[i], t, sizeof(t));
		if (nApply > 1) snprintf(label, sizeof(label), "%d. %s", i + 1, t);
		else snprintf(label, sizeof(label), "%s", t);
		PatchRow* r = patch_row_add(PR_PATCH, patchChoice.on[i], label,
		                            pi ? pi->note : "Not in the folder any more: it's left out", pi && pi->usable);
		if (r) r->order = i;
	}
	/* the rest, by name */
	int first = patchBesideAt0 ? 1 : 0;
	for (int i = first; i < nPatchInfo; ++i) {
		if (patch_choice_find(&patchChoice, patchInfo[i].file) >= 0) continue;
		char t[280];
		patch_title_type(patchInfo[i].file, t, sizeof(t));
		patch_row_add(PR_PATCH, patchInfo[i].file, t, patchInfo[i].note, patchInfo[i].usable);
	}
	if (nPatchInfo == first) {
		char note[1300];
		snprintf(note, sizeof(note), tr("Put .ips, .ups, .bps or .xdelta files in %s"), patchDir);
		patch_row_add(PR_EMPTY, NULL, "No patches here yet", note, false);
	}
	/* together, in order, as the game will apply them */
	if (nApply > 1) {
		size_t romN = 0;
		uint8_t* rom = emu_rom_read_all(romPath, &romN);
		const PatchInfo* appliedInfo[PATCH_ON_MAX + 1];
		const char* appliedName[PATCH_ON_MAX + 1];
		int nApplied = 0;
		for (int k = 0; rom && k < nApply; ++k) {
			bool beside = patchBesideAt0 && k == 0;
			const char* file = beside ? patchInfo[0].file : patchChoice.on[k - (patchBesideAt0 ? 1 : 0)];
			PatchInfo* pi = beside ? &patchInfo[0] : patch_info(file);
			PatchRow* row = NULL;
			for (int j = 0; j < nPatchRows; ++j)
				if ((patchRows[j].kind == (beside ? PR_BESIDE : PR_PATCH)) && !strcmp(patchRows[j].file, file)) row = &patchRows[j];
			char path[1400];
			if (beside) patch_find(romPath, path, sizeof(path));
			else snprintf(path, sizeof(path), "%s/%s", patchDir, file);
			size_t pn = 0;
			uint8_t* p = pi ? patch_file_read(path, &pn) : NULL;
			uint8_t* out = NULL;
			size_t outN = 0;
			int r = pn ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
			free(p);
			/* a known patch made for another patch's result (an optional one, after its main patch): fine here */
			if (r == PATCH_APPLIED && row && pi && pi->known && !pi->baseOk) {
				uint32_t here = doCrc32(rom, romN);
				for (int b = 0; b < pi->nBases; ++b)
					if (pi->bases[b] == here) snprintf(row->note, sizeof(row->note), "%s", pi->okNote);
			}
			if (r == PATCH_APPLIED) {
				free(rom);
				rom = out, romN = outN;
				for (int a = 0; pi && row && a < nApplied; ++a) {
					if (!appliedInfo[a] || !patch_overlap(pi, appliedInfo[a])) continue;
					row->chain = 1;
					char other[280];
					if (appliedName[a]) patch_title_type(appliedName[a], other, sizeof(other));
					else snprintf(other, sizeof(other), tr("the patch beside the ROM"));
					snprintf(row->note, sizeof(row->note),
					         tr("Changes some of the same bytes as %s, and goes over it there: they may not work together"), other);
					break;
				}
				appliedInfo[nApplied] = pi;
				appliedName[nApplied++] = beside ? NULL : patchChoice.on[k - (patchBesideAt0 ? 1 : 0)];
			} else {
				free(out);
				if (row && pi && pi->usable) {
					row->chain = 2;
					snprintf(row->note, sizeof(row->note),
					         tr("Left out where it is: it's made for the unchanged ROM, so it has to be first (only one such patch "
					         "can be on). Move it up with X"));
				} else if (row) {
					row->chain = 2;
				}
			}
		}
		char res[300] = "";
		if (rom) patch_result_text(rom, romN, res, sizeof(res));
		free(rom);
		snprintf(patchRows[summaryAt].label, sizeof(patchRows[0].label), tr("Together: %s"), res);
		snprintf(patchRows[summaryAt].note, sizeof(patchRows[0].note), "%s", res);
	}
	/* the selection where it was */
	patchSel = -1;
	for (int i = 0; i < nPatchRows && patchSel < 0; ++i)
		if (patchRows[i].kind == selKind && !strcmp(patchRows[i].file, selFile)) patchSel = i;
	for (int i = 0; i < nPatchRows && patchSel < 0; ++i)
		if (patchRows[i].kind == PR_SAVE) patchSel = i;
}

static void open_patches(void) {
	for (int i = 0; i < nPatchInfo; ++i) free(patchInfo[i].ranges);
	memset(patchInfo, 0, sizeof(patchInfo));
	nPatchInfo = 0;
	nPatchRows = 0;
	patchSel = patchScroll = 0;
	patchArmed = -1;
	patchMoving = false;
	patchNote[0] = 0;
	patchPressRow = -1;
	size_t romN = 0;
	uint8_t* rom = emu_rom_read_all(romPath, &romN);
	uint32_t romCrc = rom ? doCrc32(rom, romN) : 0;
	char beside[1024];
	patchBesideAt0 = patch_find(romPath, beside, sizeof(beside));
	if (patchBesideAt0) {
		PatchInfo* pi = &patchInfo[nPatchInfo++];
		snprintf(pi->file, sizeof(pi->file), "%s", patch_name(beside));
		patch_describe(rom, romN, romCrc, beside, pi);
	}
	char names[PATCH_ROWS][256];
	int n = 0;
	DIR* d = opendir(patchDir);
	struct dirent* e;
	while (d && (e = readdir(d)) && n < PATCH_ROWS - 2) {
		const char* ext = strrchr(e->d_name, '.');
		if (e->d_name[0] != '.' && ext &&
		    (!strcasecmp(ext, ".ips") || !strcasecmp(ext, ".ups") || !strcasecmp(ext, ".bps") || !strcasecmp(ext, ".xdelta") ||
		     !strcasecmp(ext, ".vcdiff")))
			snprintf(names[n++], sizeof(names[0]), "%s", e->d_name);
	}
	if (d) closedir(d);
	qsort(names, (size_t) n, sizeof(names[0]), patch_name_cmp);
	for (int i = 0; i < n; ++i) {
		char path[1300];
		snprintf(path, sizeof(path), "%s/%s", patchDir, names[i]);
		PatchInfo* pi = &patchInfo[nPatchInfo++];
		snprintf(pi->file, sizeof(pi->file), "%s", names[i]);
		patch_describe(rom, romN, romCrc, path, pi);
	}
	free(rom);
	patch_rebuild();
	patchSel = 0;
	patchesOpen = true;
}

static bool patch_is_on(const PatchRow* r) {
	if (r->kind == PR_SAVE) return patchChoice.sepSave;
	if (r->kind == PR_STATES) return patchChoice.sepStates;
	if (r->kind == PR_BESIDE) return true;
	return r->kind == PR_PATCH && r->order >= 0;
}

static bool patch_row_text_only(int kind) {
	return kind == PR_HEADER || kind == PR_SUMMARY || kind == PR_EMPTY;
}

static int patch_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nPatchRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void patch_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = patchSel * (g.rowH + g.gap);
	if (y < patchScroll) patchScroll = y;
	if (y + g.rowH > patchScroll + g.listH) patchScroll = y + g.rowH - g.listH;
	patchScroll = SDL_clamp(patchScroll, 0, patch_max_scroll(&g));
}

/* the one picked up (X) a place up or down among the ones on */
static void patch_shift(int dir) {
	if (patchSel < 0 || patchSel >= nPatchRows) return;
	int i = patchRows[patchSel].order, j = i + dir;
	if (i < 0 || j < 0 || j >= patchChoice.nOn) return;
	char t[256];
	memcpy(t, patchChoice.on[i], sizeof(t));
	memcpy(patchChoice.on[i], patchChoice.on[j], sizeof(t));
	memcpy(patchChoice.on[j], t, sizeof(t));
	patch_choice_save(gp.patches, &patchChoice);
	fprintf(stderr, "patches: %s moved to %d\n", t, j + 1);
	patch_rebuild();
	patch_keep_visible(ui_touch_screen());
}

static void patch_move(int dir) {
	if (patchMoving) {
		patch_shift(dir);
		return;
	}
	for (int i = patchSel + dir; i >= 0 && i < nPatchRows; i += dir) {
		if (!patch_row_text_only(patchRows[i].kind)) {
			patchSel = i;
			break;
		}
	}
	patchArmed = -1;
	patchNote[0] = 0;
	patch_keep_visible(ui_touch_screen());
}

/* X (or a tap on its number): pick the selected patch up to move it, or put it down */
static void patch_pick_up(void) {
	patchArmed = -1;
	if (patchMoving) {
		patchMoving = false;
		snprintf(patchNote, sizeof(patchNote), "%s",
		         patch_choice_same(&patchChoice, &patchStarted) ? tr("As the game started"): tr("Restart from the top row to play with this order"));
		return;
	}
	const PatchRow* r = patchSel >= 0 && patchSel < nPatchRows ? &patchRows[patchSel] : NULL;
	if (!r || r->kind != PR_PATCH || r->order < 0) return;
	if (patchChoice.nOn < 2) {
		snprintf(patchNote, sizeof(patchNote), tr("Only patches that are on have an order, and this is the only one"));
		return;
	}
	patchMoving = true;
	snprintf(patchNote, sizeof(patchNote), tr("Moving it: Up / Down or tap where it goes, then A (or tap it again). The one lower in the list is applied later"));
}

/* Can the game carry on from this moment with the patches as chosen (GEN-9)?
 * The patched ROM the same size as the one playing, and the battery save and
 * save states the same files. */
static bool patch_live_ok(void) {
	uint8_t h[0xB0];
	char code[5] = "????";
	if (emu_rom_header(romPath, h, sizeof(h))) memcpy(code, h + 0xAC, 4);
	size_t nowN = 0, newN = 0;
	int nowR = PATCH_NONE, newR = PATCH_NONE;
	free(emu_rom_read_played(romPath, &nowN, &nowR, NULL, NULL));
	char files[PATCH_ON_MAX + 1][1024];
	int n = patch_plan(cfg, romPath, code, &patchChoice, files, PATCH_ON_MAX + 1);
	free(emu_rom_read_patched(romPath, files, n, &newN, &newR, NULL));
	if (!nowN || nowN != newN) return false;
	GamePaths a, b;
	game_paths(cfg, romPath, &a);
	game_paths(cfg, romPath, &b);
	if (nowR == PATCH_APPLIED || nowR == PATCH_PARTIAL) game_paths_patched(cfg, &a, &patchStarted);
	if (newR == PATCH_APPLIED || newR == PATCH_PARTIAL) game_paths_patched(cfg, &b, &patchChoice);
	return !strcmp(a.sav, b.sav) && !strcmp(a.quick, b.quick) && !strcmp(a.autoState, b.autoState);
}

static bool state_save(const char* path);
/* the restart for a change: carrying on from this moment when it can */
static bool patch_restart(void) {
	patchReloadNext = true;
	patchHandoffNext = false;
	if (patch_live_ok()) {
		snprintf(patchHandoff, sizeof(patchHandoff), "%s/%s.patch-switch.state", cfg->stateDir, gp.base);
		patchHandoffNext = state_save(patchHandoff);
	}
	fprintf(stderr, "patches: %s\n", patchHandoffNext ? "carrying on with the change" : "restarting with the change");
	return true;
}

/* A on a row (or a tap). Returns true when the game is to restart (GEN-9). */
static bool patch_choose(int row, bool later) {
	if (row < 0 || row >= nPatchRows) return false;
	PatchRow* r = &patchRows[row];
	if (patchMoving) {
		patch_pick_up();
		return false;
	}
	if (r->kind == PR_SAVE || r->kind == PR_STATES) {
		bool* v = r->kind == PR_SAVE ? &patchChoice.sepSave : &patchChoice.sepStates;
		*v = !*v;
		patch_choice_save(gp.patches, &patchChoice);
		snprintf(patchNote, sizeof(patchNote), "%s", patchChoice.nOn ? tr("Used from the next time the game starts"): tr("Saved for when a patch is on"));
		return false;
	}
	if (r->kind == PR_RESTART) {
		if (later) return false;
		if (patchArmed != row) {
			patchArmed = row;
			snprintf(patchNote, sizeof(patchNote), "%s",
			         patch_live_ok() ? tr("Are you sure? A: use them now and carry on from here. B: call it off"): tr("Are you sure? Save your game first. A: restart now. B: call it off"));
			return false;
		}
		patchArmed = -1;
		return patch_restart();
	}
	if (r->kind != PR_PATCH) return false;
	bool on = patch_is_on(r);
	if (!on && !r->usable) {
		snprintf(patchNote, sizeof(patchNote), "%s", r->note);
		return false;
	}
	if (!on && patchChoice.nOn >= PATCH_ON_MAX) {
		snprintf(patchNote, sizeof(patchNote), tr("Up to %d patches can be on at once"), PATCH_ON_MAX);
		return false;
	}
	if (patchArmed != row) {
		patchArmed = row;
		/* (what A would do: tried on a copy of the choice) */
		PatchChoice keep = patchChoice;
		if (on) {
			for (int k = r->order; k + 1 < patchChoice.nOn; ++k) memcpy(patchChoice.on[k], patchChoice.on[k + 1], sizeof(patchChoice.on[0]));
			--patchChoice.nOn;
		} else {
			snprintf(patchChoice.on[patchChoice.nOn++], sizeof(patchChoice.on[0]), "%s", r->file);
		}
		bool live = patch_live_ok();
		patchChoice = keep;
		if (live)
			snprintf(patchNote, sizeof(patchNote), tr("Are you sure? A: switch it %s now and carry on from here. Y: not yet (more changes first). B: call it off"),
			         on ? tr("off"): tr("on"));
		else
			snprintf(patchNote, sizeof(patchNote),
			         tr("Are you sure? Save your game first. A: restart %s it now. Y: not yet (more changes first). B: call it off"),
			         on ? tr("without"): tr("with"));
		return false;
	}
	patchArmed = -1;
	char file[256];
	snprintf(file, sizeof(file), "%s", r->file);
	if (on) {
		int i = r->order;
		for (int k = i; k + 1 < patchChoice.nOn; ++k) memcpy(patchChoice.on[k], patchChoice.on[k + 1], sizeof(patchChoice.on[0]));
		--patchChoice.nOn;
	} else {
		snprintf(patchChoice.on[patchChoice.nOn++], sizeof(patchChoice.on[0]), "%s", file); /* (applied last) */
	}
	patch_choice_save(gp.patches, &patchChoice);
	fprintf(stderr, "patches: %s %s%s\n", file, on ? "switched off" : "switched on", later ? ", for later" : "");
	if (later) {
		patch_rebuild();
		patch_keep_visible(ui_touch_screen());
		snprintf(patchNote, sizeof(patchNote), "%s", patch_choice_same(&patchChoice, &patchStarted) ? tr("As the game started"): tr("Restart from the top row to play with it (or it's used from the next start)"));
		return false;
	}
	return patch_restart();
}

/* is screen-local x on the number of a patch that's on (to pick it up) */
static bool patch_on_number(int scr, int row, int x) {
	CheatGeom g = cheat_geom(scr);
	return row >= 0 && row < nPatchRows && patchRows[row].kind == PR_PATCH && patchRows[row].order >= 0 && patchChoice.nOn > 1 &&
	       x < g.x + g.px * 14;
}

/* row under screen-local y, or -1 */
static int patch_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + patchScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nPatchRows) return -1;
	return row;
}

static void draw_patches(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = menu_geom(scr).rad, cap = menu_geom(scr).cap;
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Patches", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5;
	int pillW = ui_label_width(scr, tcap, "Restart") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nPatchRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - patchScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		const PatchRow* pr = &patchRows[i];
		char line[300];
		if (patch_row_text_only(pr->kind)) {
			fit_text(line, sizeof(line), pr->kind == PR_HEADER ? TF_BOLD : TF_BODY, tcap, pr->label, g.w - px * 4);
			ui_font_text(scr, pr->kind == PR_HEADER ? TF_BOLD : TF_BODY, g.x + px * 2, y + g.rowH / 2, tcap, line,
			             pr->kind == PR_HEADER ? C_GOLD : pr->kind == PR_SUMMARY ? C_TEXT : C_DIS, -1);
			continue;
		}
		bool sel = i == patchSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		ui_key(scr, row, rad, sel ? KEY_SEL : KEY_PLAIN);
		bool on = patch_is_on(pr);
		bool dim = pr->kind == PR_PATCH && !pr->usable && !on;
		bool moving = sel && patchMoving;
		fit_text(line, sizeof(line), TF_BODY, tcap, pr->label, nameW);
		/* (a patch that shares bytes with one above it: its name in gold) */
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, dim ? C_DIS : on && pr->chain == 1 ? C_GOLD : C_TEXT, -1);
		const char* pill = i == patchArmed ? N_("Sure?"): moving        ? N_("Move"): pr->kind == PR_RESTART ? "Restart"
		                   : pr->kind == PR_BESIDE  ? (pr->chain == 2 ? N_("Fails"): N_("Always"))
		                   : dim                    ? "-"
		                   : on && pr->chain == 2   ? N_("Fails"): on                     ? "On"
		                                            : "Off";
		bool gold = i == patchArmed || moving || pr->kind == PR_RESTART;
		SDL_Rect pl = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		SDL_Color pc = gold                   ? C_GOLD
		               : on && pr->chain == 2 ? (SDL_Color) { 176, 72, 72, 255 }
		               : on                   ? (SDL_Color) { 64, 168, 112, 255 }
		                                      : (SDL_Color) { 60, 56, 84, 255 };
		ui_fill_round(scr, pl, pc, px * 2);
		ui_label(scr, pl.x + pl.w / 2, pl.y + pl.h / 2, tcap, pill,
		         gold ? (SDL_Color) { 20, 16, 36, 255 } : on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	int maxS = patch_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * patchScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: what was just done or asked, else what the selected row is */
	const char* foot = patchNote[0] ? patchNote : (patchSel >= 0 && patchSel < nPatchRows ? patchRows[patchSel].note : "");
	if (foot[0]) ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, foot, C_GOLD);
}

/* a button page with more rows than ROW_VIS scrolls: itemTop is its first row shown */
static int itemTop;
static bool item_shown(int i) {
	return itemRow[i] >= itemTop && itemRow[i] + item_span(menuItems[i]) <= itemTop + ROW_VIS;
}
/* the selected button kept on screen */
static void items_scroll_to_sel(void) {
	if (itemRow[menuSel] < itemTop) itemTop = itemRow[menuSel];
	if (itemRow[menuSel] + item_span(menuItems[menuSel]) > itemTop + ROW_VIS) itemTop = itemRow[menuSel] + item_span(menuItems[menuSel]) - ROW_VIS;
	itemTop = SDL_clamp(itemTop, 0, SDL_max(0, nMenuRows - ROW_VIS));
}
static SDL_Rect item_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	SDL_Rect r = { g.margin, g.top + (itemRow[i] - itemTop) * (g.h + g.gap), g.w, g.h * item_span(menuItems[i]) + g.gap * (item_span(menuItems[i]) - 1) };
	if (itemSide[i] >= 0) {
		int hw = (g.w - g.gap) / 2;
		r.x += itemSide[i] * (g.w - hw);
		r.w = hw;
	}
	return r;
}

/* box t of n across the top of the menu: the page tabs (n = N_PAGES), or
 * Back and the title on the cheats / achievements screens (n = 2) */
static SDL_Rect top_box(int scr, int t, int n) {
	MenuGeom g = menu_geom(scr);
	if (n == N_PAGES) {
		/* the page tabs (icons, drawn by draw_tab_icon) between the L1 / R1 prompts, sharing the width */
		int x = g.margin + g.prompt + g.gap, avail = g.w - (g.prompt + g.gap) * 2 - g.gap * (n - 1);
		int x0 = x + t * avail / n + t * g.gap, x1 = x + (t + 1) * avail / n + t * g.gap;
		return (SDL_Rect) { x0, g.tabY, x1 - x0, g.h };
	}
	int tw = (g.w - g.gap * (n - 1)) / n;
	return (SDL_Rect) { g.margin + t * (tw + g.gap), g.tabY, tw, g.h };
}

static SDL_Rect tab_rect(int scr, int t) {
	return top_box(scr, t, 2);
}

/* ---------------------------------------------------------------- bug report note
 *
 * After the bug button (the quick bar's, or Extras > Submit a Bug Report),
 * the game paused: a note for the report, typed on a keyboard on the
 * touchscreen (or with the D-pad and A), written at the top of the
 * report's info file. Skip, or Done with nothing typed, saves it without
 * one. The report is the moment the button was pressed: its files are
 * gathered then (bug_report), and zipped with the note (bug_finish). */
static bool noteOpen;
static bool noteFromBar;   /* the quick bar's button: done closes the menu too */
static char noteText[241];
static int noteLen;
static bool noteShift;
static int noteRow, noteCol; /* the key the D-pad is on */
static int notePressKey = -1;
static bool bug_finish(const char* note, char* out, size_t outSize);
static void note_open(bool fromBar);
static void note_done(bool keep);

/* the keys: rows of labels, "\x01" Shift, "\x02" Del, "\x03" Space, "\x04" Done */
#define NOTE_ROWS 5
static const char* const noteKeys[NOTE_ROWS][11] = {
	{ "1", "2", "3", "4", "5", "6", "7", "8", "9", "0" },
	{ "q", "w", "e", "r", "t", "y", "u", "i", "o", "p" },
	{ "a", "s", "d", "f", "g", "h", "j", "k", "l", "'" },
	{ "\x01", "z", "x", "c", "v", "b", "n", "m", ",", ".", "\x02" },
	{ "\x03", "\x04" },
};
static const char* const noteShifted[10] = { "!", "?", "\"", "-", ":", ";", "/", "(", ")", "&" };
static int note_row_len(int r) {
	int n = 0;
	while (n < 11 && noteKeys[r][n]) ++n;
	return n;
}
/* a key's width in twelfths of the row: Shift and Del a half wider, Space most of the last row */
static int note_key_units(int r, int c) {
	const char* k = noteKeys[r][c];
	if (k[0] == '\x01' || k[0] == '\x02') return 15;
	if (k[0] == '\x03') return 80;
	if (k[0] == '\x04') return 40;
	return 10;
}
typedef struct { SDL_Rect field; int top, rowH, gap; } NoteGeom;
static NoteGeom note_geom(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	NoteGeom g;
	g.gap = mg.gap;
	g.field = (SDL_Rect) { mg.margin, mg.top, mg.w, mg.h * 9 / 4 };
	g.top = g.field.y + g.field.h + mg.gap * 2;
	g.rowH = (r.h - mg.margin - g.top - g.gap * (NOTE_ROWS - 1)) / NOTE_ROWS;
	return g;
}
static SDL_Rect note_key_rect(int scr, int r, int c) {
	MenuGeom mg = menu_geom(scr);
	NoteGeom g = note_geom(scr);
	int n = note_row_len(r), units = 0, before = 0;
	for (int i = 0; i < n; ++i) units += note_key_units(r, i), before += i < c ? note_key_units(r, i) : 0;
	int avail = mg.w - g.gap * (n - 1);
	int x0 = mg.margin + before * avail / units + c * g.gap, x1 = mg.margin + (before + note_key_units(r, c)) * avail / units + c * g.gap;
	return (SDL_Rect) { x0, g.top + r * (g.rowH + g.gap), x1 - x0, g.rowH };
}
static const char* note_key_label(int r, int c) {
	const char* k = noteKeys[r][c];
	static char up[4];
	switch (k[0]) {
	case '\x01': return noteShift ? "ABC" : "abc";
	case '\x02': return N_("Del");
	case '\x03': return N_("Space");
	case '\x04': return noteLen ? N_("Done"): N_("Done (No Note)");
	}
	if (!noteShift) return k;
	if (r == 0) return noteShifted[c];
	if (k[0] >= 'a' && k[0] <= 'z') return up[0] = (char) (k[0] - 32), up[1] = 0, up;
	return k;
}
static void note_type(const char* s) {
	size_t n = strlen(s);
	if (noteLen + n < sizeof(noteText)) memcpy(noteText + noteLen, s, n), noteLen += (int) n, noteText[noteLen] = 0;
}
/* a key pressed (touch or A) */
static void note_press(int r, int c) {
	const char* k = noteKeys[r][c];
	switch (k[0]) {
	case '\x01': noteShift = !noteShift; return;
	case '\x02': if (noteLen) noteText[--noteLen] = 0; return;
	case '\x03': note_type(" "); return;
	case '\x04': note_done(true); return;
	}
	note_type(note_key_label(r, c));
	noteShift = false; /* (one letter, as on a phone) */
}
static bool note_key_at(int scr, int x, int y, int* r, int* c) {
	for (int i = 0; i < NOTE_ROWS; ++i) {
		for (int j = 0; j < note_row_len(i); ++j) {
			SDL_Rect k = note_key_rect(scr, i, j);
			if (x >= k.x && x < k.x + k.w && y >= k.y && y < k.y + k.h) return *r = i, *c = j, true;
		}
	}
	return false;
}
/* the D-pad: across a row (round at the ends), or to the key above / below nearest across */
static void note_move(int scr, int dr, int dc) {
	if (dc) {
		int n = note_row_len(noteRow);
		noteCol = (noteCol + dc + n) % n;
		return;
	}
	SDL_Rect k = note_key_rect(scr, noteRow, noteCol);
	int cx = k.x + k.w / 2;
	noteRow = (noteRow + dr + NOTE_ROWS) % NOTE_ROWS;
	int best = 0, bd = INT_MAX;
	for (int j = 0; j < note_row_len(noteRow); ++j) {
		SDL_Rect o = note_key_rect(scr, noteRow, j);
		int d = abs(o.x + o.w / 2 - cx);
		if (d < bd) bd = d, best = j;
	}
	noteCol = best;
}
static void draw_note(int scr) {
	MenuGeom mg = menu_geom(scr);
	NoteGeom g = note_geom(scr);
	int rad = mg.rad, cap = mg.cap;
	/* (on a plain backdrop: the game's picture behind the keys made them hard to read) */
	ui_fill(scr, (SDL_Rect) { 0, 0, screens[scr].rect.w, screens[scr].rect.h }, (SDL_Color) { 12, 10, 22, 255 });
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Skip", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Bug Report", (SDL_Color) { 20, 16, 36, 255 });
	/* the note so far, with a cursor; the hint while it's empty */
	ui_fill_round(scr, g.field, (SDL_Color) { 30, 26, 50, 255 }, rad);
	int tc = SDL_max(4, g.field.h / 6), pad = mg.pad;
	SDL_Rect clip = { screens[scr].rect.x + g.field.x, screens[scr].rect.y + g.field.y, g.field.w, g.field.h };
	ui_clip(scr, &clip);
	if (!noteLen) {
		ui_font_wrap(scr, TF_BODY, g.field.x + pad, g.field.y + pad / 2, g.field.w - pad * 2, tc,
		             "Add a note (optional): what happened, and what you expected to happen.", C_DIS);
	} else {
		char shown[sizeof(noteText) + 4];
		snprintf(shown, sizeof(shown), "%s%s", noteText, (SDL_GetTicks() / 500) & 1 ? "|" : "");
		ui_font_wrap(scr, TF_BODY, g.field.x + pad, g.field.y + pad / 2, g.field.w - pad * 2, tc, shown, C_TEXT);
	}
	ui_clip(scr, NULL);
	/* the keys */
	int kc = SDL_min(cap, g.rowH * 2 / 5);
	for (int r = 0; r < NOTE_ROWS; ++r) {
		for (int c = 0; c < note_row_len(r); ++c) {
			SDL_Rect k = note_key_rect(scr, r, c);
			bool sel = r == noteRow && c == noteCol;
			bool special = noteKeys[r][c][0] < ' ';
			bool lit = noteKeys[r][c][0] == '\x01' && noteShift;
			(void) special; /* (the special keys look like the others) */
			ui_key(scr, k, rad, sel ? KEY_SEL : lit ? KEY_LIT : KEY_PLAIN);
			ui_label(scr, k.x + k.w / 2, k.y + k.h / 2, kc, note_key_label(r, c), lit ? (SDL_Color) { 20, 16, 36, 255 } : C_TEXT);
		}
	}
}

/* The device's battery, 0-100, or -1 if unknown: the battery in
 * /sys/class/power_supply (ROCKNIX), else SDL's (Android). Read at most
 * every 5 seconds. */
static int battery_percent(void) {
	static int pct = -1;
	static Uint32 last;
	static bool read;
	Uint32 now = SDL_GetTicks();
	if (read && now - last < 5000) return pct;
	read = true;
	last = now;
	pct = -1;
#ifndef __ANDROID__
	DIR* d = opendir("/sys/class/power_supply");
	if (d) {
		struct dirent* de;
		while (pct < 0 && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			char path[512], type[32] = "";
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", de->d_name);
			FILE* f = fopen(path, "r");
			if (!f) continue;
			if (!fgets(type, sizeof(type), f)) type[0] = 0;
			fclose(f);
			if (strncmp(type, "Battery", 7)) continue;
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", de->d_name);
			f = fopen(path, "r");
			if (!f) continue;
			int v;
			if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100) pct = v;
			fclose(f);
		}
		closedir(d);
	}
#endif
	if (pct < 0) {
		int p = -1;
		SDL_PowerState st = SDL_GetPowerInfo(NULL, &p);
		if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY && p >= 0) pct = p;
	}
	return pct;
}

/* "Paused" in the middle of the other screen, on a key */
static void draw_paused_label(int o) {
	float px = ui_px(o) + 1;
	int cap = px * 7 * 34 / 35, tw = ui_font_width(TF_BOLD, cap, "Paused"), padX = px * 10, h = cap * 2 + px * 8;
	SDL_Rect r = screens[o].rect;
	SDL_Rect k = { (r.w - tw) / 2 - padX, (r.h - h) / 2, tw + padX * 2, h };
	ui_key(o, k, px * 4, KEY_PLAIN);
	ui_label(o, r.w / 2, r.h / 2, cap, "Paused", ui_theme_light() ? ui_key_ink() : C_GOLD); /* (dark grey on the light theme's keys) */
}

/* Status text on the top (first) screen while paused, with a drop shadow:
 * align -1 at the top left, 1 at the top right. */
static void draw_status(const char* text, int align) {
	int s = 0; /* the top screen, whatever is on it */
	float px = ui_px(s);
	int cap = px * 5;
	int x = align < 0 ? (int) (px * 5) : screens[s].rect.w - (int) (px * 5), cy = px * 7;
	/* on a key, so it shows over the game's picture (dark or light, as the menus) */
	int tw = ui_font_width(TF_BOLD, cap, text), padX = px * 4, h = cap * 2 + px * 3;
	SDL_Rect k = { align < 0 ? x - padX : x - tw - padX, cy - h / 2, tw + padX * 2, h };
	ui_key(s, k, px * 3, KEY_PLAIN);
	ui_font_text(s, TF_BOLD, x, cy, cap, text, C_TEXT, align);
}

/* While paused: the time (24-hour HH:MM) at the top left of the top screen
 * and the battery level at the top right. */
static void draw_battery(void) {
	char buf[16];
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) {
		snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
		draw_status(buf, -1);
	}
	int pct = battery_percent();
	if (pct >= 0) {
		snprintf(buf, sizeof(buf), "%d%%", pct);
		draw_status(buf, 1);
	}
}

/* ---------------------------------------------------------------- the quick bar
 *
 * Swiping up from the bottom of the touchscreen brings up a bar along its
 * bottom, over whatever it shows, without pausing: the time, the battery
 * and the frames a second, then icon buttons: Save State, Load State,
 * Settings (the pause menu), Bug Report (bug_report) and Close GBA DS
 * (tapped twice). In games with another button layout, Controls above.
 * Swiping down, the pause menu opening or 5 seconds untouched put it
 * away. It is drawn inside the second screen's picture (the GBA's 240x160
 * on the touchscreen, every size below in GBA pixels), in the menus' colour
 * (Settings > Display's accent) with the menus' raised keys. It is drawn at
 * the GBA's own resolution (a 240x160 canvas, no smoothing) and scaled up
 * with the picture. */
static bool barOpen;
static float barShown;          /* 0 hidden .. 1 all the way up (sliding) */
static Uint32 barSince;         /* last touched */
static Uint32 barLitAt;         /* a button's action just done: it shows white a moment */
static int barLit = -1;
static Uint32 barExitArmed;     /* Close GBA DS tapped once (a second tap within 3 s closes) */
enum { G_NONE, G_TRACK, G_DONE, G_BTN, G_PIN };
static int barGesture = G_NONE;
static int barBtn = -1;         /* the button under the finger (G_BTN) */
static int barGX, barGY;        /* where the finger went down */
static bool barDownOpen;        /* the bar was up when it went down */
enum { BB_SAVE, BB_LOAD, BB_SETTINGS, BB_BUG, BB_EXIT, BB_COUNT };

/* the bar's panel and its edge: the menus' colour (the header's) */
static SDL_Color bar_panel(void) {
	SDL_Color c = ui_accent_color(AC_HEADER);
	c.a = 255;
	return c;
}
#define G_BG bar_panel()
#define G_EDGE ui_accent_color(AC_EDGE)
static const SDL_Color G_TEXT = { 236, 236, 244, 255 }, G_DIS = { 120, 120, 138, 255 }, G_LIT = { 255, 255, 255, 255 };

static const char* const icon_save[] = {
	"XXXXXXXXXXXXX...",
	"XX.XXXXXXX.XXX..",
	"XX.XXXXXXX.XXXX.",
	"XX.XXXX..X.XXXXX",
	"XX.XXXX..X.XXXXX",
	"XX.XXXXXXX.XXXXX",
	"XX.........XXXXX",
	"XXXXXXXXXXXXXXXX",
	"XXXXXXXXXXXXXXXX",
	"XX............XX",
	"XX............XX",
	"XX.XXXXXXXXXX.XX",
	"XX............XX",
	"XX.XXXXXXXXXX.XX",
	"XX............XX",
	"XXXXXXXXXXXXXXXX",
	NULL,
};
static const char* const icon_load[] = {
	"................",
	"XXXXXX..........",
	"XXXXXXX.........",
	"XX...XXXXXXXXX..",
	"XX..........XX..",
	"XX..........XX..",
	"XX..XXXXXXXXXXXX",
	"XX.XXXXXXXXXXXXX",
	"XX.XX........XX.",
	"XXXX........XX..",
	"XXX.........XX..",
	"XXX........XX...",
	"XXXXXXXXXXXXX...",
	"XXXXXXXXXXXX....",
	"................",
	"................",
	NULL,
};
static const char* const icon_gear[] = {
	"......XXXX......",
	"......XXXX......",
	"..XX.XXXXXX.XX..",
	"..XXXXXXXXXXXX..",
	"...XXXXXXXXXX...",
	"..XXXX....XXXX..",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"XXXXX......XXXXX",
	"..XXXX....XXXX..",
	"...XXXXXXXXXX...",
	"..XXXXXXXXXXXX..",
	"..XX.XXXXXX.XX..",
	"......XXXX......",
	"......XXXX......",
	NULL,
};
static const char* const icon_bug[] = {
	"..X.......X..",
	"...X.....X...",
	"....XXXXX....",
	"...XXXXXXX...",
	".............",
	"X..XXX.XXX..X",
	".X.XXX.XXX.X.",
	"..XXXX.XXXX..",
	"...XXX.XXX...",
	"XXXXXX.XXXXXX",
	"...XXX.XXX...",
	"..XXXX.XXXX..",
	".X.XXX.XXX.X.",
	"X...XX.XX...X",
	NULL,
};
static const char* const icon_exit[] = {
	"XXXXXXX.........",
	"X.....X.....XX..",
	"X.....X.....XX..",
	"X.....X.........",
	"X.....X....XXX..",
	"X.....X...XXXXX.",
	"X.....X..XX.XX.X",
	"X.....X....XX...",
	"X.....X....XXX..",
	"X.....X...XX.XX.",
	"X.....X..XX...XX",
	"X.....X.XX......",
	"X.....X.........",
	"X.....X.........",
	"X.....X.........",
	"XXXXXXX.........",
	NULL,
};

/* Top to bottom: in games with another button layout (profile
 * controls), the Controls picker, centred; the status row (time, battery,
 * FPS); the buttons, across the whole width; a line for messages. All of it in
 * GBA pixels (the *_g functions: 240x160, the second screen's picture), drawn
 * into a canvas of that size (below) and scaled up with the picture. */
enum { BAR_M = 4, BAR_STATUS_H = 14, BAR_BUTTONS_H = 24, BAR_MSG_H = 19, BAR_PICK_H = 24 };
static bool sunHere; /* the cartridge has a sun sensor: the Sun row stands in the Controls row's place */
static int bar_pick_h(void) {
	return ctlProf || sunHere ? BAR_PICK_H : 0;
}
/* the row of time, FPS and battery: without the Controls row above it, tall enough that the pin
 * (2 px down, 12 high, its edge a row more) has a clear pixel row before the buttons */
static int bar_status_h(void) {
	return ctlProf || sunHere ? BAR_STATUS_H : SDL_max(BAR_STATUS_H, 2 + 12 + 1 + 1);
}
static int game_screen(void);

static SDL_Rect bar_rect_g(void) {
	int h = bar_pick_h() + bar_status_h() + BAR_BUTTONS_H + BAR_MSG_H;
	int slide = (int) ((1.0f - barShown) * (h + BAR_M));
	return (SDL_Rect) { BAR_M, GBA_H - BAR_M - h + slide, GBA_W - BAR_M * 2, h };
}
/* the buttons: the bottom row, sharing its width */
static SDL_Rect bar_button_g(int i) {
	SDL_Rect b = bar_rect_g();
	int in = 3, gap = 3, h = BAR_BUTTONS_H - in;
	int x0 = b.x + in + (b.w - in * 2 + gap) * i / BB_COUNT, x1 = b.x + in + (b.w - in * 2 + gap) * (i + 1) / BB_COUNT - gap;
	return (SDL_Rect) { x0, b.y + bar_pick_h() + bar_status_h(), x1 - x0, h };
}
/* the pin, a square in the top right corner */
static SDL_Rect bar_pin_g(void) {
	SDL_Rect b = bar_rect_g();
	return (SDL_Rect) { b.x + b.w - 2 - 12, b.y + 2, 12, 12 };
}
/* the time, FPS and battery: three cells across the row above the buttons */
static SDL_Rect bar_cell_g(int i) {
	SDL_Rect b = bar_rect_g();
	int x0 = b.x + b.w * i / 3, x1 = b.x + b.w * (i + 1) / 3;
	return (SDL_Rect) { x0, b.y + bar_pick_h(), x1 - x0, bar_status_h() };
}

/* Where the second screen's picture is on screen s (screen-local; unclipped:
 * zoom can run past the screen's edges). The bar is in it, a GBA pixel being
 * the picture's w / 240 screen pixels. With no map the touchscreen may show
 * the game: then the game's picture. */
static SDL_Rect bar_pic_full(int s) {
	bool gameHere = nScreens == 1 || s == game_screen() || !((mc || logoOnly) && cfg->map);
	return ui_image_rect(s, GBA_W, GBA_H, gameHere ? cfg->scale : cfg->mapScale);
}
static SDL_Rect bar_to_screen(int s, SDL_Rect g) {
	SDL_Rect p = bar_pic_full(s);
	int x0 = p.x + g.x * p.w / GBA_W, x1 = p.x + (g.x + g.w) * p.w / GBA_W;
	int y0 = p.y + g.y * p.h / GBA_H, y1 = p.y + (g.y + g.h) * p.h / GBA_H;
	return (SDL_Rect) { x0, y0, x1 - x0, y1 - y0 };
}
static SDL_Rect bar_rect(int s) {
	return bar_to_screen(s, bar_rect_g());
}
static SDL_Rect bar_button(int s, int i) {
	return bar_to_screen(s, bar_button_g(i));
}
static SDL_Rect bar_pin(int s) {
	return bar_to_screen(s, bar_pin_g());
}

static int bar_button_at(int s, int x, int y) {
	SDL_Point pt = { x, y };
	for (int i = 0; i < BB_COUNT; ++i) {
		SDL_Rect r = bar_button(s, i);
		if (SDL_PointInRect(&pt, &r)) return i;
	}
	return -1;
}

/* ---- The bar's canvas: 240x160, the GBA's own resolution, drawn pixel by
 * pixel (no smoothing: the text is Jost thresholded to whole pixels), uploaded
 * as a texture and scaled up in the picture's place by the same whole steps as
 * the picture. Transparent where the bar isn't. */
static uint32_t barPx[GBA_W * GBA_H]; /* ABGR8888 */
static SDL_Texture* barTex[2];
static SDL_Renderer* barTexRen[2];
/* the canvas cv_put draws on: the bar's, or the toggle button's (taller: the margin under the picture) */
static uint32_t* cvBuf = barPx;
static int cvRows = GBA_H;

static uint32_t cv_rgba(SDL_Color c) {
	return (uint32_t) c.a << 24 | (uint32_t) c.b << 16 | (uint32_t) c.g << 8 | c.r;
}
static void cv_put(int x, int y, SDL_Color c) {
	if (x >= 0 && y >= 0 && x < GBA_W && y < cvRows) cvBuf[y * GBA_W + x] = cv_rgba(c);
}
static void cv_rect(SDL_Rect r, SDL_Color c) {
	for (int y = r.y; y < r.y + r.h; ++y)
		for (int x = r.x; x < r.x + r.w; ++x) cv_put(x, y, c);
}
/* a rectangle with rounded corners: the corner pixels outside a circle of radius rad left out */
static void cv_round(SDL_Rect r, int rad, SDL_Color c) {
	rad = SDL_min(rad, SDL_min(r.w, r.h) / 2);
	for (int y = r.y; y < r.y + r.h; ++y) {
		for (int x = r.x; x < r.x + r.w; ++x) {
			float cx = x - r.x + 0.5f, cy = y - r.y + 0.5f;
			float dx = cx < rad ? rad - cx : cx > r.w - rad ? cx - (r.w - rad) : 0;
			float dy = cy < rad ? rad - cy : cy > r.h - rad ? cy - (r.h - rad) : 0;
			if (dx * dx + dy * dy <= (float) rad * rad) cv_put(x, y, c);
		}
	}
}
static void cv_disc(float cx, float cy, float rad, SDL_Color c) {
	for (int y = (int) (cy - rad) - 1; y <= (int) (cy + rad) + 1; ++y)
		for (int x = (int) (cx - rad) - 1; x <= (int) (cx + rad) + 1; ++x) {
			float dx = x + 0.5f - cx, dy = y + 0.5f - cy;
			if (dx * dx + dy * dy <= rad * rad) cv_put(x, y, c);
		}
}
static SDL_Color cv_mix(SDL_Color a, SDL_Color b, int pct) {
	return (SDL_Color) { a.r + (b.r - a.r) * pct / 100, a.g + (b.g - a.g) * pct / 100, a.b + (b.b - a.b) * pct / 100, 255 };
}
/* the menus' raised key at one pixel's detail: the edge a row below it, the border, the rim, the body in tones darker at the bottom */
static void cv_key(SDL_Rect r, int state) {
	SDL_Color border, rim, hi, lo, edge, ring;
	ui_key_palette(state, &border, &rim, &hi, &lo, &edge, &ring);
	if (state == KEY_SEL) cv_round((SDL_Rect) { r.x - 1, r.y - 1, r.w + 2, r.h + 3 }, 3, ring);
	cv_round((SDL_Rect) { r.x, r.y + 1, r.w, r.h }, 2, edge);
	cv_round(r, 2, border);
	cv_round((SDL_Rect) { r.x + 1, r.y + 1, r.w - 2, r.h - 2 }, 1, rim);
	SDL_Rect body = { r.x + 2, r.y + 2, r.w - 4, r.h - 4 };
	cv_round(body, 1, lo);
	cv_round((SDL_Rect) { body.x, body.y, body.w, body.h * 55 / 100 }, 1, cv_mix(lo, hi, 18));
	cv_round((SDL_Rect) { body.x, body.y, body.w, body.h * 25 / 100 + 1 }, 1, cv_mix(lo, hi, 30));
}
/* pixel art ('X' rows, NULL-ended) one pixel to a pixel, centred on cx, cy */
static void cv_icon(const char* const* art, int cx, int cy, SDL_Color c) {
	int rows = 0, cols = (int) strlen(art[0]);
	while (art[rows]) ++rows;
	for (int y = 0; y < rows; ++y)
		for (int x = 0; x < cols; ++x)
			if (art[y][x] == 'X') cv_put(cx - cols / 2 + x, cy - rows / 2 + y, c);
}

/* text rasterized at whole pixels: Jost (so every language works) at the
 * capitals' height cap, a pixel on where it covers over a third. Kept a while:
 * the same few strings come every frame. */
typedef struct { char* text; int cap; TfBitmap b; Uint32 used; } CvText;
static CvText cvText[16];
static CvText* cv_text_entry(const char* text, int cap) {
	CvText* slot = &cvText[0];
	for (int i = 0; i < 16; ++i) {
		CvText* e = &cvText[i];
		if (e->text && e->cap == cap && !strcmp(e->text, text)) {
			e->used = SDL_GetTicks();
			return e;
		}
		if (!e->text || (slot->text && e->used < slot->used)) slot = e;
	}
	if (slot->text) {
		free(slot->text);
		tf_free(&slot->b);
	}
	memset(slot, 0, sizeof(*slot));
	slot->text = strdup(text);
	slot->cap = cap;
	slot->used = SDL_GetTicks();
	if (!slot->text || !tf_render(TF_BOLD, cap, text, &slot->b)) memset(&slot->b, 0, sizeof(slot->b));
	return slot;
}
static void cv_text_drop(void) {
	for (int i = 0; i < 16; ++i) {
		if (cvText[i].text) {
			free(cvText[i].text);
			tf_free(&cvText[i].b);
		}
		memset(&cvText[i], 0, sizeof(cvText[i]));
	}
}
/* centred on cx, the capitals centred on cy; smaller (down to 4 pixels) if it's wider than maxW; the width it took */
static int cv_text(int cx, int cy, int cap, const char* text, SDL_Color c, int maxW) {
	const char* t = tr(text);
	int w = tf_width(TF_BOLD, cap, t);
	while (maxW > 0 && w > maxW && cap > 4) w = tf_width(TF_BOLD, --cap, t);
	CvText* e = cv_text_entry(t, cap);
	if (!e->b.alpha) return w;
	int x0 = cx - w / 2 + e->b.left, y0 = cy + cap / 2 - e->b.baseline;
	for (int y = 0; y < e->b.h; ++y)
		for (int x = 0; x < e->b.w; ++x)
			if (e->b.alpha[y * e->b.w + x] > 90) cv_put(x0 + x, y0 + y, c);
	return w;
}
/* Text in GBA DS's pixel font (pixfont.h: Jost's forms, one pixel thick, in two
 * sizes). What has a letter it lacks (other scripts, accents) is drawn in Jost,
 * thresholded, as above. */
/* the text as plain ASCII for the pixel font (an em dash as a hyphen), or false if it has anything else */
static bool cv_ascii(const char* t, char* out, size_t n) {
	size_t k = 0;
	for (const unsigned char* p = (const unsigned char*) t; *p && k + 1 < n; ++p) {
		if (*p < 0x80) out[k++] = (char) *p;
		else if (p[0] == 0xE2 && p[1] == 0x80 && p[2] == 0x94) out[k++] = '-', p += 2;
		else return false;
	}
	out[k] = 0;
	return true;
}
static void cv_pix_draw(int x0, int y0, const PixFont* f, const char* a, SDL_Color c) {
	for (; *a; ++a) {
		unsigned char ch = (unsigned char) *a;
		if (ch == ' ') {
			x0 += f->space;
			continue;
		}
		const PixGlyph* g = &f->g[ch - 32];
		for (int y = 0; y < f->height; ++y)
			for (int x = 0; x < g->w; ++x)
				if (g->row[y] >> (g->w - 1 - x) & 1) cv_put(x0 + x, y0 + y, c);
		x0 += g->w + 1;
	}
}
/* centred on cx, the capitals centred on cy; the large font if big and it fits maxW, else the small */
static void cv_label(int cx, int cy, bool big, const char* text, SDL_Color c, int maxW) {
	const char* t = tr(text);
	char a[160];
	const PixFont* f = big ? &pixfont_large : &pixfont_small;
	if (!cv_ascii(t, a, sizeof(a)) || !pixfont_has(&pixfont_small, a)) {
		cv_text(cx, cy, f->cap, t, c, maxW);
		return;
	}
	if (big && maxW > 0 && pixfont_width(f, a) > maxW) f = &pixfont_small;
	cv_pix_draw(cx - pixfont_width(f, a) / 2, cy - f->cap / 2, f, a, c);
}
/* the message line: one line of the small font if it fits, else two, broken at the space that
 * makes the lines most alike (and cut with ".." if one still doesn't fit) */
static void cv_message(SDL_Rect area, const char* msg, SDL_Color c) {
	const PixFont* f = &pixfont_small;
	const char* t = tr(msg);
	int cx = area.x + area.w / 2, cy = area.y + area.h / 2;
	char a[200];
	if (!cv_ascii(t, a, sizeof(a)) || !pixfont_has(f, a)) {
		cv_text(cx, cy, f->cap, t, c, area.w);
		return;
	}
	if (pixfont_width(f, a) <= area.w) {
		cv_pix_draw(cx - pixfont_width(f, a) / 2, cy - f->cap / 2, f, a, c);
		return;
	}
	int best = -1, bestW = 1 << 30, n = (int) strlen(a);
	for (int i = 1; i < n; ++i) {
		if (a[i] != ' ') continue;
		char left[200];
		snprintf(left, sizeof(left), "%.*s", i, a);
		int w = SDL_max(pixfont_width(f, left), pixfont_width(f, a + i + 1));
		if (w < bestW) bestW = w, best = i;
	}
	char one[200], two[200];
	snprintf(one, sizeof(one), "%.*s", best < 0 ? n : best, a);
	snprintf(two, sizeof(two), "%s", best < 0 ? "" : a + best + 1);
	char* lines[2] = { one, two };
	int top = cy - f->height;
	for (int i = 0; i < 2; ++i) {
		while (strlen(lines[i]) > 3 && pixfont_width(f, lines[i]) > area.w) strcpy(lines[i] + strlen(lines[i]) - 3, "..");
		cv_pix_draw(cx - pixfont_width(f, lines[i]) / 2, top + i * f->height, f, lines[i], c);
	}
}

static void ctl_draw(void);
static bool ctl_tap(int s, int x, int y);
static void sun_draw(void);
static bool sun_tap(int s, int x, int y);
static void save_prefs(void);

static const char* const icon_pin[] = {
	"..XXX..",
	"..XXX..",
	"..XXX..",
	".XXXXX.",
	"XXXXXXX",
	"...X...",
	"...X...",
	"...X...",
	NULL,
};

static void bar_close(void) {
	barOpen = false;
	barGesture = G_NONE;
	barExitArmed = 0;
}
/* L3, or the quick menu hotkey: up, as with a swipe; again: down */
static void bar_toggle(void) {
	if (barOpen) {
		bar_close();
		return;
	}
	barOpen = true;
	barSince = SDL_GetTicks();
}

/* The hint at a game's start, "Swipe up for quick menu" under an arrow
 * pointing up, at the bottom of the touchscreen: it slides down and away 3
 * seconds in, or at the first touch, or as the bar or the menu comes up
 * (not shown with the bar pinned: it's up already; nor with quick_bar_hint
 * = 0). */
static int hintState;     /* 0 not yet shown, 1 up, 2 going */
static Uint32 hintSince;  /* its first frame */
static float hintShown;   /* 0 away .. 1 all the way up */

static void hint_dismiss(void) {
	if (hintState == 1) hintState = 2;
}

static void draw_hint(void) {
	if (hintState == 0) {
		if (cfg->barPinned || !cfg->barHint) {
			hintState = 2;
			return;
		}
		hintState = 1, hintSince = SDL_GetTicks(), hintShown = 1;
	}
	if (menuOpen) hintState = 2, hintShown = 0;
	if (hintState == 1 && (barOpen || SDL_GetTicks() - hintSince >= 3000)) hintState = 2;
	if (hintState == 2) hintShown = SDL_max(0.0f, hintShown - 0.12f);
	if (hintShown <= 0) return;
	int s = ui_touch_screen();
	float px = ui_px(s);
	static const char* text = N_("Swipe up for quick menu");
	int cap = px * 7, tw = ui_font_width(TF_BOLD, cap, text);
	int padX = px * 6, padY = px * 4, aw = px * 7, ah = px * 6, m = px * 6;
	int w = tw + padX * 2, h = cap + padY * 2;
	SDL_Rect r = screens[s].rect;
	int y = r.h - m - h + (int) ((1.0f - hintShown) * (h + ah + m + 1));
	SDL_Rect b = { (r.w - w) / 2, y, w, h };
	ui_fill_round(s, (SDL_Rect) { b.x - 1, b.y - 1, b.w + 2, b.h + 2 }, G_EDGE, px * 3 + 1);
	ui_fill_round(s, b, G_BG, px * 3);
	/* the arrow on top: the box's edge round it, filled as the box */
	int cx = r.w / 2;
	for (int i = 0; i <= ah; ++i) {
		int half = aw * i / SDL_max(1, ah);
		ui_fill(s, (SDL_Rect) { cx - half - 1, b.y - ah - 1 + i, half * 2 + 2, 1 }, G_EDGE);
		if (half * 2 - 2 > 0) ui_fill(s, (SDL_Rect) { cx - half + 1, b.y - ah - 1 + i, half * 2 - 2, 1 }, G_BG);
	}
	ui_fill(s, (SDL_Rect) { cx - aw + 1, b.y - 1, aw * 2 - 2, 2 }, G_BG); /* (open into the box) */
	ui_font_text(s, TF_BOLD, cx, b.y + b.h / 2, cap, text, G_TEXT, 0);
}

/* While rewinding (L2): "Rewinding..." after a rewind sign (two arrows
 * pointing left) in a box like the hint's, larger, in the middle of the
 * second screen (with one screen, of the game screen); "Can't rewind
 * further" at the rewind buffer's start. While fast-forwarding (R2) the
 * same box says "Fast forwarding..." after two arrows pointing right. It
 * fades as rewinding or fast-forwarding stops. */
static int game_screen(void);
static int rewindBox;       /* 0 none, 1 rewinding, 2 at the buffer's start (this frame) */
static bool ffBox;          /* fast-forwarding (this frame) */
static float rewindShown;   /* 0 .. 1 */
static bool rewindRight;    /* the sign shown points right (fast-forward) */
static const char* rewindText = N_("Rewinding...");

static void draw_rewind(void) {
	if (rewindBox) {
		rewindShown = 1;
		rewindRight = false;
		rewindText = rewindBox == 2 ? N_("Can't rewind further"): N_("Rewinding...");
	} else if (ffBox) {
		rewindShown = 1;
		rewindRight = true;
		rewindText = N_("Fast forwarding...");
	} else {
		rewindShown = SDL_max(0.0f, rewindShown - 0.15f);
	}
	if (rewindShown <= 0) return;
	int s = nScreens > 1 ? 1 - game_screen() : 0;
	float px = ui_px(s);
	int cap = px * 10, tw = ui_font_width(TF_BOLD, cap, rewindText);
	int padX = px * 8, padY = px * 6, ih = cap * 3 / 2, iw = ih, gap = px * 6;
	int w = padX + iw + gap + tw + padX, h = SDL_max(cap, ih) + padY * 2;
	SDL_Rect r = screens[s].rect;
	SDL_Rect b = { (r.w - w) / 2, (r.h - h) / 2, w, h };
	if (rewindShown < 0.35f) return; /* (a key doesn't fade: it goes) */
	SDL_Color fg = ui_key_ink();
	ui_key(s, b, px * 4, KEY_PLAIN);
	/* the sign: two triangles side by side, pointing left (rewind) or right (fast-forward) */
	int ix = b.x + padX, cy = b.y + b.h / 2, half = ih / 2, tri = iw / 2;
	for (int t = 0; t < 2; ++t) {
		int x0 = ix + t * tri;
		for (int i = 0; i < tri; ++i) {
			int hh = half * (i + 1) / tri;
			int x = rewindRight ? x0 + tri - 1 - i : x0 + i;
			ui_fill(s, (SDL_Rect) { x, cy - hh, 1, hh * 2 }, fg);
		}
	}
	ui_font_text(s, TF_BOLD, ix + iw + gap + tw / 2, cy, cap, rewindText, fg, 0);
}

static void draw_bar(void) {
	static bool wasShown;
	barShown = barOpen ? SDL_min(1.0f, barShown + 0.25f) : SDL_max(0.0f, barShown - 0.25f);
	if (barShown <= 0) {
		wasShown = false;
		return;
	}
	if (!wasShown) cv_text_drop(); /* (the language or its fonts may have changed) */
	wasShown = true;
	if (barExitArmed && SDL_GetTicks() - barExitArmed > 3000) barExitArmed = 0;
	int s = ui_touch_screen();
	memset(barPx, 0, sizeof(barPx));
	SDL_Rect b = bar_rect_g();
	cv_round((SDL_Rect) { b.x - 1, b.y - 1, b.w + 2, b.h + 2 }, 4, G_EDGE);
	cv_round(b, 3, G_BG);
	char buf[3][24] = { "", "", "" };
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) snprintf(buf[0], sizeof(buf[0]), "%02d:%02d", tm.tm_hour, tm.tm_min);
	int pct = battery_percent();
	if (fps > 0) snprintf(buf[1], sizeof(buf[1]), "%.1f FPS", fps);
	else snprintf(buf[1], sizeof(buf[1]), "— FPS"); /* (the first second) */
	if (pct >= 0) snprintf(buf[2], sizeof(buf[2]), "%d%%", pct);
	else snprintf(buf[2], sizeof(buf[2]), "—"); /* (no battery found) */
	for (int i = 0; i < 3; ++i) {
		SDL_Rect c = bar_cell_g(i);
		cv_label(c.x + c.w / 2, c.y + c.h / 2, true, buf[i], G_TEXT, c.w - 2);
	}
	if (ctlProf) ctl_draw();
	else if (sunHere) sun_draw();
	static const char* const* const art[BB_COUNT] = { icon_save, icon_load, icon_gear, icon_bug, icon_exit };
	bool lit = barLit >= 0 && SDL_GetTicks() - barLitAt < 1500;
	for (int i = 0; i < BB_COUNT; ++i) {
		SDL_Rect r = bar_button_g(i);
		bool down = barGesture == G_BTN && barBtn == i;
		bool armed = i == BB_EXIT && barExitArmed;
		bool en = i != BB_LOAD || file_exists(gp.quick);
		bool done = lit && barLit == i; /* (just saved, loaded or reported) */
		/* the menus' keys: raised, in the menu colour while pressed or armed, lighter for a moment once done */
		cv_key(r, down || armed ? KEY_SEL : done ? KEY_LIT : KEY_PLAIN);
		SDL_Color ink = !en ? G_DIS : done ? (SDL_Color) { 20, 16, 36, 255 } : down || armed ? G_LIT : ui_key_ink();
		cv_icon(art[i], r.x + r.w / 2, r.y + r.h / 2, ink);
	}
	/* the pin: a key too, in the menu colour while pinned */
	SDL_Rect pin = bar_pin_g();
	cv_key(pin, cfg->barPinned ? KEY_SEL : KEY_PLAIN);
	cv_icon(icon_pin, pin.x + pin.w / 2, pin.y + pin.h / 2, cfg->barPinned ? G_LIT : G_DIS);
	/* messages (the OSD's, while the bar covers where it's drawn) */
	const char* msg = ui_osd_text();
	if (msg) {
		SDL_Rect last = bar_button_g(0);
		cv_message((SDL_Rect) { b.x + 4, last.y + last.h + 2, b.w - 8, BAR_MSG_H - 3 }, msg, G_TEXT);
	}
	/* up to the screen: one texture per view, made again for a new renderer; drawn in the picture's place, nearest */
	for (int v = 0; v < nViews; ++v) {
		if (barTex[v] && barTexRen[v] != views[v].ren) barTex[v] = NULL;
		if (!barTex[v]) {
			barTex[v] = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
			barTexRen[v] = views[v].ren;
			if (!barTex[v]) continue;
			SDL_SetTextureBlendMode(barTex[v], SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(barTex[v], SDL_ScaleModeNearest);
		}
		ui_update_texture_plain(barTex[v], barPx, GBA_W * 4);
	}
	SDL_Rect pic = bar_pic_full(s), sr = screens[s].rect;
	SDL_Rect clip = { sr.x, sr.y, sr.w, sr.h };
	ui_clip(s, &clip);
	ui_image(s, barTex, GBA_W, GBA_H, "stretch", &pic);
	ui_clip(s, NULL);
}

static void set_page(int page) {
	curPage = page;
	nMenuItems = pageLen[page];
	memcpy(menuItems, pages[page], sizeof(int) * nMenuItems);
	nMenuRows = layout_rows(menuItems, nMenuItems, itemRow, itemSide);
	menuSel = 0;
	itemTop = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	armedItem = -1;
	rowSel = page_is_rows(page) ? 1 : 0;
	rowTop = 0;
	rowPressed = -1;
}

/* D-pad up / down: the row above or below (round at the ends), the same side of a split row */
static void menu_move(int dir) {
	/* from the last row a tall button covers going down, the first going up */
	int from = dir > 0 ? itemRow[menuSel] + item_span(menuItems[menuSel]) - 1 : itemRow[menuSel];
	int row = (from + dir + nMenuRows) % nMenuRows, side = SDL_max(0, itemSide[menuSel]);
	int to = -1;
	for (int i = 0; i < nMenuItems; ++i) {
		if (row < itemRow[i] || row >= itemRow[i] + item_span(menuItems[i])) continue;
		if (to < 0 || itemSide[i] == side) to = i;
	}
	if (to >= 0) menuSel = to;
	items_scroll_to_sel();
}

static int tab_at(int scr, int x, int y, int n) {
	for (int t = 0; t < n; ++t) {
		SDL_Rect r = top_box(scr, t, n);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return t;
	}
	return -1;
}

static int item_at(int scr, int x, int y) {
	for (int i = 0; i < nMenuItems; ++i) {
		if (!item_shown(i)) continue;
		SDL_Rect r = item_rect(scr, i);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
	}
	return -1;
}

/* Display > Shader: the label and the note under the buttons, by SH_* */
#define SHADER_ON (cfg->shader != SH_OFF)
static const char* const shaderLabels[SH_COUNT] = {
	N_("Shader: Off"), N_("Shader: LCD Grid"), N_("Shader: Scanlines"), N_("Shader: Soft Cells"),
	N_("Shader: Subpixels (Light)"), N_("Shader: Subpixels"), N_("Shader: Subpixels (Strong)"),
};
static const char* const shaderNotes[SH_COUNT] = {
	N_("The game's pixels as they are"),
	N_("Lines between the pixels, like a GBA's screen"),
	N_("A dark line under each row of pixels"),
	N_("Each pixel a little darker toward its edges"),
	N_("Red, green and blue stripes, gentle: for dim screens"),
	N_("Red, green and blue stripes, like a GBA's screen"),
	N_("Red, green and blue stripes, strong: for bright screens"),
};
static const char* shader_label(int sh) {
	return shaderLabels[sh >= 0 && sh < SH_COUNT ? sh : 0];
}
/* Display > LCD Ghosting: the game's last frame (as the emulator drew it),
 * to show each frame half and half with it */
static uint32_t ghostPrev[GBA_W * GBA_H], ghostOut[GBA_W * GBA_H];
static bool ghostHave;
static uint64_t ghostFrame;
static const uint32_t* ghost_frame(const uint32_t* fb) {
	if (ghostHave && frameCount == ghostFrame) return ghostOut; /* (the same frame drawn again) */
	if (!ghostHave) {
		memcpy(ghostPrev, fb, sizeof(ghostPrev));
		ghostHave = true;
	}
	/* half this frame, half the one before: a sprite the game flickers
	 * every other frame shows half see-through, as on the GBA's slow LCD */
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		uint32_t a = fb[i], b = ghostPrev[i];
		ghostOut[i] = (a | b) - (((a ^ b) & 0xFEFEFEFEu) >> 1); /* the rounded-up average of each byte */
		ghostPrev[i] = a;
	}
	ghostFrame = frameCount;
	return ghostOut;
}

/* RRGGBB to 0xRRGGBB */
static uint32_t hex_rgb(const char* h) {
	return (uint32_t) strtoul(h, NULL, 16) & 0xFFFFFF;
}

/* ---------------------------------------------------------------- Display > Overlays
 *
 * A screen of its own (like Cheats): Back and the title on top, then the
 * rows: Overlays (Off / Generated / Custom (in /overlays)), Which Screen
 * (not when Off), and for Generated the logo, the two bezel colours and
 * Reset Bezels. D-pad / A (LEFT / RIGHT also cycle), B back; touch taps.
 * The colours open the picker; its Y (or a tap on the hex value) the hex
 * keypad. The game screen isn't dimmed meanwhile, so the bezel shows as it
 * will be, the picker's colour too as it's picked (draw_bezel). */
static bool ovlOpen;
static int ovlSel, ovlPress = -1;
enum { OR_MODE, OR_SCREENS, OR_LOGO, OR_INNER, OR_OUTER, OR_RESET };

static int ovl_rows(int* rows) {
	int n = 0;
	rows[n++] = OR_MODE;
	if (cfg->overlayMode != OVM_OFF) rows[n++] = OR_SCREENS;
	if (cfg->overlayMode == OVM_GENERATED) rows[n++] = OR_LOGO, rows[n++] = OR_INNER, rows[n++] = OR_OUTER, rows[n++] = OR_RESET;
	return n;
}
static SDL_Rect ovl_row_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	return (SDL_Rect) { g.margin, g.top + i * (g.h + g.gap), g.w, g.h };
}
static const char* ovl_row_label(int row) {
	static const char* const modes[3] = { N_("Overlays: Off"), N_("Overlays: Generated"), N_("Overlays: Custom (in /overlays)")};
	switch (row) {
	case OR_MODE: return modes[SDL_clamp(cfg->overlayMode, 0, 2)];
	case OR_SCREENS: return cfg->overlays == OVL_TOP ? N_("Which Screen: Top Only"): cfg->overlays == OVL_BOTTOM ? N_("Which Screen: Bottom Only"): N_("Which Screen: Both");
	case OR_LOGO: return cfg->bezelLogo ? N_("GBA DS Logo: Show"): N_("GBA DS Logo: Hide");
	case OR_INNER: return N_("Inner Bezel Color");
	case OR_OUTER: return N_("Outer Bezel Color");
	default: return N_("Reset Bezels");
	}
}

/* -------- the colour picker: hue / saturation / value, as the bezel shows it */
static int pickWhich = -1; /* 0 inner, 1 outer; -1 closed */
static float pickH, pickS, pickV;
static int pickPreset = -1; /* the last preset X stepped to */
static int pickDrag;        /* finger on 1 the square, 2 the hue strip */
static SDL_Texture* pickSqTex[2], *pickHueTex[2];
static float pickSqHue = -1;
static int pickTexW, pickTexH;
static const uint32_t pickPresets[2][8] = {
	{ 0x1A1A1A, 0x000000, 0x2B2B2B, 0x3C3C3C, 0x102060, 0x2A1F55, 0x3A1020, 0xE8E6E3 },
	{ 0x102060, 0x4B3A8C, 0x000000, 0x9FB7C9, 0xC9C9C9, 0xB03A7A, 0xD66A1E, 0xE8E6E3 },
};

static uint32_t hsv_rgb(float h, float s, float v) {
	float c = v * s, x = c * (1 - fabsf(fmodf(h / 60, 2) - 1)), m = v - c, r = 0, g = 0, b = 0;
	if (h < 60) r = c, g = x;
	else if (h < 120) r = x, g = c;
	else if (h < 180) g = c, b = x;
	else if (h < 240) g = x, b = c;
	else if (h < 300) r = x, b = c;
	else r = c, b = x;
	return ((uint32_t) lroundf((r + m) * 255) << 16) | ((uint32_t) lroundf((g + m) * 255) << 8) | (uint32_t) lroundf((b + m) * 255);
}
static void rgb_hsv(uint32_t rgb, float* h, float* s, float* v) {
	float r = ((rgb >> 16) & 0xFF) / 255.0f, g = ((rgb >> 8) & 0xFF) / 255.0f, b = (rgb & 0xFF) / 255.0f;
	float mx = fmaxf(r, fmaxf(g, b)), mn = fminf(r, fminf(g, b)), d = mx - mn;
	float hh = 0;
	if (d > 0) {
		if (mx == r) hh = fmodf((g - b) / d, 6);
		else if (mx == g) hh = (b - r) / d + 2;
		else hh = (r - g) / d + 4;
		hh *= 60;
		if (hh < 0) hh += 360;
	}
	*h = hh, *s = mx > 0 ? d / mx : 0, *v = mx;
}
static uint32_t pick_rgb(void) {
	return hsv_rgb(pickH, pickS, pickV);
}
static SDL_Color rgb_color(uint32_t rgb) {
	return (SDL_Color) { (rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF, 255 };
}
static void pick_open(int which) {
	pickWhich = which;
	rgb_hsv(hex_rgb(which ? cfg->bezelOuter : cfg->bezelInner), &pickH, &pickS, &pickV);
	pickPreset = -1;
	pickDrag = 0;
}
static void pick_close(bool keep) {
	if (keep && pickWhich >= 0) {
		snprintf(pickWhich ? cfg->bezelOuter : cfg->bezelInner, 8, "%06X", (unsigned) pick_rgb());
		save_prefs();
	}
	pickWhich = -1;
	pickDrag = 0;
}

typedef struct {
	SDL_Rect title, sq, hue, now, next, presets[8], hex, cancel, ok;
	int hintY, cap, small;
} PickGeom;
static PickGeom pick_geom(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	PickGeom p;
	int m = mg.margin, gap = mg.gap * 2;
	p.cap = mg.cap;
	p.small = mg.cap * 2 / 3;
	p.title = (SDL_Rect) { m, mg.tabY, mg.w, mg.h };
	int bh = mg.h, hw = (mg.w - mg.gap) / 2;
	p.cancel = (SDL_Rect) { m, r.h - m - bh, hw, bh };
	p.ok = (SDL_Rect) { m + mg.w - hw, r.h - m - bh, hw, bh };
	p.hintY = p.cancel.y - mg.gap * 2 - p.small;
	int top = p.title.y + p.title.h + gap, bot = p.hintY - p.small - mg.gap;
	int side = SDL_min(bot - top, mg.w * 9 / 20);
	p.sq = (SDL_Rect) { m, top, side, side };
	p.hue = (SDL_Rect) { m + side + gap, top, SDL_max(8, side / 9), side };
	int rx = p.hue.x + p.hue.w + gap, rw = m + mg.w - rx;
	int prevH = side / 5;
	p.now = (SDL_Rect) { rx, top, rw / 2, prevH };
	p.next = (SDL_Rect) { rx + rw / 2, top, rw - rw / 2, prevH };
	int hexH = SDL_min(mg.h, side / 5);
	p.hex = (SDL_Rect) { rx, top + side - hexH, rw, hexH };
	int py = p.now.y + prevH + gap, ph = (p.hex.y - gap - py - mg.gap) / 2, pw = (rw - mg.gap * 3) / 4;
	for (int i = 0; i < 8; ++i) p.presets[i] = (SDL_Rect) { rx + (i % 4) * (pw + mg.gap), py + (i / 4) * (ph + mg.gap), pw, ph };
	return p;
}
static bool in_rect(SDL_Rect r, int x, int y) {
	return x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h;
}
static void pick_touch(int scr, int x, int y) {
	PickGeom p = pick_geom(scr);
	if (pickDrag == 1) {
		pickS = SDL_clamp((float) (x - p.sq.x) / p.sq.w, 0.0f, 1.0f);
		pickV = SDL_clamp(1 - (float) (y - p.sq.y) / p.sq.h, 0.0f, 1.0f);
	} else if (pickDrag == 2) {
		pickH = SDL_clamp((float) (y - p.hue.y) / p.hue.h * 360, 0.0f, 359.9f);
	}
}
/* the textures: the square for the current hue (made again when it moves), the hue strip once */
static void pick_textures(void) {
	const int n = 64;
	if (!pickHueTex[0] && !pickHueTex[1]) {
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 1, 360, 32, SDL_PIXELFORMAT_RGBA32);
		if (s) {
			uint8_t* px = s->pixels;
			for (int y = 0; y < 360; ++y) {
				uint32_t c = hsv_rgb((float) y, 1, 1);
				uint8_t* q = px + y * s->pitch;
				q[0] = c >> 16, q[1] = c >> 8, q[2] = c, q[3] = 255;
			}
			int w, h;
			ui_load_surface(s, pickHueTex, &w, &h, true);
		}
	}
	if (pickSqHue != pickH) {
		ui_free_image(pickSqTex);
		pickSqHue = pickH;
		SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, n, n, 32, SDL_PIXELFORMAT_RGBA32);
		if (s) {
			uint8_t* px = s->pixels;
			for (int y = 0; y < n; ++y) {
				for (int x = 0; x < n; ++x) {
					uint32_t c = hsv_rgb(pickH, x / (float) (n - 1), 1 - y / (float) (n - 1));
					uint8_t* q = px + y * s->pitch + x * 4;
					q[0] = c >> 16, q[1] = c >> 8, q[2] = c, q[3] = 255;
				}
			}
			ui_load_surface(s, pickSqTex, &pickTexW, &pickTexH, true);
		}
	}
}
/* a ring marker centred on (x, y) */
static void draw_ring(int scr, int x, int y, int r, SDL_Color fill) {
	ui_fill_round(scr, (SDL_Rect) { x - r - 2, y - r - 2, r * 2 + 4, r * 2 + 4 }, (SDL_Color) { 0, 0, 0, 255 }, r + 2);
	ui_fill_round(scr, (SDL_Rect) { x - r, y - r, r * 2, r * 2 }, (SDL_Color) { 255, 255, 255, 255 }, r);
	int k = SDL_max(1, r * 2 / 3);
	ui_fill_round(scr, (SDL_Rect) { x - k, y - k, k * 2, k * 2 }, fill, k);
}
/* a small button prompt ("A", "L1") and its words after it; returns the x after them */
static int draw_btn_hint(int scr, int x, int cy, int cap, const char* btn, const char* words) {
	int bw = ui_label_width(scr, cap, btn) + cap;
	ui_fill_round(scr, (SDL_Rect) { x, cy - cap, bw, cap * 2 }, C_TEXT, cap / 2);
	ui_label(scr, x + bw / 2, cy, cap, btn, (SDL_Color) { 20, 16, 36, 255 });
	ui_font_text(scr, TF_BODY, x + bw + cap / 2, cy, cap, words, C_DIS, -1);
	return x + bw + cap / 2 + ui_font_width(TF_BODY, cap, words) + cap * 2;
}
static int hint_width(int scr, int cap, const char* btn, const char* words) {
	return ui_label_width(scr, cap, btn) + cap + cap / 2 + ui_font_width(TF_BODY, cap, words) + cap * 2;
}

/* -------- the hex keypad (Y in the picker, or a tap on the hex value) */
static bool hexOpen;
static char hexText[8];
static int hexLen, hexRow, hexCol, hexPress = -1;
static const char* const hexKeys[3][8] = {
	{ "0", "1", "2", "3", "4", "5", "6", "7" },
	{ "8", "9", "A", "B", "C", "D", "E", "F" },
	{ N_("Del"), N_("Done")},
};
static int hex_row_len(int r) {
	return r == 2 ? 2 : 8;
}
static SDL_Rect hex_field(int scr) {
	MenuGeom mg = menu_geom(scr);
	return (SDL_Rect) { mg.margin, mg.top, mg.w, mg.h * 3 / 2 };
}
static SDL_Rect hex_key_rect(int scr, int r, int c) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect f = hex_field(scr);
	int top = f.y + f.h + mg.gap * 2, rowH = (screens[scr].rect.h - mg.margin - top - mg.gap * 2) / 3;
	int n = hex_row_len(r), kw = (mg.w - mg.gap * (n - 1)) / n;
	return (SDL_Rect) { mg.margin + c * (kw + mg.gap), top + r * (rowH + mg.gap), c == n - 1 ? mg.margin + mg.w - (mg.margin + c * (kw + mg.gap)) : kw, rowH };
}
static bool hex_key_at(int scr, int x, int y, int* r, int* c) {
	for (int i = 0; i < 3; ++i)
		for (int j = 0; j < hex_row_len(i); ++j)
			if (in_rect(hex_key_rect(scr, i, j), x, y)) return *r = i, *c = j, true;
	return false;
}
static void hex_open(void) {
	hexOpen = true;
	hexLen = 0, hexText[0] = 0;
	hexRow = 0, hexCol = 0;
	hexPress = -1;
}
/* Done: six digits (or three, #ABC = #AABBCC) become the picker's colour */
static void hex_done(void) {
	if (hexLen != 6 && hexLen != 3) return;
	char full[sizeof(hexText)];
	if (hexLen == 3) snprintf(full, sizeof(full), "%c%c%c%c%c%c", hexText[0], hexText[0], hexText[1], hexText[1], hexText[2], hexText[2]);
	else snprintf(full, sizeof(full), "%s", hexText);
	rgb_hsv(hex_rgb(full), &pickH, &pickS, &pickV);
	hexOpen = false;
}
static void hex_press(int r, int c) {
	if (r == 2) {
		if (c == 0) {
			if (hexLen) hexText[--hexLen] = 0;
		} else {
			hex_done();
		}
		return;
	}
	if (hexLen < 6) hexText[hexLen++] = hexKeys[r][c][0], hexText[hexLen] = 0;
}
static void hex_move(int dr, int dc) {
	if (dc) {
		int n = hex_row_len(hexRow);
		hexCol = (hexCol + dc + n) % n;
		return;
	}
	int from = hex_row_len(hexRow);
	hexRow = (hexRow + dr + 3) % 3;
	int n = hex_row_len(hexRow);
	hexCol = SDL_min(n - 1, hexCol * n / from);
}

/* -------- drawing */
static void draw_ovl_menu(int scr) {
	MenuGeom mg = menu_geom(scr);
	int rad = mg.rad, cap = mg.cap;
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Overlays", (SDL_Color) { 20, 16, 36, 255 });
	int rows[8], n = ovl_rows(rows);
	if (ovlSel >= n) ovlSel = n - 1;
	for (int i = 0; i < n; ++i) {
		SDL_Rect r = ovl_row_rect(scr, i);
		bool sel = i == ovlSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		const char* label = ovl_row_label(rows[i]);
		int lc = SDL_min(cap, cap * (r.w - mg.pad * 2) / SDL_max(1, ui_label_width(scr, cap, label)));
		if (rows[i] == OR_INNER || rows[i] == OR_OUTER) {
			/* the label, its swatch and the hex value, centred together */
			const char* hex = rows[i] == OR_INNER ? cfg->bezelInner : cfg->bezelOuter;
			char hv[10];
			snprintf(hv, sizeof(hv), "#%s", hex);
			int sw = mg.h * 3 / 5, hc = cap * 4 / 5, gap = mg.pad / 2;
			int lw = ui_label_width(scr, cap, label), hw = ui_label_width(scr, hc, hv);
			int x = r.x + (r.w - (lw + gap + sw + gap + hw)) / 2, cy = r.y + r.h / 2;
			ui_label(scr, x + lw / 2, cy, cap, label, C_TEXT);
			x += lw + gap;
			int e2 = SDL_max(2, sw / 12);
			ui_fill_round(scr, (SDL_Rect) { x, cy - sw / 2, sw, sw }, C_TEXT, SDL_max(2, sw / 6));
			ui_fill_round(scr, (SDL_Rect) { x + e2, cy - sw / 2 + e2, sw - e2 * 2, sw - e2 * 2 }, rgb_color(hex_rgb(hex)), SDL_max(1, sw / 8));
			x += sw + gap;
			ui_label(scr, x + hw / 2, cy, hc, hv, (SDL_Color) { 185, 179, 211, 255 });
		} else {
			ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, lc, label, C_TEXT);
		}
	}
	if (cfg->overlayMode == OVM_CUSTOM) {
		SDL_Rect last = ovl_row_rect(scr, n - 1);
		ui_font_wrap(scr, TF_BODY, mg.margin + mg.pad, last.y + last.h + mg.gap * 2, mg.w - mg.pad * 2, mg.noteCap,
		             "Your own PNGs in gba-ds/overlays/. A screen without one gets the generated bezel in its default colors.", C_DIS);
	}
}

static void draw_picker(int scr) {
	PickGeom p = pick_geom(scr);
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 12, 10, 22, 255 });
	ui_fill_round(scr, p.title, C_BTN_EDGE, mg.rad);
	ui_label(scr, p.title.x + p.title.w / 2, p.title.y + p.title.h / 2, p.cap, pickWhich ? "Outer Bezel Color" : "Inner Bezel Color", (SDL_Color) { 20, 16, 36, 255 });
	pick_textures();
	ui_image(scr, pickSqTex, pickTexW, pickTexH, "stretch", &p.sq);
	ui_image(scr, pickHueTex, 1, 360, "stretch", &p.hue);
	uint32_t now = pick_rgb();
	int rr = SDL_max(6, p.sq.w / 28);
	draw_ring(scr, p.sq.x + (int) lroundf(pickS * p.sq.w), p.sq.y + (int) lroundf((1 - pickV) * p.sq.h), rr, rgb_color(now));
	int hy = p.hue.y + (int) lroundf(pickH / 360 * p.hue.h);
	ui_fill_round(scr, (SDL_Rect) { p.hue.x - 6, hy - 6, p.hue.w + 12, 12 }, (SDL_Color) { 0, 0, 0, 255 }, 5);
	ui_fill_round(scr, (SDL_Rect) { p.hue.x - 4, hy - 4, p.hue.w + 8, 8 }, (SDL_Color) { 255, 255, 255, 255 }, 3);
	/* now / new */
	uint32_t was = hex_rgb(pickWhich ? cfg->bezelOuter : cfg->bezelInner);
	ui_fill(scr, p.now, rgb_color(was));
	ui_fill(scr, p.next, rgb_color(now));
	ui_font_text(scr, TF_BOLD, p.now.x + mg.pad / 2, p.now.y + p.now.h - p.small, p.small, "Now", C_TEXT, -1);
	ui_font_text(scr, TF_BOLD, p.next.x + mg.pad / 2, p.next.y + p.next.h - p.small, p.small, "New", C_TEXT, -1);
	for (int i = 0; i < 8; ++i) {
		uint32_t c = pickPresets[pickWhich][i];
		SDL_Rect q = p.presets[i];
		bool on = c == now;
		int e = on ? SDL_max(3, q.h / 10) : 2;
		ui_fill_round(scr, q, on ? C_TEXT : (SDL_Color) { 58, 54, 80, 255 }, mg.rad);
		ui_fill_round(scr, (SDL_Rect) { q.x + e, q.y + e, q.w - e * 2, q.h - e * 2 }, rgb_color(c), SDL_max(1, mg.rad - e));
	}
	char hv[12];
	snprintf(hv, sizeof(hv), "#%06X", (unsigned) now);
	ui_fill_round(scr, p.hex, (SDL_Color) { 30, 26, 50, 255 }, mg.rad);
	ui_label(scr, p.hex.x + p.hex.w / 2, p.hex.y + p.hex.h / 2, SDL_min(p.cap, p.hex.h * 2 / 5), hv, C_TEXT);
	/* the buttons, centred in a line */
	const char* btn[4] = { N_("D-pad"), "L1 R1", "X", "Y" };
	const char* words[4] = { N_("color"), N_("hue"), N_("presets"), N_("hex")};
	int hc = p.small, tw = 0;
	for (int i = 0; i < 4; ++i) tw += hint_width(scr, hc, btn[i], words[i]);
	if (tw > mg.w) hc = hc * mg.w / tw, tw = mg.w;
	int x = (r.w - tw) / 2;
	for (int i = 0; i < 4; ++i) x = draw_btn_hint(scr, x, p.hintY, hc, btn[i], words[i]);
	ui_key(scr, p.cancel, mg.rad, KEY_PLAIN);
	ui_label(scr, p.cancel.x + p.cancel.w / 2, p.cancel.y + p.cancel.h / 2, p.cap, "Cancel", C_TEXT);
	ui_key(scr, p.ok, mg.rad, KEY_SEL);
	ui_label(scr, p.ok.x + p.ok.w / 2, p.ok.y + p.ok.h / 2, p.cap, "OK", C_TEXT);
}

static void draw_hex(int scr) {
	MenuGeom mg = menu_geom(scr);
	SDL_Rect r = screens[scr].rect;
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 12, 10, 22, 255 });
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, mg.rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, mg.cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, mg.rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, mg.cap, "Hex Color", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect f = hex_field(scr);
	ui_fill_round(scr, f, (SDL_Color) { 30, 26, 50, 255 }, mg.rad);
	char shown[16];
	snprintf(shown, sizeof(shown), "#%s%s", hexText, hexLen < 6 && (SDL_GetTicks() / 500) & 1 ? "|" : "");
	ui_label(scr, f.x + f.w / 2, f.y + f.h / 2, mg.cap * 3 / 2, shown, hexLen ? C_TEXT : C_DIS);
	bool canDone = hexLen == 6 || hexLen == 3;
	for (int i = 0; i < 3; ++i) {
		for (int j = 0; j < hex_row_len(i); ++j) {
			SDL_Rect k = hex_key_rect(scr, i, j);
			bool sel = i == hexRow && j == hexCol;
			ui_key(scr, k, mg.rad, sel ? KEY_SEL : KEY_PLAIN);
			bool dim = i == 2 && j == 1 && !canDone;
			ui_label(scr, k.x + k.w / 2, k.y + k.h / 2, SDL_min(mg.cap, k.h * 2 / 5), hexKeys[i][j], dim ? C_DIS : C_TEXT);
		}
	}
}

/* -------- what a row does (dir: -1 LEFT, else forward) */
static void ovl_activate(int i, int dir) {
	int rows[8], n = ovl_rows(rows);
	if (i < 0 || i >= n) return;
	switch (rows[i]) {
	case OR_MODE:
		cfg->overlayMode = (SDL_clamp(cfg->overlayMode, 0, 2) + (dir < 0 ? 2 : 1)) % 3;
		break;
	case OR_SCREENS: {
		static const int order[3] = { OVL_BOTH, OVL_TOP, OVL_BOTTOM };
		int k = cfg->overlays == OVL_TOP ? 1 : cfg->overlays == OVL_BOTTOM ? 2 : 0;
		cfg->overlays = order[(k + (dir < 0 ? 2 : 1)) % 3];
		break;
	}
	case OR_LOGO:
		cfg->bezelLogo = !cfg->bezelLogo;
		break;
	case OR_INNER:
	case OR_OUTER:
		pick_open(rows[i] == OR_OUTER);
		return;
	case OR_RESET:
		cfg->bezelLogo = true;
		snprintf(cfg->bezelInner, sizeof(cfg->bezelInner), "%06X", BEZEL_INNER_DEFAULT);
		snprintf(cfg->bezelOuter, sizeof(cfg->bezelOuter), "%06X", BEZEL_OUTER_DEFAULT);
		break;
	}
	commit_current("overlays"); /* (the layer in force) */
	commit_current("overlay_screens");
	commit_current("bezel_logo");
	save_prefs();
}
static void ovl_close_all(void) {
	ovlOpen = false;
	hexOpen = false;
	pickWhich = -1;
	pickDrag = 0;
}

/* ---------------------------------------------------------------- face buttons
 *
 * GEN-10: which GBA button each face button gives the game, by position
 * (right, bottom, top, left): GBA A / B / L / R, Start, Select, Turbo A /
 * Turbo B or nothing. Controls > the four button rows (button_right ... in
 * gba-ds.cfg, for this game or for all games); with none chosen, ab_swap's
 * layout. Before v1.5.4 a game's choices were kept in <game>.buttons: read
 * once into the game's own settings (fb_load). Only the game's keys change
 * (run loop, the GBA layout's branch): GBA DS's own screens keep B_A / B_B.
 * In games with a layout picker (profile `controls`), the SNES / four-button
 * layout decides the face buttons wherever it's in use. */
enum { FB_RIGHT, FB_BOTTOM, FB_TOP, FB_LEFT, FB_FACES, FB_L = FB_FACES, FB_R, FB_UP, FB_DOWN, FB_PADLEFT, FB_PADRIGHT, FB_COUNT };
enum { FG_A, FG_B, FG_L, FG_R, FG_UP, FG_DOWN, FG_LEFT, FG_RIGHT, FG_START, FG_SELECT, FG_TURBO_A, FG_TURBO_B, FG_NONE, FG_COUNT };
static const char* const fbNames[FB_COUNT] = { "right", "bottom", "top", "left", "l", "r", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
static const char* const fgNames[FG_COUNT] = { "a", "b", "l", "r", "up", "down", "left", "right", "start", "select", "turbo_a", "turbo_b", "none" };
static const char* const fgLabels[FG_COUNT] = { "GBA A", "GBA B", "GBA L", "GBA R", "GBA Up", "GBA Down", "GBA Left", "GBA Right", "Start", "Select", "Turbo A", "Turbo B", "Nothing" };
static int fbMap[FB_COUNT]; /* each face button's FG_ */
static bool fbSet;          /* a choice in force (else ab_swap's) */

/* from the settings in force (called again whenever one changes) */
static void fb_default(void) {
	fbMap[FB_RIGHT] = cfg->abSwap ? FG_B : FG_A;
	fbMap[FB_BOTTOM] = cfg->abSwap ? FG_A : FG_B;
	fbMap[FB_TOP] = fbMap[FB_LEFT] = FG_NONE;
	fbMap[FB_L] = FG_L, fbMap[FB_R] = FG_R, fbMap[FB_UP] = FG_UP, fbMap[FB_DOWN] = FG_DOWN, fbMap[FB_PADLEFT] = FG_LEFT, fbMap[FB_PADRIGHT] = FG_RIGHT;
	fbSet = false;
	for (int i = 0; i < FB_COUNT; ++i)
		for (int j = 0; j < FG_COUNT && cfg->faceBtn[i][0]; ++j)
			if (!strcmp(cfg->faceBtn[i], fgNames[j])) fbMap[i] = j, fbSet = true;
}

/* "right GBA A, bottom GBA B, ..." for the log and bug reports */
static const char* fb_describe(void) {
	static char s[320];
	s[0] = 0;
	for (int i = 0; i < FB_COUNT; ++i)
		snprintf(s + strlen(s), sizeof(s) - strlen(s), "%s%s %s", i ? ", " : "", fbNames[i], fgLabels[fbMap[i]]);
	return s;
}

static void game_keys_reload(void);
static void fb_load(void) {
	/* a game's <game>.buttons from before v1.5.4: moved into its own settings */
	char old[1400];
	snprintf(old, sizeof(old), "%s/%s.buttons", cfg->stateDir, gp.base);
	FILE* f = fopen(old, "r");
	if (f) {
		char k[16], v[16], lines[FB_COUNT][2][24];
		const char* kv[FB_COUNT][2];
		int n = 0;
		while (fscanf(f, "%15s %15s", k, v) == 2 && n < FB_COUNT) {
			for (int i = 0; i < FB_COUNT; ++i) {
				if (strcmp(k, fbNames[i])) continue;
				snprintf(lines[n][0], 24, "button_%s", fbNames[i]);
				snprintf(lines[n][1], 24, "%s", v);
				kv[n][0] = lines[n][0], kv[n][1] = lines[n][1];
				++n;
			}
		}
		fclose(f);
		if (n) {
			config_kv_update(gp.settings, "# this game's own settings: they win over the ones for all games", n, kv);
			config_load_game_file(cfg, gp.settings); /* (put over the settings now) */
			fprintf(stderr, "buttons: %s.buttons moved into %s\n", gp.base, gp.settings);
		}
		remove(old);
		game_keys_reload();
	}
	fb_default();
	if (fbSet) fprintf(stderr, "buttons: %s\n", fb_describe());
}

/* the GBA keys the face buttons give the game (the GBA layout) */
static uint32_t fb_keys(bool faces) {
	static const uint32_t key[FG_COUNT] = { K_A, K_B, K_L, K_R, K_UP, K_DOWN, K_LEFT, K_RIGHT, K_START, K_SELECT, K_A, K_B, 0 };
	/* Turbo A / B (#55): the key goes down and up turbo_rate times a second (60 frames a second) */
	int period = SDL_clamp(60 / SDL_max(1, cfg->turboRate), 2, 12);
	bool turboOn = (frameCount % (uint64_t) period) < (uint64_t) (period / 2);
	/* (ui.c puts the bottom button on B_A with ab_swap: by position again) */
	int phys[FB_COUNT] = { cfg->abSwap ? B_B : B_A, cfg->abSwap ? B_A : B_B, B_X, B_Y, B_L, B_R, B_UP, B_DOWN, B_LEFT, B_RIGHT };
	uint32_t keys = 0;
	/* faces only, or the shoulders and the D-pad (only = false) */
	for (int i = faces ? 0 : FB_FACES; i < (faces ? FB_FACES : FB_COUNT); ++i)
		if (HELD(phys[i]) && (fbMap[i] != FG_TURBO_A && fbMap[i] != FG_TURBO_B ? true : turboOn)) keys |= key[fbMap[i]];
	return keys;
}

static const char* scale_label(const char* scale) {
	return !strcmp(scale, "fit")       ? N_("Scale: Full Width"):
	       !strcmp(scale, "zoom")      ? N_("Scale: Zoom"):
	       !strcmp(scale, "stretch")   ? N_("Scale: Stretch"): N_("Scale: Pixel Perfect");
}

/* ---------------------------------------------------------------- settings pages
 *
 * Display, Screens, Controls, Speed and Map are rows of settings (config.c's
 * table), each changed for this game (A, or a tap with "This game" lit) or
 * for all games (Y, or "All games" lit): this game's go to states/<game>.cfg,
 * all games' to states/prefs.cfg. A row with a value of its own for this game
 * wears a "Game" tag; X (or a tap on the tag) takes it away. Row 0 is the
 * This game / All games bar. */
static char gameKeys[48][40]; /* the keys states/<game>.cfg holds */
static int nGameKeys;
static bool game_has_key(const char* k) {
	for (int i = 0; i < nGameKeys; ++i)
		if (!strcmp(gameKeys[i], k)) return true;
	return false;
}
static bool game_has(const Setting* st) {
	return st->field == SF_FF ? game_has_key("fast_forward") || game_has_key("ff_speed") : game_has_key(st->key);
}

/* row i (0 the bar, 1... the settings) on screen; a row scrolled off sits above or below the rows shown */
static SDL_Rect srow_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	int slot = i == 0 ? 0 : i - rowTop;
	return (SDL_Rect) { g.margin, g.top + slot * (g.h + g.gap), g.w, g.h };
}
static bool srow_shown(int i) {
	return i == 0 || (i > rowTop && i <= rowTop + ROW_VIS);
}
/* the selected row kept on screen */
static void rows_scroll_to_sel(void) {
	if (rowSel >= 1 && rowSel <= rowTop) rowTop = rowSel - 1;
	if (rowSel > rowTop + ROW_VIS) rowTop = rowSel - ROW_VIS;
	rowTop = SDL_clamp(rowTop, 0, SDL_max(0, rowN[curPage] - ROW_VIS));
}
static SDL_Rect scope_rect(int scr, int half) {
	MenuGeom g = menu_geom(scr);
	SDL_Rect r = srow_rect(scr, 0);
	int hw = (r.w - g.gap) / 2;
	return (SDL_Rect) { r.x + half * (r.w - hw), r.y, hw, r.h };
}
static SDL_Rect rowTag[ROW_MAX + 1], rowBack[ROW_MAX + 1]; /* where the last drawing put each row's Game tag and left arrow (for touch) */
static bool rowTagOn[ROW_MAX + 1];

/* why a row can't be changed here, or NULL */
static const char* row_disabled(const char* key) {
	if (!strcmp(key, "frame_buffer") && tallProf) return N_("Always on in this game: it needs it for full speed");
	if (!strcmp(key, "screen_match") && !ui_screens_differ()) return N_("Your screens are the same shape: nothing to match");
	if (!strcmp(key, "screen_placement") && nScreens != 2) return N_("Only one screen: nothing to move");
	if ((!strcmp(key, "auto_rotate") || !strcmp(key, "toggle_button")) && !cfg->toggle) return N_("Turn on the contextual toggle first");
	return NULL;
}

static void draw_rows_page(int scr, const MenuGeom* mg) {
	int cap = mg->cap, rad = mg->rad, n = rowN[curPage];
	/* the bar */
	for (int h = 0; h < 2; ++h) {
		SDL_Rect r = scope_rect(scr, h);
		bool on = (h == 1) == scopeAll;
		ui_key(scr, r, rad, on ? (rowSel == 0 ? KEY_SEL : KEY_LIT) : KEY_PLAIN);
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, cap, h ? "All games" : "This game", on ? (rowSel == 0 ? C_TEXT : (SDL_Color) { 20, 16, 36, 255 }) : C_DIS);
	}
	const char* note = NULL;
	bool noteGold = false;
	rowTop = SDL_clamp(rowTop, 0, SDL_max(0, n - ROW_VIS));
	for (int i = 1; i <= n; ++i) {
		rowTagOn[i] = false;
		if (!srow_shown(i)) continue;
		SDL_Rect r = srow_rect(scr, i);
		bool sel = i == rowSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		const char* key = rowKeys[curPage][i - 1];
		int cy = r.y + r.h / 2, lc = cap * 9 / 10;
		if (key[0] == '@') {
			ui_font_text(scr, TF_BOLD, r.x + mg->pad, cy, lc, "Overlay colors and logo", C_TEXT, -1);
			ui_font_text(scr, TF_BOLD, r.x + r.w - mg->pad, cy, cap, "›", sel ? C_TEXT : C_DIS, 1);
			if (sel) note = N_("Opens the bezel's own page, for whichever the game or all games have");
			continue;
		}
		const Setting* st = config_setting(key);
		if (!st) continue;
		const char* off = row_disabled(key);
		char val[48];
		config_setting_text(cfg, st, val, sizeof(val));
		if (off && !strcmp(key, "frame_buffer")) snprintf(val, sizeof(val), tr("Always on here"));
		bool has = game_has(st);
		SDL_Color vc = off ? C_DIS : C_GOLD;
		ui_font_text(scr, TF_BOLD, r.x + mg->pad, cy, lc, st->label, off ? C_DIS : C_TEXT, -1);
		int lw = ui_font_width(TF_BOLD, lc, st->label);
		int ax = r.x + r.w - mg->pad;
		ui_font_text(scr, TF_BOLD, ax, cy, cap, "›", sel ? C_TEXT : C_DIS, 1);
		int vw = ui_font_width(TF_BOLD, lc, val);
		ui_font_text(scr, TF_BOLD, ax - cap, cy, lc, val, vc, 1);
		int bx = ax - cap * 2 - vw;
		ui_font_text(scr, TF_BOLD, bx, cy, cap, "‹", sel ? C_TEXT : C_DIS, 1);
		rowBack[i] = (SDL_Rect) { bx - cap * 2, r.y, cap * 3, r.h };
		if (has) {
			/* the Game tag, after the label */
			int tc = SDL_max(6, cap * 5 / 8), tw = ui_font_width(TF_BOLD, tc, "Game") + mg->pad;
			rowTag[i] = (SDL_Rect) { r.x + mg->pad + lw + mg->pad / 2, cy - tc, tw, tc * 2 };
			ui_fill_round(scr, rowTag[i], C_GOLD, tc / 2);
			ui_font_text(scr, TF_BOLD, rowTag[i].x + tw / 2, cy, tc, "Game", (SDL_Color) { 20, 16, 36, 255 }, 0);
			rowTagOn[i] = true;
		}
		if (!strcmp(key, "pinball_live") && cfg->tallLive != tallLiveAtStart) {
			/* changed since GBA DS started: it only counts once GBA DS is closed and started again */
			int tc = SDL_max(6, cap * 5 / 8), gp2 = mg->pad / 2;
			int px0 = r.x + mg->pad + lw + gp2 + (has ? ui_font_width(TF_BOLD, tc, "Game") + mg->pad + gp2 : 0);
			const char* txt = N_("Restart required");
			int tw = ui_font_width(TF_BOLD, tc, txt) + mg->pad;
			if (px0 + tw > bx - cap) txt = "Restart", tw = ui_font_width(TF_BOLD, tc, txt) + mg->pad;
			SDL_Rect pr = { px0, cy - tc, tw, tc * 2 };
			ui_fill_round(scr, pr, C_GOLD, tc / 2);
			ui_font_text(scr, TF_BOLD, pr.x + tw / 2, cy, tc, txt, (SDL_Color) { 20, 16, 36, 255 }, 0);
		}
		if (sel) {
			const char* ctl = (!strncmp(key, "button_", 7) && ctlProf) ? (ctlProf->controls.snes.bottom ? N_("With SNES picked under Controls in the quick bar, the SNES layout replaces these in levels."): N_("With Four buttons picked under Controls in the quick bar, the four-button layout replaces these.")) : NULL;
			note = off ? off : has ? N_("This game has its own value. X, or going on past the last choice: same as all games"): ctl ? ctl : st->note;
		}
	}
	if (menuNote[0]) note = menuNote, noteGold = true;
	else if (rowSel == 0) note = N_("A: the one lit above. Y: all games. X: undo this game's value");
	if (n > ROW_VIS) {
		/* more rows than fit: a bar at the right edge, where the rows are in the list */
		SDL_Rect a = srow_rect(scr, rowTop + 1), z = srow_rect(scr, rowTop + ROW_VIS);
		int bw = SDL_max(2, mg->margin / 2), bx = screens[scr].rect.w - mg->margin + (mg->margin - bw) / 2;
		int th = z.y + z.h - a.y;
		ui_fill_round(scr, (SDL_Rect) { bx, a.y, bw, th }, (SDL_Color) { 60, 56, 84, 255 }, bw / 2);
		int bh = SDL_max(bw * 3, th * ROW_VIS / n), by = a.y + (th - bh) * rowTop / SDL_max(1, n - ROW_VIS);
		ui_fill_round(scr, (SDL_Rect) { bx, by, bw, bh }, C_GOLD, bw / 2);
	}
	if (note) {
		SDL_Rect last = srow_rect(scr, SDL_min(n, rowTop + ROW_VIS));
		int nc = SDL_min(mg->noteCap, mg->noteCap * mg->w / SDL_max(1, ui_font_width(TF_BOLD, mg->noteCap, note)));
		/* on a dark panel: the bezel's logo (or the game's picture) can be behind the line */
		int ny = last.y + last.h + mg->h * 3 / 5, nh = nc * 3;
		ui_fill_round(scr, (SDL_Rect) { last.x, ny - nh / 2, last.w, nh }, (SDL_Color) { 8, 6, 18, 245 }, rad);
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, ny, nc, note, noteGold ? C_GOLD : (SDL_Color) { 190, 186, 205, 255 }, 0);
	}
}

/* The tabs' pictures, drawn from rectangles on a 24 x 24 grid (nothing to
 * load): Game a handheld, Tools a toolbox, Display a monitor, Screens two
 * screens, Controls a gamepad, Speed fast forward, Map a pin. fg is the
 * picture, bg what's behind it (its cut-outs). */
static void icon_fill(int scr, SDL_Rect box, int size, float gx, float gy, float gw, float gh, float gr, SDL_Color c) {
	float u = size / 24.0f;
	int x0 = box.x + (box.w - size) / 2, y0 = box.y + (box.h - size) / 2;
	int ix = x0 + (int) lroundf(gx * u), iy = y0 + (int) lroundf(gy * u);
	int iw = SDL_max(1, (int) lroundf((gx + gw) * u) - (int) lroundf(gx * u)), ih = SDL_max(1, (int) lroundf((gy + gh) * u) - (int) lroundf(gy * u));
	ui_fill_round(scr, (SDL_Rect) { ix, iy, iw, ih }, c, (int) lroundf(gr * u));
}
/* a triangle pointing right (dir 1) or down (dir 2), as one-row strips */
static void icon_tri(int scr, SDL_Rect box, int size, float gx, float gy, float gw, float gh, int dir, SDL_Color c) {
	float u = size / 24.0f;
	int x0 = box.x + (box.w - size) / 2, y0 = box.y + (box.h - size) / 2;
	int ix = x0 + (int) lroundf(gx * u), iy = y0 + (int) lroundf(gy * u), iw = (int) lroundf(gw * u), ih = (int) lroundf(gh * u);
	if (dir == 1) {
		for (int r = 0; r < ih; ++r) {
			float t = fabsf((r + 0.5f) / ih * 2 - 1);
			int w = SDL_max(1, (int) lroundf(iw * (1 - t)));
			ui_fill(scr, (SDL_Rect) { ix, iy + r, w, 1 }, c);
		}
	} else {
		for (int r = 0; r < ih; ++r) {
			int w = SDL_max(1, (int) lroundf(iw * (1 - (r + 0.5f) / ih)));
			ui_fill(scr, (SDL_Rect) { ix + (iw - w) / 2, iy + r, w, 1 }, c);
		}
	}
}
static void draw_tab_icon(int scr, int page, SDL_Rect box, SDL_Color fg, SDL_Color bg) {
	int size = SDL_min(box.h * 68 / 100, box.w * 80 / 100);
	SDL_Rect b = box;
#define F(x, y, w, h, r, c) icon_fill(scr, b, size, x, y, w, h, r, c)
	switch (page) {
	case PG_GAME: /* a handheld: screen, D-pad, two buttons */
		F(5, 2, 14, 20, 3, fg);
		F(7, 4, 10, 8, 1, bg);
		F(7.5f, 16.8f, 4, 1.4f, 0.5f, bg);
		F(8.8f, 15.5f, 1.4f, 4, 0.5f, bg);
		F(14, 15.2f, 2.4f, 2.4f, 1.2f, bg);
		F(16.4f, 17.4f, 2.4f, 2.4f, 1.2f, bg);
		break;
	case ICON_SYSTEM: /* a toolbox */
		F(8, 2.5f, 8, 6, 2.5f, fg);
		F(10, 4.5f, 4, 4, 1, bg);
		F(3, 8, 18, 13.5f, 2, fg);
		F(3, 13.2f, 18, 1.4f, 0, bg);
		F(10.2f, 11.5f, 3.6f, 4.8f, 1, fg);
		break;
	case PG_DISPLAY: /* a monitor */
		F(2, 3.5f, 20, 14, 2, fg);
		F(4, 5.5f, 16, 10, 1, bg);
		F(10, 17.5f, 4, 3, 0, fg);
		F(7, 20, 10, 2, 1, fg);
		break;
	case PG_SCREENS: /* two screens, one over the other */
		F(4, 1.5f, 16, 9.5f, 1.5f, fg);
		F(6, 3.5f, 12, 5.5f, 0.5f, bg);
		F(4, 13, 16, 9.5f, 1.5f, fg);
		F(6, 15, 12, 5.5f, 0.5f, bg);
		break;
	case PG_CONTROLS: /* a gamepad */
		F(1, 6.5f, 22, 12, 6, fg);
		F(4.5f, 11.6f, 6, 1.8f, 0.5f, bg);
		F(6.6f, 9.5f, 1.8f, 6, 0.5f, bg);
		F(14.2f, 9.2f, 3.2f, 3.2f, 1.6f, bg);
		F(17.4f, 12.2f, 3.2f, 3.2f, 1.6f, bg);
		break;
	case PG_SPEED: /* fast forward */
		icon_tri(scr, b, size, 2.5f, 5, 9.5f, 14, 1, fg);
		icon_tri(scr, b, size, 12, 5, 9.5f, 14, 1, fg);
		break;
	case PG_MAP: /* a pin */
		icon_tri(scr, b, size, 6.2f, 12, 11.6f, 10.5f, 2, fg);
		F(4.5f, 1.5f, 15, 15, 7.5f, fg);
		F(9, 6, 6, 6, 3, bg);
		break;
	}
#undef F
}

void game_page_icon(int scr, int icon, SDL_Rect box, SDL_Color fg, SDL_Color bg) {
	draw_tab_icon(scr, icon, box, fg, bg);
}

static void draw_menu(void) {
	static SDL_Texture* shoulderTex[2][2]; /* L1, R1 prompts, per view */
	static int shoulderW, shoulderH;
	if (!shoulderTex[0][0] && !shoulderTex[0][1]) {
		ui_load_brand(BRAND_BTN_L1, shoulderTex[0], &shoulderW, &shoulderH);
		ui_load_brand(BRAND_BTN_R1, shoulderTex[1], &shoulderW, &shoulderH);
	}
	int scr = ui_touch_screen();
	/* (Overlays: the other screens undimmed, to show the bezel as it is) */
	bool preview = ovlOpen || pickWhich >= 0 || hexOpen;
	for (int s = 0; s < nScreens; ++s) if (!preview || s == scr) ui_dim(s, 170);
	draw_battery();
	float px = ui_px(scr);
	int cap = px * 11 / 2;   /* Futura capital height */
	int rad = px * 3;        /* box corner radius */
	if (achOpen) {
		cap = menu_geom(scr).cap;
		rad = menu_geom(scr).rad;
		SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
		ui_fill_round(scr, b, C_BTN, rad);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
		ui_fill_round(scr, t, C_BTN_EDGE, rad);
		ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Achievements", (SDL_Color) { 20, 16, 36, 255 });
		ra_list_draw(scr, ach_area(scr));
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (noteOpen) {
		draw_note(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (patchesOpen) {
		draw_patches(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	if (preview) {
		if (hexOpen) draw_hex(scr);
		else if (pickWhich >= 0) draw_picker(scr);
		else draw_ovl_menu(scr);
		return;
	}
	if (cheatsOpen) {
		draw_cheats(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	/* (moved off the button that asked: called off, by the D-pad or a finger) */
	if (armedItem >= 0 && menuItems[menuSel] != armedItem) armedItem = -1, menuNote[0] = 0;
	MenuGeom mg = menu_geom(scr);
	cap = mg.cap;
	rad = mg.rad;
	for (int t = 0; t < N_PAGES; ++t) {
		SDL_Rect r = top_box(scr, t, N_PAGES);
		bool on = t == curPage;
		ui_key(scr, r, rad, on ? KEY_LIT : KEY_PLAIN);
		draw_tab_icon(scr, t, r, on ? (SDL_Color) { 20, 16, 36, 255 } : ui_theme_light() ? (SDL_Color) { 92, 94, 112, 255 } : (SDL_Color) { 150, 152, 168, 255 }, ui_key_mid(on ? KEY_LIT : KEY_PLAIN));
	}
	SDL_Rect t0 = top_box(scr, 0, N_PAGES), t1 = top_box(scr, N_PAGES - 1, N_PAGES);
	/* L1 / R1 button prompts beside the tabs (the prompt art is 25:16) */
	int gw = mg.prompt, gh = gw * 16 / 25;
	SDL_Rect g0 = { mg.margin, t0.y + (t0.h - gh) / 2, gw, gh };
	SDL_Rect g1 = { mg.margin + mg.w - gw, t1.y + (t1.h - gh) / 2, gw, gh };
	if (shoulderTex[0][0] || shoulderTex[0][1]) {
		ui_image(scr, shoulderTex[0], shoulderW, shoulderH, "stretch", &g0);
		ui_image(scr, shoulderTex[1], shoulderW, shoulderH, "stretch", &g1);
	} else {
		ui_text(scr, t0.x - px * 14, t0.y + (t0.h - px * 7) / 2, px, "L1", C_DIS);
		ui_text(scr, t1.x + t1.w + px * 3, t1.y + (t1.h - px * 7) / 2, px, "R1", C_DIS);
	}
	if (page_is_rows(curPage)) {
		draw_rows_page(scr, &mg);
		if (nScreens > 1) {
			int o = ui_other_screen();
			draw_paused_label(o);
		}
		return;
	}
	itemTop = SDL_clamp(itemTop, 0, SDL_max(0, nMenuRows - ROW_VIS));
	if (nMenuRows > ROW_VIS) {
		/* more buttons than fit: a bar at the right edge */
		SDL_Rect a = { 0, mg.top, 0, 0 };
		int th = ROW_VIS * (mg.h + mg.gap) - mg.gap, bw = SDL_max(2, mg.margin / 2), bx = screens[scr].rect.w - mg.margin + (mg.margin - bw) / 2;
		ui_fill_round(scr, (SDL_Rect) { bx, a.y, bw, th }, (SDL_Color) { 60, 56, 84, 255 }, bw / 2);
		int bh = SDL_max(bw * 3, th * ROW_VIS / nMenuRows), by = a.y + (th - bh) * itemTop / SDL_max(1, nMenuRows - ROW_VIS);
		ui_fill_round(scr, (SDL_Rect) { bx, by, bw, bh }, C_GOLD, bw / 2);
	}
	for (int i = 0; i < nMenuItems; ++i) {
		if (!item_shown(i)) continue;
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		ui_key(scr, r, rad, sel ? KEY_SEL : KEY_PLAIN);
		bool en = item_enabled(menuItems[i]);
		const char* label = menuLabels[menuItems[i]];
		if (menuItems[i] == M_SCALE) label = scale_label(cfg->scale);
		if (menuItems[i] == M_LCD) label = shader_label(cfg->shader);
		if (menuItems[i] == M_COLOR) label = cfg->colorCorrection ? N_("Color Correction: On"): N_("Color Correction: Off");
		if (menuItems[i] == M_GHOST) label = cfg->ghosting ? N_("LCD Ghosting: On"): N_("LCD Ghosting: Off");
		if (menuItems[i] == M_AUTOSAVE) label = cfg->autoSave ? N_("Save on Quit: On"): N_("Save on Quit: Off");
		if (menuItems[i] == M_AUTOLOAD) label = cfg->autoLoad ? N_("Auto-Load: On"): N_("Auto-Load: Off");
		if (menuItems[i] == M_FPS) label = cfg->showFps ? N_("Show FPS: On"): N_("Show FPS: Off");
		if (menuItems[i] == M_FRAMEBUF) label = tallProf ? N_("Frame Buffer: Always on here"): cfg->frameBuffer ? N_("Frame Buffer: On"): N_("Frame Buffer: Off");
		if (menuItems[i] == M_REWIND) label = cfg->rewind ? N_("Rewind: On"): N_("Rewind: Off");
		if (menuItems[i] == M_FF) {
			static char ffLabel[32];
			if (cfg->fastForward && cfg->ffSpeed <= 0) snprintf(ffLabel, sizeof(ffLabel), tr("Fast Forward: Max"));
			else if (cfg->fastForward) snprintf(ffLabel, sizeof(ffLabel), tr("Fast Forward: %dx"), cfg->ffSpeed);
			label = cfg->fastForward ? ffLabel : N_("Fast Forward: Off");
		}
		if (menuItems[i] == M_MATCH) label = !cfg->matchDisplay ? N_("Match Display Rate: Off"): pacedFallback ? N_("Match Display Rate: Off for now"): N_("Match Display Rate: On");
		if (menuItems[i] == M_SHAPES) label = !en || !strcmp(cfg->screenMatch, "keep") ? N_("Keep device aspect ratios"):
		                                      !strcmp(cfg->screenMatch, "aspect") ? N_("Match screen aspect ratios"): N_("Match screen sizes and aspect ratios");
		if (menuItems[i] == M_PLACE) label = en && !strcmp(cfg->screenPlace, "no_gap") ? N_("Screen Placement: Closest to Gap"): N_("Screen Placement: Centered");
		if (menuItems[i] == M_PIXLOGO) label = cfg->customLogos == CL_OFF ? N_("Custom Logos: Do Not Use"): cfg->customLogos == CL_FULL ? N_("Custom Logos: Full Detail"): N_("Custom Logos: Pixelated");
		if (menuItems[i] == M_OVERLAY) {
			static const char* const ovl[3] = { N_("Overlays: Off"), N_("Overlays: Generated"), N_("Overlays: Custom")};
			label = ovl[SDL_clamp(cfg->overlayMode, 0, 2)];
		}
		if (menuItems[i] == armedItem) label = N_("Are You Sure?");
		/* (the slot they use: "Save State 3", "Load State 3") */
		static char slotBtn[2][24];
		if (menuItems[i] == M_SAVE || menuItems[i] == M_LOAD) {
			int k = menuItems[i] == M_LOAD;
			snprintf(slotBtn[k], sizeof(slotBtn[k]), k ? tr("Load State %d") : tr("Save State %d"), stateSlot);
			label = slotBtn[k];
		}
		if (menuItems[i] == M_SLOT) {
			/* the slot, with arrows at the ends: LEFT / RIGHT, or a tap on an end */
			label = slot_label();
			ui_label(scr, r.x + mg.pad, r.y + r.h / 2, cap, "\u2039", sel ? C_TEXT : C_DIS);
			ui_label(scr, r.x + r.w - mg.pad, r.y + r.h / 2, cap, "\u203A", sel ? C_TEXT : C_DIS);
		}
		int lc = cap;
		if (tall_item(menuItems[i])) {
			/* double height: the text as big as the button and its widest label allow */
			static const char* const tallLabels[] = { "Close Game", "Close GBA DS", "Are You Sure?" };
			int widest = 1;
			for (size_t k = 0; k < sizeof(tallLabels) / sizeof(*tallLabels); ++k) widest = SDL_max(widest, ui_label_width(scr, 100, tallLabels[k]));
			lc = SDL_min(r.h * 2 / 5, (r.w - mg.pad * 2) * 100 / widest);
		}
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, lc, label, en ? C_TEXT : C_DIS);
	}
	if (menuNote[0]) {
		int lastRow = SDL_min(nMenuRows, ROW_VIS) - 1;
		SDL_Rect last = { 0, mg.top + lastRow * (mg.h + mg.gap), 0, mg.h };
		int nc = SDL_min(mg.noteCap, mg.noteCap * mg.w / SDL_max(1, ui_font_width(TF_BOLD, mg.noteCap, menuNote)));
		/* (the last item is always a whole row: Close GBA DS, Show FPS...) */
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, last.y + last.h + mg.h * 3 / 5, nc, menuNote, C_GOLD, 0);
	}
	if (nScreens > 1) {
		int o = ui_other_screen();
		draw_paused_label(o);
	}
}

/* ---------------------------------------------------------------- helpers */

static int probes_hit(const uint32_t* fb, const Probe* p, int tol) {
	int n = 0;
	for (int i = 0; i < 8 && (p[i].x || p[i].y || p[i].rgb); ++i) {
		uint32_t c = fb[p[i].y * GBA_W + p[i].x];
		int dr = (int) (c & 0xFF) - (int) ((p[i].rgb >> 16) & 0xFF);
		int dg = (int) ((c >> 8) & 0xFF) - (int) ((p[i].rgb >> 8) & 0xFF);
		int db = (int) ((c >> 16) & 0xFF) - (int) (p[i].rgb & 0xFF);
		n += abs(dr) <= tol && abs(dg) <= tol && abs(db) <= tol;
	}
	return n;
}

/* Renders the current frame again without these sprites (on top of the
 * ones already hidden: `kept`, put back afterwards) into out, then puts the
 * game back exactly as it was (state, picture, sound). */
static void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV);
static void snapshot_hiding_sprites(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out) {
	snapshot_hiding_sprites_poke(hide, n, kept, nKept, out, 0, 0);
}
/* The same with a halfword written into the game's memory first (pokeAt 0:
 * none), for that one frame */
static void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV) {
	static void* st;
	static size_t stSize;
	static uint32_t raw[GBA_W * GBA_H];
	size_t sz = emu_state_size(&emu);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st || !emu_state_save(&emu, st)) {
		memcpy(out, emu.fb, sizeof(raw));
		return;
	}
	memcpy(raw, emu.fb, sizeof(raw));
	SpriteRule all[12];
	int na = 0;
	for (int i = 0; i < nKept && na < 12; ++i) all[na++] = kept[i];
	for (int i = 0; i < n && na < 12; ++i) all[na++] = hide[i];
	emu_hide_sprites(&emu, all, na);
	emu_state_load(&emu, st);
	if (pokeAt) emu_write16(&emu, pokeAt, pokeV);
	emu_frame(&emu, 0);
	memcpy(out, emu.fb, sizeof(raw));
	emu_hide_sprites(&emu, nKept ? kept : NULL, nKept);
	emu_state_load(&emu, st);
	int16_t drop[2048];
	while (emu_audio_read(&emu, drop, 1024) > 0) {}
	memcpy(emu.fb, raw, sizeof(raw));
}

/* Logo-only games: copy a recognised screen (THPS2 goal sheet) to the second
 * screen; back to the logo at the menus. */
/* The grabbed sky and scenery (the rows above the horizon), scaled up to
 * cover the canvas (centred), and dimmed well back. */
static bool frame_is_dark(const uint32_t* fb);
/* One colour all over, nearly: a fade to black or to white, a blank screen. */
static bool frame_is_flat(const uint32_t* fb) {
	long sum = 0, n = 0;
	static int l[GBA_W * GBA_H / 7 + 1];
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		l[n] = (int) ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF));
		sum += l[n++];
	}
	long mean = sum / n, off = 0;
	for (long i = 0; i < n; ++i) off += labs(l[i] - mean) > 36;
	return off * 20 < n; /* 95% within 12 a channel of the average */
}
static int hudRegrab; /* hudFrames at which to grab the map layer again (0: kept) */
static bool hudSettle; /* after a fade: wait for the brightness to settle */
static uint32_t hudMapSum; /* the grabbed map's tiles (map_tiles_sum) */

/* How bright each of the first 8 BG palettes is (the map's; the menus use
 * the others): games that fade the screen by scaling their palettes. */
static void bg_pal_levels(long out[8]) {
	for (int p = 0; p < 8; ++p) {
		out[p] = 0;
		for (int i = 0; i < 16; ++i) {
			uint16_t c = emu_read16(&emu, 0x05000000 + (p * 16 + i) * 2);
			out[p] += (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
		}
	}
}

/* How far the palettes have faded since ref, in 64ths: negative to black,
 * positive to white; the median over the palettes (one replaced, not
 * faded, doesn't count) */
static int bg_pal_fade(const long ref[8]) {
	long cur[8];
	bg_pal_levels(cur);
	int v[8], n = 0;
	for (int p = 0; p < 8; ++p) {
		if (ref[p] <= 0) continue;
		long full = 16 * 93;
		if (cur[p] < ref[p]) v[n++] = (int) -(64 - cur[p] * 64 / ref[p]);
		else v[n++] = ref[p] < full ? (int) ((cur[p] - ref[p]) * 64 / (full - ref[p])) : 0;
	}
	if (!n) return 0;
	for (int i = 1; i < n; ++i) {
		for (int j = i; j > 0 && v[j - 1] > v[j]; --j) {
			int t = v[j];
			v[j] = v[j - 1], v[j - 1] = t;
		}
	}
	return v[n / 2];
}

/* A checksum of a BG layer's tile graphics (its char block, the first 8 KB):
 * the same through a battle however the camera moves (its tile map, though,
 * is rewritten as the camera goes), another map's differ. */
static uint32_t map_tiles_sum(int bg) {
	uint16_t cnt = emu_read16(&emu, 0x04000008 + bg * 2);
	uint32_t base = 0x06000000 + ((cnt >> 2) & 3) * 0x4000, sum = 2166136261u;
	for (uint32_t a = 0; a < 0x2000; a += 4) sum = (sum ^ emu_read32(&emu, base + a)) * 16777619u;
	return sum;
}

static void hud_make_backdrop(void) {
	int h = SDL_clamp(hudProf->hud.backdropRows ? hudProf->hud.backdropRows : GBA_H, 1, GBA_H);
	if (hudProf->hud.mapCnt) {
		/* a map layer taken mid-fade, or with the camera on the sky, makes
		 * a poor picture: try again in half a second, until it's mostly
		 * there and not dark (then it stays) */
		uint32_t clear = emu_hud_clear();
		long drawn = 0, luma = 0;
		for (int i = 0; i < GBA_W * GBA_H; ++i) {
			uint32_t c = hudGrab[i];
			if ((c & 0xFFFFFF) == clear) continue;
			++drawn;
			luma += (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
		}
		long level = luma / SDL_max(drawn, 1) / 3;
		bool good = drawn > GBA_W * GBA_H / 2 && level > 60 && !frame_is_flat(hudGrab);
		/* after a fade, only once the brightness has stopped rising */
		static long lastLevel;
		if (hudSettle && good && labs(level - lastLevel) > 3) good = false;
		lastLevel = level;
		hudSettle = !good; /* (a poor take: the next must also hold steady) */
		hudRegrab = good ? 0 : hudFrames + (hudSettle ? 5 : 30);
		if (debug) fprintf(stderr, "frame %llu: backdrop taken: %ld%% drawn, brightness %ld%s\n", (unsigned long long) frameCount,
		                   drawn * 100 / (GBA_W * GBA_H), luma / SDL_max(drawn, 1) / 3, good ? "" : " (again later)");
		if (!good) return; /* keep the last good one meanwhile (or none: black) */
	}
	if (!hudBackValid && hudFadeEff) hudFadeIn = 16; /* (after a fade out) */
	hudBackValid = true;
	bg_pal_levels(hudPalRef);
	if (debug) {
		char path[1200];
		snprintf(path, sizeof(path), "%s/hud-backdrop.bmp", cfg->stateDir);
		SDL_Surface* bs = SDL_CreateRGBSurfaceWithFormatFrom(hudGrab, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
		if (bs) SDL_SaveBMP(bs, path), SDL_FreeSurface(bs);
		fprintf(stderr, "frame %llu: backdrop from %d rows (%s)\n", (unsigned long long) frameCount, h, path);
	}
	if (!hudBackValid) return;
	/* where the layers drew nothing: black, not the 'nothing here' colour */
	uint32_t clear = emu_hud_clear();
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		if ((hudGrab[i] & 0xFFFFFF) == clear) hudGrab[i] = 0xFF000000;
	}
	bool grey = hudProf->hud.backdropGrey;
	float sc = SDL_max((float) HUD_CW / GBA_W, (float) HUD_CH / h);
	float ox = (GBA_W - HUD_CW / sc) / 2, oy = (h - HUD_CH / sc) / 2;
	for (int y = 0; y < HUD_CH; ++y) {
		float fy = oy + (y + 0.5f) / sc - 0.5f;
		int y0 = SDL_clamp((int) fy, 0, h - 1), y1 = SDL_min(y0 + 1, h - 1);
		float wy = SDL_clamp(fy - y0, 0.0f, 1.0f);
		for (int x = 0; x < HUD_CW; ++x) {
			float fx = ox + (x + 0.5f) / sc - 0.5f;
			int x0 = SDL_clamp((int) fx, 0, GBA_W - 1), x1 = SDL_min(x0 + 1, GBA_W - 1);
			float wx = SDL_clamp(fx - x0, 0.0f, 1.0f);
			uint32_t a = hudGrab[y0 * GBA_W + x0], b = hudGrab[y0 * GBA_W + x1];
			uint32_t c = hudGrab[y1 * GBA_W + x0], d = hudGrab[y1 * GBA_W + x1];
			uint32_t out = 0xFF000000;
			for (int sh = 0; sh < 24; sh += 8) {
				float top = ((a >> sh) & 0xFF) * (1 - wx) + ((b >> sh) & 0xFF) * wx;
				float bot = ((c >> sh) & 0xFF) * (1 - wx) + ((d >> sh) & 0xFF) * wx;
				float v = (top * (1 - wy) + bot * wy) * 0.32f; /* dimmed */
				out |= (uint32_t) v << sh;
			}
			if (grey) {
				unsigned r = out & 0xFF, g = (out >> 8) & 0xFF, b = (out >> 16) & 0xFF;
				unsigned l = (r * 77 + g * 150 + b * 29) >> 8;
				out = 0xFF000000u | l << 16 | l << 8 | l;
			}
			hudBack[y * HUD_CW + x] = out;
		}
	}
}

/* The scene's backdrop (Mario Kart's podium): black (GEN-1; until v1.4.11
 * dark grey). (Until v1.3.2's
 * later test builds the logo was behind it, as large as fitted and
 * dimmed; at its own size, as GEN-1 has it, it's right behind the place
 * and the cup table, and they can't be read over it.) */
static void hud_make_logo_back(void) {
	hudLogoDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudLogoBack[i] = SCREEN_LIVE;
}

/* Profile hud.logoBelow: the logo as on any other second screen (GEN-1),
 * centred, kept below the HUD's rows. */
/* hud.held: the picture kept from the game screen (heldN rows: none) */
static uint32_t heldPic[GBA_W * GBA_H];
static int heldN;
static bool heldArmed = true; /* (taken once each time it's shown) */
/* hud.held.over: the number from the last capture since then (w 0: none) */
static uint32_t heldNum[GBA_W * GBA_H];
static int heldNumW, heldNumH;
static void hud_make_logo_below(void) {
	hudLogoBelowDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudLogoBelow[i] = SCREEN_LIVE; /* (under the HUD: GEN-1) */
	const typeof(hudProf->hud.held)* hp = &hudProf->hud.held;
	if (hp->y1 && heldN > 0) {
		/* (hud.held: the kept picture instead of the logo, at its own size) */
		for (int y = 0; y < heldN && hp->at + y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t* o = &hudLogoBelow[(hp->at + y) * 2 * HUD_CW + x * 2];
				o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = heldPic[y * GBA_W + x] | 0xFF000000;
			}
		}
		/* (hud.held.over: the number from the capture over its own) */
		const typeof(hp->over)* ov = &hp->over;
		if (ov->x1 && heldNumW > 0) {
			uint32_t fill = 0xFF000000 | (ov->fill >> 16 & 0xFF) | (ov->fill & 0xFF00) | (ov->fill & 0xFF) << 16;
			uint32_t key = (ov->key >> 16 & 0xFF) | (ov->key & 0xFF00) | (ov->key & 0xFF) << 16;
			for (int gy = ov->y0; gy <= ov->y1; ++gy) {
				for (int gx = ov->x0; gx <= ov->x1; ++gx) {
					int y = gy - hp->y0 + hp->at;
					if (gy < hp->y0 || y < 0 || y >= GBA_H || gx < 0 || gx >= GBA_W) continue;
					uint32_t c = fill;
					int sx = gx - (ov->x1 - heldNumW + 1), sy = gy - ov->y0;
					if (sx >= 0 && sx < heldNumW && sy >= 0 && sy < heldNumH && (heldNum[sy * heldNumW + sx] & 0xFFFFFF) != key)
						c = heldNum[sy * heldNumW + sx] | 0xFF000000;
					uint32_t* o = &hudLogoBelow[y * 2 * HUD_CW + gx * 2];
					o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
				}
			}
		}
		return;
	}
	int top = 0; /* below the HUD: its lowest row on the canvas */
	for (int m = 0; m < HUD_MOVES; ++m) {
		const HudMove* mv = &hudProf->hud.moves[m];
		if (mv->toGame && !mv->how && !mv->vhow && mv->x0 == 0 && mv->x1 >= GBA_W - 1 && mv->y0 > 0) top = mv->y0 * 2;
	}
	if (hudProf->hud.rows) top = hudProf->hud.rows * 2;
	if (hudProf->hud.logoTop) top = hudProf->hud.logoTop * 2;
	if (hudProf->hud.mapTitleRows && mapTitleN) top += (mapTitleN + 2) * 2; /* (and the level's name above it) */
	logo_draw(hudLogoBelow, HUD_CW, HUD_CH, HUD_CW / GBA_W, top, 255);
}

static int rule_count(const SpriteRule* r, int max) {
	int n = 0;
	while (n < max && (r[n].x1 || r[n].tileMax)) ++n;
	return n;
}

/* The second screen from this frame's HUD (hudPx). While racing, the turn
 * sign goes back onto the game picture and the map is drawn larger. */
/* A sprite with this tile (the 1D character number) is shown */
static bool oam_has_tile(int tile) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(&emu, 0x07000000 + i * 8);
		if ((a0 & 0x300) == 0x200) continue; /* hidden */
		if ((emu_read16(&emu, 0x07000004 + i * 8) & 0x3FF) == tile) return true;
	}
	return false;
}

/* A sprite with a tile from tile to tileMax (0: tile alone) in this
 * palette is shown */
static bool oam_has_tile_pal(int tile, int tileMax, int pal, int prio1) {
	if (tileMax < tile) tileMax = tile;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(&emu, 0x07000000 + i * 8);
		if ((a0 & 0x300) == 0x200) continue;
		uint16_t a2 = emu_read16(&emu, 0x07000004 + i * 8);
		if ((a2 & 0x3FF) >= tile && (a2 & 0x3FF) <= tileMax && a2 >> 12 == pal
		    && (!prio1 || ((a2 >> 10) & 3) == prio1 - 1)) return true;
	}
	return false;
}

/* hud.card: this frame's card (hudPx) as a take: kept if its name (dark
 * pixels on rows y0..y1) is clear of both edges and nearer the centre
 * than the best so far. True once it's centred (within a pixel). */
static bool card_take(void) {
	const typeof(hudProf->hud.card)* c = &hudProf->hud.card;
	uint32_t clear = emu_hud_clear();
	int lo = GBA_W, hi = -1;
	for (int y = SDL_max(c->y0, 0); y <= SDL_min(c->y1, GBA_H - 1); ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t p = hudPx[y * GBA_W + x];
			if ((p & 0xFFFFFF) == clear) continue;
			int luma = (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			if (luma < 3 * 96) lo = SDL_min(lo, x), hi = SDL_max(hi, x);
		}
	}
	if (hi < 0 || lo < 2 || hi > GBA_W - 3) return false;
	int off = abs(lo - (GBA_W - 1 - hi));
	if (cardScore < 0 || off < cardScore) {
		cardScore = off;
		memcpy(cardBest, hudPx, sizeof(cardBest));
	}
	return off <= 1;
}
/* hud.card.side: this frame's side part (hudPx) as its take: kept if
 * what's dark in it is clear of its top and bottom and nearer the centre
 * than the best so far. True if it was kept. */
static bool side_take(void) {
	const int* r = hudProf->hud.card.side;
	uint32_t clear = emu_hud_clear();
	int top = -1, bot = -1;
	for (int y = SDL_max(r[1], 0); y <= SDL_min(r[3], GBA_H - 1); ++y) {
		for (int x = SDL_max(r[0], 0); x <= SDL_min(r[2], GBA_W - 1); ++x) {
			uint32_t p = hudPx[y * GBA_W + x];
			if ((p & 0xFFFFFF) == clear) continue;
			int luma = (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			if (luma < 3 * 96) {
				if (top < 0) top = y;
				bot = y;
				break;
			}
		}
	}
	if (top < 0 || top < r[1] + 2 || bot > r[3] - 2) return false;
	int off = abs((top - r[1]) - (r[3] - bot));
	if (sideScore >= 0 && off >= sideScore) return false;
	sideScore = off;
	memcpy(sideBest, hudPx, sizeof(sideBest));
	if (debug) fprintf(stderr, "frame %llu: title card's side part taken (rows %d-%d, %d off centre)\n", (unsigned long long) frameCount, top, bot, off);
	return true;
}

/* hud.card: the best take as the backdrop (at its own size, black where
 * the card has nothing), with the side part's best take over it */
static void card_backdrop(void) {
	uint32_t clear = emu_hud_clear();
	const int* r = hudProf->hud.card.side;
	if (sideScore >= 0) {
		for (int y = SDL_max(r[1], 0); y <= SDL_min(r[3], GBA_H - 1); ++y) {
			for (int x = SDL_max(r[0], 0); x <= SDL_min(r[2], GBA_W - 1); ++x) cardBest[y * GBA_W + x] = sideBest[y * GBA_W + x];
		}
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = cardBest[y * GBA_W + x];
			c = (c & 0xFFFFFF) == clear && !hudProf->hud.card.whole ? 0xFF000000 : c | 0xFF000000;
			uint32_t* o = &hudBack[y * 2 * HUD_CW + x * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	hudBackValid = true;
	hudFadeIn = 0;
	bg_pal_levels(hudPalRef);
	if (debug) fprintf(stderr, "frame %llu: title card taken (%d pixels off centre)\n", (unsigned long long) frameCount, cardScore);
}

/* hud.card: no card taken yet: the logo, below the HUD's pieces; for a
 * whole-screen card, Sonic Advance 2's, centred, above the HUD along the
 * bottom */
static const uint32_t* card_logo(void) {
	static uint32_t cardLogo[HUD_CW * HUD_CH];
	if (!cardLogoMade) {
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) cardLogo[i] = SCREEN_LIVE; /* (under the HUD: GEN-1) */
		cardLogoMade = logo_draw(cardLogo, HUD_CW, HUD_CH, HUD_CW / GBA_W, hudProf->hud.card.whole ? 0 : 48 * 2, 255);
	}
	return cardLogo;
}

/* hud.mapAt.holdTile: the map as last drawn (HUD pixels; mapHeldOk false:
 * none yet this race), and the dots among the sprites read after the last
 * frame */
static uint32_t mapHeld[GBA_W * GBA_H];
static bool mapHeldOk;
static int mapUndrawn; /* frames since the map was last drawn */
static uint16_t mapDots[32][3];
static int nMapDots;

/* An OAM entry's pixel (px, py inside it, as on the screen) is opaque */
static bool obj_opaque(const uint16_t* a, int w, int h, int px, int py) {
	if (a[1] & 0x1000) px = w - 1 - px; /* (flips) */
	if (a[1] & 0x2000) py = h - 1 - py;
	bool b8 = a[0] & 0x2000;
	bool oneD = emu_read16(&emu, 0x04000000) & 0x40;
	int tile = a[2] & 0x3FF, step = b8 ? 2 : 1;
	tile += (py / 8) * (oneD ? (w / 8) * step : 32) + (px / 8) * step;
	const uint8_t* v = (const uint8_t*) emu_vram(&emu) + 0x10000 + (tile & 0x3FF) * 32;
	int x = px & 7, y = py & 7;
	if (b8) return v[y * 8 + x] != 0;
	uint8_t byte = v[y * 4 + x / 2];
	return ((x & 1) ? byte >> 4 : byte & 15) != 0;
}

static void obj_size(const uint16_t* a, int* w, int* h) {
	static const uint8_t sz[3][4][2] = { { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
	                                     { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
	                                     { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } } };
	int shape = SDL_min((a[0] >> 14) & 3, 2), size = (a[1] >> 14) & 3;
	*w = sz[shape][size][0], *h = sz[shape][size][1];
}

/* marks a sprite's opaque pixels inside the box (x0, y0, w x h) */
static void obj_mark(const uint16_t* a, uint8_t* mask, int x0, int y0, int bw, int bh) {
	int w, h;
	obj_size(a, &w, &h);
	int ox = a[1] & 0x1FF, oy = a[0] & 0xFF;
	if (ox >= 256) ox -= 512;
	if (oy >= 160) oy -= 256;
	for (int y = SDL_max(oy, y0); y < SDL_min(oy + h, y0 + bh); ++y) {
		for (int x = SDL_max(ox, x0); x < SDL_min(ox + w, x0 + bw); ++x) {
			if (obj_opaque(a, w, h, x - ox, y - oy)) mask[(y - y0) * bw + (x - x0)] = 1;
		}
	}
}

/* hud.mapAt.holdTile: after each racing frame. The game writes the sprites
 * for the next frame after one, so this frame's dots are the ones read
 * after the last frame; to be safe, those read now are left out too. A
 * frame with anything else in the map's box drew the map: its pixels
 * there, but the dots', go into mapHeld. */
static void map_hold_update(const typeof(hudProf->hud)* h, uint32_t clear) {
	if (!mapHeldOk) {
		for (int i = 0; i < GBA_W * GBA_H; ++i) mapHeld[i] = clear;
		mapHeldOk = true;
	}
	int x0 = SDL_max(h->map.x0, 0), y0 = SDL_max(h->map.y0, 0);
	int bw = SDL_min(h->map.x1, GBA_W - 1) - x0 + 1, bh = SDL_min(h->map.y1, GBA_H - 1) - y0 + 1;
	if (bw <= 0 || bh <= 0) return;
	static uint8_t mask[GBA_W * GBA_H];
	memset(mask, 0, (size_t) bw * bh);
	for (int d = 0; d < nMapDots; ++d) obj_mark(mapDots[d], mask, x0, y0, bw, bh);
	nMapDots = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a[3];
		for (int k = 0; k < 3; ++k) a[k] = emu_read16(&emu, 0x07000000 + i * 8 + k * 2);
		if ((a[0] & 0x300) == 0x200 || (a[0] & 0x100)) continue; /* hidden, rotated */
		if ((a[2] & 0x3FF) != h->mapAt.dotTile || nMapDots >= 32) continue;
		memcpy(mapDots[nMapDots++], a, sizeof(a));
		obj_mark(a, mask, x0, y0, bw, bh);
	}
	int other = 0;
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			if (!mask[y * bw + x] && (hudPx[(y + y0) * GBA_W + x + x0] & 0xFFFFFF) != clear) ++other;
		}
	}
	if (other < 16) { /* (the dots alone: the map isn't drawn this frame) */
		++mapUndrawn;
		return;
	}
	mapUndrawn = 0;
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			if (!mask[y * bw + x]) mapHeld[(y + y0) * GBA_W + x + x0] = hudPx[(y + y0) * GBA_W + x + x0];
		}
	}
}

static void hud_compose(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	uint32_t clear = emu_hud_clear();
	bool racing = !hudScene && memprobes_match(h->layout, 4, &emu);
	{
		static int lastRacing = -1;
		if (debug && racing != lastRacing) fprintf(stderr, "frame %llu: HUD layout %s\n", (unsigned long long) frameCount, racing ? "racing" : "plain");
		lastRacing = racing;
	}
	/* the map's box: grown over the race's first frames, then kept */
	if (racing && h->map.x1 && (hudFrames <= 8 || h->mapAt.scale)) {
		for (int y = h->map.y0; y <= h->map.y1; ++y) {
			for (int x = h->map.x0; x <= h->map.x1; ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (!mapFound) mapX0 = mapX1 = x, mapY0 = mapY1 = y, mapFound = true;
				mapX0 = SDL_min(mapX0, x), mapX1 = SDL_max(mapX1, x);
				mapY0 = SDL_min(mapY0, y), mapY1 = SDL_max(mapY1, y);
			}
		}
	}
	bool bigMap = racing && mapFound;
	int bx0 = SDL_max(mapX0 - 2, h->map.x0), by0 = SDL_max(mapY0 - 2, h->map.y0);
	int bx1 = SDL_min(mapX1 + 2, h->map.x1), by1 = SDL_min(mapY1 + 2, h->map.y1);
	if (racing && h->map.x1 && h->mapAt.scale) {
		/* (hud.mapAt: what the map has drawn so far this race, grown frame
		 * by frame, as it may blink; the whole box until then) */
		bigMap = true;
		if (mapFound) bx0 = mapX0, by0 = mapY0, bx1 = mapX1, by1 = mapY1;
		else bx0 = h->map.x0, by0 = h->map.y0, bx1 = h->map.x1, by1 = h->map.y1;
	}
	/* (hud.mapAt.holdTile: the map held between the frames it's drawn in) */
	bool hold = bigMap && h->mapAt.holdTile;
	if (hold) map_hold_update(h, clear);
	/* the sign: only when nothing else of the HUD is on its rows nearby */
	bool sign = false;
	if (racing && h->sign.x1) {
		bool inside = false, beside = false;
		for (int y = h->sign.y0; y <= h->sign.y1 && !beside; ++y) {
			for (int x = SDL_max(0, h->sign.x0 - 40); x <= SDL_min(GBA_W - 1, h->sign.x1 + 40); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (x >= h->sign.x0 && x <= h->sign.x1) inside = true;
				else beside = true;
			}
		}
		sign = inside && !beside;
	}
	if (sign) {
		for (int y = h->sign.y0; y <= h->sign.y1; ++y) {
			for (int x = h->sign.x0; x <= h->sign.x1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) != clear) emu.fb[y * GBA_W + x] = c;
			}
		}
	}
	if (hudScene) {
		if (!hudLogoDone) hud_make_logo_back();
		memcpy(hudCanvas, hudLogoBack, sizeof(hudCanvas));
	} else if ((hudAlt ? h->alt.mapBelow : h->mapBelow) && mapShownIdx >= 0 && mapW == GBA_W && mapH == GBA_H && map_px(mapShownIdx)) {
		/* (hud.mapBelow, hud.alt.mapBelow: the map picture behind, at the game's size) */
		const uint32_t* mp = map_px(mapShownIdx);
		for (int y = 0; y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t* o = &hudCanvas[y * 2 * HUD_CW + x * 2];
				o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = mp[y * GBA_W + x] | 0xFF000000;
			}
		}
	} else if (h->logoBelow || h->logoBehind) {
		if (!hudLogoBelowDone) hud_make_logo_below();
		memcpy(hudCanvas, hudLogoBelow, sizeof(hudCanvas));
	} else if (hudBackValid) {
		memcpy(hudCanvas, hudBack, sizeof(hudCanvas));
		/* the game fading the whole screen to black or white: the
		 * backdrop too. By BLDCNT (every BG layer, darken or brighten, by
		 * BLDY, in 16ths) */
		uint16_t bld = emu_read16(&emu, 0x04000050);
		int fade = SDL_min(emu_read16(&emu, 0x04000054) & 31, 16), eff = (bld >> 6) & 3;
		if (!((bld & 0x0F) == 0x0F && (eff == 2 || eff == 3) && fade)) fade = 0;
		/* (not behind a whole-screen card: it stays lit through the act.
		 * Sonic Advance 2 darkens the level with BLDCNT under the card's
		 * windows as it wipes off, then brings in the level's palettes) */
		if (h->card.whole) fade = 0;
		/* or by scaling its palettes (to black, or white): against how
		 * bright they were as the backdrop was taken (in 64ths) */
		int pf = fade || h->card.whole ? 0 : bg_pal_fade(hudPalRef);
		if (pf <= -2) eff = 3, fade = -pf;
		else if (pf >= 2) eff = 2, fade = pf;
		else fade *= 4;
		fade = SDL_clamp(fade, 0, 64);
		/* faded out all the way: that backdrop is done (the next scene's
		 * comes in as the game fades in, or after: then fading in too) */
		if (fade >= 62 && h->mapCnt) {
			hudBackValid = false;
			hudFadeEff = eff;
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = eff == 2 ? 0xFFFFFFFF : 0xFF000000;
			fade = 0;
		} else if (hudFadeIn > 0) {
			/* a new backdrop, faded in over 16 frames from how the last
			 * scene faded out, or less if the game's own fade is less */
			int in = hudFadeIn-- * 4;
			if (in > fade || eff != hudFadeEff) eff = hudFadeEff, fade = in;
		}
		if (fade) {
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) {
				uint32_t c = hudCanvas[i], o = 0xFF000000;
				for (int k = 0; k < 24; k += 8) {
					int v = (c >> k) & 0xFF;
					v = eff == 3 ? v - v * fade / 64 : v + (255 - v) * fade / 64;
					o |= (uint32_t) v << k;
				}
				hudCanvas[i] = o;
			}
		}
	} else if (h->card.tile) {
		memcpy(hudCanvas, card_logo(), sizeof(hudCanvas));
	} else {
		uint32_t plain = h->mapCnt ? (hudFadeEff == 2 ? 0xFFFFFFFF : 0xFF000000) : h->black ? SCREEN_LIVE : 0xFF1A1010;
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = plain;
	}
	/* moved pieces (the scene's, or racing's): how far each goes this frame */
	const HudMove* movesIn = hudScene ? h->scene.moves : racing ? h->moves : NULL;
	/* (hud.alt.moves: its own, on the other layer) */
	if (movesIn == h->moves && hudAlt && (h->alt.moves[0].how || h->alt.moves[0].vhow || h->alt.moves[0].toGame)) movesIn = h->alt.moves;
	/* with a BG mosaic on (fading out through a pipe), the boxes' top and
	 * bottom go to the blocks' edges: a block across a box's edge is drawn
	 * from its first row, so it goes with the box above */
	HudMove movesM[HUD_MOVES];
	const HudMove* moves = movesIn;
	int mosV = ((emu_io16(&emu, 0x4C) >> 4) & 15) + 1;
	if (movesIn && mosV > 1) {
		for (int m = 0; m < HUD_MOVES; ++m) {
			movesM[m] = movesIn[m];
			if (movesM[m].y0 > 0) movesM[m].y0 = (movesM[m].y0 + mosV - 1) / mosV * mosV;
			if (movesM[m].y1 < GBA_H - 1) movesM[m].y1 = (movesM[m].y1 + mosV) / mosV * mosV - 1;
		}
		moves = movesM;
	}
	/* hud.mapTitleRows: the level's name from the map picture on top, the
	 * HUD that far down */
	int titleDy = h->mapTitleRows && mapTitleN ? mapTitleN + 2 : 0; /* (2 rows apart) */
	for (int y = 0; y < titleDy; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t* o = &hudCanvas[y * 2 * HUD_CW + x * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = mapTitle[y * GBA_W + x];
		}
	}
	int moveDx[HUD_MOVES] = { 0 }, moveDy[HUD_MOVES] = { 0 };
	bool moving[HUD_MOVES] = { false };
	int pieceLo[HUD_MOVES] = { 0 }, pieceTop[HUD_MOVES] = { 0 };
	for (int m = 0; moves && m < HUD_MOVES; ++m) {
		const HudMove* mv = &moves[m];
		if (!mv->how && !mv->vhow && !mv->toGame) continue;
		if (mv->ifTile && !oam_has_tile(mv->ifTile)) continue;
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = SDL_max(mv->y0, 0); y <= SDL_min(mv->y1, GBA_H - 1); ++y) {
			for (int x = SDL_max(mv->x0, 0); x <= SDL_min(mv->x1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = SDL_min(lo, x), hi = SDL_max(hi, x), top = SDL_min(top, y), bot = SDL_max(bot, y);
			}
		}
		if (hi < 0) continue;
		moving[m] = true;
		moveDx[m] = mv->how == 'C' ? mv->x - (lo + hi) / 2 : mv->how == 'R' ? mv->x - hi : mv->how == 'L' ? mv->x - lo : 0;
		moveDy[m] = mv->vhow == 'M' ? mv->y - (top + bot) / 2 : mv->vhow == 'B' ? mv->y - bot : mv->vhow == 'T' ? mv->y - top : 0;
		pieceLo[m] = lo, pieceTop[m] = top;
	}
	/* `under`: below whatever stays in its corner (what's above its box in
	 * its columns, outside the other moving pieces), lined up with it */
	for (int m = 0; moves && m < HUD_MOVES; ++m) {
		const HudMove* mv = &moves[m];
		if (!moving[m] || !mv->under) continue;
		int ulo = GBA_W, ubot = -1;
		for (int y = 0; y < SDL_min(mv->y0, GBA_H); ++y) {
			for (int x = SDL_max(mv->x0, 0); x <= SDL_min(mv->x1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				bool other = false;
				for (int k = 0; k < HUD_MOVES && !other; ++k) {
					const HudMove* o = &moves[k];
					other = k != m && moving[k] && x >= o->x0 && x <= o->x1 && y >= o->y0 && y <= o->y1;
				}
				if (!other) ulo = SDL_min(ulo, x), ubot = SDL_max(ubot, y);
			}
		}
		if (ubot >= 0) moveDx[m] = ulo - pieceLo[m], moveDy[m] = ubot + 1 + mv->under - pieceTop[m];
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = hudPx[y * GBA_W + x];
			if ((c & 0xFFFFFF) == clear) continue;
			int nx = x, ny = y;
			bool toGame = false;
			for (int m = 0; m < HUD_MOVES; ++m) {
				const HudMove* mv = &moves[m];
				if (moving[m] && x >= mv->x0 && x <= mv->x1 && y >= mv->y0 && y <= mv->y1) {
					nx = x + moveDx[m], ny = y + moveDy[m];
					toGame = mv->toGame;
					break;
				}
			}
			if (nx < 0 || nx >= GBA_W || ny < 0 || ny >= GBA_H) continue;
			if (toGame) {
				emu.fb[ny * GBA_W + nx] = c; /* back onto the game picture */
				continue;
			}
			if (h->rows && y >= h->rows) continue; /* (hud.rows: the game draws the rest) */
			if (sign && x >= h->sign.x0 && x <= h->sign.x1 && y >= h->sign.y0 && y <= h->sign.y1) continue;
			if (bigMap && x >= bx0 && x <= bx1 && y >= by0 && y <= by1) continue;
			ny += titleDy;
			if (ny >= GBA_H) continue;
			uint32_t* o = &hudCanvas[ny * 2 * HUD_CW + nx * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	if (bigMap) {
		/* 2x if it fits comfortably right of the faces and below the top
		 * row (about 200 x 128 HUD pixels), else 1.5x; centred there */
		int w = bx1 - bx0 + 1, hh = by1 - by0 + 1;
		int q = (w * 2 <= 180 && hh * 2 <= 120) ? 4 : 3; /* canvas pixels per HUD pixel */
		int cx = 139 * 2, cy = 94 * 2;
		if (h->mapAt.scale) q = h->mapAt.scale * 2, cx = h->mapAt.cx * 2, cy = h->mapAt.bottom * 2 + 2 - hh * q / 2;
		int tx = SDL_clamp(cx - w * q / 2, 0, HUD_CW - w * q), ty = SDL_clamp(cy - hh * q / 2, 0, HUD_CH - hh * q);
		for (int y = by0; y <= by1; ++y) {
			for (int x = bx0; x <= bx1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				/* (the dots on the map held; not once the game's stopped
				 * drawing it, as a Grand Prix ends) */
				if (hold && mapUndrawn <= 3 && (c & 0xFFFFFF) == clear) c = mapHeld[y * GBA_W + x];
				if ((c & 0xFFFFFF) == clear) continue;
				for (int j = 0; j < q; ++j) {
					uint32_t* o = &hudCanvas[(ty + (y - by0) * q + j) * HUD_CW + tx + (x - bx0) * q];
					for (int i = 0; i < q; ++i) o[i] = c;
				}
			}
		}
	}
}

/* The BG layers whose tile maps mostly use palettes below pal (shown or
 * not: one turned on next frame is filled already) */
static unsigned effect_bgs(int pal) {
	const uint16_t* vram = emu_vram(&emu);
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) {
		uint16_t cnt = emu_read16(&emu, 0x04000008 + i * 2);
		int n = 1024 * ((cnt >> 14) == 0 ? 1 : (cnt >> 14) == 3 ? 4 : 2);
		const uint16_t* map = vram + ((cnt >> 8) & 31) * 1024;
		int low = 0, high = 0;
		for (int j = 0; j < n && ((cnt >> 8) & 31) * 1024 + j < 0x8000; ++j) {
			if (!(map[j] & 0x3FF)) continue;
			if ((map[j] >> 12) < pal) ++low;
			else ++high;
		}
		if (low > high) m |= 1u << i;
	}
	return m;
}

/* hud.overMap: this frame's HUD pieces into pieces[]; true if one changed. */
static bool hud_take_pieces(const typeof(hudProf->hud)* h) {
	uint32_t clear = emu_hud_clear();
	bool changed = false, seen[4] = { false };
	int found[4][4];
	if (h->findPieces) h->findPieces(&emu, hudPx, clear, found);
	for (int m = 0; m < 4; ++m) {
		const HudMove* mv = &h->moves[m];
		Piece* pc = &pieces[m];
		if (!mv->how && !mv->vhow && !mv->asIs) continue;
		int bx0 = mv->x0, by0 = mv->y0, bx1 = mv->x1, by1 = mv->y1;
		if (h->findPieces) bx0 = found[m][0], by0 = found[m][1], bx1 = found[m][2], by1 = found[m][3];
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = SDL_max(by0, 0); y <= SDL_min(by1, GBA_H - 1); ++y) {
			for (int x = SDL_max(bx0, 0); x <= SDL_min(bx1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = SDL_min(lo, x), hi = SDL_max(hi, x), top = SDL_min(top, y), bot = SDL_max(bot, y);
			}
		}
		if (hi < 0) {
			/* not there: the last one stays (one that follows another goes
			 * when that one is there without it) */
			if (mv->follow && pc->valid && seen[mv->follow - 1]) pc->valid = false, changed = true;
			if (mv->settle) pc->still = 0;
			if (mv->dropAfter && pc->valid && ++pc->gone >= mv->dropAfter) pc->valid = false, changed = true;
			continue;
		}
		seen[m] = true;
		pc->gone = 0;
		if (!mv->latch && (pc->sx != lo || pc->sy != top)) changed = true; /* (its followers move) */
		if (!mv->latch) pc->sx = lo, pc->sy = top;
		if (pc->atBottom != (bot == GBA_H - 1)) pc->atBottom = bot == GBA_H - 1, changed = true;
		static uint32_t cut[GBA_W * GBA_H];
		int w = hi - lo + 1, hh = bot - top + 1;
		for (int y = 0; y < hh; ++y) {
			for (int x = 0; x < w; ++x) {
				uint32_t c = hudPx[(top + y) * GBA_W + lo + x];
				cut[y * w + x] = (c & 0xFFFFFF) == clear ? 0 : c | 0xFF000000;
			}
		}
		size_t n = (size_t) w * hh * 4;
		if (mv->latch) {
			/* only for a new value, once it's held still for 8 frames (in
			 * the same place, the same size: it may animate, as the CO
			 * power meter does when it's full) */
			uint32_t key = emu_read8(&emu, mv->latch);
			if (pc->valid && key == pc->key && !pc->stale) continue;
			bool same = pc->cw == w && pc->ch == hh && pc->cx == lo && pc->cy == top;
			pc->still = same ? pc->still + 1 : 0;
			pc->cw = w, pc->ch = hh, pc->cx = lo, pc->cy = top;
			if (pc->still < 8) continue;
			pc->key = key;
		} else if (pc->valid && pc->w == w && pc->h == hh && !memcmp(pc->px, cut, n)) {
			continue;
		} else if (mv->settle) {
			bool same = pc->cw == w && pc->ch == hh && !memcmp(pc->cand, cut, n);
			pc->still = same ? pc->still + 1 : 0;
			pc->cw = w, pc->ch = hh;
			memcpy(pc->cand, cut, n);
			if (pc->still < mv->settle) continue;
		}
		pc->valid = true;
		pc->stale = false;
		pc->w = w, pc->h = hh;
		memcpy(pc->px, cut, n);
		changed = true;
		if (mv->clears) {
			for (int o = 0; o < 4; ++o) {
				if (o != m) pieces[o].valid = false, pieces[o].still = 0;
			}
		}
	}
	return changed;
}

/* After each frame: show the HUD it drew, and decide whether the next
 * one is split. A frame is shown only if it was split and still looks like
 * racing (the first pause-menu frame is split too, and is dropped). */
/* hud.mirror: the game screen's last picture from before */
static uint32_t mirrorHeld[GBA_W * GBA_H];
static bool mirrorHave, mirrorOn;

static void update_hud(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	/* hud.held: taken from the game screen the first frame it's shown in place */
	if (h->held.y1) {
		if (!memprobes_match(h->held.when, 3, &emu)) {
			heldArmed = true;
		} else if (heldArmed) {
			int n = 0;
			while (n < 8 && (h->held.probes[n].x || h->held.probes[n].y || h->held.probes[n].rgb)) ++n;
			if (probes_hit(emu.fb, h->held.probes, h->held.tol) >= (h->held.min ? h->held.min : n)) {
				heldArmed = false;
				heldN = SDL_min(h->held.y1, GBA_H - 1) - h->held.y0 + 1;
				memcpy(heldPic, emu.fb + h->held.y0 * GBA_W, (size_t) heldN * GBA_W * 4);
				heldNumW = 0; /* (its own number is the latest till the next capture) */
				hudLogoBelowDone = false;
				if (debug) fprintf(stderr, "frame %llu: picture kept for under the HUD\n", (unsigned long long) frameCount);
			}
		}
	}
	/* a conversation leaves the game screen only once there's a picture
	 * on the second screen to put it on */
	if (h->band) emu_hud_band(true, mapShownIdx < 0, h->band == BAND_WATCH);
	if (h->mirror[0].addr || h->mirrorOr[0].addr) {
		bool mirMain = h->mirror[0].addr && memprobes_match(h->mirror, 2, &emu);
		bool mir = mirMain || (h->mirrorOr[0].addr && memprobes_match(h->mirrorOr, 2, &emu));
		if (mir) {
			/* here, the picture from before (none after loading a state in
			 * the middle of it: left as it is; shown instead of emu.fb,
			 * which can't be written over: the renderer redraws only the
			 * lines that change) */
			if (!mirrorOn && debug) fprintf(stderr, "frame %llu: the screen mirrored\n", (unsigned long long) frameCount);
			mirrorOn = hudShown = true;
			if (mirMain && h->alt.on[0].addr && memprobes_match(h->alt.on, (int) (sizeof h->alt.on / sizeof h->alt.on[0]), &emu)) {
				/* hud.alt's probes too (a menu over the paused world): its
				 * layers alone, split as usual below; until the split's
				 * on, the background */
				if (!hudOn || !hudAlt) {
					if (h->logoBelow || h->logoBehind) {
						if (!hudLogoBelowDone) hud_make_logo_below();
						memcpy(hudCanvas, hudLogoBelow, sizeof(hudCanvas));
					} else {
						for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = 0xFF000000;
					}
					for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
				}
			} else {
				/* the whole screen over there */
				for (int y = 0; y < GBA_H; ++y) {
					for (int x = 0; x < GBA_W; ++x) {
						uint32_t* o = &hudCanvas[y * 2 * HUD_CW + x * 2];
						o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = emu.fb[y * GBA_W + x] | 0xFF000000;
					}
				}
				for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
				/* (and the split off, if it was on) */
				if (hudOn) emu_hud_split(&emu, false, 0, NULL, 0, hudPx), hudOn = false;
				return;
			}
		} else {
			memcpy(mirrorHeld, emu.fb, sizeof(mirrorHeld));
			mirrorHave = true;
			if (mirrorOn) {
				mirrorOn = hudShown = false;
				if (debug) fprintf(stderr, "frame %llu: the screen mirrored: off\n", (unsigned long long) frameCount);
			}
		}
	}
	bool main = memprobes_match(h->on, 4, &emu);
	bool altOn = !main && h->alt.on[0].addr && memprobes_match(h->alt.on, (int) (sizeof h->alt.on / sizeof h->alt.on[0]), &emu);
	bool race = main || altOn || (hudOn && h->stay[0].addr && memprobes_match(h->stay, 2, &emu));
	/* hud.offAfter: off from when it matches (a battle's results) until
	 * on[0] stops matching (the battle's over) */
	if (h->offAfter[0].addr) {
		if (!memprobes_match(h->on, 1, &emu)) {
			if (hudOffLatch && debug) fprintf(stderr, "frame %llu: HUD held off: done\n", (unsigned long long) frameCount);
			hudOffLatch = false;
		} else if (!hudOffLatch && memprobes_match(h->offAfter, 2, &emu)) {
			hudOffLatch = true;
			if (debug) fprintf(stderr, "frame %llu: HUD held off\n", (unsigned long long) frameCount);
		}
		if (hudOffLatch) race = altOn = false, main = false;
	}
	/* the HUD's other layer (alt): kept while it stays on through a fade */
	bool alt = main ? false : altOn ? true : hudAlt;
	/* the map layer: which BG has it (none: not on a map) */
	int mapBg = -1;
	bool world = false;
	for (int i = 0; h->mapCnt && i < 4; ++i) {
		uint16_t cnt = emu_read16(&emu, 0x04000008 + i * 2);
		if (cnt == h->mapCnt) mapBg = i;
		else if (h->world.mapCnt && cnt == h->world.mapCnt) mapBg = i, world = true;
	}
	if (h->mapCnt && mapBg < 0) race = false;
	if (race && h->offTile && oam_has_tile(h->offTile)) race = false;
	if (h->drop) {
		/* (only left out of the game screen: the second screen keeps
		 * its map picture or logo) */
	} else if (h->overMap) {
		/* into the map picture: each piece as last seen, kept while the
		 * HUD is hidden (menus, moves, fights) until it shows again; all
		 * gone off the battle map (the first `on` probe) */
		bool changed = false;
		if (!memprobes_match(h->on, 1, &emu)) {
			for (int m = 0; m < 4; ++m) changed |= pieces[m].valid, pieces[m].valid = false;
		} else if (hudOn && race) {
			changed = hud_take_pieces(h);
		} else {
			/* the split's off: a piece that goes alone (a conversation)
			 * isn't up any more */
			for (int m = 0; m < 4; ++m) {
				if (h->moves[m].alone && pieces[m].valid) pieces[m].valid = false, changed = true;
			}
		}
		bool any = pieces[0].valid || pieces[1].valid || pieces[2].valid || pieces[3].valid;
		if (changed || any != overOn) {
			overOn = any;
			overDirty = true;
			if (mapShownIdx >= 0) {
				upload_map_frame(mapShownIdx);
			} else if (h->logoUnder && combatPx && (combatTex[0] || combatTex[1])) {
				/* no map picture yet (the other armies' turn, after loading
				 * a state): a piece that goes alone (a conversation) on the
				 * logo picture itself */
				const uint32_t* px = hud_over_map(combatPx);
				if (!overAlone) px = combatPx;
				for (int i = 0; i < nViews; ++i) {
					if (combatTex[i]) ui_update_texture(combatTex[i], px, mapW * 4);
				}
			}
		}
	} else if (hudOn && race && hudCard) {
		/* the title card: a take a frame (once the split draws the card),
		 * the backdrop once it's centred; on the second screen from then */
		if (h->card.whole) {
			/* (the whole game screen, once the card has settled) */
			if (cardScore < 0 && hudFrames >= SDL_max(2, h->card.at)) {
				/* (card.hide / card.poke: drawn again, Sonic Advance 3's
				 * band on black without the HUD and the characters) */
				int nh = rule_count(h->card.hide, 6);
				if (nh || h->card.poke.addr) snapshot_hiding_sprites_poke(h->card.hide, nh, NULL, 0, cardBest, h->card.poke.addr, h->card.poke.value);
				else memcpy(cardBest, emu.fb, sizeof(cardBest));
				cardScore = 0;
				card_backdrop();
				cardTaken = h->card.hideTillTake; /* (the HUD over it from the next frame) */
			}
		} else {
			/* (the side part first, so the first backdrop has it if it's
			 * been seen; a better one later makes the backdrop again) */
			bool side = h->card.side[2] > h->card.side[0] && hudFrames >= 2 && side_take();
			if (hudFrames >= 2 && cardScore != 0 && card_take() && !hudBackValid) card_backdrop();
			else if (side && hudBackValid) card_backdrop();
		}
		if (hudBackValid) {
			memcpy(hudCanvas, hudBack, sizeof(hudCanvas));
			for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
			hudShown = true;
		} else if (h->card.drop[0].addr && hudFrames == 1) {
			/* (hud.card.drop, Sonic Advance 3, no take yet: its HUD stays
			 * on the game screen over the card, so not on the second
			 * screen too till the take: the logo alone) */
			memcpy(hudCanvas, card_logo(), sizeof(hudCanvas));
			for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
			hudShown = true;
		}
	} else if (hudOn && race) {
		/* hud.emptyIsOff: nothing drawn for 20 frames: the logo */
		static int emptyFor;
		bool empty = false;
		if (h->emptyIsOff) {
			uint32_t clear = emu_hud_clear();
			empty = true;
			for (int i = 0; i < GBA_W * GBA_H && empty; ++i) empty = (hudPx[i] & 0xFFFFFF) == clear;
			emptyFor = empty ? emptyFor + 1 : 0;
		}
		if (h->emptyIsOff && emptyFor >= 20) {
			if (hudShown && debug) fprintf(stderr, "frame %llu: HUD empty: the logo\n", (unsigned long long) frameCount);
			hudShown = false;
		} else if (h->freeze[0].addr && memprobes_match(h->freeze, 2, &emu)) {
			/* hud.freeze: the picture as it was (World-e's status bar
			 * sliding away as the inventory opens, and back, SM4-8) */
		} else if (!(empty && !hudShown)) {
			if (emu_hud_grabbed()) hud_make_backdrop(); /* (that frame drew no HUD: the last one is reused) */
			hud_compose();
			for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
			if (logoForced && !(hudProf->logoIf[0].addr && memprobes_match(hudProf->logoIf, 2, &emu)) &&
			    !(hudProf->logoIfOr[0].addr && memprobes_match(hudProf->logoIfOr, 2, &emu))) {
				/* (a new HUD picture: the logo's hold is over) */
				logoForced = false, logoHolding = 0;
				if (debug) fprintf(stderr, "frame %llu: logo only off: a new HUD picture\n", (unsigned long long) frameCount);
			}
			if (!hudShown && debug) fprintf(stderr, "frame %llu: HUD on the second screen\n", (unsigned long long) frameCount);
			hudShown = true;
		}
	}
	bool scene = race && h->scene.when[0].addr && memprobes_match(h->scene.when, 2, &emu);
	bool card = race && h->card.tile && oam_has_tile_pal(h->card.tile, h->card.tileMax, h->card.pal, h->card.prio1);
	if (h->card.hideTillTake) {
		/* (Sonic Advance 3, SA3-3: the card's start, its sprite or a
		 * little before it, until the take; a start seen again within 10
		 * frames is the same card) */
		bool pre = race && h->card.pre[0].addr && memprobes_match(h->card.pre, 2, &emu);
		cardPreFor = pre && !card ? cardPreFor + 1 : 0;
		bool starting = card || (pre && cardPreFor <= 30);
		if (starting && cardAway >= 10) cardTaken = false;
		cardAway = starting ? 0 : cardAway + 1;
		card = starting && !cardTaken;
	}
	/* hud.card.drop: no card here (Sonic Advance 3's hubs): the take is
	 * forgotten, the logo behind the HUD */
	if (race && h->card.drop[0].addr && (hudBackValid || cardScore >= 0) && memprobes_match(h->card.drop, 2, &emu)) {
		hudBackValid = false, cardScore = -1, sideScore = -1;
		if (debug) fprintf(stderr, "frame %llu: title card dropped\n", (unsigned long long) frameCount);
	}
	if (card != hudCard) {
		/* a card coming up: a new take; going: the best one, if not yet */
		/* (hud.card.drop, Sonic Advance 3: the last take stays till the
		 * new one, without the HUD, which stays on the game's card) */
		if (card) cardScore = -1, sideScore = -1, hudBackValid = hudBackValid && h->card.drop[0].addr;
		if (card && h->card.hideTillTake) {
			/* (this frame was drawn before the split was on: drawn again
			 * without the HUD, so it isn't on the game screen for a frame,
			 * SA3-3) */
			static uint32_t noHud[GBA_W * GBA_H];
			snapshot_hiding_sprites(hudObjs, rule_count(hudObjs, 12), NULL, 0, noHud);
			memcpy(emu.fb, noHud, sizeof(noHud));
		}
		else if (cardScore >= 0 && !hudBackValid) card_backdrop();
		if (debug) fprintf(stderr, "frame %llu: title card %s\n", (unsigned long long) frameCount, card ? "up" : "gone");
	}
	if (race != hudOn || scene != hudScene || world != hudWorld || alt != hudAlt || card != hudCard) {
		/* (hud.card.hideTillTake: the HUD split off as usual, but not shown
		 * till the take) */
		bool cardObjs = card && !h->card.hideTillTake;
		const SpriteRule* objs = cardObjs ? h->card.objs : scene ? h->scene.objs : world ? hudWorldObjs : hudObjs;
		unsigned mask = alt ? h->alt.bgMask : h->bgMask;
		int nObjs = rule_count(objs, cardObjs || scene || world ? 4 : 12);
		/* (nothing split off: only the logo behind the map picture, as
		 * for Fire Emblem and Final Fantasy Tactics Advance; no second
		 * renderer) */
		static bool splitting;
		bool some = mask || nObjs || h->band;
		if (some || splitting) emu_hud_split(&emu, race && some, mask, objs, nObjs, hudPx);
		splitting = race && some;
		if (race && some) emu_hud_copy(&emu, cardObjs); /* (the card stays on the game screen too) */
		hudCard = card;
		if (debug && race && alt != hudAlt) fprintf(stderr, "frame %llu: HUD on its other layer%s\n", (unsigned long long) frameCount, alt ? "" : ": off");
		hudAlt = alt;
		/* (by priority: the renderer picks the layers line by line) */
		emu_hud_by_priority(h->bgPrioBelow);
		{
			uint32_t a[2], m[2], v[2];
			int sz[2], n = 0;
			for (; n < 2 && h->memLayers.when[n].addr; ++n) {
				a[n] = h->memLayers.when[n].addr, m[n] = h->memLayers.when[n].mask, v[n] = h->memLayers.when[n].value, sz[n] = h->memLayers.when[n].size;
			}
			emu_hud_mem_layers(a, m, v, sz, race ? n : 0, h->memLayers.any, h->memLayers.bgMask, h->memLayers.gamePal);
		}
		hudKeep = 0;
		if (debug && race && scene != hudScene) fprintf(stderr, "frame %llu: HUD scene %s\n", (unsigned long long) frameCount, scene ? "on" : "off");
		hudOn = race;
		hudScene = scene;
		hudWorld = world;
		hudFrames = 0;
		hudRegrab = 0;
		mapFound = false;
		mapHeldOk = false, nMapDots = 0, mapUndrawn = 0; /* (hud.mapAt.holdTile: none held yet) */
	}
	/* a spell or other animation: its effects are on the text box layers,
	 * in other palettes than the text boxes; those layers stay (from the
	 * next frame) */
	unsigned keep = 0;
	if (hudOn && h->effects[0].addr && memprobes_match(h->effects, 2, &emu)) keep = effect_bgs(h->effectsPal);
	if (keep != hudKeep) {
		emu_hud_keep_bgs(keep);
		if (debug) fprintf(stderr, "frame %llu: effect layers %X\n", (unsigned long long) frameCount, keep);
		hudKeep = keep;
	}
	/* the course's backdrop, as each race starts (not for the scene) */
	if (hudOn && ++hudFrames == 2 && h->backdrop && !hudScene) emu_hud_grab(&emu, h->backdrop, hudGrab);
	/* the backdrop, the game screen (map, units, no HUD): once per map (its tiles' checksum),
	 * when the HUD comes on, not again as it comes and goes in a battle */
	if (hudOn && h->mapCnt && mapBg >= 0 && (hudFrames == 2 || hudFrames == hudRegrab)) {
		uint32_t sum = map_tiles_sum(mapBg);
		if (hudFrames == hudRegrab || !hudBackValid || sum != hudMapSum) {
			hudMapSum = sum;
			/* the game screen as it is now: the scene, without the HUD (nor
			 * the sprites the profile leaves out of it, rendered again) */
			int nh = rule_count(h->backdropHide, 2);
			if (world) {
				/* the world map: its layers below the HUD's (the land and the
				 * map, no sprites), drawn next frame */
				unsigned m = 0;
				for (int i = 0; i < 4; ++i) {
					if ((emu_read16(&emu, 0x04000008 + i * 2) & 3) >= h->bgPrioBelow) m |= 1u << i;
				}
				emu_hud_grab(&emu, m, hudGrab);
			} else {
				if (nh) snapshot_hiding_sprites(h->backdropHide, nh, hudObjs, rule_count(hudObjs, 12), hudGrab);
				else memcpy(hudGrab, emu.fb, sizeof(hudGrab));
				hud_make_backdrop();
			}
		}
	}
	/* a scene change: the game fades to black (or white) and back (to
	 * another map, or the same one elsewhere); once the picture's back,
	 * take the scene again (retried until its brightness settles) */
	static int darkRun;
	if (h->mapCnt) {
		bool dark = frame_is_flat(emu.fb);
		if (dark) {
			++darkRun;
		} else {
			if (darkRun >= 6 && hudBackValid) {
				if (debug) fprintf(stderr, "frame %llu: faded in: the backdrop again\n", (unsigned long long) frameCount);
				if (hudOn) hudRegrab = hudFrames + 10;
				hudSettle = true;
				hudMapSum = 0; /* (and when the HUD comes back on) */
			}
			darkRun = 0;
		}
	}
	/* menus: the logo (or the map), once it's been gone for a third of a
	 * second (not for a moment between rooms); no keep probes: never kept */
	static int goneFor;
	/* (not while the game screen is blank between scenes: the second
	 * screen stays as dark, or as white, as the fade left it) */
	bool gone = !race && hudShown && !(h->keep[0].addr && memprobes_match(h->keep, 4, &emu)) &&
	            !(h->mapCnt && frame_is_flat(emu.fb));
	goneFor = gone ? goneFor + 1 : 0;
	if (goneFor >= 20) {
		hudShown = false;
		goneFor = 0;
		if (debug) fprintf(stderr, "frame %llu: HUD gone\n", (unsigned long long) frameCount);
	}
}

/* profile.tall: the table over both screens (see profiles.h, PP-1). The
 * view's top row v is the game's scroll, at most rows - 320: the top screen
 * shows rows v to v + 159, the bottom one the 160 below. Each line comes
 * from the first picture that has its row: on the top screen the game's
 * while v is its scroll, else twins[0]'s (held at rows - 320) with what the
 * game puts on its screen laid over it; on the bottom screen twins[1]'s
 * (held at v + 160). Rows none has are black (only until the copies are
 * up). The score bar goes to the bottom of the bottom screen. Called after
 * each frame. */
typedef struct { const uint32_t* px; int row; int from, safe; } TallSrc;
/* (from: its first line with table rows; safe: its first line whose rows
 * are sure to be right: a carry copy's lines above gameFrom - 1 are the
 * rows its game hides under the score bar, whose tiles the game doesn't
 * keep (Muppet Pinball Mayhem's, moving down the table): used only when
 * no other picture has the row lower down) */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);
static bool tallKeep; /* (carry) a row no picture has keeps what it showed last */
/* the live view's last 10 seconds, a line a frame, for bug reports
 * (GBADS_TALL_TRACE's lines, kept whether it's set or not) */
#define TALL_LOG 600
static char tallLog[TALL_LOG][176];
static unsigned tallLogN;
static bool tall_line(uint32_t* dst, int row, const TallSrc* src, int n) {
	for (int pass = 0; pass < 2; ++pass)
		for (int i = 0; i < n; ++i) {
			int y = row - src[i].row;
			int lo = pass ? src[i].from : SDL_max(src[i].from, src[i].safe);
			if (src[i].px && y >= lo && y < GBA_H) {
				memcpy(dst, &src[i].px[y * GBA_W], sizeof(uint32_t) * GBA_W);
				return true;
			}
		}
	if (!tallKeep) for (int x = 0; x < GBA_W; ++x) dst[x] = 0xFF000000;
	return false;
}
/* Whether the game's BG0 has a pixel at screen (x, y) above the score bar
 * (its map from the profile's msg.map, row0 being the map row of screen row
 * 0; no horizontal scroll): the catch timer's digits. PP-10: the banner
 * boxes take only what isn't BG0's, so a banner flying across the timer
 * doesn't carry part of it to the bottom screen. */
static bool tall_bg0_pixel(uint32_t map, int row0, int x, int y) {
	uint16_t cnt = emu_io16(&emu, 0x08);
	uint32_t chr = 0x06000000 + ((cnt >> 2) & 3) * 0x4000;
	uint16_t e = emu_read16(&emu, map + (((row0 + (y >> 3)) & 31) * 32 + (x >> 3)) * 2);
	int px = x & 7, py = y & 7;
	if (e & 0x400) px = 7 - px;
	if (e & 0x800) py = 7 - py;
	uint32_t t = e & 0x3FF;
	if (cnt & 0x80) return emu_read8(&emu, chr + t * 64 + py * 8 + px) != 0;
	uint8_t b = emu_read8(&emu, chr + t * 32 + py * 4 + px / 2);
	return (px & 1 ? b >> 4 : b & 15) != 0;
}

static void update_tall(void) {
	const typeof(tallProf->tall)* t = &tallProf->tall;
	const typeof(t->live)* L = &t->live;
	bool on = nScreens > 1 && memprobes_match(t->on, 2, &emu);
	static bool copied; /* the score bar left in the game's picture */
	if (on != tallOn) {
		emu_hud_split(&emu, on, t->hudBg, NULL, 0, hudPx);
		emu_hud_copy_above(0);
		tallOn = on;
		copied = false;
		tallMsg = haveLastTop = false;
		tallTopLogo = tallFixedBot = false, tallFixedHold = 0;
		tallLive = false;
		mapShownIdx = -1;
		/* (carry: the copies start again from the game's: what they kept,
		 * running on through the menus, is the last table's tiles, which
		 * a new table's (Muppet Pinball Mayhem's, after its curtains)
		 * doesn't reload in one go) */
		for (int i = 0; i < TWINS; ++i) twins[i].carried = false;
		if (debug) fprintf(stderr, "frame %llu: table %s\n", (unsigned long long) frameCount, on ? "up" : "gone");
		return;
	}
	if (!on) return;
	bool up = twinOpen && twins[1].up && twins[1].havePic;
	/* (carry: and the top screen's copy, when its rows are on the top
	 * screen: one still walking to its row would leave them blank) */
	if (up && tall_carry(tallProf)) {
		int s0 = emu_io16(&emu, 0x10 + 4 * L->viewBg + 2) & 0x1FF, r0 = tall_rows_of(tallProf, &emu);
		if (s0 >= r0) s0 -= 512;
		if (s0 - twinShakeNow > r0 - 2 * GBA_H && !(twins[0].up && twins[0].havePic)) up = false;
		for (int i = 0; i < 3 && L->carry.plain[i].addr; ++i)
			if (memprobes_match(&L->carry.plain[i], 1, &emu)) up = false;
	}
	if (!up) {
		/* the copies aren't running (the table's opening sweep, a table they
		 * don't know, no ball): the game as it is, the logo on the second screen */
		if (!copied) emu_hud_copy(&emu, true), copied = true;
		emu_hud_copy_above(0);
		if (tallLive && debug) fprintf(stderr, "frame %llu: live view off\n", (unsigned long long) frameCount);
		tallLive = false;
		mapShownIdx = -1;
		return;
	}
	/* the game's picture: what's over the table (the catch timer, messages)
	 * kept, the score bar not (it goes below) */
	bool firstLive = copied; /* (the game's picture has its score bar until the next frame) */
	if (copied) emu_hud_copy(&emu, false), copied = false;
	/* (carry: the game's picture has none of the score bar's layers on any
	 * row, so it can go on either screen; they're laid over the top screen
	 * below) */
	bool carry = tall_carry(tallProf);
	emu_hud_copy_above(carry ? 0 : t->hudRow);
	int rows = tall_rows_of(tallProf, &emu), vmax = rows - 2 * GBA_H, top = tall_top_of(tallProf);
	int s = emu_io16(&emu, 0x10 + 4 * L->viewBg + 2) & 0x1FF; /* this frame's */
	if (s >= rows) s -= 512;
	/* (the screen shake moves the whole view, as it does the game's screen;
	 * the copies shake alike, by themselves) */
	int k = twinShakeNow;
	int v = SDL_max(SDL_min(s - k, vmax), top) + k;
	int safe = carry && L->carry.gameFrom ? L->carry.gameFrom - 1 : 0;
	TallSrc game = { emu.fb, s, carry ? L->carry.gameFrom : 0, 0 };
	TallSrc a = { twins[0].up && twins[0].havePic ? twins[0].pic : NULL, twins[0].picRow, 0, safe }; /* (this frame's only) */
	TallSrc b = { twins[1].pic, twins[1].picRow, 0, safe };
	TallSrc topSrc[3] = { game, a, b }, botSrc[3] = { b, a, game };
	bool gameOnTop = s == v;
	if (!gameOnTop) topSrc[0] = a, topSrc[1] = game;
	/* (carry: wherever the game's own picture has the rows, it's used: the
	 * copies only draw the rows it doesn't) */
	if (carry) topSrc[0] = game, topSrc[1] = a, botSrc[0] = game, botSrc[1] = b, botSrc[2] = a;
	tallKeep = carry && tallLive;
	int missing = 0;
	/* (carry with gameFrom: the top screen's lines above gameFrom - 1 are
	 * the game's own, the score bar's alone, where the table doesn't show:
	 * its layers are laid over them below, on what the game has behind
	 * them. The line between is the score bar's black edge, drawn black:
	 * the game's own has it black but for a sprite passing under the bar
	 * (or, in its picture without the score bar's layers, a row from under
	 * its own view, which with the lower view is another part of the
	 * table); the copies' have the table) */
	int gf = carry && L->carry.gameFrom ? L->carry.gameFrom - 1 : 0;
	/* (what the top screen showed last frame, for a frame whose rows aren't
	 * all there: below) */
	static uint32_t prevTop[GBA_W * GBA_H];
	memcpy(prevTop, tallTop, sizeof(prevTop));
	bool havePrevTop = tallLive && !firstLive;
	int topMissing = 0;
	for (int y = 0; y < GBA_H; ++y) {
		if (y < gf) memcpy(&tallTop[y * GBA_W], &emu.fb[y * GBA_W], sizeof(uint32_t) * GBA_W);
		else if (gf && y == gf) for (int x = 0; x < GBA_W; ++x) tallTop[y * GBA_W + x] = 0xFF000000u;
		else topMissing += !tall_line(&tallTop[y * GBA_W], v + y, topSrc, 3);
		missing += !tall_line(&tallBot[y * GBA_W], v + GBA_H + y, botSrc, 3);
	}
	missing += topMissing;
	if (carry) topMissing = 0; /* (carry keeps its own rows: tallKeep) */
	/* PP-11: the game dims its screen (paused, the Mart: brightness down)
	 * or shows a message box: its whole screen on the bottom one */
	uint16_t bld = emu_io16(&emu, 0x50);
	bool msg = (bld & 0xC0) == 0xC0 && (emu_io16(&emu, 0x54) & 0x1F) && !(carry && L->carry.keepDim);
	if (!msg && L->msg.map) {
		int n = 0;
		for (int r = L->msg.row0; r < L->msg.row0 + L->msg.rows; ++r) {
			for (int c = 0; c < 30; ++c) n += (emu_read16(&emu, L->msg.map + (r * 32 + c) * 2) & 0x3FF) != L->msg.blank;
		}
		msg = n > L->msg.min;
	}
	if (msg && !carry) {
		/* (the top screen: the table's rows just above the game's screen,
		 * from twins[0] held there (twin_step); none above it (the game's
		 * view near the table's top): the logo, as the rows it would show
		 * are the game's own again. Until the copy is there, what the top
		 * screen showed last) */
		if (s - k - GBA_H < top) {
			for (int i = 0; i < GBA_W * GBA_H; ++i) tallTop[i] = SCREEN_GREY;
			logo_draw(tallTop, GBA_W, GBA_H, 1, 0, 128 /* LOGO_IDLE */);
			tallTopLogo = true;
		} else {
			int off = 0;
			for (int y = 0; y < GBA_H; ++y) off += !tall_line(&tallTop[y * GBA_W], s - GBA_H + y, &a, 1);
			if (off) memcpy(tallTop, prevTop, sizeof(tallTop));
			else tallTopLogo = false;
		}
	} else {
		/* (the top screen: the table's rows, or while that would be the game's
		 * screen again, as they were before it) */
		if (msg && gameOnTop && haveLastTop) memcpy(tallTop, lastTop, sizeof(tallTop));
		/* (a copy not yet back at its place (just after PP-11's screen, which
		 * held it elsewhere): what the top screen showed last, for the frame
		 * or two it takes) */
		if (!msg && topMissing && havePrevTop) memcpy(tallTop, prevTop, sizeof(tallTop));
		else if (!msg) tallTopLogo = false;
	}
	if (msg) memcpy(tallBot, emu.fb, sizeof(tallBot)); /* (and its score bar, below) */
	if (msg != tallMsg && debug) fprintf(stderr, "frame %llu: %s\n", (unsigned long long) frameCount, msg ? "the game's screen on the bottom one (dimmed or a message)" : "the table on both screens again");
	tallMsg = msg;
	if (gameOnTop != tallGameTop && debug) fprintf(stderr, "frame %llu: top screen: %s\n", (unsigned long long) frameCount, gameOnTop ? "the game" : "a copy (the lower view)");
	tallGameTop = gameOnTop;
	int laid = 0;
	if ((!gameOnTop || carry) && !firstLive && !msg) {
		/* What the game puts on its screen rather than on the table, which
		 * the copies leave out (PP-10), laid over the top screen where it is
		 * on the game's: what the HUD split draws above the score bar, its
		 * BG0 (the catch timer, messages) and the game's sprites at the same
		 * place on the screen whatever the camera (banners, the Mart's items:
		 * run_frame). (Drawn on their own: the copies' pictures can differ
		 * a little from the game's, so it can't be found by comparing.) */
		uint32_t clear = emu_hud_clear();
		/* (the game's sprites among it (banners such as BALL SAVE): on the
		 * screen with the ball, at their place on the game's screen. Which
		 * screen is settled as they come up, from where the ball is then,
		 * and kept while they're up: they never move between the screens) */
		static uint8_t box[GBA_W * GBA_H];
		bool anyFixed = false;
		if (!carry) {
			memset(box, 0, sizeof(box));
			for (int f = 0; f < nTallFixed; ++f) {
				uint16_t a0 = tallFixed[f][0], a1 = tallFixed[f][1];
				int y0 = a0 & 0xFF, x0 = a1 & 0x1FF, h = obj_height(a0, a1), w = obj_width(a0, a1);
				if (y0 >= GBA_H) y0 -= 256;
				if (x0 >= GBA_W) x0 -= 512;
				for (int y = SDL_max(y0, 0); y < SDL_min(y0 + h, t->hudRow); ++y)
					for (int x = SDL_max(x0, 0); x < SDL_min(x0 + w, GBA_W); ++x) {
						if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
						if (L->msg.map && tall_bg0_pixel(L->msg.map, L->msg.row0, x, y)) continue; /* (BG0's: the timer) */
						box[y * GBA_W + x] = 1, anyFixed = true;
					}
			}
			if (anyFixed) {
				if (!tallFixedHold) {
					uint32_t S = emu_read32(&emu, L->state);
					int32_t bx, by;
					bool bot = twin_ball(&emu, S, &bx, &by) && (by >> 8) >= v + GBA_H - k;
					if (bot != tallFixedBot && debug) fprintf(stderr, "frame %llu: banners on the %s screen (the ball's there)\n", (unsigned long long) frameCount, bot ? "bottom" : "top");
					tallFixedBot = bot;
				}
				tallFixedHold = 20;
			} else if (tallFixedHold) {
				--tallFixedHold;
			}
		}
		for (int i = 0; i < GBA_W * t->hudRow; ++i) {
			if ((hudPx[i] & 0xFFFFFF) == clear) continue;
			if (!carry && box[i] && tallFixedBot) tallBot[i] = hudPx[i] | 0xFF000000;
			else tallTop[i] = hudPx[i] | 0xFF000000;
			++laid;
		}
	}
	if (carry) {
		/* (the rows the score bar's layers drew over the table: below the
		 * score bar itself, above the message line) */
		uint32_t cl = emu_hud_clear();
		tallBandY0 = tallBandY1 = -1;
		for (int y = 24; y < t->hudRow; ++y) {
			int n = 0;
			for (int x = 0; x < GBA_W; ++x) n += (hudPx[y * GBA_W + x] & 0xFFFFFF) != cl;
			if (n > 8) tallBandY1 = y, tallBandY0 = tallBandY0 < 0 ? y : tallBandY0;
		}
	}
	/* the score bar at the bottom of the bottom screen */
	uint32_t clear = emu_hud_clear();
	/* (carry: but what comes down into its rows from above, part of
	 * something over the table (the zone card's ribbon down the screen's
	 * left edge, a banner sliding up through them), stays with the rest of
	 * it on the top screen: the columns with something in the two rows just
	 * above, and 8 either side) */
	static bool down[GBA_W];
	bool anyDown = false;
	memset(down, 0, sizeof(down));
	if (carry && !firstLive && !msg && t->hudRow >= 2) {
		for (int x = 0; x < GBA_W; ++x) {
			if ((hudPx[(t->hudRow - 1) * GBA_W + x] & 0xFFFFFF) == clear && (hudPx[(t->hudRow - 2) * GBA_W + x] & 0xFFFFFF) == clear) continue;
			for (int d = SDL_max(0, x - 8); d <= SDL_min(GBA_W - 1, x + 8); ++d) down[d] = true;
			anyDown = true;
		}
	}
	for (int i = GBA_W * t->hudRow; i < GBA_W * GBA_H; ++i) {
		if ((hudPx[i] & 0xFFFFFF) == clear) continue;
		if (anyDown && down[i % GBA_W]) tallTop[i] = hudPx[i] | 0xFF000000;
		else tallBot[i] = hudPx[i] | 0xFF000000;
	}
	if (!msg) memcpy(lastTop, tallTop, sizeof(lastTop)), haveLastTop = true;
	uint32_t* second = cfg->swap ? tallTop : tallBot;
	for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], second, GBA_W * 4);
	int same = -1; /* (trace: where a copy shows the game's own rows, how many pixels differ) */
	if (tallTrace && twins[1].picRow == s) {
		same = 0;
		for (int i = GBA_W * 24; i < GBA_W * t->hudRow; ++i) same += (twins[1].pic[i] & 0xFFFFFF) != (emu.fb[i] & 0xFFFFFF);
	}
	if (tallTrace && carry) {
		/* (trace: the game's picture against the copies' at the same table
		 * rows: under the score bar, the middle, under the message line) */
		int d[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
		for (int k = 0; k < 2; ++k) {
			const Twin* tw = &twins[k];
			if (!tw->up || !tw->havePic) continue;
			for (int y = 0; y < GBA_H; ++y) {
				int cy = s + y - tw->picRow;
				if (cy < 0 || cy >= GBA_H) continue;
				int r = y < 24 ? 0 : y < 150 ? 1 : 2;
				++n[r];
				for (int x = 0; x < GBA_W; ++x) d[r] += (tw->pic[cy * GBA_W + x] & 0xFFFFFF) != (emu.fb[y * GBA_W + x] & 0xFFFFFF);
			}
		}
		if (n[0] + n[1] + n[2]) fprintf(stderr, "cmp %d/%d %d/%d %d/%d\n", d[0], n[0], d[1], n[1], d[2], n[2]);
	}
	if (getenv("GBADS_TALL_AT") && frameCount == (unsigned long long) atoll(getenv("GBADS_TALL_AT"))) {
		static uint32_t sh[GBA_W * 3 * GBA_H];
		for (int y = 0; y < GBA_H; ++y) for (int x = 0; x < GBA_W; ++x) {
			sh[y * GBA_W * 3 + x] = emu.fb[y * GBA_W + x] | 0xFF000000;
			sh[y * GBA_W * 3 + GBA_W + x] = twins[0].pic[y * GBA_W + x] | 0xFF000000;
			sh[y * GBA_W * 3 + 2 * GBA_W + x] = twins[1].pic[y * GBA_W + x] | 0xFF000000;
		}
		stbi_write_png(getenv("GBADS_TALL_PNG"), GBA_W * 3, GBA_H, 4, sh, GBA_W * 3 * 4);
		char sp[1200];
		snprintf(sp, sizeof sp, "%s.game.ss", getenv("GBADS_TALL_PNG")); emu_state_save_file(&emu, sp);
		snprintf(sp, sizeof sp, "%s.b.ss", getenv("GBADS_TALL_PNG")); emu_state_save_file(&twins[1].e, sp);
		FILE* of = fopen(getenv("GBADS_TALL_PNG"), "ab"); if (of) fclose(of);
		for (int w = 0; w < 2; ++w) {
			snprintf(sp, sizeof sp, "%s.%s.oam", getenv("GBADS_TALL_PNG"), w ? "b" : "game");
			FILE* f = fopen(sp, "wb");
			for (int i = 0; f && i < 512; ++i) { uint16_t v = emu_read16(w ? &twins[1].e : &emu, 0x07000000 + i * 2); fwrite(&v, 2, 1, f); }
			if (f) fclose(f);
		}
	}
	char* tl = tallLog[tallLogN++ % TALL_LOG];
	snprintf(tl, sizeof(tallLog[0]), "T %llu s %d v %d top %s a %d%s%s b %d%s%s hid %d/%d missing %d laid %d band %d-%d fixed %d/%d same %d noobj %d%s%s",
	         (unsigned long long) frameCount, s, v, gameOnTop ? "game" : "copy", twins[0].picRow, twins[0].havePic ? "" : " away",
	         twins[0].up ? "" : " down", twins[1].picRow, twins[1].havePic ? "" : " away", twins[1].up ? "" : " down", twins[0].hidden,
	         twins[1].hidden, missing, laid, tallBandY0, tallBandY1, nTallBandFixed, nTallFixed, same, (int) twinNoObj,
	         tallFixedHold && tallFixedBot ? " banners below" : "", msg && tallTopLogo ? " logo above" : "");
	if (tallTrace) fprintf(stderr, "%s\n", tl);
	if (!tallLive) fprintf(stderr, "frame %llu: live view\n", (unsigned long long) frameCount);
	tallLive = true;
	mapShownIdx = 0;
}

static void update_snapshot(void) {
	const typeof(snapProf->snap)* sn = &snapProf->snap;
	int tol = sn->tol ? sn->tol : 12;
	if (probes_hit(emu.fb, sn->show, tol) >= sn->showMin) {
		snapRevert = 0;
		if (++snapShow >= SDL_max(1, sn->stable)) {
			const uint32_t* px = emu.fb;
			if (sn->hide[0].x1) {
				/* only when the picture changes (the sheets are still) */
				static uint32_t last[GBA_W * GBA_H], clean[GBA_W * GBA_H];
				if (mapShownIdx < 0 || memcmp(last, emu.fb, sizeof(last))) {
					memcpy(last, emu.fb, sizeof(last));
					int n = 0;
					while (n < 4 && sn->hide[n].x1) ++n;
					snapshot_hiding_sprites(sn->hide, n, NULL, 0, clean);
					memcpy(snapShown, clean, sizeof(snapShown));
					for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], clean, GBA_W * 4);
				}
				px = NULL;
			}
			if (px) {
				memcpy(snapShown, px, sizeof(snapShown));
				for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, GBA_W * 4);
			}
			if (mapShownIdx < 0 && debug) fprintf(stderr, "frame %llu: screen copied to the second screen\n", (unsigned long long) frameCount);
			mapShownIdx = 0;
		}
		return;
	}
	snapShow = 0;
	if (mapShownIdx < 0) return;
	bool menu = false;
	for (int k = 0; k < 2; ++k) {
		if (sn->revertMin[k] && probes_hit(emu.fb, sn->revert[k], tol) >= sn->revertMin[k]) menu = true;
	}
	if (!menu) {
		snapRevert = 0;
	} else if (++snapRevert >= SDL_max(1, sn->stable)) {
		mapShownIdx = -1;
		snapRevert = 0;
		if (debug) fprintf(stderr, "frame %llu: back at the menus, logo\n", (unsigned long long) frameCount);
	}
}

/* ---------------------------------------------------------- control scheme
 * Games with a four-face-button layout (profile `controls`): a picker on the
 * second screen (touch only) chooses the GBA layout, the four-face one, or
 * Auto (four-face while the player controls the character). Saved per game
 * in <state_dir>/<game>.controls. */
static int game_screen(void);
enum { CTL_GBA, CTL_FOUR, CTL_AUTO };
static const char* const ctlNames[] = { "gba", "four", "auto" };
static bool snesHere; /* SNES layout: the game is where it applies (controls.snes.when: in a level) */
static int ctlMode;
static bool ctlPlaying; /* Auto: the player is controlling the character */
static char ctlPath[1400];

/* The game's own HUD is up (display mode and a sprite at each position). */
static bool ctl_playing_now(void) {
	const typeof(ctlProf->controls)* c = &ctlProf->controls;
	if (c->playing[0].addr) return memprobes_match(c->playing, 2, &emu);
	if ((emu_read16(&emu, 0x04000000) & 7) != c->mode) return false;
	int want = 0, found = 0;
	while (want < 4 && (c->hud[want].x || c->hud[want].y)) ++want;
	for (int i = 0; i < 128 && found != (1 << want) - 1; ++i) {
		uint16_t a0 = emu_read16(&emu, 0x07000000 + i * 8);
		if ((a0 & 0x0300) == 0x0200) continue; /* disabled */
		int y = a0 & 0xFF, x = emu_read16(&emu, 0x07000002 + i * 8) & 0x1FF;
		for (int k = 0; k < want; ++k) {
			if (x == c->hud[k].x && y == c->hud[k].y) found |= 1 << k;
		}
	}
	return want && found == (1 << want) - 1;
}

/* tiles in the picker: Auto only where the profile can tell when the player
 * is skating (its display mode and HUD sprites) */
static int ctl_tiles(void) {
	return ctlProf && (ctlProf->controls.hud[0].x || ctlProf->controls.hud[0].y || ctlProf->controls.playing[0].addr) ? 3 : 2;
}

static bool ctl_four(void) {
	return ctlProf && (ctlMode == CTL_FOUR || (ctlMode == CTL_AUTO && ctlPlaying));
}

/* the alternate layout is the SNES one (profile controls.snes), not four-face */
static bool ctl_snes(void) {
	return ctlProf && ctlProf->controls.snes.bottom;
}

static const char* ctl_name(int i) {
	return i == CTL_FOUR && ctl_snes() ? "snes" : ctlNames[i];
}

static void ctl_load(void) {
	ctlMode = CTL_GBA;
	ctlPlaying = false;
	snprintf(ctlPath, sizeof(ctlPath), "%s/%s.controls", cfg->stateDir, gp.base);
	FILE* f = fopen(ctlPath, "r");
	if (!f) return;
	char w[16] = "";
	if (fscanf(f, "%15s", w) == 1) {
		for (int i = 0; i < 3; ++i) {
			if (!strcmp(w, ctl_name(i))) ctlMode = i;
		}
	}
	fclose(f);
	if (ctlMode >= ctl_tiles()) ctlMode = CTL_GBA;
}

static void ctl_save(void) {
	FILE* f = fopen(ctlPath, "w");
	if (!f) return;
	fprintf(f, "%s\n", ctl_name(ctlMode));
	fclose(f);
}

/* the picker is in the quick bar, while it's up */
static bool ctl_shown(int s) {
	return ctlProf && barOpen && s == ui_touch_screen();
}

typedef struct {
	SDL_Rect tile[3];
	SDL_Rect all; /* label and tiles: taps here never open the menu */
	int labelX, labelY, cap;
} CtlGeom;

/* The quick bar's top row: "Controls" and two or three square tiles,
 * centred across the bar (in GBA pixels). */
static CtlGeom ctl_geom_g(void) {
	CtlGeom g;
	SDL_Rect b = bar_rect_g();
	int t = BAR_PICK_H - 4, tw = 32, gap = 3; /* (tiles tw wide, t high) */
	g.cap = 6;
	char ca[64];
	const char* ct = tr("Controls");
	int lw = cv_ascii(ct, ca, sizeof(ca)) && pixfont_has(&pixfont_small, ca) ? pixfont_width(&pixfont_large, ca) : tf_width(TF_BOLD, pixfont_large.cap, ct);
	int n = ctl_tiles();
	int total = lw + gap * 3 + tw * n + gap * (n - 1);
	int x = b.x + (b.w - total) / 2;
	int y = b.y + 4;
	g.all = (SDL_Rect) { x - gap, y - 1, total + gap * 2, t + 2 };
	g.labelX = x + lw / 2;
	g.labelY = y + t / 2;
	x += lw + gap * 3;
	for (int i = 0; i < 3; ++i) g.tile[i] = (SDL_Rect) { x + i * (tw + gap), y, i < n ? tw : 0, i < n ? t : 0 };
	return g;
}
/* (on screen s, for taps) */
static CtlGeom ctl_geom(int s) {
	CtlGeom g = ctl_geom_g();
	g.all = bar_to_screen(s, g.all);
	for (int i = 0; i < 3; ++i) g.tile[i] = bar_to_screen(s, g.tile[i]);
	return g;
}

/* a tile's picture in ink, one pixel to a pixel: the GBA's two shoulder buttons and its B and A, or the four-button layout's diamond */
static void ctl_tile_art(int kind, SDL_Rect t, SDL_Color ink) {
	float cx = t.x + t.w / 2.0f, cy = t.y + t.h / 2.0f;
	if (kind == CTL_GBA) {
		cv_rect((SDL_Rect) { (int) cx - 9, t.y + 4, 5, 2 }, ink);
		cv_rect((SDL_Rect) { (int) cx + 4, t.y + 4, 5, 2 }, ink);
		cv_disc(cx - 3.5f, cy + 3.5f, 2.5f, ink);
		cv_disc(cx + 3.5f, cy + 1.5f, 2.5f, ink);
	} else {
		cv_disc(cx, cy - 4.5f, 2.2f, ink);
		cv_disc(cx - 4.5f, cy, 2.2f, ink);
		cv_disc(cx + 4.5f, cy, 2.2f, ink);
		cv_disc(cx, cy + 4.5f, 2.2f, ink);
	}
}

static void ctl_draw(void) {
	CtlGeom g = ctl_geom_g();
	int active = ctl_four() ? CTL_FOUR : CTL_GBA;
	cv_label(g.labelX, g.labelY, true, "Controls", G_TEXT, 0);
	for (int i = 0; i < ctl_tiles(); ++i) {
		SDL_Rect t = g.tile[i];
		bool sel = ctlMode == i;
		/* the menus' keys: chosen: in the menu colour; in Auto, the layout in use right now: lighter */
		bool inUse = !sel && ctlMode == CTL_AUTO && i == active;
		cv_key(t, sel ? KEY_SEL : inUse ? KEY_LIT : KEY_PLAIN);
		SDL_Color tink = inUse ? (SDL_Color) { 20, 16, 36, 255 } : sel ? G_TEXT : ui_key_ink();
		if (ctl_snes()) cv_label(t.x + t.w / 2, t.y + t.h / 2, true, i == CTL_GBA ? "GBA" : "SNES", tink, t.w - 4);
		else if (i == CTL_GBA || i == CTL_FOUR) ctl_tile_art(i, t, tink);
		else cv_label(t.x + t.w / 2, t.y + t.h / 2, true, "Auto", tink, t.w - 4);
	}
}

/* A tap on the quick bar's picker: true if it was there (it then doesn't open the menu). */
static bool ctl_tap(int s, int x, int y) {
	if (!ctl_shown(s)) return false;
	CtlGeom g = ctl_geom(s);
	SDL_Point pt = { x, y };
	if (!SDL_PointInRect(&pt, &g.all)) return false;
	for (int i = 0; i < ctl_tiles(); ++i) {
		if (SDL_PointInRect(&pt, &g.tile[i]) && ctlMode != i) {
			ctlMode = i;
			ctl_save();
			if (debug) fprintf(stderr, "frame %llu: controls %s\n", (unsigned long long) frameCount, ctl_name(i));
		}
	}
	return true;
}


/* ------------------------------------------------------------------- the sun
 * Cartridges with a sun sensor (Boktai 1, 2 and 3) have a Sun row in the quick
 * bar, where Controls is in other games: Dark (0), - and + (one step, as
 * RetroArch's darken / brighten sensor buttons), the level between them, and
 * Full (10). The level is each core's sunlight (emu_solar_level), kept per game
 * in <state_dir>/<game>.sun and starting from the solar_level setting. */
enum { SUN_DARK, SUN_DOWN, SUN_LEVEL, SUN_UP, SUN_FULL, SUN_TILES };
static int sunLevel;
static char sunPath[1400];

static void sun_set(int v) {
	sunLevel = v < 0 ? 0 : v > 10 ? 10 : v;
	emu_solar_level = sunLevel;
}

static void sun_save(void) {
	FILE* f = fopen(sunPath, "w");
	if (!f) return;
	fprintf(f, "%d\n", sunLevel);
	fclose(f);
}

static void sun_load(void) {
	sunHere = emu_has_solar(&emu);
	if (!sunHere) return;
	snprintf(sunPath, sizeof(sunPath), "%s/%s.sun", cfg->stateDir, gp.base);
	int v = cfg->solarLevel;
	FILE* f = fopen(sunPath, "r");
	if (f) {
		int n;
		if (fscanf(f, "%d", &n) == 1) v = n;
		fclose(f);
	}
	sun_set(v);
	fprintf(stderr, "sun: level %d\n", sunLevel);
}

static SDL_Rect sun_tile_g(int i, int* labelX, int* labelY) {
	SDL_Rect b = bar_rect_g();
	int t = BAR_PICK_H - 4, tw = 30, gap = 3;
	char ca[64];
	const char* st = tr("Sun");
	int lw = cv_ascii(st, ca, sizeof(ca)) && pixfont_has(&pixfont_small, ca) ? pixfont_width(&pixfont_large, ca) : tf_width(TF_BOLD, pixfont_large.cap, st);
	int total = lw + gap * 3 + tw * SUN_TILES + gap * (SUN_TILES - 1);
	int x = b.x + (b.w - total) / 2, y = b.y + 4;
	if (labelX) *labelX = x + lw / 2;
	if (labelY) *labelY = y + t / 2;
	x += lw + gap * 3;
	return (SDL_Rect) { x + i * (tw + gap), y, tw, t };
}

static void sun_draw(void) {
	int lx, ly;
	sun_tile_g(0, &lx, &ly);
	cv_label(lx, ly, true, "Sun", G_TEXT, 0);
	for (int i = 0; i < SUN_TILES; ++i) {
		SDL_Rect t = sun_tile_g(i, NULL, NULL);
		bool lvl = i == SUN_LEVEL;
		cv_key(t, lvl ? KEY_LIT : KEY_PLAIN);
		SDL_Color ink = lvl ? (SDL_Color) { 20, 16, 36, 255 } : ui_key_ink();
		char b[8];
		const char* txt = i == SUN_DARK ? "Dark" : i == SUN_DOWN ? "-" : i == SUN_UP ? "+" : i == SUN_FULL ? "Full" : (snprintf(b, sizeof(b), "%d", sunLevel), b);
		cv_label(t.x + t.w / 2, t.y + t.h / 2, true, txt, ink, t.w - 4);
	}
}

/* A tap on the Sun row: true if it was there (it then doesn't open the menu). */
static bool sun_tap(int s, int x, int y) {
	if (!sunHere || ctlProf || !barOpen || s != ui_touch_screen()) return false;
	SDL_Point pt = { x, y };
	SDL_Rect br = bar_rect_g();
	SDL_Rect all = bar_to_screen(s, (SDL_Rect) { br.x, br.y + 3, br.w - 14, BAR_PICK_H - 2 }); /* (the pin is in the corner) */
	if (!SDL_PointInRect(&pt, &all)) return false;
	for (int i = 0; i < SUN_TILES; ++i) {
		SDL_Rect t = bar_to_screen(s, sun_tile_g(i, NULL, NULL));
		if (i == SUN_LEVEL || !SDL_PointInRect(&pt, &t)) continue;
		int was = sunLevel;
		sun_set(i == SUN_DARK ? 0 : i == SUN_FULL ? 10 : sunLevel + (i == SUN_UP ? 1 : -1));
		if (sunLevel != was) {
			sun_save();
			if (debug) fprintf(stderr, "frame %llu: sun %d\n", (unsigned long long) frameCount, sunLevel);
		}
	}
	return true;
}

static void save_thumbs(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(emu.fb, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
	if (s) {
		SDL_SaveBMP(s, gp.thumb);
		SDL_FreeSurface(s);
	}
	const uint32_t* still = stillValid ? stillPx : mapRes.frames[0];
	if (mapShownIdx >= 0 && still) {
		s = SDL_CreateRGBSurfaceWithFormatFrom((void*) still, mapW, mapH, 32, mapW * 4, SDL_PIXELFORMAT_XBGR8888);
		if (s) {
			SDL_SaveBMP(s, gp.mapThumb);
			SDL_FreeSurface(s);
		}
	}
}

static void map_show_new(bool fresh);

/* The second screen kept with save states (inside the state file: emu.c
 * emu_state_file_*_extra): what it holds that the loaded game can't give
 * back at once. A block "GDS2", version, the game's code, then sections
 * (a 4-letter tag, a size, the data):
 *   MAP  the map or status pictures shown (w, h, frames, step, frames' pixels)
 *   SNAP the screen copied there (THPS2's goal sheet)
 *   TITL the level name kept above the HUD (Super Mario Advance 2)
 *   CARD the title card taken (Sonic Advance)
 * Put back at the next frame after the load (screen_restore_apply), where
 * the game's code is the same; anything else of the second screen comes
 * back from the loaded game as usual. */
typedef struct { uint8_t* p; size_t n, cap; } Blob;
static void blob_add(Blob* b, const void* d, size_t n) {
	if (b->n + n > b->cap) {
		size_t c = SDL_max(b->cap * 2, b->n + n + 4096);
		uint8_t* q = realloc(b->p, c);
		if (!q) return;
		b->p = q, b->cap = c;
	}
	memcpy(b->p + b->n, d, n), b->n += n;
}
static void blob_u32(Blob* b, uint32_t v) { blob_add(b, &v, 4); }
static void blob_section(Blob* b, const char* tag, const void* d, size_t n) {
	blob_add(b, tag, 4), blob_u32(b, (uint32_t) n), blob_add(b, d, n);
}
static void* screen_blob(size_t* n) {
	Blob b = { 0 };
	blob_add(&b, "GDS2", 4), blob_u32(&b, 1), blob_add(&b, subCode, 4);
	if (mapShownIdx >= 0 && !snapProf && mapRes.ok && mapRes.frames[0] && mapRes.nFrames > 0 && mapRes.w > 0 && mapRes.h > 0) {
		Blob m = { 0 };
		size_t px = (size_t) mapRes.w * mapRes.h * 4;
		blob_u32(&m, (uint32_t) mapRes.w), blob_u32(&m, (uint32_t) mapRes.h), blob_u32(&m, (uint32_t) mapRes.nFrames), blob_u32(&m, (uint32_t) mapRes.frameStep);
		for (int i = 0; i < mapRes.nFrames; ++i) blob_add(&m, mapRes.frames[i], px);
		if (m.p) blob_section(&b, "MAP ", m.p, m.n);
		free(m.p);
	}
	if (snapProf && mapShownIdx >= 0) blob_section(&b, "SNAP", snapShown, sizeof(snapShown));
	if (mapTitleN > 0) {
		Blob t = { 0 };
		blob_u32(&t, (uint32_t) mapTitleN), blob_add(&t, mapTitle, (size_t) GBA_W * mapTitleN * 4);
		if (t.p) blob_section(&b, "TITL", t.p, t.n);
		free(t.p);
	}
	if (hudProf && hudProf->hud.held.y1 && heldN > 0) {
		Blob t = { 0 };
		blob_u32(&t, (uint32_t) heldN), blob_add(&t, heldPic, (size_t) GBA_W * heldN * 4);
		if (t.p) blob_section(&b, "HELD", t.p, t.n);
		free(t.p);
		if (heldNumW > 0) {
			Blob u = { 0 };
			blob_u32(&u, (uint32_t) heldNumW), blob_u32(&u, (uint32_t) heldNumH), blob_add(&u, heldNum, (size_t) heldNumW * heldNumH * 4);
			if (u.p) blob_section(&b, "HNUM", u.p, u.n);
			free(u.p);
		}
	}
	if (hudProf && hudProf->hud.card.tile && hudBackValid && cardScore >= 0) {
		Blob c = { 0 };
		blob_u32(&c, (uint32_t) cardScore), blob_add(&c, cardBest, sizeof(cardBest));
		if (c.p) blob_section(&b, "CARD", c.p, c.n);
		free(c.p);
	}
	*n = b.n;
	return b.p;
}
/* a slot or the automatic state in RetroArch's folder (shared with it: issue #39) */
static bool state_is_retroarch(const char* path) {
	size_t n = strlen(gp.raDir);
	return n && !strncmp(path, gp.raDir, n) && path[n] == '/';
}
/* RetroArch's thumbnail beside a state (<state>.png, for its menu): the
 * game's picture as it is now, at the GBA's size */
static void state_thumbnail(const char* path) {
	uint8_t* px = malloc((size_t) GBA_W * GBA_H * 4);
	if (!px) return;
	memcpy(px, emu.fb, (size_t) GBA_W * GBA_H * 4); /* (bytes R, G, B, X) */
	for (size_t i = 0; i < (size_t) GBA_W * GBA_H; ++i) px[i * 4 + 3] = 255;
	char png[1100], tmp[1110];
	snprintf(png, sizeof(png), "%s.png", path);
	snprintf(tmp, sizeof(tmp), "%s.tmp.png", path);
	if (!stbi_write_png(tmp, GBA_W, GBA_H, 4, px, GBA_W * 4) || rename(tmp, png) != 0) {
		remove(tmp);
		fprintf(stderr, "save state: no thumbnail %s\n", png);
	}
	free(px);
}
static bool state_file_save(const char* path) {
	size_t n;
	void* blob = screen_blob(&n);
	bool ra = state_is_retroarch(path);
	StateFormat fmt = !ra ? STATE_FORMAT_MGBA : gp.raCompress ? STATE_FORMAT_RETROARCH_RZIP : STATE_FORMAT_RETROARCH;
	if (ra) mkdirs(gp.raDir);
	bool ok = emu_state_save_file_as(&emu, path, blob, blob ? n : 0, fmt);
	free(blob);
	if (ok && ra) state_thumbnail(path);
	return ok;
}
static uint8_t* screenRestore; /* a loaded state's block, put back at the next frame */
static size_t screenRestoreN;
static bool screenRestorePending;
static const uint8_t* blob_find(const uint8_t* b, size_t n, const char* tag, uint32_t* size) {
	for (size_t at = 12; at + 8 <= n;) {
		uint32_t sz;
		memcpy(&sz, b + at + 4, 4);
		if (at + 8 + sz > n) break;
		if (!memcmp(b + at, tag, 4)) return *size = sz, b + at + 8;
		at += 8 + sz;
	}
	return NULL;
}
static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
/* a loaded state's frames are pages when this game's profile says its
 * frames are (a state keeps the frames and their pace, not what they are) */
static bool state_pages(int nFrames) {
	const MapProfile* hp = mc ? mapcap_profile(mc) : NULL;
	return nFrames > 1 && hp && (hp->pages || hp->views);
}
/* (at the start of a frame, everything set up) */
static void screen_restore_apply(void) {
	if (!screenRestorePending) return;
	screenRestorePending = false;
	const uint8_t* b = screenRestore;
	size_t n = screenRestoreN;
	bool ours = b && n >= 12 && !memcmp(b, "GDS2", 4) && rd32(b + 4) == 1 && !memcmp(b + 8, subCode, 4);
	/* what was kept from before the load is another moment's: the title
	 * card and the level name go (back from the state, if it has them) */
	if (hudProf && hudProf->hud.card.tile) hudBackValid = false, cardScore = -1, sideScore = -1;
	mapTitleN = 0, hudLogoBelowDone = false;
	heldN = 0, heldNumW = 0, heldArmed = true;
	logoForced = false, logoHolding = 0;
	if (!ours) {
		if (b && debug) fprintf(stderr, "load state: its second screen is for another game or version: not used\n");
		return;
	}
	uint32_t sz;
	const uint8_t* d;
	if ((d = blob_find(b, n, "MAP ", &sz)) && sz >= 16) {
		int w = (int) rd32(d), h = (int) rd32(d + 4), nf = (int) rd32(d + 8), step = (int) rd32(d + 12);
		size_t px = (size_t) w * h * 4;
		/* (only at the size this game's captures are: frames are that big) */
		if (mapRes.frames[0] && w == mapRes.w && h == mapRes.h && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			for (int i = 0; i < nf; ++i) memcpy(mapRes.frames[i], d + 16 + px * i, px);
			mapRes.nFrames = nf, mapRes.frameStep = step, mapRes.pages = state_pages(nf), mapRes.ok = true, mapRes.pixels = mapRes.frames[0];
			map_show_new(false);
		} else if (!mapRes.frames[0] && w > 0 && h > 0 && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			/* (no capture yet this session: the frames made here, as mapcap_poll does) */
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) mapRes.frames[i] = malloc(px);
			bool ok = true;
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) ok = ok && mapRes.frames[i];
			if (ok) {
				for (int i = 0; i < nf; ++i) memcpy(mapRes.frames[i], d + 16 + px * i, px);
				mapRes.w = w, mapRes.h = h, mapRes.nFrames = nf, mapRes.frameStep = step, mapRes.pages = state_pages(nf), mapRes.ok = true, mapRes.pixels = mapRes.frames[0];
				map_show_new(false);
			}
		}
	}
	if (snapProf && (d = blob_find(b, n, "SNAP", &sz)) && sz == sizeof(snapShown)) {
		memcpy(snapShown, d, sz);
		for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], snapShown, GBA_W * 4);
		mapShownIdx = 0;
	}
	if ((d = blob_find(b, n, "TITL", &sz)) && sz >= 4) {
		int rows = (int) rd32(d);
		if (rows > 0 && rows <= 32 && sz == 4 + (size_t) GBA_W * rows * 4) memcpy(mapTitle, d + 4, sz - 4), mapTitleN = rows;
	}
	if (hudProf && hudProf->hud.held.y1 && (d = blob_find(b, n, "HELD", &sz)) && sz >= 4) {
		int rows = (int) rd32(d);
		if (rows > 0 && rows <= GBA_H && sz == 4 + (size_t) GBA_W * rows * 4) memcpy(heldPic, d + 4, sz - 4), heldN = rows;
	}
	if (hudProf && hudProf->hud.held.over.x1 && (d = blob_find(b, n, "HNUM", &sz)) && sz >= 8) {
		int w = (int) rd32(d), hh = (int) rd32(d + 4);
		if (w > 0 && hh > 0 && w * hh <= GBA_W * GBA_H && sz == 8 + (size_t) w * hh * 4) memcpy(heldNum, d + 8, sz - 8), heldNumW = w, heldNumH = hh;
	}
	if (hudProf && hudProf->hud.card.tile && (d = blob_find(b, n, "CARD", &sz)) && sz == 4 + sizeof(cardBest)) {
		cardScore = (int) rd32(d);
		memcpy(cardBest, d + 4, sizeof(cardBest));
		card_backdrop();
	}
	if (debug) fprintf(stderr, "load state: the second screen from it (%s%s%s%s%s)\n", blob_find(b, n, "MAP ", &sz) ? " map" : "",
	                   blob_find(b, n, "SNAP", &sz) ? " copy" : "", blob_find(b, n, "TITL", &sz) ? " title" : "", blob_find(b, n, "CARD", &sz) ? " card" : "",
	                   blob_find(b, n, "HELD", &sz) ? " held" : "");
}

/* Save states, with the RetroAchievements progress beside them (<state>.ra;
 * for one in RetroArch's folder, in GBA DS's own: <state_dir>/retroarch/) */
static const char* state_ra_side(const char* path) {
	static char side[1200];
	if (!state_is_retroarch(path)) return path;
	snprintf(side, sizeof(side), "%s/retroarch", cfg->stateDir);
	mkdirs(side);
	snprintf(side, sizeof(side), "%s/retroarch/%s", cfg->stateDir, strrchr(path, '/') + 1);
	return side;
}
static bool state_save(const char* path) {
	bool ok = state_file_save(path);
	if (ok) ra_state_saved(state_ra_side(path));
	return ok;
}

/* GEN-9: a state saved with another ROM than the one playing (other
 * patches, none, another dump) still loads, with a note */
static bool stateWarned; /* (the last load's note is up: callers' "loaded" notes wait) */
static bool stateNoCheck;
static void state_rom_check(const char* path) {
	uint32_t crc;
	stateWarned = false;
	if (!emu_state_file_rom_crc(path, &crc) || crc == emu.romCrc) return;
	const char* name = NULL;
	DumpKind k = profile_dump(emu.gameCode, crc, &name);
	char what[300];
	bool patched = emu.patch == PATCH_APPLIED || emu.patch == PATCH_PARTIAL;
	if (patched && crc == emu.fileCrc) snprintf(what, sizeof(what), tr("without the patches on"));
	else if (name && (k == DUMP_MOD || k == DUMP_MOD_TESTED)) snprintf(what, sizeof(what), tr("with %s"), name);
	else if (name) {
		/* (an official dump: the unchanged game, if this one's changed) */
		DumpKind now = profile_dump(emu.gameCode, emu.romCrc, NULL);
		bool changed = patched || now == DUMP_MOD || now == DUMP_MOD_TESTED || now == DUMP_MODIFIED;
		snprintf(what, sizeof(what), changed ? tr("with the unchanged game (%s)"): tr("with another dump of the game (%s)"), name);
	}
	else snprintf(what, sizeof(what), tr("with another version of the game (other patches, or another ROM)"));
	fprintf(stderr, "load state: %s was saved %s (ROM CRC32 %08x, playing %08x)\n", path, what, crc, emu.romCrc);
	ui_osd_long("This state was saved %s: it may not work", what);
	stateWarned = true;
}

static StateFileKind stateLoadKind; /* (what the last state_load found: why it failed) */
static BiosRefusal stateLoadBios;   /* (and whether it was the GBA BIOS, issue #78) */
static bool state_load(const char* path) {
	bool fromGbads = false;
	stateLoadKind = emu_state_file_kind(&emu, path, &fromGbads);
	bool ok = emu_state_load_file(&emu, path);
	stateLoadBios = ok ? BIOS_REFUSAL_NONE : emu_state_file_bios_refusal(&emu, path);
	{
		static const char* const kinds[] = { "no file", "GBA DS's / mGBA's", "RetroArch's", "another emulator's", "unreadable" };
		fprintf(stderr, "load state: %s: %s (%s%s)\n", path, ok ? "loaded" : "not loaded", kinds[stateLoadKind],
		        stateLoadKind == STATE_FILE_RETROARCH ? (fromGbads ? ", saved by GBA DS" : ", saved by RetroArch") : "");
		if (stateLoadBios != BIOS_REFUSAL_NONE)
			fprintf(stderr, "load state: refused by mGBA: %s\n", stateLoadBios == BIOS_REFUSAL_STATE_HAS_BIOS
			                                                         ? "saved inside the GBA BIOS, which this game isn't run with"
			                                                         : "saved without the GBA BIOS, and this game runs with it");
	}
	stateWarned = false;
	if (ok && !stateNoCheck) state_rom_check(path);
	if (ok) {
		/* (saved by RetroArch over one of GBA DS's: the progress kept with
		 * that one is another moment's, so achievements start over) */
		const char* side = state_ra_side(path);
		if (side != path && !fromGbads) {
			char ra[1210];
			snprintf(ra, sizeof(ra), "%s.ra", side);
			remove(ra);
		}
		ra_state_loaded(side);
	}
	/* (the second screen kept with it: put back at the next frame) */
	if (ok) {
		free(screenRestore);
		screenRestore = emu_state_file_get_extra(&emu, path, &screenRestoreN);
		screenRestorePending = true;
	}
	if (ok) bug_reset();
	/* HUD pieces from another moment: taken again from the loaded game */
	for (int m = 0; ok && m < 4; ++m) pieces[m].stale = true, pieces[m].still = 0;
	if (ok) aheadReady = false; /* a frame emulated before the load: emulate afresh */
	if (ok) mirrorHave = false; /* (hud.mirror: the picture from before is another moment's) */
	if (ok) hudOffLatch = false; /* (hud.offAfter: found again from the loaded game) */
	return ok;
}

/* the quick bar's and the hotkey's word for a load that failed */
static const char* load_failed_osd(void) {
	if (stateLoadBios == BIOS_REFUSAL_STATE_HAS_BIOS) return "Saved with a GBA BIOS: see Settings > System > GBA BIOS.";
	if (stateLoadBios == BIOS_REFUSAL_CORE_HAS_BIOS) return "Saved without a GBA BIOS: switch yours off in Settings > System.";
	return "Load failed.";
}

#ifdef __wasi__
/* State Saver's GBA DS tab: states saved and loaded from the page, between frames */
static bool webPlaying;
__attribute__((export_name("web_save_state"))) int web_save_state(const char* path) {
	return webPlaying && state_save(path);
}
__attribute__((export_name("web_load_state"))) int web_load_state(const char* path) {
	return webPlaying && state_load(path);
}
#endif

/* ---------------------------------------------------------------- bug reports
 *
 * The quick bar's bug button: <asset_dir>/bug-reports/GAME-DATE_TIME.zip
 * holding the game now (GAME-DATE_TIME.state), 10 seconds before
 * (...-rewound.state) and every key the game got in between
 * (...-keys.txt: the presses and lifts to read, then the same as a
 * harness script that plays it back from the rewound state, exactly). */

/* README.txt in the bug-reports folder: how to send a report (written with
 * the first report, if it isn't there; replaced when the text changes) */
static const char bugReadme[] =
	"GBA DS bug reports\n"
	"==================\n"
	"\n"
	"Each zip in this folder is one bug report, saved by the quick bar's bug\n"
	"button or by Submit a Bug Report in the pause menu (Extras), after a\n"
	"note you can type (or skip) while the game waits. It holds:\n"
	"\n"
	"  - GAME-DATE.state           the game at the moment you made the report\n"
	"  - GAME-DATE-rewound.state   the game about 10 seconds before\n"
	"  - GAME-DATE-keys.txt        every button you pressed in between\n"
	"  - GAME-DATE-info.txt        your note (if you typed one), your device,\n"
	"                              system, screens and settings, and the end\n"
	"                              of GBA DS's log\n"
	"\n"
	"In the pinball games' two-screen view, also what both screens showed\n"
	"(GAME-DATE-tall.png, with the two extra copies' own pictures), the\n"
	"view's last 10 seconds (GAME-DATE-tall.txt) and the copies themselves\n"
	"(GAME-DATE-copy0.state, -copy1.state).\n"
	"\n"
	"With these, the problem can be played back exactly as it happened.\n"
	"Your game itself (the ROM) is not in the report, and lines of the log\n"
	"that mention passwords or tokens are left out.\n"
	"\n"
	"\n"
	"How to send a report\n"
	"--------------------\n"
	"\n"
	"1. Make the report right after the problem shows (it keeps only the\n"
	"   last 10 seconds or so). If it takes a while to happen, make one\n"
	"   while it's on the screen. After loading a state, rewinding or\n"
	"   restarting, play on for 10 seconds before making one.\n"
	"2. Copy the zip from this folder to a computer or phone.\n"
	"3. Open a new issue on GitHub:\n"
	"   https://github.com/kyannon/gba-ds/issues/new\n"
	"   (you need a free GitHub account).\n"
	"4. Give it a short title: the game and what's wrong, for example\n"
	"   \"F-Zero: GP Legend: the course map flickers\".\n"
	"5. Drag the zip into the description box to attach it.\n"
	"6. Write what's below, then press Create.\n"
	"\n"
	"\n"
	"What to write\n"
	"-------------\n"
	"\n"
	"  - The game and its release (USA, Europe, Japan...), if it isn't in\n"
	"    the title.\n"
	"  - What you were doing: where you were in the game (a level, a race,\n"
	"    a menu, a battle) and what you pressed.\n"
	"  - What happened, on which screen (the game screen or the second one).\n"
	"  - What you expected to see instead. For the second screen, say what\n"
	"    you'd like it to show there and when.\n"
	"  - Whether it happens every time, and how to make it happen again.\n"
	"  - Photos or screenshots of both screens, if it's something you see.\n"
	"    A photo of the device is fine.\n"
	"  - Anything you changed in Settings or gba-ds.cfg, and whether cheats\n"
	"    were on.\n"
	"\n"
	"If GBA DS crashes, won't start, or a game won't load (so there's no way\n"
	"to make a report), attach gba-ds.log instead: it's in the GBA DS folder,\n"
	"next to this bug-reports folder. Say which system you use (ROCKNIX,\n"
	"BananaOS, Anbernic's own system, Android) and which device.\n"
	"\n"
	"Please don't attach game ROMs or BIOS files.\n";

static void write_bug_readme(const char* dir) {
	char path[1200];
	snprintf(path, sizeof(path), "%s/README.txt", dir);
	FILE* f = fopen(path, "rb");
	if (f) {
		static char old[sizeof(bugReadme) + 1];
		size_t n = fread(old, 1, sizeof(old), f);
		fclose(f);
		if (n == sizeof(bugReadme) - 1 && !memcmp(old, bugReadme, n)) return;
	}
	f = fopen(path, "wb");
	if (!f) return;
	fwrite(bugReadme, 1, sizeof(bugReadme) - 1, f);
	fclose(f);
}

static uint32_t crc32_of(const uint8_t* p, size_t n) {
	static uint32_t table[256];
	if (!table[1]) {
		for (uint32_t i = 0; i < 256; ++i) {
			uint32_t c = i;
			for (int k = 0; k < 8; ++k) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
			table[i] = c;
		}
	}
	uint32_t c = 0xFFFFFFFFu;
	for (size_t i = 0; i < n; ++i) c = table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
	return c ^ 0xFFFFFFFFu;
}

static void put16(FILE* f, unsigned v) {
	fputc(v & 0xFF, f), fputc((v >> 8) & 0xFF, f);
}
static void put32(FILE* f, uint32_t v) {
	put16(f, v & 0xFFFF), put16(f, v >> 16);
}

/* A .zip of n files, stored (states don't shrink much, and it needs no zlib). */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);
static bool zip_write(const char* path, const char* const* names, uint8_t* const* data, const size_t* sizes, int n) {
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	unsigned dosTime = (tm.tm_hour << 11) | (tm.tm_min << 5) | (tm.tm_sec / 2);
	unsigned dosDate = ((tm.tm_year - 80) << 9) | ((tm.tm_mon + 1) << 5) | tm.tm_mday;
	uint32_t offs[8], crcs[8];
	for (int i = 0; i < n; ++i) {
		offs[i] = (uint32_t) ftell(f);
		crcs[i] = crc32_of(data[i], sizes[i]);
		put32(f, 0x04034b50), put16(f, 20), put16(f, 0x0800), put16(f, 0), put16(f, dosTime), put16(f, dosDate);
		put32(f, crcs[i]), put32(f, (uint32_t) sizes[i]), put32(f, (uint32_t) sizes[i]);
		put16(f, (unsigned) strlen(names[i])), put16(f, 0);
		fwrite(names[i], 1, strlen(names[i]), f);
		fwrite(data[i], 1, sizes[i], f);
	}
	uint32_t cd = (uint32_t) ftell(f);
	for (int i = 0; i < n; ++i) {
		put32(f, 0x02014b50), put16(f, 20), put16(f, 20), put16(f, 0x0800), put16(f, 0), put16(f, dosTime), put16(f, dosDate);
		put32(f, crcs[i]), put32(f, (uint32_t) sizes[i]), put32(f, (uint32_t) sizes[i]);
		put16(f, (unsigned) strlen(names[i])), put16(f, 0), put16(f, 0), put16(f, 0), put16(f, 0), put32(f, 0), put32(f, offs[i]);
		fwrite(names[i], 1, strlen(names[i]), f);
	}
	uint32_t cdEnd = (uint32_t) ftell(f);
	put32(f, 0x06054b50), put16(f, 0), put16(f, 0), put16(f, n), put16(f, n), put32(f, cdEnd - cd), put32(f, cd), put16(f, 0);
	bool ok = !ferror(f);
	return fclose(f) == 0 && ok;
}

static uint8_t* read_whole(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* b = n > 0 ? malloc(n) : NULL;
	if (b && fread(b, 1, n, f) != (size_t) n) free(b), b = NULL;
	fclose(f);
	*size = b ? (size_t) n : 0;
	return b;
}

static void keys_name(char* out, size_t size, unsigned k) {
	static const char* const names[10] = { "A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L" };
	out[0] = 0;
	for (int i = 0; i < 10; ++i) {
		if (k & (1u << i)) snprintf(out + strlen(out), size - strlen(out), "%s%s", out[0] ? "+" : "", names[i]);
	}
	if (!out[0]) snprintf(out, size, "-");
}

typedef struct {
	char* s;
	size_t n, cap;
} Text;
static void text_add(Text* t, const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	char line[512];
	int k = vsnprintf(line, sizeof(line), fmt, ap);
	va_end(ap);
	if (k < 0) return;
	if ((size_t) k >= sizeof(line)) k = sizeof(line) - 1;
	if (t->n + k + 1 > t->cap) {
		size_t cap = (t->cap ? t->cap * 2 : 16384) + k;
		char* s = realloc(t->s, cap);
		if (!s) return;
		t->s = s, t->cap = cap;
	}
	memcpy(t->s + t->n, line, k + 1);
	t->n += k;
}

/* the first line of a small file, or "" */
static void read_line(const char* path, char* out, size_t size) {
	out[0] = 0;
	FILE* f = fopen(path, "r");
	if (!f) return;
	if (fgets(out, (int) size, f)) out[strcspn(out, "\r\n")] = 0;
	fclose(f);
}

/* where the log (stderr) is being written, or "" */
static void log_path(char* out, size_t size) {
	out[0] = 0;
#ifdef __ANDROID__
	if (android_home()) snprintf(out, size, "%s/gba-ds.log", android_home());
#else
	struct stat st;
	if (fstat(2, &st) != 0 || !S_ISREG(st.st_mode)) return;
	ssize_t n = readlink("/proc/self/fd/2", out, size - 1);
	out[n > 0 ? n : 0] = 0;
#endif
}

/* The report's -info.txt: the device and system, GBA DS's settings, the
 * game and the end of the log (without lines about passwords or tokens). */
static void bug_info(Text* t, const char* date, uint64_t back) {
#ifdef GBADS_VERSION
	text_add(t, "GBA DS %s bug report, %s\n\n", GBADS_VERSION, date);
#endif
	text_add(t, "== Device\n");
	char line[256];
#ifdef __ANDROID__
	char v[PROP_VALUE_MAX];
	__system_property_get("ro.product.manufacturer", v);
	text_add(t, "Device: %s", v);
	__system_property_get("ro.product.model", v);
	text_add(t, " %s\n", v);
	__system_property_get("ro.build.version.release", v);
	text_add(t, "Android %s", v);
	__system_property_get("ro.build.version.sdk", v);
	text_add(t, " (API %s)\n", v);
	__system_property_get("ro.build.display.id", v);
	text_add(t, "Build: %s\n", v);
#else
	read_line("/sys/firmware/devicetree/base/model", line, sizeof(line));
	if (line[0]) text_add(t, "Device: %s\n", line);
	FILE* f = fopen("/etc/os-release", "r");
	while (f && fgets(line, sizeof(line), f)) {
		if (!strncmp(line, "PRETTY_NAME=", 12) || !strncmp(line, "VERSION=", 8) || !strncmp(line, "BUILD_ID=", 9)) text_add(t, "os-release %s", line);
	}
	if (f) fclose(f);
	if (file_exists("/etc/banana")) text_add(t, "BananaOS (/etc/banana)\n");
#endif
	struct utsname u;
	if (uname(&u) == 0) text_add(t, "Kernel: %s %s %s\n", u.sysname, u.release, u.machine);
	text_add(t, "SDL video %s, audio %s\n", SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "?",
	         SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "?");
	for (int i = 0; i < nScreens; ++i) {
		text_add(t, "Screen %d: %dx%d%s%s\n", i, screens[i].rect.w, screens[i].rect.h, i == ui_touch_screen() ? ", touch" : "",
		         i == game_screen() ? ", game" : "");
	}
	int pct = battery_percent();
	if (pct >= 0) text_add(t, "Battery: %d%%\n", pct);

	text_add(t, "\n== Game\n");
	const MapProfile* prof = profile_find(subCode);
	text_add(t, "ROM: %s (%s%s%s), %s\n", gp.base, emu.gameCode, strcmp(subCode, emu.gameCode) ? ", playing " : "",
	         strcmp(subCode, emu.gameCode) ? subCode : "", prof ? prof->title : "no profile");
	{
		char d[640];
		text_add(t, "ROM dump: %s\n", dump_text(d, sizeof(d)));
	}
	text_add(t, "Running %.1f minutes (%llu frames), %.1f FPS (%.0f%%) when the report was made\n", SDL_GetTicks() / 60000.0,
	         (unsigned long long) frameCount, fps, speedPct);
	text_add(t, "Second screen: %s\n", combatOn ? "logo (a fight)" : hudShown ? "HUD" : mapShownIdx >= 0 ? "map / status picture" : logoOnly ? "logo (logo-only game)" : "logo");
	if (mc) {
		const char* why = NULL;
		int fails = mapcap_failures(mc, &why);
		if (fails) text_add(t, "Last capture: failed (%d in a row): %s\n", fails, why ? why : "?");
		else text_add(t, "Last capture: ok\n");
	}
	text_add(t, "Controls: %s. Cheats on: %d. RetroAchievements: %s\n", ctlProf ? ctl_name(ctlMode) : "GBA", cheats_on(&cheats),
	         ra_enabled() ? "signed in" : "off");
	if (sunHere) text_add(t, "Sun: %d of 10\n", sunLevel);
	text_add(t, "The rewound state is %llu frames before the other\n", (unsigned long long) back);
	/* (issue #39: slots 0-9 and the resume point shared with RetroArch, and where) */
	if (gp.raDir[0])
		text_add(t, "Save states: shared with RetroArch in %s (%s, %s); slot %d\n", gp.raDir, cfg->raStateDir[0] ? "chosen by hand" : "from its settings",
		         gp.raCompress ? "compressed" : "uncompressed", stateSlot);
	else
		text_add(t, "Save states: GBA DS's own%s; slot %d\n", !strcmp(cfg->stateFolder, "retroarch") ? " (shared with RetroArch, but its folder wasn't found)" : "",
		         stateSlot);

	text_add(t, "\n== Settings\n");
	text_add(t, "layout %s, swap %d, scale %s, map_scale %s, shader %s, color_correction %d, lcd_ghosting %d, smooth %d, overlays %d (screens %d, logo %d, %s / %s), border_color %s\n", cfg->layout,
	         cfg->swap, cfg->scale, cfg->mapScale, shaderNames[cfg->shader], cfg->colorCorrection, cfg->ghosting, cfg->smooth, cfg->overlayMode, cfg->overlays, cfg->bezelLogo, cfg->bezelInner, cfg->bezelOuter, cfg->borderColor);
	text_add(t, "map %d, map_interval %d, pinball_live %d, toggle %d, auto_rotate %d, toggle_button %s\n", cfg->map, cfg->mapInterval, cfg->tallLive,
	         cfg->toggle, cfg->autoRotate, cfg->toggleButton);
	text_add(t, "logo_folder %s, custom_logos %s; logo: %s\n", cfg->logoFolder[0] ? cfg->logoFolder : "(not set)",
	         cfg->customLogos == CL_OFF ? "off" : cfg->customLogos == CL_FULL ? "full" : "pixelated", ui_logo_note());
	text_add(t, "frame_buffer %d, match_display %d, vsync %d, audio_latency %d, rewind %d (%d s), fast_forward %d, ff_speed %d\n", cfg->frameBuffer,
	         cfg->matchDisplay, cfg->vsync, cfg->audioLatency, cfg->rewind, cfg->rewindSeconds, cfg->fastForward, cfg->ffSpeed);
	text_add(t, "hotkey %s, menu_combo %s, home_button %d, ab_swap %d, touch_map %s, touch_screen %s\n", cfg->hotkey, cfg->menuCombo,
	         cfg->homeButton, cfg->abSwap, cfg->touchMap, cfg->touchScreen);
	text_add(t, "hotkeys: %s +", hotkeyBtnNames[hkMod]);
	for (int i = 0; i < HK_COUNT; ++i) text_add(t, " %s %s%s", hotkeyNames[i], hkBtn[i] >= 0 ? hotkeyBtnNames[hkBtn[i]] : "none", i + 1 < HK_COUNT ? "," : "\n");
	text_add(t, "save_on_quit %d, auto_load %d, quick_bar_pinned %d\n", cfg->autoSave, cfg->autoLoad, cfg->barPinned);
	text_add(t, "buttons (this game): %s%s\n", fb_describe(), fbSet ? "" : " (the default)");

	char lp[1024];
	log_path(lp, sizeof(lp));
	text_add(t, "\n== The end of the log%s%s\n", lp[0] ? ", " : " (not found)", lp);
	fflush(stderr);
	FILE* lf = lp[0] ? fopen(lp, "rb") : NULL;
	if (!lf) return;
	fseek(lf, 0, SEEK_END);
	long n = ftell(lf), keep = 48 * 1024;
	fseek(lf, n > keep ? n - keep : 0, SEEK_SET);
	if (n > keep && !fgets(line, sizeof(line), lf)) line[0] = 0; /* (from the next whole line) */
	char big[1024];
	while (fgets(big, sizeof(big), lf)) {
		if (strstr(big, "password") || strstr(big, "token")) continue;
		text_add(t, "%s", big);
	}
	fclose(lf);
}

/* Writes the report; out gets its file name (or why it failed). */
/* a report's files, gathered when the button was pressed, until it's zipped (bug_finish) */
static struct {
	bool pending;
	int nf;
	uint64_t back;
	char names[8][340], paths[8][1500], zip[1500], base[300];
} bugPend;

static bool bug_report(char* out, size_t outSize) {
	if (bugPend.pending) {
		char o[200];
		bug_finish("", o, sizeof(o)); /* (one still waiting for its note: saved as it is) */
	}
	/* the state closest to 10 seconds back (the oldest there is, if the log's younger) */
	int pick = -1;
	for (int i = 0; i < BUG_SNAPS; ++i) {
		if (!bugSnap[i].have) continue;
		if (pick < 0) {
			pick = i;
			continue;
		}
		bool fits = bugSnap[i].frame + BUG_BACK <= bugFrame, pickFits = bugSnap[pick].frame + BUG_BACK <= bugFrame;
		if (fits && (!pickFits || bugSnap[i].frame > bugSnap[pick].frame)) pick = i;
		else if (!fits && !pickFits && bugSnap[i].frame < bugSnap[pick].frame) pick = i;
	}
	if (pick < 0 || bugFrame - bugSnap[pick].frame >= BUG_LOG) {
		snprintf(out, outSize, tr("Nothing to report yet."));
		return false;
	}
	uint64_t from = bugSnap[pick].frame, to = bugFrame;

	char dir[1100], stamp[32], base[300], name[4][340], tmp[4][1500], zip[1500];
	snprintf(dir, sizeof(dir), "%s/bug-reports", cfg->assetDir);
	mkdirs(dir);
	write_bug_readme(dir);
	time_t now = time(NULL);
	struct tm tm;
	localtime_r(&now, &tm);
	strftime(stamp, sizeof(stamp), "%Y-%m-%d_%H-%M-%S", &tm);
	/* (a name the harness reads as one word: no spaces) */
	snprintf(base, sizeof(base), "%s-%s", gp.base, stamp);
	for (char* c = base; *c; ++c) {
		if (!((*c >= 'a' && *c <= 'z') || (*c >= 'A' && *c <= 'Z') || (*c >= '0' && *c <= '9') || *c == '-' || *c == '_' || *c == '.')) *c = '_';
	}
	snprintf(name[0], sizeof(name[0]), "%s.state", base);
	snprintf(name[1], sizeof(name[1]), "%s-rewound.state", base);
	snprintf(name[2], sizeof(name[2]), "%s-keys.txt", base);
	snprintf(name[3], sizeof(name[3]), "%s-info.txt", base);
	for (int i = 0; i < 4; ++i) snprintf(tmp[i], sizeof(tmp[i]), "%s/%s", dir, name[i]);
	snprintf(zip, sizeof(zip), "%s/%s.zip", dir, base);

	/* the game now; then the state from back then, written the same way, and the game put back */
	bool ok = state_file_save(tmp[0]);
	size_t sz = emu_state_size(&emu);
	void* cur = malloc(sz);
	if (ok && cur && emu_state_save(&emu, cur)) {
		ok = emu_state_load(&emu, bugSnap[pick].st) && emu_state_save_file(&emu, tmp[1]);
		emu_state_load(&emu, cur);
	} else {
		ok = false;
	}
	free(cur);

	/* the keys: presses and lifts to read, then the frames to play back */
	Text t = { 0 };
	double fps60 = GBA_FPS;
	char date[64], k[64];
	strftime(date, sizeof(date), "%Y-%m-%d %H:%M:%S", &tm);
#ifdef GBADS_VERSION
	const char* version = GBADS_VERSION;
#else
	const char* version = "?";
#endif
	text_add(&t, "# GBA DS bug report, %s\n", date);
	text_add(&t, "# GBA DS %s, %s (%s%s%s)\n", version, gp.base, emu.gameCode, strcmp(subCode, emu.gameCode) ? ", playing " : "",
	         strcmp(subCode, emu.gameCode) ? subCode : "");
	text_add(&t, "# Controls: %s. Cheats on: %d.\n", ctlProf ? ctl_name(ctlMode) : "GBA", cheats_on(&cheats));
	text_add(&t, "# %s is the game %.1f seconds (%llu frames) before %s.\n", name[1], (to - from) / fps60,
	         (unsigned long long) (to - from), name[0]);
	text_add(&t, "# The keys the game got from then on (after GBA DS's button layouts), by frame:\n#\n");
	text_add(&t, "#  frame    time  event\n");
	unsigned prev = 0;
	uint64_t since[10] = { 0 };
	for (uint64_t f = from; f <= to; ++f) {
		unsigned cur = f < to ? bugKeys[f & (BUG_LOG - 1)] : prev; /* (at the end: the keys still held) */
		unsigned on = cur & ~prev, off = prev & ~cur;
		for (int b = 0; b < 10; ++b) {
			keys_name(k, sizeof(k), 1u << b);
			if (off & (1u << b)) {
				text_add(&t, "# %6llu %6.2fs  lift %s (held %llu frames)\n", (unsigned long long) (f - from), (f - from) / fps60, k,
				         (unsigned long long) (f - since[b]));
			}
		}
		for (int b = 0; b < 10; ++b) {
			keys_name(k, sizeof(k), 1u << b);
			if (on & (1u << b)) {
				text_add(&t, "# %6llu %6.2fs  %s %s\n", (unsigned long long) (f - from), (f - from) / fps60, f == from ? "already holding" : "press", k);
				since[b] = f;
			}
		}
		if (f < to && bugPoke[f & (BUG_LOG - 1)]) {
			text_add(&t, "# %6llu %6.2fs  camera pan %s (SNES layout, L / R)\n", (unsigned long long) (f - from), (f - from) / fps60,
			         bugPoke[f & (BUG_LOG - 1)] == 2 ? "left" : "right");
		}
		prev = cur;
	}
	for (int b = 0; b < 10; ++b) {
		if (prev & (1u << b)) {
			keys_name(k, sizeof(k), 1u << b);
			text_add(&t, "# %6llu %6.2fs  still holding %s (for %llu frames)\n", (unsigned long long) (to - from), (to - from) / fps60, k,
			         (unsigned long long) (to - since[b]));
		}
	}
	text_add(&t, "#\n# To play it back: build/host/harness ROM OUTDIR %s, run in the folder this\n", name[2]);
	text_add(&t, "# report was unzipped to; it ends where %s is.\n", name[0]);
	text_add(&t, "load ./%s\n", name[1]);
	for (uint64_t f = from; f < to;) {
		unsigned kf = bugKeys[f & (BUG_LOG - 1)];
		uint64_t e = f + 1;
		while (e < to && bugKeys[e & (BUG_LOG - 1)] == kf && !bugPoke[e & (BUG_LOG - 1)]) ++e;
		if (bugPoke[f & (BUG_LOG - 1)] && ctlProf) {
			text_add(&t, "poke 0x%08X 1 %u\n", (unsigned) ctlProf->controls.snes.pan, bugPoke[f & (BUG_LOG - 1)]);
		}
		keys_name(k, sizeof(k), kf);
		text_add(&t, "hold %s %llu\n", k, (unsigned long long) (e - f));
		f = e;
	}
	if (t.s) {
		FILE* f = fopen(tmp[2], "wb");
		if (!f || fwrite(t.s, 1, t.n, f) != t.n) ok = false;
		if (f) fclose(f);
	} else {
		ok = false;
	}
	free(t.s);

	/* the device, settings and log */
	Text info = { 0 };
	bug_info(&info, date, to - from);
	FILE* inf = info.s ? fopen(tmp[3], "wb") : NULL;
	if (!inf || fwrite(info.s, 1, info.n, inf) != info.n) ok = false;
	if (inf) fclose(inf);
	free(info.s);

	/* the live view (profile tall): the two screens as shown and each
	 * copy's own picture side by side (-tall.png), its last 10 seconds a
	 * line a frame (-tall.txt), and the copies now (-copyN.state) */
	int nf = 4;
	char xname[4][340], xtmp[4][1500];
	if (ok && tallLive && twinOpen) {
		snprintf(xname[0], sizeof(xname[0]), "%s-tall.png", base);
		snprintf(xname[1], sizeof(xname[1]), "%s-tall.txt", base);
		snprintf(xname[2], sizeof(xname[2]), "%s-copy0.state", base);
		snprintf(xname[3], sizeof(xname[3]), "%s-copy1.state", base);
		for (int i = 0; i < 4; ++i) snprintf(xtmp[i], sizeof(xtmp[i]), "%s/%s", dir, xname[i]);
		static uint32_t sheet[GBA_W * 3 * GBA_H * 2];
		for (int y = 0; y < 2 * GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t* row = &sheet[y * GBA_W * 3];
				row[x] = (y < GBA_H ? tallTop[y * GBA_W + x] : tallBot[(y - GBA_H) * GBA_W + x]) | 0xFF000000;
				row[GBA_W + x] = y < GBA_H ? twins[0].pic[y * GBA_W + x] | 0xFF000000 : 0xFF000000;
				row[2 * GBA_W + x] = y < GBA_H ? twins[1].pic[y * GBA_W + x] | 0xFF000000 : 0xFF000000;
			}
		}
		bool xok = stbi_write_png(xtmp[0], GBA_W * 3, GBA_H * 2, 4, sheet, GBA_W * 3 * 4) != 0;
		FILE* f = fopen(xtmp[1], "wb");
		if (f) {
			fprintf(f, "# GBA DS live view, the last %u frames (top/bottom screen and the copies' rows; game.c update_tall)\n",
			        tallLogN < TALL_LOG ? tallLogN : TALL_LOG);
			for (unsigned i = tallLogN > TALL_LOG ? tallLogN - TALL_LOG : 0; i < tallLogN; ++i) fprintf(f, "%s\n", tallLog[i % TALL_LOG]);
			fclose(f);
		}
		xok = xok && f && emu_state_save_file(&twins[0].e, xtmp[2]) && emu_state_save_file(&twins[1].e, xtmp[3]);
		if (xok) nf = 8;
		else for (int i = 0; i < 4; ++i) remove(xtmp[i]);
	}

	/* kept for bug_finish, which zips them with the player's note */
	const char* names[8] = { name[0], name[1], name[2], name[3], xname[0], xname[1], xname[2], xname[3] };
	const char* paths[8] = { tmp[0], tmp[1], tmp[2], tmp[3], xtmp[0], xtmp[1], xtmp[2], xtmp[3] };
	if (!ok) {
		for (int i = 0; i < nf; ++i) remove(paths[i]);
		fprintf(stderr, "bug report: %s failed\n", zip);
		snprintf(out, outSize, tr("Couldn't save the bug report."));
		return false;
	}
	bugPend.pending = true;
	bugPend.nf = nf;
	bugPend.back = to - from;
	for (int i = 0; i < nf; ++i) {
		snprintf(bugPend.names[i], sizeof(bugPend.names[i]), "%s", names[i]);
		snprintf(bugPend.paths[i], sizeof(bugPend.paths[i]), "%s", paths[i]);
	}
	snprintf(bugPend.zip, sizeof(bugPend.zip), "%s", zip);
	snprintf(bugPend.base, sizeof(bugPend.base), "%s", base);
	snprintf(out, outSize, "bug-reports/%s.zip", base);
	return true;
}

/* The report's files into one .zip, with the player's note (if any) at the
 * top of its info file. */
static bool bug_finish(const char* note, char* out, size_t outSize) {
	if (!bugPend.pending) {
		snprintf(out, outSize, tr("Couldn't save the bug report."));
		return false;
	}
	bugPend.pending = false;
	bool ok = true;
	uint8_t* data[8] = { 0 };
	size_t sizes[8] = { 0 };
	const char* names[8];
	for (int i = 0; i < bugPend.nf; ++i) {
		names[i] = bugPend.names[i];
		ok = ok && (data[i] = read_whole(bugPend.paths[i], &sizes[i])) != NULL;
	}
	/* (the info file is the fourth: the note after its first line) */
	if (ok && note && note[0]) {
		const char* nl = memchr(data[3], '\n', sizes[3]);
		size_t head = nl ? (size_t) (nl - (const char*) data[3]) + 1 : 0;
		Text t = { 0 };
		text_add(&t, "%.*s\n== Note from the player\n%s\n", (int) head, (const char*) data[3], note);
		if (t.s) {
			size_t n = t.n + sizes[3] - head;
			uint8_t* m = malloc(n);
			if (m) {
				memcpy(m, t.s, t.n);
				memcpy(m + t.n, data[3] + head, sizes[3] - head);
				free(data[3]), data[3] = m, sizes[3] = n;
			}
			free(t.s);
		}
	}
	if (ok) ok = zip_write(bugPend.zip, names, data, sizes, bugPend.nf);
	for (int i = 0; i < bugPend.nf; ++i) free(data[i]), remove(bugPend.paths[i]);
	if (!ok) remove(bugPend.zip);
	fprintf(stderr, "bug report: %s %s (%llu frames back%s)\n", bugPend.zip, ok ? "saved" : "failed", (unsigned long long) bugPend.back,
	        note && note[0] ? ", with a note" : "");
	if (ok) snprintf(out, outSize, "bug-reports/%s.zip", bugPend.base);
	else snprintf(out, outSize, tr("Couldn't save the bug report."));
	return ok;
}

static void note_open(bool fromBar) {
	noteOpen = true;
	noteFromBar = fromBar;
	noteLen = 0, noteText[0] = 0;
	noteShift = true; /* (a capital first) */
	noteRow = 1, noteCol = 0;
	notePressKey = -1;
	menuNote[0] = 0;
}
/* Done (keep: with what's typed) or Skip: the report saved; back to the
 * menu, or (from the quick bar) to the game */
static void note_done(bool keep) {
	char out[200];
	bool ok = bug_finish(keep ? noteText : "", out, sizeof(out));
	noteOpen = false;
	if (noteFromBar) {
		menuOpen = false;
		ui_osd(ok ? "Bug report saved in bug-reports." : "%s", out);
	} else {
		snprintf(menuNote, sizeof(menuNote), "%s", ok ? tr("Bug report saved in bug-reports"): out);
	}
}

static void auto_save(void) {
	if (!cfg->autoSave) return;
	if (state_save(gp.autoState)) {
		save_thumbs();
		fprintf(stderr, "auto-saved %s\n", gp.autoState);
	} else {
		fprintf(stderr, "auto-save failed\n");
	}
}

/* The app is being put in the background (Android may kill it there):
 * keep a resume point. Called between frames, from ui_events. */
static void on_background(void) {
	auto_save();
	if (bugPend.pending) {
		/* (Android may kill the app there: a report waiting for its note saved with what's typed) */
		char out[200];
		bug_finish(noteOpen ? noteText : "", out, sizeof(out));
		if (noteOpen) noteOpen = false, menuNote[0] = 0;
	}
}

static void open_menu(void);

/* Back from Home / the app switcher: paused, rather than straight into the action. */
static void on_foreground(void) {
	if (!menuOpen) open_menu();
}



static const uint32_t* map_px(int idx) {
	return idx == STILL_IDX ? (stillValid ? stillPx : NULL) : mapRes.frames[idx];
}

/* hud.alt.shade: a pixel darkened (to a third) */
static uint32_t shade_px(uint32_t c) {
	return (c & 0xFF000000) | (((c >> 2) & 0x3F3F3F) + ((c >> 3) & 0x1F1F1F));
}

/* Profile hud.overMap: the map picture with the HUD's pieces in it (where
 * the picture has the profile's mapClear colour): each piece as last seen
 * (hud_take_pieces). */
static const uint32_t* hud_over_map(const uint32_t* px) {
	static uint32_t* over;
	static int overSize;
	if (!hudProf || (!overOn && !hudProf->hud.logoUnder)) return px;
	const typeof(hudProf->hud)* h = &hudProf->hud;
	if (h->logoUnder && combatProf && !combatMade) make_combat_canvas();
	if (overSize != mapW * mapH) {
		free(over);
		over = malloc((size_t) mapW * mapH * 4);
		overSize = over ? mapW * mapH : 0;
		if (!over) return px;
	}
	uint32_t mc = h->mapClear & 0xFFFFFF;
	/* a piece that goes alone (a conversation): on the plain background,
	 * the others waiting */
	int alone = -1;
	for (int m = 0; m < 4; ++m) {
		if (h->moves[m].alone && pieces[m].valid) alone = m;
	}
	overAlone = alone >= 0;
	memcpy(over, alone >= 0 && h->logoUnder && combatPx ? combatPx : px, (size_t) mapW * mapH * 4);
	int px0[4] = { 0 }, py0[4] = { 0 };
	for (int k = 0; k < 8; ++k) {
		int m = k & 3;
		const HudMove* mv = &h->moves[m];
		const Piece* pc = &pieces[m];
		/* the pieces, then the ones that follow them (over everything) */
		if (!pc->valid || (k < 4) == (mv->follow > 0 || mv->alone)) continue;
		if (alone >= 0 && m != alone) continue;
		int x0 = mv->how == 'C' ? mv->x - (pc->w - 1) / 2 : mv->how == 'R' ? mv->x - pc->w + 1 : mv->x;
		int y0 = mv->vhow == 'M' ? mv->y - (pc->h - 1) / 2 : mv->vhow == 'B' ? mv->y - pc->h + 1 : mv->y;
		if (mv->asIs) {
			x0 = pc->sx + (mapW - GBA_W) / 2, y0 = pc->sy + (mapH - GBA_H) / 2;
			if (mv->asIsBottom && pc->atBottom && !hudAlt) y0 = mapH - pc->h;
		}
		if (mv->wide) {
			/* as wide as the picture: each pixel from the piece's, scaled */
			int sw = pc->w * mapW / GBA_W, sh = pc->h * mapW / GBA_W;
			x0 = pc->sx * mapW / GBA_W;
			for (int y = 0; y < sh; ++y) {
				for (int x = 0; x < sw; ++x) {
					uint32_t c = pc->px[(y * GBA_W / mapW) * pc->w + x * GBA_W / mapW];
					int nx = x0 + x, ny = y0 + y;
					if (!c || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
					over[ny * mapW + nx] = c;
				}
			}
			px0[m] = x0, py0[m] = y0;
			continue;
		}
		if (mv->follow) {
			int a = mv->follow - 1;
			if (!pieces[a].valid) continue;
			x0 = pc->sx + px0[a] - pieces[a].sx;
			y0 = pc->sy + py0[a] - pieces[a].sy;
		}
		px0[m] = x0, py0[m] = y0;
		uint32_t bc = mv->box ? (mv->box & 0xFF) << 16 | (mv->box & 0xFF00) | (mv->box >> 16 & 0xFF) : 0x1000000;
		if (mv->box) {
			/* the box goes round the piece */
			for (int y = y0 - mv->pad; y < y0 + pc->h + mv->pad; ++y) {
				for (int x = x0 - mv->pad; x < x0 + pc->w + mv->pad; ++x) {
					if (x < 0 || y < 0 || x >= mapW || y >= mapH) continue;
					uint32_t* o = &over[y * mapW + x];
					if ((*o & 0xFFFFFF) == mc) *o = 0xFF000000u | bc;
				}
			}
		}
		bool shade = mv->alone && hudAlt && h->alt.shade;
		if (shade) {
			static uint32_t* orig;
			static int origSize;
			if (origSize != mapW * mapH) free(orig), orig = malloc((size_t) mapW * mapH * 4), origSize = orig ? mapW * mapH : 0;
			if (orig) {
				/* the black (the game screen's, stretched over the whole
				 * picture) darkens; the piece's other pixels (the lettering)
				 * go where it is */
				memcpy(orig, over, (size_t) mapW * mapH * 4);
				for (int ny = 0; ny < mapH; ++ny) {
					int ly = ny * GBA_H / mapH - pc->sy;
					for (int nx = 0; nx < mapW; ++nx) {
						int lx = nx * GBA_W / mapW - pc->sx;
						uint32_t c = lx >= 0 && ly >= 0 && lx < pc->w && ly < pc->h ? pc->px[ly * pc->w + lx] : 0;
						if (c && (c & 0xFFFFFF) == 0) over[ny * mapW + nx] = shade_px(orig[ny * mapW + nx]);
					}
				}
				for (int y = 0; y < pc->h; ++y) {
					for (int x = 0; x < pc->w; ++x) {
						uint32_t c = pc->px[y * pc->w + x];
						int nx = x0 + x, ny = y0 + y;
						if (!c || (c & 0xFFFFFF) == 0 || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
						over[ny * mapW + nx] = c;
					}
				}
				continue;
			}
		}
		for (int y = 0; y < pc->h; ++y) {
			for (int x = 0; x < pc->w; ++x) {
				uint32_t c = pc->px[y * pc->w + x];
				int nx = x0 + x, ny = y0 + y;
				if (!c || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
				uint32_t* o = &over[ny * mapW + nx];
				if (mv->follow || mv->alone || mv->top || (*o & 0xFFFFFF) == mc || (*o & 0xFFFFFF) == bc) *o = c;
			}
		}
	}
	if (h->logoUnder && combatPx) {
		/* the logo behind: where nothing else is: last */
		for (int i = 0; i < mapW * mapH; ++i) {
			if ((over[i] & 0xFFFFFF) == mc) over[i] = combatPx[i];
		}
	}
	return over;
}

static void upload_map_frame(int idx) {
	const uint32_t* px = map_px(idx);
	if ((idx == mapShownIdx && !overDirty) || !px) return;
	overDirty = false;
	px = hud_over_map(px);
	if (fadeLevel < 256) {
		/* (a turn that waits, GEN-14: only the picture fades, so the
		 * shader and the bezel stay as they are) */
		static uint32_t* fadeBuf;
		static size_t fadeN;
		size_t n = (size_t) mapW * mapH;
		if (n > fadeN) fadeBuf = realloc(fadeBuf, n * 4), fadeN = fadeBuf ? n : 0;
		if (fadeBuf) {
			for (size_t i = 0; i < n; ++i) {
				uint32_t c = px[i];
				uint32_t r = (c & 0xFF) * (uint32_t) fadeLevel >> 8, g = ((c >> 8) & 0xFF) * (uint32_t) fadeLevel >> 8, b = ((c >> 16) & 0xFF) * (uint32_t) fadeLevel >> 8;
				fadeBuf[i] = (c & 0xFF000000u) | r | g << 8 | b << 16;
			}
			px = fadeBuf;
		}
	}
	for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, mapW * 4);
	mapShownIdx = idx;
}

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
static void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
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
			x0 = SDL_min(x0, px), x1 = SDL_max(x1, px), y0 = SDL_min(y0, py), y1 = SDL_max(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = SDL_max(x0 - 2, 0), y0 = SDL_max(y0 - 2, 0), x1 = SDL_min(x1 + 2, w - 1), y1 = SDL_min(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) SDL_arraysize(sample); ++x) {
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
				else if (red) a += SDL_max(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
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

/* After a fresh capture: rebuild the still map. A capture that only differs
 * from the one on screen where the markers animate (a pulse caught at
 * another point of its loop) keeps the old still, so it doesn't jump around
 * every time the map is refreshed. Returns true if the still changed. */
static bool stillStale; /* stillPx is from an older map (built only when it's shown) */
/* The map animates, unless the game's is always still */
static bool map_anim(void) {
	const MapProfile* p = mc ? mapcap_profile(mc) : NULL;
	return !(p && p->mapStill);
}

static bool update_still(void) {
	int w = mapRes.w, h = mapRes.h, wh = w * h;
	uint32_t* px = malloc((size_t) wh * 4);
	unsigned char* mask = malloc((size_t) wh);
	if (!px || !mask) {
		free(px);
		free(mask);
		return false;
	}
	const MapProfile* prof = mc ? mapcap_profile(mc) : NULL;
	compose_still(&mapRes, prof ? (int) prof->stillPick : STILL_BRIGHT, px, mask);
	if (stillValid && stillW == w && stillH == h) {
		bool same = true;
		for (int i = 0; i < wh && same; ++i) same = px[i] == stillPx[i] || mask[i];
		if (same) {
			free(px);
			free(mask);
			return false;
		}
	}
	free(stillPx);
	free(mask);
	stillPx = px, stillW = w, stillH = h, stillValid = true;
	return true;
}

/* A new map picture in mapRes (a capture; or one kept with a save state,
 * `fresh` false: the level name above the HUD isn't taken from it, it's
 * kept on its own) */
static void toggle_new_subject(void);
static void map_show_new(bool fresh) {
	mapInactive = false;
	if (!fresh || capSubject) capSubject = false, toggle_new_subject();
	if (fresh && hudProf && hudProf->hud.held.over.x1 && mapRes.frames[0] && mapRes.w * mapRes.h <= GBA_W * GBA_H) {
		/* hud.held.over: the number, laid over the held picture */
		heldNumW = mapRes.w, heldNumH = mapRes.h;
		memcpy(heldNum, mapRes.frames[0], (size_t) mapRes.w * mapRes.h * 4);
		hudLogoBelowDone = false;
	}
	if (fresh && hudProf && hudProf->hud.mapTitleRows && mapRes.w == GBA_W && mapRes.frames[0]) {
		/* the level list's title row (SMA-7), held for the level */
		if (!mapTitleN) hudLogoBelowDone = false; /* (the logo moves down under it) */
		mapTitleN = SDL_min(hudProf->hud.mapTitleRows, 32);
		for (int i = 0; i < GBA_W * mapTitleN; ++i) mapTitle[i] = mapRes.frames[0][i] | 0xFF000000;
	}
	mapShownIdx = -1;
	/* (pages, a few seconds each: Fire Emblem's unit status; they
	 * keep turning at their pace through new captures) */
	bool pages = mapRes.pages;
	if (!pages) mapAnimStart = frameCount;
	/* the still (each marker frozen) is only shown with map animations
	 * off, and building it is real work: only then */
	bool stillChanged = false;
	stillStale = map_anim();
	if (!stillStale) stillChanged = update_still();
	int first = pages ? second_page() : 0;
	upload_map_frame(map_anim() ? first : STILL_IDX);
	if (debug) fprintf(stderr, "frame %llu: map %s (%d frame(s) %d apart%s)\n", (unsigned long long) frameCount, fresh ? "updated" : "from the save state",
	                   mapRes.nFrames, mapRes.frameStep, stillStale ? "" : stillChanged ? ", still changed" : ", still unchanged");
}


/* a profile's own live picture is up (liveView): drawn as it is */
static bool liveOn;

/* ---- the contextual toggle (GEN-14, issue #37). Turning the second screen
 * by hand: this is the one gate (toggle_ready: the button's active state
 * and the hotkey both read it) and the one state machine. A page is always
 * held, so it is shown at once (a cut); a turn that has to wait fades the
 * picture to black (5 frames), holds on black until the next picture is
 * ready (2 s at most, then the old picture comes back and nothing
 * changes), and fades back (5 frames). Presses outside IDLE are ignored,
 * not queued. Step 2 turns pages only; views are for the profiles (step 4),
 * and GBADS_TOGGLE_WAIT=N (frames, for tests) makes a page turn wait too. */
enum { TG_IDLE, TG_FADE_OUT, TG_HOLD, TG_FADE_IN };
#define TG_FADE_FRAMES 5
#define TG_HOLD_MAX 120
#define TG_HOLD_PRESS_MAX 600 /* (capture on press: a capture can take seconds) */
static int tgState, tgFrames, tgTarget, tgWait = -1;

/* a press turns the view (captured on the press) rather than a page: the profile's views, unless its picture is pages (Mother 3's Status where there's no map) and the other view isn't up */
static bool tg_view_mode(const MapProfile* hp) {
	if (!hp || !hp->onPress) return false;
	const bool paged = mapRes.pages && mapRes.nFrames >= 2;
	if (!paged) return true;
	/* the picture is pages: they turn as pages do, and from the other view's last page a press brings the game's own view back (map, Status p1, p2, p3, map) */
	return viewAlt && second_page() == mapRes.nFrames - 1;
}

static bool toggle_ready(void) {
	if (!cfg->toggle || tgState != TG_IDLE) return false;
	const MapProfile* hp = mc ? mapcap_profile(mc) : NULL;
	if (tg_view_mode(hp)) {
		/* the other view is captured on the press: ready when there is one here (or this is it, and the game's own is to come back), a picture is up and the hidden copy is free */
		if (mapShownIdx < 0 || logoForced || liveOn || mapInactive || pressCap || (capSubject && mapcap_busy(mc))) return false; /* (a refresh still running doesn't count: a press calls it off) */
		return viewAlt || !hp->altHere || hp->altHere(&emu);
	}
	if (!mapRes.pages || mapRes.nFrames < 2 || mapShownIdx < 0 || !map_anim()) return false; /* (one picture, the logo, Map Animations off) */
	if (logoForced || liveOn || mapInactive) return false;
	if (hp && hp->holdPage && hp->holdPage(&emu) >= 0) return false; /* (a page kept up: Pokémon's Party in a battle) */
	if (capSubject && mapcap_busy(mc)) return false; /* (the next subject's pictures aren't here yet: GEN-6) */
	return true;
}

/* the page shows, the timer (auto-rotate) starting again from it */
static void toggle_apply(int page) {
	if (cfg->autoRotate) mapAnimStart = frameCount;
	pageOffset = page;
	overDirty = true;
	if (debug) fprintf(stderr, "frame %llu: toggle: page %d of %d\n", (unsigned long long) frameCount, page + 1, mapRes.nFrames);
}

static void toggle_press(void) {
	if (!toggle_ready()) {
		if (debug) fprintf(stderr, "frame %llu: toggle: not ready, ignored\n", (unsigned long long) frameCount);
		return;
	}
	if (tgWait < 0) {
		const char* e = getenv("GBADS_TOGGLE_WAIT");
		tgWait = e ? SDL_max(0, atoi(e)) : 0;
	}
	const MapProfile* pp = mc ? mapcap_profile(mc) : NULL;
	if (tg_view_mode(pp)) {
		/* capture on press: the other view is asked for now; the picture fades out, stays black until it is here (or it isn't, in time: the old one comes back) */
		viewAlt = !viewAlt, viewWait = true, tgCapWant = 0;
		if (mc && mapcap_busy(mc)) mapcap_cancel(mc); /* (a refresh of the old view: the new one starts as soon as it has stopped) */
		tgState = TG_FADE_OUT, tgFrames = 0, tgTarget = -1;
		if (debug) fprintf(stderr, "frame %llu: toggle: view %s asked for\n", (unsigned long long) frameCount, viewAlt ? "2" : "1");
		return;
	}
	int next = (second_page() + 1) % mapRes.nFrames;
	if (tgWait > 0) tgState = TG_FADE_OUT, tgFrames = 0, tgTarget = next;
	else toggle_apply(next);
}

/* a new subject (a unit, a room, a loaded state): the pages start again at
 * the first; a turn under way ends, no fade. Nothing at all with the toggle
 * off: the pages go on turning through new captures as they always did. */
static void toggle_new_subject(void) {
	if (tgState != TG_IDLE) tgState = TG_IDLE, fadeLevel = 256, overDirty = true;
	if (!cfg->toggle) return;
	pageOffset = 0;
	if (mapRes.pages) mapAnimStart = frameCount;
}

/* each frame */
static void toggle_step(void) {
	if (!cfg->toggle && viewAlt) viewAlt = false, pressCap = true; /* (switched off while the other view was up: the game's own comes back) */
	if (!cfg->toggle && tgState != TG_IDLE) tgState = TG_IDLE, fadeLevel = 256, overDirty = true; /* (switched off meanwhile) */
	if (tgState == TG_IDLE) return;
	overDirty = true, fadeUpload = true; /* (the picture is drawn again each frame of a turn) */
	++tgFrames;
	switch (tgState) {
	case TG_FADE_OUT:
		fadeLevel = 256 * (TG_FADE_FRAMES - tgFrames) / TG_FADE_FRAMES;
		if (tgFrames >= TG_FADE_FRAMES) tgState = TG_HOLD, tgFrames = 0, fadeLevel = 0, pressCap = viewWait; /* (capture on press: asked for now that the old view is dark) */
		break;
	case TG_HOLD:
		if (tgTarget < 0) {
			/* (capture on press) the view is in: fade up; not in time: the old one stays, and the capture is called off */
			if (tgCapWant && capDone >= tgCapWant) {
				tgState = TG_FADE_IN, tgFrames = 0, viewWait = false;
				if (debug) fprintf(stderr, "frame %llu: toggle: view %s\n", (unsigned long long) frameCount, viewAlt ? "2" : "1");
			} else if (tgFrames >= TG_HOLD_PRESS_MAX) {
				viewAlt = !viewAlt, pressCap = false, viewWait = false, tgCapWant = 0;
				if (mc && mapcap_busy(mc)) mapcap_cancel(mc);
				tgState = TG_FADE_IN, tgFrames = 0;
				if (debug) fprintf(stderr, "frame %llu: toggle: the view didn't come in time, the old one stays\n", (unsigned long long) frameCount);
			}
			break;
		}
		if (tgFrames >= tgWait) {
			toggle_apply(tgTarget);
			tgState = TG_FADE_IN, tgFrames = 0;
		} else if (tgFrames >= TG_HOLD_MAX) {
			tgState = TG_FADE_IN, tgFrames = 0; /* (not found in time: the old picture comes back) */
		}
		break;
	case TG_FADE_IN:
		fadeLevel = 256 * tgFrames / TG_FADE_FRAMES;
		if (tgFrames >= TG_FADE_FRAMES) tgState = TG_IDLE, fadeLevel = 256;
		break;
	}
}

/* ---- the toggle button (GEN-14): a key in the quick bar's look, drawn at
 * the second screen's own pixel size on a canvas a little taller than the
 * picture, so it can sit in the margin under it. Hotkey-only on a screen
 * without touch. Hidden while the bar or a menu is up, and in games with
 * nothing to turn. */
#define TB_W 30
#define TB_H 13
#define TB_ROWS (GBA_H + 16)
static uint32_t tbPx[GBA_W * TB_ROWS];
static SDL_Texture* tbTex[2];
static SDL_Renderer* tbTexRen[2];
static bool tbDown; /* a finger is on it */
static int tbGX, tbGY;
static const char* const icon_toggle[] = {
	"...X.......X...",
	"..XX.......XX..",
	".XXXXXXXXXXXXX.",
	"..XX.......XX..",
	"...X.......X...",
	NULL,
};

/* the screen the button is on, or -1: the second screen, if it takes touch and the game has something to turn */
static int toggle_btn_screen(void) {
	if (!cfg->toggle || !strcmp(cfg->toggleButton, "off") || nScreens < 2 || !mc) return -1;
	const MapProfile* hp = mapcap_profile(mc);
	if (!hp || !(hp->pages || hp->views || hp->onPress)) return -1;
	int s = 1 - game_screen();
	if (s != ui_touch_screen() || !cfg->map) return -1;
	return s;
}

/* where it is: the key in the canvas's pixels (kx, ky), the canvas's size on screen s, and the area a tap counts in (screen-local) */
static void toggle_btn_geom(int s, SDL_Rect* key, SDL_Rect* canvas, SDL_Rect* tap) {
	SDL_Rect pic = bar_pic_full(s);
	float u = (float) pic.w / GBA_W;
	int margin = (int) ((screens[s].rect.h - (pic.y + pic.h)) / u);
	int x = !strcmp(cfg->toggleButton, "right") ? GBA_W - 6 - TB_W : !strcmp(cfg->toggleButton, "center") ? (GBA_W - TB_W) / 2 : 6;
	int y = margin >= TB_H + 3 ? GBA_H + (SDL_min(margin, 16) - TB_H - 1) / 2 : GBA_H - 6 - TB_H - 1;
	*key = (SDL_Rect) { x, y, TB_W, TB_H };
	*canvas = (SDL_Rect) { pic.x, pic.y, pic.w, (int) (u * TB_ROWS + 0.5f) };
	int cx = x + TB_W / 2, cy = y + TB_H / 2;
	int tx = pic.x + (int) ((cx - 32) * u), ty = pic.y + (int) ((cy - 14) * u);
	*tap = (SDL_Rect) { tx, ty, (int) (64 * u), (int) (28 * u) };
	if (tap->x < 0) tap->w += tap->x, tap->x = 0;
	if (tap->y + tap->h > screens[s].rect.h) tap->h = screens[s].rect.h - tap->y;
	if (tap->x + tap->w > screens[s].rect.w) tap->w = screens[s].rect.w - tap->x;
}

static bool toggle_btn_visible(void) {
	return toggle_btn_screen() >= 0 && !menuOpen && !barOpen && barShown <= 0;
}

static bool toggle_btn_hit(int screen, int x, int y) {
	if (!toggle_btn_visible() || screen != toggle_btn_screen()) return false;
	SDL_Rect key, canvas, tap;
	toggle_btn_geom(screen, &key, &canvas, &tap);
	SDL_Point pt = { x, y };
	return SDL_PointInRect(&pt, &tap);
}

static void draw_toggle_btn(void) {
	if (!toggle_btn_visible()) return;
	int s = toggle_btn_screen();
	SDL_Rect key, canvas, tap;
	toggle_btn_geom(s, &key, &canvas, &tap);
	bool ready = toggle_ready();
	bool pressed = tbDown && ready;
	cvBuf = tbPx, cvRows = TB_ROWS;
	memset(tbPx, 0, sizeof(tbPx));
	cv_key(key, pressed ? KEY_SEL : KEY_PLAIN);
	cv_icon(icon_toggle, key.x + key.w / 2, key.y + key.h / 2, !ready ? G_DIS : pressed ? G_LIT : ui_key_ink());
	cvBuf = barPx, cvRows = GBA_H;
	for (int v = 0; v < nViews; ++v) {
		if (tbTex[v] && tbTexRen[v] != views[v].ren) tbTex[v] = NULL;
		if (!tbTex[v]) {
			tbTex[v] = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, TB_ROWS);
			tbTexRen[v] = views[v].ren;
			if (!tbTex[v]) continue;
			SDL_SetTextureBlendMode(tbTex[v], SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(tbTex[v], SDL_ScaleModeNearest);
		}
		SDL_SetTextureAlphaMod(tbTex[v], ready ? 189 : 112); /* 74% (44% when there's nothing to turn right now) */
		ui_update_texture_plain(tbTex[v], tbPx, GBA_W * 4);
	}
	SDL_Rect sr = screens[s].rect, clip = { sr.x, sr.y, sr.w, sr.h };
	ui_clip(s, &clip);
	ui_image(s, tbTex, GBA_W, TB_ROWS, "stretch", &canvas);
	ui_clip(s, NULL);
}

static bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total;
}

static int game_screen(void) {
	if (nScreens < 2) return 0;
	return cfg->swap ? 1 : 0;
}

/* Frame pacing. The GBA runs at 59.73 frames a second; a 60 Hz screen then
 * shows one frame twice about every 3.7 s (a small, regular stutter).
 * "Match Display Rate" runs the game at the display's rate instead when it's
 * within 1.5% (60 Hz: 0.46% faster, sound resampled to match, a pitch change
 * nobody hears), and presents the game's screen in step with the display
 * (vsync), so each frame gets exactly one refresh. */
static double frameSec = 1.0 / GBA_FPS;
static double pacedHz;    /* the display's rate when matched, else 0 */
static double audioSpeed = 1.0; /* emulated time per real time */

/* ---- Keeping the game and its sound going while a frame's present is held
 * up (issue #50). On one player's RG DS Plus (ROCKNIX, a second SD card),
 * presenting a frame stalled for half a second every few seconds: the
 * graphics driver waiting for sway to give a buffer back. Nothing in GBA DS
 * can make the screen take the frame sooner, but the emulator isn't needed
 * for presenting: once a present has taken 1.5 frames, this thread runs the
 * game's frames on time with the last keys held (run_frame, as the frame
 * buffer's worker does, so the bug log keeps every frame's keys) and hands
 * their sound over, until the present returns. The picture freezes for the
 * stall and then carries on from where the game is; the sound doesn't
 * break and the game doesn't fall behind. Not run: RetroAchievements'
 * frame check (main thread only), the rewind buffer's states, the map's
 * room checks; those carry on with the next frame. keep_sound = 0: off. */
#ifndef __wasi__
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, quit, armed, running;
	Uint64 armedAt;
	uint32_t keys;
	int ran;      /* frames run while armed */
	int ahead;    /* frames already emulated ahead when armed (the frame buffer's) */
	double ms;    /* how long the present took, as the keeper saw it */
	SDL_AudioDeviceID audio;
	int outRate, maxQueued;
	SDL_AudioStream* conv;
	int convFrom;
} keeper = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };

/* the frames' sound, converted as the main loop does (not fast-forwarding,
 * not matched: none of its other adjustments apply) */
static void keeper_audio(void) {
	static int16_t in[4096 * 2], out[8192 * 2];
	size_t got;
	while ((got = emu_audio_read(&emu, in, 4096)) > 0) {
		if (!keeper.audio) continue;
		if ((int) (SDL_GetQueuedAudioSize(keeper.audio) / 4) > keeper.maxQueued) continue;
		int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
		if (rate != keeper.convFrom || !keeper.conv) {
			if (keeper.conv) SDL_FreeAudioStream(keeper.conv);
			keeper.conv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, keeper.outRate);
			keeper.convFrom = rate;
		}
		if (!keeper.conv) continue;
		SDL_AudioStreamPut(keeper.conv, in, (int) got * 4);
		int bytes;
		while ((bytes = SDL_AudioStreamGet(keeper.conv, out, sizeof(out))) > 0) {
			int vol = ui_volume();
			if (vol < 100) {
				int gain = vol * vol * 65536 / 10000;
				for (int k = 0; k < bytes / 2; ++k) out[k] = (int16_t) (out[k] * gain >> 16);
			}
			SDL_QueueAudio(keeper.audio, out, bytes);
		}
	}
}

static void* keeper_worker(void* arg) {
	(void) arg;
	const double freq = (double) SDL_GetPerformanceFrequency();
	pthread_mutex_lock(&keeper.lock);
	for (;;) {
		while (!keeper.armed && !keeper.quit) pthread_cond_wait(&keeper.cond, &keeper.lock);
		if (keeper.quit) break;
		Uint64 t0 = keeper.armedAt;
		int n = 0;
		while (keeper.armed && !keeper.quit) {
			/* the first frame once the present has taken 1.5 frames (one more if the
			 * frame buffer's worker already ran the next), then one a frame */
			double due = (1.5 + keeper.ahead + n) * frameSec, now = (double) (SDL_GetPerformanceCounter() - t0) / freq;
			if (now < due) {
				struct timespec ts;
				clock_gettime(CLOCK_REALTIME, &ts);
				long ns = (long) ((due - now) * 1e9);
				ts.tv_sec += (ts.tv_nsec + ns) / 1000000000L;
				ts.tv_nsec = (ts.tv_nsec + ns) % 1000000000L;
				pthread_cond_timedwait(&keeper.cond, &keeper.lock, &ts);
				continue;
			}
			keeper.running = true;
			uint32_t keys = keeper.keys;
			pthread_mutex_unlock(&keeper.lock);
			/* (a frame the frame buffer's worker is running comes first) */
			pthread_mutex_lock(&ahead.lock);
			while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
			pthread_mutex_unlock(&ahead.lock);
			run_frame(keys);
			keeper_audio();
			pthread_mutex_lock(&keeper.lock);
			keeper.running = false;
			++keeper.ran;
			++n;
			pthread_cond_broadcast(&keeper.cond);
		}
	}
	pthread_mutex_unlock(&keeper.lock);
	return NULL;
}

/* Before a present: the keeper may run frames until keeper_disarm. */
static void keeper_arm(uint32_t keys, SDL_AudioDeviceID audio, int outRate, int maxQueued, int ahead) {
	pthread_mutex_lock(&keeper.lock);
	if (!keeper.started) {
		keeper.quit = false;
		keeper.started = pthread_create(&keeper.thread, NULL, keeper_worker, NULL) == 0;
	}
	if (keeper.started) {
		keeper.keys = keys;
		keeper.audio = audio, keeper.outRate = outRate, keeper.maxQueued = maxQueued;
		keeper.armedAt = SDL_GetPerformanceCounter();
		keeper.ran = 0;
		keeper.ahead = ahead;
		keeper.armed = true;
		pthread_cond_broadcast(&keeper.cond);
	}
	pthread_mutex_unlock(&keeper.lock);
}

/* After the present: the emulator is the loop's again; the frames run meanwhile. */
static int keeper_disarm(void) {
	pthread_mutex_lock(&keeper.lock);
	keeper.armed = false;
	pthread_cond_broadcast(&keeper.cond);
	while (keeper.running) pthread_cond_wait(&keeper.cond, &keeper.lock);
	int n = keeper.ran;
	keeper.ran = 0;
	pthread_mutex_unlock(&keeper.lock);
	return n;
}

static void keeper_stop(void) {
	pthread_mutex_lock(&keeper.lock);
	bool started = keeper.started;
	keeper.armed = false;
	keeper.quit = true;
	pthread_cond_broadcast(&keeper.cond);
	pthread_mutex_unlock(&keeper.lock);
	if (started) pthread_join(keeper.thread, NULL);
	keeper.started = false;
	if (keeper.conv) SDL_FreeAudioStream(keeper.conv);
	keeper.conv = NULL, keeper.convFrom = 0;
}
#else
static void keeper_arm(uint32_t keys, SDL_AudioDeviceID audio, int outRate, int maxQueued, int ahead) {
	(void) keys, (void) audio, (void) outRate, (void) maxQueued, (void) ahead;
}
static int keeper_disarm(void) { return 0; }
static void keeper_stop(void) {}
#endif
/* Matched, a frame that misses its refresh waits for the next one, and the
 * time is never made up: the game slows down (the GBA's rate catches up
 * after a slow frame). A game that keeps missing (three seconds in a row
 * with 3 or more) goes back to the GBA's rate until the setting is turned
 * on again or the game is started again (pacedFallback). */
static int pacedMisses, pacedMissSecs;

static void pacing_update(void) {
	double hz = nScreens > 0 ? ui_display_hz(screens[game_screen()].view) : 0;
	pacedHz = 0;
	pacedMisses = pacedMissSecs = 0;
	if (cfg->matchDisplay && !pacedFallback && hz > 0 && fabs(hz / GBA_FPS - 1) <= 0.015) pacedHz = hz;
	frameSec = 1.0 / (pacedHz > 0 ? pacedHz : GBA_FPS);
	audioSpeed = pacedHz > 0 ? pacedHz / GBA_FPS : 1.0;
	fprintf(stderr, "frame pacing: display %.3f Hz, game %.3f frames a second (%s)\n", hz, 1.0 / frameSec,
	        pacedHz > 0 ? "matched to the display" : cfg->matchDisplay && pacedFallback ? "the GBA's rate: frames kept missing the display's refresh" :
	        cfg->matchDisplay ? "display too far off to match" : "the GBA's rate");
}

/* Rewind (hold L2): mGBA's rewind buffer, a state every second frame,
 * rewind_seconds long (default 30 s: 900 states, each kept as its
 * difference from the next, a few MB). */
static int rewindTick;
static bool wasRewinding;

static void rewind_setup(void) {
	emu_rewind_deinit();
	rewindTick = 0;
	if (!cfg->rewind || !emu.core) return;
	int secs = cfg->rewindSeconds < 5 ? 5 : cfg->rewindSeconds > 300 ? 300 : cfg->rewindSeconds;
	emu_rewind_init(&emu, (size_t) secs * 30);
	fprintf(stderr, "rewind: %d s (%d states)\n", secs, secs * 30);
}

/* ff_speed as gba-ds.cfg writes it: a number, or max (0, as atoi reads it) */
static const char* ff_speed_name(const Config* c) {
	static char s[12];
	if (c->ffSpeed <= 0) return "max";
	snprintf(s, sizeof(s), "%d", c->ffSpeed);
	return s;
}

/* Display choices made in the menu are remembered for next time (and the
 * game list's Performance setting: prefs_save from library.c). */
static void save_prefs(void) {
	prefs_save(cfg);
}

void prefs_save(Config* cfg) {
	/* what the settings table doesn't hold (it writes its own lines: the game
	 * list's Settings, the pause menu): the quick bar's pin, the shader the
	 * hotkey turns back on, the generated bezel's colours. Every other line
	 * stays (config_kv_update). */
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	const char* kv[4][2] = {
		{ "quick_bar_pinned", cfg->barPinned ? "1" : "0" },
		{ "shader_last", shaderNames[cfg->shaderLast > SH_OFF && cfg->shaderLast < SH_COUNT ? cfg->shaderLast : SH_GRID] },
		{ "bezel_inner", cfg->bezelInner },
		{ "bezel_outer", cfg->bezelOuter },
	};
	config_kv_update(prefs, "# written by gba-ds's menus; overrides gba-ds.cfg", 4, kv);
}

static bool gameTexAll = true; /* upload the game's frame to every view next time */

static void swap_screens(void) {
	/* the game moves to the other view, whose copy of the frame may be old
	 * (frames go only where the game is shown; the menu uploads none) */
	if (emu.fb) {
		for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, GBA_W * 4);
	}
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	commit_current("swap"); /* (the layer in force: this game's own, or all games') */
}

/* Display > Shader, or the shader hotkey: the pattern over the pixels */
static void set_shader(int sh) {
	bool subBefore = cfg->shader >= SH_SUB_LIGHT;
	cfg->shader = sh;
	if (sh != SH_OFF) cfg->shaderLast = sh;
	ui_shader(cfg->shader);
	if (subBefore || cfg->shader >= SH_SUB_LIGHT) rebuild_textures(); /* (the subpixels' brightening is in the colour table) */
	commit_current("shader");
	save_prefs();
}

/* ---- hotkeys (hotkeys.c): the hotkey button (HOME, SELECT, START or L3)
 * held with an action's button (Settings > Hotkeys, gba-ds.cfg).
 * SELECT or START as the hotkey still reach the game when tapped alone;
 * L3 tapped alone opens the quick menu, as it does when it isn't the hotkey. */
static void hotkeys_setup(void) {
	hotkeys_resolve(cfg, &hkMod, hkBtn, true);
	char line[200];
	int n = snprintf(line, sizeof(line), "hotkey: %s +", hotkeyBtnNames[hkMod]);
	for (int i = 0; i < HK_COUNT && n < (int) sizeof(line); ++i)
		n += snprintf(line + n, sizeof(line) - n, " %s %s%s", hotkeyNames[i], hkBtn[i] >= 0 ? hotkeyBtnNames[hkBtn[i]] : "none", i + 1 < HK_COUNT ? "," : "");
	fprintf(stderr, "%s\n", line);
}

static SDL_Color borderColor;

/* User overlay PNGs in <asset_dir>/overlays/, drawn over a whole screen
 * (transparent where the picture should show). A name is the parts that
 * apply, in this order, joined by dashes: the game's code (optional), the
 * screen (top or bottom; or, from before v1.5, primary: the game screen,
 * secondary: the other, which follow the picture when the screens are
 * swapped), the scaling (integer, fit, zoom, stretch; optional) and the
 * screen shapes (aspect or size, for screen_match; optional, and only
 * looked for while it's on and the screens' shapes differ). The most
 * specific found wins, top / bottom before primary / secondary at each step:
 *   <CODE>-<screen>-<scale>-<match>, <CODE>-<screen>-<match>,
 *   <CODE>-<screen>-<scale>, <CODE>-<screen>,
 *   (the second screen only, older names: <CODE>-<scale>, <CODE>)
 *   <screen>-<scale>-<match>, <screen>-<match>, <screen>-<scale>, <screen>
 * each with "-no_gap" after it while Screen Placement moves the picture
 * (load_overlay). wiki/Overlays.md explains them for players (top and bottom only). */
static void overlays_reload(void) {
	overlayMode[0][0] = overlayMode[1][0] = 0;
	bezelKey[0][0] = bezelKey[1][0] = 0;
}

/* s: the physical screen (0 top, 1 bottom); which: what's on it (0 the game, 1 the second screen) */
static void load_overlay(int s, int which) {
	const char* scale = which ? cfg->mapScale : cfg->scale;
	const char* match = strcmp(cfg->screenMatch, "keep") && ui_screens_differ() ? cfg->screenMatch : NULL;
	/* Screen Placement: Closest to Gap moves the picture where it doesn't
	 * fill the screen's height (Pixel Perfect, Full Width): an overlay made
	 * for it has "-no_gap" at the end of its name, and ones without it are left
	 * alone (their hole is where a centred picture goes) */
	const char* place = nScreens == 2 && !strcmp(cfg->screenPlace, "no_gap") && (!strcmp(scale, "integer") || !strcmp(scale, "fit")) ? "-no_gap" : "";
	char key[sizeof(overlayMode[0])];
	snprintf(key, sizeof(key), "%d|%s|%s|%s", which, scale, match ? match : "", place);
	if (!strcmp(overlayMode[s], key)) return;
	snprintf(overlayMode[s], sizeof(overlayMode[s]), "%s", key);
	ui_free_image(overlayTex[s]);
	const char* sides[2] = { s == 1 && nScreens == 2 ? "bottom" : "top", which ? "secondary" : "primary" };
	char names[20][64];
	int n = 0;
	for (int g = 0; g < 2; ++g) { /* this game's, then every game's */
		char code[8] = "";
		if (!g) snprintf(code, sizeof(code), "%.4s-", emu.gameCode);
		for (int k = 0; k < 2; ++k) if (match) snprintf(names[n++], 64, "%s%s-%s-%s", code, sides[k], scale, match);
		for (int k = 0; k < 2; ++k) if (match) snprintf(names[n++], 64, "%s%s-%s", code, sides[k], match);
		for (int k = 0; k < 2; ++k) snprintf(names[n++], 64, "%s%s-%s", code, sides[k], scale);
		for (int k = 0; k < 2; ++k) snprintf(names[n++], 64, "%s%s", code, sides[k]);
		if (!g && which) {
			snprintf(names[n++], 64, "%.4s-%s", emu.gameCode, scale); /* older names */
			snprintf(names[n++], 64, "%.4s", emu.gameCode);
		}
	}
	char path[1200];
	for (int i = 0; i < n; ++i) {
		snprintf(path, sizeof(path), "%s/overlays/%s%s.png", cfg->assetDir, names[i], place);
		if (ui_load_image(path, overlayTex[s], &overlayW[s], &overlayH[s], true)) {
			if (debug) fprintf(stderr, "overlay (%s screen): %s\n", sides[0], path);
			return;
		}
	}
}

/* GBA DS's own bezel round screen s's picture (bezel.c): in the player's
 * colours and with their logo setting (Overlays: Generated; while the
 * colour picker is open, its colour as it's picked), or in the defaults
 * (a screen without a PNG in Overlays: Custom). Nothing where the picture
 * fills the screen. */
static void draw_bezel(int s, const char* mode, bool defaults) {
	SDL_Rect r = ui_image_rect(s, GBA_W, GBA_H, mode), sr = screens[s].rect;
	/* (Zoom: the part on the screen) */
	int x0 = SDL_max(r.x, 0), y0 = SDL_max(r.y, 0), x1 = SDL_min(r.x + r.w, sr.w), y1 = SDL_min(r.y + r.h, sr.h);
	if (x0 <= 0 && y0 <= 0 && x1 >= sr.w && y1 >= sr.h) return;
	r = (SDL_Rect) { x0, y0, x1 - x0, y1 - y0 };
	uint32_t in = BEZEL_INNER_DEFAULT, out = BEZEL_OUTER_DEFAULT;
	bool logo = true;
	if (!defaults) {
		in = hex_rgb(cfg->bezelInner), out = hex_rgb(cfg->bezelOuter), logo = cfg->bezelLogo;
		if (pickWhich == 0) in = pick_rgb();
		if (pickWhich == 1) out = pick_rgb();
	}
	bool bottom = s == 1 && nScreens == 2;
	/* the logo by the hinge (below the top screen's picture, above the
	 * bottom one's); Screen Placement: Closest to Gap leaves no room there,
	 * so it's on the bottom screen only, below the picture (GEN-12) */
	bool above = bottom;
	if (nScreens == 2 && !strcmp(cfg->screenPlace, "no_gap")) {
		if (!bottom) logo = false;
		above = false;
	}
	char key[96];
	snprintf(key, sizeof(key), "%dx%d %d,%d,%d,%d %d%d %06X %06X %d", sr.w, sr.h, r.x, r.y, r.w, r.h, bottom, above, (unsigned) in, (unsigned) out, logo);
	if (strcmp(key, bezelKey[s])) {
		snprintf(bezelKey[s], sizeof(bezelKey[s]), "%s", key);
		ui_free_image(bezelTex[s]);
		Uint64 t0 = SDL_GetPerformanceCounter();
		SDL_Surface* srf = bezel_surface(sr.w, sr.h, r, above, in, out, logo);
		if (srf) ui_load_surface(srf, bezelTex[s], &bezelW[s], &bezelH[s], false);
		if (debug) fprintf(stderr, "bezel (%s screen): %s, %.1f ms\n", bottom ? "bottom" : "top", key,
		                   (SDL_GetPerformanceCounter() - t0) * 1000.0 / SDL_GetPerformanceFrequency());
	}
	if (bezelTex[s][0] || bezelTex[s][1]) ui_image(s, bezelTex[s], bezelW[s], bezelH[s], "stretch", NULL);
}

/* Overlays: on for this physical screen (one screen: the top one's setting) */
static bool overlay_on(int s) {
	return cfg->overlayMode != OVM_OFF && (cfg->overlays & (s == 1 && nScreens == 2 ? OVL_BOTTOM : OVL_TOP));
}

/* which: what's on the screen (0 the game, 1 the second screen) */
static void draw_overlay(int s, int which) {
	if (cfg->overlayMode == OVM_CUSTOM) {
		load_overlay(s, which);
		if (overlayTex[s][0] || overlayTex[s][1]) {
			ui_image(s, overlayTex[s], overlayW[s], overlayH[s], "stretch", NULL);
			return;
		}
	}
	draw_bezel(s, which ? cfg->mapScale : cfg->scale, cfg->overlayMode != OVM_GENERATED);
}


/* Text size for the FPS counter and messages: fits the empty band (gap px tall)
 * above or below the picture, even in full-width scaling. */
static int small_text_px(int s, int gap) {
	return SDL_max(1, SDL_min(SDL_min(ui_px(s) - 1, 3), (gap - 6) / 7)); /* 7 rows tall, 3px margin each side */
}

/* Messages ("RESUMED", "STATE SAVED"...): bottom left of the touchscreen (the
 * bottom screen), in the band below the picture, at the FPS counter's size. */
static bool shotPending; /* Take Screenshot: capture the next frame drawn */

static void draw_osd(void) {
	const char* osd = ui_osd_text();
	if (!osd || menuOpen || shotPending || barShown > 0) return; /* (the quick bar shows it) */
	int s = ui_touch_screen();
	SDL_Rect r = screens[s].rect;
	bool gameHere = nScreens == 1 || s == game_screen() || !((mc || logoOnly) && cfg->map);
	SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, gameHere ? cfg->scale : cfg->mapScale);
	int gap = r.h - (img.y + img.h);
	int fpx = small_text_px(s, img.y < gap ? img.y : gap);
	int fy = gap >= 7 * fpx + 2 ? img.y + img.h + (gap - 7 * fpx) / 2 : r.h - fpx * 9;
	int fx = img.x + fpx * 2, cy = fy + fpx * 7 / 2, cap = fpx * 6;
	ui_font_text(s, TF_BOLD, fx + SDL_max(1, fpx / 2), cy + SDL_max(1, fpx / 2), cap, osd, (SDL_Color) { 0, 0, 0, 255 }, -1);
	ui_font_text(s, TF_BOLD, fx, cy, cap, osd, C_TEXT, -1);
}

/* The game's logo at its own size (GEN-1) onto a cw x ch canvas: k canvas
 * pixels to a logo pixel (one GBA pixel each for an extracted logo),
 * centred, blended over what's there at `alpha` (255: opaque; the logo
 * alone, with nothing live to show, at half: LOGO_IDLE); with `top` (a HUD
 * above it), centred in the rows below that. A user's own logo too big
 * for that is shrunk to fit. False if there's no logo. */
#define LOGO_IDLE 128
static bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha) {
	bool pixelArt = false;
	int sub = 1;
	SDL_Surface* lg = ui_logo_surface(cfg, romPath, emu.gameCode, &pixelArt, &sub);
	if (!lg) return false;
	int room = ch - top;
	if (room <= 0) {
		/* (no room below a HUD that fills the screen: Mega Man Battle
		 * Network's custom screen, hud.rows GBA_H) */
		SDL_FreeSurface(lg);
		return false;
	}
	int w = SDL_max(1, lg->w * k / sub), h = SDL_max(1, lg->h * k / sub);
	if (sub > k) {
		/* a scraped logo at full resolution, onto a canvas with fewer
		 * pixels per GBA pixel: averaged down to them (GEN-11) */
		SDL_Surface* sm = ui_resample(lg, w, h);
		if (sm) SDL_FreeSurface(lg), lg = sm;
	}
	if (w > cw || h > room) {
		float sc = SDL_min((float) cw / lg->w, (float) room / lg->h);
		w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
	}
	/* centred on the canvas, or with a HUD above it in the room below */
	int x0 = (cw - w) / 2, y0 = top ? top + (room - h) / 2 : (ch - h) / 2;
	SDL_LockSurface(lg);
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + (size_t) (y * lg->h / h) * lg->pitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lg->w / w) * 4; /* RGBA32: bytes R, G, B, A */
			int a = p[3] * alpha / 255;
			if (!a) continue;
			uint32_t* o = &px[(y0 + y) * cw + x0 + x];
			uint32_t out = 0xFF000000;
			for (int c = 0; c < 3; ++c) {
				int bg = (*o >> (c * 8)) & 0xFF;
				out |= (uint32_t) (bg + (p[c] - bg) * a / 255) << (c * 8); /* ABGR8888 */
			}
			*o = out;
		}
	}
	SDL_UnlockSurface(lg);
	SDL_FreeSurface(lg);
	return true;
}

/* The game's logo as a GBA screen would show it: centred on dark grey in a
 * 240x160 picture, at half strength (nothing live to show), drawn with the second screen's scaling and the LCD grid.
 * Logos extracted from the ROM are at GBA resolution; a user's own logo
 * that's bigger stays a high-resolution image (logoCanvasTex NULL). */
static void make_logo_canvas(void) {
	ui_free_image(logoCanvasTex);
	bool pixelArt = false;
	int sub = 1;
	SDL_Surface* lg = ui_logo_surface(cfg, romPath, emu.gameCode, &pixelArt, &sub);
	if (!lg) return;
	logoSub = sub;
	bool big = lg->w > GBA_W * sub || lg->h > GBA_H * sub;
	SDL_FreeSurface(lg);
	if (big) return;
	if (sub > 1) {
		/* a scraped logo at full resolution (GEN-11): the same picture at
		 * sub pixels per GBA pixel, smoothly scaled to the GBA picture's place */
		int cw = GBA_W * sub, ch = GBA_H * sub;
		uint32_t* big4 = malloc((size_t) cw * ch * 4);
		if (!big4) return;
		for (int i = 0; i < cw * ch; ++i) big4[i] = SCREEN_GREY;
		logo_draw(big4, cw, ch, sub, 0, LOGO_IDLE);
		for (int i = 0; i < nViews; ++i) {
			logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, cw, ch);
			if (!logoCanvasTex[i]) continue;
			SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
			SDL_SetTextureScaleMode(logoCanvasTex[i], SDL_ScaleModeLinear);
			ui_update_texture(logoCanvasTex[i], big4, cw * 4);
		}
		free(big4);
		return;
	}
	static uint32_t px[GBA_W * GBA_H];
	for (int i = 0; i < GBA_W * GBA_H; ++i) px[i] = SCREEN_GREY;
	logo_draw(px, GBA_W, GBA_H, 1, 0, LOGO_IDLE);
	for (int i = 0; i < nViews; ++i) {
		logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		if (!logoCanvasTex[i]) continue;
		SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
		ui_update_texture(logoCanvasTex[i], px, GBA_W * 4);
	}
}

/* profile.combat's picture (and hud.logoUnder's): the logo as on any
 * other second screen (GEN-1), at a GBA pixel per GBA pixel of the map
 * picture. */
static void make_combat_canvas(void) {
	combatMade = true;
	uint32_t* px = malloc((size_t) mapW * mapH * 4);
	if (!px) return;
	for (int i = 0; i < mapW * mapH; ++i) px[i] = SCREEN_GREY;
	logo_draw(px, mapW, mapH, SDL_max(1, mapW / GBA_W), 0, LOGO_IDLE);
	for (int i = 0; i < nViews; ++i) {
		if (combatTex[i]) ui_update_texture(combatTex[i], px, mapW * 4);
	}
	free(combatPx);
	combatPx = px; /* (hud.logoUnder) */
}

static void draw_game(void) {
	bool mapOn = (mc || logoOnly) && cfg->map; /* logo-only games: the logo stands in for the map */
	for (int s = 0; s < nScreens; ++s) {
		bool showGame = nScreens == 1 ? !(singleShowsMap && mapOn) : (s == game_screen() || !mapOn);
		SDL_Rect r = screens[s].rect;
		if (showGame) {
			if (nScreens == 2 && !mapOn && s != game_screen()) {
				/* no map: the other screen black, in its bezel if it has one */
				if (overlay_on(s)) draw_overlay(s, 1);
				continue;
			}
			ui_image_pixels(s, gameTex, GBA_W, GBA_H, cfg->scale, SHADER_ON);
			if (hudProf && hudOn && hudAlt && hudProf->hud.alt.dimGame) {
				/* (the phase banner moved to the other screen) */
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				ui_fill(s, img, (SDL_Color) { 0, 0, 0, 150 });
			}
			if (overlay_on(s)) draw_overlay(s, 0);
			if (cfg->showFps) {
				/* sized to sit in the empty band above the picture */
				char buf[48];
				snprintf(buf, sizeof(buf), "%.1f FPS  %d%%", fps, (int) (speedPct + 0.5));
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				int gap = img.y;
				int fpx = small_text_px(s, gap);
				int fy = gap >= 7 * fpx + 2 ? (gap - 7 * fpx) / 2 : fpx * 2;
				int fx = img.x + fpx * 2;
				ui_text(s, fx + 1, fy + 1, fpx, buf, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, fx, fy, fpx, buf, C_TEXT);
			}
			continue;
		}
		/* the map screen */
		/* (and with hud.logoUnder, on the map before there's a map picture) */
		bool logoBack = combatProf && combatProf->hud.logoUnder && mapShownIdx < 0 && (combatInGame || combatProf->hud.logoAlways);
		if (overAlone && mapShownIdx >= 0) logoBack = false; /* (the phase banner, on the other armies' turn too) */
		if (((combatOn && !(overAlone && mapShownIdx >= 0)) || logoBack) && (combatTex[0] || combatTex[1])) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, combatTex, mapW, mapH, cfg->mapScale, SHADER_ON);
		} else if (hudShown && !logoForced) {
			/* the HUD picture is at 2x: a grid cell per GBA pixel */
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, hudTex, HUD_CW, HUD_CH, cfg->mapScale, SHADER_ON ? 2 : 0);
		} else if (mapShownIdx >= 0 && !logoForced) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, mapTex, mapW, mapH, cfg->mapScale, SHADER_ON);
		} else if ((logoCanvasTex[0] || logoCanvasTex[1]) && logoSub > 1) {
			/* a scraped logo at full resolution (GEN-11): its picture made at
			 * LOGO_SUB pixels per GBA pixel, onto where the GBA picture goes */
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			ui_image(s, logoCanvasTex, GBA_W * logoSub, GBA_H * logoSub, "stretch", &area);
		} else if (logoCanvasTex[0] || logoCanvasTex[1]) {
			/* no map yet (title screen, intro...): the game's logo, as if the
			 * game drew it on a second GBA screen */
			ui_image_pixels(s, logoCanvasTex, GBA_W, GBA_H, cfg->mapScale, SHADER_ON);
		} else if (logoTex[0] || logoTex[1]) {
			/* a user's high-resolution logo: fitted to where the picture goes */
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			ui_image(s, logoTex, logoW, logoH, logoPixel ? "integer" : "fit", &area);
		} else if (logoOnly) {
			/* no logo to show (none extracted for this game yet): its name */
			const MapProfile* tp = profile_find(emu.gameCode);
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			int in = area.w / 10;
			area = (SDL_Rect) { area.x + in, area.y + in, area.w - in * 2, area.h - in * 2 };
			if (tp) ui_title_block(s, area, tp->title, (int) (ui_px(s) * 14), (SDL_Color) { 236, 236, 244, 255 });
		}
		/* whatever this screen shows: map, HUD, logo or nothing */
		if (overlay_on(s)) draw_overlay(s, 1);
		if (!hudShown && mapShownIdx >= 0 && mapInactive && !logoForced) {
			ui_dim(s, 120);
		}
	}
	draw_osd();
}

static void close_ach(void) {
	if (achOpen) ra_list_close();
	achOpen = false;
}

/* the list's area: below the Back / title boxes */
static SDL_Rect ach_area(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	return (SDL_Rect) { px * 4, px * 25, r.w - px * 8, r.h - px * 27 };
}

static void open_menu(void) {
	bar_close();
	barShown = 0;
	cheatsOpen = false;
	patchesOpen = false;
	ovl_close_all();
	close_ach();
	scopeAll = false;
	set_page(0);
	menuOpen = true;
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	armedItem = -1;
}

/* ---- settings in layers (docs/MENUS.md): states/prefs.cfg for all games,
 * states/<game>.cfg over it for this one. uniCfg holds the settings for all
 * games while the game runs (cfg has this game's put over them). */
static Config uniCfg;
static const char* const gameHeader = "# this game's own settings: they win over the ones for all games";
static const char* const prefsHeader = "# written by gba-ds's menus; overrides gba-ds.cfg";

static void game_keys_reload(void) {
	nGameKeys = config_kv_keys(gp.settings, 48, gameKeys);
}

static void write_setting(const char* path, const char* header, const Config* c, const Setting* st) {
	char lines[2][2][24];
	const char* kv[2][2];
	int n = config_setting_lines(c, st, lines);
	for (int i = 0; i < n; ++i) kv[i][0] = lines[i][0], kv[i][1] = lines[i][1];
	config_kv_update(path, header, n, kv);
}
static void prefs_file(char* out, size_t n) {
	snprintf(out, n, "%s/prefs.cfg", cfg->stateDir);
}

/* a choice made some other way (the hotkeys, the overlay page): kept in the layer
 * in force, this game's own file if it has a value there, else all games' */
static void commit_current(const char* key) {
	const Setting* st = config_setting(key);
	if (!st) return;
	if (game_has(st)) {
		write_setting(gp.settings, gameHeader, cfg, st);
	} else {
		char path[1100];
		prefs_file(path, sizeof(path));
		config_setting_copy(&uniCfg, cfg, st);
		write_setting(path, prefsHeader, &uniCfg, st);
	}
}

/* put a changed setting to use in the game now */
static void setting_applied(const Setting* st) {
	const char* k = st->key;
	if (!strcmp(k, "shader") || !strcmp(k, "lcd_strength")) {
		if (cfg->shader != SH_OFF) cfg->shaderLast = cfg->shader;
		ui_shader(cfg->shader);
		rebuild_textures();
	} else if (!strcmp(k, "color_correction")) {
		ui_color_correction(cfg->colorCorrection);
		rebuild_textures(); /* (every picture given again, through it or not) */
	} else if (!strcmp(k, "lcd_ghosting")) {
		ghostHave = false;
	} else if (!strcmp(k, "swap")) {
		if (emu.fb) {
			for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, GBA_W * 4);
		}
	} else if (!strcmp(k, "screen_match") || !strcmp(k, "screen_placement")) {
		ui_layout_pictures();
		overlays_reload(); /* (overlays can be made for each: overlay files) */
		gameTexAll = true;
	} else if (!strcmp(k, "brightness_top") || !strcmp(k, "brightness_bottom")) {
		ui_brightness(cfg->brightTop, cfg->brightBottom);
	} else if (!strcmp(k, "match_display")) {
		pacedFallback = false;
		pacing_update();
	} else if (!strcmp(k, "rewind") || !strcmp(k, "rewind_seconds")) {
		rewind_setup();
	} else if (!strcmp(k, "custom_logos")) {
		logo_refresh();
	} else if (!strcmp(k, "ab_swap") || !strncmp(k, "button_", 7)) {
		fb_default();
	} else if (!strcmp(k, "pinball_live")) {
		snprintf(menuNote, sizeof(menuNote), tr("Takes effect after you close and restart GBA DS"));
	}
}

static void rows_note(const char* what, bool all, bool kept) {
	if (menuNote[0] && !strncmp(menuNote, "Takes", 5)) return;
	snprintf(menuNote, sizeof(menuNote), "%s%s", all ? tr("Saved for all games"): tr("Saved for this game"), kept ? ": this game keeps its own value" : "");
	(void) what;
}

/* where a setting stands on its list (or its number), to tell when a step wrapped round */
static int step_pos(const Config* c, const Setting* st) {
	if (st->nChoices) return config_setting_index(c, st);
	return *(const int*) ((const char*) c + st->off);
}

/* step row `key` by dir, for all games or this game. For this game the list has
 * one more place, "same as all games": stepping past either end of the list
 * from a value of this game's own takes it away (row_reset) */

static void row_reset(const char* key);
static void row_change(const char* key, int dir, bool all) {
	menuNote[0] = 0;
	if (!strcmp(key, "@overlay")) {
		ovlOpen = true;
		ovlSel = 0;
		ovlPress = -1;
		return;
	}
	const Setting* st = config_setting(key);
	if (!st) return;
	const char* off = row_disabled(key);
	if (off) {
		snprintf(menuNote, sizeof(menuNote), "%s", off);
		return;
	}
	char path[1100];
	if (all) {
		if (!config_setting_step(&uniCfg, st, dir)) return;
		prefs_file(path, sizeof(path));
		write_setting(path, prefsHeader, &uniCfg, st);
		bool kept = game_has(st);
		if (!kept) {
			config_setting_copy(cfg, &uniCfg, st);
			setting_applied(st);
		}
		rows_note(key, true, kept);
	} else {
		int before = step_pos(cfg, st);
		bool had = game_has(st);
		if (!config_setting_step(cfg, st, dir)) return;
		int after = step_pos(cfg, st);
		if (had && (dir > 0 ? after < before : after > before)) {
			row_reset(key); /* (round the end of the list: back to the value for all games) */
			return;
		}
		write_setting(gp.settings, gameHeader, cfg, st);
		game_keys_reload();
		setting_applied(st);
		rows_note(key, false, false);
	}
	fprintf(stderr, "setting: %s = %s (%s)\n", key, st->choices ? st->choices[config_setting_index(cfg, st) < 0 ? 0 : config_setting_index(cfg, st)].value : "?", all ? "all games" : "this game");
}

/* X: this game goes back to the value for all games */
static void row_reset(const char* key) {
	menuNote[0] = 0;
	const Setting* st = config_setting(key);
	if (!st) return;
	if (!game_has(st)) {
		snprintf(menuNote, sizeof(menuNote), tr("Already the value for all games"));
		return;
	}
	char lines[2][2][24];
	const char* keys[2];
	int n = config_setting_lines(cfg, st, lines);
	for (int i = 0; i < n; ++i) keys[i] = lines[i][0];
	config_kv_remove(gp.settings, n, keys);
	game_keys_reload();
	config_setting_copy(cfg, &uniCfg, st);
	setting_applied(st);
	snprintf(menuNote, sizeof(menuNote), tr("Same as all games again"));
}

/* the controller on a settings page (the bar, then the rows) */
static void rows_input(void) {
	int n = rowN[curPage];
	if (btn_pressed(B_DOWN)) rowSel = (rowSel + 1) % (n + 1), menuNote[0] = 0;
	if (btn_pressed(B_UP)) rowSel = (rowSel + n) % (n + 1), menuNote[0] = 0;
	rows_scroll_to_sel();
	if (rowSel == 0) {
		if (btn_pressed(B_LEFT)) scopeAll = false;
		if (btn_pressed(B_RIGHT)) scopeAll = true;
		if (btn_pressed(B_A)) scopeAll = !scopeAll;
		return;
	}
	const char* key = rowKeys[curPage][rowSel - 1];
	bool all = scopeAll || btnNow[B_Y]; /* (Y held with a button: all games) */
	int dir = btn_pressed(B_LEFT) ? -1 : (btn_pressed(B_RIGHT) || btn_pressed(B_A)) ? 1 : 0;
	if (!dir && btn_pressed(B_Y)) dir = 1, all = true;
	if (dir) row_change(key, dir, all);
	if (btn_pressed(B_X) && key[0] != '@') row_reset(key);
}

/* a touch on a settings page, on its own: the bar sets which it changes, a row is
 * changed when the finger lifts on it (the left arrow steps back, the Game tag
 * takes this game's value away, the rest steps forward) */
static void rows_touch(const Pointer* p) {
	int n = rowN[curPage];
	if (p->kind == 1) {
		ignorePointerUp = false;
		rowPressed = -1;
		for (int h = 0; h < 2; ++h)
			if (in_rect(scope_rect(p->screen, h), p->x, p->y)) scopeAll = h == 1, rowSel = 0, menuNote[0] = 0, ignorePointerUp = true;
		for (int i = 1; i <= n; ++i)
			if (srow_shown(i) && in_rect(srow_rect(p->screen, i), p->x, p->y)) rowPressed = i, rowSel = i, rowDragY = p->y, rowDragged = false;
		if (rowPressed < 0) rowDragY = p->y, rowDragged = false;
	} else if (p->kind == 3 && n > ROW_VIS) {
		/* a finger dragged up or down moves the list a row at a time, and no longer counts as a tap */
		int step = SDL_max(1, srow_rect(p->screen, 2).y - srow_rect(p->screen, 1).y);
		if (abs(p->y - rowDragY) >= step) {
			rowTop = SDL_clamp(rowTop + (p->y < rowDragY ? 1 : -1), 0, n - ROW_VIS);
			rowDragY = p->y, rowDragged = true;
			ignorePointerUp = true;
		}
	} else if (p->kind == 2) {
		int i = rowPressed;
		if (!ignorePointerUp && !rowDragged && i >= 1 && srow_shown(i) && in_rect(srow_rect(p->screen, i), p->x, p->y)) {
			const char* key = rowKeys[curPage][i - 1];
			if (key[0] != '@' && rowTagOn[i] && in_rect(rowTag[i], p->x, p->y)) row_reset(key);
			else row_change(key, key[0] != '@' && in_rect(rowBack[i], p->x, p->y) ? -1 : 1, scopeAll);
		}
		rowPressed = -1;
		ignorePointerUp = false;
	}
}

/* Returns true if the session should end (*exitTo set). */
static bool activate(int item, enum GameExit* exitTo) {
	if (confirm_item(item) && armedItem != item) {
		armedItem = item;
		const char* lost = cfg->autoSave ? N_("your place is saved"): N_("unsaved progress is lost");
		if (item == M_RESTART) snprintf(menuNote, sizeof(menuNote), tr("Choose it again to restart: unsaved progress is lost"));
		else snprintf(menuNote, sizeof(menuNote), tr("Choose it again to close %s: %s"), item == M_EXIT ? "GBA DS" : tr("the game"), lost);
		return false;
	}
	armedItem = -1;
	switch (item) {
	case M_RESUME:
		menuOpen = false;
		break;
	case M_SHOT:
		menuOpen = false; /* taken from the next frame, without the menu */
		shotPending = true;
		break;
	case M_ACH:
		ra_list_open();
		achOpen = true;
		achPressed = false;
		break;
	case M_CHEATS:
		if (!cheats.n) snprintf(menuNote, sizeof(menuNote), tr("No cheats found (see Cheats in the wiki)"));
		else open_cheats();
		break;
	case M_PATCHES:
		open_patches();
		break;
	case M_SLOT:
		slot_step(1);
		break;
	case M_SAVE:
		if (state_save(gp.slot[stateSlot])) {
			char m[40];
			snprintf(m, sizeof(m), tr("State saved to slot %d"), stateSlot);
			ui_osd(m);
			menuOpen = false;
		} else {
			snprintf(menuNote, sizeof(menuNote), tr("Save failed"));
		}
		break;
	case M_LOAD:
		if (!item_enabled(M_LOAD)) {
			snprintf(menuNote, sizeof(menuNote), tr("Nothing saved in slot %d yet"), stateSlot);
		} else if (state_load(gp.slot[stateSlot])) {
			char m[40];
			snprintf(m, sizeof(m), tr("Slot %d loaded"), stateSlot);
			if (!stateWarned) ui_osd(m);
			menuOpen = false;
			return false;
		} else if (stateLoadKind == STATE_FILE_OTHER_CORE) {
			/* (RetroArch's, from another of its GBA cores: issue #39) */
			/* (Settings > Save States says RetroArch's core has to be mGBA) */
			snprintf(menuNote, sizeof(menuNote), tr("Saved by another emulator: can't load it"));
		} else if (stateLoadKind == STATE_FILE_UNREADABLE) {
			snprintf(menuNote, sizeof(menuNote), tr("This state can't be read"));
		} else if (stateLoadBios == BIOS_REFUSAL_STATE_HAS_BIOS) {
			/* (a RetroArch state from its core with a BIOS file: issue #78; Settings > System has the option) */
			snprintf(menuNote, sizeof(menuNote), tr("Saved with a GBA BIOS: see Settings > System"));
		} else if (stateLoadBios == BIOS_REFUSAL_CORE_HAS_BIOS) {
			snprintf(menuNote, sizeof(menuNote), tr("Saved without a GBA BIOS: see Settings > System"));
		} else {
			snprintf(menuNote, sizeof(menuNote), tr("Load failed"));
		}
		break;
	case M_BUG: {
		/* the moment the menu opened, and the 10 seconds before it */
		char what[200];
		if (bug_report(what, sizeof(what))) note_open(false); /* (a note, then it's saved) */
		else snprintf(menuNote, sizeof(menuNote), "%s", what);
		break;
	}
	case M_RESTART:
		emu_reset(&emu);
		aheadReady = false;
		bug_reset();
		ra_reset();
		mapShownIdx = -1; /* back to the logo until the next map */
		menuOpen = false;
		ui_osd("Game restarted");
		break;
	case M_LIBRARY:
		*exitTo = GAME_TO_LIBRARY;
		return true;
	case M_EXIT:
		*exitTo = GAME_QUIT_APP;
		return true;
	}
	return false;
}

/* Saves what's on the screens now (before ui_present) as
 * <screenshot_dir>/<game>-YYYYMMDD-HHMMSS.png, screens stacked top to bottom. */
static void take_screenshot(void) {
	char dir[600], path[1400];
	snprintf(dir, sizeof(dir), "%s", cfg->screenshotDir);
	mkdirs(dir);
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(dir, sizeof(dir), "%s/screenshots", cfg->stateDir);
		mkdirs(dir);
	}
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
	snprintf(path, sizeof(path), "%s/%s-%s.png", dir, gp.base, stamp);
	for (int n = 2; file_exists(path) && n < 100; ++n) snprintf(path, sizeof(path), "%s/%s-%s-%d.png", dir, gp.base, stamp, n);
	ui_osd(ui_screenshot_stacked(path) ? "Screenshot saved" : "Screenshot failed");
}

/* ---------------------------------------------------------------- session */

/* The second screen's set-up for the profile with this code (the game's,
 * or for a compilation the game running in it: subCode): the map capture,
 * the HUD split, the fights' picture. */
static void ss_setup(const char* rom, const char* code) {
	mapW = GBA_W;
	mapH = GBA_H;
	memset(&mapRes, 0, sizeof(mapRes));
	mapShownIdx = -1;
	stillValid = false;
	mapInactive = false;
	singleShowsMap = false;
	logoOnly = false;
	snapProf = NULL;
	tallProf = NULL;
	tallOn = false;
	tallLive = false;
	hudProf = NULL;
	hudOn = false;
	{
		const MapProfile* cp = profile_find(code);
		ctlProf = cp && (cp->controls.fourFace || cp->controls.snes.bottom) ? cp : NULL;
		snesHere = false;
		fb_load(); /* GEN-10 */
		if (ctlProf) {
			ctl_load();
			fprintf(stderr, "controls: %s\n", ctl_name(ctlMode));
		}
	}
	if (cfg->map) {
#ifdef __ANDROID__
		android_set_pinning(cfg->pinCores);
#endif
		mc = mapcap_create(rom, code, true);
		const MapProfile* lp = profile_find(code);
		memprobe_virtual = lp ? lp->virt : NULL;
		logoOnly = lp && lp->logoOnly;
		snapProf = logoOnly && lp->snap.showMin ? lp : NULL;
		tallProf = logoOnly && lp->tall.on[0].addr && cfg->tallLive ? lp : NULL;
		if (tallProf && nScreens > 1) twin_open(rom, tallProf);
		else twin_close();
		/* the HUD moves only when there's another screen to put it on */
		hudProf = lp && lp->hud.on[0].addr && nScreens > 1 ? lp : NULL;
		emu_hud_game_reg(hudProf ? hudProf->hud.gameReg.addr : 0, hudProf ? hudProf->hud.gameReg.bits : 0);
		emu_hud_rows(hudProf ? hudProf->hud.rows : 0);
		emu_hud_band(hudProf && hudProf->hud.band, true, hudProf && hudProf->hud.band == BAND_WATCH);
		combatProf = mc && lp && lp->combat[0].addr && nScreens > 1 ? lp : NULL;
		combatOn = combatMade = false;
		overOn = false;
		for (int m = 0; m < 4; ++m) pieces[m].valid = false, pieces[m].still = 0;
		if (hudProf) {
			/* the sprites that move; tagged ones only if the tags go in */
			memcpy(hudObjs, hudProf->hud.objs, sizeof(hudObjs));
			bool patched = profile_patch_rom(hudProf, &emu);
			if (patched) fprintf(stderr, "HUD split: ROM patched\n");
			for (int i = 0; !patched && i < 12; ++i) hudObjs[i].tagged = 0;
			memcpy(hudWorldObjs, hudProf->hud.world.objs, sizeof(hudWorldObjs));
			for (int i = 0; !patched && i < 4; ++i) hudWorldObjs[i].tagged = 0;
		}
		hudOn = false;
		hudShown = false;
		mirrorHave = mirrorOn = false;
		hudScene = false;
		hudCard = false, cardScore = -1, sideScore = -1;
		hudFadeEff = hudFadeIn = 0;
		memset(hudPalRef, 0, sizeof(hudPalRef));
		hudWorld = false;
		hudAlt = false;
		hudOffLatch = false;
		hudLogoDone = false;
		hudLogoBelowDone = false;
		mapTitleN = 0;
		hudBackValid = false;
		hudRegrab = 0;
		snapShow = snapRevert = 0;
		if (mc) mapcap_set_still(mc, !map_anim());
		if (logoOnly) fprintf(stderr, "logo only: %s\n", lp->title);
		hideWhenProf = NULL;
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
			/* the full map is always up, so hide the game's own corner mini-map */
			const MapProfile* prof = profile_find(code);
			int nRules = 0;
			while (prof && nRules < (int) (sizeof(prof->hideSprites) / sizeof(prof->hideSprites[0])) && prof->hideSprites[nRules].x1) ++nRules;
			/* (with hideWhen: off until the frame loop finds it matching) */
			hideWhenProf = prof && nRules && prof->hideWhen[0].addr ? prof : NULL;
			hideWhenN = nRules, hideWhenOn = false;
			emu_hide_sprites(&emu, prof && !hideWhenProf ? prof->hideSprites : NULL, hideWhenProf ? 0 : nRules);
			emu_hide_grouped(&emu, prof && prof->hideGrouped);
		} else if (!logoOnly) {
			fprintf(stderr, "no map profile for %s\n", code);
		}
	}
}

/* the textures the second screen's set-up needs (again after a switch) */
static void ss_textures(void) {
	for (int i = 0; i < nViews; ++i) {
		if (mapTex[i]) SDL_DestroyTexture(mapTex[i]);
		if (combatTex[i]) SDL_DestroyTexture(combatTex[i]);
		if (hudTex[i]) SDL_DestroyTexture(hudTex[i]);
		mapTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH);
		combatTex[i] = combatProf ? SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH) : NULL;
		if (combatTex[i]) SDL_SetTextureBlendMode(combatTex[i], SDL_BLENDMODE_NONE);
		hudTex[i] = hudProf ? SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, HUD_CW, HUD_CH) : NULL;
		if (hudTex[i]) SDL_SetTextureBlendMode(hudTex[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureBlendMode(mapTex[i], SDL_BLENDMODE_NONE);
	}
}

/* After the device sleeps (or the renderer is reset), the GPU's copies of
 * the pictures may be gone: every texture is made again and given its
 * picture again (the game's comes with the next frame). */
static const char* romPath;
static Uint32 lastLoop; /* the last pass of the play loop (SDL_GetTicks) */
static void rebuild_textures(void) {
	for (int i = 0; i < nViews; ++i) {
		if (gameTex[i]) SDL_DestroyTexture(gameTex[i]);
		gameTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		if (gameTex[i]) SDL_SetTextureBlendMode(gameTex[i], SDL_BLENDMODE_NONE);
	}
	ss_textures();
	if (emu.fb) for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, GBA_W * 4);
	if (mapShownIdx >= 0) {
		int idx = mapShownIdx;
		overDirty = true;
		upload_map_frame(idx);
	}
	if (combatMade) make_combat_canvas();
	if (hudProf) for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
	ui_free_image(logoTex);
	if (romPath && ui_load_logo(cfg, romPath, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	else make_logo_canvas();
	logoProvisional = ui_logo_provisional();
	ui_textures_reset();
}

/* A compilation (profile.pick): the game running in it changed, so the
 * second screen is set up again for that game's profile. */
static void ss_switch(const char* rom, const char* code) {
	fprintf(stderr, "frame %llu: now %s\n", (unsigned long long) frameCount, code);
	snprintf(subCode, sizeof(subCode), "%.4s", code);
	if (hudOn) emu_hud_split(&emu, false, 0, NULL, 0, hudPx); /* (the old game's HUD split off) */
	emu_hide_sprites(&emu, NULL, 0);
	hideWhenProf = NULL;
	mapcap_destroy(mc);
	mc = NULL;
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	ss_setup(rom, subCode);
	ss_textures();
	make_logo_canvas();
}

/* ---------------------------------------------------------------- patches (GEN-9)
 * The game's patches folder (made if it isn't there), what's switched on
 * (<game>.patches), and the patches every core opens the ROM with; with one
 * applied, the saves and states named "<game> + <patch>" as the switches
 * say. */

static void patches_setup(const char* rom) {
	uint8_t h[0xB0];
	char code[5] = "????";
	if (emu_rom_header(rom, h, sizeof(h))) memcpy(code, h + 0xAC, 4);
	patch_folder(cfg, rom, code, patchDir, sizeof(patchDir), true);
	patch_choice_load(gp.patches, &patchChoice);
	patchStarted = patchChoice;
	char files[PATCH_ON_MAX + 1][1024];
	int n = patch_plan(cfg, rom, code, &patchChoice, files, PATCH_ON_MAX + 1);
	emu_patch_plan(rom, files, n);
	if (patchChoice.nOn) {
		/* their own saves only when something applies: patches that don't
		 * fit leave the game as it is, with its own saves */
		size_t sz = 0;
		int r = PATCH_NONE;
		free(emu_rom_read_played(rom, &sz, &r, NULL, NULL));
		if (r == PATCH_APPLIED || r == PATCH_PARTIAL) game_paths_patched(cfg, &gp, &patchChoice);
	}
	patchReload = patchReloadNext;
	patchReloadNext = false;
	if (!patchHandoffNext) patchHandoff[0] = 0;
	patchHandoffNext = false;
	char on[PATCH_ON_MAX * 260] = "";
	for (int i = 0, l = 0; i < patchChoice.nOn; ++i) l += snprintf(on + l, sizeof(on) - (size_t) l, "%s%s", i ? ", " : ", on: ", patchChoice.on[i]);
	fprintf(stderr, "patches: %s, %d to apply%s\n", patchDir, n, on);
}

/* The logo loaded again, and every picture it's in made again (extracted at
 * last, or a scraped / own logo's pixelate switched: GEN-11). */
static void logo_refresh(void) {
	ui_free_image(logoTex);
	ui_free_image(logoCanvasTex);
	logoSub = 1;
	bool got = ui_load_logo(cfg, romPath, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel);
	logoProvisional = ui_logo_provisional();
	if (!got) return;
	make_logo_canvas();
	hudLogoBelowDone = hudLogoDone = false; /* the HUD's pictures with it */
	cardLogoMade = false;
	if (combatMade) {
		/* and the fight picture (and the map picture's logo behind) */
		make_combat_canvas();
		overDirty = true;
		if (mapShownIdx >= 0) upload_map_frame(mapShownIdx);
	}
}

static enum GameExit game_run_inner(Config* c, const char* rom, bool fromLibrary);

/* A game's own settings (issue #53, states/<game>.cfg) are put over the
 * settings for the length of the game: what the file changed goes back to
 * what it was when the game ends, unless it was changed again meanwhile
 * (a menu choice for all games, which is saved already). */
enum GameExit game_run(Config* c, const char* rom, bool fromLibrary) {
	GamePaths p;
	game_paths(c, rom, &p);
	Config before = *c;
	int n = config_load_game_file(c, p.settings);
	Config after = *c;
	uniCfg = before;
	if (n) {
		fprintf(stderr, "game settings: %d from %s\n", n, p.settings);
		ui_layout_pictures();
	}
	enum GameExit x = game_run_inner(c, rom, fromLibrary);
	ui_stick_dpad(true);
	/* back to the settings for all games (with what was changed for them meanwhile) */
	const unsigned char *b = (const unsigned char*) &before, *a = (const unsigned char*) &after;
	unsigned char* cur = (unsigned char*) c;
	for (size_t i = 0; i < sizeof(Config); ++i)
		if (a[i] != b[i] && cur[i] == a[i]) cur[i] = b[i];
	for (int i = 0; i < config_setting_count(); ++i) {
		const Setting* st = config_setting_at(i);
		if (config_key_per_game(st->key)) config_setting_copy(c, &uniCfg, st);
	}
	ui_layout_pictures();
	ui_brightness(c->brightTop, c->brightBottom);
	return x;
}

static enum GameExit game_run_inner(Config* c, const char* rom, bool fromLibrary) {
	cfg = c;
	tallLiveAtStart = cfg->tallLive;
	debug = getenv("GBADS_DEBUG") != NULL;
	romPath = rom, lastLoop = 0;
	game_paths(cfg, rom, &gp);
	ui_brightness(cfg->brightTop, cfg->brightBottom); /* (this game's own, if it has them) */
	game_keys_reload();
	patches_setup(rom); /* GEN-9: the patches folder, what's on, and the plan every core opens the ROM with */
	profile_set_rom(NULL, 0);
	if (!emu_open(&emu, rom, gp.sav, true)) {
		ui_osd("Couldn't load the game");
		return fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP;
	}
	sun_load();
	profile_set_rom(emu.gameCode, emu.romCrc); /* (a known colour patch: the profile's colours as it draws them) */
	fprintf(stderr, "game %s (%s), save %s\n", gp.base, emu.gameCode, gp.sav);
	{
		char d[640];
		fprintf(stderr, "ROM: %s\n", dump_text(d, sizeof(d)));
	}
#ifdef __wasi__
	webPlaying = true;
#endif
	if (emu.patch != PATCH_NONE) {
		ra_game_patched(); /* (RetroAchievements doesn't allow patched games) */
	} else {
		ra_game_start(&emu, rom);
		if (ra_loading()) {
			/* The server's answers for a game with a big set are parsed on this thread
			 * (a second or two of CPU on the device): behind a screen, not a second into play. */
			ui_notice("Loading achievements...", tr("A few seconds"));
			Uint32 t0 = SDL_GetTicks();
			while (ra_loading() && SDL_GetTicks() - t0 < 12000) {
				ui_events();
				ra_idle();
				SDL_Delay(20);
			}
			fprintf(stderr, "ra: waited %u ms for the achievements before starting\n", (unsigned) (SDL_GetTicks() - t0));
		}
	}
	cheats_load(&cheats, cfg, rom, emu.gameCode, gp.cheats);
	if (cheats_on(&cheats)) cheats_apply(&cheats, &emu);
	cheatsOpen = false;
	patchesOpen = false;
	ovl_close_all();
	ui_on_background(on_background);
	ui_on_foreground(on_foreground);

	mc = NULL;
	gameTexAll = true;
	frameCount = 0;
	mapAnimStart = 0; /* (the last game's count was further on) */
	menuOpen = false;
	{
		/* a compilation: the profile of the game running in it (none yet) */
		const MapProfile* gp = profile_find(emu.gameCode);
		pickProf = gp && gp->pick ? gp : NULL;
		snprintf(subCode, sizeof(subCode), "%.4s", emu.gameCode);
	}
	ss_setup(rom, subCode);
	for (int i = 0; i < nViews; ++i) {
		gameTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		SDL_SetTextureBlendMode(gameTex[i], SDL_BLENDMODE_NONE);
	}
	ss_textures();
	unsigned bc = (unsigned) strtoul(cfg->borderColor, NULL, 16);
	borderColor = (SDL_Color) { (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255 };
	overlayMode[0][0] = overlayMode[1][0] = 0;
	bezelKey[0][0] = bezelKey[1][0] = 0;
	uint32_t logoGen = logo_generation();
	logoSub = 1;
	if (ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	logoProvisional = ui_logo_provisional();

	memset(pageLen, 0, sizeof(pageLen));
	memset(rowN, 0, sizeof(rowN));
	pages[PG_GAME][pageLen[PG_GAME]++] = M_RESUME;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_RESTART;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SAVE;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_LOAD;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SLOT;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_CHEATS;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_PATCHES;
	if (ra_enabled()) pages[PG_GAME][pageLen[PG_GAME]++] = M_ACH;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_SHOT;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_BUG;
	if (fromLibrary) pages[PG_GAME][pageLen[PG_GAME]++] = M_LIBRARY;
	pages[PG_GAME][pageLen[PG_GAME]++] = M_EXIT;
	ui_color_correction(cfg->colorCorrection); /* (Display > Color Correction, for this session's pictures) */
	ui_shader(cfg->shader);
#define ROW(pg, key) rowKeys[pg][rowN[pg]++] = key
	ROW(PG_DISPLAY, "scale");
	ROW(PG_DISPLAY, "map_scale");
	ROW(PG_DISPLAY, "shader");
	ROW(PG_DISPLAY, "lcd_strength");
	ROW(PG_DISPLAY, "color_correction");
	ROW(PG_DISPLAY, "lcd_ghosting");
	if (ui_logo_custom(cfg, romPath, emu.gameCode)) ROW(PG_DISPLAY, "custom_logos"); /* the game has the player's own or a scraped logo (GEN-11) */
	if (nScreens >= 2) ROW(PG_SCREENS, "swap");
	ROW(PG_SCREENS, "screen_match");
	ROW(PG_SCREENS, "screen_placement");
	ROW(PG_SCREENS, "overlays");
	ROW(PG_SCREENS, "overlay_screens");
	ROW(PG_SCREENS, "@overlay");
	ROW(PG_SCREENS, "brightness_top");
	if (nScreens >= 2) ROW(PG_SCREENS, "brightness_bottom");
	ROW(PG_CONTROLS, "left_stick_dpad");
	ROW(PG_CONTROLS, "ab_swap");
	ROW(PG_CONTROLS, "button_right");
	ROW(PG_CONTROLS, "button_bottom");
	ROW(PG_CONTROLS, "button_top");
	ROW(PG_CONTROLS, "button_left");
	ROW(PG_CONTROLS, "button_l");
	ROW(PG_CONTROLS, "button_r");
	ROW(PG_CONTROLS, "button_dpad_up");
	ROW(PG_CONTROLS, "button_dpad_down");
	ROW(PG_CONTROLS, "button_dpad_left");
	ROW(PG_CONTROLS, "button_dpad_right");
	ROW(PG_CONTROLS, "turbo_rate");
	ROW(PG_SPEED, "show_fps");
	ROW(PG_SPEED, "match_display");
	ROW(PG_SPEED, "frame_buffer");
	ROW(PG_SPEED, "rewind");
	ROW(PG_SPEED, "rewind_seconds");
	ROW(PG_SPEED, "fast_forward");
	ROW(PG_SPEED, "save_on_quit");
	ROW(PG_SPEED, "auto_load");
	ROW(PG_MAP, "map");
	ROW(PG_MAP, "map_interval");
	ROW(PG_MAP, "toggle");
	ROW(PG_MAP, "auto_rotate");
	ROW(PG_MAP, "toggle_button");
	ROW(PG_MAP, "pinball_live");
#undef ROW
	set_page(0);
	slot_pick_newest();

	if (cfg->loadState[0]) {
		if (!state_load(cfg->loadState)) fprintf(stderr, "could not load %s\n", cfg->loadState);
		cfg->loadState[0] = 0;
	} else if (patchHandoff[0] && file_exists(patchHandoff)) {
		/* GEN-9: patches switched without upsetting the saves: on from that very moment */
		stateNoCheck = true; /* (saved with the ROM before the change, on purpose) */
		bool ok = state_load(patchHandoff);
		stateNoCheck = false;
		remove(patchHandoff);
		patchHandoff[0] = 0;
		if (!stateWarned) ui_osd(ok ? "Patches changed: carrying on from where you were" : "Patches changed");
	} else if (cfg->autoLoad && !patchReload && file_exists(gp.autoState)) { /* (not right after switching a patch) */
		if (state_load(gp.autoState)) {
			if (!stateWarned) ui_osd("Resumed. Tap or HOME for the menu");
		} else if (stateLoadKind == STATE_FILE_OTHER_CORE) {
			/* (RetroArch's automatic state, from another of its GBA cores) */
			ui_osd_long("Not resumed: RetroArch's automatic state is from another of its GBA cores. To share states, set RetroArch's GBA core to mGBA.");
		} else if (stateLoadBios == BIOS_REFUSAL_STATE_HAS_BIOS) {
			ui_osd_long("Not resumed: the automatic state was saved with a GBA BIOS (RetroArch's core had one). To use your own, see Settings > System > GBA BIOS.");
		} else if (stateLoadBios == BIOS_REFUSAL_CORE_HAS_BIOS) {
			ui_osd_long("Not resumed: the automatic state was saved without a GBA BIOS, and GBA DS is set to use yours. Switch it off in Settings > System > GBA BIOS.");
		}
	} else {
		ui_osd("Tap the screen or press HOME for the menu");
	}
	if (cfg->barPinned) barOpen = true, barSince = SDL_GetTicks(); /* (pinned: up from the start) */
	rewind_setup();

	/* The device runs at a fixed rate. The GBA's own rate follows the game's
	 * sound settings (32768 Hz at power-on, 65536 Hz once Zero Mission is
	 * running...) and can change at any moment, so samples go through a
	 * converter that is rebuilt whenever it does. Opening the device at the
	 * rate of the moment made resumed games starve and fresh boots overflow. */
	SDL_AudioSpec want = { 0 }, have;
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	/* GBADS_AUDIO_SAMPLES: testing only (SDL's dummy driver keeps exact time
	 * only when the buffer is a whole number of milliseconds, e.g. 480) */
	want.samples = getenv("GBADS_AUDIO_SAMPLES") ? atoi(getenv("GBADS_AUDIO_SAMPLES")) : 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#if !defined(__wasi__) && !defined(__ANDROID__)
	if (!audio && getenv("AUDIODEV")) {
		/* the device launch.sh asked for (the RG DS Plus's Stock Linux OS: ALSA's pulse)
		 * won't open: the driver's own default instead */
		fprintf(stderr, "no audio on %s (%s): the default device instead\n", getenv("AUDIODEV"), SDL_GetError());
		unsetenv("AUDIODEV");
		SDL_AudioQuit();
		SDL_AudioInit(NULL);
		audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	}
#endif
#ifndef __wasi__
	if (!audio) {
		/* (a player on ROCKNIXDS heard nothing: the driver SDL picked may
		 * not reach the sound server; the others, in turn) */
		fprintf(stderr, "no audio with %s: %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "no driver", SDL_GetError());
		static const char* const tryDrivers[] = { "pipewire", "pulseaudio", "alsa" };
		const char* first = SDL_GetCurrentAudioDriver();
		for (int i = 0; i < 3 && !audio; ++i) {
			if (first && !strcmp(first, tryDrivers[i])) continue;
			SDL_AudioQuit();
			if (SDL_AudioInit(tryDrivers[i]) != 0) continue;
			audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
			if (!audio) fprintf(stderr, "no audio with %s: %s\n", tryDrivers[i], SDL_GetError());
		}
		if (!audio) SDL_AudioQuit(), SDL_AudioInit(NULL);
	}
#endif
	if (audio) {
		SDL_PauseAudioDevice(audio, 0);
		fprintf(stderr, "audio: %s%s%s, %d Hz, %d channels, %d samples\n", SDL_GetCurrentAudioDriver(), getenv("AUDIODEV") ? " on " : "",
		        getenv("AUDIODEV") ? getenv("AUDIODEV") : "", have.freq, have.channels, have.samples);
	} else {
		fprintf(stderr, "no audio: %s\n", SDL_GetError());
	}
	SDL_AudioStream* audioConv = NULL;
	int convFrom = 0;
	/* The sound delay in use: the setting, raised a step when the sound keeps
	 * running dry (audio_auto; only for this run, the setting is left alone) */
	int audioMs = cfg->audioLatency;
	int targetQueued = want.freq * audioMs / 1000;
	uint64_t audioQuietUntil = 0; /* no dry-queue counting before this frame: the sound was being started again */
	uint64_t dryWindowStart = 0;
	int dryQueueBefore = 1, dryEvents = 0;
	/* testing only: GBADS_TEST_STARVE=1 throws away the sound of 20 frames in every 100 (after frame 200) */
	const bool testStarve = getenv("GBADS_TEST_STARVE") && atoi(getenv("GBADS_TEST_STARVE"));

	static int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	/* the room key as it is now, and for how many frames it's held */
	uint32_t seenRoomKey = 0;
	int seenRoomFor = 0;
	int sinceMapRequest = 1 << 20;
	int lastFails = 0;
	bool forceMap = true, wasDark = false;
	long refreshAt = -1;
	hotkeys_setup();
	const int modBtn = hkMod;
	/* SELECT or START as the hotkey: a GBA button, held back from the game
	 * while it's down and handed over if it comes up with nothing pressed */
	const bool modIsGame = modBtn == B_SELECT || modBtn == B_START;
	bool comboUsed = false, ffModUsed = false; /* (ffModUsed: hotkey + R2 fast-forwarded) */
	int comboHold = -1, comboFrames = 0; /* menu combo: the button held back, frames since */
	int comboPulse[2] = { 0, 0 };        /* SELECT, START handed to the game late */
	int modPulse = 0; /* the hotkey button (SELECT or START), tapped alone: handed to the game */
	unsigned ffPhase = 0;
	bool noPace = getenv("GBADS_NOPACE") != NULL;
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	pacedFallback = false; /* (a new game: matched again, if it's on) */
	pacing_update();
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

#ifdef __ANDROID__
	/* a target well under the frame time: the system clocks the CPU so each
	 * frame's work takes about this long, leaving room for heavier frames
	 * (with the full 16.7 ms it clocked down until frames barely fit; with
	 * 65% the average was 11 ms and uneven frames went to 17-21 ms) */
	android_perf_init((int64_t) (frameSec * 0.5 * 1e9));
	android_set_pinning(cfg->pinCores);
	android_pin_game_thread();
#endif
	while (!done) {
		{
			/* the last iteration, if it was a stall (issue #50) */
			Uint64 t = SDL_GetPerformanceCounter();
			const double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			const LoopMarks* p = &loopCur;
			int queueMs = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4 * 1000 / want.freq) : 0;
			double gap = p->valid ? (double) (t - p->top) * msPer : 0;
			if (gap > STALL_MS && gap < 5000) {
				++stallCount;
				if (gap > stallWorst) stallWorst = gap;
				if (stallLogged < STALL_LINES) {
					++stallLogged;
					char keptNote[96] = "";
					if (p->kept) snprintf(keptNote, sizeof(keptNote), "; the game and its sound went on meanwhile (%d frame(s))", p->kept);
					fprintf(stderr,
					        "frame %llu: stall: %.0f ms between frames: waiting for the frame buffer %.1f, input and menu %.1f, emulating %.1f "
					        "(sound hand-off %.1f), second screen %.1f, drawing %.1f (top screen %.1f, bottom %.1f), after drawing %.1f, "
					        "sleeping %.1f (of %.1f asked); ran %.1f ms of it, preempted %ld time(s); sound queue %d -> %d ms; "
					        "ended at %.3f s on the system clock (as the kernel's and the journal's)%s\n",
					        p->frame, gap, (double) (p->synced - p->top) * msPer, (double) (p->emu0 - p->synced) * msPer,
					        (double) (p->emu1 - p->emu0) * msPer, p->audio, (double) (p->draw0 - p->emu1) * msPer,
					        (double) (p->draw1 - p->draw0) * msPer, p->presentTop, p->presentBottom, (double) (p->sleep0 - p->draw1) * msPer,
					        (double) (p->end - p->sleep0) * msPer, p->sleepWant * 1000.0, thread_cpu_ms() - p->cpuMs,
					        thread_preemptions() - p->preempt, p->queueMs, queueMs, mono_seconds(), p->kept ? keptNote : "");
				}
			}
			loopCur = (LoopMarks) { .top = t, .cpuMs = thread_cpu_ms(), .preempt = thread_preemptions(), .queueMs = queueMs, .frame = (unsigned long long) frameCount };
		}
		ahead_sync(); /* the core is the loop's again */
		loopCur.synced = SDL_GetPerformanceCounter();
		screen_restore_apply();
		bug_tick();
		Uint64 workT0 = SDL_GetPerformanceCounter(); /* this frame's work, without the wait at the end */
		long preempt0 = thread_preemptions();
		double cpuMs0 = thread_cpu_ms();
		if (((!logoTex[0] && !logoTex[1]) || logoProvisional) && logo_generation() != logoGen) {
			/* a logo finished extracting: the game's, or the one a scraped
			 * logo takes its size from (GEN-11) */
			logoGen = logo_generation();
			logo_refresh();
		}
		{
			/* back from sleep (frames stopped for over 2 seconds) or the
			 * renderer reset: the textures again */
			Uint32 nowT = SDL_GetTicks();
			bool reset = ui_render_reset();
			if (reset || (lastLoop && nowT - lastLoop > 2000)) {
				fprintf(stderr, "frame %llu: %s, pictures rebuilt\n", (unsigned long long) frameCount,
				        reset ? "renderer reset" : "back after a pause");
				rebuild_textures();
			}
			lastLoop = SDL_GetTicks();
		}
		ui_stick_dpad(cfg->leftStickDpad || menuOpen || barShown > 0); /* (#57: off for the game only) */
		if (!ui_events()) {
			exitTo = GAME_QUIT_APP;
			break;
		}
		ui_tick();
		ui_read_buttons();
		{
			const char* ld = ui_script_load();
			if (ld && !state_load(ld)) fprintf(stderr, "could not load %s\n", ld);
		}

		/* ---- pointer: any tap opens the menu; taps in the menu press buttons */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.kind == 1) hint_dismiss(); /* (any touch puts the hint away) */
			if (!menuOpen) {
				/* the quick bar: up from the bottom edge of the touchscreen
				 * brings it up, down puts it away; a tap still opens the
				 * menu (from the bottom edge or with the bar up: once the
				 * finger lifts without having swiped) */
				int ts = ui_touch_screen();
				float u = ui_px(ts);
				if (p.kind == 1 && toggle_btn_hit(p.screen, p.x, p.y)) {
					/* the toggle button: its tap is its own, before "a tap opens the menu" */
					tbDown = true, tbGX = p.x, tbGY = p.y;
					barGesture = G_NONE;
					continue;
				}
				if (tbDown) {
					if (p.kind == 3 && p.screen == ts && p.y - tbGY <= -u * 10) {
						/* a swipe up that began on it: the quick bar */
						tbDown = false;
						barOpen = true;
						barSince = SDL_GetTicks();
						barGesture = G_DONE;
					} else if (p.kind == 2) {
						bool in = toggle_btn_hit(p.screen, p.x, p.y);
						tbDown = false;
						if (in) toggle_press();
					}
					continue;
				}
				if (p.kind == 1) {
					barGesture = G_NONE;
					barDownOpen = barOpen;
					if (p.screen == ts && barOpen) {
						SDL_Rect pin = bar_pin(ts);
						SDL_Point pt = { p.x, p.y };
						barSince = SDL_GetTicks();
						barBtn = bar_button_at(ts, p.x, p.y);
						barGesture = barBtn >= 0 ? G_BTN : SDL_PointInRect(&pt, &pin) ? G_PIN : (ctl_tap(ts, p.x, p.y) || sun_tap(ts, p.x, p.y)) ? G_DONE : G_TRACK;
						barGX = p.x, barGY = p.y;
						continue;
					}
					if (p.screen == ts && p.y >= screens[ts].rect.h - u * 36) {
						barGesture = G_TRACK;
						barGX = p.x, barGY = p.y;
						continue;
					}
					open_menu();
					ignorePointerUp = true;
				} else if (p.kind == 3 && (barGesture == G_TRACK || barGesture == G_BTN || barGesture == G_PIN) && p.screen == ts) {
					int dy = p.y - barGY;
					if (!barOpen && dy <= -u * 10) {
						barOpen = true;
						barSince = SDL_GetTicks();
						barGesture = G_DONE;
						if (debug) fprintf(stderr, "frame %llu: quick bar up\n", (unsigned long long) frameCount);
					} else if (barOpen && dy >= u * 10) {
						bar_close();
						barGesture = G_DONE;
					}
				} else if (p.kind == 2) {
					int g = barGesture;
					barGesture = G_NONE;
					if (g == G_BTN && p.screen == ts && bar_button_at(ts, p.x, p.y) == barBtn) {
						int bb = barBtn;
						barSince = SDL_GetTicks();
						if (bb != BB_EXIT) barExitArmed = 0;
						if (bb == BB_SAVE) {
							bool ok = state_save(gp.quick);
							ui_osd(ok ? "State saved." : "Save failed.");
							if (ok) barLit = bb, barLitAt = barSince;
						} else if (bb == BB_LOAD) {
							if (!file_exists(gp.quick)) ui_osd("No quick state saved yet.");
							else if (state_load(gp.quick)) { if (!stateWarned) ui_osd("State loaded."); forceMap = true, barLit = bb, barLitAt = barSince; }
							else ui_osd(load_failed_osd());
						} else if (bb == BB_SETTINGS) {
							open_menu();
							ignorePointerUp = false;
						} else if (bb == BB_BUG) {
							char what[200];
							/* the moment now, then the game paused for a note */
							if (bug_report(what, sizeof(what))) barLit = bb, barLitAt = barSince, open_menu(), note_open(true);
							else ui_osd("%s", what);
						} else if (bb == BB_EXIT) {
							if (!barExitArmed) {
								barExitArmed = barSince;
								ui_osd("Tap again to close GBA DS.");
							} else {
								exitTo = GAME_QUIT_APP;
								done = true;
							}
						}
					} else if (g == G_PIN && p.screen == ts) {
						SDL_Rect pin = bar_pin(ts);
						SDL_Point pt = { p.x, p.y };
						if (SDL_PointInRect(&pt, &pin)) {
							cfg->barPinned = !cfg->barPinned;
							barSince = SDL_GetTicks();
							save_prefs();
							ui_osd(cfg->barPinned ? "Pinned: the bar stays up." : "Unpinned: the bar hides after 5 seconds.");
						}
					} else if (g == G_TRACK) {
						/* a tap, not a swipe: with the bar up, it goes away (pinned,
						 * it stays, and a tap outside it opens the menu);
						 * otherwise the menu */
						SDL_Rect b = bar_rect(ts);
						SDL_Point pt = { barGX, barGY };
						if (barDownOpen && !cfg->barPinned) {
							bar_close();
						} else if (!barDownOpen || !SDL_PointInRect(&pt, &b)) {
							open_menu();
							ignorePointerUp = false;
						}
					}
				}
				continue;
			}
			if (p.screen != ui_touch_screen()) continue;
			if (achOpen) {
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						close_ach(); /* Back */
						ignorePointerUp = true;
						continue;
					}
					achPressed = true;
					achPressY = p.y;
					achPressScroll = ra_list_scroll_pos();
				} else if (p.kind == 3 && achPressed) {
					ra_list_set_scroll(p.screen, achPressScroll - (p.y - achPressY));
				} else if (p.kind == 2) {
					achPressed = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (noteOpen) {
				int r, c;
				if (p.kind == 1) {
					ignorePointerUp = false;
					notePressKey = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						note_done(false); /* Skip */
						ignorePointerUp = true;
						continue;
					}
					if (note_key_at(p.screen, p.x, p.y, &r, &c)) notePressKey = r * 16 + c, noteRow = r, noteCol = c;
				} else if (p.kind == 2) {
					if (!ignorePointerUp && note_key_at(p.screen, p.x, p.y, &r, &c) && r * 16 + c == notePressKey) note_press(r, c);
					notePressKey = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (patchesOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						patchesOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					patchPressRow = patch_row_at(p.screen, p.x, p.y);
					patchPressY = p.y;
					patchPressScroll = patchScroll;
					patchDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && patchPressRow != -2) {
					if (abs(p.y - patchPressY) > px * 4) patchDragging = true;
					if (patchDragging) {
						CheatGeom g = cheat_geom(p.screen);
						patchScroll = SDL_clamp(patchPressScroll - (p.y - patchPressY), 0, patch_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = patch_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !patchDragging && row >= 0 && row == patchPressRow && patchMoving) {
						/* moving one: where it goes (another that's on), else put it down */
						int to = patchRows[row].kind == PR_PATCH ? patchRows[row].order : -1;
						int from = patchRows[patchSel].order;
						if (to >= 0 && to != from) {
							for (int k = 0; k < abs(to - from); ++k) patch_shift(to > from ? 1 : -1);
						} else {
							patch_pick_up();
						}
					} else if (!ignorePointerUp && !patchDragging && row >= 0 && row == patchPressRow &&
					           !patch_row_text_only(patchRows[row].kind)) {
						if (row != patchSel) patchArmed = -1, patchNote[0] = 0;
						patchSel = row;
						if (patch_on_number(p.screen, row, p.x)) {
							patch_pick_up(); /* (its number: picked up to move) */
						} else if (patch_choose(row, false)) {
							exitTo = GAME_RELOAD;
							done = true;
						}
					}
					patchPressRow = -2;
					patchDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (hexOpen) {
				int r, c;
				if (p.kind == 1) {
					ignorePointerUp = false;
					hexPress = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						hexOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					if (hex_key_at(p.screen, p.x, p.y, &r, &c)) hexPress = r * 16 + c, hexRow = r, hexCol = c;
				} else if (p.kind == 2) {
					if (!ignorePointerUp && hex_key_at(p.screen, p.x, p.y, &r, &c) && r * 16 + c == hexPress) hex_press(r, c);
					hexPress = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (pickWhich >= 0) {
				PickGeom pg = pick_geom(p.screen);
				if (p.kind == 1) {
					ignorePointerUp = false;
					if (in_rect(pg.sq, p.x, p.y)) pickDrag = 1;
					else if (in_rect((SDL_Rect) { pg.hue.x - pg.hue.w / 2, pg.hue.y, pg.hue.w * 2, pg.hue.h }, p.x, p.y)) pickDrag = 2;
					else {
						for (int i = 0; i < 8; ++i)
							if (in_rect(pg.presets[i], p.x, p.y)) rgb_hsv(pickPresets[pickWhich][i], &pickH, &pickS, &pickV), pickPreset = i;
						if (in_rect(pg.hex, p.x, p.y)) hex_open();
						else if (in_rect(pg.cancel, p.x, p.y)) pick_close(false);
						else if (in_rect(pg.ok, p.x, p.y)) pick_close(true);
						ignorePointerUp = true;
					}
					pick_touch(p.screen, p.x, p.y);
				} else if (p.kind == 3) {
					pick_touch(p.screen, p.x, p.y);
				} else if (p.kind == 2) {
					pick_touch(p.screen, p.x, p.y);
					pickDrag = 0;
					ignorePointerUp = false;
				}
				continue;
			}
			if (ovlOpen) {
				if (p.kind == 1) {
					ignorePointerUp = false;
					ovlPress = -1;
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						ovlOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					int rows[8], n = ovl_rows(rows);
					for (int i = 0; i < n; ++i)
						if (in_rect(ovl_row_rect(p.screen, i), p.x, p.y)) ovlPress = i, ovlSel = i;
				} else if (p.kind == 2) {
					int rows[8], n = ovl_rows(rows);
					for (int i = 0; i < n; ++i)
						if (!ignorePointerUp && i == ovlPress && in_rect(ovl_row_rect(p.screen, i), p.x, p.y)) ovl_activate(i, 1);
					ovlPress = -1;
					ignorePointerUp = false;
				}
				continue;
			}
			if (cheatsOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						cheatsOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					cheatPressRow = cheat_row_at(p.screen, p.x, p.y);
					cheatPressY = p.y;
					cheatPressScroll = cheatScroll;
					cheatDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && cheatPressRow != -2) {
					if (abs(p.y - cheatPressY) > px * 4) cheatDragging = true;
					if (cheatDragging) {
						CheatGeom g = cheat_geom(p.screen);
						cheatScroll = SDL_clamp(cheatPressScroll - (p.y - cheatPressY), 0, cheat_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = cheat_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !cheatDragging && row >= 0 && row == cheatPressRow && cheatRows[row] >= 0) {
						cheatSel = row;
						toggle_cheat(row);
					}
					cheatPressRow = -2;
					cheatDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (p.kind == 1) {
				int t = tab_at(p.screen, p.x, p.y, N_PAGES);
				if (t >= 0) {
					if (t != curPage) set_page(t);
					ignorePointerUp = true;
					continue;
				}
			}
			if (page_is_rows(curPage)) {
				rows_touch(&p);
				continue;
			}
			int it = item_at(p.screen, p.x, p.y);
			if (p.kind == 1) {
				ignorePointerUp = false;
				menuPressed = it, rowDragY = p.y;
				if (it >= 0) menuSel = it;
			} else if (p.kind == 3 && nMenuRows > ROW_VIS && menuPressed >= 0 && abs(p.y - rowDragY) >= menu_geom(p.screen).h + menu_geom(p.screen).gap) {
				/* a finger dragged up or down moves the list a row at a time: no longer a tap */
				itemTop = SDL_clamp(itemTop + (p.y < rowDragY ? 1 : -1), 0, nMenuRows - ROW_VIS);
				rowDragY = p.y, menuPressed = -1, ignorePointerUp = true;
			} else if (p.kind == 3 && menuPressed >= 0 && it >= 0) {
				menuSel = it;
			} else if (p.kind == 2) {
				if (!ignorePointerUp && it >= 0 && it == menuPressed && menuItems[it] == M_SLOT) {
					/* (the slot row: its left third goes back a slot, the rest forward) */
					SDL_Rect r = item_rect(p.screen, it);
					slot_step(p.x < r.x + r.w / 3 ? -1 : 1);
				} else if (!ignorePointerUp && it >= 0 && it == menuPressed && activate(menuItems[it], &exitTo)) done = true;
				menuPressed = -1;
				ignorePointerUp = false;
			}
		}
		if (done) break;

		/* ---- the menu combo (menu_combo = select+start; on Android, where HOME
		 * belongs to the system): pressing both opens the menu. The first of
		 * the two is held back from the game for a few frames, so that a combo
		 * doesn't also press SELECT (a map in some games) or START (pause);
		 * if the other doesn't follow, it goes to the game after all. */
		bool comboOpened = false;
		if (!menuOpen && !strcmp(cfg->menuCombo, "select+start")) {
			if (comboHold < 0) {
				if (btn_pressed(B_SELECT) && btn_pressed(B_START)) comboHold = B_START, comboFrames = 0;
				else if (btn_pressed(B_SELECT) && !btnNow[B_START]) comboHold = B_SELECT, comboFrames = 0;
				else if (btn_pressed(B_START) && !btnNow[B_SELECT]) comboHold = B_START, comboFrames = 0;
			}
			if (comboHold >= 0) {
				int other = comboHold == B_SELECT ? B_START : B_SELECT;
				if (btnNow[comboHold] && btnNow[other]) {
					open_menu();
					comboOpened = true;
					comboUsed = true; /* not a hotkey, nor a plain SELECT for the game */
					comboHold = -1;
				} else if (!btnNow[comboHold]) {
					comboPulse[comboHold == B_START] = 4; /* a quick tap: give it to the game now */
					comboHold = -1;
				} else if (++comboFrames > 5) {
					comboHold = -1; /* just held: the game gets it from here */
				}
			}
		} else {
			comboHold = -1;
		}

		/* ---- HOME (or the hotkey modifier) alone toggles the menu; with a button it's a hotkey */
		bool mod = btnNow[modBtn];
		/* Hotkey + R2 (R2 no action's button): fast-forward, as R2 alone,
		 * with the game's buttons going through, so text can still be
		 * skipped (RetroArch's habit; issue #44). No hotkeys meanwhile,
		 * and letting go of the hotkey doesn't open the menu. */
		bool ffMod = false;
		if (mod && !menuOpen && cfg->fastForward && btnNow[B_R2] && !(cfg->rewind && btnNow[B_L2])) {
			ffMod = true;
			for (int i = 0; i < HK_COUNT; ++i) ffMod = ffMod && hkBtn[i] != B_R2;
			if (ffMod) comboUsed = ffModUsed = true;
		}
		if (mod && !menuOpen && !ffMod) {
			for (int i = 0; i < HK_COUNT; ++i) {
				if (hkBtn[i] < 0 || !btn_pressed(hkBtn[i])) continue;
				if (i == HK_TOGGLE && !cfg->toggle) continue; /* (off: HOME + its button is as it always was) */
				comboUsed = true;
				if (debug) fprintf(stderr, "hotkey: %s\n", hotkeyNames[i]);
				switch (i) {
				case HK_QUIT: exitTo = GAME_QUIT_APP, done = true; break;
				/* (Close game: back to the game list; started straight into a
				 * game, by a launcher or --rom, there is none: it closes GBA DS) */
				case HK_GAME: exitTo = fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP, done = true; break;
				/* (the quick state, as the quick bar's buttons) */
				case HK_SAVE: ui_osd(state_save(gp.quick) ? "State saved." : "Save failed."); break;
				case HK_LOAD:
					if (!file_exists(gp.quick)) ui_osd("No quick state saved yet.");
					else if (state_load(gp.quick)) { if (!stateWarned) ui_osd("State loaded."); forceMap = true; }
					else ui_osd(load_failed_osd());
					break;
				case HK_BAR: bar_toggle(); break;
				case HK_SWAP: swap_screens(); break;
				case HK_TOGGLE: toggle_press(); break;
				case HK_SHADER:
					set_shader(cfg->shader != SH_OFF ? SH_OFF : cfg->shaderLast > SH_OFF && cfg->shaderLast < SH_COUNT ? cfg->shaderLast : SH_GRID);
					ui_osd("%s", shader_label(cfg->shader));
					break;
				}
			}
		}
		if (btn_released(B_HOME) && !(modBtn == B_HOME && comboUsed)) {
			if (menuOpen) menuOpen = false;
			else open_menu();
		}
		if (btn_released(modBtn)) {
			if (comboUsed && !ffModUsed) suppress_held(); /* (not after hotkey + R2: the game had them) */
			else if (!menuOpen && modIsGame) modPulse = 4; /* tapped alone: the game gets it */
			else if (!menuOpen && modBtn == B_L3) bar_toggle();
			comboUsed = ffModUsed = false;
		}
		if (done) break;

		if (menuOpen && achOpen) {
			int scr = ui_touch_screen();
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) ra_list_scroll(scr, (k ? 1 : -1) * ra_list_step(scr));
			}
			int page = ach_area(scr).h * 3 / 4;
			if (btn_pressed(B_L)) ra_list_scroll(scr, -page);
			if (btn_pressed(B_R)) ra_list_scroll(scr, page);
			if (btn_pressed(B_B)) close_ach();
			if (btn_pressed(B_START)) close_ach(), menuOpen = false;
		} else if (menuOpen && noteOpen) {
			/* D-pad (repeating while held) and A on the keys; B deletes (or,
			 * with nothing typed, skips), X a space, SELECT Shift, START done */
			static int held[4];
			const int dirs[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
			for (int k = 0; k < 4; ++k) {
				held[k] = btnNow[dirs[k]] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0))
					note_move(ui_touch_screen(), k == 0 ? -1 : k == 1 ? 1 : 0, k == 2 ? -1 : k == 3 ? 1 : 0);
			}
			if (btn_pressed(B_A)) note_press(noteRow, noteCol);
			else if (btn_pressed(B_B)) {
				if (noteLen) noteText[--noteLen] = 0;
				else note_done(false);
			} else if (btn_pressed(B_X)) note_type(" ");
			else if (btn_pressed(B_SELECT)) noteShift = !noteShift;
			else if (btn_pressed(B_START)) note_done(true);
		} else if (menuOpen && patchesOpen) {
			static int pheld[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				pheld[k] = btnNow[b] ? pheld[k] + 1 : 0;
				if (pheld[k] == 1 || (pheld[k] > 20 && pheld[k] % 4 == 0)) patch_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (!patchMoving && btn_pressed(B_L)) for (int k = 0; k < page; ++k) patch_move(-1);
			if (!patchMoving && btn_pressed(B_R)) for (int k = 0; k < page; ++k) patch_move(1);
			bool later = patchArmed >= 0 && btn_pressed(B_Y);
			if (btn_pressed(B_X)) patch_pick_up(); /* (pick a patch that's on up to move it, or put it down) */
			else if ((btn_pressed(B_A) || later) && patch_choose(patchSel, later)) {
				exitTo = GAME_RELOAD;
				done = true;
			}
			if (btn_pressed(B_B)) {
				if (patchMoving) patch_pick_up();
				else if (patchArmed >= 0) patchArmed = -1, patchNote[0] = 0;
				else patchesOpen = false;
			}
			if (btn_pressed(B_START)) patchMoving = false, patchesOpen = false, menuOpen = false;
		} else if (menuOpen && hexOpen) {
			/* D-pad (repeating) and A on the keys; B deletes (or, with nothing typed, goes back), START Done */
			static int held[4];
			const int dirs[4] = { B_UP, B_DOWN, B_LEFT, B_RIGHT };
			for (int k = 0; k < 4; ++k) {
				held[k] = btnNow[dirs[k]] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) hex_move(k == 0 ? -1 : k == 1 ? 1 : 0, k == 2 ? -1 : k == 3 ? 1 : 0);
			}
			if (btn_pressed(B_A)) hex_press(hexRow, hexCol);
			else if (btn_pressed(B_B)) {
				if (hexLen) hexText[--hexLen] = 0;
				else hexOpen = false;
			} else if (btn_pressed(B_START)) hex_done();
		} else if (menuOpen && pickWhich >= 0) {
			/* D-pad: saturation (across) and brightness (up / down); L1 / R1 the
			 * hue; held, a step at a time, then faster. X the next preset, Y
			 * the hex keypad, A OK, B Cancel, START OK and back to the game. */
			static int held[6];
			const int keys[6] = { B_LEFT, B_RIGHT, B_UP, B_DOWN, B_L, B_R };
			for (int k = 0; k < 6; ++k) {
				held[k] = btnNow[keys[k]] ? held[k] + 1 : 0;
				if (held[k] != 1 && held[k] <= 15) continue;
				float step = held[k] == 1 ? 0.01f : held[k] > 60 ? 0.03f : 0.01f;
				if (k == 0) pickS = SDL_max(0.0f, pickS - step);
				if (k == 1) pickS = SDL_min(1.0f, pickS + step);
				if (k == 2) pickV = SDL_min(1.0f, pickV + step);
				if (k == 3) pickV = SDL_max(0.0f, pickV - step);
				if (k >= 4) pickH = fmodf(pickH + (k == 4 ? -1 : 1) * step * 200 + 360, 360);
			}
			if (btn_pressed(B_X)) pickPreset = (pickPreset + 1) % 8, rgb_hsv(pickPresets[pickWhich][pickPreset], &pickH, &pickS, &pickV);
			else if (btn_pressed(B_Y)) hex_open();
			else if (btn_pressed(B_A)) pick_close(true);
			else if (btn_pressed(B_B)) pick_close(false);
			else if (btn_pressed(B_START)) pick_close(true), ovl_close_all(), menuOpen = false;
		} else if (menuOpen && ovlOpen) {
			int rows[8], n = ovl_rows(rows);
			if (btn_pressed(B_DOWN)) ovlSel = SDL_min(n - 1, ovlSel + 1);
			if (btn_pressed(B_UP)) ovlSel = SDL_max(0, ovlSel - 1);
			bool cycles = rows[ovlSel] == OR_MODE || rows[ovlSel] == OR_SCREENS || rows[ovlSel] == OR_LOGO;
			if (cycles && btn_pressed(B_LEFT)) ovl_activate(ovlSel, -1);
			else if (cycles && btn_pressed(B_RIGHT)) ovl_activate(ovlSel, 1);
			else if (btn_pressed(B_A)) ovl_activate(ovlSel, 1);
			else if (btn_pressed(B_B)) ovlOpen = false;
			else if (btn_pressed(B_START)) ovl_close_all(), menuOpen = false;
		} else if (menuOpen && cheatsOpen) {
			/* D-pad repeats while held */
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) cheat_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (btn_pressed(B_L)) for (int k = 0; k < page; ++k) cheat_move(-1);
			if (btn_pressed(B_R)) for (int k = 0; k < page; ++k) cheat_move(1);
			if (btn_pressed(B_A)) toggle_cheat(cheatSel);
			if (btn_pressed(B_B)) cheatsOpen = false;
			if (btn_pressed(B_START)) cheatsOpen = false, menuOpen = false;
		} else if (menuOpen) {
			if (!comboOpened) { /* (START just opened it: not also a close) */
				if (btn_pressed(B_L) && curPage > 0) set_page(curPage - 1);
				if (btn_pressed(B_R) && curPage < N_PAGES - 1) set_page(curPage + 1);
				if (page_is_rows(curPage)) {
					rows_input();
					if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				} else {
				if (btn_pressed(B_DOWN)) menu_move(1);
				if (btn_pressed(B_UP)) menu_move(-1);
				if (menuItems[menuSel] == M_SLOT && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) slot_step(btn_pressed(B_LEFT) ? -1 : 1);
				if ((btn_pressed(B_LEFT) || btn_pressed(B_RIGHT)) && itemSide[menuSel] >= 0) {
					/* the other half of the row */
					int to = btn_pressed(B_LEFT) ? 0 : 1;
					for (int i = 0; i < nMenuItems; ++i)
						if (itemRow[i] == itemRow[menuSel] && itemSide[i] == to) menuSel = i;
				}
				if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				else if (btn_pressed(B_A) && activate(menuItems[menuSel], &exitTo)) break;
				}
			}
			if (menuOpen && !wasMenu && audio) {
				SDL_PauseAudioDevice(audio, 1);
				SDL_ClearQueuedAudio(audio);
			}
		}
		if (wasMenu && !menuOpen) {
			if (noteOpen) note_done(true); /* (closed some other way: saved with what's typed) */
			suppress_held();
			if (cfg->barPinned) barOpen = true, barSince = SDL_GetTicks(); /* (pinned: back after the menu) */
			if (audio) SDL_PauseAudioDevice(audio, 0);
			next = SDL_GetPerformanceCounter() / freq;
			fpsT0 = next;
			fpsFrames = 0;
			pacedMisses = pacedMissSecs = 0; /* (the menu's time isn't the game's) */
			forceMap = true;
			audioQuietUntil = frameCount + 150; /* (the queue was cleared for the menu: refilling) */
		}
		wasMenu = menuOpen;

		if (menuOpen) {
			ra_idle();
			ui_clear();
			draw_game();
			draw_menu();
			ui_present();
			SDL_Delay(16);
			next = SDL_GetPerformanceCounter() / freq;
			continue;
		}

		if (btn_pressed(B_L3) && modBtn != B_L3) bar_toggle(); /* (L3: the quick menu, as a swipe up) */

		/* ---- emulate */
		uint32_t keys = 0;
		for (int b = 0; b < B_COUNT; ++b) {
			if (!btnNow[b]) suppress[b] = false; /* released: counts again */
		}
		if (modIsGame && btnNow[modBtn]) {
			/* the hotkey (SELECT or START) held: the actions' buttons are
			 * kept from the game until released, the rest go through */
			for (int i = 0; i < HK_COUNT; ++i) {
				if (hkBtn[i] >= 0 && btnNow[hkBtn[i]]) suppress[hkBtn[i]] = true;
			}
		}
		if (!btnNow[modBtn] || modIsGame || ffMod) {
			/* the SNES layout only where it applies (Super Mario Advance 2: in
			 * a level; on the world map R switches Mario and Luigi, and only A
			 * enters a level) */
			bool snes = ctl_four() && ctl_snes() && snesHere;
			if (snes) {
				/* by position, as the profile says (Super Mario Advance 2: B
				 * bottom jumps, Y left runs, A right and X top spin jump) */
				const typeof(ctlProf->controls.snes)* sn = &ctlProf->controls.snes;
				int right = cfg->abSwap ? B_B : B_A, bottom = cfg->abSwap ? B_A : B_B;
				if (HELD(right)) keys |= sn->right;
				if (HELD(bottom)) keys |= sn->bottom;
				if (HELD(B_X)) keys |= sn->top;
				if (HELD(B_Y)) keys |= sn->left;
			} else if (ctl_four() && !ctl_snes()) {
				/* by position: top A, bottom B, left L, right R (on the RG DS
				 * the top face button arrives as X, the left one as Y) */
				int right = cfg->abSwap ? B_B : B_A, bottom = cfg->abSwap ? B_A : B_B;
				if (HELD(B_X)) keys |= K_A;
				if (HELD(bottom)) keys |= K_B;
				if (HELD(B_Y)) keys |= K_L;
				if (HELD(right)) keys |= K_R;
			} else {
				keys |= fb_keys(true); /* the player's choices (GEN-10), else ab_swap's */
			}
			if (HELD(B_START) && modBtn != B_START && comboHold != B_START) keys |= K_START;
			if (HELD(B_SELECT) && modBtn != B_SELECT && comboHold != B_SELECT) keys |= K_SELECT;
			if (snes) {
				/* both shoulders are GBA L, panning the camera their own way */
				if (HELD(B_L)) keys |= K_L | KX_PAN_L;
				else if (HELD(B_R)) keys |= K_L | KX_PAN_R;
			} else {
				keys |= fb_keys(false); /* L, R and the D-pad, as chosen (GEN-10) */
				static bool told;
				if (!told && ctl_four() && ctl_snes() && (HELD(B_L) || HELD(B_R))) {
					told = true; /* (for reports: once) */
					fprintf(stderr, "controls: SNES chosen, but the GBA layout here (game mode %02x, paused %02x)\n",
					        emu_read8(&emu, ctlProf->controls.snes.when[0].addr), emu_read8(&emu, ctlProf->controls.snes.when[2].addr));
				}
			}
			if (snes) {
				if (HELD(B_UP)) keys |= K_UP;
				if (HELD(B_DOWN)) keys |= K_DOWN;
				if (HELD(B_LEFT)) keys |= K_LEFT;
				if (HELD(B_RIGHT)) keys |= K_RIGHT;
			}
		}
		if (modPulse > 0) keys |= modBtn == B_START ? K_START : K_SELECT, --modPulse;
		if (comboPulse[0] > 0) keys |= K_SELECT, --comboPulse[0];
		if (comboPulse[1] > 0) keys |= K_START, --comboPulse[1];
		{
			static uint32_t lastKeys;
			if (debug && keys != lastKeys) fprintf(stderr, "frame %llu: game keys %03x\n", (unsigned long long) frameCount, keys);
			lastKeys = keys;
		}

		bool rewinding = cfg->rewind && btnNow[B_L2];
		bool ff = cfg->fastForward && btnNow[B_R2] && !rewinding;
		ffBox = ff;
		/* Max (ff_speed 0): as many frames as fit in about 70% of a display
		 * frame (the rest for drawing it), up to FF_MAX_RUNS; the sound
		 * thinned out by the last count */
		const bool ffMax = ff && cfg->ffSpeed <= 0;
		static int maxRan = 4;
		int runs = ffMax ? FF_MAX_RUNS : ff ? cfg->ffSpeed : 1;
		int thin = ffMax ? maxRan : runs; /* every thin-th sound sample kept */
		/* Audio keeps flowing during fast-forward: every runs-th sample is kept,
		 * so the device gets real-time worth of (sped-up) sound. Starving it
		 * makes PulseAudio/PipeWire grow their buffers after each underrun,
		 * which is what made the lag worse every time. */
		int queuedNow = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4) : 0;
		bool dropAudio = queuedNow > targetQueued * 3; /* hard cap on drift */
		/* Health report (always on, every 5 s when something went wrong): audio
		 * underruns (the queue ran dry = a crackle), slow loop iterations, and
		 * how often the background map capture was running. */
		{
			static int underruns, slow, captureBusy, dropped, n;
			static int queueLo = 1 << 30, queueHi = -1, quietWindows; /* the queue's range this window (ms) */
			static long preemptT0 = -1;
			if (preemptT0 < 0) preemptT0 = thread_preemptions();
			static Uint64 lastIter;
			Uint64 nowT = SDL_GetPerformanceCounter();
			if (lastIter && !ff && (nowT - lastIter) * 1000 / SDL_GetPerformanceFrequency() > 25) ++slow;
			lastIter = nowT;
			if (audio && frameCount > 120 && queuedNow == 0 && !ff) ++underruns;
			/* The sound keeps running dry (a queue that had sound in it a frame ago and has
			 * none now, three times in about 10 s): the sound delay goes up a step. Not while
			 * fast-forwarding or rewinding, nor for 2.5 s after either or after the menu
			 * (the queue is being filled again then). */
			if (ff || rewinding || dropAudio) audioQuietUntil = frameCount + 150;
			if (audio && cfg->audioAuto && frameCount > 120 && frameCount >= audioQuietUntil) {
				if (queuedNow == 0 && dryQueueBefore > 0) {
					if (!dryEvents || frameCount - dryWindowStart > 600) dryWindowStart = frameCount, dryEvents = 0;
					if (++dryEvents >= 3) {
						static const int steps[] = { 32, 64, 96, 128, 192 };
						int to = audioMs;
						for (size_t k = 0; k < sizeof(steps) / sizeof(steps[0]); ++k)
							if (steps[k] > audioMs) { to = steps[k]; break; }
						if (to != audioMs) {
							fprintf(stderr, "frame %llu: the sound ran dry %d times in %.0f s: sound delay raised from %d to %d ms "
							                "for this run (audio_auto = 0 turns this off)\n",
							        (unsigned long long) frameCount, dryEvents, (double) (frameCount - dryWindowStart) / 60.0, audioMs, to);
							audioMs = to;
							targetQueued = want.freq * audioMs / 1000;
							ui_osd_long("Sound delay raised to %d ms: the sound kept breaking up", audioMs);
						}
						dryEvents = 0;
						audioQuietUntil = frameCount + 150; /* (the queue climbs to the new level first) */
					}
				}
				dryQueueBefore = queuedNow;
			} else {
				dryQueueBefore = 1;
			}
			if (audio && frameCount > 120 && !ff && !rewinding) {
				int qMs = queuedNow * 1000 / want.freq;
				if (qMs < queueLo) queueLo = qMs;
				if (qMs > queueHi) queueHi = qMs;
			}
			if (mc && mapcap_busy(mc)) ++captureBusy;
			if (dropAudio && !ff) ++dropped; /* (the queue over 3x its target: this frame's sound thrown away, a gap) */
			if (++n >= 300) {
#ifdef __ANDROID__
				const bool report = true; /* performance on Android devices is still being measured */
#else
				const bool report = underruns || slow || dropped || stallCount || keptTotal || debug;
#endif
				/* (all well: the line still comes every 30 s, for the queue's range) */
				bool show = report;
				if (!show && audio && ++quietWindows >= 6) show = true;
				if (show) quietWindows = 0;
				if (show) {
					double ms = 1000.0 / SDL_GetPerformanceFrequency() / n;
					fprintf(stderr, "frame %llu: last 5 s: %.1f fps, %d audio underrun(s), %d slow frame(s), map capture busy %d%%, "
					                "audio queue %d ms; per frame: emulating %.2f ms, drawing %.2f ms; game thread preempted %ld time(s)",
					        (unsigned long long) frameCount, fps, underruns, slow, captureBusy * 100 / n,
					        queuedNow * 1000 / want.freq, emuTicks * ms, drawTicks * ms, thread_preemptions() - preemptT0);
					if (queueHi >= 0) fprintf(stderr, "; audio queue between %d and %d ms (sound delay %d ms)", queueLo, queueHi, audioMs);
					if (stallCount) fprintf(stderr, "; %d stall(s) over %d ms, the longest %.0f ms", stallCount, STALL_MS, stallWorst);
					if (dropped) fprintf(stderr, "; sound thrown away on %d frame(s) (queue too long)", dropped);
					if (keptTotal) fprintf(stderr, "; %u frame(s) run while the screen held a frame up", keptTotal);
					fputc('\n', stderr);
				}
				underruns = slow = captureBusy = dropped = n = 0;
				queueLo = 1 << 30, queueHi = -1;
				stallCount = stallLogged = 0, stallWorst = 0;
				keptTotal = 0;
				preemptT0 = thread_preemptions();
				emuTicks = drawTicks = 0;
			}
		}
		if (debug && audio && frameCount % 30 == 0) {
			fprintf(stderr, "audio queue %d ms%s\n", queuedNow * 1000 / want.freq, ff ? " (ff)" : "");
		}
		Uint64 emuT0 = SDL_GetPerformanceCounter(), raT = 0, audT = 0;
		double emuCpu0 = thread_cpu_ms();
		if (rewinding) {
			/* Rewinding: each frame goes back one state of the buffer (two
			 * frames) and runs one frame to draw it, so it plays backwards at
			 * double speed; silent, no achievements. At the buffer's start
			 * the picture stays. */
			aheadReady = false; /* (a frame emulated ahead belongs to the present) */
			bug_reset();
			if (emu_rewind_step(&emu)) {
				emu_frame(&emu, 0);
				rewindBox = 1;
			} else {
				rewindBox = 2;
			}
			while (emu_audio_read(&emu, samples, 4096) > 0) {}
			rewindTick = 0;
			wasRewinding = true;
		} else if (wasRewinding) {
			wasRewinding = false;
			rewindBox = 0;
			forceMap = true; /* the map, as of where the rewind stopped */
		}
		int ran = 0;
		for (int i = 0; i < (rewinding ? 0 : runs); ++i) {
			if (ffMax && i > 0 && (SDL_GetPerformanceCounter() - emuT0) / freq > frameSec * 0.7) break;
			++ran;
			if (i == 0 && aheadReady) aheadReady = false; /* emulated by the worker while the last frame was shown */
			else run_frame(keys);
			Uint64 raT0 = SDL_GetPerformanceCounter();
			ra_frame();
			raT += SDL_GetPerformanceCounter() - raT0;
			Uint64 audT0 = SDL_GetPerformanceCounter();
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (!audio || dropAudio) continue;
				if (testStarve && frameCount > 200 && frameCount % 100 >= 20 && frameCount % 100 < 40) continue;
				if (thin > 1) {
					size_t outN = 0;
					for (size_t f = 0; f < got; ++f) {
						if (ffPhase++ % thin == 0) {
							samples[outN * 2] = samples[f * 2];
							samples[outN * 2 + 1] = samples[f * 2 + 1];
							++outN;
						}
					}
					got = outN;
				}
				if (!got) continue;
				/* matched to the display, the game makes its sound audioSpeed times
				 * faster than real time: resample it from that much higher a rate */
				int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
				if (rate != convFrom || !audioConv) {
					if (audioConv) SDL_FreeAudioStream(audioConv);
					audioConv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, want.freq);
					if (debug || convFrom) fprintf(stderr, "frame %llu: game audio rate %d Hz\n", (unsigned long long) frameCount, rate);
					convFrom = rate;
				}
				if (!audioConv) continue;
				SDL_AudioStreamPut(audioConv, samples, (int) got * 4);
				static int16_t conv[8192 * 2];
				int bytes;
				while ((bytes = SDL_AudioStreamGet(audioConv, conv, sizeof(conv))) > 0) {
					/* Paced by the display, the loop can't slow down or speed up to
					 * keep the sound queue level, so the sound gives: a sample frame
					 * dropped or repeated per chunk (at most ~0.1%, inaudible) while
					 * the queue is a quarter off its target. */
					if (pacedHz > 0 && bytes >= 8) {
						int q = (int) (SDL_GetQueuedAudioSize(audio) / 4);
						if (q > targetQueued * 5 / 4) bytes -= 4;
						else if (q < targetQueued * 3 / 4 && bytes + 4 <= (int) sizeof(conv)) {
							memcpy((char*) conv + bytes, (char*) conv + bytes - 4, 4);
							bytes += 4;
						}
					}
					int vol = ui_volume();
					if (vol < 100) {
						/* (squared: closer to how loud it sounds) */
						int gain = vol * vol * 65536 / 10000;
						for (int k = 0; k < bytes / 2; ++k) conv[k] = (int16_t) (conv[k] * gain >> 16);
					}
					SDL_QueueAudio(audio, conv, bytes);
				}
			}
			audT += SDL_GetPerformanceCounter() - audT0;
		}
		if (ffMax) maxRan = SDL_max(1, ran);
		runs = rewinding ? runs : SDL_max(1, ran); /* (the frames run, for the rewind buffer and the speed) */
		Uint64 emuT1 = SDL_GetPerformanceCounter();
		double emuCpu1 = thread_cpu_ms();
		emuTicks += emuT1 - emuT0;
		++frameCount;
		/* the rewind buffer: a state every second emulated frame */
		if (cfg->rewind && !rewinding && (rewindTick += runs) >= 2) {
			emu_rewind_append(&emu);
			rewindTick = 0;
		}

		/* ---- a compilation: the game running in it */
		if (pickProf && frameCount % 15 == 0) {
			const char* pc = pickProf->pick(&emu);
			if (!pc) pc = emu.gameCode;
			if (strncmp(pc, subCode, 4)) ss_switch(rom, pc);
		}

		/* ---- map refresh */
		if (mc) {
			++sinceMapRequest;
			/* the profile's sprite hiding, where it's only for some moments
			 * (hideWhen: Lady Sia's HUD in play, not its credits or pause
			 * screen); from the next frame drawn */
			if (hideWhenProf && memprobes_match(hideWhenProf->hideWhen, 2, &emu) != hideWhenOn) {
				hideWhenOn = !hideWhenOn;
				emu_hide_sprites(&emu, hideWhenOn ? hideWhenProf->hideSprites : NULL, hideWhenOn ? hideWhenN : 0);
				if (debug) fprintf(stderr, "frame %llu: sprite hiding %s\n", (unsigned long long) frameCount, hideWhenOn ? "on" : "off");
			}
			{
				/* (logoIf: the logo for now, SM4-11) */
				const MapProfile* lp = mapcap_profile(mc);
				bool lf = lp && ((lp->logoIf[0].addr && memprobes_match(lp->logoIf, 2, &emu)) ||
				                 (lp->logoIfOr[0].addr && memprobes_match(lp->logoIfOr, 2, &emu)));
				if (lf) logoHolding = 60;
				else if (logoHolding > 0) --logoHolding;
				lf = lf || logoHolding > 0;
				if (debug && lf != logoForced) fprintf(stderr, "frame %llu: logo only %s\n", (unsigned long long) frameCount, lf ? "on" : "off");
				logoForced = lf;
			}
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) refreshAt = (long) frameCount + 6;
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
			}
			if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
				/* quit to the title: the map belongs to the old session */
				mapShownIdx = -1;
				mapInactive = false;
				forceMap = true;
				/* (always logged: what sends the second screen back to the logo) */
				fprintf(stderr, "frame %llu: second screen back to the logo (the profile's menu probes matched)\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			if (rk != seenRoomKey) seenRoomKey = rk, seenRoomFor = 0;
			else ++seenRoomFor;
			/* a change counts once it has held for 3 frames (a cursor
			 * gliding to its place, Mario walking a path: not each step);
			 * a capture still running for the old one is called off, so
			 * the new one starts as soon as it has stopped */
			bool roomChanged = rk && rk != lastRoomKey && seenRoomFor >= 2;
			if (roomChanged && mapcap_busy(mc)) mapcap_cancel(mc);
			/* Back off while captures keep failing (e.g. the game says "playing"
			 * but the map can't open: intro cutscenes, briefings). Each failed
			 * attempt emulates a few seconds in the background, which starves
			 * the game and its audio on slower devices. */
			const char* why = NULL;
			int fails = mapcap_failures(mc, &why);
			if (fails != lastFails) {
				if (debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
				                            (unsigned long long) frameCount, fails, why ? why : "?");
				const MapProfile* dp = mapcap_profile(mc);
				if (fails && why && dp && dp->dimWhenUnavailable &&
				    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
					mapInactive = true;
				}
				lastFails = fails;
			}
			/* quick "no map here" failures (A Link to the Past indoors) cost little
			 * and are expected, so they don't back off */
			bool cheapFail = why && !strncmp(why, "map button did nothing", 22); /* (Pokémon's menu failures say why in brackets) */
			/* (a profile's own interval, when it's quicker: Swordcraft Story) */
			const MapProfile* ip = mapcap_profile(mc);
			int every = ip && ip->interval > 0 && ip->interval < cfg->mapInterval ? ip->interval : cfg->mapInterval;
			int interval = cheapFail ? every : every << SDL_min(fails, 3);
			/* after failures, a room change still triggers a try, but no more
			 * often than the normal interval */
			bool due = sinceMapRequest >= interval || pressCap ||
			           ((forceMap || roomChanged) && (!fails || sinceMapRequest >= every));
			if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu) && !(viewWait && tgState == TG_FADE_OUT)) { /* (not while the old view fades out: its picture is still the one up) */
				/* a new subject (a room, a loaded state) shows the game's own view again */
				if (roomChanged || forceMap) viewAlt = false;
				mapcap_set_alt(viewAlt && cfg->toggle);
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					capSubject = roomChanged || forceMap; /* (not a refresh of the same subject) */
					forceMap = false;
					if (pressCap) pressCap = false, tgCapWant = capDone + 1;
				}
			}
			toggle_step();
			bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
			if (fresh && mapcap_in_menu(mc, &emu)) {
				/* a capture that finished after quitting to the title: drop it */
			} else if (fresh) {
				map_show_new(true);
				++capDone;
			} else if (((map_anim() && mapRes.nFrames > 1) || fadeUpload) && mapShownIdx >= 0) {
				int idx = second_page();
				fadeUpload = false;
				upload_map_frame(idx);
			}
			/* a profile's own live picture (Mega Man Battle Network's maps on
			 * the net), drawn every frame over whatever was captured; when it
			 * stops, the captured picture again */
			const MapProfile* lvp = mapcap_profile(mc);
			const uint32_t* lv = lvp && lvp->liveView && romPath ? lvp->liveView(&emu, romPath) : NULL;
			if (lv) {
				for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], lv, mapW * 4);
				if (mapShownIdx < 0) mapShownIdx = 0;
				if (!liveOn && debug) fprintf(stderr, "frame %llu: live view on\n", (unsigned long long) frameCount);
				liveOn = true;
			} else if (liveOn) {
				liveOn = false;
				if (debug) fprintf(stderr, "frame %llu: live view off\n", (unsigned long long) frameCount);
				if (mapRes.ok && map_px(mapShownIdx)) {
					overDirty = true;
					upload_map_frame(mapShownIdx);
				} else {
					mapShownIdx = -1;
				}
			}
		}

		if (snapProf) update_snapshot();
		if (tallProf) update_tall();
		if (hudProf) update_hud();
		if (combatProf) {
			/* a fight: after a few frames of it, not on one odd frame */
			static int streak;
			combatInGame = combatProf->hud.on[0].addr && memprobes_match(combatProf->hud.on, 1, &emu);
			if ((combatInGame || combatProf->hud.logoAlways) && combatProf->hud.logoUnder && !combatMade) make_combat_canvas();
			bool c = memprobes_match(combatProf->combat, 2, &emu) ||
			         (combatProf->combatOr[0].addr && memprobes_match(combatProf->combatOr, 2, &emu));
			streak = c != combatOn ? streak + 1 : 0;
			if (streak >= 3) {
				combatOn = c;
				streak = 0;
				if (combatOn && !combatMade) make_combat_canvas();
				fprintf(stderr, "frame %llu: fight %s (the second screen's logo %s)\n", (unsigned long long) frameCount, combatOn ? "on" : "off",
				        combatOn ? "up" : "gone");
			}
		}
		if (ctl_snes()) snesHere = memprobes_match(ctlProf->controls.snes.when, 3, &emu);
		if (ctlProf) {
			/* switch after a few frames in the new state, not on one odd frame */
			static int ctlStreak;
			ctlStreak = ctl_playing_now() != ctlPlaying ? ctlStreak + 1 : 0;
			if (ctlStreak >= 3) {
				ctlPlaying = !ctlPlaying;
				ctlStreak = 0;
				if (debug && ctlMode == CTL_AUTO) {
					fprintf(stderr, "frame %llu: auto controls: %s\n", (unsigned long long) frameCount, ctlPlaying ? "four-face" : "GBA");
				}
			}
		}

		Uint64 drawT0 = SDL_GetPerformanceCounter();
		double drawCpu0 = thread_cpu_ms();
		/* frame buffer: start the next frame now; this one is drawn from a copy */
		const uint32_t* shownFb = emu.fb;
		/* (Pokemon Pinball's tall view always: two copies of the game beside
		 * it only keep full speed with the frame emulated alongside the drawing) */
		if ((cfg->frameBuffer || tallProf) && !ff && !rewinding) {
			memcpy(aheadFb, emu.fb, sizeof(aheadFb));
			if (ahead_start(keys)) aheadInFlight = true, shownFb = aheadFb;
		}
		if (mirrorOn && mirrorHave) shownFb = mirrorHeld; /* (hud.mirror: the picture from before) */
		if (tallLive) shownFb = cfg->swap ? tallBot : tallTop; /* (profile.tall: the table's upper half on the top screen) */
		if (cfg->ghosting) shownFb = ghost_frame(shownFb);
		/* only the view the game is on needs it (all of them right after a swap,
		 * since the menu draws the game without uploading) */
		for (int i = 0; i < nViews; ++i) {
			if (gameTexAll || i == screens[game_screen()].view) ui_update_texture(gameTex[i], shownFb, GBA_W * 4);
		}
		gameTexAll = false;
		/* (tests, GBADS_DRAW_SHOTS_ONLY: only the frames a screenshot or a
		 * touch needs are drawn) */
		if (!ui_script_quiet()) {
			ui_sync_view(cfg->vsync || pacedHz > 0 ? screens[game_screen()].view : -1); /* Match Display Rate; vsync: testing (docs/ANDROID.md) */
			{
				/* the other screen's redraw waits a frame if this one is past 70% of its time */
				static double budget = -1;
				if (budget < 0) budget = getenv("GBADS_DRAW_BUDGET") ? atof(getenv("GBADS_DRAW_BUDGET")) : frameSec * 1000.0 * 0.7;
				ui_present_budget(ff ? -1 : screens[game_screen()].view, workT0, budget);
			}
			ui_clear();
			draw_game();
			if (shotPending) {
				shotPending = false;
				take_screenshot();
			}
			ra_draw(nScreens > 1 ? 1 - game_screen() : 0); /* after the screenshot: not in it */
			if (barOpen && !cfg->barPinned && barGesture == G_NONE && SDL_GetTicks() - barSince > 5000) bar_close();
			draw_rewind();
			draw_hint(); /* (under the bar, as it comes up) */
			draw_bar();
			draw_toggle_btn();
			/* (the keeper: the game and its sound go on if this is held up; not
			 * for tests, whose frames are counted exactly) */
			const bool keep = cfg->keepSound && audio && !ff && !rewinding && !noPace && !ui_scripted();
			if (keep) keeper_arm(keys, audio, want.freq, targetQueued * 3, aheadInFlight ? 1 : 0);
			ui_present();
			if (keep) {
				int kept = keeper_disarm();
				if (kept) {
					/* those frames are the game's time since: the next one is
					 * due a frame after the last of them */
					next += kept * (pacedHz > 0 ? frameSec / 1.005 : frameSec);
					frameCount += (unsigned) kept;
					loopCur.kept = kept;
					keptTotal += kept;
				}
			}
		}
		Uint64 drawT1 = SDL_GetPerformanceCounter();
		drawTicks += drawT1 - drawT0;
		{
			const double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			int vTop = screens[0].view, vBot = nScreens > 1 ? screens[1].view : vTop;
			loopCur.emu0 = emuT0, loopCur.emu1 = emuT1, loopCur.draw0 = drawT0, loopCur.draw1 = drawT1;
			loopCur.audio = (double) audT * msPer;
			loopCur.presentTop = ui_last_present_ms(vTop);
			loopCur.presentBottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
			loopCur.valid = !ff && !rewinding; /* (fast-forward and rewind run their own way) */
		}
		{
			double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			double total = (double) (drawT1 - workT0) * msPer;
			/* matched, each frame includes the wait for the display's refresh:
			 * only one that missed its refresh (about two frames long) is slow */
			if (!ff && !rewinding && pacedHz > 0 && total > frameSec * 1000.0 * 1.4) ++pacedMisses;
			if (!ff && total > frameSec * 1000.0 * (pacedHz > 0 ? 1.4 : 1.0)) {
				SlowFrame f = { total, (double) (emuT0 - workT0) * msPer, (double) (emuT1 - emuT0 - raT) * msPer, (double) raT * msPer,
					            (double) (drawT0 - emuT1) * msPer, (double) (drawT1 - drawT0) * msPer, 0, 0, 0,
					            (unsigned long long) frameCount };
				/* by screen (top first, as they're stacked), not by view */
				int sTop = 0, sBot = nScreens > 1 ? 1 : 0;
				int vTop = screens[sTop].view, vBot = screens[sBot].view;
				f.top = ui_last_present_ms(vTop);
				f.bottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
				int red = ui_last_redraws();
				f.redraws = ((red >> vTop) & 1) | (vBot != vTop ? ((red >> vBot) & 1) << 1 : 0);
				f.preempted = thread_preemptions() - preempt0;
				f.cpu = current_cpu();
				f.mhz = cpu_mhz(f.cpu);
				double cpuNow = thread_cpu_ms();
				f.cpuMs = cpuNow - cpuMs0;
				f.waitEmu = SDL_max(0.0, (double) (emuT1 - emuT0) * msPer - (emuCpu1 - emuCpu0));
				f.waitDraw = SDL_max(0.0, (double) (drawT1 - drawT0) * msPer - (cpuNow - drawCpu0));
				f.audio = (double) audT * msPer;
				slow_frame_note(f);
			}
			static Uint64 lastReport;
			if (drawT1 - lastReport > SDL_GetPerformanceFrequency()) {
				slow_frame_report(debug);
				lastReport = drawT1;
			}
		}
#ifdef __ANDROID__
		if (frameCount == 1) fprintf(stderr, "startup: first frame shown\n");
#endif

		/* ---- pacing: real-time clock nudged by audio queue depth */
		double now = SDL_GetPerformanceCounter() / freq;
		fpsFrames += runs; /* emulated frames */
		if (now - fpsT0 >= 1.0) {
			fps = fpsFrames / (now - fpsT0);
			speedPct = fps / GBA_FPS * 100.0;
			if (debug) fprintf(stderr, "speed %.1f fps (%.0f%%)\n", fps, speedPct);
			fpsFrames = 0;
			fpsT0 = now;
			if (pacedHz > 0) {
				pacedMissSecs = pacedMisses >= 3 ? pacedMissSecs + 1 : 0;
				if (pacedMisses >= 3) fprintf(stderr, "frame %llu: %d frame(s) missed the display's refresh in the last second\n", (unsigned long long) frameCount, pacedMisses);
				pacedMisses = 0;
				if (pacedMissSecs >= 3) {
					pacedFallback = true;
					pacing_update(); /* (logs the "frame pacing:" line again) */
					next = now;
				}
			}
		}
		/* fast-forward is paced too: ff_speed frames per real frame. Matched to
		 * the display, presenting in step with it sets the pace; the clock runs
		 * a little faster, as a limit for when the display doesn't make us wait. */
		next += pacedHz > 0 ? frameSec / 1.005 : frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.015;
			else if (queued < targetQueued / 2) next -= frameSec * 0.015;
		}
		if (next < now - 4 * frameSec) next = now;
		if (noPace) next = now; /* benchmarking: run as fast as possible */
#ifdef __ANDROID__
		android_perf_report((int64_t) ((SDL_GetPerformanceCounter() - workT0) * 1e9 / freq));
#endif
		double wait = next - now;
		loopCur.sleep0 = SDL_GetPerformanceCounter();
		loopCur.sleepWant = wait > 0 ? wait : 0;
#ifdef __wasi__
		/* the web build: back to the page every frame, for the wait or not */
		web_sleep(wait > 0 ? (unsigned) (wait * 1000) : 0);
		logo_step(); /* (a logo a frame: the game's own, started without the game list) */
#else
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
#endif
		loopCur.end = SDL_GetPerformanceCounter();
	}

	keeper_stop();
	ahead_stop(); /* before anything else touches the core */
	ui_on_background(NULL);
	ui_on_foreground(NULL);
	ui_sync_view(-1);
	fprintf(stderr, "leaving game after %llu frames\n", (unsigned long long) frameCount);
	if (bugPend.pending) {
		/* (a report waiting for its note: saved with what's typed) */
		char out[200];
		bug_finish(noteOpen ? noteText : "", out, sizeof(out));
		noteOpen = false;
	}
	auto_save();
	ra_game_end();
	if (audio) SDL_CloseAudioDevice(audio);
	if (audioConv) SDL_FreeAudioStream(audioConv);
	mapcap_destroy(mc);
	mc = NULL;
	emu_rewind_deinit();
#ifdef __wasi__
	webPlaying = false;
#endif
	twin_close();
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	free(stillPx);
	stillPx = NULL;
	stillValid = false;
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyTexture(gameTex[i]);
		SDL_DestroyTexture(mapTex[i]);
		if (hudTex[i]) SDL_DestroyTexture(hudTex[i]);
		gameTex[i] = mapTex[i] = hudTex[i] = NULL;
	}
	ui_free_image(logoTex);
	ui_free_image(combatTex);
	ui_free_image(logoCanvasTex);
	ui_free_image(overlayTex[0]);
	ui_free_image(overlayTex[1]);
	overlayMode[0][0] = overlayMode[1][0] = 0;
	ui_free_image(bezelTex[0]);
	ui_free_image(bezelTex[1]);
	bezelKey[0][0] = bezelKey[1][0] = 0;
	return exitTo;
}

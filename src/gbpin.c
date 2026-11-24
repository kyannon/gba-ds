/* Pokémon Pinball (Game Boy Color): see gbpin.h and docs/games/pokemon-pinball-gbc.md.
 *
 * The decompilation (pret/pokepinball) names what is used here:
 *   GameScreenFunction_HandleBallPhysics   the table's logic, once a frame
 *   CheckStageTransition                   the ball crossed into the other half
 *   FieldVerticalTransition                (A = the stage to load) loads it: graphics, collision, the
 *                                          screen goes white for about 13 frames while it does
 *   wCurrentStage (0 red top, 1 red bottom, 4 blue top, 5 blue bottom; the bonus stages are single screens)
 *   wBallYPos + 1                          the ball's row, in the half it is in: crossing adds or takes 0x88
 * A copy's ROM is patched so that its physics function starts with a call into a small routine in the
 * bank's free end: if a command byte (in unused high RAM) is set, it clears it, loads the stage with
 * FieldVerticalTransition and moves the ball into that half's rows; and CheckStageTransition returns
 * after its scroll update (a copy never crosses, or loses the ball). Everything is found by its code, so
 * the three releases (USA, Europe, Japan: their RAM and code sit at their own places) need no table. */
#include "gbpin.h"

#include <mgba/core/core.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
	int bank;                         /* the ROM bank holding all of it (3) */
	uint16_t phys, cst, fvt, hook;    /* CPU addresses in that bank */
	uint16_t stage, ballY, flagAt;    /* wCurrentStage, wBallYPos+1, the command byte (high RAM) */
	uint8_t physOp[4];                /* the physics function's first instruction pair, which the call replaces */
	bool ok;
} Addrs;

/* SM83 instruction lengths, for matching code with the addresses in it left out */
static int sm_len(uint8_t op) {
	switch (op) {
	case 0xCB: return 2;
	case 0x01: case 0x11: case 0x21: case 0x31: case 0x08: case 0xC2: case 0xC3: case 0xC4: case 0xCA: case 0xCC:
	case 0xCD: case 0xD2: case 0xD4: case 0xDA: case 0xDC: case 0xEA: case 0xFA: return 3;
	case 0x06: case 0x0E: case 0x16: case 0x1E: case 0x26: case 0x2E: case 0x36: case 0x3E: case 0x10: case 0x18:
	case 0x20: case 0x28: case 0x30: case 0x38: case 0xC6: case 0xCE: case 0xD6: case 0xDE: case 0xE0: case 0xE6:
	case 0xE8: case 0xEE: case 0xF0: case 0xF6: case 0xF8: case 0xFE: return 2;
	default: return 1;
	}
}

/* the USA release's code (pret/pokepinball): the physics function and CheckStageTransition */
static const uint8_t PHYS[40] = { 0xaf, 0xea, 0xb9, 0xd7, 0xea, 0xeb, 0xd7, 0xcd, 0x68, 0x21, 0xcd, 0x80, 0x21, 0xaf, 0xea, 0xe9, 0xd7, 0xcd, 0x82, 0x35,
	                              0xfa, 0xac, 0xd4, 0xcb, 0x47, 0xe0, 0x8a, 0x3e, 0x03, 0x21, 0xfe, 0x60, 0xc4, 0x4f, 0x05, 0xfa, 0xb9, 0xd7, 0xa7, 0xfa };
static const uint8_t CST[36] = { 0xcd, 0x5e, 0x6d, 0xfa, 0xb6, 0xd4, 0xc6, 0x10, 0xfe, 0x18, 0x38, 0x1e, 0xfe, 0xb8, 0xd8, 0xfa, 0xac, 0xd4,
	                             0x4f, 0x06, 0x00, 0x21, 0x4e, 0x6d, 0x09, 0x7e, 0xfe, 0xff, 0x28, 0x27, 0xcd, 0x74, 0x66, 0xfa, 0xb6, 0xd4 };

/* every place in the ROM where `pat` matches, its 16-bit operands ignored; -1 unless exactly one */
static long find_code(const uint8_t* rom, size_t n, const uint8_t* pat, size_t len) {
	bool wild[64] = { false };
	for (size_t i = 0; i < len;) {
		int l = sm_len(pat[i]);
		if (l == 3) wild[i + 1] = wild[i + 2] = true;
		i += (size_t) l;
	}
	long found = -1;
	for (size_t i = 0; i + len <= n; ++i) {
		if (rom[i] != pat[0]) continue;
		size_t k = 0;
		while (k < len && (wild[k] || rom[i + k] == pat[k])) ++k;
		if (k < len) continue;
		if (found >= 0) return -1;
		found = (long) i;
	}
	return found;
}

static bool find_addrs(const uint8_t* rom, size_t n, Addrs* a) {
	memset(a, 0, sizeof(*a));
	long ph = find_code(rom, n, PHYS, sizeof(PHYS)), cs = find_code(rom, n, CST, sizeof(CST));
	if (ph < 0 || cs < 0 || n < 0x10000) return false;
	if (ph >> 14 != cs >> 14 || ph < 0x4000) return false;
	a->bank = (int) (ph >> 14);
	a->phys = (uint16_t) (0x4000 + (ph & 0x3FFF));
	a->cst = (uint16_t) (0x4000 + (cs & 0x3FFF));
	a->ballY = (uint16_t) (rom[cs + 4] | rom[cs + 5] << 8);
	a->stage = (uint16_t) (rom[cs + 16] | rom[cs + 17] << 8);
	a->fvt = (uint16_t) (rom[cs + 31] | rom[cs + 32] << 8); /* the second call in it */
	if (rom[ph] != 0xAF || rom[ph + 1] != 0xEA) return false;
	memcpy(a->physOp, rom + ph, 4);
	/* the free end of the bank for the routine (it is 41 bytes) */
	size_t bankEnd = ((size_t) a->bank + 1) << 14;
	if (bankEnd > n) return false;
	for (size_t i = bankEnd - 0x40; i < bankEnd; ++i)
		if (rom[i]) return false;
	a->hook = (uint16_t) (0x4000 + (bankEnd - 0x40 - ((size_t) a->bank << 14)));
	/* a byte of high RAM the game never touches, for the command */
	for (int h = 0xD9; h >= 0xC5 && !a->flagAt; --h) {
		bool used = false;
		for (size_t i = 0; i + 2 < n && !used; ++i) {
			if ((rom[i] == 0xE0 || rom[i] == 0xF0 || rom[i] == 0xE2) && rom[i + 1] == h) used = true;
			if ((rom[i] == 0xEA || rom[i] == 0xFA) && rom[i + 1] == h && rom[i + 2] == 0xFF) used = true;
		}
		if (!used) a->flagAt = (uint16_t) (0xFF00 + h);
	}
	a->ok = a->flagAt != 0;
	return a->ok;
}

static uint8_t rd8(Emu* e, uint16_t a) {
	return e->core->rawRead8(e->core, a, -1);
}
static void wr8(Emu* e, uint16_t a, uint8_t v) {
	e->core->rawWrite8(e->core, a, -1, v);
}
static void patch8(Emu* e, int bank, uint16_t a, uint8_t v) {
	e->core->rawWrite8(e->core, a, bank, v);
}

static void patch_copy(Emu* e, const Addrs* a) {
	uint8_t h[48];
	size_t k = 0;
#define B(x) h[k++] = (uint8_t) (x)
	B(0xAF);                                          /* xor a */
	B(0xEA), B(a->physOp[2]), B(a->physOp[3]);        /* ld [..],a   (the instruction the call replaced) */
	B(0xFA), B(a->flagAt & 0xFF), B(a->flagAt >> 8);  /* ld a,[flag]: the stage to load + 1 */
	B(0xA7);                                          /* and a */
	B(0x28), B(0x1D);                                 /* jr z,.done */
	B(0x3D);                                          /* dec a */
	B(0x47);                                          /* ld b,a */
	B(0xAF);                                          /* xor a */
	B(0xEA), B(a->flagAt & 0xFF), B(a->flagAt >> 8);  /* ld [flag],a */
	B(0x78);                                          /* ld a,b */
	B(0xCD), B(a->fvt & 0xFF), B(a->fvt >> 8);        /* call FieldVerticalTransition */
	B(0xFA), B(a->stage & 0xFF), B(a->stage >> 8);    /* ld a,[wCurrentStage] */
	B(0xCB), B(0x47);                                 /* bit 0,a */
	B(0xFA), B(a->ballY & 0xFF), B(a->ballY >> 8);    /* ld a,[wBallYPos+1] */
	B(0x28), B(0x04);                                 /* jr z,.up */
	B(0xD6), B(0x88);                                 /* sub $88 */
	B(0x18), B(0x02);                                 /* jr .store */
	B(0xC6), B(0x88);                                 /* .up: add $88 */
	B(0xEA), B(a->ballY & 0xFF), B(a->ballY >> 8);    /* .store: ld [wBallYPos+1],a */
	B(0xAF);                                          /* .done: xor a */
	B(0xC9);                                          /* ret */
#undef B
	for (size_t i = 0; i < k; ++i) patch8(e, a->bank, (uint16_t) (a->hook + i), h[i]);
	patch8(e, a->bank, a->phys, 0xCD), patch8(e, a->bank, a->phys + 1, a->hook & 0xFF);
	patch8(e, a->bank, a->phys + 2, a->hook >> 8), patch8(e, a->bank, a->phys + 3, 0x00);
	patch8(e, a->bank, a->cst + 3, 0xC9); /* CheckStageTransition: return after its scroll call */
}

typedef struct {
	Emu e;
	pthread_t th;
	pthread_mutex_t m;
	pthread_cond_t cv;
	bool threaded, started, go, done, quit;
	void* st;
	size_t stCap;
	uint32_t keys;
	int stage;      /* the game's stage at the fork */
	bool ok;        /* the copy loaded the other half and ran the frame */
	int wy;         /* its window's row (the score and message bar) */
	uint32_t pic[GBA_W * GBA_H];
} Copy;

struct GbPin {
	Addrs a;
	Copy c[2];
	int turn, started;      /* the copy the next job goes to; the copy the last job went to (-1 none) */
	bool sync;
	bool table;             /* the two halves are up */
	int stage;
	int lost;               /* frames since a picture was taken */
	uint32_t top[GBA_W * GBA_H], bot[GBA_W * GBA_H];
};

static bool table_stage(int s) {
	return s == 0 || s == 1 || s == 4 || s == 5;
}

static bool is_white(const uint32_t* fb) {
	for (int i = 0; i < GBA_W * GBA_H; ++i)
		if (fb[i] != fb[0]) return false;
	return true;
}

/* one job: load the game's state, load the other half, run the frame */
static void run_job(GbPin* g, Copy* c) {
	Emu* e = &c->e;
	c->ok = false;
	if (!table_stage(c->stage)) return;
	emu_state_load(e, c->st);
	wr8(e, g->a.flagAt, (uint8_t) ((c->stage ^ 1) + 1));
	emu_frame_blind(e, c->keys);
	if (rd8(e, g->a.flagAt)) return; /* the physics function wasn't run (a menu, the ball lost...) */
	bool off = false;
	int n = 0;
	for (; n < 80; ++n) {
		bool on = rd8(e, 0xFF40) & 0x80;
		if (!on) off = true;
		if (off && on) break;
		emu_frame_blind(e, c->keys);
	}
	if (n >= 80) return;
	for (int i = 0; i < 3; ++i) emu_frame(e, c->keys); /* its logic runs again, its sprites come up */
	if (rd8(e, g->a.stage) != (c->stage ^ 1)) return;
	c->wy = rd8(e, 0xFF4A);
	memcpy(c->pic, e->fb, sizeof(c->pic));
	c->ok = !is_white(c->pic);
}

static void* copy_thread(void* arg) {
	void** p = arg;
	GbPin* g = p[0];
	Copy* c = p[1];
	free(p);
	pthread_mutex_lock(&c->m);
	for (;;) {
		while (!c->go && !c->quit) pthread_cond_wait(&c->cv, &c->m);
		if (c->quit) break;
		pthread_mutex_unlock(&c->m);
		run_job(g, c);
		pthread_mutex_lock(&c->m);
		c->go = false;
		c->done = true;
		pthread_cond_broadcast(&c->cv);
	}
	pthread_mutex_unlock(&c->m);
	return NULL;
}

GbPin* gbpin_open(const char* romPath, bool sync) {
	size_t n = 0;
	uint8_t* rom = emu_rom_read_all(romPath, &n);
	if (!rom) return NULL;
	GbPin* g = calloc(1, sizeof(*g));
	bool ok = g && find_addrs(rom, n, &g->a);
	free(rom);
	if (!ok) {
		free(g);
		return NULL;
	}
	g->sync = sync;
	g->started = -1;
	for (int i = 0; i < 2; ++i) {
		Copy* c = &g->c[i];
		if (!emu_open(&c->e, romPath, NULL, false)) {
			gbpin_close(g);
			return NULL;
		}
		c->started = true;
		patch_copy(&c->e, &g->a);
		pthread_mutex_init(&c->m, NULL);
		pthread_cond_init(&c->cv, NULL);
		c->done = true;
		if (!sync) {
			void** p = malloc(2 * sizeof(void*));
			p[0] = g, p[1] = c;
			c->threaded = pthread_create(&c->th, NULL, copy_thread, p) == 0;
			if (!c->threaded) free(p);
		}
	}
	return g;
}

void gbpin_close(GbPin* g) {
	if (!g) return;
	for (int i = 0; i < 2; ++i) {
		Copy* c = &g->c[i];
		if (!c->started) continue;
		if (c->threaded) {
			pthread_mutex_lock(&c->m);
			c->quit = true;
			pthread_cond_broadcast(&c->cv);
			pthread_mutex_unlock(&c->m);
			pthread_join(c->th, NULL);
		}
		emu_close(&c->e);
		free(c->st);
	}
	free(g);
}

void gbpin_before(GbPin* g, Emu* game, uint32_t keys) {
	if (!g) return;
	Copy* c = &g->c[g->turn];
	size_t need = emu_state_size(game);
	if (c->stCap < need) {
		free(c->st);
		c->st = malloc(need);
		c->stCap = c->st ? need : 0;
	}
	if (!c->st) return;
	if (c->threaded) {
		pthread_mutex_lock(&c->m);
		while (!c->done) pthread_cond_wait(&c->cv, &c->m);
		pthread_mutex_unlock(&c->m);
	}
	c->ok = false;
	c->stage = rd8(game, g->a.stage);
	if (!table_stage(c->stage) || !emu_state_save(game, c->st)) {
		g->started = -1;
		return;
	}
	c->keys = keys;
	if (c->threaded) {
		pthread_mutex_lock(&c->m);
		c->done = false;
		c->go = true;
		pthread_cond_broadcast(&c->cv);
		pthread_mutex_unlock(&c->m);
	} else {
		run_job(g, c);
	}
	g->started = g->turn;
	g->turn ^= 1;
}

/* called after the game's frame: collects the job started a frame ago (g->started was set by this frame's
 * gbpin_before, so the job before it is the other copy's) */
void gbpin_after(GbPin* g, Emu* game) {
	if (!g) return;
	Copy* c = &g->c[g->started < 0 ? g->turn : g->started ^ 1];
	if (g->started < 0) { /* (this frame's job wasn't started: the stage isn't a table's; collect nothing) */
		g->table = false;
		g->lost = 0;
		return;
	}
	if (c->threaded) {
		pthread_mutex_lock(&c->m);
		while (!c->done) pthread_cond_wait(&c->cv, &c->m);
		pthread_mutex_unlock(&c->m);
	}
	int s = rd8(game, g->a.stage);
	g->stage = s;
	bool flash = is_white(game->fb);
	if (c->ok && c->stage == s && table_stage(s) && !flash) {
		/* the halves; the score and message bar stays on the bottom one only (PPG-3): the top half's own is
		 * left black, from the window's row (the copy's, taken when it ran) */
		memcpy((s & 1) ? g->bot : g->top, game->fb, sizeof(g->top));
		memcpy((s & 1) ? g->top : g->bot, c->pic, sizeof(g->top));
		int wy = (s & 1) ? c->wy : rd8(game, 0xFF4A);
		for (int y = game->picY + (wy < 144 ? wy : 144); y < game->picY + game->picH; ++y)
			for (int x = 0; x < GBA_W; ++x) g->top[y * GBA_W + x] = 0xFF000000u;
		g->table = true;
		g->lost = 0;
	} else if (flash || (c->ok && c->stage != s)) {
		/* the game is loading the other half (the screen white for about a fifth of a second), or has
		 * just crossed: both halves hold (PPG-4) */
		if (++g->lost > 90) g->table = false;
	} else {
		/* not the table's logic (a menu, the pause screen, the ball lost): the game plays on its own screen */
		g->table = false;
		g->lost = 0;
	}
}

bool gbpin_table(const GbPin* g, const uint32_t** top, const uint32_t** bottom) {
	if (!g || !g->table) return false;
	*top = g->top;
	*bottom = g->bot;
	return true;
}

int gbpin_stage(const GbPin* g) {
	return g && g->table ? g->stage : -1;
}

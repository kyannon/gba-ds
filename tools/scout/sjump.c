/* Sonic Pinball Party: can a copy be held at another view, taken from the
 * game every frame, with its own BG tiles carried over (docs/games/
 * sonic-pinball-party.md)?
 *   sjump ROM STATE FRAMES TARGET OUTDIR
 * The game runs with a flipper tap every 50 frames. R, a reference copy run
 * on its own from the state, and X, taken from the game's state every
 * frame, are both held with their view at TARGET (walking there 8 rows a
 * frame; TARGET -1: the game's view + 160, as the app's bottom copy); X is
 * run as the app's copies (two frames from the game's state from before the
 * frame before, its VRAM 0x0000-0xFFFF, view variables and queued ring copies
 * its own). SJ_LAYERS: the layers drawn (default 0x03, BG0 and BG1). Prints, per frame, the views and how many pixels of BG0+BG1
 * differ between R and X; writes PNGs of R, X and the game every 60 frames.
 */
#include <mgba/flags.h>
#include <mgba/core/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"

#include <stdbool.h>
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#define CAM 0x02031AE4
static const uint32_t nops[] = { 0x0800A4BE, 0x08009FAC };
/* view variables carried over: the ring counters and camera, the VOFS shadows */
static const struct { uint32_t a, n; } vars[] = { { 0x02031AD4, 0x14 }, { 0x03000784, 4 }, { 0x03002990, 4 } };

/* the game's queue of copies done at the next vblank (0x0800162C): count
 * byte, then 64 entries of dst, src, size */
#define QN 0x030004F8
#define QE 0x030020B0
static uint32_t own[64][3]; static int nOwn;
static bool ring(uint32_t d) { return d >= 0x06000000 && d < 0x06006000; }
static void q_take(Emu* e) {
	nOwn = 0;
	int n = emu_read8(e, QN);
	for (int i = 0; i < n && i < 64; ++i) {
		uint32_t d = emu_read32(e, QE + i * 12);
		if (ring(d)) own[nOwn][0] = d, own[nOwn][1] = emu_read32(e, QE + i * 12 + 4), own[nOwn][2] = emu_read32(e, QE + i * 12 + 8), ++nOwn;
	}
}
static int q_put(Emu* e) {
	int n = emu_read8(e, QN), k = 0, dropped = 0;
	for (int i = 0; i < n && i < 64; ++i) {
		uint32_t d = emu_read32(e, QE + i * 12);
		if (ring(d)) { ++dropped; continue; }
		for (int j = 0; j < 12; j += 4) emu_write32(e, QE + k * 12 + j, emu_read32(e, QE + i * 12 + j));
		++k;
	}
	for (int i = 0; i < nOwn && k < 64; ++i, ++k) for (int j = 0; j < 3; ++j) emu_write32(e, QE + k * 12 + j * 4, own[i][j]);
	emu_write8(e, QN, (uint8_t) k);
	return dropped;
}

static void hold(Emu* e, int* cur, int target) {
	int want = target + 80;
	if (*cur < want) *cur = *cur + 16 < want ? *cur + 16 : want;
	else if (*cur > want) *cur = *cur - 16 > want ? *cur - 16 : want;
	emu_write16(e, CAM, (uint16_t) *cur);
}

uint8_t sjgv[0x10000];
int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "sjump ROM STATE FRAMES TARGET OUTDIR\n"); return 1; }
	Emu g = {0}, r = {0}, x = {0};
	if (!emu_open(&g, argv[1], NULL, false) || !emu_open(&r, argv[1], NULL, false) || !emu_open(&x, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&g, argv[2]) || !emu_state_load_file(&r, argv[2]) || !emu_state_load_file(&x, argv[2])) return 1;
	int frames = atoi(argv[3]), target = atoi(argv[4]);
	/* optional 6th argument: a harness keys script for the game's keys */
	static uint32_t kk[100000]; int nk = 0;
	if (argc > 6) {
		FILE* kf = fopen(argv[6], "r"); char line[256], b[64]; int n;
		static const char* names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
		while (kf && fgets(line, sizeof line, kf))
			if (sscanf(line, "hold %63s %d", b, &n) == 2) {
				uint32_t k = 0;
				for (char* t = strtok(b, "+"); t; t = strtok(NULL, "+")) for (int i = 0; i < 10; ++i) if (!strcmp(t, names[i])) k |= 1u << i;
				for (int j = 0; j < n && nk < 100000; ++j) kk[nk++] = k;
			}
		if (kf) fclose(kf);
	}
	const char* out = argv[5];
	for (unsigned i = 0; i < sizeof nops / sizeof nops[0]; ++i) emu_write16(&r, nops[i], 0x46C0), emu_write16(&x, nops[i], 0x46C0);
	unsigned lm = getenv("SJ_LAYERS") ? (unsigned) strtoul(getenv("SJ_LAYERS"), NULL, 0) : 0x03;
	emu_layers(&r, lm); emu_layers(&x, lm);
	size_t sz = emu_state_size(&g);
	static uint8_t xv[0x10000]; static uint8_t xvar[3][0x20];
	int rc = emu_read16(&g, CAM), xc = rc;
	bool have = false, havePrev = false;
	void* prev = malloc(sz); uint32_t prevKeys = 0;
	/* the copies' patch of the ball's sprite (profiles.c A3VE) */
	static const uint32_t pa[] = { 0x08029D3A, 0x08029D3C, 0x08029D3E, 0x08029D40, 0x08029D42, 0x08029D44, 0x08029D46, 0x08029D5C, 0x08029D5E };
	static const uint16_t pv[] = { 0x4808, 0x8800, 0x1A2D, 0x3550, 0x0428, 0x0C05, 0xE00C, 0x1AE4, 0x0203 };
	for (int i = 0; i < 9; ++i) emu_write16(&r, pa[i], pv[i]), emu_write16(&x, pa[i], pv[i]);
	for (int f = 0; f < frames; ++f) {
		uint32_t keys = f < nk ? kk[f] : ((f % 50) < 6 ? (0x200 | 0x001) : 0);
		/* X, as the app's copies: the game's state from before the frame
		 * before, its own tiles and view put in, that frame again (its tiles
		 * kept after it), then this one */
		int tg = target;
		if (target < 0) { int u = emu_read16(&g, 0x03000784), vmax = emu_read16(&g, 0x030040E0) - 8 - 320; tg = (u < vmax ? (u < 8 ? 8 : u) : vmax) + 160; }
		bool xr = false;
		if (havePrev) {
			if (!emu_state_load(&x, prev)) fprintf(stderr, "load failed f%d\n", f);
			if (have && !getenv("SJ_NOCARRY")) {
				emu_vram_write(&x, 0, xv, sizeof xv);
				for (int k = 0; k < 3; ++k) for (uint32_t j = 0; j < vars[k].n; ++j) emu_write8(&x, vars[k].a + j, xvar[k][j]);
				q_put(&x);
			} else xc = emu_read16(&x, CAM);
			hold(&x, &xc, tg);
			if (getenv("SJ_DBG")) { extern uint8_t sjgv[0x10000]; const uint8_t* vv = (const uint8_t*) emu_vram(&x); int dd = 0; for (int i = 0; i < 0x10000; ++i) dd += vv[i] != sjgv[i]; fprintf(stderr, "f%d x vs g-at-save %d, x16 %p\n", f, dd, (void*) vv); }
			if (getenv("SJ_DBG")) { const uint8_t* v = (const uint8_t*) emu_vram(&x); int d = 0, z = 0; for (int i = 0; i < 0x10000; ++i) d += v[i] != xv[i], z += v[0x6000 + (i & 0xFFF)] == 0; fprintf(stderr, "f%d put diff %d mapzeros %d\n", f, d, z / 16); }
			emu_frame(&x, prevKeys);
			memcpy(xv, emu_vram(&x), sizeof xv);
			q_take(&x);
			for (int k = 0; k < 3; ++k) for (uint32_t j = 0; j < vars[k].n; ++j) xvar[k][j] = emu_read8(&x, vars[k].a + j);
			have = xr = true;
		}
		if (!emu_state_save(&g, prev)) fprintf(stderr, "save failed f%d\n", f);
		memcpy(sjgv, emu_vram(&g), sizeof sjgv); uint8_t* gv = sjgv;
		if (getenv("SJ_DBG")) { int z = 0; for (int i = 0x6000; i < 0x7000; ++i) z += !gv[i]; fprintf(stderr, "f%d g map zeros at save %d\n", f, z); }
		prevKeys = keys; havePrev = true;
		hold(&r, &rc, tg);
		if (xr) emu_frame(&x, keys);
		emu_frame(&r, keys); emu_frame(&g, keys);
		have = true;
		int diff = 0;
		for (int i = 0; i < 240 * 160; ++i) diff += (x.fb[i] & 0xFFFFFF) != (r.fb[i] & 0xFFFFFF);
		printf("f%04d game %3d r %3d x %3d diff %d\n", f, emu_io16(&g, 0x12), emu_io16(&r, 0x12), emu_io16(&x, 0x12), diff);
		if (f % 60 == 59) {
			char p[512];
			snprintf(p, sizeof p, "%s/r%04d.png", out, f); write_png(p, r.fb, 240, 160, 240);
			snprintf(p, sizeof p, "%s/x%04d.png", out, f); write_png(p, x.fb, 240, 160, 240);
			snprintf(p, sizeof p, "%s/g%04d.png", out, f); write_png(p, g.fb, 240, 160, 240);
		}
	}
	return 0;
}

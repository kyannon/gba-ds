/* Sonic Pinball Party: can a copy be held at another view, taken from the
 * game every frame, with its own BG tiles carried over (docs/games/
 * sonic-pinball-party.md)?
 *   sjump ROM STATE FRAMES TARGET OUTDIR
 * The game runs with a flipper tap every 50 frames. R, a reference copy run
 * on its own from the state, and X, taken from the game's state every
 * frame, are both held with their view at TARGET (walking there 8 rows a
 * frame); X's VRAM 0x0000-0xFFFF and view variables are its own from its
 * last frame. Prints, per frame, the views and how many pixels of BG0+BG1
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
	if (*cur < want) *cur = *cur + 8 < want ? *cur + 8 : want;
	else if (*cur > want) *cur = *cur - 8 > want ? *cur - 8 : want;
	emu_write16(e, CAM, (uint16_t) *cur);
}

int main(int argc, char** argv) {
	if (argc < 6) { fprintf(stderr, "sjump ROM STATE FRAMES TARGET OUTDIR\n"); return 1; }
	Emu g = {0}, r = {0}, x = {0};
	if (!emu_open(&g, argv[1], NULL, false) || !emu_open(&r, argv[1], NULL, false) || !emu_open(&x, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&g, argv[2]) || !emu_state_load_file(&r, argv[2])) return 1;
	int frames = atoi(argv[3]), target = atoi(argv[4]);
	const char* out = argv[5];
	for (unsigned i = 0; i < sizeof nops / sizeof nops[0]; ++i) emu_write16(&r, nops[i], 0x46C0), emu_write16(&x, nops[i], 0x46C0);
	emu_layers(&r, 0x03); emu_layers(&x, 0x03);
	size_t sz = emu_state_size(&g);
	void* st = malloc(sz);
	static uint8_t xv[0x10000]; static uint8_t xvar[3][0x20];
	int rc = emu_read16(&g, CAM), xc = rc;
	bool have = false;
	for (int f = 0; f < frames; ++f) {
		uint32_t keys = (f % 50) < 6 ? (0x200 | 0x001) : 0;
		emu_state_save(&g, st);
		/* X: the game's state, its own tiles and view */
		emu_state_load(&x, st);
		for (unsigned i = 0; i < sizeof nops / sizeof nops[0]; ++i) emu_write16(&x, nops[i], 0x46C0);
		if (have) {
			emu_vram_write(&x, 0, xv, sizeof xv);
			for (int k = 0; k < 3; ++k) for (uint32_t j = 0; j < vars[k].n; ++j) emu_write8(&x, vars[k].a + j, xvar[k][j]);
			q_put(&x);
		}
		hold(&x, &xc, target);
		hold(&r, &rc, target);
		emu_frame(&x, keys); emu_frame(&r, keys); emu_frame(&g, keys);
		memcpy(xv, emu_vram(&x), sizeof xv);
		q_take(&x);
		for (int k = 0; k < 3; ++k) for (uint32_t j = 0; j < vars[k].n; ++j) xvar[k][j] = emu_read8(&x, vars[k].a + j);
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

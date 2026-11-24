/* gbpin2: what a stage change writes outside video memory (WRAM, HRAM, I/O):
 *   gbpin2 ROM  : runs the game's own stage load twice at different moments and lists the addresses it changed
 *   the same way both times (the other stage's constants). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mgba/core/core.h>
#include "../../src/emu.h"

static Emu master, g;
static uint8_t rd(Emu* e, uint16_t a, int seg) { return e->core->rawRead8(e->core, a, seg); }
static void wr(Emu* e, uint16_t a, uint8_t v) { e->core->rawWrite8(e->core, a, -1, v); }
static void wrbank(Emu* e, uint16_t a, int bank, uint8_t v) { e->core->rawWrite8(e->core, a, bank, v); }
#define N (0x1000 * 8 + 0x100)
static void cap(Emu* e, uint8_t* o) { /* wram banks 0..7 then ff00-ffff */
	for (int i = 0; i < 0x1000; ++i) o[i] = rd(e, 0xC000 + i, -1);
	for (int b = 1; b < 8; ++b) for (int i = 0; i < 0x1000; ++i) o[b * 0x1000 + i] = rd(e, 0xD000 + i, b);
	for (int i = 0; i < 0x100; ++i) o[0x8000 + i] = rd(e, 0xFF00 + i, -1);
}
static int addr_of(int i, char* s) {
	if (i < 0x1000) return sprintf(s, "C%03X", i), 0;
	if (i < 0x8000) return sprintf(s, "bank%d D%03X", i / 0x1000, i % 0x1000), 0;
	return sprintf(s, "FF%02X", i - 0x8000), 0;
}
#define HOOK 0x7B00
static void patch(Emu* e) {
	static const uint8_t hook[] = { 0xAF, 0xEA, 0xB9, 0xD7, 0xFA, 0xD2, 0xFF, 0xA7, 0x28, 0x1D, 0x3D, 0x47, 0xAF, 0xEA, 0xD2, 0xFF, 0x78, 0xCD, 0x74, 0x66, 0xFA, 0xAC, 0xD4, 0xCB, 0x47, 0xFA, 0xB6, 0xD4, 0x28, 0x04, 0xD6, 0x88, 0x18, 0x02, 0xC6, 0x88, 0xEA, 0xB6, 0xD4, 0xAF, 0xC9 };
	for (size_t i = 0; i < sizeof(hook); ++i) wrbank(e, HOOK + i, 3, hook[i]);
	wrbank(e, 0x5909, 3, 0xCD), wrbank(e, 0x590A, 3, HOOK & 0xFF), wrbank(e, 0x590B, 3, HOOK >> 8), wrbank(e, 0x590C, 3, 0);
	wrbank(e, 0x6CEC, 3, 0xC9);
}
static bool white(const uint32_t* fb) { for (int i = 0; i < GBA_W * GBA_H; ++i) if (fb[i] != fb[0]) return false; return true; }
static uint8_t A[2][N], B[2][N];
int main(int argc, char** argv) {
	if (!emu_open(&master, argv[1], NULL, false) || !emu_open(&g, argv[1], NULL, false)) return 1;
	patch(&g);
	size_t sz = emu_state_size(&master);
	void* snap = malloc(sz);
	struct { uint32_t k; int n; } seq[] = { { 0, 300 }, { K_START, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 200 }, { K_DOWN, 40 }, { 0, 1 } };
	for (size_t i = 0; i < sizeof(seq) / sizeof(*seq); ++i) for (int f = 0; f < seq[i].n; ++f) emu_frame(&master, seq[i].k);
	int got = 0;
	for (int f = 0; f < 2000 && got < 2; ++f) {
		uint32_t k = (f / 20 % 3 == 0) ? K_LEFT : (f / 20 % 3 == 1 ? K_A : 0);
		emu_state_save(&master, snap);
		emu_frame(&master, k);
		int st = rd(&master, 0xD4AC, -1);
		if (st != 1 || f < 100 + got * 300) continue;
		cap(&master, A[got]);
		emu_state_load(&g, snap);
		wr(&g, 0xFFD2, (st ^ 1) + 1);
		int n = 0;
		while (rd(&g, 0xFFD2, -1) && n < 30) emu_frame(&g, k), ++n;
		bool seenWhite = false;
		for (int x = 0; x < 40; ++x) {
			emu_frame(&g, k);
			if (white(g.fb)) seenWhite = true;
			else if (seenWhite) break;
		}
		for (int x = 0; x < 4; ++x) emu_frame(&g, k);
		cap(&g, B[got]);
		fprintf(stderr, "run %d at f%d: stage now %d\n", got, f, rd(&g, 0xD4AC, -1));
		++got;
	}
	int cnt = 0, same = 0, dyn = 0;
	for (int i = 0; i < N; ++i) {
		if (B[0][i] == A[0][i] && B[1][i] == A[1][i]) continue;
		char s[32]; addr_of(i, s);
		if (B[0][i] == B[1][i]) { ++same; printf("const %s: %02x -> %02x\n", s, A[0][i], B[0][i]); }
		else { ++dyn; if (getenv("DYN")) printf("dyn   %s: %02x/%02x -> %02x/%02x\n", s, A[0][i], A[1][i], B[0][i], B[1][i]); }
		++cnt;
	}
	fprintf(stderr, "changed %d: constant %d, dynamic %d\n", cnt, same, dyn);
	return 0;
}

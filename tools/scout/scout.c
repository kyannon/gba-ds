/* Pinball scouting: logs, per frame, VRAM writes (by region, with the PCs
 * that made them), video register writes made during visible lines, DMA
 * timing and IRQ setup, and the camera row (docs/PINBALL.md, "Scouting").
 *   scout ROM STATE FRAMES [seed]
 * Input: both flippers for 6 frames in every 50, plus random taps
 * (SCOUT_KEYS=file: a harness keys script's hold lines instead).
 * Env: SCOUT_RING=1 compares, each frame, the BG3 tile ring with the EWRAM
 * table for the rows in view; SCOUT_OAM=1 lists the shadow OAM at the end;
 * SCOUT_DUMP=file saves VRAM + EWRAM at the end.
 */
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/core/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"

static struct GBA* gba;
static void (*origVRAM)(struct GBAVideoRenderer*, uint32_t);
static uint16_t (*origReg)(struct GBAVideoRenderer*, uint32_t, uint16_t);

#define NB 48 /* 0x18000 / 0x800 */
static unsigned vramCount[NB];
#define NPC 64
static struct { uint32_t pc; unsigned n; uint32_t lo, hi; } pcs[NPC];
static int npcs;
static unsigned midReg[0x60];  /* writes during lines 0..159, by reg/2 */
static int midLines[0x60][2];

static void hook_vram(struct GBAVideoRenderer* r, uint32_t a) {
	origVRAM(r, a);
	if (a < 0x18000) vramCount[a / 0x800]++;
	uint32_t pc = gba->cpu->gprs[ARM_PC];
	int i;
	for (i = 0; i < npcs; ++i) if (pcs[i].pc == pc) break;
	if (i == npcs && npcs < NPC) { pcs[npcs].pc = pc; pcs[npcs].n = 0; pcs[npcs].lo = a; pcs[npcs].hi = a; npcs++; }
	if (i < npcs) { pcs[i].n++; if (a < pcs[i].lo) pcs[i].lo = a; if (a > pcs[i].hi) pcs[i].hi = a; }
}
static int curFrame;
static uint16_t hook_reg(struct GBAVideoRenderer* r, uint32_t a, uint16_t v) {
	int vc = gba->video.vcount;
	if (getenv("SCOUT_BG0") && (a == 0x10 || a == 0x12) && curFrame % 60 == 0) printf("  f%d line %d BG0%cOFS = %d\n", curFrame, vc, a == 0x10 ? 'H' : 'V', v);
	if (vc > 0 && vc < 160 && a < 0xC0) {
		midReg[a >> 1]++;
		if (!midLines[a >> 1][0] || vc < midLines[a >> 1][0]) midLines[a >> 1][0] = vc;
		if (vc > midLines[a >> 1][1]) midLines[a >> 1][1] = vc;
	}
	return origReg(r, a, v);
}

int main(int argc, char** argv) {
	if (argc < 4) { fprintf(stderr, "scout ROM STATE FRAMES [rngseed]\n"); return 1; }
	Emu e = {0};
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) { fprintf(stderr, "no state\n"); return 1; }
	gba = (struct GBA*) e.core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
	origVRAM = r->writeVRAM; r->writeVRAM = hook_vram;
	origReg = r->writeVideoRegister; r->writeVideoRegister = hook_reg;
	int frames = atoi(argv[3]);
	unsigned seed = argc > 4 ? (unsigned) atoi(argv[4]) : 1;
	static unsigned totVram[NB];
	static struct { uint32_t pc; unsigned n; uint32_t lo, hi; } allpcs[256];
	int nall = 0;
	for (int f = 0; f < frames; ++f) {
		memset(vramCount, 0, sizeof vramCount); npcs = 0;
		/* flippers: both, a short tap every so often */
		seed = seed * 1103515245u + 12345u;
		uint32_t keys = ((f % 50) < 6) ? (K_L | K_A) : 0;
		if (((seed >> 16) & 63) == 0) keys = K_L | K_A;
		/* SCOUT_KEYS: a harness keys script (hold lines) instead */
		static uint32_t sk[100000]; static int nsk = -1;
		if (nsk < 0) {
			nsk = 0;
			FILE* kf = getenv("SCOUT_KEYS") ? fopen(getenv("SCOUT_KEYS"), "r") : NULL;
			char line[256], b[64]; int n;
			static const char* names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
			while (kf && fgets(line, sizeof line, kf))
				if (sscanf(line, "hold %63s %d", b, &n) == 2) {
					uint32_t k = 0;
					for (char* t = strtok(b, "+"); t; t = strtok(NULL, "+")) for (int i = 0; i < 10; ++i) if (!strcmp(t, names[i])) k |= 1u << i;
					for (int j = 0; j < n && nsk < 100000; ++j) sk[nsk++] = k;
				}
			if (kf) fclose(kf);
		}
		if (getenv("SCOUT_KEYS")) keys = f < nsk ? sk[f] : 0;
		curFrame = f;
		emu_frame(&e, keys);
		uint32_t S = emu_read32(&e, 0x020314E0);
		int cam = (int16_t) emu_read16(&e, S + 0x4E);
		uint32_t ball = emu_read32(&e, S + 0x1330);
		int by = ball ? (int) (emu_read32(&e, ball + 0x38) >> 8) : -1;
		unsigned tot = 0; for (int i = 0; i < NB; ++i) { tot += vramCount[i]; totVram[i] += vramCount[i]; }
		printf("f%04d cam %3d bally %4d vofs %3d vram %5u", f, cam, by, emu_io16(&e, 0x1E), tot);
		for (int i = 0; i < NB; ++i) if (vramCount[i]) printf(" %05X:%u", i * 0x800, vramCount[i]);
		for (int i = 0; i < npcs; ++i) {
			printf(" pc%08X[%05X-%05X]x%u", pcs[i].pc, pcs[i].lo, pcs[i].hi, pcs[i].n);
			int j; for (j = 0; j < nall; ++j) if (allpcs[j].pc == pcs[i].pc) break;
			if (j == nall && nall < 256) { allpcs[nall].pc = pcs[i].pc; allpcs[nall].n = 0; allpcs[nall].lo = pcs[i].lo; allpcs[nall].hi = pcs[i].hi; nall++; }
			if (j < nall) { allpcs[j].n += pcs[i].n; if (pcs[i].lo < allpcs[j].lo) allpcs[j].lo = pcs[i].lo; if (pcs[i].hi > allpcs[j].hi) allpcs[j].hi = pcs[i].hi; }
		}
		if (getenv("SCOUT_RING")) {
			const uint8_t* v = (const uint8_t*) emu_vram(&e);
			size_t sz; uint8_t* w = emu_memory(&e, 2, &sz);
			int T = cam / 8, mism = 0, ewch = 0;
			static uint8_t prevEw[56 * 0x400]; static int havePrev;
			for (int R = T; R < T + 21 && R < 56; ++R) {
				int s = (R + 10) % 22; uint32_t src = R <= 31 ? 0xFBB0 + R * 0x400 : 0x30A0 + (R - 32) * 0x400;
				for (int k = 0; k < 0x400; k += 32) if (memcmp(v + 0x8000 + s * 0x400 + k, w + src, 0) || memcmp(v + 0x8000 + s * 0x400 + k, w + src + k, 32)) mism++;
			}
			for (int R = 0; R < 56; ++R) { uint32_t src = R <= 31 ? 0xFBB0 + R * 0x400 : 0x30A0 + (R - 32) * 0x400;
				if (havePrev && memcmp(prevEw + R * 0x400, w + src, 0x400)) ewch++;
				memcpy(prevEw + R * 0x400, w + src, 0x400); }
			havePrev = 1;
			printf(" RING mism %d ewchanged %d", mism, ewch);
		}
		printf("\n");
	}
	if (getenv("SCOUT_DUMP")) {
		FILE* o = fopen(getenv("SCOUT_DUMP"), "wb");
		fwrite(emu_vram(&e), 1, 0x18000, o);
		size_t sz; uint8_t* w = emu_memory(&e, 2, &sz); fwrite(w, 1, 0x40000, o);
		fclose(o);
	}
	if (getenv("SCOUT_OAM")) {
		uint32_t S = emu_read32(&e, 0x020314E0);
		printf("== shadow OAM, cam %d\n", (int16_t) emu_read16(&e, S + 0x4E));
		for (int i = 0; i < 128; ++i) {
			uint16_t a0 = emu_read16(&e, 0x03005000 + i * 8), a1 = emu_read16(&e, 0x03005002 + i * 8), a2 = emu_read16(&e, 0x03005004 + i * 8);
			if ((a0 & 0x300) == 0x200) continue;
			printf("  %3d y %3d x %3d tile %3d pal %d prio %d shape %d size %d\n", i, a0 & 0xff, a1 & 0x1ff, a2 & 0x3ff, a2 >> 12, (a2 >> 10) & 3, a0 >> 14, a1 >> 14);
		}
	}
	printf("\n== totals by 2KB VRAM block\n");
	for (int i = 0; i < NB; ++i) if (totVram[i]) printf("  %05X %u\n", i * 0x800, totVram[i]);
	printf("== PCs writing VRAM\n");
	for (int j = 0; j < nall; ++j) printf("  %08X %05X-%05X %u\n", allpcs[j].pc, allpcs[j].lo, allpcs[j].hi, allpcs[j].n);
	printf("== video register writes during lines 1-159\n");
	for (int i = 0; i < 0x60; ++i) if (midReg[i]) printf("  reg %03X: %u writes, lines %d-%d\n", i * 2, midReg[i], midLines[i][0], midLines[i][1]);
	printf("== IRQ/DMA at end\n  DISPSTAT %04X IE %04X\n", emu_io16(&e, 0x04), emu_io16(&e, 0x200));
	for (int d = 0; d < 4; ++d) printf("  DMA%d CNT_H %04X\n", d, emu_io16(&e, 0xBA + d * 12));
	for (int i = 0; i < 4; ++i) printf("  BG%dCNT %04X\n", i, emu_io16(&e, 0x08 + i * 2));
	printf("  DISPCNT %04X\n", emu_io16(&e, 0x00));
	return 0;
}

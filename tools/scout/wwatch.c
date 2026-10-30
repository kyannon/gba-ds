/* Write watch: logs the PCs that write given addresses, per frame.
 *   wwatch ROM STATE FRAMES ADDR[:LEN]... [-k KEYSFILE]
 * KEYSFILE: a harness keys script (hold BUTTONS N lines; others ignored).
 * Prints "fNNNN ADDR <- VALUE (size) pc PC lr LR" for each write in a range;
 * -r: reads too ("fNNNN ADDR read (size) pc PC lr LR").
 */
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/core/core.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"

static struct ARMCore* cpu;
static void (*o32)(struct ARMCore*, uint32_t, int32_t, int*);
static void (*o16)(struct ARMCore*, uint32_t, int16_t, int*);
static void (*o8)(struct ARMCore*, uint32_t, int8_t, int*);
static uint32_t (*oMul)(struct ARMCore*, uint32_t, int, enum LSMDirection, int*);
static uint32_t (*l32)(struct ARMCore*, uint32_t, int*);
static uint32_t (*l16)(struct ARMCore*, uint32_t, int*);
static uint32_t (*l8)(struct ARMCore*, uint32_t, int*);
static int reads;
static struct { uint32_t lo, hi; } rng[16];
static int nr, curFrame;

static void hit(uint32_t a, int size, uint32_t v) {
	for (int i = 0; i < nr; ++i)
		if (a + size > rng[i].lo && a < rng[i].hi) {
			uint32_t pc = cpu->gprs[ARM_PC] - (cpu->executionMode == MODE_THUMB ? 4 : 8);
			printf("f%04d %08X <- %0*X (%d) pc %08X lr %08X\n", curFrame, a, size * 2, v, size, pc, cpu->gprs[ARM_LR]);
			return;
		}
}
static void s32(struct ARMCore* c, uint32_t a, int32_t v, int* cc) { hit(a & ~3, 4, v); o32(c, a, v, cc); }
static void s16(struct ARMCore* c, uint32_t a, int16_t v, int* cc) { hit(a & ~1, 2, (uint16_t) v); o16(c, a, v, cc); }
static void s8(struct ARMCore* c, uint32_t a, int8_t v, int* cc) { hit(a, 1, (uint8_t) v); o8(c, a, v, cc); }
static uint32_t sMul(struct ARMCore* c, uint32_t base, int mask, enum LSMDirection d, int* cc) {
	int n = __builtin_popcount(mask & 0xFFFF);
	uint32_t a = base & ~3;
	if (d & LSM_D) a -= n * 4; /* rough: decrementing */
	for (int i = 0; i < n; ++i) hit(a + i * 4, 4, 0);
	return oMul(c, base, mask, d, cc);
}

static void rhit(uint32_t a, int size) {
	for (int i = 0; i < nr; ++i)
		if (a + size > rng[i].lo && a < rng[i].hi) {
			uint32_t pc = cpu->gprs[ARM_PC] - (cpu->executionMode == MODE_THUMB ? 4 : 8);
			printf("f%04d %08X read (%d) pc %08X lr %08X\n", curFrame, a, size, pc, cpu->gprs[ARM_LR]);
			return;
		}
}
static uint32_t r32(struct ARMCore* c, uint32_t a, int* cc) { rhit(a & ~3, 4); return l32(c, a, cc); }
static uint32_t r16(struct ARMCore* c, uint32_t a, int* cc) { rhit(a & ~1, 2); return l16(c, a, cc); }
static uint32_t r8(struct ARMCore* c, uint32_t a, int* cc) { rhit(a, 1); return l8(c, a, cc); }

static uint32_t parse_keys(const char* s) {
	uint32_t k = 0;
	static const char* names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
	char buf[128]; snprintf(buf, sizeof buf, "%s", s);
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+"))
		for (int i = 0; i < 10; ++i) if (!strcmp(t, names[i])) k |= 1u << i;
	return k;
}

int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "wwatch ROM STATE FRAMES ADDR[:LEN]... [-k KEYS]\n"); return 1; }
	Emu e = {0};
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) { fprintf(stderr, "no state\n"); return 1; }
	int frames = atoi(argv[3]);
	static uint32_t keys[100000]; int nk = 0;
	for (int i = 4; i < argc; ++i) {
		if (!strcmp(argv[i], "-r")) { reads = 1; continue; }
		if (!strcmp(argv[i], "-k") && i + 1 < argc) {
			FILE* f = fopen(argv[++i], "r"); char line[256], b[64]; int n;
			while (f && fgets(line, sizeof line, f))
				if (sscanf(line, "hold %63s %d", b, &n) == 2) for (int j = 0; j < n && nk < 100000; ++j) keys[nk++] = parse_keys(b);
			if (f) fclose(f);
			continue;
		}
		char* c = strchr(argv[i], ':');
		rng[nr].lo = strtoul(argv[i], NULL, 0);
		rng[nr].hi = rng[nr].lo + (c ? strtoul(c + 1, NULL, 0) : 2);
		nr++;
	}
	struct GBA* gba = (struct GBA*) e.core->board;
	cpu = gba->cpu;
	o32 = cpu->memory.store32; cpu->memory.store32 = s32;
	o16 = cpu->memory.store16; cpu->memory.store16 = s16;
	o8 = cpu->memory.store8; cpu->memory.store8 = s8;
	oMul = cpu->memory.storeMultiple; cpu->memory.storeMultiple = sMul;
	if (reads) {
		l32 = cpu->memory.load32; cpu->memory.load32 = r32;
		l16 = cpu->memory.load16; cpu->memory.load16 = r16;
		l8 = cpu->memory.load8; cpu->memory.load8 = r8;
	}
	for (curFrame = 0; curFrame < frames; ++curFrame) emu_frame(&e, curFrame < nk ? keys[curFrame] : 0);
	return 0;
}

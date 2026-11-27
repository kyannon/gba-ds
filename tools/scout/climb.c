/* The level over both screens (src/climb.c), as a picture: the game from STATE
 * with the copy's rows above it, a frame every STEP frames.
 *   climb ROM STATE OUTPREFIX FRAMES [-k KEYSFILE] [-s STEP] [-w FRAME:ADDR=VALUE]...
 * KEYSFILE: a harness script (hold BUTTONS N lines). -w writes a 32-bit value
 * into the game before that frame. Writes OUTPREFIX-NNNN.png (240x336: the copy, a
 * 16-row grey bar, the game). Build as docs/PINBALL.md says, with src/climb.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"
#include "climb.h"
#include "profiles.h"

static uint32_t parse_keys(const char* s) {
	uint32_t k = 0;
	static const char* names[] = {"A", "B", "SELECT", "START", "RIGHT", "LEFT", "UP", "DOWN", "R", "L"};
	char buf[128]; snprintf(buf, sizeof buf, "%s", s);
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+"))
		for (int i = 0; i < 10; ++i) if (!strcmp(t, names[i])) k |= 1u << i;
	return k;
}

int main(int argc, char** argv) {
	if (argc < 5) { fprintf(stderr, "climb ROM STATE OUTPREFIX FRAMES [-k KEYS] [-s STEP]\n"); return 1; }
	Emu e = {0};
	if (!emu_open(&e, argv[1], NULL, false) || !emu_state_load_file(&e, argv[2])) return 1;
	const MapProfile* p = profile_find(e.gameCode);
	if (!p || !climb_open(argv[1], p, &e, false)) { fprintf(stderr, "no climb\n"); return 1; }
	int frames = atoi(argv[4]), step = 1;
	static uint32_t keys[100000]; int nk = 0;
	struct { int f; uint32_t a, v; } wr[16]; int nw = 0;
	for (int i = 5; i < argc; ++i) {
		if (!strcmp(argv[i], "-s") && i + 1 < argc) step = atoi(argv[++i]);
		else if (!strcmp(argv[i], "-w") && i + 1 < argc && nw < 16) {
			int f; unsigned a, v;
			if (sscanf(argv[++i], "%d:%x=%i", &f, &a, &v) == 3) wr[nw].f = f, wr[nw].a = a, wr[nw++].v = v;
		}
		else if (!strcmp(argv[i], "-k") && i + 1 < argc) {
			FILE* f = fopen(argv[++i], "r"); char line[256], b[64]; int n;
			while (f && fgets(line, sizeof line, f))
				if (sscanf(line, "hold %63s %d", b, &n) == 2) for (int j = 0; j < n && nk < 100000; ++j) keys[nk++] = parse_keys(b);
			if (f) fclose(f);
		}
	}
	static uint32_t sheet[GBA_W * (GBA_H * 2 + 16)];
	for (int f = 0; f < frames; ++f) {
		uint32_t k = f < nk ? keys[f] : 0;
		for (int j = 0; j < nw; ++j) if (wr[j].f == f) emu_write32(&e, wr[j].a, wr[j].v);
		climb_begin(&e, k, true);
		emu_frame(&e, k & 0x3FF);
		climb_end(&e);
		if (f % step) continue;
		const uint32_t* top = climb_picture();
		for (int y = 0; y < GBA_H * 2 + 16; ++y)
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t v = 0xFF303030;
				if (y < GBA_H) v = top ? top[y * GBA_W + x] : 0xFF000000;
				else if (y >= GBA_H + 16) v = e.fb[(y - GBA_H - 16) * GBA_W + x];
				sheet[y * GBA_W + x] = v | 0xFF000000;
			}
		char name[512]; snprintf(name, sizeof name, "%s-%04d.png", argv[3], f);
		write_png(name, sheet, GBA_W, GBA_H * 2 + 16, GBA_W);
	}
	climb_close();
	return 0;
}

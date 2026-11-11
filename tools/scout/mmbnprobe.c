/* Mega Man Battle Network experiment: where can MegaMan go, asked of the
 * game's walls a grid cell at a time, and the area's picture from as few
 * frames as that allows (profiles.c's net maps, MMBN-6, do the same).
 *   mmbnprobe ROM STATE OUT.png [GRID] [CELLS.png]
 * From a moment of him walking (STATE, DOWN held 4 frames), he's set down
 * at a place reached with his step (diff_x / diff_y, +0x1C / +0x20 from
 * his position) set to cross one GRID cell, one of 8 ways (DIRS), in
 * PROBEF frames (8: his running stride; 4 and less slip through some of
 * the US maze's walls); where he ends up is reached (breadth first). Then,
 * nearest first, a frame (BG2 and BG3 on the net) wherever the camera
 * hasn't yet seen SPANX x SPANY (120x80) around a place (ALL=1: at every
 * place), SETTLE frames (5) after he's set down, put where the camera was.
 * CELLS.png: the grid, white where he got. Without a key, or with the
 * game's own step, he stands or walks anywhere, off the paths too: only a
 * wall crossed stops him (the first try of this tool).
 * Build like tools/scout/jump.c (docs/PINBALL.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu.h"
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#define CAM 0x02004950
#define GSTATE 0x02006CB8
#define AREA 0x02000214

static Emu e;
static uint8_t st0[0x80000];

static long ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

int main(int argc, char** argv) {
	if (argc < 4) return fprintf(stderr, "mmbnprobe ROM STATE OUT.png [GRID] [CELLS.png]\n"), 1;
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) return 1;
	emu_state_save(&e, st0);
	int grid = argc > 4 ? atoi(argv[4]) : 16;
	int probef = getenv("PROBEF") ? atoi(getenv("PROBEF")) : 8;
	int spanx = getenv("SPANX") ? atoi(getenv("SPANX")) : 120, spany = getenv("SPANY") ? atoi(getenv("SPANY")) : 80;
	uint32_t pos = emu_read32(&e, CAM + 0x10);
	int xl = (int32_t) emu_read32(&e, CAM + 0x14) >> 16, xu = (int32_t) emu_read32(&e, CAM + 0x18) >> 16;
	int yu = (int32_t) emu_read32(&e, CAM + 0x1C) >> 16, yl = (int32_t) emu_read32(&e, CAM + 0x20) >> 16;
	if (yl > yu) { int t = yl; yl = yu; yu = t; }
	int cx0 = xl + yl - 240, cy0 = (yl - xu) / 2 - 240, W = xu + yu + 240 - cx0, H = (yu - xl) / 2 + 160 - cy0;
	xl -= 64, xu += 64, yl -= 64, yu += 64;
	uint16_t area = emu_read16(&e, AREA);
	int32_t x0 = (int32_t) emu_read32(&e, pos), y0 = (int32_t) emu_read32(&e, pos + 4), z0 = (int32_t) emu_read32(&e, pos + 8);
	emu_layers(&e, (area & 0xFF) >= 0x80 ? 0x0C : 0x0E);
	emu_state_load(&e, st0);
	int gw = (xu - xl) / grid + 1, gh = (yu - yl) / grid + 1;
	uint8_t* ok = calloc((size_t) gw * gh, 1);
	int32_t* z = calloc((size_t) gw * gh, 4);
	/* a walking moment: the same key held all along keeps the game's step
	 * (diff_x / diff_y, +0x1C / +0x20 from his position) as we set it */
	emu_state_load(&e, st0);
	for (int f = 0; f < 4; ++f) emu_frame_blind(&e, K_DOWN);
	static uint8_t st1[0x80000];
	emu_state_save(&e, st1);
	long t0 = ms();
	long frames = 0;
	int nok = 0, tries = 0;
	int* queue = malloc(sizeof(int) * gw * gh);
	int qh = 0, qt = 0;
	/* each cell: the place (16.16) he stood in it */
	int32_t* at = calloc((size_t) gw * gh * 2, 4);
#define CELLOF(X, Y) (((((Y) >> 16) - yl + grid / 2) / grid) * gw + ((((X) >> 16) - xl + grid / 2) / grid))
	{
		int c = CELLOF(x0, y0);
		ok[c] = 1, z[c] = z0, at[c * 2] = x0, at[c * 2 + 1] = y0, queue[qt++] = c, ++nok;
	}
	static const int dirs[8][2] = { { 1, 0 }, { -1, 0 }, { 0, 1 }, { 0, -1 }, { 1, 1 }, { -1, -1 }, { 1, -1 }, { -1, 1 } };
	int ndirs = getenv("DIRS") ? atoi(getenv("DIRS")) : 8;
	while (qh < qt) {
		int c = queue[qh++];
		int32_t ax = at[c * 2], ay = at[c * 2 + 1];
		for (int w = 0; w < ndirs; ++w) {
			int32_t bx = ax + (dirs[w][0] * grid << 16), by = ay + (dirs[w][1] * grid << 16);
			if ((bx >> 16) < xl || (bx >> 16) > xu || (by >> 16) < yl || (by >> 16) > yu) continue;
			if (ok[CELLOF(bx, by)]) continue;
			emu_state_load(&e, st1);
			emu_write32(&e, pos, (uint32_t) ax);
			emu_write32(&e, pos + 4, (uint32_t) ay);
			emu_write32(&e, pos + 8, (uint32_t) z[c]);
			emu_write32(&e, pos + 0x1C, (uint32_t) ((bx - ax) / probef));
			emu_write32(&e, pos + 0x20, (uint32_t) ((by - ay) / probef));
			for (int f = 0; f < probef; ++f) emu_frame_blind(&e, K_DOWN);
			frames += probef, ++tries;
			if (emu_read8(&e, GSTATE) != 4 || emu_read16(&e, AREA) != area) continue;
			int32_t nx = (int32_t) emu_read32(&e, pos), ny = (int32_t) emu_read32(&e, pos + 4);
			if ((nx >> 16) < xl || (nx >> 16) > xu || (ny >> 16) < yl || (ny >> 16) > yu) continue;
			int n = CELLOF(nx, ny);
			if (ok[n]) continue;
			ok[n] = 1, z[n] = (int32_t) emu_read32(&e, pos + 8), at[n * 2] = nx, at[n * 2 + 1] = ny, queue[qt++] = n, ++nok;
		}
	}
	fprintf(stderr, "%d tries\n", tries);
	long t1 = ms();
	/* the pictures: nearest first, wherever the place's middle is unseen */
	uint32_t* pic = calloc((size_t) W * H, 4);
	uint8_t* seen = calloc((size_t) W * H, 1);
	int n = gw * gh, shots = 0;
	int* order = malloc(sizeof(int) * n);
	long* d = malloc(sizeof(long) * n);
	for (int k = 0; k < n; ++k) {
		order[k] = k;
		long dx = xl + (k % gw) * grid - (x0 >> 16), dy = yl + (k / gw) * grid - (y0 >> 16);
		d[k] = dx * dx + dy * dy;
	}
	for (int a = 1; a < n; ++a) { /* (insertion sort: fine for a scout) */
		int v = order[a];
		int b = a - 1;
		while (b >= 0 && d[order[b]] > d[v]) order[b + 1] = order[b], --b;
		order[b + 1] = v;
	}
	for (int a = 0; a < n; ++a) {
		int k = order[a];
		if (!ok[k]) continue;
		int x = at[k * 2] >> 16, y = at[k * 2 + 1] >> 16;
		int sx = x + y - cx0, sy = (y - x) / 2 - cy0;
		bool need = false;
		for (int dy = -spany / 2; dy <= spany / 2 && !need; dy += 8)
			for (int dx = -spanx / 2; dx <= spanx / 2 && !need; dx += 8) {
				int qx = sx + dx, qy = sy + dy;
				if (qx >= 0 && qy >= 0 && qx < W && qy < H && !seen[(size_t) qy * W + qx]) need = true;
			}
		if (!need && !getenv("ALL")) continue;
		emu_state_load(&e, st0);
		emu_write32(&e, pos, (uint32_t) x << 16);
		emu_write32(&e, pos + 4, (uint32_t) y << 16);
		emu_write32(&e, pos + 8, (uint32_t) z[k]);
		int settle = getenv("SETTLE") ? atoi(getenv("SETTLE")) : 5;
		for (int f = 0; f < settle; ++f) emu_frame_blind(&e, 0);
		emu_frame(&e, 0);
		frames += settle + 1;
		if (emu_read8(&e, GSTATE) != 4 || emu_read16(&e, AREA) != area) continue;
		int camx = (int16_t) emu_read16(&e, CAM + 4), camy = (int16_t) emu_read16(&e, CAM + 6);
		for (int py = 0; py < 160; ++py) {
			int qy = camy - 80 + py - cy0;
			if (qy < 0 || qy >= H) continue;
			for (int px = 0; px < 240; ++px) {
				int qx = camx - 120 + px - cx0;
				if (qx < 0 || qx >= W) continue;
				pic[(size_t) qy * W + qx] = e.fb[py * 240 + px] | 0xFF000000;
				seen[(size_t) qy * W + qx] = 1;
			}
		}
		++shots;
	}
	long t2 = ms();
	fprintf(stderr, "area %04X: %dx%d grid, %d can stand (%ld ms); %d pictures (%ld ms); %ld frames\n", area, gw, gh, nok, t1 - t0,
	        shots, t2 - t1, frames);
	if (argc > 5) {
		uint32_t* c = calloc((size_t) gw * gh, 4);
		for (int k = 0; k < gw * gh; ++k) c[k] = ok[k] ? 0xFFFFFFFF : 0xFF000000;
		write_png(argv[5], c, gw, gh, gw);
	}
	return write_png(argv[3], pic, W, H, W) ? 0 : 1;
}

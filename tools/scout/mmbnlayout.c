/* Mega Man Battle Network experiment: an area's whole picture, drawn by the
 * game from the layout it holds for the area (MMBN-6).
 *   mmbnlayout ROM STATE OUT.png
 * The game keeps the whole area's layout (gBgDataManager, 0x02009A50: its
 * width and height in tiles, bytes +0 / +1; the layout, +0xC) and redraws
 * the screen from it when the camera jumps. On a copy with gCamera's bounds
 * opened up, MegaMan (and so the camera) is set down a screen at a time
 * over the layout (centred on the camera's 0,0: x -4w..4w, y -4h..4h), and
 * each frame of BG2 and BG3 goes into the picture where the camera was;
 * the backdrop colour (palette 0) magenta in the copy, so what's left
 * transparent stays empty. A layer with nothing transparent over the area
 * (Lan's PC's patterned floor on BG2) is the backdrop: left out.
 * Env: SETTLE frames (4), LOG=1. Build like tools/scout/jump.c. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "emu.h"
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#define CAM 0x02004950
#define GSTATE 0x02006CB8
#define AREA 0x02000214
#define BGDM 0x02009A50
#define MAGENTA 0x7C1F

static Emu e;
static uint8_t st0[0x80000];

static long ms(void) {
	struct timespec t;
	clock_gettime(CLOCK_MONOTONIC, &t);
	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
}

static uint32_t pos;
static uint16_t area;
static int settle = 4;

/* the camera brought to (cx, cy) (as near as the game takes it); false if
 * the game left walking about there (a warp pad, a battle) */
static bool look(int cx, int cy, unsigned layers, int* camx, int* camy) {
	/* (layers take effect as a state's registers load) */
	emu_layers(&e, layers);
	emu_state_load(&e, st0);
	emu_write32(&e, CAM + 0x14, (uint32_t) (-30000 << 16));
	emu_write32(&e, CAM + 0x18, (uint32_t) (30000 << 16));
	emu_write32(&e, CAM + 0x1C, (uint32_t) (30000 << 16));
	emu_write32(&e, CAM + 0x20, (uint32_t) (-30000 << 16));
	int x = (cx - 2 * cy) / 2, y = (cx + 2 * cy) / 2;
	emu_write32(&e, pos, (uint32_t) x << 16);
	emu_write32(&e, pos + 4, (uint32_t) y << 16);
	for (int f = 0; f < settle; ++f) emu_frame_blind(&e, 0);
	emu_write16(&e, 0x05000000, MAGENTA);
	emu_frame(&e, 0);
	*camx = (int16_t) emu_read16(&e, CAM + 4), *camy = (int16_t) emu_read16(&e, CAM + 6);
	return emu_read8(&e, GSTATE) == 4 && emu_read16(&e, AREA) == area;
}

int main(int argc, char** argv) {
	if (argc < 4) return fprintf(stderr, "mmbnlayout ROM STATE OUT.png\n"), 1;
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) return 1;
	emu_state_save(&e, st0);
	if (getenv("SETTLE")) settle = atoi(getenv("SETTLE"));
	pos = emu_read32(&e, CAM + 0x10);
	area = emu_read16(&e, AREA);
	int tw = emu_read8(&e, BGDM), th = emu_read8(&e, BGDM + 1);
	int W = tw * 8, H = th * 8, X0 = -tw * 4, Y0 = -th * 4;
	long t0 = ms();
	/* the backdrop: a layer alone, anything transparent over a few screens? */
	unsigned layers = 0;
	uint32_t magenta = 0;
	for (int L = 2; L <= 3; ++L) {
		long clear = 0, drawn = 0;
		for (int k = 0; k < 9; ++k) {
			int cx, cy;
			if (!look(X0 + W * (1 + k % 3) / 4, Y0 + H * (1 + k / 3) / 4, 1u << L, &cx, &cy)) continue;
			if (!magenta) {
				/* (magenta as the renderer puts it out) */
				look(X0 + W / 2, Y0 + H / 2, 0, &cx, &cy);
				magenta = e.fb[0] & 0xFFFFFF;
				look(X0 + W * (1 + k % 3) / 4, Y0 + H * (1 + k / 3) / 4, 1u << L, &cx, &cy);
			}
			for (int i = 0; i < GBA_W * GBA_H; ++i) if ((e.fb[i] & 0xFFFFFF) == magenta) ++clear; else ++drawn;
		}
		if (getenv("LOG")) fprintf(stderr, "BG%d: %ld drawn, %ld clear\n", L, drawn, clear);
		if (drawn && clear) layers |= 1u << L;
	}
	if (!layers) return fprintf(stderr, "no layers\n"), 1;
	uint32_t* pic = calloc((size_t) W * H, 4);
	uint8_t* seen = calloc((size_t) W * H, 1);
	int shots = 0, misses = 0;
	for (int pass = 0; pass < 2; ++pass)
		for (int cy = Y0 + 80; cy < Y0 + H + 80; cy += 152)
			for (int cx = X0 + 120; cx < X0 + W + 120; cx += 232) {
				/* (the second pass: only where the first left something unseen) */
				int tx = cx > X0 + W - 120 ? X0 + W - 120 : cx, ty = cy > Y0 + H - 80 ? Y0 + H - 80 : cy;
				if (pass) {
					bool need = false;
					for (int y = ty - 80; y < ty + 80 && !need; y += 4)
						for (int x = tx - 120; x < tx + 120 && !need; x += 4)
							need = x >= X0 && y >= Y0 && x < X0 + W && y < Y0 + H && !seen[(size_t) (y - Y0) * W + x - X0];
					if (!need) continue;
				}
				int camx, camy;
				++shots;
				if (!look(tx, ty, layers, &camx, &camy)) {
					++misses;
					if (getenv("LOG")) fprintf(stderr, "no look at %d,%d\n", tx, ty);
					continue;
				}
				for (int sy = 0; sy < GBA_H; ++sy) {
					int py = camy - 80 + sy - Y0;
					if (py < 0 || py >= H) continue;
					for (int sx = 0; sx < GBA_W; ++sx) {
						int px = camx - 120 + sx - X0;
						if (px < 0 || px >= W) continue;
						uint32_t c = e.fb[sy * GBA_W + sx] & 0xFFFFFF;
						pic[(size_t) py * W + px] = c == magenta ? 0 : c | 0xFF000000;
						seen[(size_t) py * W + px] = 1;
					}
				}
			}
	long unseen = 0;
	for (long i = 0; i < (long) W * H; ++i) unseen += !seen[i];
	fprintf(stderr, "area %04X: %dx%d tiles, layers %X, %d screens (%d missed), %ld pixels unseen, %ld ms\n", area, tw, th, layers,
	        shots, misses, unseen, ms() - t0);
	return write_png(argv[3], pic, W, H, W) ? 0 : 1;
}

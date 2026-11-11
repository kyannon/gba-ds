/* Mega Man Battle Network experiment: a whole area's picture, stitched from
 * frames of a copy whose MegaMan (or Lan) walks everywhere he can.
 *   mmbnmap ROM STATE OUT.png [GRID]
 * STATE is walking about in the area. From where he stands, the copy tries
 * walking each of 8 ways for a few frames, from every place reached so far
 * (a breadth-first walk over a GRID-unit grid of the area, map units: x
 * up-right, y down-right; MegaMan's position is gCamera.playerRealPos,
 * 16.16), each try from STATE's own moment with him set down at the place.
 * At each place reached, the frame's BG layers (no sprites, no BG0's
 * messages; on the net, no BG1, its sky) go into the picture at the
 * camera's screen position (cameraX = x + y, cameraY = (y - x) / 2): only
 * places he can reach, so the camera never shows what's past the area's
 * own tiles. A try that leaves the area (a warp) or starts a battle counts
 * as a wall. Env: STEPF=n frames a try walks (8), LOG=1.
 * Build like tools/scout/jump.c (docs/PINBALL.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#define CAM 0x02004950
#define GSTATE 0x02006CB8
#define AREA 0x02000214

static Emu e;
static uint8_t st0[0x80000];
static uint32_t pos;
static uint16_t area;

/* from STATE's moment, set down at (x, y, z) (16.16), walk `keys` for n
 * frames; false if he left the area or a battle began */
static bool walk(int32_t x, int32_t y, int32_t z, uint32_t keys, int n) {
	emu_state_load(&e, st0);
	emu_write32(&e, pos, (uint32_t) x);
	emu_write32(&e, pos + 4, (uint32_t) y);
	emu_write32(&e, pos + 8, (uint32_t) z);
	for (int f = 0; f < n; ++f) emu_frame_blind(&e, keys);
	return emu_read8(&e, GSTATE) == 4 && emu_read16(&e, AREA) == area;
}

int main(int argc, char** argv) {
	if (argc < 4) return fprintf(stderr, "mmbnmap ROM STATE OUT.png [GRID]\n"), 1;
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) return 1;
	emu_state_save(&e, st0);
	int grid = argc > 4 ? atoi(argv[4]) : 16, stepf = getenv("STEPF") ? atoi(getenv("STEPF")) : 8;
	pos = emu_read32(&e, CAM + 0x10);
	int xl = (int32_t) emu_read32(&e, CAM + 0x14) >> 16, xu = (int32_t) emu_read32(&e, CAM + 0x18) >> 16;
	int yu = (int32_t) emu_read32(&e, CAM + 0x1C) >> 16, yl = (int32_t) emu_read32(&e, CAM + 0x20) >> 16;
	if (yl > yu) { int t = yl; yl = yu; yu = t; }
	area = emu_read16(&e, AREA);
	unsigned layers = (area & 0xFF) >= 0x80 ? 0x0C : 0x0E;
	if (getenv("LAYERS")) layers = strtoul(getenv("LAYERS"), NULL, 0);
	emu_layers(&e, layers);
	emu_state_load(&e, st0); /* (the layers take effect as the registers load) */
	/* the grid's cells: visited, and the place (16.16) he stood in it */
	int gw = (xu - xl) / grid + 3, gh = (yu - yl) / grid + 3;
	int32_t* at = calloc((size_t) gw * gh * 3, 4);
	uint8_t* seen = calloc((size_t) gw * gh, 1);
	int* queue = malloc(sizeof(int) * gw * gh);
	int qh = 0, qt = 0;
	int32_t x0 = (int32_t) emu_read32(&e, pos), y0 = (int32_t) emu_read32(&e, pos + 4), z0 = (int32_t) emu_read32(&e, pos + 8);
#define CELL(X, Y) ((((Y) >> 16) - yl) / grid * gw + (((X) >> 16) - xl) / grid)
	int c0 = CELL(x0, y0);
	seen[c0] = 1, at[c0 * 3] = x0, at[c0 * 3 + 1] = y0, at[c0 * 3 + 2] = z0, queue[qt++] = c0;
	/* the picture */
	int cx0 = xl + yl - 240, cx1 = xu + yu + 240, cy0 = (yl - xu) / 2 - 240, cy1 = (yu - xl) / 2 + 160;
	int W = cx1 - cx0, H = cy1 - cy0;
	uint32_t* pic = calloc((size_t) W * H, 4);
	static const uint32_t ways[8] = { K_UP, K_DOWN, K_LEFT, K_RIGHT, K_UP | K_LEFT, K_UP | K_RIGHT, K_DOWN | K_LEFT, K_DOWN | K_RIGHT };
	int tries = 0, exits = 0;
	while (qh < qt) {
		int c = queue[qh++];
		int32_t x = at[c * 3], y = at[c * 3 + 1], z = at[c * 3 + 2];
		/* its picture: set down there, the camera brought to him */
		if (walk(x, y, z, 0, 6)) {
			emu_frame(&e, 0);
			int camx = (int16_t) emu_read16(&e, CAM + 4), camy = (int16_t) emu_read16(&e, CAM + 6);
			for (int sy = 0; sy < 160; ++sy) {
				int py = camy - 80 + sy - cy0;
				if (py < 0 || py >= H) continue;
				for (int sx = 0; sx < 240; ++sx) {
					int qx = camx - 120 + sx - cx0;
					if (qx < 0 || qx >= W) continue;
					pic[(size_t) py * W + qx] = e.fb[sy * 240 + sx] | 0xFF000000;
				}
			}
		}
		for (int w = 0; w < (getenv("DIRS") ? atoi(getenv("DIRS")) : 8); ++w) {
			++tries;
			if (!walk(x, y, z, ways[getenv("DIAG") ? w + 4 : w], stepf)) {
				++exits;
				if (getenv("LOG")) fprintf(stderr, "exit at %d,%d going %d: state %d area %04X\n", x >> 16, y >> 16, w, emu_read8(&e, GSTATE), emu_read16(&e, AREA));
				continue;
			}
			int32_t nx = (int32_t) emu_read32(&e, pos), ny = (int32_t) emu_read32(&e, pos + 4), nz = (int32_t) emu_read32(&e, pos + 8);
			if ((nx >> 16) < xl || (nx >> 16) > xu || (ny >> 16) < yl || (ny >> 16) > yu) continue;
			int n = CELL(nx, ny);
			if (seen[n]) continue;
			seen[n] = 1, at[n * 3] = nx, at[n * 3 + 1] = ny, at[n * 3 + 2] = nz, queue[qt++] = n;
		}
	}
	fprintf(stderr, "area %04X: %d places, %d tries (%d left the area); %dx%d\n", area, qt, tries, exits, W, H);
	return write_png(argv[3], pic, W, H, W) ? 0 : 1;
}

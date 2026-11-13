/* Baldur's Gate: Dark Alliance: a map from a copy whose camera is steered
 * (the experiment for a map on the second screen; not built by the Makefile).
 *   bgdpan ROM STATE OUT.png [X0 Y0 X1 Y1]
 * The camera-follow routine's three stores to the camera's pending deltas
 * are blanked in this copy (0x08690FD2 across, 0x0869108A / 0x0869109C
 * down), so the camera stays where it's put; it's steered by writing the
 * deltas (0x03003538, 0x03002BF0) every frame, from where it is (the game
 * adds them to the camera, 0x030017D0 / 0x030016B0, and draws the newly
 * shown 16-px strips, at most one per layer a frame: 16 px a step). At
 * each screen-sized stop over the box (camera positions, default: as far
 * as the game lets it go from where it is), BG0-BG2 alone are drawn and
 * laid into the picture where the camera was. Logs where the camera stops
 * short (the game's own bounds).
 * Build like wwatch.c (docs/PINBALL.md), with src/patch.c.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "emu.h"

bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

#define CAMX 0x030017D0
#define CAMY 0x030016B0
#define DX 0x03003538
#define DY 0x03002BF0

static Emu e;

static int camx(void) { return (int32_t) emu_read32(&e, CAMX); }
static int camy(void) { return (int32_t) emu_read32(&e, CAMY); }

/* steer to x, y; false if it stops short (stuck for 30 frames) */
static bool go(int x, int y) {
	int stuck = 0, lx = camx(), ly = camy();
	for (int f = 0; f < 4000; ++f) {
		int cx = camx(), cy = camy();
		if (cx == x && cy == y) return true;
		if (cx != lx || cy != ly) stuck = 0;
		else if (++stuck > 30) return false;
		lx = cx, ly = cy;
		if (getenv("BGDPAN_TRACE")) fprintf(stderr, "f%d cam %d,%d d %d,%d\n", f, cx, cy, (int16_t) emu_read16(&e, DX), (int16_t) emu_read16(&e, DY));
		{ /* (written every frame: another routine pushes the camera back
		   * toward the hero once he's off screen, after the scroll) */
			int dx = x - cx, dy = y - cy;
			if (dx > 16) dx = 16;
			if (dx < -16) dx = -16;
			if (dy > 16) dy = 16;
			if (dy < -16) dy = -16;
			emu_write16(&e, DX, (uint16_t) dx);
			emu_write16(&e, DY, (uint16_t) dy);
		}
		emu_frame(&e, 0);
	}
	return false;
}

int main(int argc, char** argv) {
	if (argc < 4) return 2;
	if (!emu_open(&e, argv[1], NULL, false)) return 1;
	if (!emu_state_load_file(&e, argv[2])) return 1;
	emu_frame(&e, 0);
	emu_write16(&e, 0x08690FD2, 0x46C0);
	emu_write16(&e, 0x0869108A, 0x46C0);
	emu_write16(&e, 0x0869109C, 0x46C0);
	for (int i = 0; i < 4; ++i) emu_frame(&e, 0);
	int x0, y0, x1, y1;
	if (argc >= 8) {
		x0 = atoi(argv[4]), y0 = atoi(argv[5]), x1 = atoi(argv[6]), y1 = atoi(argv[7]);
	} else {
		/* the level's size: its header (pointer at 0x030035B4) holds, for
		 * each layer, a rectangle in 16-px blocks at header + 2 + 4 * the
		 * halfword at header + 0x10 + 2 * layer */
		uint32_t hdr = emu_read32(&e, 0x030035B4);
		uint32_t r = hdr + 2 + 4 * emu_read16(&e, hdr + 0x10);
		x0 = 16 * emu_read16(&e, r), y0 = 16 * emu_read16(&e, r + 2);
		x1 = 16 * emu_read16(&e, r + 4) - 241, y1 = 16 * emu_read16(&e, r + 6) - 161; /* (the game stops a pixel short) */
		fprintf(stderr, "level %dx%d blocks: camera x %d-%d y %d-%d\n", emu_read16(&e, r + 4), emu_read16(&e, r + 6), x0, x1, y0, y1);
	}
	int W = x1 - x0 + 240, H = y1 - y0 + 160;
	uint32_t* pic = calloc((size_t) W * H, 4);
	size_t sz = emu_state_size(&e);
	void* st = malloc(sz);
	int stops = 0, frames0 = 0;
	for (int y = y0;; y += 160) {
		if (y > y1) y = y1;
		for (int x = x0;; x += 240) {
			if (x > x1) x = x1;
			if (!go(x, y)) fprintf(stderr, "stopped at %d,%d short of %d,%d\n", camx(), camy(), x, y);
			for (int i = 0; i < 3; ++i) emu_frame(&e, 0); /* (its last strips drawn) */
			int cx = camx(), cy = camy();
			emu_state_save(&e, st);
			emu_layers(&e, 0x07);
			emu_state_load(&e, st);
			emu_frame(&e, 0);
			for (int r = 0; r < 160; ++r) {
				int py = cy - y0 + r;
				if (py < 0 || py >= H) continue;
				for (int c = 0; c < 240; ++c) {
					int px = cx - x0 + c;
					if (px >= 0 && px < W) pic[py * W + px] = e.fb[r * 240 + c];
				}
			}
			emu_layers(&e, 0x1F);
			emu_state_load(&e, st);
			++stops;
			if (x == x1) break;
		}
		if (y == y1) break;
	}
	(void) frames0;
	fprintf(stderr, "%d stops, picture %dx%d\n", stops, W, H);
	write_png(argv[3], pic, W, H, W);
	return 0;
}

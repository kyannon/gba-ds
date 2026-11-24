/* gbpin3 ROM OUTDIR [FRAMES]: plays Pokemon Pinball (GBC) to its table and writes the two halves (src/gbpin.c) every 30 frames */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "../../src/gbpin.h"
static Emu game;
int main(int argc, char** argv) {
	if (argc < 3 || !emu_open(&game, argv[1], NULL, false)) return 1;
	GbPin* g = gbpin_open(argv[1], getenv("THREADS") == NULL);
	if (!g) { fprintf(stderr, "not the game\n"); return 1; }
	struct { uint32_t k; int n; } seq[] = { { 0, 300 }, { K_START, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 200 }, { K_DOWN, 40 }, { 0, 1 } };
	int shots = 0, tot = argc > 3 ? atoi(argv[3]) : 600;
	struct timespec t0, t1; clock_gettime(CLOCK_MONOTONIC, &t0);
	int frames = 0;
	for (size_t i = 0; i < sizeof(seq) / sizeof(*seq); ++i)
		for (int f = 0; f < seq[i].n; ++f) { gbpin_before(g, &game, seq[i].k); emu_frame(&game, seq[i].k); gbpin_after(g, &game); }
	for (int f = 0; f < tot; ++f, ++frames) {
		uint32_t k = (f / 20 % 3 == 0) ? K_LEFT : (f / 20 % 3 == 1 ? K_A : 0);
		gbpin_before(g, &game, k);
		emu_frame(&game, k);
		gbpin_after(g, &game);
		const uint32_t *top, *bot;
		bool t = gbpin_table(g, &top, &bot);
		if (f % 30 == 0 && shots < 40) {
			fprintf(stderr, "f%d table %d stage %d\n", f, t, gbpin_stage(g));
			if (t) {
				static uint32_t both[GBA_W * GBA_H * 2];
				memcpy(both, top, GBA_W * GBA_H * 4), memcpy(both + GBA_W * GBA_H, bot, GBA_W * GBA_H * 4);
				char p[512]; snprintf(p, sizeof(p), "%s/h%02d.png", argv[2], shots++);
				extern bool write_png(const char*, const uint32_t*, int, int, int);
				write_png(p, both, GBA_W, GBA_H * 2, GBA_W);
			}
		}
	}
	clock_gettime(CLOCK_MONOTONIC, &t1);
	fprintf(stderr, "%.1f fps\n", frames / ((t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9));
	gbpin_close(g);
	return 0;
}

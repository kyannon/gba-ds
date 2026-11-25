/* gbpin5 ROM STATE OUTDIR [FRAMES]: plays a bug report's keys (hard-coded list via KEYS env "frame:mask,...") from STATE with gbpin and writes, for every frame, a line "f table stage whiteTop whiteBot" and the halves of the frames around stage changes */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../src/gbpin.h"
extern bool write_png(const char*, const uint32_t*, int, int, int);
static Emu game;
static int white(const uint32_t* p) { int n = 0; for (int i = 0; i < GBA_W * GBA_H; i += 7) n += (p[i] & 0xFFFFFF) == 0xFFFFFF; return n; }
int main(int argc, char** argv) {
	if (argc < 4 || !emu_open(&game, argv[1], NULL, false)) return 1;
	GbPin* g = gbpin_open(argv[1], getenv("SYNC") != NULL);
	if (!emu_state_load_file(&game, argv[2])) return 2;
	int tot = argc > 4 ? atoi(argv[4]) : 640, prevS = -9, dumpUntil = -1;
	for (int f = 0; f < tot; ++f) {
		uint32_t k = 0;
		if (f < 90 || (f >= 442 && f < 455)) k |= K_A;
		if ((f >= 446 && f < 479) || (f >= 556 && f < 576) || (f >= 594 && f < 610)) k |= K_LEFT;
		gbpin_before(g, &game, k); emu_frame(&game, k); gbpin_after(g, &game);
		const uint32_t *top, *bot; bool t = gbpin_table(g, &top, &bot);
		int s = gbpin_stage(g), rs = emu_read8(&game, 0xD460 /* placeholder */);
		(void) rs;
		int wt = t ? white(top) : -1, wb = t ? white(bot) : -1, wf = white(game.fb);
		if (s != prevS) dumpUntil = f + 25;
		prevS = s;
		printf("f%d table %d stage %d white top %d bot %d game %d\n", f, t, s, wt, wb, wf);
		if (f < dumpUntil && f >= dumpUntil - 30) {
			static uint32_t both[GBA_W * GBA_H * 2];
			if (t) { memcpy(both, top, GBA_W * GBA_H * 4); memcpy(both + GBA_W * GBA_H, bot, GBA_W * GBA_H * 4); }
			else { memcpy(both, game.fb, GBA_W * GBA_H * 4); memset(both + GBA_W * GBA_H, 0x40, GBA_W * GBA_H * 4); }
			char p[512]; snprintf(p, sizeof(p), "%s/c%04d.png", argv[3], f);
			write_png(p, both, GBA_W, GBA_H * 2, GBA_W);
		}
	}
	gbpin_close(g);
	return 0;
}

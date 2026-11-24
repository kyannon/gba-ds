/* Pokemon Pinball (Game Boy Color) experiment (game/pokemon-pinball-gbc):
 *   gbpin ROM OUTDIR
 * Plays the game to its table, then every frame forks a second core from the game's state
 * from before the frame, makes it load the table's OTHER stage with the game's own routine
 * (a patched hook at the start of the ball physics function), and writes both pictures. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mgba/core/core.h>
#include "../../src/emu.h"

static Emu master, twin;
static uint8_t* snap;
static size_t snapSize;

static uint8_t rd(Emu* e, uint16_t a) { return e->core->rawRead8(e->core, a, -1); }
static void wr(Emu* e, uint16_t a, uint8_t v) { e->core->rawWrite8(e->core, a, -1, v); }
static void wrbank(Emu* e, uint16_t a, int bank, uint8_t v) { e->core->rawWrite8(e->core, a, bank, v); }

#define HOOK 0x7B00
static void patch_twin(void) {
	static const uint8_t hook[] = {
		0xAF,             /* xor a */
		0xEA, 0xB9, 0xD7, /* ld [$d7b9],a   (the instruction the call replaced) */
		0xFA, 0xD2, 0xFF, /* ld a,[$d7aa]   our command: the stage to load + 1 */
		0xA7,             /* and a */
		0x28, 0x1D,       /* jr z,.done */
		0x3D,             /* dec a */
		0x47,             /* ld b,a */
		0xAF,             /* xor a */
		0xEA, 0xD2, 0xFF, /* ld [$d7aa],a */
		0x78,             /* ld a,b */
		0xCD, 0x74, 0x66, /* call FieldVerticalTransition */
		0xFA, 0xAC, 0xD4, /* ld a,[wCurrentStage] */
		0xCB, 0x47,       /* bit 0,a */
		0xFA, 0xB6, 0xD4, /* ld a,[wBallYPos+1] */
		0x28, 0x04,       /* jr z,.up */
		0xD6, 0x88,       /* sub $88 */
		0x18, 0x02,       /* jr .st */
		0xC6, 0x88,       /* .up: add $88 */
		0xEA, 0xB6, 0xD4, /* .st: ld [wBallYPos+1],a */
		0xAF,             /* .done: xor a */
		0xC9,             /* ret */
	};
	for (size_t i = 0; i < sizeof(hook); ++i) wrbank(&twin, HOOK + i, 3, hook[i]);
	/* the physics function's first instructions -> call hook; nop */
	wrbank(&twin, 0x5909, 3, 0xCD), wrbank(&twin, 0x590A, 3, HOOK & 0xFF), wrbank(&twin, 0x590B, 3, HOOK >> 8), wrbank(&twin, 0x590C, 3, 0x00);
	/* CheckStageTransition: return after its scroll call (no transitions in the copy) */
	wrbank(&twin, 0x6CEC, 3, 0xC9);
}

#include <mgba/internal/gb/gb.h>
typedef struct { uint8_t vram[2][0x2000]; uint16_t pal[64]; bool ok; } Env;
static Env env;
static void env_capture(Emu* e) {
	for (int b = 0; b < 2; ++b)
		for (int i = 0; i < 0x2000; ++i) env.vram[b][i] = e->core->rawRead8(e->core, 0x8000 + i, b);
	struct GB* gb = e->core->board;
	memcpy(env.pal, gb->video.palette, sizeof(env.pal));
	env.ok = true;
}
static void env_apply(Emu* e) {
	for (int b = 0; b < 2; ++b)
		for (int i = 0; i < 0x2000; ++i) e->core->rawWrite8(e->core, 0x8000 + i, b, env.vram[b][i]);
	struct GB* gb = e->core->board;
	for (int i = 0; i < 64; ++i) {
		gb->video.palette[i] = env.pal[i];
		gb->video.renderer->writePalette(gb->video.renderer, i, env.pal[i]);
	}
}
static void keys_frame(uint32_t k) { emu_frame(&master, k); }


static Emu gt;
static void patch_core(Emu* e, bool hook) {
	Emu* keep = &twin;
	twin = *e; /* (patch_twin writes to `twin`) */
	if (hook) patch_twin();
	else wrbank(&twin, 0x6CEC, 3, 0xC9);
	(void) keep;
}
static bool white(const uint32_t* fb) {
	for (int i = 0; i < GBA_W * GBA_H; ++i) if (fb[i] != fb[0]) return false;
	return true;
}
static uint32_t hashfb(const uint32_t* fb) { uint32_t h = 0; for (int i = 0; i < GBA_W * GBA_H; ++i) h = h * 31 + fb[i]; return h; }

static void hs(Emu* e, int stage) { /* wCurrentStage, the window's row */
	wr(e, 0xD4AC, stage);
	int bt = rd(e, 0xD5CA);
	wr(e, 0xFF8A, (stage & 1) ? 0x86 : (bt ? 0x86 : 0x90));
}

int main(int argc, char** argv) {
	if (argc < 3) return 1;
	if (!emu_open(&master, argv[1], NULL, false) || !emu_open(&twin, argv[1], NULL, false) || !emu_open(&gt, argv[1], NULL, false)) return 1;
	Emu tw = twin; /* persistent copy: no hook */
	Emu g = gt;
	twin = tw; wrbank(&twin, 0x6CEC, 3, 0xC9); tw = twin;
	twin = g; patch_twin(); g = twin;
	snapSize = emu_state_size(&master);
	snap = malloc(snapSize);
	const char* out = argv[2];
	struct { uint32_t k; int n; } seq[] = { { 0, 300 }, { K_START, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 120 }, { K_A, 3 }, { 0, 200 }, { K_DOWN, 40 }, { 0, 1 } };
	for (size_t i = 0; i < sizeof(seq) / sizeof(*seq); ++i)
		for (int f = 0; f < seq[i].n; ++f) keys_frame(seq[i].k);
	int shots = 0, envStage = -1;
	static uint32_t both[GBA_W * GBA_H * 3];
	for (int f = 0; f < 1500 && shots < 10; ++f) {
		uint32_t k = (f / 20 % 3 == 0) ? K_LEFT : (f / 20 % 3 == 1 ? K_A : 0);
		emu_state_save(&master, snap);
		keys_frame(k);
		int stage = rd(&master, 0xD4AC);
		if (!(stage == 0 || stage == 1 || stage == 4 || stage == 5)) continue;
		int sp = stage ^ 1;
		bool ground = f % 60 == 0;
		if (envStage != sp) {
			/* build the environment with the game's own load */
			emu_state_load(&g, snap);
			wr(&g, 0xFFD2, sp + 1);
			int n = 0;
			while (rd(&g, 0xFFD2) && n < 30) emu_frame(&g, k), ++n;
			for (int x = 0; x < 30; ++x) { emu_frame(&g, k); if (!white(g.fb) && x > 8) break; }
			env_capture(&g);
			envStage = sp;
			fprintf(stderr, "f%d: environment for stage %d built\n", f, sp);
			continue;
		}
		emu_state_load(&tw, snap);
		env_apply(&tw);
		hs(&tw, sp);
		int by = rd(&tw, 0xD4B6);
		wr(&tw, 0xD4B6, (sp & 1) ? by - 0x88 : by + 0x88);
		emu_frame(&tw, k);
		env_capture(&tw);
		if (!ground) continue;
		/* ground truth */
		emu_state_load(&g, snap);
		wr(&g, 0xFFD2, sp + 1);
		int n = 0;
		while (rd(&g, 0xFFD2) && n < 30) emu_frame(&g, k), ++n;
		for (int x = 0; x < 30; ++x) { emu_frame(&g, k); if (!white(g.fb) && x > 8) break; }
		if (getenv("DIFF")) {
			static const char* nm = "io/hram";
			(void) nm;
			for (int a = 0xFF00; a < 0xFFFF; ++a) {
				int m = rd(&master, a), t = rd(&g, a), pp = rd(&tw, a);
				if (m != t || m != pp) fprintf(stderr, "  %04x master %02x persistent %02x truth %02x\n", a, m, pp, t);
			}
		}
		memcpy(both, master.fb, GBA_W * GBA_H * 4);
		memcpy(both + GBA_W * GBA_H, tw.fb, GBA_W * GBA_H * 4);
		memcpy(both + 2 * GBA_W * GBA_H, g.fb, GBA_W * GBA_H * 4);
		char p[512];
		snprintf(p, sizeof(p), "%s/p%02d.png", out, shots++);
		write_png(p, both, GBA_W, GBA_H * 3, GBA_W);
		fprintf(stderr, "f%d stage %d ballY %02x: master %08x persistent %08x truth %08x\n", f, stage, rd(&master, 0xD4B6), hashfb(master.fb), hashfb(tw.fb), hashfb(g.fb));
	}
	return 0;
}

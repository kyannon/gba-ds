/* Pokemon Pinball experiment (docs/PINBALL.md, "Scouting"): a second core
 * loaded from STATE, its camera stores NOPed, its camera held OFFSET rows
 * from the game's, each layer (bg1 bg2 bg3 obj all) shot alone every frame.
 *   jump ROM STATE OUTDIR OFFSET
 * Env: FILL=1 jumps the camera at once (tile ring refilled from EWRAM, every
 * eased copy of the scroll set: fill() below); without it the game eases its
 * scroll 8 rows a frame. FRAMES=n (10). STRUCT=1 prints S+0x40..0x7F of both
 * cores at the first and last frame. CAND=1 lists RAM holding 272/288 before
 * the first frame. HOOK=1 logs who writes VRAM 0x8800-0x8BFF in frames 0-2.
 * DUMP=1 saves both cores' EWRAM / IWRAM for frames 0-2; VDUMP=file the
 * copy's VRAM + EWRAM at the end. NO68 / NOSTK / NOIO leave out those writes.
 * Build: the Makefile's tools flags plus -Isrc, with src/emu.c and friends
 * (see docs/PINBALL.md). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/arm/arm.h>
#include <mgba/core/core.h>
#include "emu.h"
static struct GBA* gb; static void (*oV)(struct GBAVideoRenderer*, uint32_t); static int curF = -1, filling;
static void hV(struct GBAVideoRenderer* r, uint32_t a) {
	oV(r, a);
	if (!filling && a >= 0x8800 && a < 0x8C00 && curF >= 0 && curF < 3) {
		static uint32_t seen[64]; static int ns;
		uint32_t pc = gb->cpu->gprs[ARM_PC], key = pc ^ (uint32_t) curF << 28;
		for (int i = 0; i < ns; ++i) if (seen[i] == key) return;
		if (ns < 64) seen[ns++] = key;
		fprintf(stderr, "frame %d: VRAM %05X by pc %08X (lr %08X)\n", curF, a, pc, gb->cpu->gprs[ARM_LR]);
	}
}

static void fill(Emu* e, uint32_t T, int camRow) {
	int nt = camRow >> 3;
	for (int R = nt < 0 ? 0 : nt; R < nt + 22 && R <= 55; ++R) {
		uint32_t src = R <= 31 ? 0x0200FBB0 + R * 0x400 : 0x020030A0 + (R - 32) * 0x400;
		uint32_t dst = 0x06008000 + ((R + 10) % 22) * 0x400;
		for (int k = 0; k < 0x400; k += 4) emu_write32(e, dst + k, emu_read32(e, src + k));
	}
	emu_write16(e, T + 0x64, (uint16_t) nt);
	for (int i = 0; i < 3; ++i) emu_write16(e, 0x0200B3AE + i * 4, (uint16_t) camRow);
	emu_write16(e, T + 0x6A, (uint16_t) camRow);
	if (!getenv("NO68")) { emu_write16(e, T + 0x68, (uint16_t) camRow); emu_write16(e, T + 0x5A, (uint16_t) camRow); emu_write16(e, T + 0xCC, (uint16_t) camRow); }
	if (!getenv("NOSTK")) emu_write16(e, 0x03007ED8, (uint16_t) (camRow + 16));
	if (!getenv("NOIO")) for (int i = 1; i < 4; ++i) emu_write16(e, 0x04000012 + i * 4, (uint16_t) camRow);
}

int main(int argc, char** argv) {
	Emu a = {0}, b = {0};
	if (!emu_open(&a, argv[1], NULL, false) || !emu_open(&b, argv[1], NULL, false)) return 1;
	emu_state_load_file(&a, argv[2]); emu_state_load_file(&b, argv[2]);
	if (getenv("HOOK")) { gb = (struct GBA*) b.core->board; oV = gb->video.renderer->writeVRAM; gb->video.renderer->writeVRAM = hV; }
	uint32_t nops[] = {0x0804DCFA, 0x0804DD0A, 0x0804DA54, 0x0804DA64};
	for (int i = 0; i < 4; ++i) emu_write16(&b, nops[i], 0x46C0);
	int off = atoi(argv[4]);
	const char* masks[] = {"bg1", "bg2", "bg3", "obj", "all"};
	unsigned m[] = {0x02, 0x04, 0x08, 0x10, 0x1E};
	int nf = getenv("FRAMES") ? atoi(getenv("FRAMES")) : 10;
	for (int f = 0; f < nf; ++f) {
		uint32_t S = emu_read32(&a, 0x020314E0), T = emu_read32(&b, 0x020314E0);
		int32_t cam = (int32_t) emu_read32(&a, S + 0x54) + (off << 8);
		emu_write32(&b, T + 0x54, (uint32_t) cam);
		emu_write16(&b, T + 0x4E, (uint16_t) (cam >> 8));
		int last = (int16_t) emu_read16(&b, T + 0x64), nt = (cam >> 8) >> 3;
		if (getenv("FILL") && (nt - last > 1 || last - nt > 1)) { filling = 1; fill(&b, T, cam >> 8); filling = 0; }
		curF = f;
		if (f == 0 && getenv("CAND")) {
			size_t n; uint8_t* w = emu_memory(&b, 2, &n);
			for (size_t i = 0; i + 4 <= n; i += 2) { int16_t v = (int16_t) (w[i] | w[i + 1] << 8); int32_t v32 = (int32_t) (w[i] | w[i+1] << 8 | w[i+2] << 16 | (uint32_t) w[i+3] << 24);
				if (v == 272 || v == 288 || v == 34 || (i % 4 == 0 && (v32 == 272 << 8 || v32 == 288 << 8))) printf("cand EW %08zX h %d w %d\n", 0x02000000 + i, v, v32); }
			w = emu_memory(&b, 3, &n);
			for (size_t i = 0; i + 4 <= n; i += 2) { int16_t v = (int16_t) (w[i] | w[i + 1] << 8); int32_t v32 = (int32_t) (w[i] | w[i+1] << 8 | w[i+2] << 16 | (uint32_t) w[i+3] << 24);
				if (v == 272 || v == 288 || (i % 4 == 0 && (v32 == 272 << 8 || v32 == 288 << 8))) printf("cand IW %08zX h %d w %d\n", 0x03000000 + i, v, v32); }
		}
		emu_frame(&a, 0);
		/* every layer view needs its own render of the same frame: save, run, restore */
		static uint8_t st[1 << 20];
		emu_state_save(&b, st);
		for (int l = 0; l < 5; ++l) {
			emu_state_load(&b, st);
			emu_layers(&b, m[l]);
			emu_frame(&b, 0);
			char p[512]; snprintf(p, sizeof p, "%s/f%d-%s.png", argv[3], f, masks[l]);
			write_png(p, b.fb, GBA_W, GBA_H, GBA_W);
		}
		emu_layers(&b, 0x1F);
		if (getenv("STRUCT") && (f == 0 || f == nf - 1)) {
			printf("f%d S+0x40..0x80 a:", f); for (int k = 0x40; k < 0x80; k += 2) printf(" %d", (int16_t) emu_read16(&a, S + k));
			printf("\n     b:"); for (int k = 0x40; k < 0x80; k += 2) printf(" %d", (int16_t) emu_read16(&b, T + k));
			printf("\n");
		}
		if (f < 3 && getenv("DUMP")) {
			size_t n; char p2[512];
			uint8_t* wa = emu_memory(&a, 2, &n); snprintf(p2, sizeof p2, "%s/a%d.ew", argv[3], f); FILE* o = fopen(p2, "wb"); fwrite(wa, 1, n, o); fclose(o);
			uint8_t* wb = emu_memory(&b, 2, &n); snprintf(p2, sizeof p2, "%s/b%d.ew", argv[3], f); o = fopen(p2, "wb"); fwrite(wb, 1, n, o); fclose(o);
			uint8_t* ia = emu_memory(&a, 3, &n); snprintf(p2, sizeof p2, "%s/a%d.iw", argv[3], f); o = fopen(p2, "wb"); fwrite(ia, 1, n, o); fclose(o);
			uint8_t* ib = emu_memory(&b, 3, &n); snprintf(p2, sizeof p2, "%s/b%d.iw", argv[3], f); o = fopen(p2, "wb"); fwrite(ib, 1, n, o); fclose(o);
		}
		printf("f%d cam a %d b %d | a VOFS bg1 %d bg2 %d bg3 %d | b VOFS bg1 %d bg2 %d bg3 %d | b last %d disp %d\n", f, (int) (emu_read32(&a, S + 0x54) >> 8), (int) (emu_read32(&b, T + 0x54) >> 8), emu_io16(&a,0x16), emu_io16(&a,0x1A), emu_io16(&a,0x1E), emu_io16(&b,0x16), emu_io16(&b,0x1A), emu_io16(&b,0x1E), (int16_t) emu_read16(&b, T + 0x64), (int16_t) emu_read16(&b, 0x0200B3B6));
	}
	if (getenv("VDUMP")) { FILE* o = fopen(getenv("VDUMP"), "wb"); fwrite(emu_vram(&b), 1, 0x18000, o); size_t n; uint8_t* w = emu_memory(&b, 2, &n); fwrite(w, 1, 0x40000, o); fclose(o); }
	return 0;
}

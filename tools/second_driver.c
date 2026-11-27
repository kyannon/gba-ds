/* The second-screen module's reference driver (discussion #75): a game run
 * through the module alone, with no SDL and none of the app, the way a
 * libretro core will run it. It replays a trace of the app's calls into the
 * module (GBADS_MODULE_TRACE: second.c's second_trace_begin) and writes what
 * the screens show at each of its screenshots, through the same trace code:
 *
 *   second-driver ROM TRACE_DIR OUT_DIR [SAVE]
 *
 * The game is opened as the app opens it: with a battery save (OUT_DIR/game.sav,
 * a copy of SAVE when given: the one the app started with) and
 * profile_set_rom (a known colour patch's colours).
 * OUT_DIR gets its own trace.txt and NAME.second / NAME.game files;
 * tools/regress.py's "driver" cases check that they're byte for byte the
 * app's. A core's frame is this file's loop: second_frame_start, the game's
 * frame through second_run_frame, second_step, and the picture second_shown
 * names; at a state load second_jump, second_state_loaded, second_new_subject.
 *
 * Its hooks: present / presentGba / presentHud / presentCombat do nothing
 * (the trace keeps the pictures; a core would put them in its frame), the
 * logo is the one logo.c extracts from the ROM (the app's own, scraped or
 * extracted logo as its logos folder has it; with none of the player's own,
 * the same picture), hasCombat says whether the profile has a fights'
 * picture (the app makes a place for it then).
 *
 * GBADS_DRIVER_SERIALIZE=1 also saves the game's state and loads it back in
 * memory after every frame (emu_state_save_mem / emu_state_load_mem, a
 * core's retro_serialize / retro_unserialize): the pictures must still be the
 * app's. GBADS_DRIVER_SAVE_MEM=1 gives the game its battery save in memory
 * (emu_save_memory, a core's RETRO_MEMORY_SAVE_RAM) instead of a file. */
#include "../src/second.h"
#include "../src/hud.h"
#include "../src/logo.h"
#include "../src/toggle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "../src/stb_image.h"
#pragma GCC diagnostic pop

static Emu emu;
static Config cfg;
static const char* romPath;
static const char* outDir;
static unsigned char* logoPx;
static int logoW, logoH;
static bool logoTried;

static void d_present(const uint32_t* px) { (void) px; }
static void d_present_gba(const uint32_t* px) { (void) px; }
static void d_present_hud(const uint32_t* px) { (void) px; }
static void d_present_combat(const uint32_t* px) { (void) px; }
static bool d_has_combat(void) { return hud.combatProf != NULL; }
static void d_save_bmp(const uint32_t* px, int w, int h, const char* path) { (void) px, (void) w, (void) h, (void) path; }

static bool d_logo_load(LogoPic* p, int* sub) {
	*sub = 1;
	if (!logoTried) {
		logoTried = true;
		char png[800];
		snprintf(png, sizeof(png), "%s/logo.png", outDir);
		int n;
		if (logo_extract(romPath, emu.gameCode, png)) logoPx = stbi_load(png, &logoW, &logoH, &n, 4);
	}
	if (!logoPx) return false;
	p->px = logoPx, p->w = logoW, p->h = logoH, p->pitch = logoW * 4, p->own = NULL;
	return true;
}
static void d_logo_free(LogoPic* p) { (void) p; }

static void* read_file(const char* dir, const char* name, size_t* n) {
	char path[900];
	snprintf(path, sizeof(path), "%s%s%s", dir ? dir : "", dir ? "/" : "", name); /* (dir NULL: name as it is) */
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* b = len > 0 ? malloc((size_t) len) : NULL;
	if (b && fread(b, 1, (size_t) len, f) != (size_t) len) free(b), b = NULL;
	fclose(f);
	if (n) *n = b ? (size_t) len : 0;
	return b;
}

int main(int argc, char** argv) {
	if (argc != 4 && argc != 5) {
		fprintf(stderr, "usage: %s ROM TRACE_DIR OUT_DIR [SAVE]\n", argv[0]);
		return 2;
	}
	romPath = argv[1], outDir = argv[3];
	const char* traceDir = argv[2];
	mkdir(outDir, 0755);
	char path[900];
	snprintf(path, sizeof(path), "%s/trace.txt", traceDir);
	FILE* t = fopen(path, "r");
	if (!t) {
		fprintf(stderr, "second-driver: no trace %s\n", path);
		return 2;
	}
	char save[900];
	snprintf(save, sizeof(save), "%s/game.sav", outDir);
	remove(save);
	if (argc == 5) {
		size_t n;
		void* b = read_file(NULL, argv[4], &n);
		FILE* f = b ? fopen(save, "wb") : NULL;
		if (f) fwrite(b, 1, n, f), fclose(f);
		free(b);
	}
	/* GBADS_DRIVER_SAVE_MEM=1: the battery save in memory instead (emu_save_memory, a
	 * core's RETRO_MEMORY_SAVE_RAM), loaded from SAVE */
	static uint8_t saveMem[0x20000]; /* (GBA_SIZE_FLASH1M: 128 KB) */
	bool inMemory = getenv("GBADS_DRIVER_SAVE_MEM") && atoi(getenv("GBADS_DRIVER_SAVE_MEM"));
	if (inMemory) {
		memset(saveMem, 0xFF, sizeof(saveMem));
		size_t n = 0;
		void* b = argc == 5 ? read_file(NULL, argv[4], &n) : NULL;
		if (b) memcpy(saveMem, b, n < sizeof(saveMem) ? n : sizeof(saveMem));
		free(b);
	}
	profile_set_rom(NULL, 0);
	if (!emu_open(&emu, romPath, inMemory ? NULL : save, false) || (inMemory && !emu_save_memory(&emu, saveMem, sizeof(saveMem)))) {
		fprintf(stderr, "second-driver: can't open %s\n", romPath);
		return 2;
	}
	profile_set_rom(emu.gameCode, emu.romCrc);
	/* exact runs: the captures, logo extraction and the profiles' workers on
	 * this thread (a trace is only replayed exactly so; emu_sync_work) */
	emu_set_sync_work(true);
	/* what the module reads from the host, set once per game */
	sec.emu = &emu, sec.cfg = &cfg, sec.debug = getenv("GBADS_DEBUG") != NULL, sec.romPath = romPath;
	sec.hooks = (SecondHooks) { d_present, d_present_gba, d_logo_load, d_logo_free };
	hud.hooks = (HudHooks) { d_present_hud, d_present_combat, d_has_combat, d_save_bmp };
	if (!second_trace_begin(outDir)) {
		fprintf(stderr, "second-driver: can't write into %s\n", outDir);
		return 2;
	}
	char line[1024];
	int lineNo = 0, bad = 0;
	bool roundTrip = getenv("GBADS_DRIVER_SERIALIZE") && atoi(getenv("GBADS_DRIVER_SERIALIZE"));
	while (fgets(line, sizeof(line), t)) {
		++lineNo;
		line[strcspn(line, "\n")] = 0;
		char a[260], b[260];
		unsigned x;
		int s;
		unsigned long long fr;
		size_t n;
		if (!strcmp(line, "frame_start")) second_frame_start();
		else if (sscanf(line, "run %x", &x) == 1) {
			second_run_frame(x);
			if (roundTrip) { /* (GBADS_DRIVER_SERIALIZE: the game's state saved and loaded back in memory after every frame) */
				/* (with the second screen's block inside, as a core's retro_serialize keeps it:
				 * it must come out of the bytes as it went in) */
				size_t sn, bn = 0, en = 0;
				void* block = second_state_save(&bn);
				uint8_t* st = emu_state_save_mem(&emu, block, bn, &sn);
				void* back = st ? emu_state_mem_get_extra(&emu, st, sn, &en) : NULL;
				if (!st || (block && (en != bn || memcmp(back, block, bn))) || !emu_state_load_mem(&emu, st, sn)) {
					fprintf(stderr, "second-driver: line %d: the state didn't go through memory\n", lineNo);
					++bad;
				}
				free(st), free(block), free(back);
			}
		}
		else if (sscanf(line, "step %d %llu", &s, &fr) == 2) sec.frame = fr, second_step(s); /* (the frame count is the host's) */
		else if (!strcmp(line, "new_subject")) second_new_subject();
		else if (!strcmp(line, "jump")) second_jump();
		else if (sscanf(line, "loaded %259s %259s %zu", a, b, &n) == 3) {
			/* (the file the app loaded, loaded the same way; else the snapshot) */
			bool ok;
			if (strstr(a, ".ss")) {
				snprintf(path, sizeof(path), "%s/%s", traceDir, a);
				ok = emu_state_load_file(&emu, path);
			} else {
				size_t sn;
				void* st = read_file(traceDir, a, &sn);
				ok = st && sn == emu_state_size(&emu) && emu_state_load(&emu, st);
				free(st);
			}
			if (!ok) {
				fprintf(stderr, "second-driver: line %d: the state %s didn't load\n", lineNo, a);
				++bad;
			}
			if (ok && strstr(a, ".ss")) second_trace_state_file(path); /* (as the app's state_load does) */
			uint8_t* block = strcmp(b, "-") ? read_file(traceDir, b, NULL) : NULL;
			second_state_loaded(block, block ? n : 0); /* (taken over) */
		} else if (sscanf(line, "open %4s %d %259s %zu", a, &s, b, &n) == 4) {
			if (!strcmp(a, "-")) a[0] = 0; /* (a Game Boy game: no code) */
			size_t cn;
			void* c = read_file(traceDir, b, &cn);
			if (!c || cn != sizeof(cfg) || n != sizeof(cfg)) {
				fprintf(stderr, "second-driver: line %d: settings %s are %zu bytes, this build's are %zu\n", lineNo, b, cn, sizeof(cfg));
				return 1;
			}
			memcpy(&cfg, c, sizeof(cfg));
			free(c);
			second_open(romPath, a, s);
		} else if (!strcmp(line, "drop")) second_drop();
		else if (!strcmp(line, "close")) second_close();
		else if (!strcmp(line, "game_start")) second_game_start();
		else if (!strcmp(line, "toggle_press")) toggle_press();
		else if (!strcmp(line, "restarted")) emu_reset(&emu), second_restarted(); /* (the app reset the game, then second_jump: traced before this) */
		else if (!strcmp(line, "logo_changed")) second_logo_changed();
		else if (!strcmp(line, "loop_start")) second_loop_start();
		else if (sscanf(line, "shot %259s", a) == 1) second_trace_shot(a);
		else {
			fprintf(stderr, "second-driver: line %d not understood: %s\n", lineNo, line);
			++bad;
		}
	}
	fclose(t);
	emu_close(&emu);
	free(logoPx);
	printf("second-driver: %d lines replayed%s\n", lineNo, bad ? " (with errors)" : "");
	return bad ? 1 : 0;
}

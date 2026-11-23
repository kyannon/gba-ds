/* Cheats from RetroArch .cht files (libretro-database layout). */
#ifndef GBADS_CHEATS_H
#define GBADS_CHEATS_H

#include "config.h"
#include "emu.h"

#include <stdbool.h>

#define CHEAT_MAX_FILES 8
#define CHEATS_PER_FILE_MAX 512 /* cheats read from one file, and switched on from it at once */

typedef struct {
	char path[1024];
	char label[64]; /* "Code Breaker", "GameShark", ... (from the file name) */
	int type;       /* EMU_CHEAT_* */
} CheatFile;

typedef struct {
	char desc[128];
	char* code;
	bool on;
	int file;
} Cheat;

typedef struct {
	CheatFile files[CHEAT_MAX_FILES];
	int nFiles;
	Cheat* items;
	int n;
	char prefs[1024]; /* where the on/off choices are kept */
} Cheats;

/* Finds the .cht files for this ROM (by title, and region where the file
 * names one) in GBA DS's own cheats folder (assetDir/cheats, not on Android)
 * and then cfg->cheatDir's folders, reads them, and restores the saved
 * choices (or the files' own cheatN_enable when nothing was saved yet). */
void cheats_load(Cheats* c, const Config* cfg, const char* romPath, const char* gameCode, const char* prefsPath);
/* Puts the switched-on cheats into the emulator (one set per file, in file order). */
void cheats_apply(const Cheats* c, Emu* e);
void cheats_save(const Cheats* c);
int cheats_on(const Cheats* c);
void cheats_free(Cheats* c);

#endif

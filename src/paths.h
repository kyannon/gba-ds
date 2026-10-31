#ifndef GBADS_PATHS_H
#define GBADS_PATHS_H

#include "config.h"

typedef struct {
	char base[256];       /* ROM file name without extension */
	char sav[1024];       /* battery save (next to the ROM unless save_dir is set) */
	char quick[1024];     /* the quick bar's save state (and HOME + R1 / L1) */
	char slot[10][1024];  /* the pause menu's save states, slots 0 to 9 */
	char autoState[1024]; /* written on exit, loaded on launch */
	char thumb[1024];     /* last game frame, for the library */
	char mapThumb[1024];  /* last map frame, for the library */
	char cheats[1024];    /* which cheats are switched on */
} GamePaths;

void game_paths(const Config* cfg, const char* rom, GamePaths* out);
bool file_exists(const char* path);
void mkdirs(const char* path);
bool copy_file(const char* from, const char* to);

#endif

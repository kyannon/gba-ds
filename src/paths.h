#ifndef GBADS_PATHS_H
#define GBADS_PATHS_H

#include "config.h"

#include <stddef.h>

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

/* key = value files (gba-ds.cfg's form): one key's value (the last line
 * with it), and setting one key, the file's other lines kept (header: the
 * first line of a new file) */
bool kv_get(const char* file, const char* key, char* out, size_t n);
bool kv_set(const char* file, const char* key, const char* val, const char* header);

/* GBA DS's data folder (issue #34): states/ (settings, save states,
 * extracted logos...), cheats/, overlays/, logos/, patches/, bug-reports/
 * and screenshots/. By default the app's folder (Linux) or "GBA DS" on the
 * internal storage (Android: defDir). `want` (data_dir) puts it elsewhere;
 * `record` is the file that says where it is now (data_dir_now, written
 * here). When want and the record differ, what's there is moved over (or,
 * if `want` already has GBA DS's data, that's used as it is). On Linux only
 * GBA DS's own folders move (the app folder keeps the program); with
 * `all`, everything in the old folder does (Android) and the emptied folder
 * goes. Writes the folder to use into out. */
void data_dir_setup(const char* defDir, const char* record, const char* want, bool all, char* out, size_t n);
/* what went wrong, for the game list's header ("" if nothing) */
const char* data_dir_note(void);

#endif

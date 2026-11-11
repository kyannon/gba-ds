#ifndef GBADS_PATHS_H
#define GBADS_PATHS_H

#include "config.h"

#include <stdbool.h>
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
	char patches[1024];   /* which patch is switched on, and its saves' switches (GEN-9) */
} GamePaths;

void game_paths(const Config* cfg, const char* rom, GamePaths* out);

/* ------------------------------------------------ patches (GEN-9) */
/* What's switched on for a ROM (<state_dir>/<game>.patches) */
typedef struct {
	char on[256];  /* the patch switched on: its file name in the game's folder, "" for none */
	bool sepSave;  /* its own battery save while a patch is on (the default) */
	bool sepStates; /* its own save states while a patch is on (the default) */
} PatchChoice;
void patch_choice_load(const char* path, PatchChoice* c);
void patch_choice_save(const char* path, const PatchChoice* c);
/* The game's patches folder: <patch_dir>/<the release's No-Intro name>, or
 * <patch_dir>/<header title> (<code>) for a game with no list of dumps;
 * made if it isn't there (make) */
void patch_folder(const Config* cfg, const char* rom, const char* code, char* out, size_t size, bool make);
/* The patches to apply, in order: the file beside the ROM with its name
 * (always on), then the one switched on in its folder. Returns how many. */
int patch_plan(const Config* cfg, const char* rom, const char* code, const PatchChoice* c, char files[][1024], int max);
/* A patch file's name without its folder or extension ("Superstar Saga Plus") */
void patch_title(const char* file, char* out, size_t size);
/* The game's saves while a patch is on: "<game> + <patch>" for the battery
 * save and / or the save states, as the choice says */
void game_paths_patched(const Config* cfg, GamePaths* o, const PatchChoice* c);
bool file_exists(const char* path);
void mkdirs(const char* path);
bool copy_file(const char* from, const char* to);

#endif

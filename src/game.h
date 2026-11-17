#ifndef GBADS_GAME_H
#define GBADS_GAME_H

#include "config.h"

#include <SDL.h>
#include <stdbool.h>

enum GameExit {
	GAME_QUIT_APP,
	GAME_TO_LIBRARY,
	GAME_RELOAD, /* start the same ROM again (a patch switched on or off, GEN-9) */
};

/* Plays one game until the player exits. fromLibrary adds a "Game list" menu item. */
enum GameExit game_run(Config* cfg, const char* rom, bool fromLibrary);
/* The pause menu's tab pictures (Settings uses them too): the page number, 0 Game, 1 Display, 2 Screens,
 * 3 Controls, 4 Speed, 5 Map, or GAME_ICON_SYSTEM (a toolbox). fg is the picture, bg what's behind it. */
#define GAME_ICON_SYSTEM 100
void game_page_icon(int scr, int icon, SDL_Rect box, SDL_Color fg, SDL_Color bg);
/* states/prefs.cfg: the pause menu's choices and the game list's Performance setting */
void prefs_save(Config* cfg);

#endif

#ifndef GBADS_GAME_H
#define GBADS_GAME_H

#include "config.h"

#include <stdbool.h>

enum GameExit {
	GAME_QUIT_APP,
	GAME_TO_LIBRARY,
	GAME_RELOAD, /* start the same ROM again (a patch switched on or off, GEN-9) */
};

/* Plays one game until the player exits. fromLibrary adds a "Game list" menu item. */
enum GameExit game_run(Config* cfg, const char* rom, bool fromLibrary);

#endif

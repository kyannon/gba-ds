#ifndef GBADS_LIBRARY_H
#define GBADS_LIBRARY_H

#include "config.h"

#include <stdbool.h>
#include <stddef.h>

/* Shows the game list (rescanned from cfg->romDir every time it opens).
 * Returns true with the chosen ROM in outPath, or false to quit the app. */
void library_bios_apply(const Config* cfg); /* emu_bios_path from gba_bios (issue #78) */
bool library_run(Config* cfg, char* outPath, size_t outSize);
/* The first-run language question (issue #92): whether this build asks it, and the screen itself,
 * shown until the player keeps a language (cfg->languageAsked false: a first install). */
bool library_language_ask_built(void);
void library_language_ask(Config* cfg);

#endif

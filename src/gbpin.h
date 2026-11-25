/* Pokémon Pinball (Game Boy Color): the table over both screens (docs/games/pokemon-pinball-gbc.md).
 * The game holds one half of its table in video memory at a time (the top stage or the bottom one) and
 * switches with a white flash. A hidden copy of the game (a "copy" below) is taken from the game's state
 * every frame, made to load the OTHER half with the game's own routine, and run that frame: its picture is
 * the other screen. Nothing of the game itself is changed. */
#ifndef GBADS_GBPIN_H
#define GBADS_GBPIN_H

#include <stdbool.h>
#include <stdint.h>
#include "emu.h"

typedef struct GbPin GbPin;

/* The copies for this ROM, or NULL when the ROM isn't one of the game's releases (its code isn't where it
 * should be). Two copies run side by side, each on a thread of its own unless `sync` (tests: every frame is
 * finished inside gbpin_after, so runs are exact). */
GbPin* gbpin_open(const char* romPath, bool sync);
void gbpin_close(GbPin* g);

/* Around each of the game's frames: before it (the game's state from before the frame is kept and a copy
 * starts on it with the same keys), after it (the copy started one frame earlier is collected). */
void gbpin_before(GbPin* g, Emu* game, uint32_t keys);
void gbpin_after(GbPin* g, Emu* game);

/* The table's two halves as the screens show them (GBA_W x GBA_H frames as Emu.fb has them: the game's
 * picture where emu.c puts it). false: the table isn't up (menus, the bonus stages): the game plays on
 * its own screen. top and bottom are valid until the next gbpin_after. */
/* true while a crossing's load is held (the game's white screen is being run through) */
bool gbpin_holding(const GbPin* g);
bool gbpin_table(const GbPin* g, const uint32_t** top, const uint32_t** bottom);
/* The stage the game is in (0 top, 1 bottom of a table), or -1 (for the tests' log) */
int gbpin_stage(const GbPin* g);

#endif

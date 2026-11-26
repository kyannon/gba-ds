/* The level over both screens for games whose levels are climbs: a copy of
 * the game with its camera a screen higher draws what's above the game's
 * picture (profile climb; docs/games/dk-king-of-swing.md). */
#ifndef GBADS_CLIMB_H
#define GBADS_CLIMB_H

#include "profiles.h"

/* Opens the copy beside the game; false (no copy) when the ROM isn't the one
 * the profile's patches are for. With `worker` the copy runs on a thread of its own, its pictures a frame or
 * two behind and moved with the camera (with no threads or one core: in step, the game waiting for it). */
bool climb_open(const char* rom, const MapProfile* p, Emu* game, bool worker);
void climb_close(void);
bool climb_is_open(void);

/* Before the game's frame: run is whether the copy is wanted this frame. The
 * copy's two frames (the game's before this one, then this one) run beside
 * the game's own, from the game's state before the frame before. */
void climb_begin(Emu* game, uint32_t keys, bool run);
/* After the game's frame: waits for the copy; its picture is kept only if its layers were drawn from the
 * scroll they should have (the game's, a screen higher). */
void climb_end(Emu* game);
/* The copy's picture of this frame's rows, NULL when it has none */
const uint32_t* climb_picture(void);
/* The copy runs on its own and hasn't had a picture ready yet since the level began or the game jumped */
bool climb_waiting(void);
/* The game jumped (a state loaded, a rewind): what the copy kept is from another moment */
void climb_reset(void);

#ifdef __wasi__
/* The web player's climb worker (main, with GBADS_CLIMB_SERVE=CODE, the ROM last): the copy's frames as jobs come */
int climb_web_serve(const char* rom, const char* code);
#endif

#endif

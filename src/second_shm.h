/* The second screen handed to another process (issue #86): on systems where each
 * panel belongs to its own compositor (Armada OS on the AYN Thor), no window of
 * the game can reach the other panel. With `second_screen = shm` the game draws
 * the second screen with a software renderer into a surface, as on Android, and
 * publishes every frame in shared memory; `gba-ds --present-second`, started on
 * the other compositor's display, shows it there. */
#ifndef GBADS_SECOND_SHM_H
#define GBADS_SECOND_SHM_H

#include <SDL.h>
#include <stdbool.h>

/* the game's side: make the shared frame w x h, then publish a surface of that size */
bool second_shm_open(int w, int h);
void second_shm_publish(const SDL_Surface* frame);
void second_shm_close(void);

/* "WxH" -> w, h; false if it isn't one */
bool second_shm_parse_size(const char* s, int* w, int* h);

/* `gba-ds --present-second [--second-shm=PATH]`: the presenter's whole run; returns the exit code */
int second_shm_present_main(int argc, char** argv);

#endif

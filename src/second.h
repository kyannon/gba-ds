/* The second screen's picture: the pure pixel steps (see second.c). */
#ifndef SECOND_H
#define SECOND_H
#include "mapcap.h"
#include "profiles.h"
#include <stddef.h>
#include <stdint.h>

/* the still map for "Map Animations: Off" into out (r->w * r->h), the pixels
 * that animate marked in mask; pick is the profile's stillPick (STILL_*) */
void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask);
/* the picture faded to level (0 .. 256) */
const uint32_t* second_fade(const uint32_t* px, size_t n, int level);

#endif

#ifndef GBADS_BEZEL_H
#define GBADS_BEZEL_H

#include <stdbool.h>
#include <stdint.h>
#include <SDL.h>

/* GBA DS's own bezel round the picture (Overlays: Generated), drawn on the
 * device: see bezel.c. */

#define BEZEL_INNER_DEFAULT 0x1A1A1A
#define BEZEL_OUTER_DEFAULT 0x102060

/* A screen of sw x sh whose picture is pic: RGBA32, transparent inside
 * the picture. bottom: the bottom screen's design (the logo above the
 * picture, by the hinge); inner, outer: RRGGBB; logo: the GBA DS logo in
 * the bezel. NULL if out of memory. */
SDL_Surface* bezel_surface(int sw, int sh, SDL_Rect pic, bool bottom, uint32_t inner, uint32_t outer, bool logo);

#endif

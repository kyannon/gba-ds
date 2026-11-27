/* min / max / clamp as SDL defines them (SDL_min, SDL_max, SDL_clamp: macros,
 * any arithmetic type, the arguments evaluated as SDL's are), for the code
 * moved out of game.c without SDL (libretro core, discussion #75). Not int
 * functions: the HUD's backdrop scaling clamps floats. */
#ifndef CLAMP_H
#define CLAMP_H
#define S_MIN(x, y) (((x) < (y)) ? (x) : (y))
#define S_MAX(x, y) (((x) > (y)) ? (x) : (y))
#define S_CLAMP(x, a, b) (((x) < (a)) ? (a) : (((x) > (b)) ? (b) : (x)))
#endif

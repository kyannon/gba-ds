/* Upscaling filters (issue #89): the game's picture made 2x or 4x larger by
 * a pixel-art scaler on the GPU (Scale2x / Scale4x, and mGBA's hq2x, MMPX,
 * xBR-lv2, xBR-lv3 and OmniScale), before SDL scales it to the screen.
 *
 * With SDL's OpenGL ES 2 renderer (the device, Android): our own small
 * GLES2 program runs between SDL's draws, on SDL's own context, and puts
 * back every piece of GL state SDL keeps track of. In the web build, the web
 * player's own WebGL2 context runs the same programs (each pass read back).
 * Anywhere else (SDL's software renderer on a computer, desktop OpenGL)
 * upscale_run returns NULL and the picture is drawn as without a filter. */
#ifndef GBADS_UPSCALE_H
#define GBADS_UPSCALE_H

#include "config.h" /* FILTER_*: Settings > Display > Scaling filter */

#include <SDL.h>
#include <stdbool.h>
#include <stdint.h>

/* how many times larger a filter makes the picture (0: not an upscaler) */
int upscale_factor(int filter);

/* Whether ren can run upscalers at all (an OpenGL ES 2 renderer); the first
 * call for a renderer sets it up and logs the result. */
bool upscale_ready(SDL_Renderer* ren);

/* How a filter suits ren's GPU (the menus grey and explain by it):
 *   FITS: runs (or isn't an upscaler);
 *   UNTRIED: a heavy filter on a GPU not tried: runs, may not hold full speed;
 *   TOO_HEAVY: a heavy filter on a GPU known too slow: drawn as Sharp;
 *   NO_GL: no OpenGL ES here: drawn as Sharp. */
enum { UPSCALE_FITS, UPSCALE_UNTRIED, UPSCALE_TOO_HEAVY, UPSCALE_NO_GL };
int upscale_fit(SDL_Renderer* ren, int filter);

/* Whether ren can run this filter: its program is built the first time it's
 * asked for (logged), and a filter whose program didn't build draws as Sharp. */
bool upscale_filter_ready(SDL_Renderer* ren, int filter);

/* src (w x h, SDL_PIXELFORMAT_ABGR8888) upscaled by the filter: a texture of
 * w x h times its factor, owned by this module (kept for the next frame), or
 * NULL when ren can't (then draw src as usual). Call with ren's current render
 * target being whatever it should stay: it is put back. */
SDL_Texture* upscale_run(SDL_Renderer* ren, SDL_Texture* src, int w, int h, int filter);

/* The web build: the pixels (ABGR8888) of upscale_run's last result, which
 * stay good until its next call; NULL elsewhere (they are on the GPU). */
const uint32_t* upscale_pixels(int* w, int* h);

/* ren is about to be destroyed: forget what was made for it */
void upscale_forget(SDL_Renderer* ren);

#endif

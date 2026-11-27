/* Upscaling filters (issue #89): the game's picture made 2x or 4x larger by
 * a pixel-art scaler on the GPU (Scale2x / Scale4x, and mGBA's hq2x and
 * xBR-lv2), before SDL scales it to the screen.
 *
 * Only with SDL's OpenGL ES 2 renderer (the device, Android): our own small
 * GLES2 program runs between SDL's draws, on SDL's own context, and puts
 * back every piece of GL state SDL keeps track of. Anywhere else (SDL's
 * software renderer, desktop OpenGL, the web build) upscale_run returns NULL
 * and the picture is drawn as without a filter. */
#ifndef GBADS_UPSCALE_H
#define GBADS_UPSCALE_H

#include "config.h" /* FILTER_*: Settings > Display > Scaling filter */

#include <SDL.h>
#include <stdbool.h>

/* how many times larger a filter makes the picture (0: not an upscaler) */
int upscale_factor(int filter);

/* Whether ren can run upscalers at all (an OpenGL ES 2 renderer); the first
 * call for a renderer sets it up and logs the result. */
bool upscale_ready(SDL_Renderer* ren);

/* Whether ren can run this filter: its program is built the first time it's
 * asked for (logged), and a filter whose program didn't build draws as Sharp. */
bool upscale_filter_ready(SDL_Renderer* ren, int filter);

/* src (w x h, SDL_PIXELFORMAT_ABGR8888) upscaled by the filter: a texture of
 * w x h times its factor, owned by this module (kept for the next frame), or
 * NULL when ren can't (then draw src as usual). Call with ren's current render
 * target being whatever it should stay: it is put back. */
SDL_Texture* upscale_run(SDL_Renderer* ren, SDL_Texture* src, int w, int h, int filter);

/* ren is about to be destroyed: forget what was made for it */
void upscale_forget(SDL_Renderer* ren);

#endif

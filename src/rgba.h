/* RGBA32 pictures (bytes R, G, B, A) without SDL: shared by ui.c (on SDL
 * surfaces' pixels) and the second screen's logo (second.c), so there's one
 * implementation (libretro core, discussion #75). */
#ifndef RGBA_H
#define RGBA_H
#include <stdint.h>

/* src (sw x sh, spitch bytes a row) averaged down (or picked up) to w x h
 * into dst (dpitch bytes a row): each output pixel the alpha-weighted mean of
 * the source pixels it covers (GEN-11). (Was ui.c's ui_resample loop.) */
void rgba_resample(const uint8_t* src, int sw, int sh, int spitch, uint8_t* dst, int w, int h, int dpitch);

#endif

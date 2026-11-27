/* The second screen on a panel leased from the compositor (DRM lease), for
 * systems whose compositor drives only one of the two panels: Armada's Game
 * Mode on the AYN Thor, where gamescope shows the top panel and leases the
 * bottom one (DSI-1) to a program that asks. GBA DS draws that screen with a
 * software renderer (ui.c, as Android's software second screen) and puts the
 * picture straight on the panel. Linux only. */
#ifndef GBADS_DRMLEASE_H
#define GBADS_DRMLEASE_H

#if defined(__linux__) && !defined(__ANDROID__)
#define GBADS_DRM_LEASE 1

#include <SDL.h>
#include <stdbool.h>

/* Leases the panel named by GBADS_DRM_LEASE (a connector such as DSI-1, or
 * "auto": the first internal panel offered) from the compositor at
 * GAMESCOPE_WAYLAND_DISPLAY (else WAYLAND_DISPLAY) and lights it, black.
 * False (logged) when GBADS_DRM_LEASE isn't set or no lease is granted.
 * *w, *h: the picture's size, upright (the panel's mode, turned by its
 * orientation). GBADS_DRM_LEASE_TEST=WxH,ROTATION[,PNG] (tests): no
 * compositor and no panel, a WxH panel in memory instead, written to PNG at
 * drmlease_close. */
bool drmlease_open(int* w, int* h);
/* the panel's orientation, counter-clockwise degrees as DRM counts them
 * (Right Side Up: 270), for the touchscreen's turn */
int drmlease_rotation(void);
/* Once a frame: the compositor's and the panel's events, then the surface
 * (the upright picture) onto the panel if it changed since it was last put
 * there (changed: drawn this frame). Waits for nothing: while the panel
 * still has a flip to show, the picture waits for the next frame. */
void drmlease_present(SDL_Surface* s, bool changed);
void drmlease_close(void);

#endif
#endif

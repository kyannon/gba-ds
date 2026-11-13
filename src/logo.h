/* Title-screen logos pulled out of the game itself (no image files needed).
 *
 * The ROM is booted in a silent emulator core, run to its title screen, and
 * the background layer holding the logo is rendered on its own; pixels that
 * match a backdrop-only render are transparent. The result is cropped and
 * cached as a PNG, so this only happens once per game. */
#ifndef GBADS_LOGO_H
#define GBADS_LOGO_H

#include <stdbool.h>
#include <stdint.h>

/* Synchronous extraction. Returns false if the game has no logo profile or
 * the logo couldn't be found. */
bool logo_extract(const char* romPath, const char* gameCode, const char* outPng);

/* Background queue: extracts outPng if it doesn't exist yet. */
void logo_request(const char* romPath, const char* gameCode, const char* outPng);
/* The web build: the next queued job (one a frame; it has no thread to do them). */
void logo_step(void);
/* Changes every time a queued logo finishes; compare to know when to reload. */
uint32_t logo_generation(void);
/* Background work done / total (queued + running + done) since the queue was
 * last idle; done == total when there's nothing left to do. */
void logo_progress(int* done, int* total);
/* A game's jobs (its logo) to the front of the queue, in
 * the order of the calls' reverse (the last call's first); urgent: they run
 * even while the queue's paused (the game about to start). */
void logo_front(const char* gameCode, bool urgent);
/* A job for this game queued or running; NULL: any job running. */
bool logo_busy(const char* gameCode);
/* While a game's played: only urgent jobs run (the game's own, asked for
 * while paused), the rest wait for the game list. */
void logo_pause(bool on);
void logo_shutdown(void);

#endif

/* SDL's timer for the web build: the clock is WASI's monotonic one, and
 * SDL_Delay hands control back to the page for that long (web_sleep, an
 * import the page answers with asyncify), so a wait doesn't freeze the tab. */
#include "../deps/SDL2/src/SDL_internal.h"

#include <time.h>

extern void web_sleep(unsigned ms) __attribute__((import_module("env"), import_name("web_sleep")));

static Uint64 start_ns;
static SDL_bool ticks_started;

static Uint64 now_ns(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (Uint64) ts.tv_sec * 1000000000u + (Uint64) ts.tv_nsec;
}

void SDL_TicksInit(void) {
	if (ticks_started) return;
	ticks_started = SDL_TRUE;
	start_ns = now_ns();
}

void SDL_TicksQuit(void) {
	ticks_started = SDL_FALSE;
}

Uint64 SDL_GetTicks64(void) {
	if (!ticks_started) SDL_TicksInit();
	return (now_ns() - start_ns) / 1000000u;
}

Uint64 SDL_GetPerformanceCounter(void) {
	return now_ns();
}

Uint64 SDL_GetPerformanceFrequency(void) {
	return 1000000000u;
}

void SDL_Delay(Uint32 ms) {
	web_sleep(ms);
}

/* The web build's side of the page (State Saver's GBA DS tab): the page
 * presses keys in (web_key, as SDL keyboard events and state), reads the
 * window's pixels after each frame (web_screen) and answers web_sleep
 * (game.c, SDL_Delay) by setting asyncify going and coming back later. */
#include <SDL.h>
#include <stdint.h>
#include <stdlib.h>

/* SDL's own (src/events/SDL_keyboard_c.h) */
extern int SDL_SendKeyboardKey(Uint8 state, SDL_Scancode scancode);

__attribute__((export_name("web_key"))) void web_key(int scancode, int down) {
	if (scancode > 0 && scancode < SDL_NUM_SCANCODES) SDL_SendKeyboardKey(down ? SDL_PRESSED : SDL_RELEASED, (SDL_Scancode) scancode);
}

/* The biggest window's pixels (XRGB8888, so bytes B, G, R, X), with its
 * size and row length in bytes at out[0..2]; NULL before there's one. */
__attribute__((export_name("web_screen"))) void* web_screen(int* out) {
	SDL_Window* best = NULL;
	int bestArea = 0;
	for (Uint32 id = 1; id < 16; ++id) {
		SDL_Window* w = SDL_GetWindowFromID(id);
		int ww, wh;
		if (!w) continue;
		SDL_GetWindowSize(w, &ww, &wh);
		if (ww * wh > bestArea) best = w, bestArea = ww * wh;
	}
	SDL_Surface* s = best ? SDL_GetWindowSurface(best) : NULL;
	if (!s) return NULL;
	out[0] = s->w;
	out[1] = s->h;
	out[2] = s->pitch;
	return s->pixels;
}

__attribute__((export_name("web_alloc"))) void* web_alloc(size_t n) {
	return malloc(n);
}

/* `make module-check`: the second-screen module (second.c, hud.c, twins.c,
 * toggle.c, workers.c, rgba.c) and the core it runs on (emu.c, mapcap.c,
 * profiles.c, logo.c...) built with no SDL include path and linked with no
 * SDL library, so they stay what a libretro core can compile (discussion
 * #75). This program only has to build: it takes the address of every call a
 * core makes (second.h's first section), so none of them can go missing. */
#include "../src/second.h"
#include "../src/hud.h"
#include "../src/toggle.h"
#include "../src/twins.h"
#include "../src/workers.h"
#include "../src/rgba.h"
#include "../src/clamp.h"
#include <stdio.h>

#ifdef SDL_h_
#error "SDL reached the second-screen module"
#endif

int main(void) {
	const void* calls[] = {
		(const void*) second_open, (const void*) second_drop, (const void*) second_step, (const void*) second_close,
		(const void*) second_new_subject, (const void*) second_state_save, (const void*) second_state_load,
		(const void*) second_idle_picture, (const void*) logo_draw,
	};
	int n = 0;
	for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) n += calls[i] != NULL;
	printf("module-check: the second-screen module builds and links without SDL (%d calls, SecondState %zu bytes, HudState %zu bytes)\n",
	       n, sizeof(SecondState), sizeof(HudState));
	return n == (int) (sizeof(calls) / sizeof(calls[0])) ? 0 : 1;
}

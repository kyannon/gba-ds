/* `make module-check`: the second-screen module (second.c, hud.c, twins.c,
 * toggle.c, workers.c, rgba.c) and the core it runs on (emu.c, mapcap.c,
 * profiles.c, logo.c...) built with no SDL include path and linked with no
 * SDL library, so they stay what a libretro core can compile (discussion
 * #75). This program only has to build: it takes the address of every call a
 * core makes (second.h's first section, and emu.c's for its states and
 * battery save in memory), so none of them can go missing. */
#include "../src/second.h"
#include "../src/hud.h"
#include "../src/toggle.h"
#include "../src/twins.h"
#include "../src/workers.h"
#include "../src/rgba.h"
#include "../src/clamp.h"
#include <stdio.h>
#include <stdlib.h>

#ifdef SDL_h_
#error "SDL reached the second-screen module"
#endif

int main(void) {
	const void* calls[] = {
		(const void*) second_open, (const void*) second_drop, (const void*) second_step, (const void*) second_close,
		(const void*) second_new_subject, (const void*) second_state_save, (const void*) second_state_load,
		(const void*) second_idle_picture, (const void*) logo_draw,
		(const void*) second_run_frame, (const void*) second_frame_start,
		(const void*) second_shown, (const void*) second_jump, (const void*) second_state_loaded,
		(const void*) second_restarted, (const void*) second_logo_changed, (const void*) second_game_start,
		(const void*) second_loop_start, (const void*) emu_set_sync_work,
		(const void*) emu_state_save_mem, (const void*) emu_state_load_mem, (const void*) emu_state_mem_get_extra,
		(const void*) emu_save_memory, (const void*) emu_save_memory_size,
	};
	/* the synchronous-work setting (checklist item 4): off unless GBADS_SYNC says so, then
	 * as a core sets it */
	const char* env = getenv("GBADS_SYNC");
	bool want = env && atoi(env);
	if (emu_sync_work() != want) {
		printf("module-check: emu_sync_work() is %d with GBADS_SYNC=%s\n", emu_sync_work(), env ? env : "(unset)");
		return 1;
	}
	emu_set_sync_work(true);
	bool on = emu_sync_work();
	emu_set_sync_work(false);
	if (!on || emu_sync_work()) {
		printf("module-check: emu_set_sync_work doesn't take\n");
		return 1;
	}
	int n = 0;
	for (size_t i = 0; i < sizeof(calls) / sizeof(calls[0]); ++i) n += calls[i] != NULL;
	printf("module-check: the second-screen module builds and links without SDL (%d calls, SecondState %zu bytes, HudState %zu bytes)\n",
	       n, sizeof(SecondState), sizeof(HudState));
	return n == (int) (sizeof(calls) / sizeof(calls[0])) ? 0 : 1;
}

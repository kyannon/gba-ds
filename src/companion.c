/* The Pokemon companion on the second screen: see companion.h. The host
 * part of PokeDaisy's 3DS port (its ctr/main.c) done for GBA DS. */
#include "companion.h"

#include <string.h>

bool comp_supports(const char* code) {
#ifdef GBADS_POKEDAISY
	return !strcmp(code, "BPRE") || !strcmp(code, "BPGE") || !strcmp(code, "BPEE");
#else
	(void) code;
	return false;
#endif
}

#ifdef GBADS_POKEDAISY

#include <stdio.h>
#include <string.h>

#include "pd_canvas.h"
#include "pd_game.h"
#include "pd_settings.h"
#include "pd_snapshot.h"
#include "pd_ui.h"

#define SNAPSHOT_EVERY 15 /* frames between reads of the game (4 a second) */

static bool on;
static struct pd_game game;
static struct pd_settings settings;
static char settingsPath[1100];
static struct pd_ui pui;
static struct pd_snapshot snap, shown;
static struct pd_canvas canvas;
static uint32_t px[PD_UI_WIDTH * PD_UI_HEIGHT];
static SDL_Texture* tex[2];
static bool redraw, uploaded[2];
static unsigned long frame;
static Uint32 lastTick;
static Emu* emu;
static struct {
	uint8_t *ewram, *iwram;
	size_t ewramSize, iwramSize;
} mem;

/* The ROM's bytes, asked for again each time: mGBA swaps the ROM for a copy
 * (and unmaps the file) when something writes to it (game hardware,
 * cheats, patches), so a pointer kept from the start can go stale. */
static void refresh_rom(void) {
	size_t n = 0;
	const uint8_t* rom = emu_memory(emu, 8, &n);
	if (rom && rom != game.rom) {
		if (game.rom) fprintf(stderr, "companion: the ROM moved in memory (mGBA's copy on write)\n");
		game.rom = rom;
	}
}

static bool bus_read(void* ctx, uint32_t addr, void* out, size_t len) {
	(void) ctx;
	if (addr >= 0x02000000 && addr - 0x02000000 + len <= mem.ewramSize) {
		memcpy(out, mem.ewram + (addr - 0x02000000), len);
		return true;
	}
	if (addr >= 0x03000000 && addr - 0x03000000 + len <= mem.iwramSize) {
		memcpy(out, mem.iwram + (addr - 0x03000000), len);
		return true;
	}
	/* ROM: pointers in RAM often lead into it (a map's layout) */
	if (game.rom && addr >= 0x08000000 && addr - 0x08000000 + len <= game.romSize) {
		memcpy(out, game.rom + (addr - 0x08000000), len);
		return true;
	}
	return false;
}

bool comp_open(Emu* e, const char* path) {
	comp_close();
	size_t romSize = 0;
	const uint8_t* rom = emu_memory(e, 8, &romSize);
	romSize = SDL_min(romSize, emu_rom_size(e)); /* the file, not the 32 MB window */
	if (!rom || romSize < 0xC0) return false;
	memset(&game, 0, sizeof(game));
	pd_game_detect(&game, rom, romSize);
	if (game.kind == PD_GAME_NONE) return false;
	if (!game.cfg) {
		fprintf(stderr, "companion: %s (%s) not supported: %s\n", game.title ? game.title : "?", game.code,
		        game.unsupported ? game.unsupported : "?");
		return false;
	}
	game.rom = rom;
	game.romSize = romSize;
	emu = e;
	mem.ewram = emu_memory(e, 2, &mem.ewramSize);
	mem.iwram = emu_memory(e, 3, &mem.iwramSize);
	snprintf(settingsPath, sizeof(settingsPath), "%s", path);
	memset(&settings, 0, sizeof(settings));
	settings.ffSpeed = PD_FF_MIN;
	pd_settings_load(&settings, settingsPath);
	pd_ui_init(&pui, &settings);
	memset(&snap, 0, sizeof(snap));
	pd_snapshot_read(&snap, &game, bus_read, NULL);
	shown = snap;
	pd_canvas_init(&canvas, PD_UI_WIDTH, PD_UI_HEIGHT, px);
	frame = 0;
	lastTick = SDL_GetTicks();
	redraw = true;
	on = true;
	fprintf(stderr, "companion: %s (%s rev %d)\n", game.title, game.code, game.revision);
	return true;
}

void comp_close(void) {
	for (int i = 0; i < 2; ++i) {
		if (tex[i]) SDL_DestroyTexture(tex[i]);
		tex[i] = NULL;
	}
	on = false;
}

bool comp_active(void) {
	return on;
}

void comp_frame(Emu* e) {
	if (!on) return;
	if (++frame % SNAPSHOT_EVERY == 0 && mem.ewram && mem.iwram) {
		refresh_rom();
		mem.ewram = emu_memory(e, 2, &mem.ewramSize);
		mem.iwram = emu_memory(e, 3, &mem.iwramSize);
		pd_snapshot_read(&snap, &game, bus_read, NULL);
		pd_ui_update(&pui, &snap);
		/* only a change the companion shows is worth a redraw (not the frame counter) */
		snap.frame = 0;
		if (memcmp(&snap, &shown, sizeof(snap))) {
			shown = snap;
			redraw = true;
		}
	}
	(void) e;
}

void comp_draw(int s, bool hasState, bool ffOn) {
	if (!on) return;
	Uint32 now = SDL_GetTicks();
	redraw |= pd_ui_tick(&pui, now - lastTick);
	lastTick = now;
	if (redraw) {
		refresh_rom();
		struct pd_host_info host;
		memset(&host, 0, sizeof(host));
		host.labels[0] = "EMULATOR";
		snprintf(host.values[0], sizeof(host.values[0]), "GBA DS");
		host.count = 1;
		host.ffOn = ffOn;
		host.hasState = hasState;
		pd_ui_draw(&pui, &canvas, &game, &shown, &host);
		redraw = false;
		uploaded[0] = uploaded[1] = false;
	}
	for (int v = 0; v < nViews; ++v) {
		if (!tex[v]) {
			tex[v] = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_RGB888, SDL_TEXTUREACCESS_STREAMING, PD_UI_WIDTH,
			                           PD_UI_HEIGHT);
			uploaded[v] = false;
		}
		if (tex[v] && !uploaded[v]) {
			ui_update_texture(tex[v], px, PD_UI_WIDTH * 4);
			uploaded[v] = true;
		}
	}
	/* a whole number of times as big (3x on the RG DS Plus), centred, sharp */
	ui_image_pixels(s, tex, PD_UI_WIDTH, PD_UI_HEIGHT, "integer", 0);
}

bool comp_pointer(int s, const Pointer* p) {
	if (!on) return false;
	SDL_Rect r = ui_image_rect(s, PD_UI_WIDTH, PD_UI_HEIGHT, "integer");
	if (!r.w || !r.h) return false;
	int x = (p->x - r.x) * PD_UI_WIDTH / r.w, y = (p->y - r.y) * PD_UI_HEIGHT / r.h;
	x = SDL_max(0, SDL_min(x, PD_UI_WIDTH - 1));
	y = SDL_max(0, SDL_min(y, PD_UI_HEIGHT - 1));
	/* down and moves: held at (x, y); up: let go */
	redraw |= pd_ui_touch(&pui, x, y, p->kind != 2);
	return true;
}

int comp_take_action(void) {
	if (!on) return COMP_NONE;
	switch (pd_ui_take_action(&pui)) {
	case PD_ACTION_SAVE_STATE: return COMP_SAVE_STATE;
	case PD_ACTION_LOAD_STATE: return COMP_LOAD_STATE;
	case PD_ACTION_LEAVE_GAME: return COMP_LEAVE;
	case PD_ACTION_TOGGLE_FF: return COMP_TOGGLE_FF;
	case PD_ACTION_SETTINGS_CHANGED:
		pd_settings_save(&settings, settingsPath);
		redraw = true;
		return COMP_NONE;
	default: return COMP_NONE;
	}
}

#else /* no companion in this build */

bool comp_open(Emu* e, const char* settingsPath) {
	(void) e, (void) settingsPath;
	return false;
}
void comp_close(void) {}
bool comp_active(void) {
	return false;
}
void comp_frame(Emu* e) {
	(void) e;
}
void comp_draw(int s, bool hasState, bool ffOn) {
	(void) s, (void) hasState, (void) ffOn;
}
bool comp_pointer(int s, const Pointer* p) {
	(void) s, (void) p;
	return false;
}
int comp_take_action(void) {
	return COMP_NONE;
}

#endif

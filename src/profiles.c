#include "profiles.h"

#include <mgba/core/core.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>

/* Values below were found with tools/harness + tools/ramdiff.py against the
 * USA releases. Each profile describes how to bring up the in-game map and
 * how to tell, from memory and from the picture, that it is really showing. */
/* Metroid Fusion's SLEEP / STATUS plates cycle through four palettes
 * (green, teal, blue, bright blue): body, edge, lettering. */
#define PLATE_TEXT { 0x10FF84, 0x31FFAD, 0x52FFD6, 0x7BFFFF, 0x10B55A, 0x18B594, 0x29ADCE, 0x39A5FF }
#define PLATE_FROM { 0x086B31, 0x106B73, 0x1863BD, 0x2963FF, 0x10B55A, 0x18B594, 0x29ADCE, 0x39A5FF, \
                     0x10FF84, 0x31FFAD, 0x52FFD6, 0x7BFFFF, 0x21214A }
#define PLATE_TO   { 0xADC6BD, 0xADC6BD, 0xADC6BD, 0xADC6BD, 0xC6DED6, 0xC6DED6, 0xC6DED6, 0xC6DED6, \
                     0xDEF7EF, 0xDEF7EF, 0xDEF7EF, 0xDEF7EF, 0x5A5A5A }

/* ---------------------------------------------------------- Advance Wars
 *
 * Advance Wars and Advance Wars 2. The second screen, after Advance Wars:
 * Dual Strike's: the unit under the cursor's intel (R, with its text box)
 * when there's a unit there, yours or the enemy's (or the one being moved),
 * or else the Battle Intel (or when R shows nothing: fog). Each is opened
 * on the shadow core from the live state: B (backs out of a move or an
 * action menu), then R, or SELECT > Intel > Status. On the enemy's turn
 * neither opens and the last picture stays. Nothing leaves the game
 * screen. (Until v1.3.2's later test builds the minimap was below them,
 * and the CO plate and the terrain and unit info moved there with a HUD
 * split: see git history.) (USA addresses; Advance Wars 2's in AwRegion.) */
#define AW_ARMY 0x030035E8   /* the army whose turn it is, 1-4; 0 off the battle map */
#define AW_CURSOR 0x030033F0 /* map cursor x, y (stays on a unit while it moves) */
#define AW_UNITS 0x02019F34  /* + 0x300 * (army - 1) + 12 * id (1-64): type, -, x, y, ... */
#define AW_MENU_ROW 0x03000E50 /* the SELECT menu's and its submenus' cursor */
#define AW_MINIMAP_OPEN 0x03000E2E /* counts 0-12 as the START minimap opens */
/* Advance Wars 2 (USA; Europe the same): found by matching the code that
 * reads Advance Wars' (the army) and by watching them change */
#define AW2_ARMY 0x030005D0
#define AW2_CURSOR 0x03003140
#define AW2_UNITS 0x02022684
#define AW2_MENU_ROW 0x030014B2
#define AW2_MINIMAP_OPEN 0x0300148E
/* Game Boy Wars Advance 1+2 (Japan): game 1 (Advance Wars) and game 2
 * (Advance Wars 2) */
#define GBW1_ARMY 0x030033A8
#define GBW1_CURSOR 0x030031B0
#define GBW1_UNITS 0x0201A138
#define GBW2_ARMY 0x030005D8
#define GBW2_CURSOR 0x03003250
#define GBW2_UNITS 0x02022574
/* the weather's sprites: rain (tiles 169, 171), in OBJ palette 2 */
#define AW_WEATHER { -512, -256, 511, 255, 160, 191, 0, 1u << 2, 0 }
#define MAP_BG_NAVY 0xFF382418 /* behind the panels: dark navy (bytes R, G, B) */
#define AW_BG MAP_BG_NAVY

/* The computer's turn (AW-4, AW2-4): an army after the first. The
 * player's is the first in the campaigns (and in Versus from the first
 * slot); nothing found says which armies a person plays (each army's
 * settings, 0x3C / 0x68 bytes from 0x02023284 + 0x3C / 0x0201AB9C, hold
 * 1, 2... at +0x1B whoever plays them; a two-player Versus state
 * (us-co-screen) and two against the computer read alike), so a later
 * army played by a person shows the logo too (a known limitation). */
#define AW_V_CPU 0xF0000050 /* 1 on the computer's turn */
#define AW_TASK 0x03000000 /* the game's first task: its routine (AwRegion.mapTask) */
#define AW_V_OFFMAP 0xF0000051 /* 1 off the battle map: the title, menus, a battle's results */
typedef struct {
	uint32_t army, cursor, units, menuRow, minimapOpen; /* (minimapOpen: counts to 12 as START's minimap opens) */
	int intelItem; /* Intel's row in the SELECT menu */
	uint32_t intelPx[2][3]; /* the Battle Intel is up: x, y, colour (0xRRGGBB) */
	int intelRows;          /* the Battle Intel's panel: the rows above the map */
	uint32_t unitOpen[2];   /* this word holds this value (the screen's routine) while the unit intel (R) is up */
	uint32_t nextArmy; /* the army whose turn is starting: set as soon as End is chosen,
	                    * while the army byte keeps the last one's through the day
	                    * banner (Advance Wars' engine; 0: none, Advance Wars 2's army
	                    * byte changes at once) */
	int cornerTile; /* the yellow corners blinking round the unit intel's picture (AW-1,
	                 * AW2-1): four 8x8 sprites of this tile (flipped), palette 1, left out */
	uint32_t mapTask; /* 0x03000000 holds this (a routine) on the battle map, 0 at moments
	                   * (the computer's turn, fights), anything else elsewhere (a battle's
	                   * results, the title, the mode menu: the army byte keeps the last
	                   * battle's there). Found in Field Training's results (AW1), the mode
	                   * menu and a Versus battle (AW2). */
	int statusItem;   /* Status's row in the Intel menu tried first (the campaign's: Terms,
	                   * Status, CO; other modes: Status, CO, Rules; aw_panel tries the others) */
	int labelTile;    /* the unit intel's label over its panel (INTEL, SPEC): a 32x8 sprite of
	                   * this tile in palette 6, left out of AW-1's frame (0: none) */
} AwRegion;

static const AwRegion awRegions[] = {
	{ AW_ARMY, AW_CURSOR, AW_UNITS, AW_MENU_ROW, AW_MINIMAP_OPEN, 1, { { 4, 4, 0xFFE7C6 }, { 200, 100, 0xFFE7C6 } }, 124, { 0x03000ED0, 0x082E3FD4 }, 0x030036AC, 876, 0x0801FA69, 0, 537 },
	/* (its SELECT menu has no Unit item: CO, Intel, Options, Save, End) */
	{ AW2_ARMY, AW2_CURSOR, AW2_UNITS, AW2_MENU_ROW, AW2_MINIMAP_OPEN, 1, { { 200, 100, 0xFFEFEF }, { 120, 140, 0xFFEFEF } }, 152, { 0x03001530, 0x0849E2C0 }, 0, 873, 0x08022049 },
	/* Game Boy Wars Advance 1+2 (Japan): each game at its own addresses
	 * (game 1's found by the literal pools around its code: 0x240 lower
	 * than Advance Wars' above 0x03003000, 0x10 lower around 0x03000E00;
	 * the unit intel's routine by its shape; game 2's by watching them) */
	{ GBW1_ARMY, GBW1_CURSOR, GBW1_UNITS, 0x03000E40, 0x03000E1E, 1, { { 4, 4, 0xFFE7C6 }, { 200, 100, 0xFFE7C6 } }, 124, { 0x03000EC0, 0x08DEC9D8 }, 0x0300346C, 876, 0x08B1FFA1, 0, 537 },
	{ GBW2_ARMY, GBW2_CURSOR, GBW2_UNITS, AW2_MENU_ROW, AW2_MINIMAP_OPEN, 1, { { 200, 100, 0xFFEFEF }, { 120, 140, 0xFFEFEF } }, 152, { 0x03001530, 0x0881A0A4 }, 0, 873, 0x08322509 },
	/* Advance Wars 1 and 2 in Europe (AWRP, AW2P): the USA's RAM, their
	 * routines elsewhere in the ROM (the map's and the unit intel's,
	 * read from the owner's campaign states) */
	{ AW_ARMY, AW_CURSOR, AW_UNITS, AW_MENU_ROW, AW_MINIMAP_OPEN, 1, { { 4, 4, 0xFFE7C6 }, { 200, 100, 0xFFE7C6 } }, 124, { 0x03000ED0, 0x083C356C }, 0x030036AC, 876, 0x0801FDD1, 1, 537 },
	{ AW2_ARMY, AW2_CURSOR, AW2_UNITS, AW2_MENU_ROW, AW2_MINIMAP_OPEN, 1, { { 200, 100, 0xFFEFEF }, { 120, 140, 0xFFEFEF } }, 152, { 0x03001530, 0x08521548 }, 0, 873, 0x080221B5 },
};

/* Game Boy Wars Advance 1+2: which game is running, by where the
 * interrupt handler is (game 1 runs it from IWRAM, game 2 from the
 * ROM; the compilation's own menus elsewhere): 1, 2, or 0 */
#define GBW_IRQ1 0x03000708
#define GBW_IRQ2 0x083000FC
static int gbw_game(Emu* s) {
	uint32_t v = emu_read32(s, 0x03007FFC);
	return v == GBW_IRQ1 ? 1 : v == GBW_IRQ2 ? 2 : 0;
}

static const AwRegion* aw_r(Emu* s) {
	if (!strncmp(s->gameCode, "BGWJ", 4)) return &awRegions[gbw_game(s) == 1 ? 2 : 3];
	return &awRegions[(s->gameCode[2] == '2' ? 1 : 0) + (s->gameCode[3] == 'P' ? 4 : 0)];
}

static uint32_t aw_virt(Emu* e, uint32_t addr) {
	if (!strncmp(e->gameCode, "BGWJ", 4) && !gbw_game(e)) return 0; /* (the compilation's own menus) */
	const AwRegion* r = aw_r(e);
	uint8_t army = emu_read8(e, r->army);
	uint32_t task = emu_read32(e, AW_TASK);
	/* (0 counts as off too: the pictures go and come back once a capture's
	 * possible again, the computer's turn and fights showing the logo anyway) */
	if (addr == AW_V_OFFMAP) return !army || task != r->mapTask;
	if (addr != AW_V_CPU) return 0;
	uint8_t next = r->nextArmy ? emu_read8(e, r->nextArmy) : 0;
	return ((army >= 2 && army <= 4) || (next >= 2 && next <= 4)) && (task == r->mapTask || !task);
}

/* n frames with these buttons held; only the last two drawn (what's
 * looked at next: the second-to-last too, so anything that keeps state
 * from one drawn frame to the next has it) */
static void aw_run(Emu* s, uint32_t keys, int n) {
	for (int i = 0; i < n; ++i) {
		if (i < n - 2) emu_frame_blind(s, keys);
		else emu_frame(s, keys);
	}
}

static void aw_tap(Emu* s, uint32_t keys, int n) {
	/* the quickest the game takes presses reliably */
	for (int i = 0; i < n; ++i) {
		aw_run(s, keys, 2);
		aw_run(s, 0, 4);
	}
}

/* The map with the cursor free (the CO plate and terrain info up: window 0
 * alone, as the map has it), where SELECT and START work. */
static bool aw_idle(Emu* s) {
	if ((emu_read16(s, 0x04000000) & 0x6000) != 0x2000 || emu_read16(s, 0x04000048) != 0x003F) return false;
	/* and the CO plate up (picking a target has the info, not the plate):
	 * its first sprite, tile 0 in palette 7 */
	for (uint32_t a = 0x07000000; a < 0x07000400; a += 8) {
		uint16_t a0 = emu_read16(s, a), a2 = emu_read16(s, a + 4);
		if ((a0 & 0x0300) != 0x0200 && (a0 & 0xFF) < 160 && (a2 & 0xF3FF) == 0x7000) return true;
	}
	return false;
}

/* Backs out of whatever is going on (a move, an action menu, a menu)
 * with B until the cursor is free; false if it never is (the enemy's
 * turn, a fight). */
static bool aw_back(Emu* s) {
	for (int i = 0; i < 16; ++i) {
		for (int f = 0; f < 8; ++f) {
			emu_frame(s, 0);
			if (aw_idle(s)) return true;
		}
		aw_tap(s, K_B, 1);
	}
	return false;
}

/* pixel (x, y) of the screen is about this colour (0xRRGGBB) */
static bool aw_px(Emu* s, int x, int y, uint32_t rgb) {
	uint32_t c = s->fb[y * GBA_W + x];
	int d[3] = { (int) (c & 0xFF) - (int) (rgb >> 16), (int) ((c >> 8) & 0xFF) - (int) ((rgb >> 8) & 0xFF),
	             (int) ((c >> 16) & 0xFF) - (int) (rgb & 0xFF) };
	for (int i = 0; i < 3; ++i) {
		if (d[i] < -16 || d[i] > 16) return false;
	}
	return true;
}

/* the unit intel (R) is up: the CO plate's gone (what's up is checked
 * afterwards: aw_unit_cols) */
static bool aw_unit_up(Emu* s) {
	return !aw_idle(s);
}

static bool aw_intel_up(Emu* s) {
	const AwRegion* r = aw_r(s);
	return aw_px(s, r->intelPx[0][0], r->intelPx[0][1], r->intelPx[0][2]) &&
	       aw_px(s, r->intelPx[1][0], r->intelPx[1][1], r->intelPx[1][2]);
}

/* Runs until `up` holds and the picture stops changing (a few blinking
 * pixels allowed); false if it never does. */
static bool aw_settle(Emu* s, bool (*up)(Emu*), int limit) {
	static uint32_t prev[GBA_W * GBA_H];
	int stable = 0;
	bool have = false;
	for (int f = 0; f < limit; ++f) {
		emu_frame(s, 0);
		if (!up(s)) {
			have = false;
			stable = 0;
			continue;
		}
		if (have) {
			int d = 0;
			for (int i = 0; i < GBA_W * GBA_H && d <= 64; ++i) d += prev[i] != s->fb[i];
			stable = d <= 64 ? stable + 1 : 0;
			if (stable >= 3) return true;
		}
		memcpy(prev, s->fb, sizeof(prev));
		have = true;
	}
	return false;
}

/* The minimap (START), alone: rendered with only BG0, where the game draws
 * it, and cut out by comparing with a render of no layers at all. */
static bool aw_minimap(Emu* s, const void* state, void* tmp, uint32_t* mm, int* mw, int* mh) {
	const AwRegion* r = aw_r(s);
	if (!emu_state_load(s, state)) return false;
	if (!aw_back(s)) return false;
	/* START (again if a cancelled move was still going back and took no
	 * presses), until the minimap's opening count reaches its end */
	bool open = false;
	for (int tries = 0; tries < 3 && !open; ++tries) {
		aw_run(s, K_START, 2);
		for (int f = 0; f < 30 && !open; ++f) {
			emu_frame(s, 0);
			open = emu_read8(s, r->minimapOpen) == 12;
		}
	}
	if (!open) return false;
	aw_run(s, 0, 2);
	if (!emu_state_save(s, tmp)) return false;
	static uint32_t none[GBA_W * GBA_H];
	emu_layers(s, 0);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	memcpy(none, s->fb, sizeof(none));
	emu_layers(s, 0x01);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1;
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			if (s->fb[y * GBA_W + x] == none[y * GBA_W + x]) continue;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	}
	/* the minimap is an opaque box in the middle of the screen; anything
	 * else on BG0 (START did nothing: an action menu, a move range) isn't */
	bool ok = x1 - x0 >= 15 && y1 - y0 >= 15 && abs(x0 + x1 - (GBA_W - 1)) <= 4 && abs(y0 + y1 - (GBA_H - 1)) <= 4;
	if (ok) {
		int filled = 0;
		for (int y = y0; y <= y1; ++y) {
			for (int x = x0; x <= x1; ++x) filled += s->fb[y * GBA_W + x] != none[y * GBA_W + x];
		}
		ok = filled * 100 >= (x1 - x0 + 1) * (y1 - y0 + 1) * 97;
	}
	if (ok) {
		*mw = x1 - x0 + 1;
		*mh = y1 - y0 + 1;
		for (int y = 0; y < *mh; ++y) memcpy(&mm[y * *mw], &s->fb[(y0 + y) * GBA_W + x0], *mw * sizeof(uint32_t));
	}
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	return ok;
}

/* The unit under the map cursor, if it's one of the army whose turn it is:
 * its id (1-64), else 0. */
static int aw_cursor_unit(Emu* s, int army) {
	const AwRegion* r = aw_r(s);
	int cx = emu_read8(s, r->cursor), cy = emu_read8(s, r->cursor + 1);
	uint32_t base = r->units + 0x300 * (uint32_t) (army - 1);
	for (int id = 1; id <= 64; ++id) {
		uint32_t a = base + 12 * (uint32_t) id;
		if (emu_read8(s, a) && emu_read8(s, a + 2) == cx && emu_read8(s, a + 3) == cy) return id;
	}
	return 0;
}

/* Any army's unit under the cursor (R shows enemies' too); true if one is. */
static bool aw_cursor_any(Emu* s) {
	for (int army = 1; army <= 4; ++army) {
		if (aw_cursor_unit(s, army)) return true;
	}
	return false;
}

/* The panel now on screen without the battle map around it: rendered
 * again with BG0 and BG2 (menus and text) and the sprites, less the
 * priority-3 ones (the map's buildings); what that leaves empty is AW_BG.
 * bg2: BG2 alone too (the boxes), AW_BG where it has nothing. */
static void aw_clean(Emu* s, void* tmp, uint32_t* pan, uint32_t* bg2) {
	static uint32_t none[GBA_W * GBA_H];
	int ct = aw_r(s)->cornerTile;
	/* (palette 1 from the corners' tile on: the corners and the map's
	 * cursors, its targeting pointer too (Advance Wars: 876-928; Advance
	 * Wars 2: 873-906), which can reach into the text box's edge; the
	 * panel's own icons in palette 1 are 960 and up) */
	const SpriteRule rules[3] = { { -512, -256, 511, 255, 0, 1023, 3, 0, 0 }, AW_WEATHER, { -512, -256, 511, 255, ct, ct + 60, 0, 1u << 1, 0 } };
	if (!emu_state_save(s, tmp)) {
		memcpy(pan, s->fb, GBA_W * GBA_H * sizeof(uint32_t));
		return;
	}
	emu_layers(s, 0);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	memcpy(none, s->fb, sizeof(none));
	if (bg2) {
		emu_layers(s, 0x04);
		emu_state_load(s, tmp);
		emu_frame(s, 0);
		for (int i = 0; i < GBA_W * GBA_H; ++i) bg2[i] = s->fb[i] == none[i] ? AW_BG : s->fb[i];
	}
	emu_layers(s, 0x15);
	/* (and the unit intel's label, INTEL / SPEC, over its panel: AW-1's
	 * frame has the panel without its edge) */
	int lt = aw_r(s)->labelTile;
	const SpriteRule hide[4] = { rules[0], rules[1], rules[2], { -512, -16, 511, 8, lt, lt, -1, 1u << 6, 0 } };
	emu_hide_sprites(s, hide, lt ? 4 : 3);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	for (int i = 0; i < GBA_W * GBA_H; ++i) pan[i] = s->fb[i] == none[i] ? AW_BG : s->fb[i];
	emu_hide_sprites(s, &rules[1], 2);
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp);
}

/* The unit intel's panel: the columns where BG2 (the boxes) covers both
 * the top and the bottom of the screen (the text box beside it is only
 * at one or the other); the longest such run. False if there's none (the
 * unit intel isn't what opened). */
static bool aw_unit_cols(const uint32_t* bg2, int* x0, int* x1) {
	int best = 0, run = 0;
	for (int x = 0; x <= GBA_W; ++x) {
		bool tall = x < GBA_W && bg2[12 * GBA_W + x] != AW_BG && bg2[148 * GBA_W + x] != AW_BG;
		if (tall) {
			++run;
			continue;
		}
		if (run > best) best = run, *x0 = x - run, *x1 = x - 1;
		run = 0;
	}
	return best >= 64;
}

/* Moves the open menu's cursor to item i (the menus wrap round). */
static void aw_pick(Emu* s, int i) {
	const AwRegion* r = aw_r(s);
	for (int tries = 0; tries < 6 && emu_read8(s, r->menuRow) != i; ++tries) aw_tap(s, K_DOWN, 1);
}

/* tryUnit: the unit intel (R) if the cursor's on a unit, yours or not
 * (*unitPanel says which opened); else the Battle Intel. */
static const char* aw_panel(Emu* s, const void* state, bool tryUnit, bool* unitPanel) {
	const AwRegion* r = aw_r(s);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int army = emu_read8(s, r->army);
	if (army < 1 || army > 4) return "not on the battle map";
	if (!aw_back(s)) return "the cursor isn't free";
	*unitPanel = tryUnit && aw_cursor_any(s);
	if (*unitPanel) {
		aw_tap(s, K_R, 1);
		if (!aw_settle(s, aw_unit_up, 90)) return "unit intel did not open";
	} else {
		/* Status: where it was last time, else the region's guess, else
		 * the other rows (the campaign's menu has Terms first; the menu
		 * keeps its last choice, so each try picks its row) */
		static int worked = -1;
		int rows[4] = { worked, r->statusItem, 0, 1 }, tried = 0;
		bool up = false;
		for (int k = 0; k < 4 && !up; ++k) {
			if (rows[k] < 0 || (tried >> rows[k] & 1)) continue;
			tried |= 1 << rows[k];
			if (k && !emu_state_load(s, state)) return "state load failed";
			if (k) {
				emu_frame(s, 0);
				if (!aw_back(s)) return "the cursor isn't free";
			}
			aw_tap(s, K_SELECT, 1);
			aw_run(s, 0, 8);
			aw_pick(s, r->intelItem); /* Intel */
			aw_tap(s, K_A, 1);
			aw_run(s, 0, 8);
			aw_pick(s, rows[k]);
			aw_tap(s, K_A, 1);
			up = aw_settle(s, aw_intel_up, 90);
			if (up) worked = rows[k];
		}
		if (!up) return "battle intel did not open";
	}
	return NULL;
}

/* The game's window background (AW-1's frame): in BG2 alone, the 8x8
 * tile repeated most inside the box at x0-x1, y0-y1, among those dithered
 * in exactly two colours (not the plain lines behind the text), at
 * whichever of the 64 alignments repeats it most (the game's tile grid).
 * False if none repeats. *ax, *ay: where a copy of it starts, mod 8;
 * in[2]: its colours. */
static bool aw_window_tile(const uint32_t* bg2, int x0, int y0, int x1, int y1, uint32_t tile[64], int* ax, int* ay, uint32_t in[2]) {
	int best = 0;
	for (int fy = 0; fy < 8; ++fy) {
		for (int fx = 0; fx < 8; ++fx) {
			int sx = x0 + ((fx - x0) % 8 + 8) % 8, sy = y0 + ((fy - y0) % 8 + 8) % 8;
			for (int ty = sy; ty + 7 <= y1; ty += 8) {
				for (int tx = sx; tx + 7 <= x1; tx += 8) {
					uint32_t c0 = bg2[ty * GBA_W + tx], c1 = c0;
					bool two = true;
					for (int i = 0; i < 64 && two; ++i) {
						uint32_t c = bg2[(ty + i / 8) * GBA_W + tx + i % 8];
						if (c == c0 || c == c1) continue;
						if (c1 == c0) c1 = c;
						else two = false;
					}
					if (!two || c1 == c0 || c0 == AW_BG || c1 == AW_BG) continue;
					int same = 0;
					for (int uy = sy; uy + 7 <= y1; uy += 8) {
						for (int ux = sx; ux + 7 <= x1; ux += 8) {
							bool eq = true;
							for (int i = 0; i < 64 && eq; ++i) eq = bg2[(uy + i / 8) * GBA_W + ux + i % 8] == bg2[(ty + i / 8) * GBA_W + tx + i % 8];
							same += eq;
						}
					}
					if (same > best) {
						best = same, *ax = fx, *ay = fy, in[0] = c0, in[1] = c1;
						for (int i = 0; i < 64; ++i) tile[i] = bg2[(ty + i / 8) * GBA_W + tx + i % 8];
					}
				}
			}
		}
	}
	return best >= 2;
}

/* How deep a window's edge is, from (x, y) inward by (dx, dy) in BG2 alone:
 * up to the first of three pixels running as its background's tile has
 * them there (tile, aligned at ax, ay); -1 if there's no such run within
 * 16 (a line behind the text). */
static int aw_edge_depth(const uint32_t* bg2, int x, int y, int dx, int dy, const uint32_t tile[64], int ax, int ay) {
	for (int k = 0; k < 16; ++k) {
		bool run = true;
		for (int j = 0; j < 3 && run; ++j) {
			int xx = x + (k + j) * dx, yy = y + (k + j) * dy;
			uint32_t c = xx >= 0 && yy >= 0 && xx < GBA_W && yy < GBA_H ? bg2[yy * GBA_W + xx] : 0;
			run = c == tile[((yy - ay) & 7) * 8 + ((xx - ax) & 7)];
		}
		if (run) return k;
	}
	return -1;
}

/* The second screen (AW-1): the unit intel of the unit under the cursor
 * (R), its panel at the right and its text box at the top left, or else
 * the Battle Intel's panel, centred; on AW_BG. */
static const char* aw_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	const AwRegion* r = aw_r(s);
	static uint32_t pan[GBA_W * GBA_H], bg2[GBA_W * GBA_H];
	bool unitPanel = false;
	/* weather (rain falling over the menus) out of every picture: the
	 * menus settle, and it isn't kept in them */
	int ct = r->cornerTile;
	const SpriteRule weather[2] = { AW_WEATHER, { -512, -256, 511, 255, ct, ct, 0, 1u << 1, 0 } };
	emu_hide_sprites(s, weather, 2);
	if (!emu_state_load(s, state)) return "state load failed";
	void* tmp = malloc(emu_state_size(s));
	if (!tmp) return "out of memory";
	const char* err = aw_panel(s, state, true, &unitPanel);
	if (!err) aw_clean(s, tmp, pan, unitPanel ? bg2 : NULL);
	int c0, c1;
	if (!err && unitPanel && !aw_unit_cols(bg2, &c0, &c1)) {
		/* R showed nothing (a unit hidden in the fog of war): the Battle Intel */
		err = aw_panel(s, state, false, &unitPanel);
		if (!err) aw_clean(s, tmp, pan, NULL);
	}
	/* with the unit intel, the minimap (AW-1): taken again only when a
	 * unit or the army whose turn it is changes (a move, a build, a unit
	 * lost), and every 8th time (a property captured, the fog); a cursor
	 * moving over the map doesn't change it. If START won't open it,
	 * the intel goes without. */
	static uint32_t mm[GBA_W * GBA_H], mmKey;
	static int mw, mh, mmAge;
	if (!err && unitPanel) {
		emu_state_load(s, state);
		uint32_t key = 2166136261u;
		for (uint32_t a = r->units; a < r->units + 4 * 0x300; a += 4) key = (key ^ emu_read32(s, a)) * 16777619u;
		key = (key ^ emu_read8(s, r->army)) * 16777619u;
		if (!mw || key != mmKey || ++mmAge >= 8) {
			mw = mh = 0;
			if (aw_minimap(s, state, tmp, mm, &mw, &mh)) mmKey = key, mmAge = 0;
			else mw = mh = 0;
		}
	}
	free(tmp);
	if (err) return err;
	for (int i = 0; i < w * h; ++i) out[i] = AW_BG;
	/* what goes: the unit intel (its panel and the text box beside it,
	 * as the game has them: what's inside BG2's boxes, not the cursor or
	 * units around them) where it is; or the Battle Intel's panel (the
	 * rows above the map), centred on what it draws */
	int rows = unitPanel ? GBA_H : r->intelRows;
	int x0 = GBA_W, x1 = -1, y0 = rows, y1 = -1;
	for (int y = 0; y < rows; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			if (pan[y * GBA_W + x] == AW_BG || (unitPanel && bg2[y * GBA_W + x] == AW_BG)) continue;
			x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
		}
	}
	if (x1 < 0) return "nothing on the panel";
	if (unitPanel) {
		/* AW-1: the panel at the right and the text box at the top left,
		 * wherever the game put them (it moves both away from the cursor),
		 * each at the game's own distance from the screen's edges */
		int px0 = GBA_W, px1 = -1, py0 = GBA_H, py1 = -1, tx0 = GBA_W, tx1 = -1, ty0 = GBA_H, ty1 = -1;
		for (int y = 0; y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				if (pan[y * GBA_W + x] == AW_BG || bg2[y * GBA_W + x] == AW_BG) continue;
				if (x >= c0 && x <= c1) {
					px0 = x < px0 ? x : px0, px1 = x > px1 ? x : px1, py0 = y < py0 ? y : py0, py1 = y > py1 ? y : py1;
				} else {
					tx0 = x < tx0 ? x : tx0, tx1 = x > tx1 ? x : tx1, ty0 = y < ty0 ? y : ty0, ty1 = y > ty1 ? y : ty1;
				}
			}
		}
		int edge = px0 < GBA_W - 1 - px1 ? px0 : GBA_W - 1 - px1;
		int pdx = (GBA_W - 1 - edge) - px1;                                  /* the panel's move */
		int tdx = tx1 < 0 ? 0 : (tx0 < c0 ? tx0 : GBA_W - 1 - tx1) - tx0;    /* the text box's */
		int tdy = tx1 < 0 ? 0 : py0 - ty0;
		int ox = (w - GBA_W) / 2, oy = (h - GBA_H) / 2;
		/* one frame across the screen (AW-1): the game's window background
		 * (its tile, in line with the text box's), a 1-pixel edge in the
		 * colour of the panel's outline; on it the text box's inside (its
		 * text and lines, not its edge) and the panel as the game draws it */
		uint32_t tile[64], in[2] = { 0, 0 };
		int ax = 0, ay = 0, tin[4] = { 0, 0, 0, 0 }; /* the text box's edge: left, top, right, bottom */
		int pin[4] = { 0, 0, 0, 0 };                 /* the panel's */
		bool framed = tx1 >= 0 && aw_window_tile(bg2, tx0, ty0, tx1, ty1, tile, &ax, &ay, in);
		if (framed) {
			/* (each side's edge: the shallowest along all of it but its
			 * corners, as the lines behind the text run into most) */
			for (int k = 0; k < 4; ++k) tin[k] = 16;
			for (int y = ty0 + 3; y <= ty1 - 3; ++y) {
				int l = aw_edge_depth(bg2, tx0, y, 1, 0, tile, ax, ay), r = aw_edge_depth(bg2, tx1, y, -1, 0, tile, ax, ay);
				tin[0] = l >= 0 && l < tin[0] ? l : tin[0], tin[2] = r >= 0 && r < tin[2] ? r : tin[2];
			}
			for (int x = tx0 + 3; x <= tx1 - 3; ++x) {
				int t = aw_edge_depth(bg2, x, ty0, 0, 1, tile, ax, ay), b = aw_edge_depth(bg2, x, ty1, 0, -1, tile, ax, ay);
				tin[1] = t >= 0 && t < tin[1] ? t : tin[1], tin[3] = b >= 0 && b < tin[3] ? b : tin[3];
			}
			/* the panel's edge the same way: its inside goes in the frame
			 * too (one window across the screen) */
			for (int k = 0; k < 4; ++k) pin[k] = 16;
			for (int y = py0 + 3; y <= py1 - 3; ++y) {
				int l = aw_edge_depth(bg2, px0, y, 1, 0, tile, ax, ay), r = aw_edge_depth(bg2, px1, y, -1, 0, tile, ax, ay);
				pin[0] = l >= 0 && l < pin[0] ? l : pin[0], pin[2] = r >= 0 && r < pin[2] ? r : pin[2];
			}
			for (int x = px0 + 3; x <= px1 - 3; ++x) {
				int t = aw_edge_depth(bg2, x, py0, 0, 1, tile, ax, ay), b = aw_edge_depth(bg2, x, py1, 0, -1, tile, ax, ay);
				pin[1] = t >= 0 && t < pin[1] ? t : pin[1], pin[3] = b >= 0 && b < pin[3] ? b : pin[3];
			}
			for (int k = 0; k < 4; ++k) pin[k] = pin[k] == 16 ? 0 : pin[k];
			/* the edge's colour: the panel's outline, outside in, the first
			 * colour that's neither its background's nor near black (Advance
			 * Wars 2's red or blue); else its outermost (Advance Wars' dark edge) */
			/* (the commonest such colour down the panel's left edge, its
			 * depth measured above) */
			uint32_t line = pan[((py0 + py1) / 2) * GBA_W + px0], seen[8];
			int counts[8], ns = 0, bestN = 0;
			for (int y = py0 + 8; y <= py1 - 8; ++y) {
				for (int x = px0; x < px0 + pin[0] && x <= px1; ++x) {
					uint32_t c = pan[y * GBA_W + x]; /* (Advance Wars' dark edge isn't BG2's) */
					if (c == in[0] || c == in[1] || (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF) < 120) continue;
					int k = 0;
					while (k < ns && seen[k] != c) ++k;
					if (k == ns) {
						if (ns == 8) continue;
						seen[ns] = c, counts[ns++] = 0;
					}
					if (++counts[k] > bestN) bestN = counts[k], line = c;
				}
			}
			int fx = ((ax + tdx) % 8 + 8) % 8, fy = ((ay + tdy) % 8 + 8) % 8;
			for (int y = 0; y < GBA_H; ++y) {
				for (int x = 0; x < GBA_W; ++x) {
					bool rim = x == 0 || y == 0 || x == GBA_W - 1 || y == GBA_H - 1;
					uint32_t c = rim ? line : tile[((y - fy) % 8 + 8) % 8 * 8 + ((x - fx) % 8 + 8) % 8];
					int nx = x + ox, ny = y + oy;
					if (nx >= 0 && ny >= 0 && nx < w && ny < h) out[ny * w + nx] = c;
				}
			}
		}
		for (int y = 0; y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t c = pan[y * GBA_W + x];
				if (c == AW_BG || bg2[y * GBA_W + x] == AW_BG) continue;
				bool inPanel = x >= c0 && x <= c1;
				if (framed && !inPanel && (x < tx0 + tin[0] || x > tx1 - tin[2] || y < ty0 + tin[1] || y > ty1 - tin[3])) continue;
				if (framed && inPanel && (x < px0 + pin[0] || x > px1 - pin[2] || y < py0 + pin[1] || y > py1 - pin[3])) continue;
				int nx = x + (inPanel ? pdx : tdx) + ox, ny = y + (inPanel ? 0 : tdy) + oy;
				if (nx >= 0 && ny >= 0 && nx < w && ny < h) out[ny * w + nx] = c;
			}
		}
		/* the minimap at the bottom left: in line with the text box's left
		 * edge and the panel's bottom */
		int mx = (tx1 < 0 ? edge : tx0 + tdx) + ox, my = py1 - mh + 1 + oy;
		if (framed) mx = 2 + ox, my = GBA_H - 2 - mh + oy; /* (a pixel in from the frame's edge) */
		for (int y = 0; y < mh; ++y) {
			for (int x = 0; x < mw; ++x) {
				int nx = mx + x, ny = my + y;
				if (nx >= 0 && ny >= 0 && nx < w && ny < h) out[ny * w + nx] = mm[y * mw + x];
			}
		}
		return NULL;
	}
	int dx = unitPanel ? (w - GBA_W) / 2 : (w - (x1 - x0 + 1)) / 2 - x0;
	int dy = unitPanel ? (h - GBA_H) / 2 : (h - (y1 - y0 + 1)) / 2 - y0;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			uint32_t c = pan[y * GBA_W + x];
			int nx = x + dx, ny = y + dy;
			if (c == AW_BG || (unitPanel && bg2[y * GBA_W + x] == AW_BG)) continue;
			if (nx >= 0 && ny >= 0 && nx < w && ny < h) out[ny * w + nx] = c;
		}
	}
	return NULL;
}

static const char* gbw_pick(Emu* e) {
	int g = gbw_game(e);
	return g == 1 ? "BGW1" : g == 2 ? "BGW2" : NULL;
}

/* Super Mario Advance 4 (USA): a revision after the first (the ROM
 * header's version byte) keeps its RAM as Europe and Japan do: AX4R */
static const char* sma4_pick(Emu* e) {
	return emu_read8(e, 0x080000BC) ? "AX4R" : NULL;
}

/* -------------------------------------------------- Summon Night: Swordcraft Story
 *
 * The second screen is the Status screen (START > Status), the whole
 * screen, with the menu's Place bar (where you are) over its bottom rows:
 * the box with the name across the whole width, its PLACE heading only as
 * far as the stats' lines start (clear of the item row under the stats).
 * Both are brought up on the shadow core: START (from walking about), DOWN
 * to Status, A; the Place bar is taken from the menu on the way. With
 * several characters (SN-5), R turns to each in turn: one page each, shown
 * 5 seconds. Every half second (SN-6, profile interval), and after a fade
 * to black (a new area). Where the menu won't open (a scene, a fight), the
 * last picture stays, and the next try isn't held back. (The Japanese
 * release, Craft Sword Monogatari, has the same addresses.) */
#define SN_MENU 0x03000120     /* 0 in the START menu, 4 on its Status screen */
#define SN_MENU_ROW 0x03000122 /* the START menu's cursor: Status is 5 */
#define SN_MENU_DISP 0x3340    /* DISPCNT in the menu and its screens */
#define SN_PLACE_Y 136         /* the Place bar: the menu's rows 136-159 */
#define SN_PLACE_HEAD 144      /* its heading (PLACE) above this row, the box with the name below */
#define SN_STATS_X 80          /* where the Status's lines between ATK, DEF... start */
#define SN_CAP_X0 233          /* the heading's end piece, columns 233-238 */
#define SN_CAP_X1 238





#define SN_V_TITLE 0xF0000020  /* 1 on the title screen (BG1-3 set up as there) or in a battle: the logo */
#define SN_SCENE 0x02000400    /* 1 in a battle (walking about 2-4, by area; 0 on the title) */

static uint32_t sn_virt(Emu* e, uint32_t addr) {
	if (addr != SN_V_TITLE) return 0;
	if (emu_read16(e, 0x0400000A) == 0x1F0B && emu_read16(e, 0x0400000C) == 0x0F06) return 1;
	/* a battle (SN-7): the scene byte and display mode 1 */
	return emu_read8(e, SN_SCENE) == 1 && (emu_read16(e, 0x04000000) & 7) == 1;
}

static bool sn_in(Emu* s, int menu) {
	return emu_read16(s, 0x04000000) == SN_MENU_DISP && emu_read8(s, SN_MENU) == menu;
}

static bool sn_wait(Emu* s, int menu, int frames) {
	for (int f = 0; f < frames && !sn_in(s, menu); ++f) emu_frame(s, 0);
	return sn_in(s, menu);
}

#define SN_PAGES 6 /* characters on the Status at most (the hero and the guardian beasts) */

/* the Status page in fb, with the menu's Place bar over its bottom (SN-2) */
static void sn_page(const uint32_t* status, const uint32_t* place, uint32_t* out, int w, int h) {
	int cw = w < GBA_W ? w : GBA_W;
	for (int y = 0; y < h && y < GBA_H; ++y) {
		uint32_t* o = &out[y * w];
		memcpy(o, &status[y * GBA_W], (size_t) cw * 4);
		if (y < SN_PLACE_Y) continue;
		const uint32_t* pl = &place[(y - SN_PLACE_Y) * GBA_W];
		if (y >= SN_PLACE_HEAD) {
			memcpy(o, pl, (size_t) cw * 4); /* the box with the name: the full width */
			continue;
		}
		/* its heading only as far as the stats' lines start, with the
		 * heading's end piece (columns 233-238) moved there */
		int cap = SN_CAP_X1 - SN_CAP_X0 + 1, x0 = SN_STATS_X - cap;
		memcpy(o, pl, (size_t) x0 * 4);
		memcpy(o + x0, pl + SN_CAP_X0, (size_t) cap * 4);
	}
}

/* most of the picture dark (the pause between two characters' pages) */
static bool sn_dark(const uint32_t* fb) {
	int dark = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		if ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF) < 96) ++dark;
	}
	return dark * 7 > GBA_W * GBA_H * 3 / 4;
}

/* runs frames until the picture has held still for 3, not dark (the next
 * character's page has slid in: the last one slides out, the screen stays
 * dark ~18 frames, the next slides in, ~47 frames in all); false if it
 * hasn't within max */
static bool sn_settle(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max; ++f) {
		emu_frame(s, 0);
		still = f && !memcmp(prev, s->fb, sizeof(prev)) && !sn_dark(s->fb) ? still + 1 : 0;
		if (still >= 3) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}

/* The Status with the Place bar; with several characters (SN-5), one page
 * each, R turning to the next, until the first comes round again. Where the
 * menu won't open (a scene, a fade), "map button did nothing": a quick
 * failure that doesn't hold the next capture back (SN-6). */
static const char* sn_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t place[GBA_W * (GBA_H - SN_PLACE_Y)];
	static uint32_t first[GBA_W * GBA_H];
	int max = *nOut < SN_PAGES ? *nOut : SN_PAGES;
	*nOut = 1;
	*step = 300; /* 5 seconds a character */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (sn_in(s, 4)) {
		/* the Status open: back to the menu for the Place bar first */
		aw_tap(s, K_B, 1);
		if (!sn_wait(s, 0, 90)) return "the menu didn't come back";
	} else if (!sn_in(s, 0)) {
		aw_tap(s, K_START, 1);
		if (!sn_wait(s, 0, 60)) return "map button did nothing";
	}
	aw_run(s, 0, 20); /* (past its fade-in) */
	memcpy(place, &s->fb[SN_PLACE_Y * GBA_W], sizeof(place));
	for (int i = 0; i < 10 && emu_read8(s, SN_MENU_ROW) != 5; ++i) aw_tap(s, K_DOWN, 1);
	if (emu_read8(s, SN_MENU_ROW) != 5) return "Status not reached in the menu";
	aw_tap(s, K_A, 1);
	if (!sn_wait(s, 4, 90)) return "the Status didn't open";
	aw_run(s, 0, 40); /* (past its fade-in) */
	memcpy(first, s->fb, sizeof(first));
	sn_page(first, place, out[0], w, h);
	/* the next characters: R, until the first is back (one character: R
	 * changes nothing) */
	static uint32_t last[GBA_W * GBA_H];
	memcpy(last, first, sizeof(last));
	int n = 1;
	for (int tries = 0; n < max && tries < 3;) {
		aw_tap(s, K_R, 1);
		aw_run(s, 0, 30); /* (the page sliding out, the dark between: not drawn) */
		if (!sn_settle(s, 90)) break;
		if (!memcmp(first, s->fb, sizeof(first))) break; /* round to the first again */
		if (!memcmp(last, s->fb, sizeof(last))) {
			/* (R not taken yet, just after a page slid in: again; one
			 * character, R never changes anything) */
			++tries;
			continue;
		}
		memcpy(last, s->fb, sizeof(last));
		sn_page(s->fb, place, out[n], w, h);
		++n, tries = 0;
	}
	*nOut = n;
	return NULL;
}

/* ----------------------------------------- Summon Night: Swordcraft Story 2
 *
 * The Status screen (START, UP twice past the page arrow to Status, A),
 * whole, as the game draws it; with more than one character (SN2-5), R
 * wipes to the next, one page each, 5 seconds apart, until R changes
 * nothing (the last: it has only the L icon). No Place bar (no clean place
 * to take it from). Where the menu won't open, the quick failure, as for
 * Swordcraft Story (USA and Japan alike). */
#define SN2_DISP 0x3F40      /* DISPCNT in the menu and the Status */
#define SN2_SCREEN 0x03000002 /* 0x0434 on the Status, 0x03AC in the menu */

static bool sn2_status(Emu* s) {
	return emu_read16(s, 0x04000000) == SN2_DISP && emu_read16(s, SN2_SCREEN) == 0x0434;
}

/* The L and R icons in the Status's bottom corners (several characters:
 * R on the first, L on the last, both between), covered: an icon pixel
 * becomes the clean page's (the last's corner has no R, the first's no L)
 * where that's the panel's frame, else the nearest pixel further in on its
 * own row (the portrait or the panel behind). Boxes: L x 0-12, R x 226-238,
 * rows 148-159 (SN2-5). */
#define SN2_ICON_Y0 148
static bool sn2_icon_px(uint32_t c) {
	c &= 0xFFFFFF;
	return c == 0x525252 || c == 0xFFFFFF || c == 0xC6C6C6 || c == 0xA5A5A5;
}

static bool sn2_frame_px(uint32_t c) {
	/* (the frame's browns, in the framebuffer's R, G, B byte order) */
	static const uint32_t frame[] = { 0x4284AD, 0x428CAD, 0x398CAD, 0x3984AD, 0x396B84, 0x315A6B, 0x31526B, 0x73ADCE };
	c &= 0xFFFFFF;
	for (size_t i = 0; i < sizeof(frame) / sizeof(*frame); ++i) {
		if (c == frame[i]) return true;
	}
	return false;
}

static void sn2_cover(uint32_t* page, int w, const uint32_t* clean, int x0, int x1, int dir) {
	for (int y = SN2_ICON_Y0; y < GBA_H; ++y) {
		for (int x = x0; x <= x1; ++x) {
			uint32_t* p = &page[y * w + x];
			if (!sn2_icon_px(*p)) continue;
			uint32_t c = clean[y * w + x];
			if (!sn2_frame_px(c)) {
				int i = x;
				while (i >= x0 && i <= x1 && sn2_icon_px(page[y * w + i])) i += dir;
				if (i >= 0 && i < GBA_W) c = page[y * w + i];
			}
			*p = c;
		}
	}
}

static const char* sn2_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t last[GBA_W * GBA_H];
	int max = *nOut < SN_PAGES ? *nOut : SN_PAGES;
	*nOut = 1;
	*step = 300; /* 5 seconds a character */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (emu_read16(s, 0x04000000) != 0x1F40) return "map button did nothing"; /* (not walking about) */
	aw_tap(s, K_START, 1);
	int f = 0;
	while (f < 60 && emu_read16(s, 0x04000000) != SN2_DISP) emu_frame(s, 0), ++f;
	if (f >= 60) return "map button did nothing";
	aw_run(s, 0, 36); /* (the menu takes input ~30 frames after START) */
	aw_tap(s, K_UP, 2);
	aw_tap(s, K_A, 1);
	for (f = 0; f < 60 && !sn2_status(s); ++f) emu_frame(s, 0);
	if (!sn2_status(s)) return "the Status didn't open";
	if (!sn_settle(s, 120)) return "the Status didn't settle";
	int cw = w < GBA_W ? w : GBA_W;
	memcpy(last, s->fb, sizeof(last));
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[0][y * w], &last[y * GBA_W], (size_t) cw * 4);
	int n = 1;
	for (int tries = 0; n < max && tries < 3;) {
		aw_tap(s, K_R, 1);
		aw_run(s, 0, 10);
		if (!sn_settle(s, 120)) break;
		if (!memcmp(last, s->fb, sizeof(last))) {
			/* (R not taken yet, or the last character) */
			++tries;
			continue;
		}
		memcpy(last, s->fb, sizeof(last));
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[n][y * w], &last[y * GBA_W], (size_t) cw * 4);
		++n, tries = 0;
	}
	*nOut = n;
	if (n > 1 && w >= GBA_W && h >= GBA_H) {
		/* the icons covered on every page, from copies of the first and the
		 * last as taken */
		static uint32_t firstC[GBA_W * GBA_H], lastC[GBA_W * GBA_H];
		memcpy(firstC, out[0], sizeof(firstC));
		memcpy(lastC, out[n - 1], sizeof(lastC));
		for (int i = 0; i < n; ++i) {
			if (i > 0) sn2_cover(out[i], w, firstC, 0, 12, 1);         /* (L: all but the first) */
			if (i < n - 1) sn2_cover(out[i], w, lastC, 226, 238, -1);  /* (R: all but the last) */
		}
	}
	return NULL;
}

/* ------------------------------------------ Final Fantasy Tactics Advance
 *
 * In battles the second screen shows, with the cursor on a unit, that
 * unit's Status screen (the whole screen, as the game draws it), turning
 * between its Equip and Abilities pages every 5 seconds; with the cursor
 * on no unit (or one whose Status won't open), the turn order (L's list)
 * centred, with the Laws (SYSTEM > Laws) above it. All are brought up on
 * the shadow core: B to back out of whatever's open, then for the Status A
 * (twice if the first only puts the cursor on the unit), DOWN to its
 * menu's Status (the last item, highlighted orange), A; RIGHT for the
 * other page. Before the first picture, and outside battles, the logo
 * (hud.logoUnder, logoAlways). They're taken only with the map (or a
 * Status) showing: while a unit moves or acts, on the enemy's turn and in
 * scenes the last ones stay. (The earlier versions: the game's menus and
 * dialogue moved to the second screen with a ROM patch tagging its UI
 * sprites, on the branch archive/ffta-hud; the Status, Mission, Laws and
 * turn order in the corners of a bigger picture, until v1.3.2's later test
 * builds, in git history.) */
#define FT_MAP_CNT 0x4C03    /* BG0CNT on the battle map */
#define FT_STATUS_CNT 0x8E0B /* BG0CNT on a unit's Status screen */
#define FT_CURSOR 0x0200F6A4 /* the map cursor's x, y (Japan: 0x30 lower) */
#define FT_AT_UNIT 0x0200F6A0 /* the unit under the cursor (its data), 0 if none, while
                               * nothing's open on the map (Japan: 0x30 lower) */
#define FT_ACTIVE 0x0200F4EC  /* the unit whose turn it is (its data; Japan: 0x30 lower) */
#define FT_UI 0x0200F5C6     /* 1 with nothing open on the map (0 the active unit's menu,
                              * 2 another unit's, the system menu or a Status, 3 the turn
                              * order, 5 the active unit's menu once it's moved...; Japan:
                              * 0x30 lower) */
#define FT_IN_BATTLE 0x03000E4E /* 1 in a battle (its scenes, moves, menus too), else 0 */
#define FT_SCENE 0x0201639C /* 0 while a battle's fought; set while a scene plays on the
                             * battle map (the camera turning to the last enemy KO'd,
                             * the talk once it's won, the snowball fight's end), so
                             * the battle's over for the second screen (Japan's
                             * FT_SCENE_: 0x30 lower). (0x03000E08 stays set
                             * from the first battle won on; 0x0202F254 is set at the
                             * snowball fight's end only.) */
/* The world map (FT-14; Japan's addresses: the FT_*_ lookups) */
#define FT_MODE_TOWN 60      /* FT_MODE in a town's Pub, Shop and Prison (early on and
                                * by Kingmoon alike) */
#define FT_WM_BG01 0x8C028A03u /* BG0CNT, BG1CNT on the world map (its Laws, notices and
                                * town menus too; Japan the same) */
#define FT_WM_BG23 0x2F082E09u /* BG2CNT, BG3CNT there */
#define FT_MODE 0x02002191     /* not the screen: it follows the story (0x02001F6C, 0 on
                                * the title, 1, 2, 3 by Kingmoon): that in a battle, one
                                * more on the world map (3 early on, 4 by Kingmoon);
                                * 1 or 2 in a battle, 60 in a town's Pub... */
#define FT_WM_STATE 0x0200FD2B /* on the world map: 255 free, 1 the Laws (L) open, 2 a
                                * town's menu, 0 a notice */
#define FT_LOC 0x02001F69      /* the location the clan is at (18 Cyril, 19 Sprohm...), from
                                * when it gets there */
#define FT_WM_STILL 0x02002C29 /* 0 while the clan travels and in a place's menu, else 24
                               * (climbing back over ~15 frames as a menu closes) */
#define FT_WM_CURSOR 0x02002C2A /* the location under the world map's cursor, 0 between
                                 * them. The Laws window shows the laws on the day the
                                 * clan would get there (lettered on blue and red); on the
                                 * clan's own place or between places, today's (orange) */
/* (Japan's: its world map's found by what they do, its scene's 0x30 lower) */
#define FT_JP(e, usa, jpn) ((e)->gameCode[3] == 'J' ? (uint32_t) (jpn) : (uint32_t) (usa))
#define FT_MODE_(e) FT_JP(e, FT_MODE, 0x02002221)
#define FT_WM_STATE_(e) FT_JP(e, FT_WM_STATE, 0x0200FB63)
#define FT_LOC_(e) FT_JP(e, FT_LOC, 0x02001F59)
#define FT_WM_CURSOR_(e) FT_JP(e, FT_WM_CURSOR, 0x02002C3A)
#define FT_WM_STILL_(e) FT_JP(e, FT_WM_STILL, 0x02002C39)
#define FT_SCENE_(e) FT_JP(e, FT_SCENE, 0x0201636C)
#define FT_V_BATTLE 0xF0000010 /* 1 in a battle, outside its scenes (FT_SCENE) */
#define FT_V_IDLE 0xF0000011   /* 1 on a battle's map or a unit's Status (not a move or an
                                * action), or on the world map with nothing or the Laws open */
#define FT_V_SHOWN 0xF0000012  /* 1 in a battle (FT_V_BATTLE) or on the world map */
#define FT_V_TURN 0xF0000015   /* in a battle, the active unit's data (its low 16 bits),
                                * + 0x10000 if it isn't yours (FT_V_THEIRS), its top
                                * bits from the other units' turn counters
                                * (ft_turn_sig); 0 for a few frames between two turns:
                                * each turn's start a change, one unit's second in a
                                * row too (FT-13) */
#define FT_V_THEIRS 0xF0000014 /* 1 in a battle when it isn't your turn: the active unit
                               * an enemy, the judge or a guest (its data's flags, +0x29 of
                               * the unit FT_ACTIVE's record points to: 0x80 an enemy, 0x10
                               * the judge, 0x20 a guest; 0 a clan member) */
#define FT_V_LOC 0xF0000013    /* on the world map, the clan's location + 1, FT_WM_STATE
                                * << 8 and 1 << 16 with the cursor on it; else 0 */

static bool ft_world(Emu* e) {
	/* (the world map's own screen: its four BG layers set up so, with its
	 * Laws, a notice or a town's menu open too, in Japan as well; FT_MODE
	 * isn't the world map's: it climbs with the story, 3 there early on,
	 * 4 by Kingmoon) */
	return emu_read8(e, FT_IN_BATTLE) == 0 && emu_read32(e, 0x04000008) == FT_WM_BG01 &&
	       emu_read32(e, 0x0400000C) == FT_WM_BG23;
}

/* A unit's second turn in a row (FT-13): the other units' turn counters
 * (CT, 16 bits at +0xD0 of each unit's data; whoever reaches 1000 gets the
 * turn), hashed. They stay put all through a turn and all move on in the
 * gap between two turns, the same unit's next one too, which reads the
 * same active record and its CT 1000 again. The units are a table of
 * 0x90-byte records, each starting with a pointer to its unit's data
 * (0x020223AC in the USA's battles, Japan's 0x30 lower), the active one
 * (FT_ACTIVE) among them; the words either side of the table aren't EWRAM
 * pointers, so it's walked out from the active record. With no other
 * unit's counter moving (all stopped or KO'd), a second turn still goes
 * unnoticed. (The game keeps no count of turns and no previous unit; the
 * gap alone was missed: game.c's back-off after the facing prompt's
 * failed captures covers it.) */
static uint32_t ft_turn_sig(Emu* e, uint32_t rec) {
	uint32_t h = 2166136261u, lo = rec, hi = rec;
	while (emu_read32(e, lo - 0x90) >> 24 == 0x02 && rec - lo < 0x90 * 16) lo -= 0x90;
	while (emu_read32(e, hi + 0x90) >> 24 == 0x02 && hi - rec < 0x90 * 16) hi += 0x90;
	for (uint32_t r = lo; r <= hi; r += 0x90) {
		uint32_t ch = emu_read32(e, r);
		if (r == rec || ch >> 24 != 0x02) continue;
		h = (h ^ emu_read16(e, ch + 0xD0)) * 16777619u;
	}
	return h;
}

static uint32_t ft_virt(Emu* e, uint32_t addr) {
	/* (a scene on the battle map is out of it: the logo; above all the
	 * one once the battle's won) */
	/* (the scene's word is a pointer into EWRAM while one plays; else 0,
	 * or in some battles a stray byte: Japan's read 0x80 in one) */
	bool in = emu_read8(e, FT_IN_BATTLE) == 1 && (emu_read32(e, FT_SCENE_(e)) >> 24) != 0x02 &&
	          emu_read8(e, FT_MODE_(e)) != FT_MODE_TOWN; /* (a town's Pub, Shop or Prison reads in a battle too) */
	if (addr == FT_V_BATTLE) return in;
	if (addr == FT_V_TURN) {
		if (!in) return 0;
		uint32_t rec = emu_read32(e, FT_ACTIVE - (e->gameCode[3] == 'J' ? 0x30 : 0));
		if (rec >> 24 != 0x02) return 0;
		/* (the other units' counters in its top bits: the key stays changed
		 * after a unit's second turn has begun, not only for the gap) */
		return ((rec & 0xFFFF) | ft_virt(e, FT_V_THEIRS) << 16) ^ (ft_turn_sig(e, rec) & 0xFFFE0000u);
	}
	if (addr == FT_V_THEIRS) {
		uint32_t rec = emu_read32(e, FT_ACTIVE - (e->gameCode[3] == 'J' ? 0x30 : 0));
		uint32_t ch = rec >> 24 == 0x02 ? emu_read32(e, rec) : 0;
		return in && ch >> 24 == 0x02 && (emu_read8(e, ch + 0x29) & 0xF0) != 0;
	}
	if (addr == FT_V_SHOWN) return in || ft_world(e);
	if (addr == FT_V_LOC) {
		if (!ft_world(e)) return 0;
		uint8_t at = emu_read8(e, FT_LOC_(e)), cur = emu_read8(e, FT_WM_CURSOR_(e));
		return (at + 1u) | (uint32_t) emu_read8(e, FT_WM_STATE_(e)) << 8 | (cur == at || !cur) << 16;
	}
	if (addr != FT_V_IDLE) return 0;
	if (ft_world(e)) {
		/* (a place's menu, a town's or a mission's: the clan's there; it
		 * reads FT_WM_STILL 0 too) */
		uint8_t w = emu_read8(e, FT_WM_STATE_(e));
		return ((w == 255 || w == 1) && emu_read8(e, FT_WM_STILL_(e))) || w == 2;
	}
	uint16_t c = emu_read16(e, 0x04000008);
	return in && (c == FT_MAP_CNT || c == FT_STATUS_CNT);
}

static uint32_t ft_cursor(Emu* e) {
	return FT_CURSOR - (e->gameCode[3] == 'J' ? 0x30 : 0);
}

/* the bottom row of a unit's menu (always Status) has orange lettering in
 * it when it's selected (white when not); counts of each */
static void ft_menu_row(Emu* s, int* orange, int* white) {
	*orange = *white = 0;
	for (int y = 128; y < 144; ++y) {
		for (int x = 180; x < 236; ++x) {
			uint32_t c = s->fb[y * GBA_W + x];
			int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
			if (r > 200 && g > 60 && g < 170 && b < 90) ++*orange;
			else if (r > 220 && g > 220 && b > 220) ++*white;
		}
	}
}

/* B until nothing's open on the map (out of the Status, a menu, a move,
 * the list), and no more: B with nothing open hides the unit's panel and
 * then changes the view. False if it never gets there. */
static bool ft_back(Emu* s) {
	uint32_t ui = FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0);
	for (int i = 0; i < 8; ++i) {
		aw_run(s, 0, 12);
		if (emu_read8(s, ui) == 1 && emu_read16(s, 0x04000008) == FT_MAP_CNT) return true;
		aw_tap(s, K_B, 1);
	}
	return false;
}

static bool ft_bg0(Emu* s, uint16_t cnt) {
	return emu_read16(s, 0x04000008) == cnt;
}

/* FT-9's background: the Status screen's angled checkerboard (BG0 alone),
 * from the first Status the hidden copy opens; the same in every battle. */
static uint32_t ftChecker[GBA_W * GBA_H];
static bool ftCheckerOk;
static void ft_take_checker(Emu* s) {
	if (ftCheckerOk) return;
	void* t = malloc(emu_state_size(s));
	if (!t) return;
	if (emu_state_save(s, t)) {
		emu_layers(s, 0x01);
		emu_state_load(s, t);
		emu_frame(s, 0);
		memcpy(ftChecker, s->fb, sizeof(ftChecker));
		int other = 0;
		for (int i = 1; i < GBA_W * GBA_H; ++i) other += ftChecker[i] != ftChecker[0];
		ftCheckerOk = other > GBA_W * GBA_H / 8; /* (a pattern, not a blank) */
		emu_layers(s, 0x1F);
		emu_state_load(s, t);
	}
	free(t);
}

/* The Status screen's two pages into page[0], page[1], of the unit under
 * the cursor (or the one open already); false if there's none there or
 * it didn't open. */
static bool ft_status(Emu* s, const void* state, uint32_t page[2][GBA_W * GBA_H]) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_STATUS_CNT)) {
		uint32_t jp = s->gameCode[3] == 'J' ? 0x30 : 0, ui = FT_UI - jp;
		/* the active unit's menu up once it's moved (5): its Status from
		 * there (B on it frees the cursor only for a moment, with the unit
		 * under it not yet known) */
		bool other = false;
		if (!(ft_bg0(s, FT_MAP_CNT) && emu_read8(s, ui) == 5)) {
			if (!ft_bg0(s, FT_MAP_CNT) || !ft_back(s)) return false;
			/* no unit under the cursor: no Status (FT-9: the turn order) */
			uint32_t at = emu_read32(s, FT_AT_UNIT - jp);
			if (!at) return false;
			/* another unit than the one whose turn it is: A brings up its own
			 * menu (2); the judge has none, and A on it brings up the active
			 * unit's (0) instead: no Status for the judge (FT-9) */
			other = at != emu_read32(s, FT_ACTIVE - jp);
		}
		bool sel = false;
		int moves = 0;
		for (int i = 0; i < 14 && !sel; ++i) {
			aw_run(s, 0, 8);
			int orange, white;
			ft_menu_row(s, &orange, &white);
			uint8_t u = emu_read8(s, ui);
			bool menu = u == 0 || u == 2 || u == 5; /* (the active unit's, another unit's, the active one's once it's moved) */
			if (other && u == 0) return false; /* (the judge: the active unit's menu came up) */
			sel = menu && orange > 40;
			if (sel) break;
			/* to Status, the last item: UP once (the menu wraps round),
			 * else DOWN; A while there's no menu yet */
			if (menu) aw_tap(s, moves++ ? K_DOWN : K_UP, 1);
			else aw_tap(s, K_A, 1);
		}
		if (!sel) return false;
		aw_tap(s, K_A, 1);
		int f = 0;
		while (f < 120 && !ft_bg0(s, FT_STATUS_CNT)) emu_frame(s, 0), ++f;
		if (!ft_bg0(s, FT_STATUS_CNT)) return false;
	}
	aw_run(s, 0, 40); /* (past its fade-in) */
	memcpy(page[0], s->fb, sizeof(page[0]));
	ft_take_checker(s);
	aw_tap(s, K_RIGHT, 1);
	aw_run(s, 0, 30);
	memcpy(page[1], s->fb, sizeof(page[1]));
	return ft_bg0(s, FT_STATUS_CNT);
}

/* A window the game draws on BG2 (lettering, frame) over BG3 (its box)
 * cut out of the frame showing now, without the map: the box around
 * BG2's and BG3's pixels, w x h within the limits given, FT_HOLE where
 * they have nothing (its corners); false if not. */
#define FT_HOLE 0x00000001u
/* (bg3 given: out gets BG2 alone and bg3 BG3 alone, over the same box) */
static bool ft_cut(Emu* s, void* tmp, uint32_t* out, uint32_t* bg3, int* ow, int* oh, int minW, int maxW, int minH, int maxH) {
	if (!emu_state_save(s, tmp)) return false;
	static uint32_t none[GBA_W * GBA_H], only3[GBA_W * GBA_H];
	emu_layers(s, 0);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	memcpy(none, s->fb, sizeof(none));
	if (bg3) {
		emu_layers(s, 0x08);
		emu_state_load(s, tmp);
		emu_frame(s, 0);
		memcpy(only3, s->fb, sizeof(only3));
	}
	emu_layers(s, bg3 ? 0x04 : 0x0C);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1;
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			int i = y * GBA_W + x;
			if (s->fb[i] == none[i] && (!bg3 || only3[i] == none[i])) continue;
			x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
		}
	}
	int w = x1 - x0 + 1, h = y1 - y0 + 1;
	bool ok = x1 >= 0 && w >= minW && w <= maxW && h >= minH && h <= maxH;
	if (ok) {
		*ow = w, *oh = h;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int i = (y0 + y) * GBA_W + x0 + x;
				uint32_t c = s->fb[i];
				out[y * w + x] = c == none[i] ? FT_HOLE : c | 0xFF000000;
				if (bg3) bg3[y * w + x] = only3[i] == none[i] ? FT_HOLE : only3[i] | 0xFF000000;
			}
		}
	}
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp);
	return ok;
}

/* The turn order list (L) cut out; false if it didn't open. */
static uint16_t ftListBlend; /* BLDALPHA with the list up (its box over the battle) */
static bool ft_list(Emu* s, const void* state, void* tmp, uint32_t* out, uint32_t* bg3, int* ow, int* oh) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_MAP_CNT) && !ft_bg0(s, FT_STATUS_CNT)) return false;
	/* (the active unit's menu once it's moved (5): B on it frees the
	 * cursor only for a moment, L then brings the menu back (0); B there,
	 * and L again. Only from that menu: elsewhere, as it comes) */
	uint32_t ui = FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0);
	bool moved = emu_read8(s, ui) == 5;
	if (!ft_back(s)) return false;
	for (int i = 0; i < 3; ++i) {
		aw_tap(s, K_L, 1);
		aw_run(s, 0, 40);
		if (!moved || emu_read8(s, ui) != 0) break;
		aw_tap(s, K_B, 1);
		aw_run(s, 0, 12);
	}
	ftListBlend = emu_read16(s, 0x04000052);
	return ft_cut(s, tmp, out, bg3, ow, oh, 100, 180, 100, GBA_H);
}

/* SYSTEM (Start on the map) > row (1 Mission, 2 Laws) and its window cut
 * out; false if it didn't open. The menu's rows are 16 lines apart from
 * line 48, the selected one lettered in orange. */
static bool ft_system(Emu* s, const void* state, void* tmp, int row, uint32_t* out, uint32_t* bg3, int* ow, int* oh) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_MAP_CNT) && !ft_bg0(s, FT_STATUS_CNT)) return false;
	/* (as in ft_list: the moved unit's menu coming back, B, START again) */
	uint32_t ui = FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0);
	bool moved = emu_read8(s, ui) == 5;
	if (!ft_back(s)) return false;
	for (int i = 0; i < 3; ++i) {
		aw_tap(s, K_START, 1);
		if (!moved) break;
		aw_run(s, 0, 16);
		if (emu_read8(s, ui) != 0) break;
		aw_tap(s, K_B, 1);
		aw_run(s, 0, 12);
	}
	bool sel = false;
	for (int i = 0; i < 8 && !sel; ++i) {
		aw_run(s, 0, 16);
		int orange = 0;
		for (int y = 48 + row * 16; y < 64 + row * 16; ++y) {
			for (int x = 170; x < 236; ++x) {
				uint32_t c = s->fb[y * GBA_W + x];
				int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
				if (r > 200 && g > 60 && g < 170 && b < 90) ++orange;
			}
		}
		sel = orange > 40;
		if (!sel) aw_tap(s, K_DOWN, 1);
	}
	if (!sel) return false;
	aw_tap(s, K_A, 1);
	aw_run(s, 0, 40);
	return row == 1 ? ft_cut(s, tmp, out, bg3, ow, oh, 150, GBA_W, 100, GBA_H)
	                : ft_cut(s, tmp, out, bg3, ow, oh, 90, GBA_W, 24, 90); /* (Japan's Laws: about 104 wide) */
}

static void ft_put(uint32_t* o, int w, int h, const uint32_t* src, int sw, int sh, int px, int py) {
	for (int y = 0; y < sh; ++y) {
		for (int x = 0; x < sw; ++x) {
			uint32_t c = src[y * sw + x];
			int nx = px + x, ny = py + y;
			if (c != FT_HOLE && nx >= 0 && ny >= 0 && nx < w && ny < h) o[ny * w + nx] = c | 0xFF000000;
		}
	}
}

/* the Status screen's checkerboard as a background (FT-9, FT-14), dark
 * grey before there's one */
static void ft_backdrop(uint32_t* o, int w, int h) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int cx = x - (w - GBA_W) / 2, cy = y - (h - GBA_H) / 2;
			o[y * w + x] = ftCheckerOk && cx >= 0 && cy >= 0 && cx < GBA_W && cy < GBA_H ? ftChecker[cy * GBA_W + cx] | 0xFF000000 : SCREEN_GREY;
		}
	}
}

static bool ft_is_cyan(uint32_t c);

/* The checkerboard from the Party screen (FT-14): what's open on the
 * world map closed (B: the Laws, a place's menu or Info), then START,
 * Party (the menu's first row): the party's screen has it on BG0, as a
 * Status has. True if it got as far as START (a try), false if the map
 * never came free (a notice, travelling). Leaves the copy where it got. */
static bool ft_party_checker(Emu* s) {
	for (int i = 0; i < 6 && emu_read8(s, FT_WM_STATE_(s)) != 255; ++i) {
		aw_tap(s, K_B, 1);
		for (int f = 0; f < 30 && emu_read8(s, FT_WM_STATE_(s)) != 255; ++f) emu_frame_blind(s, 0);
	}
	for (int f = 0; f < 90 && (emu_read8(s, FT_WM_STATE_(s)) != 255 || emu_read8(s, FT_WM_STILL_(s)) < 24); ++f) emu_frame_blind(s, 0);
	if (emu_read8(s, FT_WM_STATE_(s)) != 255 || !emu_read8(s, FT_WM_STILL_(s))) return false;
	aw_run(s, 0, 15); /* (START just after a window closes does nothing) */
	aw_tap(s, K_START, 1);
	aw_run(s, 0, 30);
	aw_tap(s, K_A, 1);
	for (int f = 0; f < 90 && !ft_bg0(s, FT_STATUS_CNT); ++f) emu_frame_blind(s, 0);
	aw_run(s, 0, 40); /* (past its fade-in) */
	if (ft_bg0(s, FT_STATUS_CNT)) ft_take_checker(s);
	return true;
}

/* The world map's Laws (FT-14): the Laws window (L) cut out (BG2 its
 * boxes, BG3 its lettering: not the map, the L Law sign or the pointer,
 * sprites), centred on dark grey; taken again when the clan's somewhere new (or the Laws are
 * open in the game), else the last one again; only with the cursor on the
 * clan or between places (elsewhere it shows another day's laws). */
static uint32_t ftLaws[GBA_W * GBA_H];
static int ftLawsW, ftLawsH;
static uint32_t ftLawsAt; /* the location it was taken at + 1; 0 none */
static const char* ft_world_laws(Emu* s, uint32_t* const* out, int w, int h) {
	uint32_t at = emu_read8(s, FT_LOC_(s)) + 1u;
	/* (a place's menu opening on arrival passes through 4 and 6: let it
	 * finish) */
	for (int f = 0; f < 30 && emu_read8(s, FT_WM_STATE_(s)) != 1 && emu_read8(s, FT_WM_STATE_(s)) != 2 && emu_read8(s, FT_WM_STATE_(s)) != 255; ++f) emu_frame_blind(s, 0);
	uint8_t st = emu_read8(s, FT_WM_STATE_(s));
	uint8_t cur = emu_read8(s, FT_WM_CURSOR_(s));
	if ((st == 1 || at != ftLawsAt) && cur && cur + 1u != at) return "the cursor's on another place";
	if (st == 2 && at != ftLawsAt) {
		/* just arrived, the place's menu up (a town's, a mission's): B
		 * closes it */
		for (int i = 0; i < 4 && emu_read8(s, FT_WM_STATE_(s)) == 2; ++i) {
			aw_tap(s, K_B, 1);
			for (int f = 0; f < 30 && emu_read8(s, FT_WM_STATE_(s)) == 2; ++f) emu_frame_blind(s, 0);
		}
		/* (it passes through other values as it closes, and FT_WM_STILL
		 * climbs back to 24 over about 15 frames) */
		for (int f = 0; f < 90 && (emu_read8(s, FT_WM_STATE_(s)) != 255 || emu_read8(s, FT_WM_STILL_(s)) < 24); ++f) emu_frame_blind(s, 0);
		st = emu_read8(s, FT_WM_STATE_(s));
		if (st != 255) return "the place's menu didn't close";
		cur = emu_read8(s, FT_WM_CURSOR_(s));
		if (cur && cur + 1u != at) return "the cursor's on another place";
	}
	if (st != 1 && at != ftLawsAt) {
		if (st != 255 || !emu_read8(s, FT_WM_STILL_(s))) return "not free on the world map";
		/* (L does nothing while a town's menu is still closing: again) */
		for (int i = 0; i < 6 && emu_read8(s, FT_WM_STATE_(s)) != 1; ++i) {
			aw_tap(s, K_L, 1);
			for (int f = 0; f < 20 && emu_read8(s, FT_WM_STATE_(s)) != 1; ++f) emu_frame_blind(s, 0);
		}
		if (emu_read8(s, FT_WM_STATE_(s)) != 1) return "the Laws didn't open";
	}
	if (st == 1 || at != ftLawsAt) {
		aw_run(s, 0, 40); /* (the window's opening) */
		if (emu_cancelled(s)) return "called off";
		void* tmp = malloc(emu_state_size(s));
		if (!tmp) return "out of memory";
		static uint32_t cut[GBA_W * GBA_H];
		int cw, ch;
		bool ok = ft_cut(s, tmp, cut, NULL, &cw, &ch, 150, GBA_W, 100, GBA_H);
		free(tmp);
		if (emu_cancelled(s)) return "called off";
		if (!ok) return "the Laws window wasn't found";
		/* (R on the world map opens a place's Info window, with the same
		 * FT_WM_STATE as the Laws: the Laws have their cyan fields, the
		 * Info window none) */
		int cyan = 0;
		for (int i = 0; i < cw * ch; ++i) cyan += ft_is_cyan(cut[i]);
		if (getenv("GBADS_DEBUG")) fprintf(stderr, "ft laws: %d cyan of %dx%d\n", cyan, cw, ch);
		if (cyan < cw * ch / 10) return "not the Laws (a place's Info?)";
		memcpy(ftLaws, cut, (size_t) cw * ch * 4), ftLawsW = cw, ftLawsH = ch;
		ftLawsAt = at;
	}
	ft_backdrop(out[0], w, h);
	ft_put(out[0], w, h, ftLaws, ftLawsW, ftLawsH, (w - ftLawsW) / 2, (h - ftLawsH) / 2);
	return NULL;
}

/* FT-9 / FT-12's screen: the turn order in two columns under its LIST
 * bar made wide enough for both, its box see-through over the Status
 * screen's checkerboard by the list's own blend, the Laws below it
 * without their empty fields; centred across, together centred down.
 * lb2/lb3: the list's BG2 (lettering, bar, line) and BG3 (box) cut out,
 * lw x lh; wb2/wb3 the Laws' (BG2 the laws' names, BG3 the window).
 * False if the list isn't as expected (then FT-9's old way). */
static bool ft_is_cyan(uint32_t c) { return c != FT_HOLE && ((c >> 16) & 0xFF) > (c & 0xFF) + 40; }
static bool ft_is_tan(uint32_t c) { return c != FT_HOLE && (c & 0xFF) > ((c >> 16) & 0xFF) + 40; }
static uint32_t ft_blend(uint32_t top, uint32_t under, int eva, int evb) {
	uint32_t o = 0xFF000000;
	for (int sh = 0; sh < 24; sh += 8) {
		int v = ((int) ((top >> sh) & 0xFF) * eva + (int) ((under >> sh) & 0xFF) * evb) / 16;
		o |= (uint32_t) (v > 255 ? 255 : v) << sh;
	}
	return o;
}
static bool ft_turn_layout(uint32_t* o, int w, int h, const uint32_t* lb2, const uint32_t* lb3, int lw, int lh,
                           const uint32_t* wb2, const uint32_t* wb3, int aw, int ah) {
	/* the list: its lettering in bands (the bar, an entry a band, the line) */
	int bs[12], be[12], nb = 0;
	for (int y = 0, on = 0; y <= lh; ++y) {
		bool row = false;
		for (int x = 0; y < lh && x < lw && !row; ++x) row = lb2[y * lw + x] != FT_HOLE;
		if (row && !on && nb < 12) bs[nb] = y, on = 1;
		else if (!row && on) be[nb++] = y - 1, on = 0;
	}
	int n = nb - 2, p0 = -1, p1 = -1;
	for (int y = 0; y < lh; ++y) {
		for (int x = 0; x < lw; ++x) {
			if (lb3[y * lw + x] == FT_HOLE) continue;
			if (p0 < 0) p0 = y;
			p1 = y;
		}
	}
	if (n < 1 || n > 8 || p0 <= be[0] || p1 >= bs[nb - 1]) return false;
	int sh = n >= 2 ? (bs[n] - bs[1]) / (n - 1) : p1 - p0 + 1;
	if (sh < 8 || p0 + n * sh > p1 + 1 + 2) return false;
	int nL = (n + 1) / 2, nR = n - nL, right = 0;
	for (int k = 1; k <= n; ++k) {
		for (int y = bs[k]; y <= be[k]; ++y) {
			for (int x = 0; x < lw; ++x) if (lb2[y * lw + x] != FT_HOLE && x > right) right = x;
		}
	}
	int colW = right + 1 + 8, lim = w < GBA_W ? w : GBA_W;
	/* (two columns wider than the GBA's screen: the first alone, 01-04,
	 * rather than names cut off) */
	if (nR && 2 * colW > lim) nR = 0, n = nL;
	colW = colW > lim ? lim : colW;
	int W = nR ? 2 * colW : colW, bottom = lh - 1 - p1, H = p0 + nL * sh + bottom;
	/* the Laws: each row of fields (where the cyan field is, down its left
	 * quarter), each field empty or not (its name a dash, or nothing) */
	int rs[4], re[4], nr = 0;
	for (int y = 0, on = 0; wb3 && y <= ah; ++y) {
		bool cy = y < ah && ft_is_cyan(wb3[y * aw + aw / 4]);
		if (cy && !on && nr < 4) rs[nr] = y, on = 1;
		else if (!cy && on) {
			on = 0;
			if (y - rs[nr] >= 8) re[nr] = y - 1, ++nr; /* (a field's rows, not the heading's rim) */
		}
	}
	bool full[4][2], keep[4];
	int split[GBA_H], lawsH = 0, kept = 0;
	for (int y = 0; y < ah && y < GBA_H; ++y) {
		split[y] = aw;
		for (int x = 0; x < aw; ++x) if (ft_is_tan(wb3[y * aw + x])) { split[y] = x; break; }
	}
	for (int r = 0; r < nr; ++r) {
		for (int c = 0; c < 2; ++c) {
			int x0 = aw, x1 = -1;
			for (int y = rs[r]; y <= re[r]; ++y) {
				for (int x = 0; x < aw; ++x) {
					if (wb2[y * aw + x] == FT_HOLE || (x < split[(rs[r] + re[r]) / 2]) != (c == 0)) continue;
					x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1;
				}
			}
			full[r][c] = x1 - x0 + 1 >= 10; /* (a dash is about 6 wide) */
		}
		keep[r] = full[r][0] || full[r][1];
		kept += keep[r];
	}
	/* (the window's rows: its head, each kept row with the line below it,
	 * its foot) */
	int rowsOf[GBA_H], nRows = 0;
	if (kept) {
		for (int y = 0; y < rs[0]; ++y) rowsOf[nRows++] = y;
		for (int r = 0; r < nr; ++r) {
			if (!keep[r]) continue;
			for (int y = rs[r]; y <= re[r] + 1 && y < ah; ++y) rowsOf[nRows++] = y;
		}
		for (int y = re[nr - 1] + 2; y < ah && nRows < GBA_H; ++y) rowsOf[nRows++] = y;
		lawsH = nRows;
	}
	int gap = kept ? 4 : 0, top = (h - H - gap - lawsH) / 2;
	top = top < 0 ? 0 : top;
	ft_backdrop(o, w, h);
	/* the list */
	int eva = ftListBlend & 31, evb = (ftListBlend >> 8) & 31;
	eva = eva > 16 ? 16 : eva, evb = evb > 16 ? 16 : evb;
	if (!eva && !evb) eva = 16;
	int ox = (w - W) / 2;
	for (int y = 0; y < H; ++y) {
		for (int x = 0; x < W; ++x) {
			int sx, sy;
			uint32_t c2 = FT_HOLE, c3 = FT_HOLE;
			if (y < p0 || y >= p0 + nL * sh) {
				/* the bar and the line: their left part, then their middle
				 * repeated, then their end */
				sx = x >= W - 8 ? lw - 8 + (x - (W - 8)) : x < lw - 8 ? x : lw - 16 + (x - (lw - 8)) % 8;
				sy = y < p0 ? y : y - (p0 + nL * sh) + p1 + 1;
				if (sx >= 0 && sx < lw && sy < lh) c2 = lb2[sy * lw + sx], c3 = lb3[sy * lw + sx];
			} else {
				int r = y - p0, col = x < colW ? 0 : 1, xx = x - col * colW, k = r / sh + col * nL;
				if (k < n && xx < lw) c2 = lb2[(p0 + k * sh + r % sh) * lw + xx];
				/* (the box's colours top to bottom once over the shorter box) */
				int g = p0 + r * (p1 - p0 + 1) / (nL * sh);
				c3 = lb3[(g > p1 ? p1 : g) * lw + lw / 2];
			}
			uint32_t* d = &o[(top + y) * w + ox + x];
			if (top + y >= h || ox + x < 0 || ox + x >= w) continue;
			if (c2 != FT_HOLE) *d = c2 | 0xFF000000;
			else if (c3 != FT_HOLE) *d = ft_blend(c3, *d, eva, evb);
		}
	}
	/* the Laws, without their empty fields */
	int lx = (w - aw) / 2, ly = top + H + gap;
	for (int i = 0; i < nRows; ++i) {
		int y = rowsOf[i], r = -1;
		for (int k = 0; k < nr; ++k) if (y >= rs[k] && y <= re[k]) r = k;
		for (int x = 0; x < aw; ++x) {
			if (r >= 0 && !full[r][x < split[y] ? 0 : 1]) continue;
			uint32_t c = wb2[y * aw + x] != FT_HOLE ? wb2[y * aw + x] : wb3[y * aw + x];
			int dx = lx + x, dy = ly + i;
			if (c != FT_HOLE && dx >= 0 && dy >= 0 && dx < w && dy < h) o[dy * w + dx] = c | 0xFF000000;
		}
	}
	return true;
}

/* the turn order and the Laws as last taken: BG2, BG3, w x h */
static uint32_t ftWin[2][GBA_W * GBA_H], ftWin3[2][GBA_W * GBA_H];
static int ftWw[2], ftWh[2];
/* the active unit whose turn the turn order was last checked for, and
 * whether it's been taken in that turn (FT-13) */
static uint32_t ftTurnRec, ftTurnSig;
static bool ftTurnTaken;
static unsigned ftTurnN; /* (turns noticed since the world map, for GBADS_DEBUG) */
/* FT-9's screen from the turn order and the Laws as last taken */
static void ft_draw_turn(uint32_t* o, int w, int h) {
	uint32_t (*win)[GBA_W * GBA_H] = ftWin, (*win3)[GBA_W * GBA_H] = ftWin3;
	int *ww = ftWw, *wh = ftWh;
	for (int i = 0; i < w * h; ++i) o[i] = SCREEN_GREY;
	if (ww[0] && ft_turn_layout(o, w, h, win[0], win3[0], ww[0], wh[0], ww[1] ? win[1] : NULL, ww[1] ? win3[1] : NULL, ww[1], ww[1] ? wh[1] : 0)) return;
	/* (else as before: the Laws above the list, its last rows off the
	 * bottom) */
	for (int y = 0; y < wh[1] && ww[1]; ++y) {
		for (int x = 0; x < ww[1]; ++x) if (win[1][y * ww[1] + x] == FT_HOLE) win[1][y * ww[1] + x] = win3[1][y * ww[1] + x];
	}
	for (int y = 0; y < wh[0] && ww[0]; ++y) {
		for (int x = 0; x < ww[0]; ++x) if (win[0][y * ww[0] + x] == FT_HOLE) win[0][y * ww[0] + x] = win3[0][y * ww[0] + x];
	}
	int lh = ww[1] ? wh[1] : 0, th = ww[0] ? wh[0] : 0;
	int y0 = lh + th <= h ? (h - lh - th) / 2 : 0;
	if (ww[1]) ft_put(o, w, h, win[1], ww[1], wh[1], (w - ww[1]) / 2, y0);
	if (ww[0]) ft_put(o, w, h, win[0], ww[0], wh[0], (w - ww[0]) / 2, y0 + lh);
}

static const char* ft_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[2][GBA_W * GBA_H];
	uint32_t (*win)[GBA_W * GBA_H] = ftWin, (*win3)[GBA_W * GBA_H] = ftWin3;
	int *ww = ftWw, *wh = ftWh;
	static int pages, age;
	static uint32_t key;
	int max = *nOut;
	*nOut = 1;
	*step = 300; /* 5 seconds a page */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (ft_world(s)) {
		ftWw[0] = ftWw[1] = 0; /* (a battle's turn order isn't the next one's) */
		ftTurnRec = 0, ftTurnN = 0;
		/* no checkerboard yet (FT-14): taken from the Party screen; the
		 * Laws aren't drawn until it is (the logo or the last picture
		 * stays), three tries at most, then on dark grey */
		static int partyTries;
		if (!ftCheckerOk && partyTries < 3) {
			if (ft_party_checker(s)) ++partyTries;
			if (emu_cancelled(s)) return "called off";
			if (!emu_state_load(s, state)) return "state load failed";
			emu_frame(s, 0);
			if (!ftCheckerOk && partyTries < 3) return "no checkerboard yet";
		}
		return ft_world_laws(s, out, w, h);
	}
	ftLawsAt = 0; /* (a battle: opened again on the world map next time) */
	/* each turn's start (FT-13): between two turns the game has no active
	 * unit for a few frames; the turn order is taken again in the next of
	 * your units' turns, whatever's under the cursor */
	if (ft_virt(s, FT_V_BATTLE)) {
		uint32_t rec = emu_read32(s, FT_ACTIVE - (s->gameCode[3] == 'J' ? 0x30 : 0));
		/* (between two turns, or in a scene once the battle's won (no
		 * unit's turn either): nothing taken, the last picture or the logo
		 * stays) */
		if (rec >> 24 != 0x02) {
			ftTurnRec = 0;
			return "between turns";
		}
		/* (the same unit again, its second turn in a row: the other units'
		 * counters have moved on) */
		uint32_t sig = ft_turn_sig(s, rec);
		if (rec != ftTurnRec || sig != ftTurnSig) ftTurnRec = rec, ftTurnSig = sig, ftTurnTaken = false, ++ftTurnN;
	}
	/* not your turn (FT-13): the turn order and the Laws as last taken */
	if (ft_virt(s, FT_V_THEIRS)) {
		if (!ww[0] && !ww[1]) return "not your turn";
		*nOut = 1;
		ft_draw_turn(out[0], w, h);
		return NULL;
	}
	if (!ft_virt(s, FT_V_IDLE)) return "not on the battle map";
	/* taken again when the cursor moves, or every 4th picture (the units
	 * act, the turn order turns) */
	uint32_t k = emu_read32(s, ft_cursor(s)) ^ (uint32_t) emu_read16(s, 0x04000008) << 16;
	/* (and what's open: A on the judge brings up the active unit's menu
	 * with the cursor where it was; that's when its Status shows) */
	k = (k ^ emu_read8(s, FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0))) * 16777619u;
	bool again = k != key || ++age >= 4 || (!pages && !ww[0]) || !ftTurnTaken;
	void* tmp = malloc(emu_state_size(s));
	if (!tmp) return "out of memory";
	if (again) {
		static uint32_t np[2][GBA_W * GBA_H], nw[2][GBA_W * GBA_H], nw3[2][GBA_W * GBA_H];
		/* a unit under the cursor: its Status (FT-8); else the turn order
		 * and the Laws (FT-9, FT-12); kept only if the capture wasn't
		 * called off on the way (the cursor moved on) */
		bool st = ft_status(s, state, np), ok[2] = { false, false };
		int a[2] = { 0 }, b[2] = { 0 };
		/* (your unit's turn just begun: the turn order too, beside its
		 * Status; kept for the enemy's turns, not shown now) */
		bool turn = st && !ftTurnTaken && !emu_cancelled(s);
		for (int i = 0; (!st || turn) && i < 2; ++i) {
			ok[i] = i == 0 ? ft_list(s, state, tmp, nw[i], nw3[i], &a[i], &b[i]) : ft_system(s, state, tmp, 2, nw[i], nw3[i], &a[i], &b[i]);
		}
		/* (no checkerboard yet: the first unit's Status, once, by A on the
		 * list's first: FT-9) */
		if (!st && !ftCheckerOk && !emu_cancelled(s) && emu_state_load(s, state)) {
			emu_frame(s, 0);
			if (ft_back(s)) {
				aw_tap(s, K_L, 1);
				aw_run(s, 0, 40);
				aw_tap(s, K_A, 1);
				aw_run(s, 0, 30);
				void* at = malloc(emu_state_size(s));
				static uint32_t scratch[2][GBA_W * GBA_H];
				if (at && emu_state_save(s, at)) ft_status(s, at, scratch);
				free(at);
			}
		}
		if (emu_cancelled(s)) {
			free(tmp);
			return "called off";
		}
		/* (neither a Status nor the turn order opened, as while a unit
		 * picks what to hit: the last picture stays, FT-13) */
		if (!st && !ok[0]) {
			key = k, age = 0;
			free(tmp);
			return "nothing opened";
		}
		if (st) memcpy(page, np, sizeof(page)), pages = 2;
		else pages = 0;
		if (!st || turn) {
			if (ok[0] && !ftTurnTaken && getenv("GBADS_DEBUG")) fprintf(stderr, "ft turn %u (unit %04x): turn order taken\n", ftTurnN, (unsigned) (ftTurnRec & 0xFFFF));
			if (ok[0]) ftTurnTaken = true;
			for (int i = 0; i < 2; ++i) {
				if (ok[i]) memcpy(win[i], nw[i], (size_t) a[i] * b[i] * 4), memcpy(win3[i], nw3[i], (size_t) a[i] * b[i] * 4), ww[i] = a[i], wh[i] = b[i];
			}
		}
		key = k, age = 0;
	}
	free(tmp);
	if (!pages && !ww[0] && !ww[1]) return "nothing opened";
	*nOut = pages && max >= 2 ? 2 : 1;
	for (int n = 0; n < *nOut; ++n) {
		uint32_t* o = out[n];
		for (int i = 0; i < w * h; ++i) o[i] = SCREEN_GREY;
		if (pages) {
			ft_put(o, w, h, page[n], GBA_W, GBA_H, (w - GBA_W) / 2, (h - GBA_H) / 2);
			continue;
		}
		ft_draw_turn(o, w, h);
	}
	return NULL;
}

/* ----------------------------------------------------------- Fire Emblem
 *
 * The second screen: with the cursor free on a unit, its status screen
 * (brought up on the shadow core: fe_compose); otherwise the game's logo
 * (hud.logoUnder, hud.logoAlways). Nothing leaves the game screen. (Until
 * v1.3.2's later test builds the windows, menus, conversations and
 * banners moved to the second screen, with the minimap: see git history,
 * fe_find_pieces.) (Each release's addresses: FeRegion.)
 *
 * What the game is doing comes from its processes (the engine's "procs":
 * 64 slots of 0x6C bytes from FE_PROCS, each starting with its script's
 * start and current position), found by script wherever they are. */
/* Each release's addresses (USA; Europe, both En/Es/It and En/Fr/De: the
 * same RAM, their own ROM; Japan: both moved), found by matching the code
 * and the process scripts that use the USA ones. fe_r is the running
 * game's (fe_region: the main and the shadow core run the same game). */
typedef struct {
	char game, region; /* the game code's third and last letters (AE7E: '7', 'E'; The Sacred Stones, BE8E: '8', 'E') */
	uint32_t chapter; /* the chapter's data: 0 off the map (its play time) */
	uint32_t procs; /* the process slots */
	uint32_t cursor; /* the map cursor's x, y (16 bits each) */
	uint32_t units; /* every unit's data (0x48 bytes each) */
	uint32_t phase, event, battle, banner, status, status2; /* process scripts (Europe: a status screen per language) */
	/* the player phase's positions: the minimap and map menu, the cursor
	 * free, a unit's menu, a fight */
	uint16_t atMini, atIdle, atMenu, atFight;
} FeRegion;
#define FE7_AT .atMini = 0x50, .atIdle = 0x70, .atMenu = 0xD0, .atFight = 0xF0
#define FE8_AT .atMini = 0x60, .atIdle = 0x80, .atMenu = 0xE8, .atFight = 0x108
static const FeRegion feRegions[] = {
	{ '7', 'E', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08B93374, 0x08B90D88, 0x08B9A99C, 0x08B937AC, 0x08CC1F5C, 0, FE7_AT },
	{ '7', 'Y', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08C57730, 0x08C54ECC, 0x08C5ECE8, 0x08C57B8C, 0x08D88F1C, 0x08DC195C, FE7_AT },
	{ '7', 'X', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08C8EBA0, 0x08C8C33C, 0x08C96158, 0x08C8EFFC, 0x08DC195C, 0, FE7_AT },
	{ '7', 'J', 0x0202BBF4, 0x02024E20, 0x0202BBC8, 0x0202BD4C, 0x08C02630, 0x08BFFF78, 0x08C09CE8, 0x08C02A68, 0x08D8A520, 0, FE7_AT },
	/* Fire Emblem: The Sacred Stones (the same engine; its RAM mapped
	 * from Fire Emblem's by tools/romxref.py, its process scripts found
	 * running in the owner's states and a new game played through: the
	 * player phase by its code, the banner by its commands, the event
	 * engine with the text boxes, the battle scene in a fight, the
	 * status with R) */
	{ '8', 'E', 0x0202BCF0, 0x02024E68, 0x0202BCC4, 0x0202BE4C, 0x0859AAD8, 0x08591AC0, 0x085B9358, 0x0859AF80, 0x08A009D8, 0, FE8_AT },
	/* (Europe: the USA's RAM; Japan: 4 bytes lower but the process slots.
	 * Their scripts found by the USA ones' commands) */
	{ '8', 'P', 0x0202BCF0, 0x02024E68, 0x0202BCC4, 0x0202BE4C, 0x087ABDB8, 0x087A2ADC, 0x087DBBCC, 0x087AC29C, 0x08A8EE00, 0, FE8_AT },
	{ '8', 'J', 0x0202BCEC, 0x02024E68, 0x0202BCC0, 0x0202BE48, 0x085C2F58, 0x085B98A0, 0x085E3538, 0x085C3400, 0x08A72A90, 0, FE8_AT },
};
static const FeRegion* fe_r = &feRegions[0];
static void fe_region(Emu* e) {
	for (size_t i = 0; i < sizeof(feRegions) / sizeof(*feRegions); ++i) {
		if (e->gameCode[2] == feRegions[i].game && e->gameCode[3] == feRegions[i].region) fe_r = &feRegions[i];
	}
}
#define FE_CHAPTER (fe_r->chapter)
#define FE_TURN (fe_r->chapter + 0xF) /* whose turn: 0 yours, 0x40 other, 0x80 the enemy's */
#define FE_PROCS (fe_r->procs)
#define FE_CURSOR (fe_r->cursor)
#define FE_P_PHASE (fe_r->phase) /* the player phase */
/* (its positions: each game's) */
#define FE_AT_MINI (fe_r->atMini)   /* the minimap (and the map menu) */
#define FE_AT_IDLE (fe_r->atIdle)   /* the cursor free */
#define FE_AT_MENU (fe_r->atMenu)   /* a unit's menu: Attack, Wait... */
#define FE_AT_FIGHT (fe_r->atFight) /* a fight */
#define FE_IDLE (FE_P_PHASE + FE_AT_IDLE)
#define FE_MINI (FE_P_PHASE + FE_AT_MINI)
#define FE_FIGHT (FE_P_PHASE + FE_AT_FIGHT)
#define FE_MENU (FE_P_PHASE + FE_AT_MENU)
#define FE_P_EVENT (fe_r->event)   /* the event engine: a conversation, the tutorial */
#define FE_P_BATTLE (fe_r->battle) /* a fight's battle scene (any army's turn) */
#define FE_P_BANNER (fe_r->banner) /* ENEMY PHASE / PLAYER PHASE across the map */
/* virtual addresses (fe_virt) */
#define FE_V_PHASE 0xF0000001 /* the player phase's position (FE_AT_*) + 1, 0 if it isn't running */
#define FE_V_HUD 0xF0000002   /* what goes to the second screen: FE_HUD_* */
#define FE_V_FREE 0xF0000003  /* 1 while the cursor's free (no unit selected, no menu) */
#define FE_V_OFFMAP 0xF0000004 /* 1 off the battle map: the title, a story scene */
#define FE_V_CHAPTER 0xF0000005 /* FE_CHAPTER's word */
#define FE_V_TURN 0xF0000006   /* FE_TURN's byte */
#define FE_V_CURSOR 0xF0000007 /* the cursor's x, y */
#define FE_V_FIGHT 0xF0000008  /* 1 in a fight (the player phase at FE_AT_FIGHT) */
#define FE_IN_CHAPTER { FE_V_CHAPTER, 0xFFFFFFFF, 1, 4, true, 0xFFFFFFFF, true }
enum { FE_HUD_NONE, FE_HUD_IDLE, FE_HUD_TALK, FE_HUD_MENU, FE_HUD_BANNER = 5 };

/* The current position of the process running `start`, 0 if none is. */
static uint32_t fe_proc(Emu* e, uint32_t start) {
	for (int i = 0; i < 64; ++i) {
		uint32_t a = FE_PROCS + i * 0x6C;
		if (emu_read32(e, a) == start) return emu_read32(e, a + 4);
	}
	return 0;
}

/* A talking portrait on screen: sprites in palettes 6-7, tiles from 640
 * (on the map: a slot from 640 and one from 768), or palette 3, tiles 64-159 (before and after a fight). */
static bool fe_portrait(Emu* e) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(e, 0x07000000 + i * 8), a2 = emu_read16(e, 0x07000004 + i * 8);
		if ((a0 & 0x0300) == 0x0200 || ((a0 & 0xFF) >= 160 && (a0 & 0xFF) < 192)) continue;
		if ((a2 & 0x3FF) >= 640 && (a2 >> 12 == 6 || a2 >> 12 == 7)) return true;
		/* (in a fight's battle scene: palette 3, tiles 64-159) */
		if ((a2 & 0x3FF) >= 64 && (a2 & 0x3FF) < 160 && a2 >> 12 == 3) return true;
	}
	return false;
}

/* Units on the map on screen: sprites in palettes 12-15. A story scene
 * (a picture behind the talk, or black: before and after the battle) has
 * none; the battle map nearly always does. */
static bool fe_units_shown(Emu* e) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(e, 0x07000000 + i * 8), a2 = emu_read16(e, 0x07000004 + i * 8);
		if ((a0 & 0x0300) == 0x0200 || ((a0 & 0xFF) >= 160 && (a0 & 0xFF) < 240)) continue;
		if (a2 >> 12 >= 12) return true;
	}
	return false;
}

/* Off the battle map: the title and menus (no chapter), or an event with
 * no units on screen, outside a fight. */
static bool fe_off_map(Emu* e) {
	if (!emu_read32(e, FE_CHAPTER)) return true;
	return fe_proc(e, FE_P_EVENT) && fe_proc(e, FE_P_PHASE) != FE_FIGHT && !fe_proc(e, FE_P_BATTLE) && !fe_units_shown(e);
}

/* The tutorial's boxes and a unit's own small speech bubbles (no
 * portrait: those stay on the map, by the unit) are the same sprites in
 * palette 5: the tutorial's are parchment yellow (its 5th colour has
 * little blue), the bubbles white. */
static bool fe_tutorial_box(Emu* e) {
	uint16_t c = emu_read16(e, 0x05000200 + 5 * 32 + 4 * 2);
	return ((c >> 10) & 31) + 6 < ((c >> 5) & 31);
}

static uint32_t fe_virt(Emu* e, uint32_t addr) {
	fe_region(e);
	switch (addr) {
	case FE_V_PHASE: {
		uint32_t at = fe_proc(e, FE_P_PHASE);
		return at ? at - FE_P_PHASE + 1 : 0;
	}
	case FE_V_CHAPTER: return emu_read32(e, FE_CHAPTER);
	case FE_V_TURN: return emu_read8(e, FE_TURN);
	case FE_V_CURSOR: return emu_read32(e, FE_CURSOR);
	case FE_V_FIGHT: return fe_proc(e, FE_P_PHASE) == FE_FIGHT;
	case FE_V_FREE: return fe_proc(e, FE_P_PHASE) == FE_IDLE;
	case FE_V_OFFMAP: return fe_off_map(e);
	case FE_V_HUD: {
		if (fe_proc(e, FE_P_BANNER)) return FE_HUD_BANNER;
		uint32_t ph = fe_proc(e, FE_P_PHASE);
		/* (a story scene's talk stays on the game screen) */
		if (fe_proc(e, FE_P_EVENT) && (!ph || fe_portrait(e) || fe_tutorial_box(e))) return fe_off_map(e) ? FE_HUD_NONE : FE_HUD_TALK;
		if (ph == FE_IDLE) return FE_HUD_IDLE;
		if (ph == FE_MENU) return FE_HUD_MENU;
		return FE_HUD_NONE;
	}
	}
	return 0;
}

/* The map pictures (FE-3): with the cursor free on a unit, that unit's
 * status screen (R) as the game draws it, filling the picture, one for
 * each of its three pages (shown in turn, 5 seconds each); otherwise
 * (no unit there, or a unit selected: its commands take the place) a
 * picture of nothing but the background, which hud.logoUnder fills with
 * the logo (FE-1). The status is opened once the cursor is free (B out of
 * whatever's open first), on this copy of the game. */
#define FE_CHAPTER_NO (FE_CHAPTER + 0xE)
#define FE_P_STATUS (fe_r->status) /* a unit's status screen */
#define FE_PAGES 3
#define FE_UNITS (fe_r->units)
#define FE_UNITS_LEN (0x48 * 144)
static uint32_t pageKey;
static int pageN;

static const char* fe_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[FE_PAGES][GBA_W * GBA_H], fresh[FE_PAGES][GBA_W * GBA_H];
	static int lastChapter = -1;
	int max = *nOut;
	fe_region(s);
	*nOut = 1;
	*step = 300; /* 5 seconds a page */
	int pages = 0;
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int chapter = emu_read8(s, FE_CHAPTER_NO);
	/* a unit selected (or its menu): no status (its commands take the place) */
	bool cursorFree = fe_proc(s, FE_P_PHASE) == FE_IDLE;
	if (chapter != lastChapter) lastChapter = chapter, pageKey = 0, pageN = 0;
	if (cursorFree) {
		/* the status of the unit under the cursor, if there's one: taken
		 * again only when the cursor or a unit changes (so it doesn't move
		 * between pictures: a blink, a step) */
		uint32_t key = 2166136261u;
		for (uint32_t a = FE_UNITS; a < FE_UNITS + FE_UNITS_LEN; a += 4) key = (key ^ emu_read32(s, a)) * 16777619u;
		key = (key ^ emu_read32(s, FE_CURSOR)) * 16777619u;
		key = (key ^ (uint32_t) chapter) * 16777619u;
		if (key == pageKey) {
			pages = pageN;
		} else {
			/* (the cursor still gliding to its place takes no R: let it
			 * settle first) */
			aw_run(s, 0, 12);
			aw_tap(s, K_R, 1);
			aw_run(s, 0, 8);
			if (fe_proc(s, FE_P_STATUS) || (fe_r->status2 && fe_proc(s, fe_r->status2))) {
				aw_run(s, 0, 40); /* (past its fade-in, which rewrites the palette) */
				for (; pages < FE_PAGES && pages < max; ++pages) {
					if (pages) {
						aw_tap(s, K_RIGHT, 1);
						aw_run(s, 0, 24);
					}
					memcpy(fresh[pages], s->fb, sizeof(fresh[pages]));
				}
			}
			/* kept for this cursor and these units only when R opened a
			 * status, or there's no unit under the cursor: a status that
			 * didn't open for a moment (the cursor still moving) is tried
			 * again at the next picture */
			bool unit = false;
			uint32_t cur = emu_read32(s, FE_CURSOR);
			for (uint32_t a = FE_UNITS; a < FE_UNITS + FE_UNITS_LEN && !unit; a += 0x48) {
				unit = emu_read8(s, a + 3) == 8 && emu_read8(s, a + 0x10) == (cur & 0xFF) && emu_read8(s, a + 0x11) == ((cur >> 16) & 0xFF);
			}
			if (emu_cancelled(s)) return "called off";
			memcpy(page, fresh, sizeof(page[0]) * (size_t) pages);
			pageKey = pages || !unit ? key : 0, pageN = pages;
		}
	}
	*nOut = pages ? pages : 1;
	for (int k = 0; k < *nOut; ++k) {
		uint32_t* o = out[k];
		for (int i = 0; i < w * h; ++i) o[i] = MAP_BG_NAVY; /* (the logo goes there) */
		for (int y = 0; pages && y < GBA_H && y < h; ++y) {
			for (int x = 0; x < GBA_W && x < w; ++x) o[y * w + x] = page[k][y * GBA_W + x] | 0xFF000000;
		}
	}
	return NULL;
}

/* ---------------------------------------------------------- Sword of Mana
 *
 * The second screen: the Status screen (START's ring > Status > Status),
 * opened on the shadow core from the live state at every capture, so it's
 * up to date without the player opening it, at the game screen's size. The open ring's items are a
 * list of 8-byte entries from SOM_ring - 0x64 (the item first: in the main
 * ring 1 Items, 2 Weapons, 5 Spirits, 8 Status, as far as the story has
 * them; in Status's own, 1 Status, 2 NPC Setting...), their number at
 * - 0x0C and the one turned to at the ring address itself, each ring
 * starting where it was left. (Each release's address: SomRegion.) */
#define SOM_MAIN_STATUS 8
#define SOM_SUB_STATUS 1
typedef struct {
	char region;
	uint32_t ring;
	/* the title's lettering, where it doesn't shimmer (and isn't the
	 * clouds drifting behind it): 7 of them on the title screen */
	Probe title[8];
} SomRegion;
#define SOM_TITLE_EN { { 6, 83, 0xFFFFFF }, { 55, 75, 0xFFF7EF }, { 83, 76, 0xFFFFFF }, { 143, 66, 0xFFFFFF }, \
	                   { 159, 68, 0xF7DED6 }, { 184, 68, 0xFFF7EF }, { 37, 68, 0x737BD6 }, { 234, 93, 0x427BD6 } }
static const SomRegion somRegions[] = {
	{ 'E', 0x02013EC4, SOM_TITLE_EN },
	{ 'Y', 0x02013ED4, SOM_TITLE_EN },
	{ 'J', 0x02013EC4, { { 54, 67, 0xFFBD6B }, { 79, 82, 0xF7DED6 }, { 171, 91, 0xF79C31 }, { 200, 63, 0xBDCEDE },
	                     { 112, 77, 0xFFFFFF }, { 137, 56, 0xFFFFFF }, { 34, 57, 0xF7DED6 }, { 222, 52, 0xFFFFFF } } },
};
static const SomRegion* som_r(Emu* e) {
	for (size_t i = 0; i < sizeof(somRegions) / sizeof(*somRegions); ++i) {
		if (e->gameCode[3] == somRegions[i].region) return &somRegions[i];
	}
	return &somRegions[0];
}
#define som_ring(e) (som_r(e)->ring)
#define SOM_V_TITLE 0xF0000001 /* 1 on the title screen */

/* n of the probes are on screen (each channel within tol) */
static bool som_probes(Emu* e, const Probe* p, int n, int need, int tol) {
	int hit = 0;
	for (int i = 0; i < n; ++i) {
		uint32_t c = e->fb[p[i].y * GBA_W + p[i].x];
		int d[3] = { (int) (c & 0xFF) - (int) (p[i].rgb >> 16), (int) ((c >> 8) & 0xFF) - (int) ((p[i].rgb >> 8) & 0xFF),
		             (int) ((c >> 16) & 0xFF) - (int) (p[i].rgb & 0xFF) };
		hit += abs(d[0]) <= tol && abs(d[1]) <= tol && abs(d[2]) <= tol;
	}
	return hit >= need;
}

/* the Status screen's panels and stat icons (not the portrait) */
static const Probe somStatus[7] = { { 90, 10, 0x945229 }, { 68, 111, 0xFF3100 }, { 115, 123, 0xFF3100 }, { 150, 46, 0x945229 },
                                    { 150, 148, 0x945229 }, { 197, 120, 0x945229 }, { 121, 148, 0x945229 } };
static uint32_t som_virt(Emu* e, uint32_t addr) {
	return addr == SOM_V_TITLE && som_probes(e, som_r(e)->title, 8, 7, 8);
}

/* Turns the open ring to item `item` (RIGHT, a step at a time; the first
 * steps may come while it's still opening). */
static bool som_turn(Emu* s, int item) {
	uint32_t ring = som_ring(s);
	for (int i = 0; i < 8; ++i) {
		int at = emu_read8(s, ring), n = emu_read8(s, ring - 0x0C);
		if (n > 0 && n <= 8 && at < n && emu_read16(s, ring - 0x64 + 8 * at) == item) {
			aw_run(s, 0, 20);
			return true;
		}
		aw_run(s, K_RIGHT, 2);
		aw_run(s, 0, 22);
	}
	return false;
}

static bool som_status_up(Emu* s) {
	return som_probes(s, somStatus, 7, 6, 8);
}

static const char* som_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	aw_run(s, K_START, 2);
	aw_run(s, 0, 40);
	if (!som_turn(s, SOM_MAIN_STATUS)) return "the ring did not open";
	aw_run(s, K_A, 2);
	aw_run(s, 0, 40);
	if (!som_turn(s, SOM_SUB_STATUS)) return "the Status ring did not open";
	/* (the Status ring takes presses once it's done opening: A again if
	 * the first came too early) */
	aw_run(s, 0, 30);
	bool up = false;
	for (int tries = 0; tries < 3 && !up; ++tries) {
		aw_run(s, K_A, 2);
		up = aw_settle(s, som_status_up, 90);
	}
	if (!up) return "the Status screen did not open";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ---------------------------------------------------------- Dragon Ball Z: The Legacy of Goku
 *
 * The second screen (DBZ-2): the pause menu (START), opened on the shadow
 * core from the live state at every capture, with its list of options left
 * out (the panel drawn again from its layers alone: BG2's panel over BG3's
 * picture, without BG1's text or the cursor sprite). In that empty panel
 * goes the game's top-left HUD, live (hud: the HP / Ki bars, or SELECT's /
 * flying's counter, DBZ-3 and DBZ-4).
 */
#define DBZ_FIELD_DISP 0x1F40
#define DBZ_PAUSE_DISP 0x1F00
#define DBZ_PAUSE_BG0 0x1C0F
#define DBZ_COUNTER 0x0200384C /* bit 0x08: the counter at the top left, not the bars */
/* the options panel's inside */
#define DBZ_PANEL_X0 11
#define DBZ_PANEL_Y0 84
#define DBZ_PANEL_X1 147
#define DBZ_PANEL_Y1 147
static bool dbz_pause_up(Emu* s) {
	return emu_io16(s, 0x00) == DBZ_PAUSE_DISP && emu_io16(s, 0x08) == DBZ_PAUSE_BG0;
}
static const char* dbz_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	static uint8_t* st;
	static size_t stSize;
	size_t sz = emu_state_size(s);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st) return "no memory";
	if (!emu_state_load(s, state)) return "state load failed";
	if (emu_io16(s, 0x00) != DBZ_FIELD_DISP) return "not walking about";
	/* the pause menu, once it's settled */
	emu_frame(s, 0);
	aw_run(s, K_START, 3);
	if (!aw_settle(s, dbz_pause_up, 90)) return "the pause menu did not open";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	/* the options panel again without its text and cursor */
	if (!emu_state_save(s, st)) return "state save failed";
	emu_layers(s, 0x0C);
	emu_state_load(s, st);
	emu_frame(s, 0);
	for (int y = DBZ_PANEL_Y0; y <= DBZ_PANEL_Y1 && y < h; ++y)
		for (int x = DBZ_PANEL_X0; x <= DBZ_PANEL_X1 && x < w; ++x) out[y * w + x] = s->fb[y * GBA_W + x];
	emu_layers(s, 0x1F);
	emu_state_load(s, st);
	return NULL;
}

/* ---------------------------------------------------------- Dragon Ball Z: The Legacy of Goku II
 *
 * The second screen (DBZ2-2): the Scouter's map of the area (R), opened on
 * the shadow core from the live state at every capture, once it has faded
 * in (BLDALPHA up to 16), without its four scroll arrows. Where R opens
 * nothing (before the Scouter), the pause menu's Status page (START,
 * DBZ2-3). An attack under way takes no R, so R is tried a few times
 * before giving up on the map.
 */
#define DBZ2_FIELD_DISP 0x1F40
#define DBZ2_MAP_DISP 0x1D40
#define DBZ2_PAUSE_DISP 0x0E00
static const SpriteRule dbz2Arrows[4] = {
	{ 2, 70, 6, 74, 0, 1023, 0, 1u << 0, 0, 1u << 8 },     /* left (8x16) */
	{ 226, 70, 230, 74, 0, 1023, 0, 1u << 0, 0, 1u << 8 }, /* right */
	{ 110, 2, 114, 6, 0, 1023, 0, 1u << 0, 0, 1u << 4 },   /* top (16x8) */
	{ 110, 146, 114, 150, 0, 1023, 0, 1u << 0, 0, 1u << 4 }, /* bottom */
};
/* the pause menu, faded in (BLDALPHA up to 16, as the map) */
/* DBZ2-8: the button prompts that don't apply on the second screen. The
 * map's L and R (the scouter frame's top corners, BG0): their greys and
 * the black outline round them (within 2 pixels) painted the frame's own
 * dark green, its bright lines left as they are */
#define DBZ2_FRAME_BG 0x004218 /* (24, 66, 0) as mGBA's XBGR */
static bool dbz2_grey(uint32_t c) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	return (r == g && g == b && r > 40) || (r > 150 && g > 150 && b > 150); /* (and its highlights) */
}
static bool dbz2_black(uint32_t c) {
	return (c & 0xF8F8F8) == 0;
}
static void dbz2_map_prompts(uint32_t* out, int w, int h) {
	static const int box[2][4] = { { 0, 0, 31, 25 }, { 208, 0, 239, 25 } };
	static uint8_t mask[26][32];
	for (int k = 0; k < 2; ++k) {
		const int* b = box[k];
		memset(mask, 0, sizeof(mask));
		for (int y = b[1]; y <= b[3] && y < h; ++y)
			for (int x = b[0]; x <= b[2] && x < w; ++x) mask[y - b[1]][x - b[0]] = dbz2_grey(out[y * w + x]);
		for (int y = b[1]; y <= b[3] && y < h; ++y) {
			for (int x = b[0]; x <= b[2] && x < w; ++x) {
				uint32_t* c = &out[y * w + x];
				bool near = false;
				for (int dy = -2; dy <= 2 && !near; ++dy)
					for (int dx = -2; dx <= 2 && !near; ++dx) {
						int yy = y + dy - b[1], xx = x + dx - b[0];
						near = yy >= 0 && yy < 26 && xx >= 0 && xx < 32 && mask[yy][xx] == 1;
					}
				if (mask[y - b[1]][x - b[0]] || (near && dbz2_black(*c))) *c = (*c & 0xFF000000) | DBZ2_FRAME_BG;
			}
		}
	}
}
/* The Status page's: its corner arrows (BG1) from the page drawn without
 * BG1; L JOURNAL and ITEMS R (BG1, BG2) below the orange tab line from the
 * stars alone (BG3); the panel's UP and DOWN arrows (BG2) its own blue */
#define DBZ2_PANEL 0xDEB56B /* (107, 181, 222) */
static bool dbz2_line(uint32_t c) {
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	return r > 150 && g > 60 && b < 100; /* (orange, yellow) */
}
static const char* dbz2_status_prompts(Emu* s, uint32_t* out, int w, int h) {
	static uint8_t* st;
	static size_t stSize;
	static uint32_t noBg1[GBA_W * GBA_H];
	size_t sz = emu_state_size(s);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st) return "no memory";
	if (!emu_state_save(s, st)) return "state save failed";
	emu_layers(s, 0x0C);
	emu_state_load(s, st);
	emu_frame(s, 0);
	memcpy(noBg1, s->fb, sizeof(noBg1));
	emu_layers(s, 0x08);
	emu_state_load(s, st);
	emu_frame(s, 0);
	/* the corner arrows */
	static const int arrows[2][4] = { { 4, 0, 27, 28 }, { 212, 0, 235, 28 } };
	for (int k = 0; k < 2; ++k)
		for (int y = arrows[k][1]; y <= arrows[k][3] && y < h; ++y)
			for (int x = arrows[k][0]; x <= arrows[k][2] && x < w; ++x) out[y * w + x] = noBg1[y * GBA_W + x];
	/* the tabs either side of STATUS: below the line, column by column */
	static const int tabs[2][2] = { { 0, 82 }, { 160, 239 } };
	for (int k = 0; k < 2; ++k) {
		for (int x = tabs[k][0]; x <= tabs[k][1] && x < w; ++x) {
			int cut = 140;
			for (int y = 124; y < GBA_H && y < h; ++y)
				if (dbz2_line(out[y * w + x])) cut = y + 2; /* (its dark edge below) */
			for (int y = cut; y < GBA_H && y < h; ++y) out[y * w + x] = s->fb[y * GBA_W + x];
			/* (the icons' tops, over the line's ends: their greys) */
			for (int y = 124; y < cut && y < h; ++y)
				if (dbz2_grey(out[y * w + x])) out[y * w + x] = s->fb[y * GBA_W + x];
		}
	}
	/* the orange line where the L and R icons covered it: its four rows
	 * (orange, yellow, orange, dark edge) along the straight ends, copied
	 * from a clean column of the line beside them */
	static const int ends[2][3] = { { 0, 75, 40 }, { 165, 239, 200 } };
	for (int k = 0; k < 2; ++k)
		for (int y = 136; y <= 139 && y < h; ++y)
			for (int x = ends[k][0]; x <= ends[k][1] && x < w; ++x) out[y * w + x] = out[y * w + ends[k][2]];
	/* UP and DOWN */
	static const int ud[2][4] = { { 23, 35, 40, 53 }, { 23, 99, 40, 117 } };
	for (int k = 0; k < 2; ++k)
		for (int y = ud[k][1]; y <= ud[k][3] && y < h; ++y)
			for (int x = ud[k][0]; x <= ud[k][2] && x < w; ++x) out[y * w + x] = (out[y * w + x] & 0xFF000000) | DBZ2_PANEL;
	emu_layers(s, 0x1F);
	emu_state_load(s, st);
	return NULL;
}
static bool dbz2_pause_up(Emu* s) {
	return emu_io16(s, 0x00) == DBZ2_PAUSE_DISP && emu_io16(s, 0x52) == 0x0010;
}
static const char* dbz2_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	if (emu_io16(s, 0x00) != DBZ2_FIELD_DISP) return "not walking about";
	/* the map: R (three tries), then its fade-in */
	bool map = false;
	for (int t = 0; t < 3 && !map; ++t) {
		aw_run(s, K_R, 3);
		for (int f = 0; f < 40 && !map; ++f) {
			emu_frame_blind(s, 0);
			map = emu_io16(s, 0x00) == DBZ2_MAP_DISP;
		}
	}
	if (map) {
		int f = 0;
		while (f < 60 && emu_io16(s, 0x52) != 0x0010) emu_frame_blind(s, 0), ++f;
		if (emu_io16(s, 0x00) != DBZ2_MAP_DISP || emu_io16(s, 0x52) != 0x0010) return "the map did not settle";
		emu_hide_sprites(s, dbz2Arrows, 4);
		emu_frame(s, 0);
		emu_frame(s, 0);
		emu_hide_sprites(s, NULL, 0);
		if (emu_io16(s, 0x00) != DBZ2_MAP_DISP) return "the map went away";
	} else {
		/* no map: the Status page, from the live state again */
		if (!emu_state_load(s, state)) return "state load failed";
		emu_frame(s, 0);
		aw_run(s, K_START, 3);
		if (!aw_settle(s, dbz2_pause_up, 90)) return "neither the map nor the pause menu opened";
	}
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	/* without the prompts that don't apply here (DBZ2-8) */
	if (map) dbz2_map_prompts(out, w, h);
	else return dbz2_status_prompts(s, out, w, h);
	return NULL;
}

/* ---------------------------------------------------------- Mario & Luigi: Superstar Saga
 *
 * The second screen (ML-2): the Bros. Info screen, opened on the shadow core
 * from the live state at every capture (SELECT's menu, its third icon, A),
 * on the page of the brother in front (it opens on Mario's; R turns to
 * Luigi's).
 *   ML_STATE, ML_MODE and the pairs after them: the game's screen and the
 *     stack of scenes it's in, written only as they change (by the game's
 *     scene code, the same on every map: walking about is set by the one
 *     room setup, 0x08022FCC, with constants). ML_PAGE: the top scene.
 *     (Until v1.4.11's test builds 0x02000C99: a DMA queue entry's transfer
 *     count, which held 1 walking about on the first maps and flickered 0 /
 *     1 on others, after a battle: the capture took the field for the menu
 *     and failed there)
 *   ML_CURSOR the menu's icon (0 Items, 1 Equipment, 2 Bros. Info)
 */
#define ML_STATE 0x02000008 /* 8 walking about (its menus too), 3 a battle, 2 the title */
#define ML_MODE 0x02000009 /* with state 8: 21 walking about, 10 SELECT's menu opening, 26 the menus */
#define ML_PAGE 0x0200000D /* in the menus: 9 SELECT's menu, 12 Bros. Info (Mario's page), 14 Luigi's */
#define ML_WALKING 0x1508 /* ML_STATE and ML_MODE as a halfword, walking about */
#define ML_CURSOR 0x030040A9
#define ML_LEAD 0x02004F4C /* the brother in front: 0 Mario, 1 Luigi (START swaps) */
/* BG3's tile map entry inside the results' red EXP panel (row 7, column 9):
 * empty through a battle, set from the results' first frame (no battle box
 * reaches it). (Until v1.4.11's test builds 0x02036EB0, a word that was 0
 * in the first battles and a pointer in their results, but holds other
 * things in later ones: their boxes stayed on the game screen) */
#define ML_RESULTS 0x0600F9D2
/* BG3's tile map entries at the top right box's right edge (row 1,
 * column 29) and the bottom box's (row 18): palette 8 while a box is there */
#define ML_BOX_TOP 0x0600F87A
#define ML_BOX_BOTTOM 0x0600FC7A
enum { ML_OTHER, ML_FIELD, ML_MENU, ML_BROS };
static int ml_screen(Emu* s) {
	if (emu_read16(s, ML_STATE) == ML_WALKING) return ML_FIELD;
	if (emu_read8(s, ML_STATE) != 8 || emu_read8(s, ML_MODE) != 26) return ML_OTHER;
	int page = emu_read8(s, ML_PAGE);
	return page == 9 ? ML_MENU : page == 12 || page == 14 ? ML_BROS : ML_OTHER;
}
static bool ml_wait(Emu* s, int v, int max) {
	for (int f = 0; f < max; ++f) {
		emu_frame(s, 0);
		if (ml_screen(s) == v) return true;
	}
	return false;
}
/* runs frames until the picture has held (nearly) still for 8: the page's
 * opening and the brothers' turn pause for 2 or 3 frames at a time, and a
 * sparkle blinks (66 pixels) every 4 */
static bool ml_settle(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max; ++f) {
		emu_frame(s, 0);
		int d = 0;
		for (int i = 0; i < GBA_W * GBA_H && d <= 200; ++i) d += prev[i] != s->fb[i];
		still = f && d <= 200 ? still + 1 : 0;
		if (still >= 8) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}
/* ---------------------------------------------------------- Kingdom Hearts: Chain of Memories
 *
 * The second screen (KHC-1): the World Map, opened on the shadow core from
 * the live state at every capture with SELECT (straight from walking
 * about), checked by its fixed pixels and taken once its rooms are in
 * (they appear over about 70 frames), without its cursor and A button
 * bubble. If it doesn't open, the last map stays (KHC-2). (START's menu
 * was the first way in: its top edge is see-through, so its pixels took
 * the room's colours and the check failed in some rooms.) The European
 * and Japanese releases: the same, their battle pointer elsewhere
 * (khc_eur, khc_jpn). */
#define KHC_BATTLE 0x02039DD4 /* the battle's pointer: 0 outside one */
/* the World Map: its banner (WORLD MAP on blue), the floor's badge at the
 * bottom left, the dark strip along the bottom */
static const Probe khcMap[8] = { { 4, 4, 0x00298C }, { 20, 6, 0xFFFFFF }, { 40, 5, 0xFFFFFF }, { 60, 8, 0x8C9CD6 },
                                 { 70, 3, 0x395AB5 }, { 4, 157, 0x087BA5 }, { 236, 157, 0x08184A }, { 120, 157, 0x08184A } };
/* the map's cursor, the pointing hand, and its A button bubble: the only
 * sprites in palette 0 at priority 0 there (their tiles move with what the
 * game has loaded: the bubble 454 in one room, 450 in another) */
static const SpriteRule khcHide[1] = { { -64, -16, 239, 159, 0, 1023, 0, 1u << 0 } };
static bool khc_map_up(Emu* s) {
	return som_probes(s, khcMap, 8, 7, 8);
}
/* the title (KHC-4): the emblem and the art at its corner, as they stand
 * once it's up, the same in all three releases (not the emblem on black
 * before it) */
#define KHC_V_TITLE 0xF0000040
static const Probe khcTitle[8] = { { 128, 0, 0xF7EFE7 }, { 222, 12, 0xBDAD94 }, { 48, 14, 0xADADAD }, { 84, 18, 0xADADAD },
                                   { 106, 28, 0x7B7B7B }, { 64, 30, 0x7B7B7B }, { 16, 32, 0xB5B5B5 }, { 148, 38, 0xADADAD } };
static uint32_t khc_virt(Emu* e, uint32_t addr) {
	return addr == KHC_V_TITLE && som_probes(e, khcTitle, 8, 7, 8);
}
static bool khc_wait(Emu* s, bool (*up)(Emu*), int n) {
	for (int f = 0; f < n && !up(s); ++f) emu_frame(s, 0);
	return up(s);
}
static const char* khc_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, khcHide, 1);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	aw_run(s, K_SELECT, 4);
	if (!khc_wait(s, khc_map_up, 90)) return "the World Map didn't open";
	aw_run(s, 0, 90); /* (its rooms all in) */
	if (emu_cancelled(s)) return "called off";
	if (!khc_map_up(s)) return "the World Map closed";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ---------------------------------------------------------- Yu-Gi-Oh! The Eternal Duelist Soul
 *
 * The second screen (EDS-1 to EDS-4, EDS-10): the Card View of the card
 * under the player's cursor, the game's own full-screen page for a card
 * (its picture, name, type and text), opened on the shadow core from the
 * live state at every capture: on the duel field A (the card's commands,
 * Card View first and chosen) then A; in a card list A alone, or A then A
 * where A brings up a command bar (Deck Edit; Japan's Deck screen). With
 * Card View already open in the game, it's taken as it is. It's checked by
 * its fixed pixels and taken once it has faded in and held still.
 *
 * Both releases work alike but draw their screens differently (Duel
 * Monsters 5 Expert 1 is the original; The Eternal Duelist Soul its
 * English remake), so each has a table of its registers, pixels and
 * addresses (edsUS, edsJP; the variant AY5J picks the second).
 *
 * Which screen is up is told by BG0CNT and BG1CNT (each screen sets them
 * once and keeps them; DISPCNT where two share them):
 *              USA                       Japan
 *   field      0x0007 0x8106             0x0087 0x8186
 *   view       0x0044 0x0186             0x0005 0x0187     Card View
 *   list       0x0004 0x0104             0x0004 0x0185 +   a pile's list in a
 *                                        DISPCNT 0x1F40    duel (Japan: also
 *                                                          the Deck screen;
 *                                                          its conversations
 *                                                          have DISPCNT 0x1F04)
 *   deck       0x1800 0x1A01             -                 Deck Edit (Card
 *                                                          Trading's choice)
 *   won        0x0004 0x0105 + DISPCNT 0x1F40 (both)       the cards won
 *   (USA: Card Trading and Password share won's two, with other DISPCNTs)
 *
 * USA addresses: 0x0201D7E0 7 while a duel is set up, 6 while it's under
 * way, 4 after it, 0 before the first; the field cursor 0x0201D7D4 its side,
 * 0x0201D7D8 its zone (0 monsters, 5 Magic & Trap, 0x0B the hand; 0x0A,
 * 0x0C to 0x0F the piles round the field), 0x0201D7DC its column;
 * 0x0201D7B8 bit 3 while the player moves it freely; a list's cursor
 * 0x02015274; Deck Edit's 0x0201E140, its list 0x0201F73C.
 * Japan: the opponent's life points 0x02007908 (0 out of a duel: cleared
 * when one ends); the field cursor's place on the screen 0x02008490 (x),
 * 0x02008492 (y) and its spot 0x02008482, 0x02008483 bit 7 while the
 * player moves it freely; the Deck screen's and a pile list's cursor
 * 0x02005F36 (its row) and 0x02005F3A; the cards won's 0x02009D02.
 *
 * The field's info bar (its bottom rows) names the card under the cursor
 * in white. USA: a pile's count is cyan, an empty zone and the opponent's
 * face-down cards leave it empty. Japan: a pile's name starts in pure green
 * (自分の, 相手の); a monster's LV. is green too, but on the line above. */
#define EDS_V_LIVE 0xF0000040 /* a card's under a cursor (or in Card View): taken */
#define EDS_V_IDLE 0xF0000041 /* out of the duels and the card screens: the logo */
#define EDS_V_ROOM 0xF0000042 /* the screen and where its cursor is */
enum { EDS_NONE, EDS_FIELD, EDS_LIST, EDS_VIEW, EDS_DECK, EDS_WON };
typedef struct {
	uint16_t field[2], view[2], list[2], listDisp, deck[2];
	Probe viewProbes[8];
	bool jp;
	/* the card's commands on the field: the magnifier's tile (Card View, the
	 * first), the chosen command's name's tile, and how far right of its
	 * icon the name is (eds_wait_commands) */
	uint16_t cmdGlass, cmdName;
	int cmdDx;
} EdsRelease;
static const EdsRelease edsUS = {
	{ 0x0007, 0x8106 }, { 0x0044, 0x0186 }, { 0x0004, 0x0104 }, 0, { 0x1800, 0x1A01 },
	/* the blue name bar's end (the name never reaches it), the darker bar
	 * under it, the black left of and under the card and between the card
	 * and its text */
	{ { 239, 0, 0x0000FF }, { 239, 12, 0x0000FF }, { 239, 22, 0x000084 }, { 1, 20, 0x000000 },
	  { 102, 60, 0x000000 }, { 102, 140, 0x000000 }, { 1, 159, 0x000000 }, { 60, 159, 0x000000 } },
	false, 548, 612, 0
};
static const EdsRelease edsJP = {
	{ 0x0087, 0x8186 }, { 0x0005, 0x0187 }, { 0x0004, 0x0185 }, 0x1F40, { 0xFFFF, 0xFFFF },
	/* the card's dark grey edge (left, bottom), its pale silver right edge,
	 * the black at the left and between the card and its text panel: the
	 * same for every kind of card */
	{ { 0, 80, 0x000000 }, { 2, 40, 0x212121 }, { 2, 120, 0x212121 }, { 50, 159, 0x212121 },
	  { 108, 60, 0x94A5AD }, { 108, 130, 0xCEDEE7 }, { 110, 40, 0x000000 }, { 110, 140, 0x000000 } },
	true, 436, 468, 2
};
static int eds_screen(const EdsRelease* r, Emu* e) {
	uint16_t bg0 = emu_io16(e, 0x08), bg1 = emu_io16(e, 0x0A), disp = emu_io16(e, 0x00);
	if (bg0 == r->field[0] && bg1 == r->field[1]) return EDS_FIELD;
	if (bg0 == r->view[0] && bg1 == r->view[1]) return EDS_VIEW;
	if (bg0 == r->list[0] && bg1 == r->list[1] && (!r->listDisp || disp == r->listDisp)) return EDS_LIST;
	if (bg0 == r->deck[0] && bg1 == r->deck[1]) return EDS_DECK;
	if (bg0 == 0x0004 && bg1 == 0x0105 && disp == 0x1F40) return EDS_WON;
	return EDS_NONE;
}
static bool eds_view(const EdsRelease* r, Emu* s) {
	return eds_screen(r, s) == EDS_VIEW && som_probes(s, r->viewProbes, 8, 8, 8);
}
/* the field's info bar names a card (see above) */
static bool eds_named(const EdsRelease* r, Emu* e) {
	int white = 0, green = 0;
	if (!r->jp) {
		for (int y = 151; y < 159; ++y)
			for (int x = 2; x < 120; ++x) white += (e->fb[y * GBA_W + x] & 0xFFFFFF) == 0xFFFFFF;
		return white >= 12;
	}
	for (int y = 152; y < 160; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = e->fb[y * GBA_W + x] & 0xFFFFFF;
			white += c == 0xFFFFFF;
			green += c == 0x00FF00;
		}
	return white >= 30 && !green;
}
/* in a duel. USA: 0x0201ADAC is 0 from a duel's set-up until it's won or
 * lost (0x10 after), and before the first since power-on, where
 * 0x0201D7E0 is 0 too. (0x0201D7E0 alone isn't the duel's state: 4 from a
 * battle's Quick-play question to the end of the turn, still 6 after a
 * lost duel: issue #26.) */
static bool eds_in_duel(const EdsRelease* r, Emu* e) {
	/* Japan: both players' life points (the opponent's 0 after a duel won,
	 * the player's after one lost, both before the first) */
	if (r->jp) return emu_read16(e, 0x02007908) && emu_read16(e, 0x020078F0);
	return emu_read8(e, 0x0201ADAC) == 0 && emu_read8(e, 0x0201D7E0) != 0;
}
/* in a duel, the player's turn over (EDS-11). USA: the duel's step
 * 0x02015EE8 6 from Complete your turn, 7 handing over, 8 the opponent's
 * turn; or 0x0201ADF2 (the turn count) bit 1, the opponent's turn. Japan:
 * 0x0200831A bit 0, the opponent's turn (from its Draw Phase) */
static bool eds_turn_over(const EdsRelease* r, Emu* e) {
	if (!eds_in_duel(r, e)) return false;
	if (r->jp) return emu_read8(e, 0x0200831A) & 1;
	uint8_t step = emu_read8(e, 0x02015EE8);
	return (step >= 6 && step <= 8) || (emu_read8(e, 0x0201ADF2) & 2);
}
/* the player moves the field cursor freely, in a duel (any phase of their
 * turn) */
static bool eds_free(const EdsRelease* r, Emu* e) {
	if (r->jp) return eds_in_duel(r, e) && (emu_read8(e, 0x02008483) & 0x80);
	return eds_in_duel(r, e) && (emu_read8(e, 0x0201D7B8) & 0x08);
}
static uint32_t eds_virt_for(const EdsRelease* r, Emu* e, uint32_t addr) {
	int sc = eds_screen(r, e);
	if (addr == EDS_V_LIVE) {
		if (sc != EDS_FIELD) return sc != EDS_NONE;
		if (!eds_free(r, e) || !eds_named(r, e)) return 0;
		if (r->jp) return 1;
		/* USA: a monster, a Magic or Trap card or a card in the hand (not
		 * the opponent's hand); not a pile (A opens its list, or draws, or
		 * offers to give up) */
		uint8_t side = emu_read8(e, 0x0201D7D4), zone = emu_read8(e, 0x0201D7D8);
		return zone == 0 || zone == 5 || (zone == 0x0B && side == 0);
	}
	/* (the field too, once its duel is decided: the winner's last line,
	 * over the field: EDS-6); and from the end of the player's turn on
	 * anything but a Card View opened in it (EDS-11) */
	if (addr == EDS_V_IDLE)
		return ((sc == EDS_NONE || sc == EDS_FIELD) && !eds_in_duel(r, e)) || (sc != EDS_VIEW && eds_turn_over(r, e));
	if (addr == EDS_V_ROOM) {
		uint32_t k = (uint32_t) sc << 28;
		if (r->jp) {
			if (sc == EDS_FIELD)
				k |= (uint32_t) emu_read8(e, 0x02008490) << 16 | emu_read8(e, 0x02008492) << 8 | emu_read8(e, 0x02008482);
			else if (sc == EDS_LIST) k |= emu_read8(e, 0x02005F36) << 8 | emu_read8(e, 0x02005F3A);
			else if (sc == EDS_WON) k |= emu_read8(e, 0x02009D02);
			return k;
		}
		if (sc == EDS_FIELD)
			k |= (uint32_t) emu_read8(e, 0x0201D7D4) << 16 | emu_read8(e, 0x0201D7D8) << 8 | emu_read8(e, 0x0201D7DC);
		else if (sc == EDS_LIST || sc == EDS_WON) k |= emu_read8(e, 0x02015274);
		else if (sc == EDS_DECK) k |= emu_read8(e, 0x0201F73C) << 16 | emu_read16(e, 0x0201E140);
		return k;
	}
	return 0;
}
static uint32_t eds_virt(Emu* e, uint32_t addr) {
	return eds_virt_for(&edsUS, e, addr);
}
static uint32_t eds_virt_jp(Emu* e, uint32_t addr) {
	return eds_virt_for(&edsJP, e, addr);
}
/* On the field: runs frames until the card's commands are up with Card View
 * chosen (its magnifier with the chosen command's name under it: USA tiles
 * 548 and 612, Japan 436 and 468, 2 pixels right of the icon); false if
 * they don't come (A chose a target: an attack's, Snatch Steal's; Japan's
 * Battle Phase: A on the player's monster chooses it to attack) */
static bool eds_wait_commands(const EdsRelease* r, Emu* s, int max) {
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		int glass = -1, name = -2;
		for (int i = 0; i < 128; ++i) {
			uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a1 = emu_read16(s, 0x07000002 + i * 8),
			         a2 = emu_read16(s, 0x07000004 + i * 8);
			if ((a0 & 0x0300) == 0x0200) continue; /* hidden */
			if ((a2 & 0x3FF) == r->cmdGlass) glass = a1 & 0x1FF;
			else if ((a2 & 0x3FF) == r->cmdName) name = a1 & 0x1FF;
		}
		if (glass >= 0 && name - glass == r->cmdDx) return true;
	}
	return false;
}
/* runs frames until Card View is up and has held still for 3 (its fade-in
 * done) */
static bool eds_wait_view(const EdsRelease* r, Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		if (!eds_view(r, s)) {
			still = 0;
			continue;
		}
		still = still && !memcmp(prev, s->fb, sizeof(prev)) ? still + 1 : 1;
		if (still >= 3) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}
/* a list after A: runs frames until its screen gives way to another it
 * knows; true if to Card View (false: elsewhere, A chose the card or the
 * list's Exit; or still the list after max frames, an empty one). The
 * frames between, blanked for the fade (Japan's cards won: DISPCNT
 * 0x0140), aren't a screen yet. */
static bool eds_wait_leave(const EdsRelease* r, Emu* s, int sc, int max) {
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame_blind(s, 0);
		int now = eds_screen(r, s);
		if (now != sc && now != EDS_NONE) return now == EDS_VIEW;
	}
	return false;
}
/* USA, the field cursor (0x0201D7D4 its side, 0x0201D7D8 its zone,
 * 0x0201D7DC its column) after a press: runs frames until it has glided to
 * its place */
static void eds_settle(Emu* s) {
	aw_run(s, 0, 12);
}
/* USA: the field's cursor choosing the monster to attack (A brought up no
 * commands there): from the state again, B backs out of the choice to the
 * attacking monster, the cursor free (EDS-1); UP takes it to the
 * opponent's monster above (the opponent's columns run the other way:
 * 4 - col), LEFT and RIGHT to the one that was chosen (the same name in
 * the info bar, or nothing: the choice's cursor stops only on monsters it
 * can attack, the free one on every place); then A, its commands. True with them up, Card View chosen. Anything else (a card
 * effect's choice: B doesn't back out to a monster of the player's): false */
static bool eds_target_commands(const EdsRelease* r, Emu* s, const void* state) {
	if (r->jp || !emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	eds_settle(s);
	uint8_t side = emu_read8(s, 0x0201D7D4), zone = emu_read8(s, 0x0201D7D8), col = emu_read8(s, 0x0201D7DC);
	if (side != 1 || zone != 0 || col > 4) return false;
	/* the name in the info bar, to check the same card's reached */
	static uint32_t bar[8 * 118];
	for (int y = 0; y < 8; ++y) memcpy(&bar[y * 118], &s->fb[(151 + y) * GBA_W + 2], 118 * sizeof(uint32_t));
	aw_tap(s, K_B, 1);
	eds_settle(s);
	if (!(emu_read8(s, 0x0201D7B8) & 0x08) || emu_read8(s, 0x0201D7D4) != 0 || emu_read8(s, 0x0201D7D8) != 0) return false;
	aw_tap(s, K_UP, 1);
	eds_settle(s);
	for (int i = 0; i < 5 && !emu_cancelled(s); ++i) {
		if (emu_read8(s, 0x0201D7D4) != 1 || emu_read8(s, 0x0201D7D8) != 0) return false;
		int at = emu_read8(s, 0x0201D7DC);
		if (at == col) break;
		aw_tap(s, at > col ? K_RIGHT : K_LEFT, 1);
		eds_settle(s);
	}
	if (emu_read8(s, 0x0201D7DC) != col || emu_read8(s, 0x0201D7D4) != 1 || !eds_named(r, s)) return false;
	for (int y = 0; y < 8; ++y)
		if (memcmp(&bar[y * 118], &s->fb[(151 + y) * GBA_W + 2], 118 * sizeof(uint32_t))) return false;
	aw_tap(s, K_A, 1);
	return eds_wait_commands(r, s, 20);
}
static const char* eds_compose_for(const EdsRelease* r, Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int sc = eds_screen(r, s);
	if (sc == EDS_NONE) return "not a card screen";
	if (sc != EDS_VIEW) {
		/* (after a moment: the cursor glides to its place, and A on the
		 * way does nothing) */
		aw_run(s, 0, 12);
		if (sc == EDS_FIELD) {
			/* the info bar names the cursor's card once it's there: none, an
			 * empty zone, a pile or a face-down card of the opponent's;
			 * nothing taken, and no slowing down of the next tries */
			if (!eds_named(r, s)) return "map button did nothing";
			/* A: the card's commands, Card View first and chosen, told by
			 * their sprites. Where none come, A chose a target (the cursor
			 * counts as free there): an attack's, its monster's Card View
			 * all the same (USA, EDS-1); a card effect's, or Japan's (in
			 * its Battle Phase A on the player's monster chooses it to
			 * attack): nothing taken, cheaply (EDS-5, EDS-10). Or it isn't
			 * a card (Japan: a pile's list): the same. */
			aw_tap(s, K_A, 1);
			if (!eds_wait_commands(r, s, 20) && !eds_target_commands(r, s, state)) return "map button did nothing";
			aw_tap(s, K_A, 1);
		} else if (sc == EDS_DECK) {
			aw_tap(s, K_A, 1);
			aw_run(s, 0, 8);
			if (eds_screen(r, s) != sc) return "A did something else";
			aw_tap(s, K_A, 1);
		} else if (r->jp && sc == EDS_LIST) {
			/* Japan's Deck screen and pile lists: A opens Card View, or (the
			 * Deck screen) a command bar, 詳細 (details) chosen, then A */
			aw_tap(s, K_A, 1);
			if (!eds_wait_view(r, s, 40)) {
				if (eds_screen(r, s) != EDS_LIST) return "A did something else";
				aw_tap(s, K_A, 1);
			}
		} else {
			/* a list: A opens Card View; where it doesn't (the list's Exit
			 * chosen, an empty list, a Designation list: A chose the
			 * card), nothing taken, cheaply (EDS-5) */
			aw_tap(s, K_A, 1);
			if (!eds_wait_leave(r, s, sc, 48)) return "map button did nothing";
		}
	}
	if (!eds_view(r, s) && !eds_wait_view(r, s, 60)) return "Card View didn't open";
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}
static const char* eds_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	return eds_compose_for(&edsUS, s, state, out, w, h);
}
static const char* eds_compose_jp(Emu* s, const void* state, uint32_t* out, int w, int h) {
	return eds_compose_for(&edsJP, s, state, out, w, h);
}

/* ---------------------------------------------------------- Zoids: Legacy
 *
 * The second screen, from zl_compose on the hidden copy:
 *  - walking about (ZL-1): the map screen (SELECT), whole, its marker's
 *    spin a loop of 16 frames, without the line in the box under it (the
 *    game's prompt) or the town list's cursor (captureHide);
 *  - in the past (ZL-7), where SELECT says the map can't be seen: that
 *    message's text box alone (BG3), once it's written out, in the middle
 *    on dark grey;
 *  - a Zoid choosing in a battle (ZL-8): its Status (R), whole.
 * While a battle plays out (ZL-3) the logo (combat), and what was shown
 * before is forgotten (menu). What the game is doing comes from zl_virt:
 * a mode byte (2 in the story's battles, 1 its scenes) and the place byte
 * 3 after it (0 on the title), a word that's the battle's task list in
 * every battle, the menu window up (0x03005E00) and the map Zeru is on;
 * the map screen is told by its pixels. (The routine in front, a word in
 * one of the game's task slots, isn't used: after RESUME A GAME that slot
 * keeps the title's.) Each release's addresses: ZlRegion. */
typedef struct {
	char region;
	uint32_t mode;   /* the mode byte; the place 3 after it */
	uint32_t battle; /* holds ZL_BATTLE_ON through every battle */
	uint32_t mapNo;  /* the map Zeru is on (a halfword) */
	uint8_t zoidW;   /* a Zoid's menu's width (its window at 00 11) */
} ZlRegion;
#define ZL_WINDOW 0x03005E00 /* the menu window up: x, y, 00 06, width, height (16 bits) */
#define ZL_BATTLE_ON 0x03005B74
static const ZlRegion zlRegions[] = {
	{ 'E', 0x02030664, 0x02032E8C, 0x0202ECF4, 10 },
	/* (the mode byte and the map number by romxref; the battle's word near
	 * the USA's, checked in its battles) */
	{ 'J', 0x0202FDB0, 0x02032584, 0x0202E4C0, 8 },
};
static const ZlRegion* zl_r(Emu* e) {
	return &zlRegions[e->gameCode[3] == 'J' ? 1 : 0];
}
#define ZL_V_LIVE 0xF0000060   /* 1 where a picture is taken: walking about (ZL_V_FREE) or a Zoid choosing */
#define ZL_V_MAP 0xF0000061    /* 1 with the map screen up */
#define ZL_V_FORGET 0xF0000062 /* 1 on the title, or a battle playing out: what was shown goes (ZL-3, ZL-4) */
#define ZL_V_PLAYOUT 0xF0000063 /* 1 in a battle, no Zoid choosing: the logo (ZL-3) */
#define ZL_V_PLACE 0xF0000064  /* the byte 3 after the mode: changes with the place (ZL-1's early capture) */
#define ZL_V_MAPNO 0xF0000065  /* the map Zeru is on (ZL-7's early capture) */
#define ZL_V_CHOOSE 0xF0000066 /* 1 with a Zoid choosing (ZL-8) */
/* the past (ZL-7), where the game has no map: the Past Overworld (map 64)
 * and the Empire Capital there (58 to 62: its streets and buildings; the
 * same numbers in both releases) */
static bool zl_past(Emu* e) {
	uint16_t m = emu_read16(e, zl_r(e)->mapNo);
	return m == 64 || (m >= 58 && m <= 62);
}
static bool zl_battle(Emu* e) {
	const ZlRegion* r = zl_r(e);
	return emu_read32(e, r->battle) == ZL_BATTLE_ON || emu_read8(e, r->mode) == 2;
}
/* the window up is one of choosing a Zoid's move (ZL-8): its menu, or the
 * turn's own in random battles (both at 00 11; so is the story's battles'
 * opening menu, in Japan as wide as a Zoid's: a byte, 1 at every opening
 * menu, 0 at the others, tells it apart), the turn's menu in the story's,
 * the weapons, Decide/Cancel, the Status */
#define ZL_OPENING 0x0300567E
static bool zl_choose_window(Emu* e) {
	uint8_t x = emu_read8(e, ZL_WINDOW), y = emu_read8(e, ZL_WINDOW + 1);
	return (x == 0x00 && y == 0x11 && emu_read8(e, ZL_OPENING) == 0) || (x == 0x40 && y == 0x11) || (x == 0x42 && y == 0x1A) ||
	       (x == 0x42 && y == 0x18) || (x == 0x16 && y == 0x19) || (x == 0x1E && y == 0x13);
}
static bool zl_choosing(Emu* e) {
	return zl_battle(e) && zl_choose_window(e);
}
/* the title and its menu: no battle, the mode and the place 0 (walking
 * about the place is set; name entry and the opening are scenes, mode 1) */
static bool zl_title(Emu* e) {
	const ZlRegion* r = zl_r(e);
	return !zl_battle(e) && emu_read8(e, r->mode) == 0 && emu_read8(e, r->mode + 3) == 0;
}
/* walking about: no battle, no story scene, not the title */
static bool zl_free(Emu* e) {
	return !zl_battle(e) && emu_read8(e, zl_r(e)->mode) == 0 && !zl_title(e);
}
static bool zl_map_up(Emu* s);
static uint32_t zl_virt(Emu* e, uint32_t addr) {
	const ZlRegion* r = zl_r(e);
	switch (addr) {
	case ZL_V_LIVE: return zl_free(e) || zl_choosing(e);
	case ZL_V_MAP: return zl_map_up(e);
	case ZL_V_FORGET: return zl_title(e) || (zl_battle(e) && !zl_choosing(e));
	case ZL_V_PLAYOUT: return zl_battle(e) && !zl_choosing(e);
	case ZL_V_PLACE: return emu_read8(e, r->mode + 3);
	case ZL_V_MAPNO: return emu_read16(e, r->mapNo);
	case ZL_V_CHOOSE: return zl_choosing(e);
	}
	return 0;
}

/* the map screen's fixed pixels (ZL-1): the panels' dark edges, the list's
 * and the box's light green at their right ends (clear of any town's
 * name), the sea at the map's top corners: the same in both releases */
static const Probe zlMap[8] = { { 239, 0, 0x424242 }, { 129, 0, 0x424242 }, { 128, 128, 0x424242 }, { 238, 60, 0xE7F7D6 },
                                { 238, 120, 0xE7F7D6 }, { 235, 140, 0xE7F7D6 }, { 2, 2, 0x004AFF }, { 126, 2, 0x004AFF } };
#define ZL_BOX_FILL 0xE7F7D6 /* the boxes' light green */
static bool zl_map_up(Emu* s) {
	return som_probes(s, zlMap, 8, 7, 8);
}
static void zl_copy(Emu* s, uint32_t* out, int w, int h) {
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
}
/* (0xRRGGBB to the frame buffer's bytes R, G, B) */
static uint32_t zl_px(uint32_t rgb) {
	return 0xFF000000u | (rgb >> 16 & 0xFF) | (rgb & 0xFF00) | (rgb & 0xFF) << 16;
}

/* ZL-1: the map, 16 frames 3 apart (the marker's spin), the box under the
 * list emptied: inside its edges (x 2-237, rows 131-158) the light green */
static const char* zl_map(Emu* s, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	if (zl_map_up(s)) return "screen already looks like the map"; /* (open in the game: ZL-2) */
	aw_run(s, K_SELECT, 4);
	int f = 0;
	while (f < 60 && !zl_map_up(s)) emu_frame(s, 0), ++f;
	if (!zl_map_up(s)) return "map screen did not appear";
	aw_run(s, 0, 6); /* (drawn at once; a few frames for the sprites) */
	if (emu_cancelled(s)) return "called off";
	int n = *nOut < 16 ? *nOut : 16;
	for (int i = 0; i < n; ++i) {
		if (i) aw_run(s, 0, 3);
		if (!zl_map_up(s)) return "the map closed";
		zl_copy(s, out[i], w, h);
		for (int y = 131; y <= 158 && y < h; ++y)
			for (int x = 2; x <= 237 && x < w; ++x) out[i][y * w + x] = zl_px(ZL_BOX_FILL);
	}
	*nOut = n;
	*step = 3;
	return NULL;
}

/* the arrow at the end of a written-out message: a sprite, tile 1001 */
static bool zl_arrow(Emu* s) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a2 = emu_read16(s, 0x07000004 + i * 8);
		if ((a0 & 0x0300) != 0x0200 && (a0 & 0xFF) < 160 && (a2 & 0x3FF) == 1001) return true;
	}
	return false;
}
/* ZL-7: SELECT in the past brings up the game's message; once it's written
 * out (its arrow up), its box (BG3, rows 96-159, the whole width; the
 * arrow a sprite) alone, in the middle on dark grey */
#define ZL_MSG_Y 96
#define ZL_MSG_H 64
static const char* zl_message(Emu* s, uint32_t* out, int w, int h) {
	aw_run(s, K_SELECT, 4);
	int f = 0;
	while (f < 300 && !zl_arrow(s)) emu_frame_blind(s, 0), ++f;
	if (!zl_arrow(s)) return "the message didn't come up";
	if (emu_cancelled(s)) return "called off";
	/* (its frame's edges: dark grey at the top rows, then grey, then the
	 * light green) */
	void* st = malloc(emu_state_size(s));
	if (!st) return "out of memory";
	if (!emu_state_save(s, st)) {
		free(st);
		return "state save failed";
	}
	emu_layers(s, 0x08);
	emu_state_load(s, st);
	emu_frame(s, 0);
	static const Probe box[4] = { { 120, ZL_MSG_Y, 0x424242 }, { 120, ZL_MSG_Y + 2, 0x8C8C84 }, { 120, ZL_MSG_Y + 3, 0xE7F7D6 },
	                              { 238, ZL_MSG_Y + 40, 0xE7F7D6 } };
	bool ok = som_probes(s, box, 4, 4, 8);
	for (int i = 0; i < w * h; ++i) out[i] = SCREEN_GREY;
	int y0 = (h - ZL_MSG_H) / 2, x0 = (w - GBA_W) / 2;
	for (int y = 0; ok && y < ZL_MSG_H; ++y) {
		if (y0 + y < 0 || y0 + y >= h) continue;
		for (int x = 0; x < GBA_W; ++x)
			if (x0 + x >= 0 && x0 + x < w) out[(y0 + y) * w + x0 + x] = s->fb[(ZL_MSG_Y + y) * GBA_W + x] | 0xFF000000;
	}
	emu_layers(s, 0x1F);
	emu_state_load(s, st);
	free(st);
	return ok ? NULL : "not the message box";
}

/* ZL-8: the Status (R) of the Zoid choosing. The window at ZL_WINDOW is the
 * last one opened, not the one up: B from the Status back to the Zoid's menu
 * leaves it 1E 13 (and B from Decide leaves 16 19). The Status is up when
 * that's 1E 13 and BG0 is scrolled to its rows (BG0VOFS 0x181; 0x68 at the
 * menus). So: R; if no Status comes up (in the weapons or Decide), B and R
 * again, a few times; B at a Zoid's menu does nothing */
#define ZL_STATUS_VOFS 0x181
static bool zl_status_up(Emu* s) {
	return emu_read8(s, ZL_WINDOW) == 0x1E && emu_read8(s, ZL_WINDOW + 1) == 0x13 &&
	       (emu_io16(s, 0x12) & 0x1FF) == ZL_STATUS_VOFS;
}
static const char* zl_status(Emu* s, uint32_t* out, int w, int h) {
	for (int i = 0; i < 5 && !zl_status_up(s); ++i) {
		if (i) {
			aw_tap(s, K_B, 1);
			aw_run(s, 0, 12);
		}
		aw_run(s, K_R, 4);
		int f = 0;
		while (f < 14 && !zl_status_up(s)) emu_frame_blind(s, 0), ++f;
		if (emu_cancelled(s)) return "called off";
	}
	if (!zl_status_up(s)) return "R didn't bring up a Status";
	aw_run(s, 0, 16); /* (its numbers and the Zoid drawn) */
	if (emu_cancelled(s)) return "called off";
	if (!zl_status_up(s)) return "the Status closed";
	zl_copy(s, out, w, h);
	return NULL;
}

static const char* zl_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	int max = *nOut;
	*nOut = 1;
	*step = 1;
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (zl_battle(s)) return zl_choosing(s) ? zl_status(s, out[0], w, h) : "the battle playing out";
	if (!zl_free(s)) return "not walking about";
	if (zl_past(s)) return zl_message(s, out[0], w, h);
	*nOut = max;
	return zl_map(s, out, nOut, step, w, h);
}

/* Bros. Info's frame, the same on both brothers' pages (its leather edges,
 * the ring's rim, the plaid, the boxes): not the field or the menu */
static const Probe mlBrosInfo[8] = { { 10, 150, 0x6B8C73 }, { 4, 80, 0x633921 }, { 235, 80, 0x7B736B }, { 120, 157, 0xBDC6B5 },
                                     { 60, 5, 0x7B7B73 }, { 200, 4, 0x633921 }, { 20, 100, 0xEFEFE7 }, { 5, 5, 0x7B4231 } };
static bool ml_bros_info(Emu* s) {
	return som_probes(s, mlBrosInfo, 8, 7, 8);
}
static void ml_page(const uint32_t* fb, uint32_t* out, int w, int h) {
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
}
static const char* ml_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	*nOut = 1;
	*step = 300;
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	bool luigi = emu_read8(s, ML_LEAD) == 1;
	int at = ml_screen(s);
	if (at == ML_FIELD) {
		aw_tap(s, K_SELECT, 1);
		/* (the menu's scene some 50 frames on, as the suitcase opens) */
		if (!ml_wait(s, ML_MENU, 90)) return "map button did nothing";
		at = ML_MENU;
	}
	if (at == ML_MENU) {
		aw_run(s, 0, 20); /* (past its opening: DOWN is taken 10 to 15 frames on) */
		for (int i = 0; i < 8 && emu_read8(s, ML_CURSOR) != 2; ++i) {
			aw_tap(s, emu_read8(s, ML_CURSOR) < 2 ? K_DOWN : K_UP, 1);
			aw_run(s, 0, 4);
		}
		if (emu_read8(s, ML_CURSOR) != 2) return "Bros. Info not reached in the menu";
		aw_tap(s, K_A, 1);
		if (!ml_wait(s, ML_BROS, 90)) return "Bros. Info didn't open";
	} else if (at != ML_BROS) {
		return "not walking about";
	}
	if (!ml_settle(s, 150)) return "Bros. Info didn't settle";
	if (!ml_bros_info(s)) return "not Bros. Info";
	if (!luigi) {
		ml_page(s->fb, out[0], w, h);
		return NULL;
	}
	/* Luigi in front: R (the portrait turns round), until the page changes */
	static uint32_t first[GBA_W * GBA_H];
	memcpy(first, s->fb, sizeof(first));
	for (int tries = 0; tries < 3; ++tries) {
		aw_tap(s, K_R, 1);
		aw_run(s, 0, 20); /* (the turn: 12 frames) */
		if (!ml_settle(s, 150) || !ml_bros_info(s)) break;
		if (memcmp(first, s->fb, sizeof(first))) {
			ml_page(s->fb, out[0], w, h);
			return NULL;
		}
	}
	return "Luigi's page didn't come up";
}

/* ---------------------------------------------------------- Harvest Moon: Friends of Mineral Town
 *
 * The second screen (HM-1, HM-2): the town map (L+START), opened on the
 * shadow core from the live state at every capture, its pointing glove
 * moved onto the player's head (so the text box under the map says which
 * part of the valley that is) and then left out of the picture. Where L+START
 * opens nothing (a conversation, an event, a menu), the capture fails and the
 * last map stays (HM-3).
 *
 * HM_MODE is the game's mode (0x78 from the opening through the title and
 * the naming screens, 0 at power-on; walking about 0x37, other values in
 * scenes). The game's own data (the clock, money, the area number) sits
 * 0x2834 higher once a battery save has been loaded, so nothing here reads
 * it. The map's sprites: the glove tile 200 in palette 5, the player's head
 * tile 204 in palette 6 (16x16 both; every release). */
#define HM_MODE 0x02000012
#define HM_MODE_TITLE 0x78
#define HM_V_TITLE 0xF0000001 /* 1 on the title, its opening, the naming screens (mode 0x78; not 0,
                                 * which an area change passes through for a frame) */
#define HM_GLOVE_TILE 200
#define HM_GLOVE_PAL 5
#define HM_HEAD_TILE 204
#define HM_HEAD_PAL 6
#define HM_DRAWN 3 /* the frames drawn at the end of a capture */
static uint32_t hm_virt(Emu* e, uint32_t addr) {
	if (addr != HM_V_TITLE) return 0;
	return emu_read8(e, HM_MODE) == HM_MODE_TITLE;
}

/* the town map is up: its display (window 1 on) and BG3, the map picture */
static bool hm_map_up(Emu* s) {
	return emu_io16(s, 0x00) == 0x7F40 && emu_io16(s, 0x0E) == 0x1E40;
}

/* the first shown sprite with this tile and palette: its x, y */
static bool hm_find(Emu* s, int tile, int pal, int* x, int* y) {
	static uint16_t oam[512];
	emu_oam_read(s, oam);
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = oam[i * 4], a1 = oam[i * 4 + 1], a2 = oam[i * 4 + 2];
		if ((a0 & 0x0300) == 0x0200 || (a2 & 0x3FF) != tile || (a2 >> 12) != pal) continue;
		*x = a1 & 0x1FF, *y = a0 & 0xFF;
		if (*x >= 256) *x -= 512;
		if (*y >= 160) *y -= 256;
		return true;
	}
	return false;
}

/* the glove's fingertip, 24.8 x then y, is kept in a word pair whose place
 * isn't fixed (0x02005B68 in a new game, 0x02002620 in one continued from
 * a save): found by its value, the fingertip under the glove sprite */
static uint32_t hm_glove_var(Emu* s, int gx, int gy) {
	size_t n = 0;
	const uint8_t* ram = emu_memory(s, 2, &n);
	if (!ram) return 0;
	uint32_t want_x = (uint32_t)(gx + 15) << 8, want_y = (uint32_t)(gy + 5) << 8;
	for (size_t i = 0; i + 8 <= n; i += 4) {
		uint32_t x, y;
		memcpy(&x, ram + i, 4);
		if (x != want_x) continue;
		memcpy(&y, ram + i + 4, 4);
		if (y == want_y) return 0x02000000u + (uint32_t)i;
	}
	return 0;
}

static const char* hm_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	/* (open in the game already: as it is, the glove moved back) */
	if (!hm_map_up(s)) {
		/* going from one area to the next (a mosaic out, the backgrounds
		 * off while the next loads, a mosaic in: nearly 2 seconds walking)
		 * the map won't open: a quick failure, which game.c retries at the
		 * usual interval rather than backing off (HM-1) */
		if (emu_io16(s, 0x4C) != 0 || !(emu_io16(s, 0x00) & 0x0F00)) return "map button did nothing";
		emu_frame_blind(s, K_L);
		emu_frame_blind(s, K_L);
		emu_frame_blind(s, K_L | K_START);
		emu_frame_blind(s, K_L | K_START);
		/* the map's mosaic starts 2 frames after L+START; nothing by 6 is
		 * a conversation, an event, a menu: quick, as above (HM-3) */
		for (int i = 0; i < 6 && emu_io16(s, 0x4C) == 0 && !hm_map_up(s); ++i) emu_frame_blind(s, 0);
		if (emu_io16(s, 0x4C) == 0 && !hm_map_up(s)) return "map button did nothing";
	}
	/* a mosaic out, the map built, a mosaic in (about 70 frames), undrawn:
	 * the glove takes no input before the mosaic's end */
	int f = 0;
	while (f < 150 && !(hm_map_up(s) && emu_io16(s, 0x4C) == 0)) emu_frame_blind(s, 0), ++f;
	if (f >= 150) return "map screen did not appear";
	emu_frame_blind(s, 0);
	/* the glove's fingertip (its right end, 5 rows down) onto the middle
	 * of the head: put there, 2 pixels short, then RIGHT for a frame (the
	 * text box follows the glove only when it's moved); if its place in
	 * RAM isn't found, steered there (2 pixels a frame) */
	int gx, gy, hx, hy;
	if (!hm_find(s, HM_HEAD_TILE, HM_HEAD_PAL, &hx, &hy)) return "no head on the map";
	if (!hm_find(s, HM_GLOVE_TILE, HM_GLOVE_PAL, &gx, &gy)) return "no glove on the map";
	uint32_t var = hm_glove_var(s, gx, gy);
	if (var) {
		emu_write32(s, var, (uint32_t)(hx + 5) << 8);
		emu_write32(s, var + 4, (uint32_t)(hy + 7) << 8);
		emu_frame_blind(s, K_RIGHT);
	} else {
		for (f = 0; f < 200; ++f) {
			if (!hm_find(s, HM_GLOVE_TILE, HM_GLOVE_PAL, &gx, &gy)) return "no glove on the map";
			int dx = hx - 8 - gx, dy = hy + 2 - gy;
			if (dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1) break;
			uint32_t k = (dx < -1 ? K_LEFT : dx > 1 ? K_RIGHT : 0) | (dy < -1 ? K_UP : dy > 1 ? K_DOWN : 0);
			emu_frame_blind(s, k);
		}
	}
	/* the text box redrawn for the new place (2 frames); the glove left out */
	const SpriteRule glove[1] = { { -512, -256, 511, 255, HM_GLOVE_TILE, HM_GLOVE_TILE + 3, -1, 1u << HM_GLOVE_PAL, 0 } };
	emu_hide_sprites(s, glove, 1);
	for (int i = 0; i < HM_DRAWN; ++i) emu_frame(s, 0);
	bool up = hm_map_up(s);
	emu_hide_sprites(s, NULL, 0);
	if (!up) return "the map went away";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ---------------------------------------------------------- Golden Sun
 *
 * The second screen: the Status screen (SELECT's menu, its last icon, A),
 * opened on the shadow core from the live state at every capture, one
 * page per party member (RIGHT turns to the next, round to the first),
 * shown in turn 5 seconds each (GS-1, GS-2). The window colour is the
 * player's choice (the game's settings), so the screens are told apart by
 * memory, not pixels:
 *   GS_FMENU  1 while SELECT's menu or one of its screens is up, else 0
 *             (walking about, in a battle, on the title)
 *   GS_SCREEN 1 while one of the menu's screens (Psynergy, Item, Status)
 *             is up, else 0
 * The menu always opens on its first icon, and its last is Status (LEFT
 * from the first wraps to it), so reaching it by LEFT needs no count of
 * the icons (Djinn come later). Nothing in memory tells the Status from
 * the Psynergy and Item screens (0x03001B62, read before, is a slot of the
 * windows' table and reads one window behind after some battles), so a
 * screen already open is backed out of and the Status opened afresh.
 *
 * In a battle (GS-7), while the player chooses what to do, the battle's
 * own Status (the party menu: Fight, Flee, Status) of the one choosing:
 * B back to the party menu, LEFT to Status, A, R to that character.
 *   GS_INPUT  5 while the player chooses (the menus, the Status), else 0
 *   GS_BSCENE 0x5F walking about and on the title (0xFF for a moment going
 *             through a doorway); in a battle 3 while
 *             it plays out, 6 on its Status, 255 in its menus
 *   GS_BMENU  0 in the party menu, 2 in a character's menu, 3 on the Status
 *             (GS_BSCENE reads 6 or 7 there: not used for it)
 *   GS_WHO    the character choosing (0 the first)
 * The title: its emblem (GS_V_TITLE). */
#define GS_FMENU 0x02030172
#define GS_SCREEN 0x02030E64
#define GS_INPUT 0x0300037A
#define GS_BSCENE 0x03001812
#define GS_BMENU 0x030017C3
/* (Germany: those two 0x10 higher, the others where they are) */
#define GS_GER(e) ((e)->gameCode[3] == 'D' ? 0x10u : 0u)
#define GS_WHO 0x02030343
#define GS_PAGES 8
#define GS_V_TITLE 0xF0000040
#define GS_V_FORGET 0xF0000041 /* the title, or a battle not at the player's choice:
                                * the pictures from before go (GS-4, GS-8) */
static const Probe gsTitle[8] = { { 150, 50, 0x844A29 }, { 165, 53, 0x945A29 }, { 134, 57, 0x734A29 }, { 149, 57, 0xBD734A },
                                  { 164, 60, 0xB5734A }, { 64, 63, 0x8C6B4A }, { 79, 63, 0x9C6339 }, { 94, 63, 0x9C6B4A } };
static uint32_t gs_virt(Emu* e, uint32_t addr) {
	bool title = som_probes(e, gsTitle, 8, 7, 8);
	if (addr == GS_V_TITLE) return title;
	if (addr == GS_V_FORGET) return title || (emu_read8(e, GS_BSCENE + GS_GER(e)) < 0x5F && emu_read8(e, GS_INPUT) != 5); /* (0xFF going through a doorway too: not a battle) */
	return 0;
}

static bool gs_walking(Emu* s) {
	return emu_read8(s, GS_FMENU) == 0 && emu_read8(s, GS_SCREEN) == 0;
}
static bool gs_menu(Emu* s) {
	return emu_read8(s, GS_FMENU) == 1 && emu_read8(s, GS_SCREEN) == 0;
}
/* one of the menu's screens: the Status, when reached by its icon */
static bool gs_status(Emu* s) {
	return emu_read8(s, GS_FMENU) == 1 && emu_read8(s, GS_SCREEN) == 1;
}
/* a battle's party menu, settled (a character's menu passes through 0
 * on the way back to the one before) */
static bool gs_party_menu(Emu* s) {
	int still = 0;
	for (int f = 0; f < 60 && still < 16; ++f) {
		emu_frame_blind(s, 0);
		still = emu_read8(s, GS_BMENU + GS_GER(s)) == 0 ? still + 1 : 0;
	}
	return still >= 16;
}

/* runs up to n frames (not drawn) until up(s) */
static bool gs_wait(Emu* s, bool (*up)(Emu*), int n) {
	for (int f = 0; f < n && !up(s); ++f) emu_frame_blind(s, 0);
	return up(s);
}

/* The battle's Status without its help line (GS-7): "Use the L & R
 * buttons to switch between characters." at 120-152 in the window under
 * the stats (the character's figure ends above it); its letters (white,
 * with black shadows) take the window's colour on their row (x 231,
 * inside its right edge). */
static void gs_no_help(uint32_t* px, int w) {
	if (w < GBA_W) return;
	for (int y = 120; y <= 152; ++y) {
		uint32_t* row = &px[y * w];
		uint32_t bg = row[231];
		if ((bg & 0xFFFFFF) == 0 || (bg & 0xFFFFFF) == 0xFFFFFF) continue;
		for (int x = 4; x < 231; ++x) {
			uint32_t c = row[x] & 0xFFFFFF;
			if (c == 0 || c == 0xFFFFFF) row[x] = bg;
		}
	}
}

/* The Status from walking about without its top-right box (GS-1: the L-R:
 * Rearrange / A: Details prompt): black, as between the game's windows.
 * The box is found by its frame (a black column, then dark grey and white)
 * on line 20 and its bottom (black, then the next box's dark grey) below;
 * left as it is if not found. */
static void gs_no_prompt_box(uint32_t* px, int w) {
	if (w < GBA_W) return;
	int x0 = -1, y1 = -1;
	for (int x = 80; x < 160 && x0 < 0; ++x) {
		uint32_t a = px[20 * w + x - 1] & 0xFFFFFF, b = px[20 * w + x] & 0xFFFFFF, c = px[20 * w + x + 1] & 0xFFFFFF;
		if (a == 0 && b == 0x525252 && c == 0xFFFFFF) x0 = x;
	}
	if (x0 < 0) return;
	for (int y = 24; y < 60 && y1 < 0; ++y) {
		uint32_t a = px[y * w + x0 + 20] & 0xFFFFFF, b = px[(y + 1) * w + x0 + 20] & 0xFFFFFF;
		if (a == 0 && b == 0x525252) y1 = y;
	}
	if (y1 < 0) return;
	for (int y = 0; y <= y1; ++y) {
		for (int x = x0; x < GBA_W; ++x) px[y * w + x] = 0xFF000000;
	}
}

static const char* gs_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[GS_PAGES][GBA_W * GBA_H];
	int max = *nOut < GS_PAGES ? *nOut : GS_PAGES;
	*nOut = 1;
	*step = 300; /* 5 seconds a party member */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (emu_read8(s, GS_INPUT) == 5) {
		/* a battle, the player choosing: the battle's Status of the one choosing (GS-7) */
		int who = emu_read8(s, GS_WHO);
		bool wasStatus = emu_read8(s, GS_BMENU + GS_GER(s)) == 3; /* (back in the menu, its cursor stays on Status) */
		for (int i = 0; i < 6 && !gs_party_menu(s); ++i) aw_run(s, K_B, 6);
		if (!gs_party_menu(s)) return "not in the party menu";
		if (!wasStatus) {
			aw_run(s, K_LEFT, 6); /* (to its last icon: Status) */
			aw_run(s, 0, 12);
		}
		aw_run(s, K_A, 6);
		for (int f = 0; f < 60 && emu_read8(s, GS_BMENU + GS_GER(s)) != 3; ++f) emu_frame_blind(s, 0);
		if (emu_read8(s, GS_BMENU + GS_GER(s)) != 3) return "the battle's Status didn't open";
		aw_run(s, 0, 16);
		for (int i = 0; i < who && i < 8; ++i) aw_run(s, K_R, 6), aw_run(s, 0, 16);
		aw_run(s, 0, 10);
		if (emu_cancelled(s)) return "called off";
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[0][y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		gs_no_help(out[0], w);
		*nOut = 1;
		return NULL;
	}
	if (emu_read8(s, GS_BSCENE + GS_GER(s)) != 0x5F) return "a battle playing out"; /* (the logo: GS-8) */
	/* back to walking about first (from the menu or one of its screens,
	 * the Status too), then SELECT, LEFT to Status, A */
	for (int i = 0; i < 4 && !gs_walking(s); ++i) {
		aw_run(s, K_B, 8);
		gs_wait(s, gs_walking, 60);
	}
	if (!gs_walking(s)) return "not walking about";
	aw_run(s, K_SELECT, 8);
	if (!gs_wait(s, gs_menu, 60)) return "the menu didn't open";
	aw_run(s, 0, 12);
	aw_run(s, K_LEFT, 6); /* (to the last icon: Status) */
	aw_run(s, 0, 16);
	aw_run(s, K_A, 6);
	if (!gs_wait(s, gs_status, 60)) return "the Status didn't open";
	aw_run(s, 0, 20);
	int n = 0;
	for (; n < max; ++n) {
		if (n) {
			aw_run(s, K_RIGHT, 6);
			aw_run(s, 0, 16);
			if (!gs_status(s)) return "the Status closed";
			/* round to the first again: the stats panel (below the party
			 * row, whose walking figures move) as on the first page, but
			 * for a few pixels (once it didn't quite match, and the pages
			 * ran on to the most there can be; another member's differs
			 * by thousands) */
			int diff = 0;
			for (int i = 40 * GBA_W; i < GBA_W * GBA_H; ++i) diff += (s->fb[i] & 0xFFFFFF) != (page[0][i] & 0xFFFFFF);
			if (diff < 100) break;
		}
		if (emu_cancelled(s)) return "called off";
		memcpy(page[n], s->fb, sizeof(page[n]));
	}
	*nOut = n;
	for (int k = 0; k < n; ++k) {
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[k][y * w], &page[k][y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		gs_no_prompt_box(out[k], w);
	}
	return NULL;
}

/* ---------------------------------------- Golden Sun: The Lost Age
 *
 * Golden Sun's second screen (docs/games/golden-sun-2.md): walking about,
 * the Status screen, a page per party member (up to eight); in a battle,
 * while the player chooses, the battle's Status of the one choosing; the
 * logo while a battle plays out and on the title. The menus work as in
 * Golden Sun, the addresses are the game's own:
 *   GS2_MAP    (half) the map: 0 on the title, the naming screens, the
 *              opening's text and the credits; 510 in a battle
 *   GS2_FMENU  1 while SELECT's menu or one of its screens is up
 *   GS2_SCREEN 1 while one of its screens (Psynergy, Djinn, Item, Status)
 *   GS2_INPUT  5 while the player chooses in a battle
 *   GS2_WHO    the character choosing (a character number; 0xFF in the
 *              party menu and on its Status)
 *   GS2_HILITE x of the party menu's highlight (the first sprite in the
 *              game's copy of the sprite table; Fight, Switch, Flee, Status
 *              a row of icons ending at GS2_PSTATUS, Switch left out with
 *              one in the party): the menu's cursor moves round a window
 *              whose memory isn't at a fixed place
 *   GS2_BSTAT  1 while the battle's Status is up
 *   GS2_ORDER  the party's order (8 character numbers; four fight)
 * The other releases: the same layout, their addresses in gs2Addr. */
typedef struct {
	char region;
	uint32_t map, fmenu, screen, input, who, hilite, bstat, order;
} Gs2Addr;
static const Gs2Addr gs2Addr[] = {
	{ 'E', 0x02000420, 0x0203015A, 0x020036E0, 0x02030012, 0x02035EFC, 0x03001382, 0x02035E64, 0x02000458 },
	/* (Germany, France, Italy, Spain: the USA / Europe release's) */
	{ 'J', 0x02000420, 0x0203015A, 0x020036E0, 0x02030012, 0x02035CFC, 0x03001382, 0x02035C64, 0x02000458 },
};
#define GS2_PSTATUS 121
#define GS2_BATTLE 510
#define GS2_V_TITLE 0xF0000050
#define GS2_V_FORGET 0xF0000051 /* the title, or a battle not at the player's choice:
                                 * the pictures from before go (GS2-4, GS2-8) */
/* Sprites left out of the pictures: the walking Status's scroll arrows (the
 * party row's right edge, up 264, down 268, bobbing), the battle Status's
 * hand (tile 20, by the name) */
static const SpriteRule gs2Arrows = { 80, -16, 120, 40, 264, 271, -1 };
static const SpriteRule gs2Hand = { 0, -16, 100, 40, 20, 23, -1 };
/* The windows' corner pixels, see-through in the game (the map or battle
 * behind shows there), black (GS2-1, GS2-7): the walking Status's three
 * windows (the party row, the prompt box, the stats) and the battle
 * Status's one; the same places in every release */
static void gs2_black_corners(uint32_t* px, int w, bool walking) {
	static const uint8_t walk[][2] = { { 0, 0 }, { 103, 0 }, { 104, 0 }, { 239, 0 }, { 0, 39 }, { 103, 39 },
	                                   { 104, 39 }, { 239, 39 }, { 0, 40 }, { 239, 40 }, { 0, 159 }, { 239, 159 } };
	static const uint8_t battle[][2] = { { 0, 0 }, { 239, 0 }, { 0, 159 }, { 239, 159 } };
	if (w < GBA_W) return;
	int n = walking ? 12 : 4;
	for (int i = 0; i < n; ++i) {
		const uint8_t* c = walking ? walk[i] : battle[i];
		px[c[1] * w + c[0]] = 0xFF000000;
	}
}

/* the logo's copy: the save check's veneer (ldr r4, [pc]; bx r4) made
 * to answer one save (GS2-5) */
static const uint8_t gs2SaveCheck[4] = { 0x00, 0x4C, 0x20, 0x47 };
static const uint8_t gs2SaveOne[4] = { 0x01, 0x20, 0x70, 0x47 };
static const Gs2Addr* gs2_addr(Emu* e) {
	for (size_t i = 0; i < sizeof(gs2Addr) / sizeof(gs2Addr[0]); ++i) {
		if (gs2Addr[i].region == e->gameCode[3]) return &gs2Addr[i];
	}
	return &gs2Addr[0];
}
static uint32_t gs2_virt(Emu* e, uint32_t addr) {
	const Gs2Addr* a = gs2_addr(e);
	uint16_t map = emu_read16(e, a->map);
	if (addr == GS2_V_TITLE) return map == 0;
	if (addr == GS2_V_FORGET) return map == 0 || (map == GS2_BATTLE && emu_read8(e, a->input) != 5);
	return 0;
}

static bool gs2_walking(Emu* s) {
	const Gs2Addr* a = gs2_addr(s);
	return emu_read8(s, a->fmenu) == 0 && emu_read8(s, a->screen) == 0;
}
static bool gs2_menu(Emu* s) {
	const Gs2Addr* a = gs2_addr(s);
	return emu_read8(s, a->fmenu) == 1 && emu_read8(s, a->screen) == 0;
}
static bool gs2_status(Emu* s) {
	const Gs2Addr* a = gs2_addr(s);
	return emu_read8(s, a->fmenu) == 1 && emu_read8(s, a->screen) == 1;
}
/* a battle's party menu, settled (a character's menu passes through it
 * on the way back to the one before) */
static bool gs2_party_menu(Emu* s) {
	const Gs2Addr* a = gs2_addr(s);
	int still = 0;
	for (int f = 0; f < 60 && still < 16; ++f) {
		emu_frame_blind(s, 0);
		bool up = emu_read8(s, a->input) == 5 && emu_read8(s, a->who) == 0xFF && emu_read8(s, a->bstat) == 0;
		still = up ? still + 1 : 0;
	}
	return still >= 16;
}

static const char* gs2_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[GS_PAGES][GBA_W * GBA_H];
	const Gs2Addr* a = gs2_addr(s);
	int max = *nOut < GS_PAGES ? *nOut : GS_PAGES;
	*nOut = 1;
	*step = 300; /* 5 seconds a party member */
	emu_hide_sprites(s, NULL, 0); /* (what the last picture left out) */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	uint16_t map = emu_read16(s, a->map);
	if (map == 0) return "the title";
	if (map == GS2_BATTLE) {
		if (emu_read8(s, a->input) != 5) return "a battle playing out"; /* (the logo: GS2-8) */
		/* the player choosing: the battle's Status of the one choosing (GS2-7),
		 * R as many times as their place in the party */
		int who = emu_read8(s, a->who), slot = 0;
		for (int i = 0; i < 8; ++i) {
			if (emu_read8(s, a->order + i) == who) slot = i;
		}
		if (who == 0xFF || slot > 3) slot = 0;
		for (int i = 0; i < 6 && !gs2_party_menu(s); ++i) aw_run(s, K_B, 6);
		if (!gs2_party_menu(s)) return "not in the party menu";
		for (int i = 0; i < 4 && (emu_read16(s, a->hilite) & 0x1FF) != GS2_PSTATUS; ++i) {
			aw_run(s, K_LEFT, 6); /* (round to its last icon: Status) */
			aw_run(s, 0, 12);
		}
		if ((emu_read16(s, a->hilite) & 0x1FF) != GS2_PSTATUS) return "the party menu's cursor isn't on Status";
		aw_run(s, K_A, 6);
		for (int f = 0; f < 60 && emu_read8(s, a->bstat) != 1; ++f) emu_frame_blind(s, 0);
		if (emu_read8(s, a->bstat) != 1) return "the battle's Status didn't open";
		aw_run(s, 0, 16);
		for (int i = 0; i < slot; ++i) aw_run(s, K_R, 6), aw_run(s, 0, 16);
		emu_hide_sprites(s, &gs2Hand, 1); /* (GS2-7) */
		aw_run(s, 0, 10);
		if (emu_cancelled(s)) return "called off";
		if (emu_read8(s, a->bstat) != 1) return "the battle's Status closed";
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[0][y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		gs_no_help(out[0], w);
		gs2_black_corners(out[0], w, false);
		*nOut = 1;
		return NULL;
	}
	/* back to walking about first (from the menu or one of its screens,
	 * the Status too), then SELECT, LEFT to Status, A */
	for (int i = 0; i < 4 && !gs2_walking(s); ++i) {
		aw_run(s, K_B, 8);
		gs_wait(s, gs2_walking, 60);
	}
	if (!gs2_walking(s)) return "not walking about";
	aw_run(s, K_SELECT, 8);
	if (!gs_wait(s, gs2_menu, 60)) return "the menu didn't open";
	aw_run(s, 0, 12);
	aw_run(s, K_LEFT, 6); /* (to the last icon: Status) */
	aw_run(s, 0, 16);
	aw_run(s, K_A, 6);
	if (!gs_wait(s, gs2_status, 60)) return "the Status didn't open";
	emu_hide_sprites(s, &gs2Arrows, 1); /* (GS2-1) */
	aw_run(s, 0, 20);
	int n = 0;
	for (; n < max; ++n) {
		if (n) {
			aw_run(s, K_RIGHT, 6);
			aw_run(s, 0, 16);
			if (!gs2_status(s)) return "the Status closed";
			/* round to the first again: the stats panel as on the first
			 * page, but for a few pixels (as in Golden Sun) */
			int diff = 0;
			for (int i = 40 * GBA_W; i < GBA_W * GBA_H; ++i) diff += (s->fb[i] & 0xFFFFFF) != (page[0][i] & 0xFFFFFF);
			if (diff < 100) break;
		}
		if (emu_cancelled(s)) return "called off";
		memcpy(page[n], s->fb, sizeof(page[n]));
	}
	*nOut = n;
	for (int k = 0; k < n; ++k) {
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[k][y * w], &page[k][y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		gs_no_prompt_box(out[k], w);
		gs2_black_corners(out[k], w, true);
	}
	return NULL;
}

/* ---------------------------------------------------------- Tactics Ogre
 *
 * The second screen (docs/games/tactics-ogre.md). In a battle, on a unit:
 * its details (B on it), three pages, 2.5 seconds each (TO-8); on an
 * empty square, the battle's conditions on black (TO-9). Out of battle
 * (USA): the world map's HELP (TO-10), Organize's details (TO-11). */
/* the unit list (R's menu, View List; Organize's roster): the cursor, its
 * first row in view (it scrolls over 12 units): room keys, so Organize's
 * details follow its roster's cursor */
#define TO_LIST_IDX_ 0x0200A08A
#define TO_LIST_TOP_ 0x0200A09A
#define TO_PAGES 8
/* left out (the buttons don't work there): the details' START:DATA label,
 * two sprites (tiles 1005 and 1009) at the top right, and the arrows that
 * blink in their bottom corners, tile 762 at 0 and 232 x 152 */
static const SpriteRule toHide[2] = { { -64, -16, 239, 159, 1005, 1012, -1 }, { -8, 144, 239, 159, 762, 762, -1 } };

/* the list's background and frames, where no unit, box or START:DATA
 * label is ever drawn (the box and the label move about; a full list,
 * Training's 13 units, fills a third row and starts a fourth from y 152:
 * the two lowest are in the gap between them) */
static const Probe toList[7] = { { 103, 0, 0x6F7346 }, { 137, 16, 0x6F7346 }, { 60, 53, 0x807F46 }, { 100, 100, 0x807F46 },
                                 { 140, 147, 0x807F46 }, { 30, 150, 0x312921 }, { 160, 150, 0x6F7346 } };
static bool to_list_up(Emu* s) {
	return som_probes(s, toList, 7, 6, 8);
}

/* the Winning (or Losing) Condition: its box at the top and the place's
 * at the bottom left, over the field (both pages, both releases, wherever
 * the camera is) */
static const Probe toAims[8] = { { 12, 24, 0xFFFF8C }, { 228, 24, 0xFFFF8C }, { 120, 10, 0x525231 }, { 60, 34, 0xDEE78C },
                                 { 200, 34, 0xDEE78C }, { 14, 140, 0xDEE78C }, { 103, 128, 0x394252 }, { 60, 147, 0x525231 } };
static bool to_aims_up(Emu* s) {
	return som_probes(s, toAims, 8, 7, 8);
}
/* the supply list (Expendables): its box's edges */
static const Probe toSupply[8] = { { 56, 26, 0x101818 }, { 160, 26, 0x525231 }, { 56, 134, 0x101818 }, { 160, 134, 0xA5AD63 },
                                   { 56, 80, 0x101818 }, { 162, 80, 0xFFFF8C }, { 100, 26, 0x525231 }, { 100, 134, 0xA5AD63 } };
static bool to_supply_up(Emu* s) {
	return som_probes(s, toSupply, 8, 7, 8);
}
/* the conditions' prompt (LOSING CONDITION or WINNING CONDITION, BG2,
 * beside the R or L button), at the bottom right: the place's box ends at
 * x 100 */
#define TO_AIM_X0 104
#define TO_AIM_Y0 126
#define TO_AIM_Y1 147

/* the field cursor's square (x, y), and after it (+4) the unit on it: a
 * pointer, 0 on an empty square (both releases). Not the unit's box on
 * the screen: the game moves that about to keep clear of the cursor
 * (bottom right, middle left, top left...). */
#define TO_FIELD_CURSOR(s) ((s)->gameCode[3] == 'J' ? 0x0202B5C0u : 0x0202AB90u)
static bool to_on_unit(Emu* s) {
	return emu_read32(s, TO_FIELD_CURSOR(s) + 4) != 0;
}

/* the title's lettering (TACTICS OGRE; the Japanese release's too) */
#define TO_V_TITLE 0xF0000030
/* 1: Training's empty square, as its last capture found it (the logo, TO-9) */
#define TO_V_NOAIMS 0xF0000031
static volatile uint32_t toNoAimsAt;
/* 1: the logo: out of battle with nothing to take any more (until a
 * picture is taken again, TO-12: a capture failed after the last picture
 * came from the world map or Organize), or the enemy's phase (TO-13) */
#define TO_V_STALE 0xF0000032
/* the battle won: 3 from as the last blow's EXP box clears (Japan: as
 * the stage-clear NAVI message comes up), through Congratulations! and
 * the scene after (0 in a battle, its scenes, out of battle). Japan's
 * 0x9D0 higher, as R's menu's (TO_MENU_UP), found in a victory made on
 * jp-field.ss: Stan set to 1 HP beside Kybard, who hit him */
#define TO_WON(s) ((s)->gameCode[3] == 'J' ? 0x0202B1DDu : 0x0202A80Du)
/* the battle's phase: bit 7 set on the enemy's (129; the player's: 1;
 * 0 out of battle) */
#define TO_PHASE(s) ((s)->gameCode[3] == 'J' ? 0x020018ACu : 0x020011CCu)
static volatile int toStale, toLastWorld, toWasWorld;
/* the enemy's phase seen: the logo until the player's phase's first
 * picture (not the last turn's for a moment) */
static volatile int toAfterEnemy;
/* a room key: Organize's roster cursor and scroll (USA), and the phase,
 * so its turn back to the player's takes a picture at once */
#define TO_V_KEY 0xF0000033
static const Probe toTitle[8] = { { 169, 13, 0xF7C600 }, { 140, 19, 0xDEAD00 }, { 25, 20, 0xBD7300 }, { 169, 20, 0xFFEF00 },
                                  { 62, 22, 0xCE8C00 }, { 91, 26, 0xEFBD00 }, { 132, 26, 0xFFFF5A }, { 13, 27, 0xC68400 } };
static bool to_world(Emu* s);
static uint32_t to_virt(Emu* e, uint32_t addr) {
	if (addr == TO_V_STALE) {
		if (!to_world(e) && (emu_read8(e, TO_PHASE(e)) & 0x80)) toAfterEnemy = 1;
		/* (the battle won: the logo, TO-14; the next battle's first
		 * picture ends it, as after the enemy's phase) */
		{
			/* (and lost: 1, from the leader's fall through Game Over;
			 * Training's end, one team out: 4, from as the last blow's
			 * EXP box clears through Team A (B) Wins!) */
			int over = emu_read8(e, TO_WON(e));
			if (!to_world(e) && (over == 3 || over == 1 || over == 4)) toAfterEnemy = 1;
		}
		return toStale || toAfterEnemy;
	}
	if (addr == TO_V_KEY)
	{
		uint32_t j = e->gameCode[3] == 'J' ? 0x4D4u : 0u; /* (Japan's list 0x4D4 lower) */
		/* (and Organize's roster on screen, so it's taken again at once
		 * when it's back from Organize's own menu, TO-11) */
		return emu_read8(e, TO_LIST_IDX_ - j) | emu_read8(e, TO_LIST_TOP_ - j) << 8 | (emu_read8(e, TO_PHASE(e)) & 0x80) << 9 |
		       (uint32_t)(to_world(e) && to_list_up(e)) << 18;
	}
	if (addr == TO_V_NOAIMS)
		return toNoAimsAt == (0x10000u | emu_read16(e, TO_FIELD_CURSOR(e))) && !to_world(e) && !to_on_unit(e);
	return addr == TO_V_TITLE && som_probes(e, toTitle, 8, 7, 8);
}

static bool to_wait_list(Emu* s, int n) {
	for (int f = 0; f < n && !to_list_up(s); ++f) emu_frame(s, 0);
	return to_list_up(s);
}

/* the unit's details (B on it, TO-8): its frame, a line all one colour
 * along rows 0, 79, 80 and 159 and down columns 119-120 between its
 * lower boxes (the player's yellow, the enemy's green: the colours differ,
 * not the lines), the boxes inside another colour. Nothing else the game
 * shows comes near (the field, the list, R's menu, a black screen: at
 * most half the lines, or the insides the same). */
static bool to_details_up(const uint32_t* fb) {
	uint32_t c = fb[79 * GBA_W + 4] & 0xFFFFFF;
	int same = 0, all = 0;
	for (int x = 0; x < GBA_W; ++x) {
		static const int rows[4] = { 0, 79, 80, GBA_H - 1 };
		for (int k = 0; k < 4; ++k) same += (fb[rows[k] * GBA_W + x] & 0xFFFFFF) == c, ++all;
	}
	for (int y = 81; y < GBA_H - 1; ++y) {
		same += (fb[y * GBA_W + 119] & 0xFFFFFF) == c, ++all;
		same += (fb[y * GBA_W + 120] & 0xFFFFFF) == c, ++all;
	}
	return same * 10 >= all * 8 && (fb[40 * GBA_W + 60] & 0xFFFFFF) != c && (fb[120 * GBA_W + 60] & 0xFFFFFF) != c &&
	       (fb[120 * GBA_W + 180] & 0xFFFFFF) != c;
}

/* R's menu open on the field: 2 (0 with R pressed anywhere else: a unit
 * picked or acting, the details or the list open, the enemy's turn);
 * Japan's by the same RAM, the 5 after it too */
#define TO_MENU_UP(s) ((s)->gameCode[3] == 'J' ? 0x0202A733u : 0x02029D63u)

/* R's menu, its icon k (from the first), A; false if R's menu didn't
 * open (only from the free field: TO-5) */
static bool to_menu_pick(Emu* s, int k) {
	aw_run(s, K_R, 6);
	aw_run(s, 0, 34);
	if (emu_read8(s, TO_MENU_UP(s)) != 2) return false;
	for (int i = 0; i < k; ++i) aw_run(s, K_RIGHT, 6), aw_run(s, 0, 14);
	aw_run(s, K_A, 6);
	return true;
}

/* bounding box of the pixels of colour c in x0-x1 x y0-y1 of fb */
static bool to_bbox(const uint32_t* fb, uint32_t c, int x0, int y0, int x1, int y1, int r[4]) {
	r[0] = GBA_W, r[1] = GBA_H, r[2] = -1, r[3] = -1;
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			if ((fb[y * GBA_W + x] & 0xFFFFFF) != c) continue;
			if (x < r[0]) r[0] = x;
			if (x > r[2]) r[2] = x;
			if (y < r[1]) r[1] = y;
			if (y > r[3]) r[3] = y;
		}
	}
	return r[2] >= 0;
}

/* The conditions' boxes alone (TO-9): BG3 draws the boxes, outlined in
 * 101818, and BG2 their text and labels, inside them, over the field (BG0,
 * BG1, sprites; windows darken it, so its backdrop isn't one colour). From
 * the frame after with BG3 alone, the outlines' extent: the condition's
 * box at the top (8-231 x 8-39), the place's at the bottom left (x 0-119:
 * the prompt at the bottom right has no box); then BG2 and BG3 drawn,
 * into out. */
static bool to_cut_boxes(Emu* s, uint32_t* out, int r[2][4]) {
	static uint8_t* tmp;
	static size_t tmpSize;
	size_t sz = emu_state_size(s);
	if (sz > tmpSize) free(tmp), tmp = malloc(sz), tmpSize = tmp ? sz : 0;
	if (!tmp || !emu_state_save(s, tmp)) return false;
	emu_layers(s, 0x08);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	bool ok = to_bbox(s->fb, 0x181810, 0, 0, GBA_W - 1, 79, r[0]) && to_bbox(s->fb, 0x181810, 0, 100, 119, GBA_H - 1, r[1]); /* (XBGR) */
	emu_layers(s, 0x0C);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	if (ok) memcpy(out, s->fb, GBA_W * GBA_H * 4);
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	return ok;
}

/* Training's empty squares: no conditions, the logo (TO-9). The capture
 * that finds it notes the field cursor's square; the live game's
 * TO_V_NOAIMS reads 1 while the cursor stays there on no unit. */

/* The hidden copy back at the live game's moment and `extra` frames on,
 * where R's menu opens on the field (TO-5): 0, or (just after the enemy's
 * phase, the player's has its banner up for ~4 seconds and moves the
 * cursor to the leader, R doing nothing meanwhile) up to 4.5 seconds on,
 * so the turn's first picture comes with it (TO-13). -1: not there. */
static int to_field_ready(Emu* s, const void* state) {
	if (!emu_state_load(s, state)) return -1;
	emu_frame(s, 0);
	bool enemy = emu_read8(s, TO_PHASE(s)) & 0x80;
	aw_run(s, K_R, 6);
	aw_run(s, 0, 34);
	if (emu_read8(s, TO_MENU_UP(s)) == 2) return 0;
	if (enemy || !toAfterEnemy) return -1;
	/* (one run on, R every 40 frames, until its menu opens: the guests'
	 * phase, which reads as the player's, costs ~330 frames a try, not a
	 * restart for each) */
	for (int extra = 40; extra <= 280; extra += 40) {
		if (emu_cancelled(s) || (emu_read8(s, TO_PHASE(s)) & 0x80)) return -1;
		aw_run(s, K_R, 6);
		aw_run(s, 0, 34);
		if (emu_read8(s, TO_MENU_UP(s)) == 2) return extra;
	}
	return -1;
}
static bool to_field_back(Emu* s, const void* state, int extra) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (extra) aw_run(s, 0, extra);
	return true;
}

/* an empty square (TO-2, TO-9): one page, the Winning Condition's box at
 * the top, the Losing Condition's at the bottom, the place's in the
 * middle, on black. R's second icon is the Winning Condition (R on it:
 * the Losing Condition); in Training, which has none, it's the supply
 * list: the logo. */
static const char* to_aims(Emu* s, const void* state, int extra, uint32_t* page, int* nOut) {
	static uint32_t win[GBA_W * GBA_H], lose[GBA_W * GBA_H];
	int rw[2][4], rl[2][4];
	bool aims = false, supply = false;
	if (!to_field_back(s, state, extra)) return "state load failed";
	uint32_t at = emu_read16(s, TO_FIELD_CURSOR(s));
	if (!to_menu_pick(s, 1)) return "R's menu didn't open";
	for (int f = 0; f < 90 && !(aims = to_aims_up(s)) && !(supply = to_supply_up(s)); ++f) emu_frame(s, 0);
	if (emu_cancelled(s)) return "called off";
	if (supply) {
		toNoAimsAt = 0x10000u | at;
		return "no conditions here (Training): the logo";
	}
	if (!aims) return "the conditions didn't open";
	aw_run(s, 0, 30);
	if (!to_aims_up(s)) return "the conditions closed";
	if (!to_cut_boxes(s, win, rw)) return "the conditions' boxes weren't found";
	aw_run(s, K_R, 6);
	aw_run(s, 0, 40);
	if (!to_aims_up(s)) return "the losing condition didn't open";
	if (!to_cut_boxes(s, lose, rl)) return "the conditions' boxes weren't found";
	if (emu_cancelled(s)) return "called off";
	/* the page: the winning box where the game has it, the losing box as
	 * far from the bottom, the place centred */
	memset(page, 0, GBA_W * GBA_H * 4);
	int m = rw[0][1];
	for (int y = rw[0][1]; y <= rw[0][3]; ++y)
		memcpy(&page[y * GBA_W + rw[0][0]], &win[y * GBA_W + rw[0][0]], (rw[0][2] - rw[0][0] + 1) * 4);
	int dy = GBA_H - 1 - m - rl[0][3];
	for (int y = rl[0][1]; y <= rl[0][3]; ++y)
		memcpy(&page[(y + dy) * GBA_W + rl[0][0]], &lose[y * GBA_W + rl[0][0]], (rl[0][2] - rl[0][0] + 1) * 4);
	int pw = rw[1][2] - rw[1][0] + 1, ph = rw[1][3] - rw[1][1] + 1;
	int px = (GBA_W - pw) / 2, py = (GBA_H - ph) / 2;
	for (int y = 0; y < ph; ++y) memcpy(&page[(py + y) * GBA_W + px], &win[(rw[1][1] + y) * GBA_W + rw[1][0]], pw * 4);
	/* (round each box's corners: black outside its outline, on every row:
	 * the field's colour showed there) */
	int boxes[3][4] = { { rw[0][0], rw[0][1], rw[0][2], rw[0][3] },
	                    { rl[0][0], rl[0][1] + dy, rl[0][2], rl[0][3] + dy },
	                    { px, py, px + pw - 1, py + ph - 1 } };
	for (int b = 0; b < 3; ++b) {
		for (int y = boxes[b][1]; y <= boxes[b][3]; ++y) {
			uint32_t* row = &page[y * GBA_W];
			int x = boxes[b][0];
			for (; x <= boxes[b][2] && (row[x] & 0xFFFFFF) != 0x181810; ++x) row[x] = 0;
			for (int x1 = boxes[b][2]; x1 > x && (row[x1] & 0xFFFFFF) != 0x181810; --x1) row[x1] = 0;
		}
	}
	*nOut = 1;
	return NULL;
}

/* ------------------------------------------------- out of battle (TO-10, TO-11)
 * 0x02004453: 17 on the world map and in its menus (Organize...), 0 in a
 * battle (Training's too), whether or not a battle came before. Japan's
 * 0x80 lower, its world map's cursor and location 0x470 lower (found on a
 * Japanese world map reached by playing on from jp-victory-field.ss) */
#define TO_WORLD(s) ((s)->gameCode[3] == 'J' ? 0x020043D3u : 0x02004453u)
static bool to_world(Emu* s) {
	/* (Japan's Organize reads 0 there, not 17: there, out of battle is
	 * told by the battle's phase too, 0 out of battle, 1 or 129 in one) */
	return emu_read8(s, TO_WORLD(s)) == 17 || (s->gameCode[3] == 'J' && emu_read8(s, TO_PHASE(s)) == 0);
}
/* the world map's cursor (x, y: where it points), the location it's on
 * (0xFF: none) */
#define TO_WM_CURSOR_ 0x03000ECEu
#define TO_WM_HOVER_ 0x03000EC2u
#define TO_WM_CURSOR (TO_WM_CURSOR_ - ((s)->gameCode[3] == 'J' ? 0x470u : 0u))
#define TO_WM_HOVER (TO_WM_HOVER_ - ((s)->gameCode[3] == 'J' ? 0x470u : 0u))
/* the HELP box SELECT opens on a location: 8-231 x 16-63, its outline */
#define TO_HELP_X0 8
#define TO_HELP_Y0 16
#define TO_HELP_X1 231
#define TO_HELP_Y1 63
/* the world map itself: its lion and the location's box at the bottom
 * right (both releases; not a shop, whose own HELP SELECT would bring
 * up: TO-12) */
static const Probe toWorldMap[12] = { { 150, 120, 0x8770BD }, { 215, 122, 0xA1B742 }, { 185, 125, 0x211000 }, { 155, 128, 0xD3D94A },
                                      { 215, 130, 0xDE9C00 }, { 185, 133, 0x393900 }, { 155, 136, 0x393900 }, { 222, 139, 0xF7EF00 },
                                      { 237, 145, 0x393900 }, { 233, 151, 0x393900 }, { 203, 154, 0x393900 }, { 173, 157, 0x211000 } };
static bool to_help_up(const uint32_t* fb) {
	int n = 0, all = 0;
	for (int x = TO_HELP_X0; x <= TO_HELP_X1; ++x) {
		n += (fb[TO_HELP_Y0 * GBA_W + x] & 0xFFFFFF) == 0x181810; /* (XBGR: 101818) */
		n += (fb[TO_HELP_Y1 * GBA_W + x] & 0xFFFFFF) == 0x181810;
		all += 2;
	}
	for (int y = TO_HELP_Y0; y <= TO_HELP_Y1; ++y) {
		n += (fb[y * GBA_W + TO_HELP_X0] & 0xFFFFFF) == 0x181810;
		n += (fb[y * GBA_W + TO_HELP_X1] & 0xFFFFFF) == 0x181810;
		all += 2;
	}
	return n * 10 >= all * 9 && (fb[30 * GBA_W + 120] & 0xFFFFFF) != 0x181810;
}

/* the world map (TO-10): the HELP box of the location the cursor is on,
 * or (on none) of the player's own: the cursor put on the army's flag
 * (sprite tiles 528-536, palette 2), 9 right of it and 23 down, where the
 * map opens it; SELECT; the box alone, in the middle, on black */
static const char* to_world_map(Emu* s, uint32_t* page, int* nOut) {
	/* (the world map itself under the HELP box too: the opening's
	 * birthdate question, before the world map, has a box of its shape) */
	if (!som_probes(s, toWorldMap, 12, 10, 8)) return "not the world map";
	if (!to_help_up(s->fb)) {
		if (emu_read8(s, TO_WM_HOVER) == 0xFF) {
			uint16_t oam[512];
			emu_oam_read(s, oam);
			int fx = -1, fy = -1;
			for (int i = 0; i < 128; ++i) {
				uint16_t a0 = oam[i * 4], a1 = oam[i * 4 + 1], a2 = oam[i * 4 + 2];
				if ((a0 & 0x300) == 0x200) continue; /* (hidden) */
				int tile = a2 & 0x3FF, pal = a2 >> 12;
				if (tile >= 528 && tile <= 536 && pal == 2) {
					fx = a1 & 0x1FF, fy = a0 & 0xFF;
					break;
				}
			}
			if (fx < 0 || fx > 239 - 9 || fy > 159 - 23) return "not on a location, and no flag";
			emu_write8(s, TO_WM_CURSOR, fx + 9);
			emu_write8(s, TO_WM_CURSOR + 1, fy + 23);
			aw_run(s, 0, 10);
			if (emu_read8(s, TO_WM_HOVER) == 0xFF) return "the flag's location wasn't found";
		}
		aw_run(s, K_SELECT, 6);
		for (int f = 0; f < 60 && !to_help_up(s->fb); ++f) emu_frame(s, 0);
		aw_run(s, 0, 10);
		if (!to_help_up(s->fb)) return "no HELP box";
	}
	if (emu_cancelled(s)) return "called off";
	memset(page, 0, GBA_W * GBA_H * 4);
	int h = TO_HELP_Y1 - TO_HELP_Y0 + 1, y0 = (GBA_H - h) / 2;
	for (int y = 0; y < h; ++y)
		memcpy(&page[(y0 + y) * GBA_W + TO_HELP_X0], &s->fb[(TO_HELP_Y0 + y) * GBA_W + TO_HELP_X0], (TO_HELP_X1 - TO_HELP_X0 + 1) * 4);
	*nOut = 1;
	return NULL;
}

/* A unit's details, up (B on the field, A in Organize): their three
 * pages: as they open (equipment, talents and their POW), START (the
 * talents' MP costs), RIGHT (the Emblems, which slide in over ~40 frames;
 * from POW: back there with START first) (TO-8, TO-11) */
static const char* to_detail_pages(Emu* s, uint32_t (*page)[GBA_W * GBA_H], int max, int* nOut) {
	static const int keys[3] = { 0, K_START, K_RIGHT };
	int n = 0;
	for (; n < 3 && n < max; ++n) {
		if (keys[n]) {
			if (n == 2) aw_run(s, K_START, 4), aw_run(s, 0, 14);
			aw_run(s, keys[n], 4);
			aw_run(s, 0, n == 2 ? 50 : 20);
		}
		if (!to_details_up(s->fb) && n < 2) return "the details closed";
		if (emu_cancelled(s)) return "called off";
		memcpy(page[n], s->fb, sizeof(page[n]));
	}
	*nOut = n;
	return NULL;
}

/* the details opening after the key that opens them: up after ~35 frames,
 * then a fade of 6 */
static bool to_wait_details(Emu* s) {
	bool up = false;
	for (int f = 0; f < 90 && !(up = to_details_up(s->fb)); ++f) emu_frame(s, 0);
	if (!up) return false;
	aw_run(s, 0, 16);
	return to_details_up(s->fb);
}

/* Organize (TO-11): the details of the unit the roster's cursor is on (A).
 * Opened in the game: back (B) to the roster and in again, so they start
 * on the first page. */
static const char* to_organize(Emu* s, uint32_t (*page)[GBA_W * GBA_H], int max, int* nOut) {
	if (to_details_up(s->fb)) {
		aw_run(s, K_B, 6);
		if (!to_wait_list(s, 90)) return "the roster didn't come back";
		aw_run(s, 0, 10);
	}
	aw_run(s, K_A, 6);
	if (!to_wait_details(s)) {
		/* (the roster's cursor still moving, or its page turning: A not
		 * taken yet; once more when it's settled, rather than the logo for
		 * a moment at every move, TO-12) */
		if (!to_wait_list(s, 30)) return "the details didn't open";
		aw_run(s, 0, 20);
		aw_run(s, K_A, 6);
		if (!to_wait_details(s)) return "the details didn't open";
	}
	return to_detail_pages(s, page, max, nOut);
}

static const char* to_compose_(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h);
/* (a picture taken: Training's logo is over, TO_V_NOAIMS) */
static const char* to_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	toWasWorld = 0;
	const char* err = to_compose_(s, state, out, nOut, step, w, h);
	if (!err) toNoAimsAt = 0, toStale = 0, toAfterEnemy = 0, toLastWorld = toWasWorld;
	else if ((toWasWorld || toLastWorld) && !emu_cancelled(s)) toStale = 1;
	return err;
}
static const char* to_compose_(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[TO_PAGES][GBA_W * GBA_H];
	int max = *nOut < TO_PAGES ? *nOut : TO_PAGES;
	*nOut = 1;
	*step = 150; /* 2.5 seconds a page */
	emu_hide_sprites(s, toHide, 2);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (to_world(s)) {
		toWasWorld = 1;
		/* out of battle: Organize (its roster, or a unit's details open),
		 * else the world map */
		int n = 0;
		/* (the roster between two of its pages, or a screen on its way:
		 * a moment for it, rather than the logo, TO-12) */
		for (int f = 0; f < 30 && !to_list_up(s) && !to_details_up(s->fb) &&
		                som_probes(s, toWorldMap, 12, 10, 8) == false; ++f)
			emu_frame(s, 0);
		const char* err = to_list_up(s) || to_details_up(s->fb) ? to_organize(s, page, max, &n) : to_world_map(s, page[0], &n);
		if (err) return err;
		*nOut = n;
		for (int k = 0; k < n; ++k) {
			for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[k][y * w], &page[k][y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		}
		return NULL;
	}
	/* a battle: the list open in the game (or anything else where R's menu
	 * doesn't open on the field): the last pictures (TO-5) */
	if (to_list_up(s)) return "the list is open";
	/* the field fading in (after the details, the list or a scene close:
	 * BLDCNT darkening, 0x00FF): B there wouldn't open the details yet. The
	 * last pictures stay; the next refresh takes them (TO-5). */
	if ((emu_io16(s, 0x50) & 0xC0) >= 0x80) return "the field is fading in";
	/* (the box comes 2 frames after the darkening ends: 4 more) */
	aw_run(s, 0, 4);
	int n = 0;
	const char* err;
	int extra = to_field_ready(s, state);
	if (extra < 0) return "R's menu didn't open";
	if (!to_field_back(s, state, extra)) return "state load failed";
	/* (the unit on the cursor's square: 0 while a screen closes, until the
	 * field is back; R's menu opened on it, so a few frames on it is) */
	for (int f = 0; f < 40 && !to_on_unit(s); ++f) emu_frame(s, 0);
	bool onUnit = to_on_unit(s);
	if (!to_field_back(s, state, extra)) return "state load failed";
	if (!onUnit) {
		/* an empty square: the battle's conditions (TO-9) */
		err = to_aims(s, state, extra, page[0], &n);
	} else {
		/* a unit: B on it (TO-8) */
		aw_run(s, K_B, 6);
		if (!to_wait_details(s)) return "the details didn't open";
		err = to_detail_pages(s, page, max, &n);
	}
	if (err) return err;
	*nOut = n;
	for (int k = 0; k < n; ++k) {
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[k][y * w], &page[k][y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	}
	return NULL;
}

/* Lady Sia: the level the game is in or loading, the byte at 0x03000B50
 * (tcrf.net/Lady_Sia: held before a stage loads, it picks the stage):
 * 0x10-0x33 the levels (0x2E and 0x33 unused), below the title, the
 * menus and story scenes, from 0x34 the maps between levels and the
 * ending's scenes */
#define LS_LEVEL 0x03000B50
#define LS_PAUSE 0x03001545 /* 5 with the pause screen up, 3 in a level or the menus */
#define LS_V_OFFLEVEL 0xF0000060 /* 1 outside the levels: the logo (LS-3) */
#define LS_V_PLAY 0xF0000061 /* 1 in play: in a level (or the jail's ID, which comes
                              * while a level's last life is still being lost,
                              * the lives up), not paused: the HUD hidden (LS-6) */
#define LS_JAIL 0x3B
/* a SpriteRule's shapes bit: GBA sprite shape (0 square, 1 wide, 2 tall) and size */
#define LS_SHAPE(shape, size) (1u << ((shape) * 4 + (size)))
static uint32_t ls_virt(Emu* e, uint32_t addr) {
	if (addr == LS_V_OFFLEVEL) {
		uint8_t l = emu_read8(e, LS_LEVEL);
		return l < 0x10 || l > 0x33;
	}
	if (addr == LS_V_PLAY) {
		uint8_t l = emu_read8(e, LS_LEVEL);
		return ((l >= 0x10 && l <= 0x33) || l == LS_JAIL) && emu_read8(e, LS_PAUSE) == 3;
	}
	return 0;
}

/* Wario Land 4 (both releases): the screen the game is on, 0x03000C3A
 * (0 scenes, 1 the hub, a passage's level select, the vortex and tally,
 * 2 in a level, 4 the pause screen up, 9 the file screen), what it's
 * doing there, 0x03000C3C (2 playing), and the level, 0x03000003 (0-3;
 * 4 a boss's corridor and fight, whose pause is a plain PAUSE) */
#define WL4_SCREEN 0x03000C3A
#define WL4_SUB 0x03000C3C
#define WL4_LEVEL 0x03000003
#define WL4_HEARTS 0x03001910
/* 1 outside the levels, or once Wario has lost his last heart: the logo
 * (WL4-7) */
#define WL4_V_OFFLEVEL 0xF0000067
/* 1 while the HUD is left off the game screen: in a level's play, Wario
 * alive (WL4-4) */
#define WL4_V_HUDOFF 0xF0000069
/* 1 while a boss fight's countdown is in its place: its figures, the
 * sprite with tile 128 in palette 4, at (96, 2). The fight opens with them
 * big in the middle of the room and the stopwatch bouncing about, then
 * slides them up to there (WL4-9). */
#define WL4_V_CLOCKSET 0xF0000068
static uint32_t wl4_virt(Emu* e, uint32_t addr) {
	if (addr == WL4_V_CLOCKSET) {
		for (int i = 0; i < 128; ++i) {
			uint16_t a0 = emu_read16(e, 0x07000000 + i * 8);
			uint16_t a1 = emu_read16(e, 0x07000000 + i * 8 + 2);
			uint16_t a2 = emu_read16(e, 0x07000000 + i * 8 + 4);
			if ((a0 & 0x0300) == 0x0200) continue; /* (not drawn) */
			if ((a2 & 0x3FF) == 128 && a2 >> 12 == 4 && (a0 & 0xFF) == 2 && (a1 & 0x1FF) == 96) return 1;
		}
		return 0;
	}
	if (addr == WL4_V_OFFLEVEL) {
		uint16_t s = emu_read16(e, WL4_SCREEN);
		if (s != 2 && s != 4) return 1;
		return emu_read8(e, WL4_LEVEL) > 3 || emu_read8(e, WL4_HEARTS) == 0;
	}
	if (addr == WL4_V_HUDOFF)
		return emu_read16(e, WL4_SCREEN) == 2 && emu_read8(e, WL4_LEVEL) <= 3 && emu_read8(e, WL4_HEARTS) != 0;
	return 0;
}

/* ---------------------------------------------------------- Mega Man Battle Network
 *
 * Walking about, the second screen is MegaMan's status and money in one
 * frame (MMBN-2): START's menu opened on the shadow core, its money counter
 * taken (inside its orange frame); then the MegaMan screen (MegaMan), its
 * panel (rows 0-95) taken with the cursor arrow left out. Put together:
 * the counter's top edge across the screen, the counter at its place across
 * (top right), the panel right below it, the counter's bottom edge, the
 * whole centred top to bottom on black. In a battle the custom screen (BG3
 * and its cursor) moves to the second screen while it's up (MMBN-4: the
 * profile's hud). Addresses from the Silenthal/bn1 disassembly (USA;
 * Europe and Japan the same). */
#define MMBN_STATE 0x02006CB8   /* gManager.gameState: 4 walking about, 8 battle, 0x18 a PET menu */
#define MMBN_AREA 0x02000214    /* gWorld.currentArea (halfword) */
#define MMBN_HP 0x0200022C      /* gWorld.hpCurrent, then hpMax */
#define MMBN_ZENNY 0x02000284   /* gWorld.zenny (word) */
#define MMBN_SMENU 0x0200ACF0   /* gStartMenu: state (+0, 4 open), cursor (+6) */
#define MMBN_MENU 0x020062E0    /* gMenu: which (+0, 8 MegaMan), step (+1, 4 open) */
#define MMBN_FADING 0x02006CB3  /* gFadeSettings.isFadeActive */
#define MMBN_BATTLE 0x02003710  /* gBattle: its step (+1: 4 the custom screen, 8 fighting) */
#define MMBN_PANEL_ROWS 96      /* the MegaMan screen's panel; its message box below */
/* START's money counter: x 168-239, rows 0-31 (at the screen's right edge,
 * with no right end); its outline and orange frame rows 0-3 and 28-31 and
 * columns 170-175, inside it x 176-239, rows 4-27 */
#define MMBN_MONEY_X 176
#define MMBN_MONEY_Y 4
#define MMBN_MONEY_H 24
#define MMBN_EDGE 4             /* the frame's top (outline, orange) and bottom rows */
#define MMBN_GLOW_X 176         /* its edges' glow, x 176-239 (64 wide), with plain orange either side */
#define MMBN_PLAIN_X 172        /* a plain column of its edges */
/* the panel's frame and grid, the same with any stats (not the values,
 * the armor or the silhouette) */
static const Probe mmbnPanel[8] = { { 5, 5, 0x4A5A63 }, { 120, 3, 0x4A5A63 }, { 238, 10, 0x4A5A63 }, { 2, 50, 0x313939 },
                                    { 10, 30, 0x313939 }, { 160, 60, 0x313939 }, { 230, 90, 0x313939 }, { 120, 92, 0x7B8C84 } };
/* the money counter's outline, frame, glow and inside (not the amount) */
static const Probe mmbnMoney[8] = { { 170, 0, 0x313939 }, { 170, 31, 0x313939 }, { 172, 2, 0xCE5A39 }, { 172, 29, 0xCE5A39 },
                                    { 200, 1, 0xF78C00 }, { 200, 28, 0xF78C00 }, { 180, 5, 0x4A5A63 }, { 180, 25, 0x5A6B6B } };

static void mmbn_tap(Emu* s, uint32_t keys) {
	/* (a press held 2 frames or more: 1 is missed) */
	aw_run(s, keys, 3);
	aw_run(s, 0, 5);
}
static bool mmbn_megaman_up(Emu* s) {
	return emu_read8(s, MMBN_STATE) == 0x18 && emu_read8(s, MMBN_MENU) == 8 && emu_read8(s, MMBN_MENU + 1) == 4 &&
	       !emu_read8(s, MMBN_FADING);
}
static const char* mmbn_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (w != GBA_W || h != GBA_H) return "not a GBA screen";
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	if (emu_read8(s, MMBN_STATE) != 4) return "not walking about";
	mmbn_tap(s, K_START);
	int f = 0;
	while (f < 40 && emu_read8(s, MMBN_SMENU) != 4) emu_frame_blind(s, 0), ++f;
	if (emu_cancelled(s)) return "called off";
	if (emu_read8(s, MMBN_SMENU) != 4) return "START opened nothing";
	aw_run(s, 0, 3); /* (drawn) */
	if (emu_cancelled(s)) return "called off";
	/* the counter's frame rows and inside, kept: from a frame drawn with
	 * the menu's layer (BG0) alone (whatever is behind the counter, the
	 * net's paths, a Navi walking by, shows through it in places) */
	static uint8_t tmp[0x80000];
	if (emu_state_size(s) > sizeof(tmp) || !emu_state_save(s, tmp)) return "state save failed";
	emu_layers(s, 0x01);
	emu_state_load(s, tmp); /* (the layers take effect as the display registers are loaded) */
	emu_frame(s, 0);
	static uint32_t money[32 * GBA_W];
	memcpy(money, s->fb, sizeof(money));
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	if (emu_cancelled(s)) return "called off";
	for (int i = 0; i < 8; ++i) {
		uint32_t c = money[mmbnMoney[i].y * GBA_W + mmbnMoney[i].x], want = mmbnMoney[i].rgb;
		if (abs((int) (c & 0xFF) - (int) (want >> 16)) > 8 || abs((int) ((c >> 8) & 0xFF) - (int) ((want >> 8) & 0xFF)) > 8 ||
		    abs((int) ((c >> 16) & 0xFF) - (int) (want & 0xFF)) > 8)
			return "no money counter";
	}
	for (int i = 0; i < 8 && emu_read8(s, MMBN_SMENU + 6) != 2; ++i)
		mmbn_tap(s, emu_read8(s, MMBN_SMENU + 6) < 2 ? K_DOWN : K_UP);
	if (emu_read8(s, MMBN_SMENU + 6) != 2) return "MegaMan not reached in the menu";
	mmbn_tap(s, K_A);
	f = 0;
	while (f < 90 && !mmbn_megaman_up(s)) emu_frame_blind(s, 0), ++f;
	if (emu_cancelled(s)) return "called off";
	if (!mmbn_megaman_up(s)) return "the MegaMan screen didn't open";
	aw_run(s, 0, 4); /* (its values printed) */
	if (emu_cancelled(s)) return "called off";
	if (!som_probes(s, mmbnPanel, 8, 8, 8)) return "not the MegaMan screen";
	/* the panel, then the cursor arrow (BG2, left of the armor and the
	 * stats) from a frame drawn without BG2 there: the panel's grid (BG1) */
	static uint32_t panel[MMBN_PANEL_ROWS * GBA_W];
	memcpy(panel, s->fb, sizeof(panel));
	if (!emu_state_save(s, tmp)) return "state save failed";
	emu_layers(s, 0x1B);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	for (int y = 20; y < MMBN_PANEL_ROWS; ++y) {
		for (int x = 112; x <= 139; ++x) panel[y * GBA_W + x] = s->fb[y * GBA_W + x];
	}
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	/* the frame: edge, money, panel, edge, centred top to bottom */
	for (int i = 0; i < GBA_W * GBA_H; ++i) out[i] = 0xFF000000;
	int all = 2 * MMBN_EDGE + MMBN_MONEY_H + MMBN_PANEL_ROWS, y0 = (GBA_H - all) / 2;
	int glow0 = (GBA_W - (GBA_W - MMBN_GLOW_X)) / 2; /* (the glow in the middle) */
	for (int e = 0; e < 2; ++e) {
		int from = e ? 32 - MMBN_EDGE : 0, to = e ? y0 + all - MMBN_EDGE : y0;
		for (int r = 0; r < MMBN_EDGE; ++r) {
			const uint32_t* src = &money[(from + r) * GBA_W];
			for (int x = 0; x < GBA_W; ++x) {
				int g = x - glow0;
				out[(to + r) * GBA_W + x] = g >= 0 && g < GBA_W - MMBN_GLOW_X ? src[MMBN_GLOW_X + g] : src[MMBN_PLAIN_X];
			}
		}
	}
	/* the counter's row in the panel's top row's colour (its own lighter
	 * grey behind the amount too), so the two read as one piece */
	uint32_t head = panel[2 * GBA_W + 2], light = money[(MMBN_MONEY_Y + 6) * GBA_W + MMBN_MONEY_X + 2];
	for (int r = 0; r < MMBN_MONEY_H; ++r) {
		uint32_t* o = &out[(y0 + MMBN_EDGE + r) * GBA_W];
		const uint32_t* m = &money[(MMBN_MONEY_Y + r) * GBA_W];
		for (int x = 0; x < GBA_W; ++x) o[x] = x < MMBN_MONEY_X || m[x] == light ? head : m[x];
	}
	memcpy(&out[(y0 + MMBN_EDGE + MMBN_MONEY_H) * GBA_W], panel, sizeof(panel));
	return NULL;
}

/* -------------------------------------------------- Super Mario Advance 3: Yoshi's Island
 *
 * In a level, the status screen SELECT opens (SM3-1); on a world's panel,
 * its score page (SM3-3): sm3_compose, on the hidden copy. The screen the
 * game is on, 0x03006AF6: 0 a level, 6 a world's panel, 0x0A the island
 * map, 1 a bonus game, 2 the file menu and the opening story, 3 the
 * title, 9 the game select, 5 and 8 Mario Bros. */
#define SM3_SCREEN 0x03006AF6
#define SM3_IN_LEVEL 0x00
#define SM3_ON_PANEL 0x06
/* the world the panel shows: (world - 1) * 2 */
#define SM3_WORLD 0x0300633C
/* the panel's hand on the tab row: the tab's place * 2 (world 1 0 ...
 * world 6 10, SCORE 12); 0 on the levels' rows too */
#define SM3_TAB 0x03002E92
#define SM3_TAB_SCORE 12
/* the panel's score page up: 1 from SCORE picked, 2 as the bar slides
 * in and as the cards turn back, 0 once they're back (beside the hand's
 * own fields) */
#define SM3_SCORE_UP 0x03002ECF
/* each level's score (12 bytes a world, 100 = 0x64), and what follows
 * them (the worlds' totals): what the score page shows */
#define SM3_SCORES 0x03006B08
#define SM3_SCORES_LEN 0x68
/* the status screen: BG3 and the sprites (the items), from about 50
 * frames after SELECT (the level fades out, the screen fades in by its
 * palette over the next 35) */
#define SM3_STATUS_DISP 0x1800
#define SM3_STATUS_BG3 0x7A04
/* the item cursor's colour (BG palette 1, colour 14), cycled by the game;
 * where it's drawn, round an item slot, is black otherwise */
#define SM3_CURSOR_PAL 0x0500003C
#define SM3_CURSOR_MAGENTA 0x7C1F
/* the item row, the only place it's drawn: four slots, 24 pixels apart
 * from x 132, the cursor's ring 24 wide over the slot's white frame */
#define SM3_ITEMS_Y0 124
#define SM3_SLOT0_X 132
#define SM3_SLOT_STEP 24
#define SM3_SLOTS 4
/* what the cursor's tiles change on slot 0 (slot k: 24k to the right) */
#define SM3_RING_X0 131
#define SM3_RING_W 26
#define SM3_RING_Y0 131
#define SM3_RING_Y1 156
/* the red arrows beside the slots (more items that way), 8 x 10 on the
 * bar's white, at x 122 and 230 from row 139 */
#define SM3_ARROW_L_X 122
#define SM3_ARROW_R_X 230
#define SM3_ARROW_Y0 139
/* the score page's GBA box with #1 (another menu), at the panel's bottom
 * right in every world and release, painted over with the panel's colour,
 * taken from beside the cards (SM3-3) */
#define SM3_BOX_X0 187
#define SM3_BOX_X1 220
#define SM3_BOX_Y0 59
#define SM3_BOX_Y1 92
#define SM3_PANEL_AT_X 10
#define SM3_PANEL_AT_Y 60
#define SM3_CARD_GREEN 0x08A542 /* (also the bar's) */
/* 1 in a level or on a world's panel: a picture is taken */
#define SM3_V_LIVE 0xF0000070
/* 1 anywhere but a level or a world's panel (the titles, file menu,
 * opening, island map, a bonus game, Mario Bros.): the logo, the last
 * picture forgotten (SM3-4) */
#define SM3_V_MENU 0xF0000071

static uint32_t sm3_virt(Emu* e, uint32_t addr) {
	uint8_t s = emu_read8(e, SM3_SCREEN);
	if (addr == SM3_V_LIVE) return s == SM3_IN_LEVEL || s == SM3_ON_PANEL;
	if (addr == SM3_V_MENU) return s != SM3_IN_LEVEL && s != SM3_ON_PANEL;
	return 0;
}

static bool sm3_rgb(const uint32_t* fb, int x, int y, uint32_t rgb) {
	uint32_t c = fb[y * GBA_W + x];
	return (((c & 0xFF) << 16) | (c & 0xFF00) | ((c >> 16) & 0xFF)) == rgb;
}

/* the score page's total bar in its place (it slides in from the right
 * once the last card has turned): its green and black left edge, and its
 * rim the panel's colour (each world's panel has its own) */
static bool sm3_bar(const uint32_t* fb) {
	uint32_t panel = fb[60 * GBA_W + 10] & 0xFFFFFF;
	return sm3_rgb(fb, 20, 140, SM3_CARD_GREEN) && sm3_rgb(fb, 200, 140, SM3_CARD_GREEN) && sm3_rgb(fb, 16, 140, 0) &&
	       (fb[132 * GBA_W + 12] & 0xFFFFFF) == panel && (fb[132 * GBA_W + 228] & 0xFFFFFF) == panel;
}

static int sm3_green(const uint32_t* fb) {
	int n = 0;
	for (int y = 24; y < 88; y += 2) {
		for (int x = 24; x < 208; x += 2) n += sm3_rgb(fb, x, y, SM3_CARD_GREEN);
	}
	return n;
}

/* a press the game takes (its logic runs every other frame at times):
 * 5 frames down, 10 up */
static void sm3_tap(Emu* s, uint32_t keys, int n) {
	for (int i = 0; i < n; ++i) {
		aw_run(s, keys, 5);
		aw_run(s, 0, 10);
	}
}

static bool sm3_status(Emu* s) {
	return emu_read16(s, 0x04000000) == SM3_STATUS_DISP && emu_read16(s, 0x0400000E) == SM3_STATUS_BG3;
}

/* SM3-1, SM3-2: SELECT; once the status screen is up and its fade-in done
 * (its palette still for 4 frames), without the item cursor and the
 * arrows */
static const char* sm3_status_page(Emu* s, uint32_t* out, int w, int h) {
	if (sm3_status(s)) return "screen already looks like the map";
	sm3_tap(s, K_SELECT, 1);
	int f = 0;
	while (f < 90 && !sm3_status(s)) emu_frame_blind(s, 0), ++f;
	if (!sm3_status(s)) return "map button did nothing";
	uint16_t last[4] = { 0 };
	int still = 0;
	for (f = 0; f < 90 && still < 4; ++f) {
		emu_frame_blind(s, 0);
		if (!sm3_status(s)) return "the status screen closed";
		bool same = true;
		for (int i = 0; i < 4; ++i) {
			uint16_t c = emu_read16(s, 0x05000002 + i * 2);
			same &= c == last[i];
			last[i] = c;
		}
		still = same && last[2] ? still + 1 : 0;
	}
	if (still < 4) return "the status screen never settled";
	/* The cursor cycles blue, magenta, red, yellow, green and back, 8
	 * frames each: the picture is taken while it's magenta, which nothing
	 * else on the screen is (in its yellow, the star, the flower and the
	 * figures would go black with it) */
	for (f = 0; f < 72 && emu_read16(s, SM3_CURSOR_PAL) != SM3_CURSOR_MAGENTA; ++f) {
		emu_frame_blind(s, 0);
		if (!sm3_status(s)) return "the status screen closed";
	}
	if (emu_read16(s, SM3_CURSOR_PAL) != SM3_CURSOR_MAGENTA) return "the status screen's cursor never turned magenta";
	/* The game sets the cursor's colour in its vblank, before drawing:
	 * the frame is drawn twice from the same moment, BG3 alone and
	 * whole, and where BG3 alone has the cursor's colour and the whole
	 * picture too (no item over it), black instead (SM3-2). */
	size_t n = emu_state_size(s);
	uint8_t* st = malloc(n);
	if (!st || !emu_state_save(s, st)) {
		free(st);
		return "out of memory";
	}
	/* (the whole picture from the frame as the game runs on: drawn again
	 * from a loaded state, its sprites can be missing, depending on the
	 * moment) */
	static uint32_t bg3[GBA_W * GBA_H], whole[GBA_W * GBA_H], moved[GBA_W * GBA_H];
	emu_frame(s, 0);
	memcpy(whole, s->fb, sizeof(whole));
	uint16_t cc = emu_read16(s, SM3_CURSOR_PAL);
	emu_layers(s, 0x08);
	emu_state_load(s, st);
	emu_frame(s, 0);
	memcpy(bg3, s->fb, sizeof(bg3));
	emu_layers(s, 0x1F);
	uint32_t r = cc & 31, g = (cc >> 5) & 31, b = (cc >> 10) & 31;
	uint32_t rgb = ((r << 3 | r >> 2) << 16) | ((g << 3 | g >> 2) << 8) | (b << 3 | b >> 2);
	/* the cursor's ring: where BG3 alone has its colour and the whole
	 * picture too (no item over it), in the item row */
	int cx0 = GBA_W, cx1 = -1, cy0 = GBA_H, cy1 = -1;
	for (int y = SM3_ITEMS_Y0; y < GBA_H; ++y) {
		for (int x = SM3_SLOT0_X; x < GBA_W; ++x) {
			if (!sm3_rgb(bg3, x, y, rgb) || !sm3_rgb(whole, x, y, rgb)) continue;
			if (x < cx0) cx0 = x;
			if (x > cx1) cx1 = x;
			if (y < cy0) cy0 = y;
			if (y > cy1) cy1 = y;
		}
	}
	/* The slot under it, as the game draws a slot the cursor isn't on.
	 * The cursor's tiles on slot k change x 131 + 24k to 156 + 24k, rows
	 * 131-156 (a column more than its dots on each side), so two slots'
	 * share their two columns of the separator between them. The cursor
	 * is moved two slots over (no scrolling; the other way if it can't
	 * go that way) and BG3 alone drawn again: in slot c's area, where
	 * that differs, it's the slot as the game draws it (its white frame,
	 * the black round it, the bar's zigzag). Moved only one slot (two or
	 * three items), the shared columns come from another separator with
	 * no cursor by it; with one item it can't move, and the slot comes
	 * from the next one, its left edge from the bar beside it. (The
	 * slots differ at their edges, the first against the bar, so one
	 * slot can't stand in for another whole.) */
	int c = cx1 >= 0 ? (cx0 - SM3_SLOT0_X) / SM3_SLOT_STEP : -1, m = c;
	for (int tries = 0; c >= 0 && m == c && tries < 2; ++tries) {
		bool right = (c < SM3_SLOTS / 2) != (tries == 1);
		emu_state_load(s, st);
		sm3_tap(s, right ? K_RIGHT : K_LEFT, 2);
		if (!sm3_status(s)) {
			free(st);
			return "the status screen closed";
		}
		uint8_t* st2 = malloc(n);
		if (!st2 || !emu_state_save(s, st2)) {
			free(st2);
			break;
		}
		emu_layers(s, 0x08);
		emu_state_load(s, st2);
		emu_frame(s, 0);
		memcpy(moved, s->fb, sizeof(moved));
		emu_layers(s, 0x1F);
		free(st2);
		/* where it went: the other slot whose ring differs (its colour
		 * may change on the frame drawn) */
		int most = 20;
		for (int k = 0; k < SM3_SLOTS; ++k) {
			if (k == c) continue;
			int x0 = SM3_RING_X0 + SM3_SLOT_STEP * k + 2, x1 = x0 + SM3_RING_W - 5, diff = 0;
			for (int y = SM3_RING_Y0; y <= SM3_RING_Y1; ++y) {
				for (int x = x0; x <= x1; ++x) diff += bg3[y * GBA_W + x] != moved[y * GBA_W + x];
			}
			if (diff > most) most = diff, m = k;
		}
	}
	free(st);
	int rx0 = SM3_RING_X0 + SM3_SLOT_STEP * c, rx1 = rx0 + SM3_RING_W - 1;
	int mx0 = SM3_RING_X0 + SM3_SLOT_STEP * m, mx1 = mx0 + SM3_RING_W - 1;
	/* a separator with no cursor by it: not c - 1 | c, nor c | c + 1 */
	int sep = c >= 2 ? 0 : c + 1;
	uint32_t white[2] = { whole[SM3_ARROW_Y0 * GBA_W + SM3_ARROW_L_X - 1], whole[SM3_ARROW_Y0 * GBA_W + SM3_ARROW_R_X + 8] };
	for (int y = 0; y < h && y < GBA_H; ++y) {
		for (int x = 0; x < w && x < GBA_W; ++x) {
			uint32_t px = whole[y * GBA_W + x];
			if (c >= 0 && y >= SM3_RING_Y0 && y <= SM3_RING_Y1 && x >= rx0 && x <= rx1) {
				int sx = x, sy = y;
				const uint32_t* from = whole;
				if (m == c) {
					/* one item: the next slot (where BG3 differs: not
					 * the item inside); the first's left edge, the bar */
					sx = c < SM3_SLOTS - 1 ? x + SM3_SLOT_STEP : x - SM3_SLOT_STEP;
					if (c == 0 && x < SM3_RING_X0 + 2) sx = y < SM3_RING_Y0 + 2 || y > SM3_RING_Y1 - 2 ? x - 8 : SM3_RING_X0 - 1;
					if (bg3[y * GBA_W + x] == bg3[sy * GBA_W + sx]) sx = x;
				} else if (x >= mx0 && x <= mx1) {
					/* the columns shared with where it went: another
					 * separator's */
					int at = (x - (SM3_RING_X0 + SM3_SLOT_STEP)) / SM3_SLOT_STEP;
					sx = x + SM3_SLOT_STEP * (sep - at);
				} else if (bg3[y * GBA_W + x] != moved[y * GBA_W + x]) {
					from = moved;
				}
				px = (from[sy * GBA_W + sx] & 0xFFFFFF) | (px & 0xFF000000u);
			}
			/* the arrows (more items to the left or right): the bar's
			 * white */
			if (y >= SM3_ARROW_Y0 && y < SM3_ARROW_Y0 + 10) {
				if (x >= SM3_ARROW_L_X && x < SM3_ARROW_L_X + 8) px = white[0];
				if (x >= SM3_ARROW_R_X && x < SM3_ARROW_R_X + 8) px = white[1];
			}
			out[y * w + x] = px;
		}
	}
	return NULL;
}

/* SM3-3: UP twice (from either row of levels to the tabs; on the tabs it
 * does nothing), RIGHT to SCORE, A, and the cards turning until the total
 * bar is in; then, of the next 64 frames, the one with the most green
 * (the 100-point cards blink red and green), drawn again with the BG
 * layers alone (the panel, cards, bar and the map's scenery; Yoshi, the
 * levels' markers and the hand are sprites), the #1 box painted over. The
 * same world and scores as last time: the same picture, without the 5
 * seconds of turning cards. */
static const char* sm3_score_page(Emu* s, uint32_t* out, int w, int h) {
	static uint8_t lastKey[SM3_SCORES_LEN + 1];
	static uint32_t lastPage[GBA_W * GBA_H];
	static bool have;
	uint8_t key[SM3_SCORES_LEN + 1];
	key[0] = emu_read8(s, SM3_WORLD);
	for (int i = 0; i < SM3_SCORES_LEN; ++i) key[i + 1] = emu_read8(s, SM3_SCORES + i);
	bool cw = w == GBA_W && h == GBA_H;
	if (have && cw && !memcmp(key, lastKey, sizeof(key))) {
		memcpy(out, lastPage, sizeof(lastPage));
		return NULL;
	}
	if (!sm3_bar(s->fb)) {
		sm3_tap(s, K_UP, 2);
		for (int i = 0; i < 8 && emu_read8(s, SM3_TAB) != SM3_TAB_SCORE; ++i) sm3_tap(s, K_RIGHT, 1);
		if (emu_read8(s, SM3_TAB) != SM3_TAB_SCORE) return "map button did nothing";
		sm3_tap(s, K_A, 1);
		int f = 0, still = 0;
		while (f < 480 && still < 3) {
			aw_run(s, 0, 4);
			f += 4;
			still = sm3_bar(s->fb) ? still + 1 : 0;
		}
		if (still < 3) return "the score page never came up";
	}
	size_t n = emu_state_size(s);
	uint8_t* st = malloc(n);
	uint8_t* best = malloc(n);
	if (!st || !best) {
		free(st);
		free(best);
		return "out of memory";
	}
	int bestGreen = -1;
	for (int f = 0; f < 64; ++f) {
		emu_state_save(s, st);
		emu_frame(s, 0);
		if (!sm3_bar(s->fb)) break;
		int g = sm3_green(s->fb);
		if (g > bestGreen) bestGreen = g, memcpy(best, st, n);
	}
	if (bestGreen < 0) {
		free(st);
		free(best);
		return "the score page closed";
	}
	/* every BG layer, no sprites */
	emu_layers(s, 0x0F);
	emu_state_load(s, best);
	emu_frame(s, 0);
	emu_layers(s, 0x1F);
	free(st);
	free(best);
	uint32_t panel = s->fb[SM3_PANEL_AT_Y * GBA_W + SM3_PANEL_AT_X];
	for (int y = 0; y < h && y < GBA_H; ++y) {
		for (int x = 0; x < w && x < GBA_W; ++x) {
			bool box = x >= SM3_BOX_X0 && x <= SM3_BOX_X1 && y >= SM3_BOX_Y0 && y <= SM3_BOX_Y1;
			out[y * w + x] = box ? panel : s->fb[y * GBA_W + x];
		}
	}
	if (cw && !emu_cancelled(s)) {
		memcpy(lastPage, out, sizeof(lastPage));
		memcpy(lastKey, key, sizeof(key));
		have = true;
	}
	return NULL;
}

static const char* sm3_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	uint8_t screen = emu_read8(s, SM3_SCREEN);
	if (screen == SM3_IN_LEVEL) return sm3_status_page(s, out, w, h);
	if (screen == SM3_ON_PANEL) return sm3_score_page(s, out, w, h);
	return "map button did nothing";
}

static const MapProfile profiles[] = {
	{
		.codes = { "A2CE" },
		.title = "Castlevania: Aria of Sorrow",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* dark purple background in the corners (3 of 4, so a room in a corner can't break it) */
		.probes = { { 1, 1, 0x29007B }, { 238, 1, 0x29007B }, { 1, 158, 0x29007B }, { 238, 158, 0x29007B } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x29007B, .minForeground = 12,
		.mem = { { 0x03002C68, 0xFF, 0x02, 1 } },
		.logo = { .wait = 1700, .layer = 1, .maxY = 160 },
		/* name entry screen: power on, Start through the title, A (Game Start), A (new file) */
		.frame = { .steps = { { 0, 900 }, { K_START, 3 }, { 0, 9 }, { K_START, 3 }, { 0, 69 }, { K_A, 3 }, { 0, 49 },
		                      { K_A, 3 }, { 0, 69 } },
		           .layer = 1, .t = 8, .p = 8, .x0 = 0, .y0 = 40, .x1 = 232, .y1 = 152 },
		.animFrames = 8, .animStep = 4, .animCycle = 90, /* glow pulse: 84 frames */
	},
	{
		.codes = { "ACHE" },
		.title = "Castlevania: Harmony of Dissonance",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 1, 1, 0x000000 }, { 238, 158, 0x000000 }, { 1, 158, 0x000000 }, { 120, 1, 0x000000 } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x000000, .minForeground = 12,
		.mem = { { 0x03002B28, 0xFF, 0x02, 1 } },
		.logo = { .wait = 1700, .layer = 1, .maxY = 130 },
		/* name entry screen */
		.frame = { .steps = { { 0, 700 }, { K_START, 3 }, { 0, 9 }, { K_A, 3 }, { 0, 39 }, { K_START, 3 }, { 0, 9 },
		                      { K_A, 3 }, { 0, 39 }, { K_START, 3 }, { 0, 9 }, { K_A, 3 }, { 0, 39 }, { K_START, 3 },
		                      { 0, 9 }, { K_A, 3 }, { 0, 90 } },
		           .layer = 1, .t = 8, .p = 8, .x0 = 0, .y0 = 40, .x1 = 232, .y1 = 152, .minLuma = 90 },
		.animFrames = 8, .animStep = 3, .animCycle = 64, /* glow pulse: 64 frames */
	},
	{
		.codes = { "AAME" },
		.title = "Castlevania: Circle of the Moon",
		.open = { { K_SELECT, 4 } },
		.settleMax = 150, .stableFrames = 3, .maxDiff = 160,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 1, 1, 0x000000 }, { 238, 1, 0x000000 }, { 1, 158, 0x000000 }, { 238, 158, 0x000000 } },
		.minProbes = 3, .tolerance = 8,
		.bgColor = 0x000000, .minForeground = 8,
		.mem = { { 0x03000C80, 0xFF, 0x01, 1 } },
		.live = { { 0x03000C80, 0xFF, 0x00, 1 } },
		.logo = { .wait = 700, .layer = 0, .maxY = 120, .blackIsClear = true },
		/* the bevelled boxes of the file menu (same style as name entry) */
		.frame = { .steps = { { 0, 600 }, { K_START, 3 }, { 0, 9 }, { K_START, 3 }, { 0, 400 } },
		           .layer = 1, .t = 4, .p = 1, .x0 = 10, .y0 = 10, .x1 = 98, .y1 = 34 },
		/* the "x.x%" after RATE on the pause screen (fully drawn ~30 frames after
		 * Start); digits only, so it fits beside a fully explored castle */
		.rate = { .open = { { K_START, 4 } }, .wait = 40, .x = 183, .y = 25, .w = 56, .h = 7, .text = 0xEFFFEF },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "BMXE" },
		.title = "Metroid: Zero Mission",
		.open = { { K_START, 4 } },
		.settleMax = 180, .stableFrames = 3, .maxDiff = 300,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 0, 0, 0xCEDECE }, { 239, 0, 0xCEDECE }, { 2, 80, 0x212921 }, { 237, 80, 0x212921 } },
		.tolerance = 8,
		/* the whole pause screen, frame included; map_crop erases its lettering:
		 * the L and R keys, SLEEP, the area name and STATUS (the keys and
		 * plates stay) */
		.glyphs = { { 3, 3, 6, 5, 2, { 0x000000, 0x314231 }, true },     /* L */
		            { 232, 3, 5, 5, 2, { 0x000000, 0x314231 }, true },   /* R */
		            { 13, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true },         /* SLEEP */
		            { 98, 3, 44, 8, -1, { 0xCE21D6 } },              /* area name (animates), */
		            { 99, 3, 1, 8, -1, { 0xE742EF } },               /* with the plate's */
		            { 140, 3, 1, 8, -1, { 0xE742EF } },              /* inner bevel */
		            { 192, 2, 35, 7, 2, { 0xFFFF73, 0xFFBD00 }, true } },      /* STATUS */
		/* left out of the capture: the purple scroll arrows (tiles 512-515),
		 * the play-time clock (916-927) and the corner minimap's own markers
		 * (priority 1, bottom right) */
		.captureHide = { { -256, -128, 255, 159, 512, 515, 3 }, { -256, 130, 255, 159, 916, 927, 3 } },
		/* the corner minimap and its A button are part of the bezel layers:
		 * there the capture shows the map layer (and sprites) on their own */
		.underRect = { { 160, 90, 231, 151 } }, .underLayers = 0x18,
		/* without the minimap's own sprites (priority 1, bottom right; hiding
		 * them all the time stops the map screen from ever settling early on) */
		.underHide = { { 150, 80, 255, 159, 540, 700, 1 } },
		/* the now-blank magenta plates and orange L/R keys, in the grey-green of
		 * the curved pieces beside the area name: edge/shade, body, bevel/highlight */
		.recolor = { { 13, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 97, 2, 46, 10, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 192, 2, 35, 7, 3, { 0xAD18B5, 0xCE21D6, 0xE742EF }, { 0x212921, 0x314231, 0x637363 } },
		             { 2, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } },
		             { 230, 2, 8, 7, 4, { 0xFF7B00, 0xFFBD00, 0xFFFF73, 0xFFFFFF }, { 0x212921, 0x314231, 0x637363, 0x94A594 } } },
		/* SELECT / CHANGE AREAS (when the map has more than one area):
		 * covered by a raised piece of the bezel bar */
		.tab = { .x0 = 101, .x1 = 136, .top = 146, .base = 152, .n0 = 89, .n1 = 150, .bottom = 159, .n = 9,
		         .colors = { 0x000000, 0xFFFFFF, 0xCEDECE, 0xCEDECE, 0x94A594, 0x637363, 0x637363, 0x314231, 0x000000 } },
		.mem = { { 0x03000C70, 0xFF, 0x05, 1 } },
		.live = { { 0x03000C70, 0xFF, 0x04, 1 } },
		.logo = { .wait = 200, .pressStart = true, .wait2 = 200, .layer = 1, .maxY = 104 },
		/* the HUD mini-map in the top-right corner: a 32x32 frame (tile 220)
		 * and the blinking 8x8 position marker, both drawn on top (priority 0) */
		.hideSprites = { { 200, -16, 239, 24, 160, 255, 0 } },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "AMTE" },
		.title = "Metroid Fusion",
		.open = { { K_START, 4 } },
		.settleMax = 180, .stableFrames = 3, .maxDiff = 640,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 0, 0, 0x9C9C9C }, { 239, 0, 0x9C9C9C }, { 0, 159, 0x9C9C9C }, { 239, 159, 0x9C9C9C },
		            { 2, 80, 0xFFFFFF }, { 237, 80, 0xFFFFFF } },
		.tolerance = 8,
		/* the whole pause screen, frame included; map_crop erases its lettering
		 * (the L and R key letters, SLEEP, the area name, STATUS); the
		 * "A OBJECTIVE" prompt is left out (captureHide below) */
		.glyphs = { { 4, 3, 5, 6, 1, { 0x21214A }, true },                      /* L */
		            { 231, 3, 5, 6, 1, { 0x21214A }, true },                    /* R */
		            { 13, 4, 22, 5, 8, PLATE_TEXT, true },                      /* SLEEP */
		            { 203, 4, 26, 5, 8, PLATE_TEXT, true },                     /* STATUS */
		            { 97, 0, 46, 7, -1, { 0xADC6BD } } },                       /* area name */
		/* "A OBJECTIVE" is sprites (A button tile 942, OBJECTIVE 976/980,
		 * priority 2): left out of the capture, so the map under it shows */
		.captureHide = { { 160, 130, 239, 159, 940, 1023, 2 },
		                 /* the yellow scroll arrows (tiles 512-515) */
		                 { -256, -128, 255, 159, 512, 515, 2 },
		                 /* the play-time clock, bottom left (408-415) */
		                 { -256, 130, 255, 159, 408, 415, 3 },
		                 /* and the row of ticked item icons along the top (the
		                  * counters down the left stay) */
		                 { 30, 0, 120, 20, 400, 439, 3 } },
		/* the blank plates and keys, in the bezel's greys */
		.recolor = { { 2, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 196, 1, 42, 10, 13, PLATE_FROM, PLATE_TO },
		             { 96, 0, 48, 8, 1, { 0x21214A }, { 0x5A5A5A } } },
		.gridX = 0, .gridY = 0, .gridCell = 8,
		.mem = { { 0x03000BDE, 0xFF, 0x03, 1 } },
		.live = { { 0x03000BDE, 0xFF, 0x01, 1 } },
		.logo = { .wait = 200, .pressStart = true, .wait2 = 300, .layer = 1, .maxY = 125, .blackIsClear = true,
		          .rev = 1 /* was saved with its black fill */ },
		/* the HUD mini-map in the top-right corner: a 32x32 frame (tile 220)
		 * and the blinking 8x8 position marker, both drawn on top (priority 0) */
		.hideSprites = { { 200, -16, 239, 24, 160, 255, 0 } },
		.animFrames = 8, .animStep = 4,
	},
	{
		.codes = { "AZLE" },
		.title = "The Legend of Zelda: A Link to the Past",
		/* the cartridge also holds Four Swords (multiplayer only), so list it
		 * by the game that has the map */
		.listName = "Legend of Zelda, The - A Link to the Past (USA)",
		.open = { { K_L, 4 } },
		/* on the world map, L again: the zoomed-out whole world, which doesn't
		 * scroll along with Link like the close-up does */
		.open2If = { { 0x03002BDD, 0xFF, 0x07, 1 } }, .open2Delay = 30, .open2 = { { K_L, 4 } },
		.settleMax = 150, .openWithin = 30, .stableFrames = 3, .maxDiff = 300,
		.dimWhenUnavailable = true,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* not still black: dungeon maps can sit on a black screen for a few
		 * frames before fading in (dark rooms), which otherwise looks "settled" */
		.bgColor = 0x000000, .minForeground = 2000, .tolerance = 8,
		/* the game's main mode byte (as on the SNES): 0x0E with sub-mode 7 =
		 * world map, 3 = dungeon map */
		.mem = { { 0x03002BDC, 0xFF, 0x0E, 1 }, { 0x03002BDD, 0xFF, 0x07, 1, true, 0x03 } },
		/* A Link to the Past is running (0x03000010: 2; 1 on the shared menus
		 * and in Four Swords) and in normal play: 9 outdoors, 7 indoors, with
		 * sub-mode 0 (not mid screen transition or closing the map) */
		.live = { { 0x03000010, 0xFF, 0x02, 1 }, { 0x03002BDC, 0xFF, 0x09, 1, true, 0x07 }, { 0x03002BDD, 0xFF, 0x00, 1 } },
		/* save & quit returns to the cartridge's title and file select */
		.menu = { { 0x03000010, 0xFF, 0x01, 1 } },
		/* A Link to the Past's own title (the cartridge's shows Four Swords too):
		 * name a file, pick it, choose the game, wait for the title */
		.logo = { .steps = { { 0, 400 }, { K_A, 4 }, { 0, 60 }, { K_A, 4 }, { 0, 20 }, { K_A, 4 }, { 0, 20 }, { K_A, 4 },
		                     { 0, 20 }, { K_START, 4 }, { 0, 20 }, { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 120 },
		                     { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 200 }, { K_A, 4 }, { 0, 900 } },
		          .layer = 1, .maxY = 130, .blackIsClear = true },
		/* recorded, but always shown still, each blinking thing (the floor
		 * buttons, Link's and the goal's markers, which blink at their own
		 * rates) in its frame where it shows most; the X marking the goal
		 * fades red to white: at its reddest */
		.animFrames = 8, .animStep = 4, .mapStill = true, .stillPick = STILL_RED,
	},
	{
		.codes = { "AVSE" },
		.title = "Sword of Mana",
		/* the Status screen, always: see som_compose */
		.compose = som_compose,
		.virt = som_virt,
		/* the GBA's own screen, scaled as the game screen is */
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		/* the HUD's portrait and HP / MP figures go from the game screen
		 * (the Status screen has them); the charge bar under them stays
		 * (tile 19, row 144) */
		.hideSprites = { { 48, 120, 183, 143, 0, 18, -1, 1u << 0 }, { 54, 126, 58, 130, 0, 1023, -1, 1u << 5 } },
		/* the title screen: the logo again (and no captures there) */
		.menu = { { SOM_V_TITLE, 0xFF, 1, 4 } },
		.live = { { SOM_V_TITLE, 0xFF, 0, 4 } },
		/* title: START past the opening, A past the crystal; the SWORD of MANA
		 * wordmark is BG0 above the PUSH START line */
		.logo = { .steps = { { 0, 700 }, { K_START, 4 }, { 0, 200 }, { K_A, 4 }, { 0, 150 } },
		          .layer = 0, .maxY = 112 },
	},
	{
		/* the battle's unit list, on the field cursor's unit: see to_compose */
		.codes = { "ATOE" },
		.title = "Tactics Ogre: The Knight of Lodis",
		.composePages = to_compose,
		.virt = to_virt,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first page) */
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* the field cursor moved: the list again, on its unit */
		/* (and the world map's location under the cursor, Organize's roster
		 * cursor and its scroll, the phase: TO-10, TO-11, TO-13) */
		.room = { { 0x0202AB90, 0xFFFF, 0, 2 }, { TO_WM_HOVER_, 0xFF, 0, 1 }, { TO_V_KEY, 0, 0, 4 } },
		/* the title: the logo again, no captures (TO-4) */
		.menu = { { TO_V_TITLE, 0xFF, 1, 4 } },
		.live = { { TO_V_TITLE, 0xFF, 0, 4 } },
		/* Training's empty square: the logo (TO-9); out of battle with
		 * nothing to take, and the enemy's phase: the logo (TO-12, TO-13) */
		.logoIf = { { TO_V_NOAIMS, 0xFF, 1, 4 } },
		.logoIfOr = { { TO_V_STALE, 0xFF, 1, 4 } },
		/* the title's lettering (BG2) and The Knight of Lodis (sprites, above
		 * PUSH START), not the emblem behind or OGRE BATTLE SAGA (BG1) */
		.logo = { .steps = { { 0, 480 }, { K_START, 4 }, { 0, 50 } }, .layers = 0x14, .maxY = 90 },
	},
	{
		/* Japan (Tactics Ogre Gaiden): the same in battle (its title's
		 * lettering is the USA one, 外伝 beside it); the field cursor's place
		 * its own (by romxref), R's menu's too (TO_MENU_UP); out of battle
		 * not known (to_world: USA only) */
		.codes = { "ATOJ" },
		.title = "Tactics Ogre: The Knight of Lodis",
		.composePages = to_compose,
		.virt = to_virt,
		.stillPick = STILL_FIRST,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.room = { { 0x0202B5C0, 0xFFFF, 0, 2 }, { TO_WM_HOVER_ - 0x470, 0xFF, 0, 1 }, { TO_V_KEY, 0, 0, 4 } },
		.menu = { { TO_V_TITLE, 0xFF, 1, 4 } },
		.live = { { TO_V_TITLE, 0xFF, 0, 4 } },
		.logoIf = { { TO_V_NOAIMS, 0xFF, 1, 4 } },
		.logoIfOr = { { TO_V_STALE, 0xFF, 1, 4 } },
		.logo = { .steps = { { 0, 480 }, { K_START, 4 }, { 0, 50 } }, .layers = 0x14, .maxY = 90 },
	},
	{
		/* the Status screen, a page per party member: see gs_compose
		 * (the other releases: the same addresses, docs/games/golden-sun.md) */
		.codes = { "AGSE", "AGSF", "AGSI" },
		.title = "Golden Sun",
		.composePages = gs_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first member) */
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.virt = gs_virt,
		/* the title: the logo again, no captures (GS-4) */
		.live = { { GS_V_TITLE, 0xFF, 0, 4 } },
		.menu = { { GS_V_FORGET, 0xFF, 1, 4 } }, /* (GS-4, GS-8) */
		/* a battle playing out: the logo (GS-8) */
		.combat = { { GS_INPUT, 0xFF, 0, 1 }, { GS_BSCENE, 0xFF, 0, 1, true, 0x5E, true } },
		/* the one choosing in a battle: their Status at once */
		.room = { { GS_WHO, 0xFF, 0, 1 }, { GS_INPUT, 0xFF, 0, 1 } }, /* (the one choosing; a turn to choose) */
		/* the emblem with its TM, BG2 over black (GS-5) */
		.logo = { .wait = 420, .layer = 2, .maxY = 160, .blackIsClear = true },
	},
	{
		/* the same for Spain and Japan (codes: a profile takes 4) */
		.codes = { "AGSS", "AGSJ" },
		.title = "Golden Sun",
		.composePages = gs_compose,
		.stillPick = STILL_FIRST,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.virt = gs_virt,
		.live = { { GS_V_TITLE, 0xFF, 0, 4 } },
		.menu = { { GS_V_FORGET, 0xFF, 1, 4 } }, /* (GS-4, GS-8) */
		.combat = { { GS_INPUT, 0xFF, 0, 1 }, { GS_BSCENE, 0xFF, 0, 1, true, 0x5E, true } },
		.room = { { GS_WHO, 0xFF, 0, 1 }, { GS_INPUT, 0xFF, 0, 1 } }, /* (the one choosing; a turn to choose) */
		.logo = { .wait = 420, .layer = 2, .maxY = 160, .blackIsClear = true },
	},
	{
		/* Golden Sun: The Lost Age: Golden Sun's Status screens, a page
		 * per party member: see gs2_compose */
		.codes = { "AGFE" },
		.title = "Golden Sun: The Lost Age",
		.composePages = gs2_compose,
		.stillPick = STILL_FIRST,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.virt = gs2_virt,
		/* the title: the logo again, no captures (GS2-4) */
		.live = { { GS2_V_TITLE, 0xFF, 0, 4 } },
		.menu = { { GS2_V_FORGET, 0xFF, 1, 4 } }, /* (GS2-4, GS2-8) */
		/* a battle playing out: the logo (GS2-8) */
		.combat = { { 0x02030012, 0xFF, 0, 1 }, { 0x02000420, 0xFFFF, GS2_BATTLE, 2 } },
		/* the one choosing in a battle: their Status at once */
		.room = { { 0x02035EFC, 0xFF, 0, 1 }, { 0x02030012, 0xFF, 0, 1 } }, /* (the one choosing; a turn to choose) */
		/* the title's emblem, cut out of its picture of the lighthouse
		 * (GS2-5); the title only comes with a save, so the copy the logo
		 * is taken from has its save check answer "one" (the veneer every
		 * release has at 0x080382F0: movs r0, #1; bx lr) */
		.logo = { .steps = { { 0, 600 }, { K_START, 4 }, { 0, 120 } }, .mask = &logoMaskGS2E,
		          .patch = { { 0x080382F0, 4, gs2SaveCheck, gs2SaveOne } }, .rev = 3 /* (the test builds' logos made again) */ },
	},
	{
		/* the Status screen with the menu's Place bar over its bottom:
		 * see sn_compose */
		.codes = { "AB4E", "AB4J" },
		.title = "Summon Night: Swordcraft Story",
		.composePages = sn_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first character) */
		/* every half second (SN-6) */
		.interval = 30,
		.virt = sn_virt,
		/* the GBA's own screen, scaled as the game screen is */
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		/* the title screen and battles: the logo (SN-3, SN-7; no captures there) */
		.menu = { { SN_V_TITLE, 0xFF, 1, 4 } },
		.live = { { SN_V_TITLE, 0xFF, 0, 4 } },
		/* the sword (BG2), the name (BG3) and its subtitle (sprites): from
		 * row 124 the sword alone, without PRESS START BUTTON (a sprite
		 * across its hilt, faded by blending, so still there without the
		 * sky), above the copyright line (row 147) */
		.logo = { .wait = 800, .pressStart = true, .wait2 = 200, .layers = 0x1C, .maxY = 146, .objMaxY = 124, .rev = 1 },
	},
	{
		/* the table over both screens, live (see profiles.h tall); the
		 * score bar (BG0) on the second screen */
		.codes = { "BPPE" },
		.title = "Pokemon Pinball: Ruby & Sapphire",
		.logoOnly = true,
		.tall = {
			/* on a table: the score bar's BG0 and the table's BG3 set up */
			.on = { { 0x04000008, 0xFFFF, 0x8404, 2 }, { 0x0400000E, 0xFFFF, 0x804B, 2 } },
			.hudBg = 0x01,
			.rows = 432, /* (Ruby's and Sapphire's: the camera's 0-272) */
			.hudRow = 144,
			/* the camera: written each frame by 0x0804DCFA (24.8) and
			 * 0x0804DD0A (row), in catch mode by 0x0804DA54 and 0x0804DA64;
			 * its lowest target 0x164 on both tables (camera row 272). The
			 * game's camera is never changed: the game behaves differently
			 * with things off its screen (catch mode drifted with it held up) */
			.live = { .state = 0x020314E0, .cam = 0x54, .camRow = 0x4E,
			          .nop = { 0x0804DCFA, 0x0804DD0A, 0x0804DA54, 0x0804DA64 },
			          .bound = 0x02031538, .boundFrom = 0x164,
			          .ball = 0x1330, .oam = 0x03005000,
			          /* (BG3's VOFS shadow: S+0x5A, made by 0x0804DBB8 (catch mode
			           * 0x0804DF6A) from the camera's row S+0x4E plus the screen shake
			           * (signed byte S+0x121), the opening sweep's offset (S+0x5FC),
			           * S+0xE6 (pans: catch mode's reveal) and in catch mode S+0x2AC;
			           * other routines too, and the sprites follow them. A copy is
			           * held with its own offsets taken off its camera's row.) */
			          .view = 0x0200B3B6, .viewBg = 3, .shake = 0x121,
			          /* (BG0's map; scrolled to row 80 above the score bar: map rows
			           * 10-27. The catch timer is 8 tiles, a message box 140-240) */
			          .msg = { .map = 0x06002000, .row0 = 10, .rows = 18, .blank = 0x1FF, .min = 40 },
			          /* (the table's opening sweep: its offset at 0x020005FC, running
			           * to 0; the copies wait until it's done. Not 0x020005FB, set
			           * during the sweep but also for a frame when the Wailmer on
			           * Sapphire spits the ball out) */
			          .idle = { { 0x020005FC, 0xFFFF, 0, 2 } },
			          /* BG3's tiles: the whole table stays in EWRAM; 0x08047030
			           * streams a row a frame (DMA3, table 0x086B0010) into a 22-row
			           * ring as the scroll's tile row (S+0x64) changes. The scroll:
			           * BG1-3 VOFS shadows at 0x0200B3AE/B2/B6 and the registers,
			           * S+0x68 (this frame's; S+0x6A gets it next frame, and the
			           * table's lights are drawn by S+0x6A's ranges, 0x0804E814),
			           * S+0x5A, S+0xCC, and the main loop's row + 16 (on the stack).
			           * Missing S+0x68 leaves wrong light tiles in the ring. */
			          .jump = { .ring = 0x06008000, .slots = 22, .slotOff = 10,
			                    .src = { 0x0200FBB0, 0x020030A0 }, .split = 32, .tileRows = 56,
			                    .last = 0x64, .scroll = { 0x5A, 0x68, 0x6A, 0xCC },
			                    .scrollAt = { 0x0200B3AE, 0x0200B3B2, 0x0200B3B6 },
			                    .scrollVofs = 0x0E, .plus16 = 0x03007ED8 } },
		},
		/* the title's logo is BG1, on its own white striped background
		 * (cleared from the edges in), above PRESS START */
		.logo = { .wait = 1440, .layer = 1, .maxY = 140, .lightEdgeClear = true },
	},
	{
		/* the table over both screens, live (see profiles.h tall; the two
		 * releases differ only in their header): the score bar (BG3, rows
		 * 0-23) on the top screen, the message line (BG2, rows 150-159) at
		 * the bottom of the bottom one; BG2's boxes (the pause menu) over
		 * the top screen */
		.codes = { "A3VE", "A3VP" },
		.title = "Sonic Pinball Party",
		.logoOnly = true,
		.tall = {
			/* on a table: the table's BG0 and the rails' BG1 set up */
			.on = { { 0x04000008, 0xFFFF, 0x8C03, 2 }, { 0x0400000A, 0xFFFF, 0x8E0A, 2 } },
			.hudBg = 0x0C,
			.hudRow = 144,
			/* the camera: a row (0x02031AE4) copied each frame from the game's
			 * object (+0x9A) by 0x0800A4BE, and walked down by 0x08009FAC as a
			 * table opens; 0x0801FA30 makes the view from it, clamped to rows
			 * 8 to the table's height (0x030040E0: 416 Sonic, 432 NiGHTS) -
			 * 168, plus the shake (0x0203524E), and streams the table's tiles
			 * into BG0's 22-row ring (VRAM 0x0000-0x57FF, counters
			 * 0x02031AD4-0x02031AE3) through the vblank queue of 0x0800162C.
			 * The view's shadows: 0x03000784 (BG0, BG1: the next frame's),
			 * 0x03002990 (HOFS, VOFS). Shadow sprites at 0x03000E50 */
			.live = { .nop = { 0x0800A4BE, 0x08009FAC },
			          .oam = 0x03000E50, .view = 0x03000784, .viewBg = 0,
			          .carry = { .cam = 0x02031AE4, .camOff = 80, .rowsAt = 0x030040E0, .top = 8, .bottomLess = 8,
			                     .shakeAt = 0x0203524E, .ringLo = 0x0000, .ringHi = 0x8000,
			                     .vars = { { 0x02031AD4, 16 }, { 0x03000784, 4 }, { 0x03002990, 4 } },
			                     .qCount = 0x030004F8, .qEntries = 0x030020B0,
			                     /* (the opening sweep: 0x020319AC is 1) */
			                     .noObj = { 0x020319AC, 0xFF, 1, 1 },
			                     /* the ball's sprite (0x08029D38) is placed by the camera's
			                      * own clamp of the ball's row (0x50 in the middle: the camera
			                      * follows it); in a copy, its row less the copy's view:
			                      * ldrh r5,[r0] (the ball's row); ldr r0,=cam; ldrh r0,[r0];
			                      * subs r5,r5,r0; adds r5,#0x50; r5 &= 0xFFFF; b 0x08029D62 */
			                     .patch = { { 0x08029D3A, 0x4808 }, { 0x08029D3C, 0x8800 }, { 0x08029D3E, 0x1A2D },
			                                { 0x08029D40, 0x3550 }, { 0x08029D42, 0x0428 }, { 0x08029D44, 0x0C05 },
			                                { 0x08029D46, 0xE00C }, { 0x08029D5C, 0x1AE4 }, { 0x08029D5E, 0x0203 } } } },
		},
		/* the title (A for English at the language menu of a game with no
		 * save; with one, A only skips some of the intro): the name (BG1)
		 * on its badge (BG2, down to row 141), without PRESS START
		 * (sprites) or the copyright lines (BG1 from row 137: from row 122
		 * down, the badge alone) */
		.logo = { .steps = { { 0, 300 }, { K_A, 6 }, { 0, 1000 } }, .layers = 0x06, .maxY = 142, .lowFrom = 122, .lowLayers = 0x04, .rev = 1 },
	},
	{
		/* Muppet Pinball Mayhem: the table over both screens, live (see
		 * profiles.h tall; docs/games/muppet-pinball-mayhem.md). The score
		 * bar (BG0, BG1: lines 0-15, in display mode 1 with window 0, where
		 * the table's BG3 doesn't show) stays at the top of the top screen. */
		.codes = { "AMWE", "AMWP" }, /* (the same code and RAM; Europe has a Ubi Soft screen before the title) */
		.title = "Muppet Pinball Mayhem",
		.logoOnly = true,
		.tall = {
			/* on a table: the score bar's BG0 and the table's BG3 (256
			 * colours, 256x512, map 0x0000) set up */
			.on = { { 0x04000008, 0xFFFF, 0x0200, 2 }, { 0x0400000E, 0xFFFF, 0x8083, 2 } },
			.hudBg = 0x03,
			.rows = 359, /* (every table: the camera's 0-215, its view 16 rows above) */
			.hudRow = GBA_H, /* (nothing at the bottom: the score bar stays on the top screen) */
			/* the camera: a row (0x030019C8; BG3VOFS = it - 16, set at the
			 * vblank by 0x080008EA, HOFS 0x030019CA) eased toward 0x120 less
			 * the ball's height (0x08004B4E), clamped to 0-0xD7, its speed at
			 * 0x030019EC. As it moves, 0x08004E40 queues the tile rows coming
			 * into view (BG3's map is fixed; table tile rows far apart share
			 * tiles) into a ring of 64 16-byte entries (source, destination,
			 * size, kind; start 0x03002AA0, count 0x03002A9C) done at the next
			 * vblank (0x080064E8). The sprites are written to OAM by the
			 * vblank code from the objects (no shadow table) */
			.live = { .oam = 0x07000000, .view = 0x030019C8, .viewBg = 3,
			          .carry = { .cam = 0x03001A00, .camOff = 16, .viewOff = 16, .top = -16, .gameFrom = 17,
			                     /* (the game's mode 0x030013B6: 4 in play, 6 a ball's bonus
			                      * count, 3 then 2 the next ball; 5 paused; 7 then 8 the
			                      * game over, through its curtains) */
			                     .plain = { { 0x030013B6, 0xFF, 5, 1 }, { 0x030013B6, 0xFF, 7, 1 }, { 0x030013B6, 0xFF, 8, 1 } },
			                     .ready = { 0x030013B6, 0xFF, 2, 1, true, 0x0C, true }, /* (modes 0 and 1: the table being set up, its tiles still the menus') */
			                     .keepObj = true,
			                     .keepDim = true, /* (its fade out at a game's end: both screens fade) */
			                     .ringLo = 0x2D40, .ringHi = 0x10000,
			                     /* (its camera's row) */
			                     .vars = { { 0x030019C8, 4 } },
			                     .qCount = 0x03002A9C, .qWide = true, .qIndex = 0x03002AA0, .qSlots = 64,
			                     .qEntries = 0x0300269C, .qStride = 16, .qDst = 4, .qSize = 8, .qReload = 0x8000,
			                     .patch = {
			                         /* the camera's target: the halfword at 0x03001A00 (never read
			                          * or written by the game, nor named in its code; game.c puts the
			                          * copy's row there) instead of 0x120 less the ball's height:
			                          * mov r1,r8; ldrh r1,[r1,#0x38]. (Not 0x030019E0: that's the
			                          * hold timer of the pan up the table after a ball is saved, at
			                          * 0x08004D30, which a target there kept running: the copies'
			                          * view froze.) The game's own easing and streaming then move
			                          * the copy there */
			                         { 0x08004B50, 0x4641 }, { 0x08004B52, 0x8F09 }, { 0x08004B54, 0x46C0 },
			                         { 0x08004B56, 0x46C0 }, { 0x08004B58, 0x46C0 },
			                         /* and always that way: not the game's own pans (a ball
			                          * starting or lost: 0x030019E8 clear, b 0x08004CC4) */
			                         { 0x08004B4C, 0x46C0 },
			                         /* and there at once, not eased (the stores of the eased row;
			                          * the streaming sees the move all the same): a copy's target
			                          * moves with the game's view, which its easing would trail */
			                         { 0x08004C58, 0x46C0 }, { 0x08004CAA, 0x46C0 },
			                         /* nor the easing up the table after a ball is saved
			                          * (0x030019E9 set: the view eased from 0x030019D4 by
			                          * 0x08004D76, held by the timer at 0x030019E0), which both
			                          * paths reach at 0x08004CEE: beq 0x08004D7C -> b */
			                         { 0x08004CF4, 0xE042 },
			                         /* the table on lines 0-15 too (under the score bar, which the
			                          * copies leave out): display mode 1's store at line 0 gone, and
			                          * line 16's (0x08000A7A) stores DISPCNT without window 0 (whose
			                          * WININ leaves BG3 out): lsrs r2,r1,#13 (0x2000); bics r0,r2;
			                          * strh r0,[r1]; then BLDALPHA 0x1000 as before: adds r1,#0x52;
			                          * lsrs r0,r1,#14; strh r0,[r1] */
			                         { 0x0800087E, 0x46C0 },
			                         { 0x08000A80, 0x0B4A }, { 0x08000A82, 0x4390 }, { 0x08000A84, 0x8008 },
			                         { 0x08000A86, 0x3152 }, { 0x08000A88, 0x0B88 }, { 0x08000A8A, 0x8008 } } } },
		},
		/* the title (from frame 1650 on USA's, 2050 on Europe's, after its
		 * Ubi Soft screen) is one picture on BG0, the name painted into its
		 * top left corner on a badge of a starburst and three bumpers: that
		 * corner by its mask (tools/make_logo_masks.py), without the sky,
		 * the roller coaster or the Muppets */
		.logo = { .wait = 2300, .mask = &logoMaskAMW, .rev = 1 /* (cut along the official logo's outline: test builds' first cut made again) */ },
	},
	{
		/* Sonic Advance (USA, Europe and Japan (Rev 1): the same sprites,
		 * registers and title steps in each). In a level
		 * (display mode 0 with BG1-3: the zone card, play, the act's
		 * results; not the title or menus), everything on top is sprites:
		 * the zone card (the stripe and SONIC, palette 15; the zone's name
		 * scrolling along its white band, the act, palette 6 from tile
		 * 400), the HUD (rings, score and time top left, palette 6 tiles
		 * 64-127; the lives bottom left, the icon in palette 0, tile 64,
		 * the count palette 6), the character (palette 0, tile 0) and the
		 * results' figures (the HUD's tiles, mid-screen) */
		.codes = { "ASOE", "ASOP", "ASOJ" },
		.title = "Sonic Advance",
		.logoOnly = true,
		/* the title: SONIC ADVANCE (BG2), above the copyright line (A for
		 * English at the language menu of a game with no save, START past
		 * the intro) */
		.logo = { .steps = { { 0, 60 }, { K_A, 4 }, { 0, 900 }, { K_START, 4 }, { 0, 90 } }, .layer = 2, .maxY = 120 },
		.hud = {
			.on = { { 0x04000000, 0x0E07, 0x0E00, 2 } },
			/* the rings, score and time; the lives (icon and count) */
			.objs = { { -16, -16, 110, 26, 64, 127, -1, 1u << 6 },
			          { -16, 130, 48, 159, 64, 127, -1, 1u << 0 | 1u << 6 } },
			/* the zone card (its vertical lettering: tile 64 in palette 15;
			 * the stripe's tiles move with what else is loaded, such as the
			 * demo's label): the backdrop, from the frame its name is
			 * centred (SA-2) */
			.card = { .tile = 64, .pal = 15,
			          .objs = { { -64, -64, 255, 175, 0, 1023, -1, 1u << 15 },
			                    { -64, 110, 255, 175, 400, 1023, -1, 1u << 6 } }, /* (the band: not the demo's DEMO, row 76) */
			          .y0 = 128, .y1 = 143,
			          /* the character's name (four 32x32 sprites at x 20, rows
			           * 0-127, scrolled inside by the game), above the band (SA-2) */
			          .side = { 20, 0, 51, 125 } },
			/* the rings, score and time at the top right; the lives below
			 * them (SA-3) */
			.moves = { { -16, -16, 110, 40, 'R', 236, 'T', 4 },
			           { -16, 130, 48, 159, 'R', 236, 'T', 32 } },
		},
	},
	{
		/* Sonic Advance 2 (USA, Europe and Japan: the same sprites,
		 * registers and title steps). In a level (display mode 0 with
		 * BG1-3, BG0 too but under water: the zone card, play, pause; not
		 * the title, menus or character select), the zone card fills the screen (BG
		 * shapes on the blue backdrop, sprites for the emblem, icons and
		 * name; the swirl by the name: tile 464, palette 8) and the HUD is
		 * sprites: rings, score and time top left (palette 6 from tile
		 * 484), the lives bottom left (the face palette 0, tile 508; the
		 * count palette 6, tile 488). docs/games/sonic-advance-2.md */
		.codes = { "A2NE", "A2NP", "A2NJ" },
		.title = "Sonic Advance 2",
		.logoOnly = true,
		/* the title: SONIC ADVANCE 2 (BG2 alone; the sky BG1, the
		 * copyright lines BG0). From power-on with no save: the language
		 * menu (A), name entry (a letter, START to END, A), the intro */
		.logo = { .steps = { { 0, 60 }, { K_A, 4 }, { 0, 80 }, { K_DOWN, 5 }, { 0, 9 }, { K_RIGHT, 6 }, { 0, 24 },
		                     { K_A, 5 }, { 0, 11 }, { K_START, 7 }, { 0, 32 }, { K_A, 8 }, { 0, 1000 } },
		          .layer = 2, .maxY = 120 },
		.hud = {
			/* in a level: mode 0 with BG1-3 (BG0 goes off with water on
			 * screen, SA2-7; the language menu has BG0-2, the rest mode 1) */
			.on = { { 0x04000000, 0x0E07, 0x0E00, 2 } },
			/* the rings, score and time (in an act tiles 484-547 with
			 * Sonic, Knuckles and Amy, 500-563 with Cream and Tails: the
			 * digits, the ring box and icon; in a boss fight 704-783, not
			 * 64x64: the countdown's START!! is from tile 740;
			 * palette 7: the count flashing red at no rings); the lives
			 * (the face, palette 0: 508, 524 or 728; the count). The
			 * character is palette 0 from tile 0; all at priority 0 (not the
			 * rings' sparkles, palette 7 from tile 760 at priority 1) */
			.objs = { { -16, -16, 160, 32, 480, 563, 0, 1u << 6 | 1u << 7 },
			          { -16, -16, 160, 32, 704, 783, 0, 1u << 6 | 1u << 7, 0, 0xFFFFu & ~(1u << 3) },
			          { -16, 130, 48, 159, 464, 767, 0, 1u << 0 },
			          { -16, 130, 48, 159, 464, 1023, -1, 1u << 6 } },
			/* the zone card: the whole screen, once settled, its emblem face on
			 * with its ring of ticks full (SA2-2) */
			/* the swirl: tile 464 (480 with Cream and Tails), priority 0
			 * (the bosses use palette 8 from tile 464 at priority 2) */
			.card = { .tile = 464, .tileMax = 480, .pal = 8, .prio1 = 0 + 1, .whole = true, .at = 61 },
			/* the rings, score and time along the bottom (SA2-3); the
			 * lives in the bottom right corner, as far in from the right
			 * as they are from the left on the game screen (SA2-4) */
			.moves = { { 0, 0, 239, 40, 0, 0, 'B', 156 },
			           { 0, 120, 60, 159, 'R', 233, 0, 0 } },
		},
	},
	{
		/* Sonic Advance 3 (USA, Europe and Japan: the same sprites,
		 * registers, RAM and title steps). gStageData (the SAT-R/sa3
		 * decompilation's name) at 0x030008A0: +3 the game mode (0 the
		 * main game), +9 the zone, +0xA the act (1 Sonic Factory, 2 the
		 * zone's hub, 3-5 acts 1-3, 7 the boss, 8-9 bonus games), +0xE the
		 * level. Acts and hubs: display mode 0 with BG1-3 (acts and the
		 * factory with window 1 too); bosses mode 1 with BG0-1 and windows
		 * 0-1. The HUD is sprites of priority 0, palettes 6-7, tiles
		 * 448-511 (rings and time top left, the lives bottom left; in a
		 * hub, the keys), plus a hub's zone label (palette 6, tiles from
		 * 620) bottom right. The act's card: black, the zone's name
		 * (palette 9, priority 3, from tile 620 or 622) on a white band
		 * across the middle, with the HUD over it.
		 * docs/games/sonic-advance-3.md */
		.codes = { "B3SE", "B3SP", "B3SJ" },
		.title = "Sonic Advance 3",
		.logoOnly = true,
		/* the title: SONIC ADVANCE 3 (BG1 alone; the copyright lines are
		 * sprites). From power-on with no save: the language menu (A),
		 * name entry (a letter, START to END, A), the intro */
		.logo = { .steps = { { 0, 60 }, { K_A, 3 }, { 0, 69 }, { K_A, 3 }, { 0, 29 }, { K_START, 3 }, { 0, 29 },
		                     { K_A, 3 }, { 0, 2650 } },
		          .layer = 1, .maxY = 110 },
		.hud = {
			/* the main game (not the demo, Time Attack or multiplayer), in
			 * an act, a boss, a hub or the factory (not a bonus game), the
			 * display a level's: mode 0 with BG1-3, or mode 1 with BG1 (a
			 * boss; not the title: BG3 or window 1 on too) */
			.on = { { 0x04000000, 0x0E07, 0x0E00, 2, true, 0x0201 },
			        { 0x04000000, 0x4800, 0x0800, 2, true, 0x4800, true },
			        { 0x030008A3, 0xFF, 0, 1 },
			        { 0x030008AA, 0xFF, 1, 1, true, 7, true } },
			/* the rings, time and lives (and a hub's keys); a hub's zone
			 * label */
			.objs = { { -64, -64, 255, 175, 448, 511, 0, 1u << 6 | 1u << 7 },
			          { -64, -64, 255, 175, 600, 711, 0, 1u << 6 } },
			/* the act's card once the band has settled at the bottom with
			 * the team and the act (SA3-2): drawn again with the BLDY
			 * shadow (gBldRegs + 4) at 16, black but for the band (window
			 * 0), as in the card's first moments, without the other
			 * sprites (priority 1-2; the HUD; at priority 3 all but
			 * palette 9; at priority 0 all but palettes 6, 7 and 9). The
			 * name's sprite at priority 3: an enemy's points are the same
			 * tile and palette at priority 0. None in a hub or the factory
			 * (SA3-4) */
			.card = { .tile = 620, .tileMax = 623, .pal = 9, .prio1 = 3 + 1, .whole = true, .at = 165,
			          .drop = { { 0x030008AA, 0xFF, 1, 1, true, 2, true } },
			          .hide = { { -64, -64, 255, 175, 0, 1023, 1, 0 },
			                    { -64, -64, 255, 175, 0, 1023, 2, 0 },
			                    { -64, -64, 255, 175, 0, 711, 0, 1u << 6 | 1u << 7 },
			                    { -64, -64, 255, 175, 0, 1023, 3, 0xFFFFu & ~(1u << 9) },
			                    { -64, -64, 255, 175, 0, 1023, 0, 0xFFFFu & ~(1u << 6 | 1u << 7 | 1u << 9) } },
			          .poke = { 0x03002BFC, 16 },
			          /* the HUD on neither screen from the card's start until
			           * the take, then over it on the second screen (SA3-3):
			           * its start, the darkening (gBldRegs: BLDCNT 0x3FEF, BLDY
			           * 16), 7 frames before the name's sprite */
			          .hideTillTake = true,
			          .pre = { { 0x03002BF8, 0xFFFF, 0x3FEF, 2 }, { 0x03002BFC, 0x1F, 16, 2 } } },
		},
	},
	{
		.codes = { "ATHE", "ATHF", "ATHD" },
		.title = "Tony Hawk's Pro Skater 2",
		/* no map yet: listed, logo on the second screen */
		.logoOnly = true,
		/* the title is one full-screen picture with the badge painted in:
		 * cut out by its shape (the USA/Europe, French and German titles are
		 * identical), on the frame after 920 */
		.logo = { .wait = 920, .mask = &logoMaskTHPS2 },
	/* goal sheet before a run (Free Skate, Single Session, career checklist),
	 * without the sprites drawn over it;
	 * back to the logo on the menus with the blue banner (main menu, skater
	 * and level select). The results screen after a run keeps it: Retry
	 * goes straight back in without the sheet. */
	.snap = {
		.show = { { 16, 12, 0x392931 }, { 52, 29, 0x7B7B7B }, { 5, 49, 0x294252 }, { 214, 71, 0x943908 },
		          { 7, 92, 0x29949C }, { 27, 112, 0x9C8410 }, { 87, 130, 0xA5ADAD }, { 86, 146, 0xC6CED6 } },
		.showMin = 7,
		.revert = {
			{ { 238, 6, 0x94A5DE }, { 180, 24, 0x8494CE }, { 203, 24, 0x8494CE }, { 27, 69, 0x313939 },
			  { 116, 86, 0x313942 }, { 194, 102, 0x31424A }, { 66, 131, 0x000829 }, { 217, 136, 0xADBDD6 } },
		},
		.revertMin = { 7 },
		.stable = 3,
		.tol = 12,
		/* CAREER / CASH along the top and the button prompts along the bottom
		 * are sprites; so are the career sheet's scratch-outs and ticks
		 * in between, which stay */
		.hide = { { -64, -64, 255, 8, 0, 1023, -1 }, { -64, 144, 255, 159, 0, 1023, -1 } },
	},
	/* console layout: grind (A) top, ollie (B) bottom, flip (L) left, grab (R)
	 * right. Skating: display mode 2 (menus and goal sheets are mode 1) and
	 * the SCORE / SPECIAL / meter sprites in the top-left corner, which the
	 * pause screen hides; the same in every region and mode. */
	.controls = {
		.fourFace = true,
		.mode = 2,
		.hud = { { 2, 2 }, { 2, 24 }, { 4, 35 } },
	},
	},
	{
		.codes = { "AT3E" },
		.title = "Tony Hawk's Pro Skater 3",
		/* like THPS2's: one full-screen picture with the badge painted in
		 * (the same in all three releases), steady from frame 540 to 640 */
		.logo = { .wait = 590, .mask = &logoMaskTHPS3 },
		/* The "map" is the goal list (pause, View Goals), captured while
		 * skating (DISPCNT 0x1C40: BG2, BG3, sprites; menus add BG0),
		 * recognised by its VIEW GOALS heading (cyan sprite text). Only the
		 * panel (BG0) and its text are kept, on black: not the scene
		 * behind, the skater and the scene's sprites (priority 1-3), the
		 * score and timer along the top, the PAUSE heading (row 20) and
		 * the prompts along the bottom. The logo again at the menus (no
		 * BG2). The French and German headings (VOIR OBJ., ZIELE ZEIGEN)
		 * have their own probes (variants). */
		.open = { { K_START, 3 }, { 0, 25 }, { K_DOWN, 3 }, { 0, 9 }, { K_DOWN, 3 }, { 0, 9 }, { K_A, 3 } },
		.settleMax = 90, .openWithin = 60, .stableFrames = 3, .maxDiff = 40,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 86, 32, 0x00D6FF }, { 90, 32, 0x00D6FF }, { 98, 32, 0x00D6FF }, { 108, 34, 0x00D6FF },
		            { 118, 30, 0x00D6FF }, { 128, 30, 0x00D6FF }, { 141, 32, 0x00D6FF }, { 152, 32, 0x00D6FF } },
		.tolerance = 8,
		.mem = { { 0x04000000, 0xFFFF, 0x1D40, 2 } },
		.live = { { 0x04000000, 0xFFFF, 0x1C40, 2 } },
		.menu = { { 0x04000000, 0x0400, 0x0000, 2 } },
		.captureLayers = 0x1,
		.captureHide = { { -64, -64, 255, 24, 0, 1023, -1 }, { -64, 126, 255, 159, 0, 1023, -1 },
		                 { -512, -256, 511, 255, 0, 1023, 1 }, { -512, -256, 511, 255, 0, 1023, 2 },
		                 { -512, -256, 511, 255, 0, 1023, 3 } },
		/* the console layout as for THPS2; Auto: skating is the display
		 * with BG2, BG3 and sprites and no menu layer (0x1C40; menus and
		 * the pause menu add BG0) */
		.controls = { .fourFace = true, .playing = { { 0x04000000, 0xFFFF, 0x1C40, 2 } } },
	},
	{
		.codes = { "AA2E", "AA2P" },
		.title = "Super Mario Advance 2: Super Mario World",
		/* Super Mario World's own title (Start during the intro, Start for
		 * single player, A on Super Mario World): its logo on BG3, inside
		 * the wooden frame (16 pixels wide, rows 0-16 at the top), above
		 * the copyright line (the owner's choice over the Super Mario
		 * Advance 2 one, on BG2 of the first title) */
		.logo = { .steps = { { 0, 400 }, { K_START, 4 }, { 0, 200 }, { K_START, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 200 } },
		          .layer = 3, .minY = 17, .maxY = 100, .minX = 16, .maxX = 224, .rev = 1 },
		/* The game mode (0x03002BC6) is the SNES game's plus 0x10: 0x24 in
		 * a level (paused too, and the intro), 0x1E on the world map (paused
		 * too), 0x3D in the SELECT menu, 0x1B-0x23 between them.
		 * On the world map, not paused (0x03002C74 = 2; 7 paused), the
		 * SELECT menu is the "map": the level list with time and score, in a
		 * yellow frame. Its display settings (0x1700: BG0-2 and sprites) are
		 * its own. */
		.open = { { K_SELECT, 4 } },
		/* the list's ◁ ▷ (other worlds' lists: L / R) at the screen's left
		 * and right edges, about 92-105 down (SMA-1): left out */
		.captureHide = { { -16, 80, 8, 116, 0, 1023, -1 }, { 222, 80, 255, 116, 0, 1023, -1 } },
		.settleMax = 200, .openWithin = 40, .openTries = 4, .stableFrames = 3, .maxDiff = 400,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 2, 2, 0xF7DE6B }, { 237, 2, 0xF7DE6B }, { 2, 157, 0xFFFF9C }, { 237, 157, 0xFFFF9C },
		            { 120, 1, 0xD6BD4A }, { 4, 80, 0xFFFF9C }, { 235, 80, 0xFFFF9C }, { 120, 158, 0xCEDE73 } },
		.tolerance = 8,
		.mem = { { 0x04000000, 0xFFFF, 0x1700, 2 } },
		.live = { { 0x03002BC6, 0xFF, 0x1E, 1 }, { 0x04000000, 0xFFFF, 0x3E00, 2 }, { 0x03002C74, 0xFF, 0x02, 1 } },
		/* Mario's place on the world map (x, y in pixels: they change as
		 * he walks a path and hold once he's on a level): the list again
		 * as soon as he's there, not at the next refresh (USA, Europe).
		 * The game ignores SELECT for about 50 frames after he gets
		 * there: pressed again, up to 4 times 40 frames apart */
		.room = { { 0x030042AC, 0xFFFF, 0, 2 }, { 0x030042B0, 0xFFFF, 0, 2 } },
		/* the title, game select and file screens: modes 0x00-0x17 (title
		 * 0x05, game select 0x09, Super Mario World's title 0x13, files 0x15) */
		.menu = { { 0x03002BC6, 0xFF, 0x00, 1, true, 0x17, true } },
		/* Peach's HELP! and the selected level's name in the list blink:
		 * recorded, but shown still, with both (the state that covers more:
		 * they're dark text on light) */
		.animFrames = 8, .animStep = 8, .mapStill = true, .stillPick = STILL_COVER,
		/* In a level (game mode 0x24, also paused) the status bar is BG3 (0xDC44,
		 * at any priority: some castle rooms put it at 3, 0xDC47), rows
		 * 0-15: it goes to the top of the second screen, except the blue
		 * item box (104-135; the item in it is a sprite and stays). The rest
		 * of BG3 (message and pause boxes) stays on the game picture. */
		.hud = {
			.on = { { 0x03002BC6, 0xFF, 0x24, 1 }, { 0x0400000E, 0xFFFC, 0xDC44, 2 },
			        { 0x04000000, 0x0800, 0x0800, 2 } },
			/* through doors and pipes: the fade out (0x1F, also the world
			 * map's into a level: on only from a level), the level loading
			 * again (0x20-0x23) */
			.stay = { { 0x03002BC6, 0xFF, 0x1F, 1, true, 0x23, true } },
			/* to the world map (0x1B, 0x1D) or the title it goes */
			.keep = { { 0x03002BC6, 0xFF, 0x1F, 1, true, 0x23, true } },
			.bgMask = 8,
			/* the boss rooms (display mode 1, BG2 rotating): the status bar
			 * is BG0 (0xD044), the lava BG1 */
			.alt = { .on = { { 0x03002BC6, 0xFF, 0x24, 1 }, { 0x04000000, 0x0107, 0x0101, 2 },
			                 { 0x04000008, 0xFFFC, 0xD044, 2 } },
			         .bgMask = 1 },
			.moves = { { 104, 0, 135, 15, 0, 0, 0, 0, true } },
			/* the status bar's rows; below them BG3 stays in the game, drawn
			 * by it at its own priority: message and pause boxes in front,
			 * a cave's scenery behind (until v1.4 those rows were laid
			 * back over the game's picture: the scenery in front) */
			.rows = 16,
			.logoBelow = true,
			/* the level list's title row (the level's name) above it */
			.mapTitleRows = 14,
		},
		/* SNES layout: B (bottom) jumps (GBA A), Y (left) runs (GBA B), A
		 * (right) and X (top) spin jump (GBA R); L pans the camera left, R
		 * right. In a level and not paused (0x03002BC3: 2 paused, 0
		 * playing; message boxes 0 too). While GBA L is held the game pans
		 * the way 0x03004005 says (1 right, 2 left), which it takes from
		 * the way Mario (or Yoshi, when he rides) faces; set each frame, it
		 * pans either way without turning anyone. */
		.controls = {
			.snes = { .right = K_R, .bottom = K_A, .top = K_R, .left = K_B,
			          .pan = 0x03004005,
			          .when = { { 0x03002BC6, 0xFF, 0x24, 1 }, { 0x0400000E, 0xFFFC, 0xDC44, 2 },
			                    { 0x03002BC3, 0xFF, 0x00, 1 } } },
		},
	},
	{
		/* Super Mario Advance 4: Super Mario Bros. 3 (USA; Europe and Japan
		 * in sma4_eur / sma4_jpn). The byte at 0x03003DE2 says where Mario
		 * is: 0x41 the world map, 0x40 a level, 0x08-0x09 a Toad House,
		 * a Spade panel game or a king's room (the level engine, with the
		 * world map's status bar along the bottom), bit 7 set on the title,
		 * game select and file screens. 0x03003781 is 3 on the world map
		 * with no menu open (START's 15, R's e-Reader menu 14). */
		.codes = { "AX4E" },
		.title = "Super Mario Advance 4: Super Mario Bros. 3",
		/* Super Mario Bros. 3's own title (from power-on: the save formatted,
		 * the Super Mario Advance 4 title, Super Mario Bros. 3 picked, START
		 * through its curtain): SUPER MARIO BROS. 3 alone on BG1, the
		 * copyright line below it a sprite (SM4-5) */
		.logo = { .steps = { { 0, 1600 }, { K_START, 4 }, { 0, 150 }, { K_A, 4 }, { 0, 150 }, { K_A, 4 }, { 0, 300 },
		                     { K_START, 4 }, { 0, 150 } },
		          .layer = 1, .maxY = 120,
		          /* its orange shadow's rim along the letters' upper and
		           * left sides left out (in the game's other colours too) */
		          .shadow = { 0xF7A54A, 0xEF9431 }, .shadowReach = 3, .rev = 1 },
		/* (the start, up to the opening's airship, left to play by
		 * itself: each release and revision formats its save and shows
		 * its notices for a different time, and takes buttons there
		 * differently; by 1600 frames all are in the opening) */
		/* USA Rev 1 (and the Virtual Console and Switch Online dumps made
		 * from it) keeps where Mario is 4 bytes lower, as in Europe and
		 * Japan: its own variant, AX4R, picked by the ROM's version */
		.pick = sma4_pick,
		/* On the world map, L opens the item inventory: a blue panel (BG0)
		 * over the map's lower part (rows 56-157), the items in it as
		 * outlines with the chosen one a sprite in colour. 0x03003CAA is
		 * the panel: 0 closed, 1-4 opening, 5 open, 6-8 closing. The
		 * panel alone (BG0 and its sprite) is the "map", where the game
		 * draws it, on dark grey (SM4-1). */
		.open = { { K_L, 4 } },
		/* World-e's map with Mario on a level's spot: its info box takes
		 * no L (nor START); B opens the Level Card list and B again goes
		 * back to the map without the box, where L works, as a player
		 * does it (0x03004FC8 is World-e's map's own state: 2 free, 10
		 * with the box up, 16-18 with the inventory) */
		.openAltIf = { { 0x03004FC8, 0xFF, 10, 1 }, { 0x0400000C, 0xFFFF, 0xDC00, 2 } },
		.openAlt = { { K_B, 4 }, { 0, 60 }, { K_B, 4 }, { 0, 40 }, { K_L, 4 } },
		.settleMax = 60, .openWithin = 40, .openTries = 3, .stableFrames = 3, .maxDiff = 400,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* its white and black edges only: the game has two sets of
		 * colours (a colour patch players use picks the other one), and
		 * the blues differ between them */
		.probes = { { 120, 56, 0xFFFFFF }, { 120, 57, 0xFFFFFF }, { 120, 58, 0x000000 }, { 3, 100, 0xFFFFFF },
		            { 236, 100, 0xFFFFFF }, { 4, 100, 0x000000 }, { 120, 157, 0xFFFFFF }, { 120, 155, 0x000000 } },
		.tolerance = 8,
		/* (5: open; World-e keeps it at 0, its panel told by the probes) */
		.mem = { { 0x03003CAA, 0xFF, 5, 1, true, 0 } },
		/* World-e's map (the e-Reader levels) is another engine: where
		 * Mario is reads 0x09 (as in a Toad House), 0x03003781 is 4 or 5
		 * with no menu open (3-7 taken: menus make it 8 or more, a
		 * house's is 0 or 0x42), its inventory is open
		 * while 0x03003759 is 1, and its panel and status bar are BG2
		 * (0xDC00), the same size and place */
		.live = { { 0x03003DE2, 0xFF, 0x41, 1, true, 0x09 }, { 0x03003781, 0xFF, 3, 1, true, 7, true },
		          { 0x03003CAA, 0xFF, 0, 1 }, { 0x03003759, 0xFF, 0, 1 } },
		.captureLayers = 0x5,
		/* (captureBack 0: black, as a live second screen is, GEN-1) */
		/* the L and R signs at the top corners, and the map's sprites
		 * (Mario, HELP!, the Hammer Bros.: priority 1-3; the chosen item
		 * is priority 0) */
		.captureHide = { { -512, -256, 511, 47, 0, 1023, -1 }, { -512, -256, 511, 255, 0, 1023, 1 },
		                 { -512, -256, 511, 255, 0, 1023, 2 }, { -512, -256, 511, 255, 0, 1023, 3 } },
		/* Mario's place on the world map (y, x in pixels: they change as
		 * he walks and hold once he stops), and 0x03003E4A, 0xFF while he
		 * stands there taking input (0 walking, arriving back from a level
		 * or with a menu open, when L does nothing): the inventory again
		 * at once. (World-e's map: every second.) */
		.room = { { 0x03003DE4, 0xFF, 0, 1 }, { 0x03003DE8, 0xFF, 0, 1 }, { 0x03003E4A, 0xFF, 0, 1 } },
		/* the title, game select and file screens */
		.menu = { { 0x03003DE2, 0x80, 0x80, 1 } },
		/* World-e's castle hall (its doors lead to the e-Reader levels):
		 * where Mario is reads 0x08-0x09 as in a Toad House, but BG0, the
		 * status bar's layer, is off: no status bar, the logo; and from
		 * the moment Mario goes into it, or into a level, from World-e's
		 * map (its state 0x03004FC8 12, while the map closes in a circle),
		 * SM4-11 */
		.logoIf = { { 0x03003DE2, 0xFE, 0x08, 1 }, { 0x04000000, 0x0100, 0, 2 } },
		.logoIfOr = { { 0x03004FC8, 0xFF, 0x0C, 1 }, { 0x0400000C, 0xFFFF, 0xDC00, 2 } },
		/* In a level the status bar along the top (world, lives, time,
		 * coins, score) and the P meter at the bottom left are BG0
		 * (0x9400): the whole layer goes to the second screen, the status
		 * bar where the game draws it, the P meter centred along the
		 * bottom, with the logo between (SM4-2). The item cards shown at
		 * the start and end of a level are sprites and stay. */
		.hud = {
			/* (where Mario is: 0x40 a level, 0x08-0x09 an airship too,
			 * as well as the houses; their status bar is the map's,
			 * along the bottom, by BG0's vertical scroll, 0 in levels
			 * and airships) */
			.on = { { 0x03003DE2, 0xFF, 0x00, 1, true, 0x40, true }, { 0x04000008, 0xFFFC, 0x9400, 2 },
			        { 0x04000000, 0x0100, 0x0100, 2 }, { 0x04000012, 0x01FF, 0, 2 } },
			.bgMask = 1,
			.logoBelow = true,
			/* the status bar is rows 0-7 (and World-e's coin count below
			 * it, 8-15), the P meter 150-157: what BG0 has between (LEVEL
			 * CLEARED! and the card won, at the goal, from row 27) stays
			 * on the game screen (SM4-3) */
			.moves = { { 0, 16, 239, 149, 0, 0, 0, 0, true }, { 0, 150, 239, 159, 'C', 120, 0, 0 } },
			/* On the world map (inventory closed; 0x41) and in Toad
			 * Houses, Spade games and kings' rooms (0x08-0x09) the status
			 * bar along the bottom (BG0, rows 139-159: world, lives,
			 * coins, score, cards) goes to the top of the second screen,
			 * over the inventory picture (SM4-6, SM4-4); anything else on
			 * BG0 there stays. (0x40, a level, is the main HUD's.) */
			/* (not in the opening after Super Mario Bros. 3 is picked,
			 * where Mario reads 0x41 too: BG2's tiles at character block
			 * 2 there, 0 on every map and in the houses (SM4-9); nor on
			 * Europe's language menu, which leaves BG1 off (0x1900), shown
			 * on every map and in the houses (0x1300, 0x1B00, 0x1F00),
			 * SM4-10) */
			.alt = { .on = { { 0x03003DE2, 0xFE, 0x40, 1, true, 0x08 }, { 0x03003CAA, 0xFF, 0, 1 },
			                 { 0x04000000, 0x0300, 0x0300, 2 }, { 0x03003759, 0xFF, 0, 1 },
			                 { 0x0400000C, 0x000C, 0, 2 } },
			         .bgMask = 1,
			         .moves = { { 0, 0, 239, 127, 0, 0, 0, 0, true }, { 0, 128, 239, 159, 0, 0, 'T', 0 } },
			         .mapBelow = true },
			/* the inventory, the START menu or the e-Reader menu open
			 * there, or a house's own menu: the last picture stays */
			.keep = { { 0x03003DE2, 0xFE, 0x40, 1, true, 0x08 } },
			/* World-e's map: its two-row status bar is BG2 (0xDC00; in
			 * levels and houses BG2 is the background, never 0xDC00),
			 * while the inventory isn't open (0x03003759 0: the bar at
			 * rest or sliding; the inventory panel, on the same layer,
			 * stays on the game screen) */
			.memLayers = { .when = { { 0x0400000C, 0xFFFF, 0xDC00, 2 }, { 0x03003759, 0xFF, 0, 1 } }, .bgMask = 0x4 },
			/* World-e's status bar sliding off as the inventory opens
			 * (0x03003759 still 0), and back as it closes: BG2 scrolled
			 * up to 32 rows down (0x1E0-0x1FF). It's off the game screen
			 * (memLayers) and held still on the second screen as it was
			 * at rest, through the inventory too (keep), SM4-8 */
			.freeze = { { 0x0400000C, 0xFFFF, 0xDC00, 2 }, { 0x0400001A, 0x01FF, 0x1E0, 2, true, 0x1FF, true } },
		},
	},
	{
		/* the second screen is put together by ft_compose: a unit's Status,
		 * or the turn order with the Laws */
		.codes = { "AFXE" },
		.title = "Final Fantasy Tactics Advance",
		.virt = ft_virt,
		/* a Status's page prompt (FT-8): the D-pad and arrow at the right of
		 * its page bar, one 16x16 sprite (tile 452, wobbling across); and
		 * on its Abilities page, the R Info prompt under WT: a 16x16 and
		 * an 8x16 sprite (tiles 464, 468) at 100 and 116, line 62 */
		.captureHide = { { 200, 30, 239, 70, 452, 455, 0 }, { 90, 50, 130, 76, 464, 471, 3 } },
		.composePages = ft_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the Status's first page) */
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* taken on the battle map (or a unit's Status open), or on the
		 * world map (FT-14); kept while a unit moves or acts, through the
		 * battle's scenes, and in the world map's town menus and notices */
		.live = { { FT_V_IDLE, 0xFF, 1, 4 } },
		.menu = { { FT_V_SHOWN, 0xFF, 0, 4 } },
		.room = { { FT_CURSOR, 0xFFFF, 0, 2 }, { FT_UI, 0xFF, 0, 1 }, { FT_V_LOC, 0xFFFFFFFF, 0, 4 }, { FT_V_TURN, 0xFFFFFFFF, 0, 4 } },
		.hud = {
			/* nothing moves off the game screen (FT-10); this is for the
			 * logo before the first picture and elsewhere (FT-7) */
			.on = { { FT_V_SHOWN, 0xFF, 1, 4 } },
			.overMap = true,
			.mapClear = MAP_BG_NAVY,
			.logoUnder = true,
			.logoAlways = true,
		},
		/* (never a fight: this only makes the logo's picture) */
		.combat = { { FT_V_BATTLE, 0xFF, 2, 4 } },
		/* Start during the intro: the logo on BG2 with its shadow on BG1 (the
		 * copyright line below it is cut off by maxY) */
		.logo = { .wait = 400, .pressStart = true, .wait2 = 200, .layers = 0x6, .maxY = 72 },
	},
	{
		.codes = { "AMKE", "AMKP", "AMKJ" },
		.title = "Mario Kart: Super Circuit",
		/* no map yet: listed, logo on the second screen */
		.logoOnly = true,
		/* the title screen settles by frame ~900 (Japan's is a little
		 * later); BG0 holds the logo alone, with the copyright line at the
		 * bottom (rows 147-157), so keep rows above 100 */
		.logo = { .wait = 1000, .layer = 0, .maxY = 100 },
		/* The race HUD (coins, lap, item box, time, the top-4 faces, place,
		 * the course map) is BG0, and the racers' dots on the map are the
		 * only priority-0 sprites (karts and Lakitu are priority 1). Racing:
		 * mode 1 with all four BGs and sprites (0x1F41), BG0's map in screen
		 * block 7 (0x0700). The pause menu is drawn on BG0 too, from block 6:
		 * it stays on the game screen and the last HUD stays up. Menus and
		 * the course fly-over use other settings. */
		.hud = {
			.on = { { 0x04000000, 0xFFFF, 0x1F41, 2 }, { 0x04000008, 0xFFFF, 0x0700, 2 } },
			.keep = { { 0x04000000, 0xFFFF, 0x1F41, 2 } },
			.bgMask = 1,
			.objs = { { -512, -256, 511, 255, 0, 1023, 0 } }, /* priority 0 */
			/* Racing (all three versions): the LAP label in BG0's top bar
			 * (map cells row 0 col 4 = 0xC044, row 1 col 5 = 0xC048), and two
			 * cells the results table fills (row 11 col 4, row 12 col 8)
			 * empty. The label alone tells racing apart from the lap-time
			 * table and the results; the table slides in from the right, so
			 * the empty cells alone said "racing" for a few dozen frames of
			 * it (and the map, item box and faces moves scrambled it). */
			.layout = { { 0x06003808, 0xFFFF, 0xC044, 2 }, { 0x0600384A, 0xFFFF, 0xC048, 2 },
			            { 0x06003AC8, 0xFFFF, 0, 2 }, { 0x06003B10, 0xFFFF, 0, 2 } },
			/* the turn signs under the item box (108-130 x 40-62); the course
			 * map (181-228 x 99-154 on Peach Circuit) */
			.sign = { 104, 30, 135, 66 },
			.map = { 140, 70, 239, 159 },
			/* the sky (BG3), the scenery along the horizon (BG1) and the
			 * buildings over it (BG2: the screen is mode 0 above the horizon,
			 * mode 1 for the track below it, from row 76) */
			.backdrop = 0xE,
			.backdropRows = 76,
			/* The podium after a cup: the same registers as racing but BG1
			 * at priority 3 and other blending (0x2F40). BG0 holds the cup
			 * table; CONGRATULATIONS! (tiles 768-959), MUSHROOM CUP etc.
			 * (304-319) and the player's portrait (288-303) are priority-1
			 * sprites like the karts on the podium (512-703), so they're
			 * picked by tile. */
			.scene = {
				.when = { { 0x0400000A, 0x0003, 0x0003, 2 }, { 0x04000050, 0xFFFF, 0x2F40, 2 } },
				.objs = { { -512, -256, 511, 255, 768, 959, -1 }, { -512, -256, 511, 255, 288, 319, -1 } },
				/* the trophy stays on the game screen, so close up its gap:
				 * CONGRATULATIONS! centred, the portrait to the left edge,
				 * the place ("1st", BG0 139-167 x 40-62) centred */
				.moves = { { 0, 0, 239, 33, 'C', 120 }, { 88, 34, 132, 70, 'L', 6 }, { 134, 36, 180, 66, 'C', 120 } },
			},
			/* racing: the item box (107-133 x 4-27, with the item in it) to
			 * the game screen's top left and the place (8-44 x 128-151) to its
			 * top right; the four faces (5-26 x 21-114) centred down the
			 * second screen */
			.moves = { { 104, 0, 136, 28, 'L', 4, 'T', 4, true }, { 0, 120, 60, 159, 'R', 235, 'T', 4, true },
			           { 0, 18, 30, 118, 0, 0, 'M', 80 } },
		},
	},
	{
		/* F-Zero: GP Legend (docs/games/f-zero-gp-legend.md). The race HUD is
		 * all sprites, priority 0 (the track and scenery are BG2 / BG3 in
		 * mode 2): POWER (palette 8, tiles 353-374) top right, SPARE MACHINE,
		 * RANK and LAST (palettes 12, 13: tiles 379, 680-692, 970-980,
		 * digits 960-1016; Time Trial's BEST TIME, 711-715, palette 15,
		 * instead of RANK) top left, the time, lap times and KO count
		 * (palettes 12, 15: digits, 769) right, the speed (digits, km/h
		 * 350-352) bottom right, the course map one 64x64 sprite (tile 882,
		 * palette 10) at -2, 98, drawn every other frame, with the racers'
		 * dots (tile 679, any palette) on it. The machines, the countdown
		 * (palette 9), the lap sign (tile 256) and the glow under the machine
		 * (598, 638) are elsewhere or other tiles. */
		.codes = { "BFZE", "BFZJ" },
		.title = "F-Zero: GP Legend",
		.logoOnly = true,
		/* the title from frame ~520: the lettering alone on BG1 (FZ-1) */
		.logo = { .steps = { { 0, 640 } }, .layer = 1 },
		.hud = {
			/* racing: mode 2 with BG2, BG3 and sprites (0x3C62), without
			 * RESULTS (tiles 579-594) over the track as a race ends or is
			 * given up (FZ-6). (0x0300016D, 0x3C while racing, is still 0
			 * through the countdown after RETRY.) */
			.on = { { 0x04000000, 0xFFFF, 0x3C62, 2 } },
			.offTile = 579,
			/* (not a rival's place marker over the track ahead (FZ-4): a
			 * small digit, 950-989 in palette 12, with leader lines, tiles
			 * 678 and 688. The HUD never uses those two (its are 680-692
			 * and 711-715), and its small palette-12 digits are only
			 * SPARE MACHINE's count (64, 2) and LAST's (32, 34): only
			 * there; small digits in its other palettes and the tall
			 * ones (990-1023) anywhere. Found by listing the sprites of
			 * every test state.) */
			.objs = { { -16, -16, 239, 56, 350, 380, 0, 1u << 8 | 1u << 12 | 1u << 13 | 1u << 15 },
			          { -16, -16, 239, 56, 680, 687, 0, 1u << 8 | 1u << 12 | 1u << 13 | 1u << 15 },
			          { -16, -16, 239, 56, 689, 720, 0, 1u << 8 | 1u << 12 | 1u << 13 | 1u << 15 },
			          { -16, -16, 239, 56, 760, 780, 0, 1u << 8 | 1u << 12 | 1u << 13 | 1u << 15 },
			          { -16, -16, 239, 56, 990, 1023, 0, 1u << 8 | 1u << 12 | 1u << 13 | 1u << 15 },
			          { -16, -16, 239, 56, 950, 989, 0, 1u << 8 | 1u << 13 | 1u << 15 },
			          { 56, -16, 80, 6, 950, 989, 0, 1u << 12 },
			          { 24, 30, 48, 38, 950, 989, 0, 1u << 12 },
			          /* (x from -64: Clip Oval's map, narrow, is at -27) */
			          { -64, 88, 64, 150, 882, 882, 0, 1u << 10 },
			          { -16, 88, 64, 159, 679, 679, 0 } },
			/* the map's box (the sprite at -2, 98; at -2, 92 on some
			 * courses: from row 88), twice as big at the bottom centre;
			 * the game draws it every other frame, the dots (tile 679)
			 * every frame: held between (FZ-4) */
			.map = { 0, 88, 63, 159 },
			.mapAt = { 2, 120, 159, 882, 679 },
			/* the course fly-in: the race's registers, no HUD yet (FZ-6) */
			.emptyIsOff = true,
			/* RANK to the game screen's bottom left (FZ-3); POWER to the top
			 * centre; LAST under SPARE MACHINE (or Time Trial's BEST TIME),
			 * two rows below, lined up with it, or in the corner (Training:
			 * nothing else there); the right-hand column up into POWER's
			 * place (FZ-4). RANK moves only with its label there (tile
			 * 379): Time Trial has its best time there instead, which
			 * stays where it is (FZ-8) */
			.moves = { { 0, 12, 79, 31, 'L', 4, 'B', 155, true, .ifTile = 379 },
			           { 150, 0, 239, 17, 'C', 120, 'T', 2 },
			           { 0, 32, 79, 56, 'L', 0, 'T', 2, .under = 2 },
			           { 150, 18, 239, 80, 0, 0, 'T', 2 } },
		},
	},
	{
		/* F-Zero: Maximum Velocity (docs/games/f-zero-maximum-velocity.md),
		 * GP Legend's engine before it. The race HUD is all sprites,
		 * priority 0, on the track's BG2 / BG3 in mode 2 (DISPCNT 0x3C42,
		 * the menus' too): SPARE MACHINES (tiles 676-687, palettes 12, 13)
		 * top left, RANK under it (label 399; the place 750-770, the limit
		 * 394-405, palette 14 when outside it), LAST (734-745) under that;
		 * POW (376-393, palettes 12, 15) top right, the speed (366-374,
		 * 952-961) under it and the time (962-986, palettes 12, 15; a lap's
		 * time under it as it ends) under that; BOOST (406-408) bottom
		 * right; the course map one 64x64 sprite (tile 884, palette 15) at
		 * 0, 96, drawn every other frame, with the player's dot (675) on
		 * it. Not the HUD: the number over a rival ahead and its line
		 * (688-698, anywhere over the track's top half, the RANK corner
		 * too), the signs over the track (256-280: READY, the laps left,
		 * TURN BACK, the place at the finish; 592-660), CHECK (412) under a
		 * machine close behind, the machines (rotated, palette 0) and
		 * their glow (667) and sparks (456-536, 700). Found by listing the
		 * sprites of whole races (Grand Prix, Training, all three
		 * releases). */
		.codes = { "AFZE", "AFZJ" },
		.title = "F-Zero: Maximum Velocity",
		.logoOnly = true,
		/* the title's sprites alone: the lettering above PRESS START (row
		 * 123) and the copyright line (FZM-1) */
		.logo = { .steps = { { 0, 900 } }, .layers = 0x10, .maxY = 115 },
		.hud = {
			/* the race HUD is up: the game's mode (0x03000BFB: the handler
			 * table at 0x080513D0's index) one with a race on screen, and
			 * the HUD's own state (0x020168C9: 0x63 shown, 0x64 paused,
			 * 0x61 gone; it changes a frame before the HUD's sprites do).
			 * The modes: the countdown (3), the race (5), the finish (6 and
			 * 0x10 seen: second place on the debug AUTO, first driven),
			 * RANK OUT (8), YOU LOST after GIVE UP (9), the machine
			 * destroyed (10): 3 to 0x10 with the low three bits 0 to 6, so
			 * not 7, the results, where the state byte holds other things.
			 * The HUD stays through YOU LOST till A is pressed (0x61 then);
			 * the camera circling a wreck has none (0x61). Before the
			 * owner's reports of Nov 5 (a driven finish: mode 0x10; RANK
			 * OUT: mode 8) this went by the modes alone and the flag
			 * 0x030005F8, which drops at the finish (FZM-2, FZM-6, FZM-8).
			 * The display in mode 2 too (the track, 0x3C42 in every race
			 * moment seen): the menus are mode 0 (0x1240, 0x1440, 0x1740),
			 * and after a race their mode byte (3, 9) and the state byte
			 * (0x61-0x64, which the menus use too) read as a race's: the
			 * second screen went black there (the owner's reports of Nov 6,
			 * FZM-6) */
			.on = { { 0x03000BFB, 0xFF, 3, 1, true, 0x10, true }, { 0x03000BFB, 0x07, 0, 1, true, 6, true },
			        { 0x020168C9, 0xFF, 0x63, 1, true, 0x64, true }, { 0x04000000, 0x0007, 2, 2 } },
			.objs = { /* POW */
			          { 130, -8, 239, 12, 376, 393, 0 },
			          /* SPARE MACHINES */
			          { -8, -8, 72, 9, 676, 687, 0 },
			          /* RANK: label, place, limit */
			          { -8, 8, 50, 34, 399, 399, 0 },
			          { -8, 8, 50, 34, 750, 770, 0 },
			          { -8, 8, 50, 34, 394, 405, 0 },
			          /* LAST */
			          { -8, 33, 50, 44, 734, 745, 0 },
			          /* BOOST and its three marks */
			          { 170, 140, 240, 159, 406, 408, 0 },
			          /* the time and the lap's time */
			          { 150, 18, 250, 56, 962, 986, 0, 1u << 12 | 1u << 15 },
			          /* the map (x from -16: a narrow course's further left)
			           * and the dot */
			          { -16, 88, 64, 159, 884, 884, 0, 1u << 15 },
			          { -16, 84, 72, 159, 675, 675, 0 } },
			/* the map's box (the sprite at 0, 96; the dot reaches row 92),
			 * twice as big at the bottom centre; the game draws it every
			 * other frame, the dot every frame: held between (FZM-4) */
			.map = { 0, 90, 63, 159 },
			.mapAt = { 2, 120, 159, 884, 675 },
			/* RANK to the game screen's bottom left (FZM-3), only with its
			 * label (Training has none); POW to the top centre; LAST under
			 * SPARE MACHINES, two rows below, lined up with it, or in the
			 * corner (Training); the time up into the speed's place; BOOST
			 * under POW (FZM-4) */
			.moves = { { 0, 10, 50, 34, 'L', 4, 'B', 155, true, .ifTile = 399 },
			           { 130, 0, 239, 13, 'C', 120, 'T', 2 },
			           { 0, 35, 50, 45, 'L', 2, 'T', 2, .under = 2 },
			           { 150, 18, 250, 56, 0, 0, 'T', 2 },
			           /* BOOST centred under POW (FZM-4) */
			           { 170, 140, 239, 159, 'C', 120, 'T', 12 } },
		},
	},
	{
		/* Crash Nitro Kart (docs/games/crash-nitro-kart.md), Mario Kart's
		 * second screen. Driving (races, Time Trial, the Adventure hub),
		 * the end-of-race menus and the pause all use display mode 1 with
		 * BG0-2 and sprites (DISPCNT 0x1741), the HUD on BG0 (BG0CNT 0x1800,
		 * map at 0x0600C000). BG0's tiles are handed out as each screen is
		 * built (from 0x300), so no tile number is fixed: what the map
		 * holds tells the screens apart. The course map is an 8x8 block of
		 * cells (rows 12-19, columns 22-29), filled while driving whatever
		 * the course's shape: cell (16, 26). RACE RANKINGS has the faces'
		 * column (cell (3, 0)) but no map; TRY AGAIN? neither; the pause
		 * takes the HUD off BG0. Sprite tiles are handed out the same way:
		 * the HUD's sprites are told by priority 0, place and size. */
		.codes = { "BCNE", "BCNP" },
		.title = "Crash Nitro Kart",
		.logoOnly = true,
		/* the title is one bitmap (mode 4) from frame ~880: the tyre and
		 * lettering inside the mask, the swirl behind left out (CNK-1) */
		.logo = { .wait = 950, .mask = &logoMaskCNK },
		.hud = {
			/* driving: the course map's cell filled (CNK-2), and the game's
			 * fade-in done (0x03002A5B: 0xB0-0xF0 as a race fades in through
			 * its palettes, the HUD already up; 0xFF from then on, all
			 * through a race): the scenery behind the map is taken then,
			 * not half faded in */
			.on = { { 0x04000000, 0xFFFF, 0x1741, 2 }, { 0x04000008, 0xFFFF, 0x1800, 2 },
			        { 0x0600C434, 0x03FF, 1, 2, true, 0x3FF, true }, { 0x03002A5B, 0xFF, 0xFF, 1 } },
			/* paused: the player's pause byte (0x030045A1, 1 from START until
			 * the HUD is back after RESUME): the last HUD stays (CNK-6) */
			.keep = { { 0x030045A1, 0xFF, 1, 1 } },
			/* RACE RANKINGS: the map's cell blank, the faces' column filled:
			 * the table as it is (no moves, no map), CNK-5 */
			.alt = { .on = { { 0x04000000, 0xFFFF, 0x1741, 2 }, { 0x04000008, 0xFFFF, 0x1800, 2 },
			                 { 0x0600C434, 0x03FF, 0, 2 }, { 0x0600C0C0, 0x03FF, 1, 2, true, 0x3FF, true } },
			         .bgMask = 1 },
			.bgMask = 1,
			/* priority 0: the place numbers by the faces (8x8 at x 26), the
			 * wumpa icon (32x32 at x 128), the hub's trophy and key (tall
			 * shapes, top: the key spins, 8x32 edge-on), the racers' dots on
			 * the map (8x8, bottom right). Not the
			 * item you hold (32x32 at x 96: it stays on the game screen,
			 * CNK-3), the countdown's lights (16x16 at row 47; at GO they fly
			 * up past the top as 32x32s) or its letters (row 86) */
			.objs = { { 16, -8, 40, 127, 0, 1023, 0, 0, 0, 1u << 0 },
			          { 126, -2, 130, 3, 0, 1023, 0, 0, 0, 1u << 2 },
			          { 64, 4, 80, 13, 0, 1023, 0, 0, 0, 0xFu << 8 },
			          { 146, 4, 164, 13, 0, 1023, 0, 0, 0, 0xFu << 8 },
			          { 150, 80, 239, 159, 0, 1023, 0, 0, 0, 1u << 0 } },
			.layout = { { 0x0600C434, 0x03FF, 1, 2, true, 0x3FF, true } },
			/* the course map (the hub's too) in the bottom-right corner,
			 * twice as big on every course (CNK-4): up to 62x62 (Velo's
			 * Challenge), taller than Mario Kart's way allows at 2x (60 with
			 * its margin), so at a fixed place: right of the faces, its
			 * bottom at row 156 (the tallest from row 33, below the top
			 * row's time and wumpa icon) */
			.map = { 176, 90, 239, 159 },
			.mapAt = { 2, 139, 156 },
			/* the sky (BG3) and the scenery (BG1, BG2 above the horizon) */
			.backdrop = 0xE,
			.backdropRows = 70,
			/* back onto the game screen where the game draws them (CNK-3):
			 * the place (bottom left), the middle (turn signs; below the
			 * hub's trophy count, rows 21-32), the bottom centre (the hub's
			 * place names) */
			.moves = { { 0, 128, 60, 159, 0, 0, 0, 0, true }, { 50, 36, 175, 127, 0, 0, 0, 0, true },
			           { 61, 128, 175, 159, 0, 0, 0, 0, true } },
		},
	},
	{
		/* V-Rally 3 (docs/games/v-rally-3.md), the racing games' second
		 * screen. A stage is a bitmap (mode 4, DISPCNT 0x1444 / 0x1454 as
		 * the game flips its frames) with the HUD, sky and scenery as
		 * sprites: the HUD priority 0, the scenery priority 1. The menus
		 * use the same display mode; only a stage has the V-count
		 * interrupt on (DISPSTAT bit 5). Sprite tiles are handed out per
		 * mode (808-815 is LAP in V-Rally Cross, the damage lights in
		 * V-Rally Mode), so each piece is told by tile, palette and place.
		 * The countdown, NEW RECORD, the hazard boards, the pause box and
		 * the in-car dashboard are drawn into the bitmap: they stay on the
		 * game screen with the track (VR3-2). */
		.codes = { "AVRE", "AVRP" },
		.title = "V-Rally 3",
		.logoOnly = true,
		/* no title screen: the main menu's heading, V-RALLY 3, painted into
		 * its bitmap over purple stripes (VR3-1). From power-on: the
		 * language (START), the save slot (A, A), its name and flag (A,
		 * A, START), then A on to the main menu (no battery save on the
		 * logo's copy, so the same every time) */
		.logo = { .steps = { { 0, 1500 }, { K_START, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 120 },
		                     { K_A, 4 }, { 0, 90 }, { K_A, 4 }, { 0, 90 }, { K_START, 4 }, { 0, 90 },
		                     { K_A, 4 }, { 0, 90 }, { K_A, 4 }, { 0, 120 }, { K_A, 4 }, { 0, 120 } },
		          .mask = &logoMaskVR3 },
		.hud = {
			/* a stage: the V-count interrupt on and the bitmap mode; the
			 * HUD's own sprites tell its first frames (none yet) apart
			 * (VR3-2, VR3-7) */
			.on = { { 0x04000004, 0x0020, 0x0020, 2 }, { 0x04000000, 0x0007, 4, 2 } },
			.emptyIsOff = true,
			.objs = { /* BEST TIME / CHECK POINT and its time */
			          { -8, -8, 80, 24, 816, 831, 0, 1u << 5 },
			          /* the clock */
			          { 150, -8, 240, 24, 1000, 1015, 0, 1u << 8 },
			          /* POS (moved back onto the game screen, VR3-3) */
			          { -8, 24, 80, 40, 1016, 1023, 0, 1u << 5 },
			          /* LAP (V-Rally Cross) */
			          { -8, 40, 80, 56, 808, 815, 0, 1u << 5 },
			          /* the damage lights (V-Rally Mode) */
			          { 200, 16, 239, 100, 800, 815, 0, 1u << 8 },
			          /* the course map: its line, the dots, the round backing */
			          { 160, 80, 239, 159, 832, 832, 0, 1u << 6 },
			          { 160, 80, 239, 159, 896, 899, 0, 1u << 6 },
			          { 160, 80, 239, 159, 900, 912, 0, 1u << 7 } },
			/* the map's box (the 64x64 sprite at 176, 96 and its backing),
			 * twice as big at the bottom centre (VR3-4) */
			.map = { 176, 96, 239, 159 },
			.mapAt = { 2, 120, 158 },
			/* POS up into BEST TIME's corner of the game screen (VR3-3);
			 * LAP up into POS's place on the second screen (VR3-4) */
			.moves = { { 0, 26, 80, 39, 'L', 7, 'T', 6, true },
			           { 0, 42, 80, 55, 'L', 7, 'T', 30 } },
		},
	},
	{
		/* Summon Night: Swordcraft Story 2 (USA; in Japan Craft Sword
		 * Monogatari 2, the same RAM and menus). Walking about: DISPCNT
		 * 0x1F40; START's menu and its Status screen 0x3F40, the Status with
		 * the halfword at 0x03000002 = 0x0434 (0x03AC in the menu; other
		 * values in the field). The menu's cursor starts on Equipment each
		 * time and wraps: UP twice (past the page arrow) is Status. The
		 * title: DISPCNT 0x3740, BG2CNT 0x1F0A. */
		.codes = { "BSKE", "BSKJ" },
		.title = "Summon Night: Swordcraft Story 2",
		/* the title's lettering, BG0 alone (its glow is BG1); START skips
		 * the intro from frame ~600 (SN2-1) */
		.logo = { .steps = { { 0, 650 }, { K_START, 4 }, { 0, 250 } }, .layer = 0, .maxY = 110 },
		/* the Status screen, whole (SN2-2), each character in turn (SN2-5):
		 * see sn2_compose */
		.composePages = sn2_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first character) */
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.live = { { 0x04000000, 0xFFFF, 0x1F40, 2 } },
		/* the title: the logo (SN2-3) */
		.menu = { { 0x04000000, 0xFFFF, 0x3740, 2 }, { 0x0400000C, 0xFFFF, 0x1F0A, 2 } },
	},
	{
		/* Spider-Man: Mysterio's Menace (USA and Europe; Japan: the ASEJ
		 * variant). In a level the display is mode 0 with every layer and
		 * sprites (DISPCNT 0x1F40, the title's too); SELECT brings up the
		 * Select menu, a mode 4 bitmap with sprite text (0x1444) that fades
		 * in over some 40 frames (BLDY); START a small pause menu over the
		 * level (0x1F40 still). The intro, story pictures and the city map
		 * between levels are mode 4 bitmaps too, with sprites (0x1444) on
		 * the continue screen and the city map. The halfword at 0x03002170
		 * (the screen's handler) is 0x612C in a level (its Select menu
		 * too), 0x420C at the title, the intro, between levels and on the
		 * continue screen after QUIT GAME. (Not 0x03000228, which stays 0
		 * after QUIT GAME: the owner's reports of Oct 30.) */
		.codes = { "ASEE" },
		.title = "Spider-Man: Mysterio's Menace",
		/* the title's lettering: sprites above the menu (SMM-1); START
		 * skips the intro (both releases' titles are up by then) */
		.logo = { .steps = { { 0, 560 }, { K_START, 4 }, { 0, 150 } }, .layers = 0x10, .maxY = 70 },
		/* the Select menu, whole, as the game draws it, once it's faded in
		 * (SMM-2) */
		.open = { { K_SELECT, 3 } },
		.settleMax = 120, .openWithin = 40, .stableFrames = 4, .maxDiff = 40,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* (it opens faded all the way to black, held a few frames: not
		 * taken until most of it isn't black) */
		.bgColor = 0x000000, .minForeground = 20000, .tolerance = 8,
		.mem = { { 0x04000000, 0xFFFF, 0x1444, 2 }, { 0x03002170, 0xFFFF, 0x612C, 2 } },
		.live = { { 0x04000000, 0xFFFF, 0x1F40, 2 }, { 0x03002170, 0xFFFF, 0x612C, 2 } },
		/* the title, the intro, between levels, the continue screen: the
		 * logo (SMM-3) */
		.menu = { { 0x03002170, 0xFFFF, 0x420C, 2 } },
	},
	{
		/* Lady Sia (USA; Europe: the ALDP variant, same addresses). Where
		 * the game is: LS_LEVEL (the level, 0x10-0x33; ls_virt) and
		 * LS_PAUSE (the pause screen up). (0x03003090 looked like it at
		 * first, but its bits go on and off in conversations, boss fights
		 * and some levels: every level was loaded through LS_LEVEL and
		 * checked.) The pause screen
		 * (START) is sprites on black, drawn the frame it opens: lives, gems
		 * and rescued villagers in rows 8-36, the level's time 49-62,
		 * "Pause" 73-86, its choice with blinking arrows 97-110 (the time,
		 * Pause and the choice are one block of 64x64 text sprites), the
		 * ring and heart 126-158 (the ring pulses). */
		.codes = { "ALDE" },
		.title = "Lady Sia",
		/* the title's lettering, once it has slid into place (the language
		 * list follows at ~1050): at this frame BG2 is the lettering, BG1
		 * its pink edge and the TM, BG0 the white behind; Press START is a
		 * sprite (the game moves them between layers from frame to frame:
		 * another frame needs other layers) (LS-1) */
		.logo = { .steps = { { 0, 990 } }, .layers = 0x6, .maxY = 106 },
		/* the pause screen, whole (LS-2). Its ring glows in a 40-frame
		 * cycle: 10 frames plain (x 44-86, y 129-156), then three steps of
		 * a white outline round it; taken plain (the first probes: black
		 * just outside the plain ring, lit in every glow step), which it
		 * is as the screen opens, else within a cycle. The other probes
		 * are black rows of the pause screen (between the top row and the
		 * time, above the ring): in a conversation START doesn't open it,
		 * and the level there must not pass for it (LS-5). */
		.open = { { K_START, 3 } },
		.settleMax = 60, .openWithin = 45, .stableFrames = 2, .maxDiff = 700,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.probes = { { 61, 128, 0x000000 }, { 42, 143, 0x000000 }, { 62, 157, 0x000000 }, { 84, 154, 0x000000 },
		            { 60, 42, 0x000000 }, { 120, 42, 0x000000 }, { 180, 42, 0x000000 }, { 120, 118, 0x000000 } },
		.bgColor = 0x000000, .minForeground = 2000, .tolerance = 8,
		/* map_crop: the rows of Pause and its choice, arrows included, in
		 * the screen's black (LS-2) */
		.glyphs = { { 0, 67, 240, 49, -1, { 0x000000 } } },
		/* the pause screen up: 0x03001545 is 5 there, 3 in a level
		 * (0x03003090's bit 4, which also goes on as it opens in the
		 * first levels, stays off in some and comes on elsewhere) */
		.mem = { { LS_PAUSE, 0xFF, 5, 1 } },
		/* in a level (whatever else 0x03003090 says: bit 1 goes on in its
		 * conversations and some boss fights, bit 0 flickers in some
		 * levels), not paused (the unused level 0x2E has 0 there, so it
		 * gets no pictures); paused in the game, the last picture stays
		 * (LS-4). A conversation that doesn't let the pause screen open
		 * fails the probes (LS-5). */
		.live = { { LS_LEVEL, 0xFF, 0x10, 1, true, 0x33, true }, { LS_PAUSE, 0xFF, 3, 1 } },
		/* the title, the menus, the story, the maps between levels: the
		 * logo (LS-3) */
		.menu = { { LS_V_OFFLEVEL, 0, 1, 1 } },
		.virt = ls_virt,
		/* the HUD off the game screen, the status picture having it
		 * (LS-6): shown for ~3 s as a level starts or Sia picks something
		 * up, the heart while she's hurt. Found by where each piece's
		 * corner is, nothing else: its palettes, priority and tiles
		 * change from level to level (the first: priority 0, palettes 0,
		 * 4, 14; the second: palettes 6, 7, and Sia priority 0 too). The
		 * top row: lives face 2,7 and count 40,10; gem 94,3 and count
		 * 135,10; villager face 193,1 and count 223,10; from 10 on, each
		 * count's tens 8 to the left (32, 127, 215: a player's bug report,
		 * the tenth gem; the game draws two digits at most; Sia jumping was seen at 55,10, the menus' 64x64 grid at
		 * 192,0). The ring bobs: 39-43,125-128; 69-76,126-131; 52,153.
		 * The heart: 165-170,131-132; 168-169,144-145. Each also by its
		 * shape and size (the faces, gem and ring 32x32, the counts 8x16,
		 * ...): a level's things pass the same places (level 0x28's
		 * pedestal, 32x32 at 169,130; a log's 8x8 pieces by the ring in
		 * 0x2D), seen across every level's start (LS-6). The game's own
		 * pause screen has its pieces in the same places: the hiding is
		 * off while it's up (hideWhen), so it keeps them. */
		.hideSprites = { { 2, 7, 2, 7, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 31, 10, 49, 10, 0, 1023, -1, 0, 0, LS_SHAPE(2, 0) },
		                 { 94, 3, 94, 3, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 126, 10, 144, 10, 0, 1023, -1, 0, 0, LS_SHAPE(2, 0) },
		                 { 193, 1, 193, 1, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 214, 10, 232, 10, 0, 1023, -1, 0, 0, LS_SHAPE(2, 0) },
		                 { 38, 124, 44, 129, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 68, 125, 77, 132, 0, 1023, -1, 0, 0, LS_SHAPE(2, 2) | LS_SHAPE(2, 1) },
		                 { 51, 152, 53, 154, 0, 1023, -1, 0, 0, LS_SHAPE(1, 1) },
		                 { 164, 130, 171, 133, 0, 1023, -1, 0, 0, LS_SHAPE(0, 1) | LS_SHAPE(2, 0) },
		                 { 167, 143, 170, 146, 0, 1023, -1, 0, 0, LS_SHAPE(1, 0) | LS_SHAPE(0, 1) } },
		/* only in play: the menus, the story scenes, the credits (items
		 * floating past the HUD's places) and the game's own pause screen
		 * are drawn whole (LS-6) */
		.hideWhen = { { LS_V_PLAY, 0, 1, 1 } },
	},
	{
		/* Ninja Five-O (USA; Europe, Ninja Cop: the ANXP variant). No
		 * screen of the game's own holds its status (its pause is a menu
		 * over the stage), so the HUD moves: see NFO_HUD below. */
		.codes = { "ANXE" },
		.title = "Ninja Five-O",
		/* the title's NINJA and FIVE-O, cut out of the title by their
		 * colours (tools/make_logo_masks.py nfo): without the kanji's
		 * brush strokes behind them, NINJA's white outline, the white
		 * screen, the TM, PRESS START or the copyright line (NFO-1) */
		.logo = { .steps = { { 0, 1200 }, { K_START, 4 }, { 0, 60 }, { K_START, 4 }, { 0, 60 } },
		          .mask = &logoMaskNFO, .rev = 2 },
		.logoOnly = true,
		.hud = {
			/* in a stage: the HUD's layer on, with its settings (BG0,
			 * screen 31, tiles 3; priority 0, or 1 in the Launch Site) */
			.on = { { 0x04000008, 0xFFFC, 0x1F0C, 2 }, { 0x04000000, 0x0100, 0x0100, 2 },
			        /* the mission (0 Bank ... 4 Base): not the Launch Site */
			        { 0x03003D41, 0xFF, 0, 1, true, 4, true },
			        /* not the opening's demo (1 there, from its first frame) */
			        { 0x03003D52, 0xFF, 0, 1 } },
			.bgMask = 0x1,
			/* the Launch Site (mission 5): its BG0 is no HUD row but the
			 * red that fades in over the stage as the ninja dies (kept
			 * hidden by the blend till then): the sprites alone go, BG0
			 * stays on the game screen; a move that does nothing, so
			 * the bottom row's (none here) isn't copied up over the
			 * countdown (NFO-2, NFO-3) */
			.alt = { .on = { { 0x04000008, 0xFFFC, 0x1F0C, 2 }, { 0x04000000, 0x0100, 0x0100, 2 },
			                 { 0x03003D41, 0xFF, 5, 1 }, { 0x03003D52, 0xFF, 0, 1 } },
			         .bgMask = 0,
			         .moves = { { 0, 159, 0, 159, 0, 0, 'T', 159 } } },
			/* the bars, points or time, the boss's bar and the Launch
			 * Site's countdown: priority 1 at the top (the stage's own
			 * things are 2, the card and pause menu 0) */
			.objs = { { -16, -16, 255, 24, 0, 1023, 1, 0 } },
			/* the bottom row (shuriken level, keys, RESCUE) up under
			 * the bars, the logo below them both */
			.moves = { { 0, 152, 239, 159, 0, 0, 'T', 24 } },
			.logoBelow = true,
			.logoTop = 32,
		},
	},
	{
		/* Wario Land 4 (USA and Europe; Japan: the AWAJ variant, same
		 * addresses and screens). The pause screen (START) slides down
		 * from the top over some 40 frames: its menu in rows 0-23 (BG0
		 * words, BG1 frames, the hand a sprite), the status bar, the wall
		 * with the jewel, Keyzer and CD (BG2, in 8-row courses from row
		 * 24) and the level's name plate (rows 141-158). */
		.codes = { "AWAE" },
		.title = "Wario Land 4",
		/* the title's lettering and its TM: BG0 alone (PUSH START, the
		 * car and the copyright line are sprites), up once START has
		 * skipped the intro (WL4-1) */
		.logo = { .steps = { { 0, 900 }, { K_START, 4 }, { 0, 240 } }, .layers = 0x1, .maxY = 160 },
		/* the pause screen, once it has slid into place: the menu's three
		 * frames (cyan along row 18, black while it slides) and the name
		 * plate's edge and inside, the same in every passage and both
		 * releases (WL4-2, WL4-8) */
		.open = { { K_START, 3 } },
		.settleMax = 90, .openWithin = 30, .stableFrames = 3, .maxDiff = 200,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		/* map_crop: the menu's rows (down to the wall's first row of
		 * bricks) from a render of BG2 alone, the wall the game draws
		 * behind the menu, without its words, frames or hand (WL4-2) */
		.underRect = { { 0, 0, 239, 23 } }, .underLayers = 0x4,
		.probes = { { 60, 18, 0x5AFFFF }, { 120, 18, 0x5AFFFF }, { 200, 18, 0x5AFFFF },
		            { 10, 150, 0x218484 }, { 220, 150, 0x000000 } },
		.bgColor = 0x000000, .minForeground = 2000, .tolerance = 8,
		.mem = { { WL4_SCREEN, 0xFFFF, 4, 2 } },
		/* in a level's play (not a boss's: level 4), not paused or going
		 * through a door; elsewhere the last picture stays (WL4-6) */
		.live = { { WL4_SCREEN, 0xFFFF, 2, 2 }, { WL4_SUB, 0xFF, 2, 1 }, { WL4_LEVEL, 0xFF, 0, 1, true, 3, true } },
		/* the title, the hub, the level select, the vortex, a boss: the
		 * logo (WL4-7) */
		.menu = { { WL4_V_OFFLEVEL, 0, 1, 1 } },
		.virt = wl4_virt,
		/* another level, a gem piece, the CD or the Keyzer found, or a
		 * heart gained or lost: taken again at once (WL4-3) */
		.room = { { 0x03000002, 0, 0, 2 }, { 0x03000C07, 0, 0, 4 }, { 0x03000C0B, 0, 0, 2 },
		          { WL4_HEARTS, 0, 0, 1 } },
		/* In a level's play, the hearts and heart meter (palette 7, top
		 * left) and the coins (the coin palette 6, its figures 4, top
		 * right) leave the game screen: the pause screen has them. The
		 * escape's stopwatch and countdown (palette 4, x 80-128) stay
		 * (WL4-4). All priority 0, in the top 16 rows; the level's own
		 * sprites are priority 1-2. */
		.hideSprites = { { 0, -8, 79, 15, 0, 1023, 0, 1 << 7 },
		                 { 160, -8, 239, 15, 0, 1023, 0, 1 << 6 | 1 << 4 } },
		.hideWhen = { { WL4_V_HUDOFF, 0, 1, 1 } },
		/* A boss fight (a level's number 4, playing): its HUD, all
		 * priority-0 sprites in the top rows (the hearts in palette 7,
		 * the stopwatch and countdown in 4, the boss's bar in 14), goes
		 * to the top of the second screen, on grey with the logo below.
		 * TIME UP!! (palette 2, from row 32) stays. The corridor before
		 * it has no HUD: the logo (WL4-9, WL4-7). Only once the fight's
		 * opening is over (the countdown in its place, WL4_V_CLOCKSET),
		 * then to the fight's end, TIME UP too (stay). The countdown's
		 * big figures reach row 35. */
		.hud = {
			.on = { { WL4_SCREEN, 0xFFFF, 2, 2 }, { WL4_LEVEL, 0xFF, 4, 1 }, { WL4_V_CLOCKSET, 0, 1, 1 } },
			/* (the level byte goes 4 to 6 as the boss is beaten: the room
			 * fades with Wario and his hearts, then CLEAR!, screen 1) */
			.stay = { { WL4_SCREEN, 0xFFFF, 2, 2 }, { WL4_LEVEL, 0xFF, 4, 1, true, 6, true } },
			.objs = { { 0, -8, 239, 23, 0, 1023, 0, 1 << 4 | 1 << 7 | 1 << 14 } },
			.rows = 40,
			.logoBelow = true,
			.emptyIsOff = true,
		},
	},
	{
		/* Super Mario Advance 3: Yoshi's Island (USA and Japan; Europe:
		 * the A3AP variant, its language menu first; all three with the
		 * same addresses). In a level the status screen, on a world's
		 * panel its score page: see sm3_compose (SM3-1 to SM3-3).
		 * Nothing leaves the game screen (SM3-6). */
		.codes = { "A3AE", "A3AJ" },
		.title = "Super Mario Advance 3: Yoshi's Island",
		.compose = sm3_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.virt = sm3_virt,
		/* in a level or on a world's panel: taken (about once a second;
		 * the score page only when its world or scores change) */
		.live = { { SM3_V_LIVE, 0, 1, 1 } },
		/* the title, game select, file menu, opening, island map, a bonus
		 * game, Mario Bros.: the logo (SM3-4) */
		.menu = { { SM3_V_MENU, 0, 1, 1 } },
		/* the game screen showing the status screen (its display set)
		 * or a world's score page (from SCORE picked until the cards
		 * have turned back): the logo, not the same on both (SM3-1,
		 * SM3-3); the picture back after */
		.logoIf = { { 0x04000000, 0xFFFF, SM3_STATUS_DISP, 2 }, { 0x0400000E, 0xFFFF, SM3_STATUS_BG3, 2 } },
		.logoIfOr = { { SM3_SCREEN, 0xFF, SM3_ON_PANEL, 1 }, { SM3_SCORE_UP, 0xFF, 1, 1, true, 2, true } },
		/* into a level or back to the panel, or another world's panel
		 * picked: taken again at once */
		.room = { { SM3_SCREEN, 0, 0, 1 }, { SM3_WORLD, 0, 0, 1 } },
		/* Yoshi's Island's own title (from power-on: the Super Mario
		 * Advance 3 title, START, the game select, A): YOSHI'S ISLAND
		 * and its TM on BG1, rows 16-63, the copyright line below them
		 * left out (SM3-5) */
		.logo = { .steps = { { 0, 300 }, { K_START, 5 }, { 0, 200 }, { K_START, 5 }, { 0, 200 }, { K_A, 5 }, { 0, 360 } },
		          .layer = 1, .maxY = 64 },
		/* Controls GBA or SNES (SM3-7): the SNES game's buttons by
		 * position, B bottom jumps (GBA A), Y left the tongue (GBA B), A
		 * right and X top throw an egg (GBA R), both shoulders lock the aim
		 * (GBA L); in a level only. No camera pan (`pan` 0). */
		.controls = {
			.snes = { .right = K_R, .bottom = K_A, .top = K_R, .left = K_B,
			          .when = { { SM3_SCREEN, 0xFF, SM3_IN_LEVEL, 1 } } },
		},
	},
	{
		/* Mario & Luigi: Superstar Saga (USA; Europe and Japan, Mario &
		 * Luigi RPG: variants). Walking about, the Bros. Info screen, the
		 * page of the brother in front: see ml_compose (ML-2) */
		.codes = { "A88E" },
		.title = "Mario & Luigi: Superstar Saga",
		.composePages = ml_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first page) */
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about: taken (about once a second); elsewhere (scenes,
		 * conversations) the last picture stays */
		.live = { { ML_STATE, 0xFFFF, ML_WALKING, 2 } },
		/* a battle, the title: the logo (ML-1, ML-3) */
		.menu = { { ML_STATE, 0xFF, 3, 1, true, 2 } },
		/* the brothers swapped (START): taken again at once */
		.room = { { ML_LEAD, 0xFF, 0, 1 } },
		/* the title's MARIO & LUIGI (BG2) and SUPERSTAR SAGA (sprites),
		 * above PRESS START and the suitcase */
		.logo = { .steps = { { 0, 600 }, { K_START, 4 }, { 0, 120 } }, .layers = 0x14, .maxY = 80,
		          /* (without the sky at the lettering's edge, and its
		           * shadow's dithered specks: ML-1) */
		          .edgeClear = { 0x6BBDD6, 0x635242, 0x73634A }, .rev = 1 },
		.hud = {
			/* in a battle, before its results (ML-5): those stay on the
			 * game screen whole, the logo alone on the second screen */
			.on = { { ML_STATE, 0xFF, 3, 1 }, { ML_RESULTS, 0xFFFF, 0, 2 } },
			/* and once the results are up, off to the battle's end: a
			 * level-up after them clears their panels before its own
			 * (the brother jumping, his level, the star), and stays on
			 * the game screen too (ML-5, issue #29) */
			.offAfter = { { ML_RESULTS, 0xFFFF, 1, 2, true, 0xFFFF, true } },
			/* the boxes' frames (BG3: only ever the boxes; empty in a
			 * conversation, and between the boxes) */
			.bgMask = 0x8,
			/* their text (BG2) only while a box is up: BG2 is also the
			 * tutorials' speech balloons, which stay by the speaker, also
			 * while a box is up (the Starshade Bros.' Bros. Attack
			 * tutorial): their lines, BG2 in palette 15 (the boxes' text
			 * is 9; they never share a line), stay on the game screen
			 * (issue #31) */
			.memLayers = { .when = { { ML_BOX_TOP, 0xF000, 0x8000, 2 }, { ML_BOX_BOTTOM, 0xF000, 0x8000, 2 } }, .bgMask = 0x4, .any = true, .gamePal = 16 },
			/* priority 0: the meters, the menus' cursor and icons (the
			 * blocks, the A over the brother's head and the rest are 2);
			 * from tile 112 only palettes 11 (the cursor and icons) and
			 * 13 (the BROS list's level marks): a Bros. Attack's big
			 * button prompts (7), the "!" over a brother's head (8) and
			 * a speech balloon's marker and edges (15) stay (issue #31).
			 * The
			 * meters' digits and backing (tiles 0-111) only in their
			 * corner (x 4-93, rows 130-142 seen): the damage numbers over
			 * an enemy or a brother are the same digits, and stay */
			.objs = { { -16, 120, 110, 159, 0, 111, 0, 0x7FFF }, { -256, -96, 255, 159, 112, 1023, 0, 0x2800 } },
			/* the meters (bottom left; the bottom box starts at 120) to
			 * the top left corner, against the top right box (from
			 * column 104) and above the item list's cursor (from row 29);
			 * the name box of the enemy aimed at (columns 0-103, rows
			 * 1-21) stays on the game screen, out of their way */
			.moves = { { 0, 120, 117, 159, 'L', 0, 'T', 0 }, { 0, 0, 103, 22, 0, 0, 0, 0, true } },
			.logoBelow = true,
		},
	},
	{
		/* Dragon Ball Z: The Legacy of Goku (USA; Europe: the same, ALGP).
		 * Walking about, the pause menu with the HUD in its empty options
		 * panel: see dbz_compose (DBZ-2 to DBZ-4) */
		.codes = { "ALGE" },
		.title = "Dragon Ball Z: The Legacy of Goku",
		.compose = dbz_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about: taken (about once a second) */
		.live = { { 0x04000000, 0xFFFF, DBZ_FIELD_DISP, 2 }, { 0x04000008, 0xFFFF, 0x1C00, 2 } },
		/* the title (its picture, then Press Start): the logo (DBZ-1) */
		.menu = { { 0x04000000, 0xFFFF, 0x0404, 2, true, 0x0900 } },
		.hud = {
			/* walking about, and in the pause menu (where the bars stay
			 * on the second screen as last seen) */
			/* (the first: walking about, the pause menu or the blank frame
			 * between, not the title: the bars go only there) */
			.on = { { 0x04000000, 0x0F07, 0x0F00, 2, true, 0x0000 },
			        { 0x04000000, 0xFFFF, DBZ_FIELD_DISP, 2, true, DBZ_PAUSE_DISP }, { 0x04000008, 0xFFFF, 0x1C00, 2, true, DBZ_PAUSE_BG0 } },
			/* SELECT held or flying, walking about: the counter instead,
			 * its box on BG0 (which has nothing else then) and its icon a
			 * sprite (DBZ-4); decided as each frame starts drawing, since
			 * the game sets the flag and draws the box in the same vblank */
			.memLayers = { .when = { { DBZ_COUNTER, 0x08, 0x08, 1 }, { 0x04000000, 0xFFFF, DBZ_FIELD_DISP, 2 } }, .bgMask = 0x01 },
			/* the HP / Ki bars (sprites 0-3 at the top left, palettes 14
			 * and 15), or the counter's icon (palette 15, at 6,12) leave
			 * the game screen for the middle of the empty options panel,
			 * over the picture (DBZ-3) */
			/* (any tiles: in a conversation the portrait takes the first,
			 * and the bars' move up) */
			.objs = { { 4, 6, 90, 12, 0, 1023, 1, (1u << 14) | (1u << 15) } },
			/* (taken again only walking about: kept as they were in the
			 * pause menu) */
			.layout = { { 0x04000000, 0xFFFF, DBZ_FIELD_DISP, 2 }, { 0x04000008, 0xFFFF, 0x1C00, 2 } },
			.overMap = true,
			.mapClear = 0x000000,
			/* (taken once it's held still two frames: as SELECT's counter
			 * comes up, its icon's graphics are in the bars' tiles for a
			 * frame) */
			.moves = { { 0, 0, 100, 34, 'C', 79, 'M', 115, .top = true, .settle = 2 } },
		},
		/* the title with THE LEGACY OF GOKU, after A skips the intro: the
		 * lettering inside its mask (DBZ-1) */
		.logo = { .steps = { { 0, 900 }, { K_A, 9 }, { 0, 150 } }, .mask = &logoMaskDBZ },
	},
	{
		/* Dragon Ball Z: The Legacy of Goku II (USA; Japan's International
		 * the same, ALFJ; Europe ALFP). Walking about, the Scouter's area
		 * map, or before the Scouter the Status page: see dbz2_compose
		 * (DBZ2-2, DBZ2-3) */
		.codes = { "ALFE" },
		.title = "Dragon Ball Z: The Legacy of Goku II",
		.compose = dbz2_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about (conversations too: there neither R nor START
		 * opens anything, and the last picture stays, DBZ2-5) */
		.live = { { 0x04000000, 0xFFFF, DBZ2_FIELD_DISP, 2 } },
		/* the area: 0 on the title, the file menu and the intro, 99 flying
		 * over the world: the logo, the last map forgotten (DBZ2-1,
		 * DBZ2-6) */
		.menu = { { 0x03002081, 0xFF, 0x00, 1, true, 0x63 } },
		/* zone and area: a new area's map at once (GEN-6) */
		.room = { { 0x03002080, 0xFFFF, 0, 2 } },
		/* flying over the world (DISPCNT 0x1741, area 99): the inset map at
		 * the top right (BG0 rows 4-49, its marks 8x8 sprites: your arrow,
		 * the places' star and dots) moves to the second screen, twice as
		 * big and in the middle (DBZ2-6). BG0's haze over the horizon,
		 * from row 56 down, stays on the game screen as the game draws it
		 * (blended) */
		.hud = {
			.on = { { 0x04000000, 0xFFFF, 0x1741, 2 }, { 0x03002081, 0xFF, 0x63, 1 } },
			.bgMask = 1,
			.rows = 56,
			.objs = { { 150, -8, 239, 55, 0, 1023, -1, 0, 0, 1u << 0 } },
			/* (grown from what's drawn: the outline, 63x46, centred at
			 * row 80; a mark past its edge grows it a little) */
			.map = { 150, 0, 239, 55 },
			.mapAt = { 2, 120, 125 },
		},
		/* the title, after START skips the intro: DRAGON BALL Z and THE
		 * LEGACY OF GOKU II inside its mask (DBZ2-1) */
		.logo = { .steps = { { 0, 900 }, { K_START, 6 }, { 0, 150 } }, .mask = &logoMaskDBZ2 },
	},
	{
		/* River City Ransom EX (USA; in Japan Downtown Nekketsu Monogatari
		 * EX, with the same RAM and menus). The byte at 0x0300496E is the
		 * game's screen: 1 the title card, 3 the Story / Manage Data menu,
		 * 5 the street (a story's conversations too), 7 the pause menu, 8
		 * to 15 its screens (8 Item, 9 Status, 10 Strategy ... 15 Mode; 15
		 * is also the setup screen before a story, which has no top bar).
		 * Every one of them is BG0 over the street (BG1-3, sprites), with
		 * the top bar (name, stamina bar, money) in rows 0-15 and a black
		 * message box at the bottom. */
		.codes = { "BDTE", "BDTJ" },
		.title = "River City Ransom EX",
		/* the title card's lettering (BG1, on black) above the copyright
		 * lines; from power-on: Story, OK on the setup screen, START to
		 * skip the opening conversation (RCR-1) */
		.logo = { .steps = { { 0, 400 }, { K_A, 4 }, { 0, 140 }, { K_A, 4 }, { 0, 130 }, { K_START, 6 }, { 0, 120 } },
		          .layer = 1, .maxY = 128, .blackIsClear = true },
		/* the Status screen (pause, RIGHT to Status, A), on the street;
		 * the panel and its text (BG0) only, on the pause tint's blue
		 * (0x000094), without the street or the sprites (RCR-2). The pause
		 * menu takes RIGHT only some 20 frames after it opens. */
		.open = { { K_START, 3 }, { 0, 30 }, { K_RIGHT, 3 }, { 0, 8 }, { K_A, 3 } },
		.settleMax = 90, .openWithin = 60, .stableFrames = 3, .maxDiff = 40,
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = 160,
		.mem = { { 0x0300496E, 0xFF, 9, 1 } },
		.live = { { 0x0300496E, 0xFF, 5, 1 } },
		/* the title card, the Story / Manage Data menu (RCR-4) */
		.menu = { { 0x0300496E, 0xFF, 1, 1, true, 3, true } },
		.captureLayers = 0x1,
		.captureBack = 18 << 10,
		.captureHide = { { -512, -256, 511, 255, 0, 1023, -1 } },
		/* the message box along the bottom (rows 128-159: "Alex's
		 * status", OK, Name) left black, without its text (RCR-2) */
		.glyphs = { { 0, 128, 240, 32, -1, { 0x000000 } } },
		.hud = {
			/* the top bar leaves the game screen on the street and in the
			 * pause screens (RCR-3); the Status capture shows it */
			.on = { { 0x0300496E, 0xFF, 5, 1, true, 15, true } },
			.bgMask = 0x1,
			.rows = 16,
			.drop = true,
		},
	},
	{
		/* USA: see fe_compose */
		.codes = { "AE7E" },
		.title = "Fire Emblem",
		.virt = fe_virt,
		.composePages = fe_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the status's first page) */
		/* the cursor moved: the next unit's status; a unit selected or
		 * let go: its status off or on again */
		.room = { { FE_V_CURSOR, 0, 0, 4 }, { FE_V_FREE, 0, 0, 4 } },
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.live = { FE_IN_CHAPTER, { FE_V_TURN, 0xFF, 0, 1 } },
		/* off the battle map: the title, a story scene (the logo) */
		.menu = { { FE_V_OFFMAP, 0xFF, 1, 4 } },
		.hud = {
			/* nothing moves off the game screen (FE-10); this is for the
			 * logo wherever there's no status (FE-1): in the map pictures
			 * (logoUnder), and wherever there's no map picture yet, in
			 * the game's chapters and outside them (logoAlways) */
			.on = { FE_IN_CHAPTER },
			.overMap = true,
			.mapClear = MAP_BG_NAVY,
			.logoUnder = true,
			.logoAlways = true,
		},
		/* fights, and the other armies' turns: the logo alone */
		.combat = { { FE_V_FIGHT, 0xFF, 1, 4 } },
		.combatOr = { FE_IN_CHAPTER, { FE_V_TURN, 0xFF, 0x40, 1, true, 0xFF, true } },
		/* the title's lettering and sword (sprites), its shadow (BG1)
		 * and axe (BG2), not the copyright line */
		.logo = { .wait = 300, .pressStart = true, .wait2 = 64, .layers = 0x16, .maxY = 140 },
	},
	{
		/* USA: as Fire Emblem (the same engine and second screen: see
		 * fe_compose; its addresses in feRegions) */
		.codes = { "BE8E" },
		.title = "Fire Emblem: The Sacred Stones",
		.virt = fe_virt,
		.composePages = fe_compose,
		.stillPick = STILL_FIRST,
		.room = { { FE_V_CURSOR, 0, 0, 4 }, { FE_V_FREE, 0, 0, 4 } },
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.live = { FE_IN_CHAPTER, { FE_V_TURN, 0xFF, 0, 1 } },
		.menu = { { FE_V_OFFMAP, 0xFF, 1, 4 } },
		.hud = {
			.on = { FE_IN_CHAPTER },
			.overMap = true,
			.mapClear = MAP_BG_NAVY,
			.logoUnder = true,
			.logoAlways = true,
		},
		.combat = { { FE_V_FIGHT, 0xFF, 1, 4 } },
		.combatOr = { FE_IN_CHAPTER, { FE_V_TURN, 0xFF, 0x40, 1, true, 0xFF, true } },
		/* the title (A past the health and safety screen, START past the
		 * intro): the dragons (BG0) under the lettering and its ribbon
		 * (sprites), above Press START and the copyright line (sprites,
		 * from row 120) */
		.logo = { .steps = { { 0, 200 }, { K_A, 4 }, { 0, 700 }, { K_START, 4 }, { 0, 90 } }, .layers = 0x11, .maxY = 119 },
	},
	{
		/* USA; the second screen is put together by aw_compose */
		.codes = { "AWRE" },
		.title = "Advance Wars",
		.compose = aw_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* a fight: both windows on */
		.combat = { { AW_ARMY, 0xFF, 1, 1, true, 4, true }, { 0x04000000, 0x4000, 0x4000, 2 } },
		/* the computer's turn: the logo too (AW-4) */
		.virt = aw_virt,
		.combatOr = { { AW_V_CPU, 0xFF, 1, 4 } },
		.live = { { AW_ARMY, 0xFF, 1, 1, true, 4, true } },
		/* off the battle map (the title, menus, a battle's results; between
		 * turns too, where the logo's up already): the pictures go (AW-6) */
		.menu = { { AW_V_OFFMAP, 0xFF, 1, 4 } },
		.room = { { AW_CURSOR, 0xFFFF, 0, 2 } },
		/* the title's lettering is sprites */
		.logo = { .wait = 240, .pressStart = true, .wait2 = 100, .layers = 0x10, .maxY = 128 },
	},
	{
		/* Advance Wars 2 (USA): as Advance Wars, at its own addresses */
		.codes = { "AW2E" },
		.title = "Advance Wars 2: Black Hole Rising",
		.compose = aw_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* a fight: both windows on */
		.combat = { { AW2_ARMY, 0xFF, 1, 1, true, 4, true }, { 0x04000000, 0x4000, 0x4000, 2 } },
		/* the computer's turn: the logo too (AW2-4) */
		.virt = aw_virt,
		.combatOr = { { AW_V_CPU, 0xFF, 1, 4 } },
		.live = { { AW2_ARMY, 0xFF, 1, 1, true, 4, true } },
		/* off the battle map (as Advance Wars': AW2-6) */
		.menu = { { AW_V_OFFMAP, 0xFF, 1, 4 } },
		.room = { { AW2_CURSOR, 0xFFFF, 0, 2 } },
		/* the title's lettering is sprites (PRESS START! below it too) */
		.logo = { .wait = 240, .pressStart = true, .wait2 = 100, .layers = 0x10, .maxY = 92,
		          .rev = 1 /* the first builds saved it with PRESS START! */ },
	},
	{
		/* Game Boy Wars Advance 1+2 (Japan): Advance Wars and Advance Wars 2,
		 * each with its own code and RAM, picked from the compilation's
		 * menu. Its own menus: the logo; in either game, that game's
		 * profile (the BGW1 and BGW2 variants), switched to by gbw_pick */
		.codes = { "BGWJ" },
		.title = "Game Boy Wars Advance 1+2",
		.logoOnly = true,
		.pick = gbw_pick,
		/* the title's lettering is sprites (PRESS START! below it too) */
		.logo = { .wait = 640, .layers = 0x10, .maxY = 92 },
	},
	{
		/* Kingdom Hearts: Chain of Memories (USA; Europe and Japan:
		 * khc_eur, khc_jpn). Walking about in a world, the World Map:
		 * see khc_compose (KHC-1, KHC-2) */
		.codes = { "B8CE" },
		.title = "Kingdom Hearts: Chain of Memories",
		.compose = khc_compose,
		.virt = khc_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* not in a battle, not on the title: taken (about once a second;
		 * where the menu doesn't open, nothing: KHC-2) */
		.live = { { KHC_BATTLE, 0xFFFFFFFF, 0, 4 }, { KHC_V_TITLE, 0xFF, 0, 4 } },
		/* the title: the logo, the map forgotten (KHC-4) */
		.menu = { { KHC_V_TITLE, 0xFF, 1, 4 } },
		/* a battle: its pointer set (the fight, its conversations), or the
		 * display in mode 2 (its flash, its end): the logo (KHC-3) */
		.combat = { { KHC_BATTLE + 3, 0xFF, 0x02, 1 } },
		.combatOr = { { 0x04000000, 0x0007, 2, 2 } },
		/* the emblem full size on black before the title, faded in, before
		 * it flashes and shrinks (KHC-5): BG2 alone */
		.logo = { .wait = 700, .layer = 2, .maxY = 160 },
	},
	{
		/* Yu-Gi-Oh! The Eternal Duelist Soul (USA): the Card View of the
		 * card under the cursor, see eds_compose (EDS-1 to EDS-5) */
		.codes = { "AY5E" },
		.title = "Yu-Gi-Oh! The Eternal Duelist Soul",
		.compose = eds_compose,
		.virt = eds_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.live = { { EDS_V_LIVE, 0xFF, 1, 4 } },
		/* out of the duels and the card screens: the logo, the card
		 * forgotten (EDS-6) */
		.menu = { { EDS_V_IDLE, 0xFF, 1, 4 } },
		.room = { { EDS_V_ROOM, 0, 0, 4 } },
		/* the title's lettering (BG0), flame (BG1) and disc (BG2), above
		 * New Game and the copyright line (EDS-7) */
		.logo = { .wait = 900, .layers = 0x07, .maxY = 100 },
	},
	{
		/* Zoids: Legacy (USA; Japan, Zoids Saga II: its addresses in
		 * zlRegions). Walking about, in a town, on the world or in a
		 * dungeon, the map screen (ZL-1); battles and the past the logo
		 * (ZL-3, ZL-7); see zl_virt */
		.codes = { "AZ2E" },
		.title = "Zoids: Legacy",
		.virt = zl_virt,
		/* the map, the past's message, a Zoid's Status: zl_compose
		 * (ZL-1, ZL-7, ZL-8) */
		.composePages = zl_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about, or a Zoid choosing: taken (where nothing opens,
		 * nothing: ZL-2) */
		.live = { { ZL_V_LIVE, 0xFF, 1, 4 } },
		/* the title, or a battle playing out: what was shown goes (ZL-3,
		 * ZL-4) */
		.menu = { { ZL_V_FORGET, 0xFF, 1, 4 } },
		/* a battle playing out: the logo (ZL-3) */
		.combat = { { ZL_V_PLAYOUT, 0xFF, 1, 4 } },
		/* into a town, a dungeon or the past, out again, or a Zoid's menu
		 * coming up: a capture at once (ZL-1, ZL-7, ZL-8) */
		.room = { { ZL_V_PLACE, 0xFF, 0, 4 }, { ZL_V_MAPNO, 0xFFFF, 0, 4 }, { ZL_V_CHOOSE, 0xFF, 0, 4 } },
		/* the town list's cursor (the triangle by the first town: it spins,
		 * tiles 1012-1022; the map's marker is 936-945) */
		.captureHide = { { 100, -16, 180, 159, 1012, 1023, -1 } },
		/* the title's lettering is sprites (PRESS START and the copyright
		 * line below it left out) */
		.logo = { .wait = 1300, .layers = 0x10, .maxY = 100 },
	},
	{
		/* Harvest Moon: Friends of Mineral Town (USA, Europe; Japan:
		 * hm_jpn). Walking about: the town map, see hm_compose (HM-1 to
		 * HM-3) */
		.codes = { "A4NE", "A4NP", "A4ND" },
		.title = "Harvest Moon: Friends of Mineral Town",
		.compose = hm_compose,
		.virt = hm_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* not on the title: taken twice a second (the head and the clock
		 * keep moving: HM-1); where L+START opens nothing, nothing (HM-3) */
		.live = { { HM_V_TITLE, 0xFF, 0, 4 } },
		.interval = 30,
		/* the title: the logo, the map forgotten (HM-4) */
		.menu = { { HM_V_TITLE, 0xFF, 1, 4 } },
		/* the title's sign (sprites), without the rider at its left and
		 * what's below it (HM-5) */
		.logo = { .wait = 1000, .layer = 4, .minX = 46, .maxY = 96 },
	},
	{
		/* Super Mario Advance (USA and Europe; Japan, Super Mario USA +
		 * Mario Brothers, the same: its levels use the same display
		 * settings). Super Mario Bros. 2 has no map and its pause box only
		 * the overall Ace Coin percentage: no capture, the logo, and in a
		 * level the status bar on the second screen (SMB2-1). */
		.codes = { "AMAE", "AMAJ" },
		.title = "Super Mario Advance",
		/* The "map": the pause box's Ace Coin percentage (START on the
		 * hidden copy, in a level, about once a second), which counts the
		 * coins found in this level: laid over the course board's own
		 * under the status bar (hud.held.over, SMB2-9). The pause box is
		 * sprites over a cream fill (0xFFFFCE, a darker rim), with the
		 * window registers on (DISPCNT 0x7F00); its number right-aligned,
		 * ending at x 144, rows 50-62. */
		.open = { { K_START, 4 } },
		.settleMax = 90, .openWithin = 40, .stableFrames = 3, .maxDiff = 0,
		.crop_x = 112, .crop_y = 50, .crop_w = 34, .crop_h = 13,
		.probes = { { 50, 30, 0xFFFFCE }, { 190, 30, 0xFFFFCE }, { 120, 20, 0xFFFFCE }, { 120, 47, 0xFFFFCE },
		            { 50, 140, 0xFFE7A5 }, { 190, 140, 0xFFE7A5 } },
		.tolerance = 8,
		.mem = { { 0x04000000, 0xE000, 0x6000, 2 }, { 0x0400000E, 0xFFFC, 0x1900, 2 } },
		/* in a level, not paused (no windows) */
		.live = { { 0x0400000E, 0xFFFC, 0x1900, 2 }, { 0x04000000, 0xE807, 0x0800, 2 } },
		.interval = 60,
		/* the game select and file select: forgotten (another file's) */
		.menu = { { 0x0400000E, 0xFF00, 0x3E00, 2 } },
		/* out of a level (BG3 anything but 0x19xx): the logo, never the
		 * number by itself (SMB2-4, SMB2-5) */
		.logoIf = { { 0x0400000E, 0xFF00, 0x1A00, 2, true, 0xFF00, true } },
		.logoIfOr = { { 0x0400000E, 0xFF00, 0x0000, 2, true, 0x1800, true } },
		/* the title (START through the intro): SUPER MARIO ADVANCE with
		 * Mario and Birdo round it, BG1 (mode 1); the choices and the
		 * copyright line are sprites, the trees and hills BG0, the sky
		 * BG2 (SMB2-6) */
		.logo = { .steps = { { 0, 600 }, { K_START, 4 }, { 0, 150 } }, .layer = 1, .maxY = 140 },
		/* In a level (both releases): mode 0 with BG3 on, BG3 the status
		 * bar (0x1900, priority 0): rows 0-15 (WORLD, the level's Ace
		 * Coins, lives, SCORE), the pause box below them on the same
		 * layer. Elsewhere BG3 is 0x1C03 (character select, course board,
		 * level card), 0x3E03 (title, menus) or 0xD603 (Mario Bros.). The
		 * hearts are sprites and stay (SMB2-1, SMB2-2). */
		.hud = {
			.on = { { 0x0400000E, 0xFFFC, 0x1900, 2 }, { 0x04000000, 0x0807, 0x0800, 2 } },
			/* the screen blanked between rooms (BG3 still 0x1900): the
			 * last picture stays (SMB2-3) */
			.keep = { { 0x0400000E, 0xFFFC, 0x1900, 2 } },
			.bgMask = 8,
			.rows = 16,
			.logoBelow = true,
			/* The course board before each level (all 20 levels and their
			 * Ace Coins, BG0 in mode 1, 0x1101, at BG0HOFS 0x10 once in
			 * place; it fades in from white): kept from its first frame
			 * in place, its panel (rows 8-150: the frame's top edge to the
			 * grass) below the status bar from row 17, instead of the logo
			 * (SMB2-8). Probes on the panel's cream and the black line
			 * along its bottom: the same on the Yoshi Challenge's board
			 * (whose frame is cyan, not orange), not on the level card. */
			.held = { .when = { { 0x04000000, 0xFFFF, 0x1101, 2 }, { 0x04000010, 0x01FF, 0x0010, 2 } },
			          .probes = { { 120, 12, 0xFFF7BD }, { 60, 80, 0xFFF7BD }, { 180, 80, 0xFFF7BD },
			                      { 60, 144, 0x000000 }, { 180, 144, 0x000000 } },
			          /* (one may miss: a little creature walks the grass) */
			          .min = 4, .tol = 8, .y0 = 8, .y1 = 150, .at = 17,
			          /* the board's percentage (right-aligned, from x 179 at
			           * 100% to 207, rows 129-142; its = ends at 174)
			           * replaced by the pause box's, whose 100% reaches x 178
			           * here (until the fourth test build the box began at
			           * 180 and cut the 1's left edge off) */
			          .over = { 176, 129, 208, 142, 0xFFF7BD, 0xFFFFCE } },
		},
	},
	{
		/* Mega Man Battle Network (USA; Europe and Japan, Battle Network
		 * Rockman EXE: variants). Walking about, the PET's MegaMan
		 * screen: see mmbn_compose (MMBN-2); in battles the custom screen
		 * on the second screen (MMBN-4) */
		.codes = { "AREE" },
		.title = "Mega Man Battle Network",
		.compose = mmbn_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about (scenes too: START opens nothing there, the last
		 * picture stays, MMBN-3) */
		.live = { { MMBN_STATE, 0xFF, 4, 1 } },
		/* the title, a battle: the logo, the status forgotten (MMBN-1,
		 * MMBN-3: an old HP never shown after a battle) */
		.menu = { { MMBN_STATE, 0xFF, 0, 1, true, 8 } },
		/* HP (back from a battle, a recovery), its maximum, the area, the money:
		 * taken again at once (MMBN-2) */
		.room = { { MMBN_HP, 0xFFFF, 0, 2 }, { MMBN_HP + 2, 0xFFFF, 0, 2 }, { MMBN_AREA, 0xFFFF, 0, 2 }, { MMBN_ZENNY, 0, 0, 4 } },
		/* the title's lettering and emblem (BG3), above PRESS START */
		.logo = { .wait = 400, .layers = 0x8, .maxY = 96 },
		.hud = {
			/* in a battle, while the custom screen is in: BG3HOFS
			 * 0x188 in place, sliding from and back to 0 in steps of 8
			 * (0 while fighting, the HP box at the top left; the
			 * results slide in from 0xF0). Not the battle's step: in
			 * the tutorials the custom screen stays up, the teacher
			 * talking over it, after the step has gone on to 8
			 * (MMBN-4) */
			.on = { { MMBN_STATE, 0xFF, 8, 1 }, { 0x0400001C, 0x1FF, 0x188, 2, true, 0x1F8, true },
			        { 0x0400001C, 0x7, 0, 2 } },
			/* the custom screen and the HP box beside it */
			.bgMask = 0x8,
			/* its cursor (priority 0, palette 13; the field's sprites
			 * are 2), and the tutorials' arrows (the same) where they
			 * point into it; not their arrows over the field, or the
			 * teacher's portrait by her message (priority 0, palette 14) */
			.objs = { { -256, -256, 119, 255, 0, 1023, 0, 1u << 13 } },
			/* on black (GEN-1), with no logo: the custom screen fills
			 * the screen's height, the logo has no room below it */
			.logoBelow = true,
			.rows = GBA_H,
		},
	},
};

/* ---------------------------------------------------------------- regions
 *
 * European and Japanese releases reuse the USA profile. Where their RAM
 * layout moved, memShift is added to every memory probe address (found by
 * matching the code that reads them in the two ROMs); patch
 * adjusts anything else that differs (button sequences through extra
 * screens, lettering positions). */
typedef struct {
	const char* code;
	const char* base;
	int memShift;
	void (*patch)(MapProfile*);
} Variant;

/* Inserts n steps at position at (arrays hold max entries). */
static void insert_steps(MacroStep* steps, int max, int at, const MacroStep* add, int n) {
	int len = 0;
	while (len < max && steps[len].frames) ++len;
	if (len + n > max) return;
	memmove(&steps[at + n], &steps[at], (size_t) (len - at) * sizeof(*steps));
	memcpy(&steps[at], add, (size_t) n * sizeof(*steps));
}

/* European multi-language releases start with a language menu (English
 * selected): wait for it and press A, then boot as the USA release does. */
static const MacroStep LANGUAGE_A[] = { { 0, 60 }, { K_A, 4 } };

static void lang_menu(MapProfile* p) {
	insert_steps(p->logo.steps, 24, 0, LANGUAGE_A, 2);
	if (p->frame.steps[0].frames) insert_steps(p->frame.steps, 24, 0, LANGUAGE_A, 2);
}

/* Super Mario Advance 3 in Europe: the language menu (A: English), then
 * a story screen before the Super Mario Advance 3 title (SM3-5) */
static void sm3_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 300 }, { K_A, 5 }, { 0, 300 }, { K_START, 5 }, { 0, 200 }, { K_START, 5 },
	                                   { 0, 200 }, { K_A, 5 }, { 0, 360 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* Fire Emblem: The Sacred Stones in Europe: the language menu (A for
 * English), then as the USA's; in Japan, no health and safety screen */
static void fe8_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 100 }, { K_A, 4 }, { 0, 250 }, { K_A, 4 }, { 0, 700 }, { K_START, 4 }, { 0, 90 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* Mario & Luigi: Superstar Saga in Europe: a language menu first (A:
 * English); the title is up by the end */
static void ml_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 700 }, { K_A, 8 }, { 0, 600 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* Dragon Ball Z: The Legacy of Goku in Europe: a language menu first
 * (A: English); the counter's flag 0x3840 lower (at the USA's address
 * Europe holds 0xFF, which took BG0, conversations' boxes too, off the
 * game screen walking about: DBZ-5) */
static void dbz_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 300 }, { K_A, 6 }, { 0, 700 }, { K_A, 9 }, { 0, 150 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
	p->hud.memLayers.when[0].addr = 0x0200000C;
}
/* Dragon Ball Z: The Legacy of Goku II in Europe: a language menu first
 * (A: English), then the title on its own (no intro); its zone and area
 * 0x1174 lower */
static void dbz2_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 450 }, { K_A, 6 }, { 0, 800 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
	p->menu[0].addr = 0x03000F0D;
	p->room[0].addr = 0x03000F0C;
	p->hud.on[1].addr = 0x03000F0D;
}
/* ... in Japan (International): the same RAM; its own title (with
 * INTERNATIONAL under the logo) */
static void dbz2_jpn(MapProfile* p) {
	p->logo.mask = &logoMaskDBZ2J;
}
static void fe8_jpn(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 400 }, { K_START, 4 }, { 0, 90 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

/* Fire Emblem in Japan: "Press start" comes higher, under the subtitle */
static void fe_jpn(MapProfile* p) {
	p->logo.maxY = 123;
}

/* Sword of Mana in Europe: the language menu (it takes A once it's faded
 * in), then the title comes by itself */
static void som_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 150 }, { K_A, 4 }, { 0, 1400 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* and in Japan: 新約 over the title is sprites */
static void som_jpn(MapProfile* p) {
	p->logo.layers = 0x11;
}

/* Advance Wars (1 and 2) in Europe: the language menu comes up later than
 * the other games' */
static void aw_eur(MapProfile* p) {
	static const MacroStep lang[] = { { 0, 180 }, { K_A, 4 } };
	insert_steps(p->logo.steps, 24, 0, lang, 2);
}

/* Game Boy Wars Advance 1+2's two games: as Advance Wars and Advance
 * Wars 2, at their addresses there (the compose code's: awRegions) */
static void gbw_set(MapProfile* p, uint32_t army, uint32_t cursor) {
	p->combat[0].addr = army;
	p->live[0].addr = army;
	p->room[0].addr = cursor;
	memset(&p->logo, 0, sizeof(p->logo)); /* (the compilation's: BGWJ) */
}

static void gbw1(MapProfile* p) {
	p->title = "Game Boy Wars Advance 1+2: Game Boy Wars Advance";
	gbw_set(p, GBW1_ARMY, GBW1_CURSOR);
}

static void gbw2(MapProfile* p) {
	p->title = "Game Boy Wars Advance 1+2: Game Boy Wars Advance 2";
	gbw_set(p, GBW2_ARMY, GBW2_CURSOR);
}

static void thps2_jpn(MapProfile* p) {
	/* SK8: its own badge, and a later title screen */
	p->logo.wait = 1130;
	p->logo.mask = &logoMaskSK8;
}

static void zelda_eur(MapProfile* p) {
	p->listName = "Legend of Zelda, The - A Link to the Past (Europe)";
	/* the language menu comes after the title, where USA has the file menu */
	static const MacroStep lang[] = { { K_A, 4 }, { 0, 60 } };
	insert_steps(p->logo.steps, 24, 1, lang, 2);
}
static void zelda_jpn(MapProfile* p) {
	p->listName = "Zelda no Densetsu - Kamigami no Triforce (Japan)";
}


/* Super Mario Advance 2 in Japan: the game mode is 0x10 lower, the
 * camera's pan direction 0x14 (the I/O registers don't move);
 * the world map's paused flag isn't where it is in the others (left out) */
static void sma2_jpn(MapProfile* p) {
	p->room[0].addr -= 0x10; /* (Mario's place on the world map, as the others) */
	p->room[1].addr -= 0x10;
	p->live[0].addr -= 0x10;
	p->live[2] = (MemProbe) { 0 };
	p->hud.on[0].addr -= 0x10;
	p->hud.keep[0].addr -= 0x10;
	p->hud.stay[0].addr -= 0x10;
	p->menu[0].addr -= 0x10;
	p->controls.snes.when[0].addr -= 0x10;
	p->controls.snes.when[2].addr -= 0x10;
	p->controls.snes.pan -= 0x14;
}

/* Super Mario Advance 4 in Europe and Japan, and the USA's Rev 1 (AX4R):
 * where Mario is (his place on the world map, whether he's taking input
 * there) 4 bytes lower; the rest as in the USA's first release. Every
 * revision of Europe's and Japan's tried (Europe's Rev 1 Wii U Virtual
 * Console, Japan's Rev 2) is like its first. Europe asks for a language
 * first. */
static void sma4_moved(MapProfile* p) {
	p->pick = NULL;
	p->live[0].addr -= 4;
	p->room[0].addr -= 4;
	p->room[1].addr -= 4;
	p->room[2].addr -= 4;
	p->menu[0].addr -= 4;
	p->hud.on[0].addr -= 4;
	p->hud.alt.on[0].addr -= 4;
	p->hud.keep[0].addr -= 4;
	p->logoIf[0].addr -= 4;
}
static void sma4_eur(MapProfile* p) {
	sma4_moved(p);
	/* (A on the language menu first) */
	static const MacroStep steps[] = { { 0, 300 }, { K_A, 4 }, { 0, 1600 }, { K_START, 4 }, { 0, 150 }, { K_A, 4 }, { 0, 150 },
	                                   { K_A, 4 }, { 0, 300 }, { K_START, 4 }, { 0, 150 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

/* Final Fantasy Tactics Advance in Europe: a language menu first (its RAM
 * is the USA release's); in Japan the cursor's 0x30 lower (ft_r) */
/* Golden Sun: The Lost Age elsewhere: each release's logo (its emblem
 * sits a little differently in each title picture); in Japan, the one choosing 0x200 lower (gs2Addr) */
static void gs2_ger(MapProfile* p) {
	p->logo.mask = &logoMaskGS2D;
}
static void gs2_fra(MapProfile* p) {
	p->logo.mask = &logoMaskGS2F;
}
static void gs2_ita(MapProfile* p) {
	p->logo.mask = &logoMaskGS2I;
}
static void gs2_spa(MapProfile* p) {
	p->logo.mask = &logoMaskGS2S;
}
static void gs2_jpn(MapProfile* p) {
	p->room[0].addr -= 0x200;
	p->logo.mask = &logoMaskGS2J;
}

/* Golden Sun in Germany: the battle's bytes 0x10 higher (GS_GER) */
static void gs_ger(MapProfile* p) {
	p->combat[1].addr += 0x10;
}

static void ffta_eur(MapProfile* p) {
	lang_menu(p);
}

static void ffta_jpn(MapProfile* p) {
	p->room[0].addr -= 0x30;
	p->room[1].addr -= 0x30;
}

/* Tony Hawk's Pro Skater 3: the goal list's heading, VOIR OBJ. / ZIELE ZEIGEN */
static void thps3_fra(MapProfile* p) {
	static const Probe show[8] = { { 93, 32, 0x00D6FF }, { 101, 30, 0x00D6FF }, { 108, 32, 0x00D6FF }, { 116, 31, 0x00D6FF },
	                               { 124, 32, 0x00D6FF }, { 132, 32, 0x00D6FF }, { 140, 34, 0x00D6FF }, { 147, 34, 0x00D6FF } };
	memcpy(p->probes, show, sizeof(show));
}

static void thps3_ger(MapProfile* p) {
	static const Probe show[8] = { { 80, 30, 0x00D6FF }, { 91, 32, 0x00D6FF }, { 103, 34, 0x00D6FF }, { 111, 30, 0x00D6FF },
	                               { 124, 30, 0x00D6FF }, { 135, 32, 0x00D6FF }, { 148, 32, 0x00D6FF }, { 159, 32, 0x00D6FF } };
	memcpy(p->probes, show, sizeof(show));
}

/* F-Zero: Maximum Velocity in China: the mode byte 0x20 higher
 * (0x03000C1B), the HUD's state 0x40 lower (0x02016889: the EWRAM block's
 * literals, 0x020167B0... here 0x02016770...); its title shows ~200 frames
 * later */
static void fzm_chn(MapProfile* p) {
	p->hud.on[0].addr += 0x20;
	p->hud.on[1].addr += 0x20;
	p->hud.on[2].addr -= 0x40;
	p->logo.steps[0].frames = 1100;
}

/* Crash Nitro Kart in Japan: the player's block 0x30 higher (its pause byte
 * 0x030045D1), the BG0 cells and registers the same; its own title, shown
 * from frame ~1050 to ~1170 before its menu comes up by itself */
static void cnk_jpn(MapProfile* p) {
	p->hud.on[3].addr += 0x10; /* (the fade byte: 0x03002A6B) */
	p->hud.keep[0].addr += 0x30;
	p->logo.wait = 1110;
	p->logo.mask = &logoMaskCNKJ;
}

/* V-Rally 3 in Japan: no language screen, so the main menu (the logo's
 * place, VR3-1) is up three presses sooner; the HUD the same */
static void vr3_jpn(MapProfile* p) {
	p->logo.steps[13].frames = 0;
	p->logo.mask = &logoMaskVR3J;
}

/* Spider-Man: Mysterio's Menace in Japan: the screen's handler 0x34 lower
 * (0x0300213C), and its values too (0x60F8 in a level, 0x41D8 elsewhere);
 * the display registers are the same */
/* Ninja Cop (Europe): its title's NINJA COP to its white outline, without
 * the black and purple rim round it or the blue screen (NFO-1) */
static void nfo_eur(MapProfile* p) {
	p->logo.mask = &logoMaskNFOP;
}

static void smm_jpn(MapProfile* p) {
	p->mem[1].addr -= 0x34, p->mem[1].value -= 0x34;
	p->live[1].addr -= 0x34, p->live[1].value -= 0x34;
	p->menu[0].addr -= 0x34, p->menu[0].value -= 0x34;
}

/* Kingdom Hearts: Chain of Memories: the battle pointer elsewhere (by
 * romxref, checked in battles); the logo the same emblem, in Europe after
 * A on its language menu, in Japan 200 frames sooner (no licence screen) */
static void khc_eur(MapProfile* p) {
	p->live[0].addr = 0x0203A3E4;
	p->combat[0].addr = 0x0203A3E4 + 3;
	p->logo.wait = 0;
	p->logo.steps[0] = (MacroStep) { 0, 100 };
	p->logo.steps[1] = (MacroStep) { K_A, 4 };
	p->logo.steps[2] = (MacroStep) { 0, 746 };
}
static void khc_jpn(MapProfile* p) {
	p->live[0].addr = 0x02039D44;
	p->combat[0].addr = 0x02039D44 + 3;
	p->logo.wait = 500;
}

/* Yu-Gi-Oh! Duel Monsters 5 Expert 1 (Japan), The Eternal Duelist Soul's
 * original: the same second screen with its own screens' registers, pixels
 * and addresses (edsJP, EDS-10); its logo the title's lettering (BG1), above
 * the copyright lines, after START past the opening (EDS-9) */
static void eds_jpn(MapProfile* p) {
	p->compose = eds_compose_jp;
	p->virt = eds_virt_jp;
	p->logo = (typeof(p->logo)) { .steps = { { 0, 1440 }, { K_START, 4 }, { 0, 120 } }, .layer = 1, .maxY = 120 };
}

/* Harvest Moon: Friends of Mineral Town in Japan (Bokujou Monogatari:
 * Mineral Town no Nakama-tachi): its own title sign, wider and lower, with
 * no rider beside it */
static void hm_jpn(MapProfile* p) {
	p->logo.minX = 0;
	p->logo.maxY = 113;
}

/* Battle Network Rockman EXE (Japan): its logo, ロックマン エグゼ, fills
 * BG3 with the blue behind it (MMBN-1) */
static void mmbn_jpn(MapProfile* p) {
	p->logo.maxY = GBA_H;
	p->logo.edgeClear[0] = 0x52B5FF;
	p->logo.rev = 1;
}

static const Variant variants[] = {
	{ "AGSD", "AGSE", 0, gs_ger },
	/* Golden Sun: The Lost Age's other releases */
	{ "AGFD", "AGFE", 0, gs2_ger },
	{ "AGFF", "AGFE", 0, gs2_fra },
	{ "AGFI", "AGFE", 0, gs2_ita },
	{ "AGFS", "AGFE", 0, gs2_spa },
	{ "AGFJ", "AGFE", 0, gs2_jpn },
	/* F-Zero: GP Legend in Europe: its language menu first (the title
	 * comes ~700 frames after A); its races not tried (f-zero-gp-legend.md) */
	{ "BFZP", "BFZE", 0, lang_menu },
	{ "AFZC", "AFZE", 0, fzm_chn },
	{ "BCNJ", "BCNE", 0, cnk_jpn },
	{ "AVRJ", "AVRE", 0, vr3_jpn },
	{ "A2CP", "A2CE", 0, lang_menu },
	{ "A2CJ", "A2CE", -0x30, NULL },
	{ "ACHP", "ACHE", 0, NULL },
	{ "ACHJ", "ACHE", -0x30, NULL },
	{ "AAMP", "AAME", 0, NULL },
	{ "AAMJ", "AAME", 0, NULL },
	{ "BMXP", "BMXE", 0, lang_menu },
	{ "BMXJ", "BMXE", 0, NULL },
	{ "AMTP", "AMTE", 0, lang_menu },
	{ "AMTJ", "AMTE", 0x34, NULL },
	{ "AZLP", "AZLE", 0, zelda_eur },
	{ "AZLJ", "AZLE", 0, zelda_jpn },
	{ "ATHJ", "ATHE", 0, thps2_jpn },
	{ "AFXP", "AFXE", 0, ffta_eur },
	{ "AFXJ", "AFXE", 0, ffta_jpn },
	{ "AA2J", "AA2E", 0, sma2_jpn },
	{ "AX4P", "AX4E", 0, sma4_eur },
	{ "AX4J", "AX4E", 0, sma4_moved },
	{ "AX4R", "AX4E", 0, sma4_moved },
	{ "AT3F", "AT3E", 0, thps3_fra },
	{ "AT3D", "AT3E", 0, thps3_ger },
	{ "ASEJ", "ASEE", 0, smm_jpn },
	{ "ALDP", "ALDE", 0, NULL },
	{ "ANXP", "ANXE", 0, nfo_eur },
	{ "AWAJ", "AWAE", 0, NULL },
	/* Super Mario Advance 3 in Europe: its language menu first */
	{ "A3AP", "A3AE", 0, sm3_eur },
	/* (Fire Emblem's addresses: FeRegion) */
	{ "AE7X", "AE7E", 0, lang_menu },
	{ "AE7Y", "AE7E", 0, lang_menu },
	{ "AE7J", "AE7E", 0, fe_jpn },
	{ "BE8P", "BE8E", 0, fe8_eur },
	{ "ALGP", "ALGE", 0, dbz_eur },
	{ "ALFP", "ALFE", 0, dbz2_eur },
	{ "ALFJ", "ALFE", 0, dbz2_jpn },
	{ "A88P", "A88E", 0, ml_eur },
	{ "A88J", "A88E", 0, NULL },
	{ "BE8J", "BE8E", 0, fe8_jpn },
	{ "AWRP", "AWRE", 0, aw_eur },
	/* (the same RAM as the USA release; a language menu first too) */
	{ "AW2P", "AW2E", 0, aw_eur },
	/* (the two games in Game Boy Wars Advance 1+2: BGWJ's pick) */
	{ "BGW1", "AWRE", 0, gbw1 },
	{ "BGW2", "AW2E", 0, gbw2 },
	/* (Sword of Mana's ring address: SomRegion) */
	{ "AVSY", "AVSE", 0, som_eur },
	{ "AVSJ", "AVSE", 0, som_jpn },
	{ "B8CP", "B8CE", 0, khc_eur },
	{ "B8CJ", "B8CE", 0, khc_jpn },
	{ "AY5J", "AY5E", 0, eds_jpn },
	/* (Zoids Saga II's addresses: ZlRegion) */
	{ "AZ2J", "AZ2E", 0, NULL },
	{ "A4NJ", "A4NE", 0, hm_jpn },
	/* (Mega Man Battle Network's RAM: the same in every release) */
	{ "AREP", "AREE", 0, NULL },
	{ "AREJ", "AREE", 0, mmbn_jpn },
};
#define N_VARIANTS (sizeof(variants) / sizeof(*variants))
static MapProfile variantProfiles[N_VARIANTS];
static pthread_once_t variantsOnce = PTHREAD_ONCE_INIT;

static const MapProfile* base_find(const char* gameCode) {
	for (size_t i = 0; i < sizeof(profiles) / sizeof(*profiles); ++i) {
		for (int j = 0; j < 4 && profiles[i].codes[j]; ++j) {
			if (!strncmp(profiles[i].codes[j], gameCode, 4)) {
				return &profiles[i];
			}
		}
	}
	return NULL;
}

static void shift(MemProbe* list, int n, int by) {
	for (int i = 0; i < n && list[i].addr; ++i) list[i].addr += by;
}

static void build_variants(void) {
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		const Variant* v = &variants[i];
		MapProfile* p = &variantProfiles[i];
		const MapProfile* b = base_find(v->base);
		if (!b) continue;
		*p = *b;
		memset(p->codes, 0, sizeof(p->codes));
		p->codes[0] = v->code;
		shift(p->mem, 4, v->memShift);
		shift(p->live, 4, v->memShift);
		shift(p->menu, 2, v->memShift);
		shift(p->room, 4, v->memShift);
		shift(p->open2If, 2, v->memShift);
		if (v->patch) v->patch(p);
	}
}

bool profile_ram_moved(const char* gameCode) {
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		if (!strncmp(variants[i].code, gameCode, 4)) return variants[i].memShift != 0;
	}
	return false;
}

const MapProfile* profile_find(const char* gameCode) {
	const MapProfile* p = base_find(gameCode);
	if (p) return p;
	pthread_once(&variantsOnce, build_variants);
	for (size_t i = 0; i < N_VARIANTS; ++i) {
		if (!strncmp(variants[i].code, gameCode, 4) && variantProfiles[i].codes[0]) return &variantProfiles[i];
	}
	return NULL;
}

bool profile_patch_rom(const MapProfile* p, Emu* e) {
	if (!p || !p->romPatch[0].addr) return false;
	for (int pass = 0; pass < 2; ++pass) {
		for (const RomPatch* r = p->romPatch; r < p->romPatch + 3 && r->addr; ++r) {
			for (int i = 0; i < r->len; ++i) {
				if (pass == 0 && emu_read8(e, r->addr + i) != (r->orig ? r->orig[i] : 0xFF)) return false;
				if (pass == 1) e->core->rawWrite8(e->core, r->addr + i, -1, r->bytes[i]);
			}
		}
	}
	return true;
}

/* ---------------------------------------------------------- known dumps */
typedef struct {
	char code[5];
	uint32_t crc;
	bool tested;
	const char* name;
} KnownDump;
#include "dumps_data.h"

DumpKind profile_dump(const char* gameCode, uint32_t crc, const char** name) {
	bool listed = false;
	if (name) *name = NULL;
	for (size_t i = 0; i < sizeof(knownDumps) / sizeof(knownDumps[0]); ++i) {
		if (strncmp(knownDumps[i].code, gameCode, 4)) continue;
		listed = true;
		if (knownDumps[i].crc != crc) continue;
		if (name) *name = knownDumps[i].name;
		return knownDumps[i].tested ? DUMP_TESTED : DUMP_OFFICIAL;
	}
	return listed ? DUMP_MODIFIED : DUMP_UNLISTED;
}

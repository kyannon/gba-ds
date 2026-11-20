#include "profiles.h"

#include <mgba/core/core.h>

#include <pthread.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <time.h>

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
	/* a unit's x, y bytes in its data, and how many units there are (Fire
	 * Emblem: Fuuin no Tsurugi's units keep them 2 bytes earlier, and it
	 * has fewer) */
	uint8_t unitXY, nUnits;
} FeRegion;
#define FE7_AT .atMini = 0x50, .atIdle = 0x70, .atMenu = 0xD0, .atFight = 0xF0, .unitXY = 0x10, .nUnits = 144
#define FE8_AT .atMini = 0x60, .atIdle = 0x80, .atMenu = 0xE8, .atFight = 0x108, .unitXY = 0x10, .nUnits = 144
/* (Fuuin no Tsurugi: its player phase script from the fireemblem6j
 * decompilation, ProcScr_PlayerPhase; the map menu opens from the cursor
 * free, so atMini is the idle loop's label, unused) */
#define FE6_AT .atMini = 0x50, .atIdle = 0x70, .atMenu = 0xC8, .atFight = 0xE8, .unitXY = 0x0E, .nUnits = 62 + 50 + 20 + 5
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
	/* Fire Emblem: Fuuin no Tsurugi (Japan only; AFEJ: 'E', 'J'): its RAM
	 * mapped from Rekka no Ken's by tools/romxref.py (gPlaySt, the units)
	 * and the process slots by their free list; its scripts by matching
	 * the fireemblem6j decompilation's (ProcScr_PlayerPhase, ProcScr_Event,
	 * ProcScr_EkrBattle, ProcScr_PhaseIntro, ProcScr_StatScreen). The fan
	 * translation (FE6 Localization) keeps all of them. */
	{ 'E', 'J', 0x0202AA48, 0x02023CC4, 0x0202AA1C, 0x0202AB78, 0x085C56A8, 0x085C40A4, 0x085CB528, 0x085C5AA0, 0x08677680, 0, FE6_AT },
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
#define FE_UNITS_LEN (0x48 * fe_r->nUnits)
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
				unit = emu_read8(s, a + 3) == 8 && emu_read8(s, a + fe_r->unitXY) == (cur & 0xFF) && emu_read8(s, a + fe_r->unitXY + 1) == ((cur >> 16) & 0xFF);
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

/* ---------------------------------------------------------- Dragon Ball Z: Buu's Fury
 *
 * The Legacy of Goku II's engine. The second screen (BF-2): the Scouter's
 * map of the area (SELECT here), opened on the shadow core from the live
 * state at every capture, once it has faded in (BLDALPHA up to 16), without
 * its four scroll arrows (the same sprites as The Legacy of Goku II's) and
 * its L and R letters (BF-7). The Scouter works from the start, so there's
 * nothing to fall back on: where SELECT opens nothing (a conversation, a
 * scene), the last picture stays (BF-4). An attack under way takes no
 * SELECT, so it's tried a few times.
 */
#define BF_FIELD_DISP 0x1F40
#define BF_MAP_DISP 0x1D40
#define BF_ZONE 0x0300197D /* 0 on the title and file menu, 0x32 flying */
#define BF_AREA 0x03001980 /* the area's map data */
/* BF-7: the frame's top corners, dark green letters on the plates' green */
#define BF_LETTER 0x004A00 /* (0, 74, 0) as mGBA's XBGR */
#define BF_PLATE 0x00A500  /* (0, 165, 0) */
static void bf_map_prompts(uint32_t* out, int w, int h) {
	static const int box[2][4] = { { 4, 3, 11, 11 }, { 228, 3, 235, 12 } };
	for (int k = 0; k < 2; ++k)
		for (int y = box[k][1]; y <= box[k][3] && y < h; ++y)
			for (int x = box[k][0]; x <= box[k][2] && x < w; ++x) {
				uint32_t* c = &out[y * w + x];
				if ((*c & 0xF8F8F8) == (BF_LETTER & 0xF8F8F8)) *c = (*c & 0xFF000000) | BF_PLATE;
			}
}
static const char* bf_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	if (emu_io16(s, 0x00) != BF_FIELD_DISP) return "not walking about";
	/* the map: SELECT (three tries), then its fade-in */
	bool map = false;
	for (int t = 0; t < 3 && !map; ++t) {
		aw_run(s, K_SELECT, 3);
		for (int f = 0; f < 40 && !map; ++f) {
			emu_frame_blind(s, 0);
			map = emu_io16(s, 0x00) == BF_MAP_DISP;
		}
	}
	if (!map) return "the map did not open";
	int f = 0;
	while (f < 60 && emu_io16(s, 0x52) != 0x0010) emu_frame_blind(s, 0), ++f;
	if (emu_io16(s, 0x00) != BF_MAP_DISP || emu_io16(s, 0x52) != 0x0010) return "the map did not settle";
	emu_hide_sprites(s, dbz2Arrows, 4);
	emu_frame(s, 0);
	emu_frame(s, 0);
	emu_hide_sprites(s, NULL, 0);
	if (emu_io16(s, 0x00) != BF_MAP_DISP) return "the map went away";
	/* no map here (a shop: the frame alone, all greens, no area or dot):
	 * the last map dimmed (BF-8, dimWhenUnavailable) */
	bool any = false;
	for (int i = 0; i < GBA_W * GBA_H && !any; ++i) {
		uint32_t c = s->fb[i];
		any = (c & 0xFF) > 100 || ((c >> 16) & 0xFF) > 100;
	}
	if (!any) return "map button did nothing";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	bf_map_prompts(out, w, h);
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
 *   ML_CURSOR the menu's icon (0 Items, 1 Equipment, 2 Bros. Info); in
 *     Japan 0x20 higher (0x030040C9: with the USA's address the capture
 *     never reached Bros. Info there, and the logo stayed)
 */
#define ML_STATE 0x02000008 /* 8 walking about (its menus too), 3 a battle, 2 the title */
#define ML_MODE 0x02000009 /* with state 8: 21 walking about, 10 SELECT's menu opening, 26 the menus */
#define ML_PAGE 0x0200000D /* in the menus: 9 SELECT's menu, 12 Bros. Info (Mario's page), 14 Luigi's */
#define ML_WALKING 0x1508 /* ML_STATE and ML_MODE as a halfword, walking about */
/* ML_STATE and ML_MODE as a halfword in a battle (USA, Europe, Japan: set
 * at once from walking about, kept to the battle's end, results and
 * level-ups too). The mine cart minigame (Mysterious Mine Carts, Hoohoo
 * Mountain) is state 3 too but mode 0x1C (0x1C03): no battle boxes there,
 * the whole minigame on the game screen (ML-7, issue #51) */
#define ML_BATTLE 0x5A03
#define ML_CURSOR(e) (0x030040A9 + ((e)->gameCode[3] == 'J' ? 0x20 : 0))
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

/* ---------------------------------------------------------- Yu-Gi-Oh! World Championship Tournament 2004
 *
 * The second screen (WCT-1 to WCT-5, WCT-9): the Card View of the card
 * under the player's cursor, the game's own full-screen page for a card,
 * opened on the shadow core from the live state at every capture, as in
 * The Eternal Duelist Soul (the same engine; its code above is the model,
 * with this game's own registers, pixels and addresses, all three releases
 * alike): on the duel field A (the card's commands, Card View first and
 * chosen) then A; in a list A (a bar of commands, Detail first) then A; in
 * the cards won A alone. With Card View already open in the game, it's
 * taken as it is. It's checked by its fixed pixels and taken once it has
 * faded in and held still.
 *
 * Which screen is up is told by BG0CNT and BG1CNT (each screen sets them
 * once and keeps them; DISPCNT where two share them):
 *   field  0x0007 0x8106     list  0x1F09 0x1E80 (Deck Edit, Card Trade's
 *   view   0x0087 0x0104           choice, a duel's piles), won 0x0004
 *                                  0x1E0D with DISPCNT 0x1B40
 * Addresses: the life points 0x0201BFB4 (the player's) and 0x0201D378
 * (the opponent's: 0 out of a duel when it's the loser's), the duel's step
 * 0x020154F0 (3 to 5 in the player's turn, 6 from the end of turn, 7, 8
 * the opponent's), the field cursor 0x03006594 its side, 0x03006598 its
 * zone (0 monsters, 5 Magic & Trap, 0x0B the hand), 0x0300659C its column,
 * 0x03006578 bit 3 while the player moves it freely; Deck Edit's row
 * 0x0203123A; the cards won's 0x0202463A. */
#define WCT_V_LIVE 0xF00000E0 /* a card's under a cursor (or in Card View): taken */
#define WCT_V_IDLE 0xF00000E1 /* out of the duels and the card screens, or the opponent's turn: the logo */
#define WCT_V_ROOM 0xF00000E2 /* the screen and where its cursor is */
enum { WCT_NONE, WCT_FIELD, WCT_LIST, WCT_VIEW, WCT_WON };
/* the name bar along the bottom (the name never reaches its right end),
 * the darker text panel, and the black between the card and its text and
 * at the card's left: the same for every kind of card */
static const Probe wctView[8] = {
	{ 239, 159, 0x0000FF }, { 239, 150, 0x0000FF }, { 200, 159, 0x0000FF }, { 239, 100, 0x000084 },
	{ 239, 60, 0x000084 }, { 102, 60, 0x000000 }, { 103, 100, 0x000000 }, { 1, 20, 0x000000 }
};
static int wct_screen(Emu* e) {
	uint16_t bg0 = emu_io16(e, 0x08), bg1 = emu_io16(e, 0x0A), disp = emu_io16(e, 0x00);
	if (bg0 == 0x0007 && bg1 == 0x8106) return WCT_FIELD;
	if (bg0 == 0x0087 && bg1 == 0x0104 && disp == 0x0F00) return WCT_VIEW;
	if (bg0 == 0x1F09 && bg1 == 0x1E80) return WCT_LIST;
	if (bg0 == 0x0004 && bg1 == 0x1E0D && disp == 0x1B40) return WCT_WON;
	return WCT_NONE;
}
static bool wct_view(Emu* s) {
	return wct_screen(s) == WCT_VIEW && som_probes(s, wctView, 8, 8, 8);
}
/* the field's info bar names a card: white pixels along its bottom rows */
static bool wct_named(Emu* e) {
	int white = 0;
	for (int y = 151; y < 159; ++y)
		for (int x = 2; x < 120; ++x) white += (e->fb[y * GBA_W + x] & 0xFFFFFF) == 0xFFFFFF;
	return white >= 12;
}
/* in a duel: both players' life points (0 out of a duel for the one that
 * lost, and for both before the first duel) */
static bool wct_in_duel(Emu* e) {
	return emu_read16(e, 0x0201BFB4) && emu_read16(e, 0x0201D378);
}
/* in a duel, the player's turn over (WCT-9): the step 6 from the end of
 * turn, 7 handing over, 8 the opponent's turn */
static bool wct_turn_over(Emu* e) {
	if (!wct_in_duel(e)) return false;
	uint8_t step = emu_read8(e, 0x020154F0);
	return step >= 6 && step <= 8;
}
static uint32_t wct_virt(Emu* e, uint32_t addr) {
	int sc = wct_screen(e);
	if (addr == WCT_V_LIVE) {
		if (sc != WCT_FIELD) return sc != WCT_NONE;
		if (!wct_in_duel(e) || !(emu_read8(e, 0x03006578) & 0x08) || !wct_named(e)) return 0;
		/* a monster, a Magic or Trap card or a card in the hand (not the
		 * opponent's hand); not a pile (A opens its list, or draws) */
		uint8_t side = emu_read8(e, 0x03006594), zone = emu_read8(e, 0x03006598);
		return zone == 0 || zone == 5 || (zone == 0x0B && side == 0);
	}
	/* (the field too, once its duel is decided: WCT-6); and from the end of
	 * the player's turn on anything but a Card View opened in it (WCT-9) */
	if (addr == WCT_V_IDLE)
		return ((sc == WCT_NONE || sc == WCT_FIELD) && !wct_in_duel(e)) || (sc != WCT_VIEW && wct_turn_over(e));
	if (addr == WCT_V_ROOM) {
		uint32_t k = (uint32_t) sc << 28;
		if (sc == WCT_FIELD)
			k |= (uint32_t) emu_read8(e, 0x03006594) << 16 | emu_read8(e, 0x03006598) << 8 | emu_read8(e, 0x0300659C);
		else if (sc == WCT_LIST) k |= emu_read16(e, 0x0203123A);
		else if (sc == WCT_WON) k |= emu_read8(e, 0x0202463A);
		return k;
	}
	return 0;
}
/* On the field: runs frames until the card's commands are up with Card View
 * chosen (its magnifier, a 16x16 sprite of tile 324, with the chosen
 * command's name under it, a 64x32 sprite of tile 376, 16 pixels left of
 * the icon); false if they don't come (A chose a target: an attack's) */
static bool wct_wait_commands(Emu* s, int max) {
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		int glass = -1, name = -99;
		for (int i = 0; i < 128; ++i) {
			uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a1 = emu_read16(s, 0x07000002 + i * 8),
			         a2 = emu_read16(s, 0x07000004 + i * 8);
			if ((a0 & 0x0300) == 0x0200) continue; /* hidden */
			if ((a2 & 0x3FF) == 324) glass = a1 & 0x1FF;
			else if ((a2 & 0x3FF) == 376) name = a1 & 0x1FF;
		}
		if (glass >= 0 && name - glass == -16) return true;
	}
	return false;
}
/* runs frames until Card View is up and has held still for 3 (its fade-in
 * done) */
static bool wct_wait_view(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		if (!wct_view(s)) {
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
 * knows; true if to Card View (false: elsewhere, A chose the card; or
 * still the list after max frames). The frames between, blanked for the
 * fade, aren't a screen yet. */
static bool wct_wait_leave(Emu* s, int sc, int max) {
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame_blind(s, 0);
		int now = wct_screen(s);
		if (now != sc && now != WCT_NONE) return now == WCT_VIEW;
	}
	return false;
}
static const char* wct_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int sc = wct_screen(s);
	if (sc == WCT_NONE) return "not a card screen";
	if (sc != WCT_VIEW) {
		/* (after a moment: the cursor glides to its place, and A on the
		 * way does nothing) */
		aw_run(s, 0, 12);
		if (sc == WCT_FIELD) {
			/* the info bar names the cursor's card once it's there: none, an
			 * empty zone, a pile or a face-down card of the opponent's;
			 * nothing taken, and no slowing down of the next tries */
			if (!wct_named(s)) return "map button did nothing";
			/* A: the card's commands, Card View first and chosen, told by
			 * their sprites. Where none come, A chose a target (an attack's,
			 * a card effect's) or it isn't a card (a pile's list): nothing
			 * taken, cheaply (WCT-5). */
			aw_tap(s, K_A, 1);
			if (!wct_wait_commands(s, 20)) return "map button did nothing";
			/* the commands take a press only a little after they are drawn */
			aw_run(s, 0, 12);
			aw_tap(s, K_A, 1);
		} else if (sc == WCT_LIST) {
			/* a bar of commands, Detail chosen, then A (where there's none,
			 * A chose the card: a Designation list; nothing taken, WCT-5) */
			aw_tap(s, K_A, 1);
			aw_run(s, 0, 8);
			if (wct_screen(s) != sc) return "A did something else";
			aw_tap(s, K_A, 1);
		} else {
			/* the cards won: A opens Card View */
			aw_tap(s, K_A, 1);
			if (!wct_wait_leave(s, sc, 48)) return "map button did nothing";
		}
	}
	if (!wct_view(s) && !wct_wait_view(s, 60)) return "Card View didn't open";
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ------------------------------ Yu-Gi-Oh! Ultimate Masters: World Championship Tournament 2006
 *
 * The second screen (UM6-1 to UM6-5): the Card View of the card under the
 * player's cursor, as for The Eternal Duelist Soul (eds_compose above): the
 * game's own full-screen page for a card, opened on the shadow core from the
 * live state at every capture. On the duel field A (the card's commands, Card
 * View first and chosen) then A; in Deck Edit A (its command bar, Detail
 * first) then A. With Card View already open in the game it's taken as it is.
 * It's checked by its fixed pixels and taken once it has faded in and held
 * still.
 *
 * Which screen is up: BG0CNT and BG1CNT (the duel field 0x1F08 0x1E09; Deck
 * Edit's grid, list, command bar and Card View 0x1C0A 0x1D09), Card View by
 * its pixels (the dark olive text panel and the black round the card:
 * um6View), wherever it's opened. In a duel: the life points 0x0201C4E0
 * (the player's) and 0x0201CD48 (the opponent's) are both non-zero; the
 * opponent's turn 0x0201BCC1 bit 7; the field cursor 0x02023340 (zone, bit 7
 * the opponent's side) and 0x02023341 (column); the field's info bar names
 * the card under the cursor in blue (the player's) or red (the opponent's).
 * Deck Edit's cursor is a sprite (palette 10, tile 924): at y 44 and below on
 * a card, above it on the tabs. */
#define UM6_V_LIVE 0xF0000050 /* a card's under a cursor (or in Card View): taken */
#define UM6_V_IDLE 0xF0000051 /* out of the duels and Deck Edit, or the opponent's turn: the logo */
#define UM6_V_ROOM 0xF0000052 /* the screen and where its cursor is */
enum { UM6_NONE, UM6_FIELD, UM6_DECK, UM6_VIEW };
static const Probe um6View[8] = {
	/* the text panel's dark olive (the same for every kind of card), the black
	 * left of the card's frame and between the card and its text */
	{ 239, 60, 0x423918 }, { 239, 80, 0x423918 }, { 239, 100, 0x423918 }, { 239, 120, 0x423918 },
	{ 0, 143, 0x000000 }, { 110, 24, 0x000000 }, { 0, 30, 0x000000 }, { 2, 45, 0x000000 }
};
static int um6_screen(Emu* e) {
	if (som_probes(e, um6View, 8, 8, 8)) return UM6_VIEW;
	uint16_t bg0 = emu_io16(e, 0x08), bg1 = emu_io16(e, 0x0A);
	if (bg0 == 0x1F08 && bg1 == 0x1E09) return UM6_FIELD;
	if (bg0 == 0x1C0A && bg1 == 0x1D09) return UM6_DECK;
	return UM6_NONE;
}
static bool um6_in_duel(Emu* e) {
	return emu_read16(e, 0x0201C4E0) && emu_read16(e, 0x0201CD48);
}
static bool um6_opp_turn(Emu* e) {
	return um6_in_duel(e) && (emu_read8(e, 0x0201BCC1) & 0x80);
}
/* the field's info bar names a card (not a pile: its count is cyan; not an
 * empty zone or a face-down card of the opponent's: nothing) */
static bool um6_named(Emu* e) {
	int n = 0;
	for (int y = 146; y < 160; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = e->fb[y * GBA_W + x] & 0xFFFFFF;
			n += c == 0xFF0000 || c == 0x0000FF; /* (the fb is ABGR: red and blue swap, both are counted) */
		}
	return n >= 20;
}
/* Deck Edit's cursor: its y (0 when none is up); a sprite of palette 10 and
 * tile 924, 32x32 over a card, 16x16 on the tabs. Its place goes with its x. */
static int um6_deck_cursor(Emu* e, int* x) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(e, 0x07000000 + i * 8), a1 = emu_read16(e, 0x07000002 + i * 8),
		         a2 = emu_read16(e, 0x07000004 + i * 8);
		if ((a0 & 0x0300) == 0x0200) continue; /* hidden */
		if ((a2 & 0x3FF) == 924 && (a2 >> 12) == 10) {
			if (x) *x = a1 & 0x1FF;
			return (a0 & 0xFF) ? (a0 & 0xFF) : 1;
		}
	}
	return 0;
}
static uint32_t um6_virt(Emu* e, uint32_t addr) {
	int sc = um6_screen(e);
	if (addr == UM6_V_LIVE) {
		if (sc == UM6_VIEW) return 1;
		if (sc == UM6_DECK) return um6_deck_cursor(e, NULL) >= 40;
		if (sc != UM6_FIELD || !um6_in_duel(e) || um6_opp_turn(e) || !um6_named(e)) return 0;
		/* a monster, a Magic or Trap card or a card in the hand (not the
		 * opponent's hand); not a pile (A opens its list, or draws) */
		uint8_t at = emu_read8(e, 0x02023340), zone = at & 0x7F;
		return zone == 0 || zone == 5 || (zone == 0x0B && !(at & 0x80));
	}
	/* out of the duels and Deck Edit (the field too, once its duel is decided),
	 * and through the opponent's turn: anything but a Card View opened in it
	 * (UM6-4, UM6-5) */
	if (addr == UM6_V_IDLE)
		return ((sc == UM6_NONE || sc == UM6_FIELD) && !um6_in_duel(e)) || (sc != UM6_VIEW && um6_opp_turn(e));
	if (addr == UM6_V_ROOM) {
		uint32_t k = (uint32_t) sc << 28;
		if (sc == UM6_FIELD) k |= (uint32_t) emu_read8(e, 0x02023340) << 8 | emu_read8(e, 0x02023341);
		else if (sc == UM6_DECK) {
			int x = 0, y = um6_deck_cursor(e, &x);
			k |= (uint32_t) y << 12 | (x & 0xFFF);
		}
		return k;
	}
	return 0;
}
/* On the field: runs frames until the card's commands are up (the magnifier,
 * tile 448, with the chosen command's name's two sprites, tiles 520 and 524);
 * false if they don't come (A chose a target, or the cursor isn't free) */
static bool um6_wait_commands(Emu* s, int max) {
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		bool glass = false, name = false;
		for (int i = 0; i < 128; ++i) {
			uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a2 = emu_read16(s, 0x07000004 + i * 8);
			if ((a0 & 0x0300) == 0x0200) continue; /* hidden */
			glass |= (a2 & 0x3FF) == 448;
			name |= (a2 & 0x3FF) == 520;
		}
		if (glass && name) return true;
	}
	return false;
}
/* runs frames until Card View is up and has held still for 3 (its fade-in
 * done) */
static bool um6_wait_view(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		if (um6_screen(s) != UM6_VIEW) {
			still = 0;
			continue;
		}
		still = still && !memcmp(prev, s->fb, sizeof(prev)) ? still + 1 : 1;
		if (still >= 3) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}
static const char* um6_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int sc = um6_screen(s);
	if (sc == UM6_NONE) return "not a card screen";
	if (sc != UM6_VIEW) {
		/* (after a moment: the cursor glides to its place, and A on the way
		 * does nothing) */
		aw_run(s, 0, 12);
		if (sc == UM6_FIELD) {
			/* the info bar names the cursor's card once it's there; none,
			 * nothing taken, and no slowing down of the next tries */
			if (!um6_named(s)) return "map button did nothing";
			/* A: the card's commands, Card View first and chosen, told by their
			 * sprites. Where none come, A chose a target or the cursor wasn't
			 * free (a banner, a question): nothing taken (UM6-3). */
			aw_tap(s, K_A, 1);
			if (!um6_wait_commands(s, 20)) return "map button did nothing";
			aw_run(s, 0, 16); /* (the bar takes a few frames to take A) */
			aw_tap(s, K_A, 1);
		} else {
			/* Deck Edit: on a card A brings up the command bar, Detail chosen;
			 * on a tab or a menu it brings up something else, and no Card View
			 * follows */
			if (um6_deck_cursor(s, NULL) < 40) return "map button did nothing";
			aw_tap(s, K_A, 1);
			aw_run(s, 0, 8);
			if (um6_screen(s) == UM6_VIEW) return "A did something else";
			aw_tap(s, K_A, 1);
		}
	}
	if (um6_screen(s) != UM6_VIEW && !um6_wait_view(s, 120)) return "Card View didn't open";
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ---------------------------------------------------- Duel Masters: Kaijudo Showdown
 *
 * The second screen (DMK-1 to DMK-4): the game's own Card Details page for
 * the card under the player's cursor, opened on the shadow core from the
 * live state at every capture: SELECT in a duel (the zone views, the field
 * while a creature is chosen), A in Deck Management's card list. It's
 * checked by its fixed pixels (the black name, description and statistics
 * panels) and taken once it has faded in and held still, with its "B Exit"
 * prompt (a sprite) left out.
 *
 * Which screen is up is told by BG0CNT to BG3CNT (each screen sets them once):
 *   duel   0x1F02 0x1E02 0x1D03 0x1C0B   zones, the field, Summon/Cast
 *   view   0x1F03 0x1E02 0x1D01 0x1C02   Card Details, wherever it's opened
 *   list   0x1F08 0x1E81 0x1D0A 0x1C0B   Deck Management (its deck choice too)
 * Nothing else a duel can be in shares the duel's (the overworld has
 * 0x1F00 0x1E01 0x1D0A 0x1C03, the coin flip 0x1F03 0x1E01 0x1D0A 0x1C03).
 * The hand's cursor is 0x0300145A (the slot), which with the bar naming the
 * card (the rows above the bottom line) is the room key. */
#define DMK_V_LIVE 0xF0000050 /* a card may be under a cursor: taken */
#define DMK_V_IDLE 0xF0000051 /* out of the duels and the card lists: the logo */
#define DMK_V_ROOM 0xF0000052 /* the screen and where its cursor is */
enum { DMK_NONE, DMK_DUEL, DMK_VIEW, DMK_LIST };
static const Probe dmkView[8] = {
	/* the black panels: the name box, the description box (right of its
	 * text), the power and cost box, and the corner under the card */
	{ 230, 17, 0x000000 }, { 80, 17, 0x000000 }, { 230, 45, 0x000000 }, { 230, 100, 0x000000 },
	{ 230, 140, 0x000000 }, { 8, 90, 0x000000 }, { 100, 90, 0x000000 }, { 100, 136, 0x000000 },
};
static int dmk_screen(Emu* e) {
	uint16_t b0 = emu_io16(e, 0x08), b1 = emu_io16(e, 0x0A), b2 = emu_io16(e, 0x0C), b3 = emu_io16(e, 0x0E);
	if (b0 == 0x1F02 && b1 == 0x1E02 && b2 == 0x1D03 && b3 == 0x1C0B) return DMK_DUEL;
	if (b0 == 0x1F03 && b1 == 0x1E02 && b2 == 0x1D01 && b3 == 0x1C02) return DMK_VIEW;
	if (b0 == 0x1F08 && b1 == 0x1E81 && b2 == 0x1D0A && b3 == 0x1C0B) return DMK_LIST;
	return DMK_NONE;
}
static bool dmk_view(Emu* s) {
	if (dmk_screen(s) != DMK_VIEW || !som_probes(s, dmkView, 8, 8, 8)) return false;
	/* not the black of a fade (the probes are black panels): the page's
	 * background and card fill much of the rest */
	int lit = 0;
	for (int i = 0; i < GBA_W * GBA_H; ++i) lit += (s->fb[i] & 0xFFFFFF) != 0;
	return lit >= 5000;
}
static uint32_t dmk_virt(Emu* e, uint32_t addr) {
	int sc = dmk_screen(e);
	/* a card may be under the cursor: a duel's views and the card list; the
	 * page itself open in the game isn't taken again (DMK-3) */
	if (addr == DMK_V_LIVE) return sc == DMK_DUEL || sc == DMK_LIST;
	if (addr == DMK_V_IDLE) return sc == DMK_NONE;
	if (addr == DMK_V_ROOM) {
		uint32_t k = (uint32_t) sc << 28;
		if (sc == DMK_DUEL) {
			/* the cursor's slot and the name bar's pixels (the card under it) */
			uint32_t h = 2166136261u;
			for (int y = 128; y < 140; ++y)
				for (int x = 8; x < 232; x += 2) h = (h ^ (e->fb[y * GBA_W + x] & 0xFFFFFF)) * 16777619u;
			k |= (emu_read8(e, 0x0300145A) << 16) | (h & 0xFFFF);
		}
		return k;
	}
	return 0;
}
/* runs frames until Card Details is up and has held still for 3 (its
 * fade-in done); false if the screen stays as it was (nothing opened) */
static bool dmk_wait_view(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	static uint32_t first[GBA_W * GBA_H];
	int still = 0;
	bool moved = false;
	memcpy(first, s->fb, sizeof(first));
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		if (!moved && f == 24 && !memcmp(first, s->fb, sizeof(first))) return false;
		if (!dmk_view(s)) {
			still = 0;
			continue;
		}
		moved = true;
		still = still && !memcmp(prev, s->fb, sizeof(prev)) ? still + 1 : 1;
		if (still >= 3) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}
static const char* dmk_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int sc = dmk_screen(s);
	if (sc != DMK_DUEL && sc != DMK_LIST) return "not a card screen";
	/* (the cursor glides to its place, and a press on the way does nothing) */
	aw_run(s, 0, 12);
	aw_run(s, sc == DMK_DUEL ? K_SELECT : K_A, 3);
	if (!dmk_wait_view(s, 180)) return "no Card Details";
	/* the "B Exit" prompt: a 32x8 sprite at the bottom left */
	static const SpriteRule exitPrompt = { 0, 142, 48, 156, 0, 1023, 0, 0, 0, 0 };
	emu_hide_sprites(s, &exitPrompt, 1);
	emu_frame(s, 0);
	emu_frame(s, 0);
	emu_hide_sprites(s, NULL, 0);
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}

/* ---------------------------------------------------------- Yu-Gi-Oh! 7 Trials to Glory
 *
 * The second screen (YWC-1 to YWC-3): the Card View (Detail) of the card
 * under the player's cursor, opened on the hidden copy as Eternal Duelist
 * Soul's is (eds_compose_for), from BG0CNT / BG1CNT alone: the duel
 * field 0x4007 0x4206, Card View 0x0087 0x4104, the lists (Deck select,
 * Deck mode, the banlist) 0x0004 0x0105. 0x03000330's high byte is 0x35
 * while the field is up (free, or a card's commands open) and 0x20 with
 * "End your turn?"; the info bar (its bottom rows) names the card in
 * white. Where the hidden copy's A does something else, nothing is taken. */
#define YW_V_LIVE 0xF0000050
#define YW_V_IDLE 0xF0000051
#define YW_V_ROOM 0xF0000052
enum { YW_NONE, YW_FIELD, YW_VIEW, YW_LIST };
/* the two releases' addresses (the same engine, 0x10 apart: tools/romxref.py) */
typedef struct {
	uint32_t state; /* the duel's state byte */
	uint32_t list;  /* the list's cursor row */
} YwRelease;
static const YwRelease ywUS = { 0x0201A460, 0x02026AB0 };
static const YwRelease ywJP = { 0x0201A450, 0x02026AA0 };
static int yw_screen(Emu* e) {
	uint16_t bg0 = emu_io16(e, 0x08), bg1 = emu_io16(e, 0x0A);
	if (bg0 == 0x4007 && bg1 == 0x4206) return YW_FIELD;
	if (bg0 == 0x0087 && (bg1 == 0x4104 || bg1 == 0x0104)) return YW_VIEW; /* Japan's 0x0104 */
	if (bg0 == 0x0004 && bg1 == 0x0105) return YW_LIST;
	return YW_NONE;
}
static const Probe ywView[8] = {
	{ 239, 30, 0x424242 }, { 239, 50, 0x424242 }, { 239, 70, 0x424242 }, { 239, 100, 0x424242 },
	{ 239, 130, 0x424242 }, { 1, 20, 0x000000 }, { 1, 100, 0x000000 }, { 5, 159, 0xA5A5A5 },
};
static bool yw_view(Emu* s) {
	return yw_screen(s) == YW_VIEW && som_probes(s, ywView, 8, 8, 8);
}
/* the info bar names a card: white pixels in its text */
static bool yw_named(Emu* e) {
	int white = 0;
	for (int y = 151; y < 159; ++y)
		for (int x = 2; x < 120; ++x) white += (e->fb[y * GBA_W + x] & 0xFFFFFF) == 0xFFFFFF;
	return white >= 12;
}
/* the duel's state byte: 8 through the opponent's whole turn (YWC-8) */
static bool yw_opp_turn(const YwRelease* r, Emu* e) {
	return emu_read8(e, r->state) == 8;
}
/* not the player's free cursor: the turn being ended (6), the opponent's
 * turn (8). The Draw Phase (2, 3) is free: the cursor can go to the hand */
static bool yw_busy(const YwRelease* r, Emu* e) {
	int v = emu_read8(e, r->state);
	return v == 6 || v == 8;
}
static bool yw_field_up(Emu* e) {
	return (emu_read16(e, 0x03000330) & 0xFF00) == 0x3500;
}
static uint32_t yw_virt_for(const YwRelease* r, Emu* e, uint32_t addr) {
	int sc = yw_screen(e);
	if (addr == YW_V_LIVE) {
		if (sc == YW_FIELD) return !yw_busy(r, e) && yw_field_up(e) && yw_named(e);
		return sc != YW_NONE;
	}
	if (addr == YW_V_IDLE) return sc == YW_NONE || (sc == YW_FIELD && yw_opp_turn(r, e));
	if (addr == YW_V_ROOM) {
		uint32_t k = (uint32_t) sc << 28;
		if (sc == YW_FIELD) {
			uint32_t h = 2166136261u;
			for (int y = 151; y < 159; ++y)
				for (int x = 2; x < 120; ++x) h = (h ^ (e->fb[y * GBA_W + x] & 0xFFFFFF)) * 16777619u;
			h = (h ^ emu_read8(e, r->state)) * 16777619u; /* a capture again once the state settles */
			k |= (h & 0x0FFFFFFF);
		} else if (sc == YW_LIST) k |= emu_read8(e, r->list);
		return k;
	}
	return 0;
}
static bool yw_wait_view(Emu* s, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max && !emu_cancelled(s); ++f) {
		emu_frame(s, 0);
		if (!yw_view(s)) {
			still = 0;
			continue;
		}
		still = still && !memcmp(prev, s->fb, sizeof(prev)) ? still + 1 : 1;
		if (still >= 3) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}
static const char* yw_compose_for(const YwRelease* r, Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int sc = yw_screen(s);
	if (sc == YW_NONE) return "not a card screen";
	if (sc != YW_VIEW) {
		aw_run(s, 0, sc == YW_LIST ? 30 : 12); /* the cursor glides to its place (a list scrolls) */
		if (sc == YW_FIELD && (yw_busy(r, s) || !yw_field_up(s) || !yw_named(s))) return "map button did nothing";
		/* A: the card's commands (Card View chosen), or the list's command
		 * bar (Detail chosen); with them already up, A opens the page. The
		 * field's first A can be lost to a banner (the turn's "Main Phase"),
		 * so up to four; a list's are exactly two (a third would go into
		 * Deck mode's first card from Deck select). A press that leaves the
		 * screen starts the page fading in, or is something else, which
		 * yw_wait_view gives up on. */
		for (int n = 0; n < (sc == YW_FIELD ? 4 : 2); ++n) {
			aw_tap(s, K_A, 1);
			aw_run(s, 0, sc == YW_LIST ? 30 : 12);
			if (yw_screen(s) != sc) break;
		}
	}
	if (!yw_view(s) && !yw_wait_view(s, 60)) return "Card View didn't open";
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	return NULL;
}
static uint32_t yw_virt_us(Emu* e, uint32_t addr) {
	return yw_virt_for(&ywUS, e, addr);
}
static uint32_t yw_virt_jp(Emu* e, uint32_t addr) {
	return yw_virt_for(&ywJP, e, addr);
}
static const char* yw_compose_us(Emu* s, const void* state, uint32_t* out, int w, int h) {
	return yw_compose_for(&ywUS, s, state, out, w, h);
}
static const char* yw_compose_jp(Emu* s, const void* state, uint32_t* out, int w, int h) {
	return yw_compose_for(&ywJP, s, state, out, w, h);
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
/* Bros. Info's L and R prompts (ML-2): each a pink tab with its letter,
 * drawn into BG2's magnifying glass (tiles 0x170-0x173 on map row 9 and
 * 0x190-0x193 on row 10, columns 2-3 for L and 6-7 for R; not sprites, so
 * they can't be hidden alone). Only the tabs' pixels (their letters, faces,
 * shading and outlines) are replaced, from the owner's own repaint of the
 * glass (Nov 10, 2026, on the Equipment page, which shows more of it),
 * tidied: the lens's rim and its lighter outline closed round at the widths
 * the game draws them on the lower arc, the glass's edge going
 * on round under L, its thin dark outline drawn on to the glass's bottom
 * outline, what's behind outside it, tapering into the handle under R and
 * between the tabs' feet. The table is rows 72-87 from x 14:
 * a-e the glass's colours, as the page has them on its left edge, '.' what
 * lies behind BG2 there, '-' the game's own pixel kept (the portrait's
 * sprites are clear outside the lens). Not this layout: left as it is */
static const char mlGlass[16][53] = {
	"----------------------------------------------------",
	"----------------------------------------------------",
	"----eddd--------------------------------aaabddd-----",
	"----eedd--------------------------------abddddee----",
	"----eeee--------------------------------bdddeeee----",
	"----eeee---------------------------------ddeeeee----",
	"----ddee---------------------------------eeeeeee----",
	"---bddeeeee------------------------------eeeeeee----",
	"...aaddddeeeeeee--------------------eeeeeeeeeeee----",
	".....aaaddddeeee--------------------eeeeeeeeeeeee---",
	"........aaddddee--------------------eeeeeeeeeeeeee--",
	"..........aaaddd--------------------eeeeeeeeeeeeee--",
	".............aaddd------------------eeeeeeeeeeeeee--",
	"................aaddddd-------------eeeeeeeeeeeee---",
	"..................abb---------------eeeeeeeeeeee----",
	"...................a---------------b----------------"
};
static void ml_no_prompts(Emu* s, uint32_t* out, int w, int h) {
	if (w < 66 || h < 88) return;
	uint32_t map = 0x06000000 + ((emu_read16(s, 0x0400000C) >> 8) & 0x1F) * 0x800;
	static const uint16_t want9[4] = { 0x172, 0x173, 0x170, 0x171 }, want10[4] = { 0x192, 0x193, 0x190, 0x191 };
	static const int cols[4] = { 2, 3, 6, 7 };
	for (int i = 0; i < 4; ++i)
		if ((emu_read16(s, map + (9 * 32 + cols[i]) * 2) & 0x3FF) != want9[i] ||
		    (emu_read16(s, map + (10 * 32 + cols[i]) * 2) & 0x3FF) != want10[i])
			return;
	/* the page without BG2, the same frame on: BG2's whole map blank for a
	 * frame (tile 0 is empty), then back (emu_layers doesn't reach the
	 * app's renderer) */
	static uint32_t bare[GBA_W * GBA_H];
	static uint16_t saved[32 * 32];
	for (int i = 0; i < 32 * 32; ++i) saved[i] = emu_read16(s, map + i * 2), emu_write16(s, map + i * 2, 0);
	emu_frame(s, 0);
	memcpy(bare, s->fb, sizeof(bare));
	for (int i = 0; i < 32 * 32; ++i) emu_write16(s, map + i * 2, saved[i]);
	/* the glass's colours, from its left edge (dark, -, highlight, light,
	 * medium; b a step in from row 52's) */
	const uint32_t glass[5] = { out[60 * w + 2], out[52 * w + 8], out[60 * w + 3], out[60 * w + 4], out[60 * w + 5] };
	for (int y = 72; y < 88; ++y)
		for (int x = 14; x < 66; ++x) {
			char c = mlGlass[y - 72][x - 14];
			if (c == '-') continue;
			uint32_t* o = &out[y * w + x];
			int i = y * GBA_W + x;
			*o = c == '.' ? bare[i] : glass[c - 'a'];
		}
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
		for (int i = 0; i < 8 && emu_read8(s, ML_CURSOR(s)) != 2; ++i) {
			aw_tap(s, emu_read8(s, ML_CURSOR(s)) < 2 ? K_DOWN : K_UP, 1);
			aw_run(s, 0, 4);
		}
		if (emu_read8(s, ML_CURSOR(s)) != 2) return "Bros. Info not reached in the menu";
		aw_tap(s, K_A, 1);
		if (!ml_wait(s, ML_BROS, 90)) return "Bros. Info didn't open";
	} else if (at != ML_BROS) {
		return "not walking about";
	}
	if (!ml_settle(s, 150)) return "Bros. Info didn't settle";
	if (!ml_bros_info(s)) return "not Bros. Info";
	if (!luigi) {
		ml_page(s->fb, out[0], w, h);
		ml_no_prompts(s, out[0], w, h);
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
			ml_no_prompts(s, out[0], w, h);
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

/* ------------------------------ Banjo-Kazooie: Grunty's Revenge
 *
 * Every release (USA and Europe, Europe's English, French and German, Italy,
 * Spain) has the same RAM. Found by comparing dumps (the owner's two 100%
 * saves, a new game, the title and the intro; then 65 moments of play,
 * jumps and the owner's state after Bozzeye's Pack Whack among them, 15
 * paused, 32 at the title and in the story):
 * - BK_OBJS, the word at 0x02000188: a pointer into EWRAM (0x02xxxxxx)
 *   wherever Banjo is about (a conversation too, mid-jump too, through an
 *   area change frame by frame), 0 at the title, the file screen and the
 *   opening story. (The first build's 0x0200030C is 0 mid-jump: the logo
 *   flashed up at each jump. Its 0x02000318, taken for "paused", isn't 0
 *   again after the Pack Whack scene: no more pictures.)
 * - BK_PAUSE, the byte at 0x0203F4D8 (0x0203F4DC in Europe's, Italy's
 *   and Spain's: BK_PAUSE_EU, by tools/romxref.py, checked in each): 1
 *   from the frame START is pressed while the pause screen (or a page of
 *   it: Totals, Options, Save) is up, else 0;
 * - BK_AREA, the byte at 0x02000004: the area (Spiral Mountain 0x0D, Spiral
 *   Rise 0x0E, Cliff Farm's first area 0x06...).
 * The pause screen (START) is all sprites over the level, which it darkens:
 * the health bar and the notes along the top, the eggs, feathers and Hollow
 * Honeycombs down the left, the Jiggies, Jinjos and the rest down the right,
 * and its menu in the middle (CONTINUE, TOTALS, OPTIONS, SAVE, each with a
 * Jiggy beside it: the Jiggies 16x16 at x 52, the words from x 72 (to 149
 * in English, further in French and Spanish), rows 36,
 * 60, 84 and 108, 16 high). Its counts come in one after another: the
 * screen is still ~90 frames after START, and takes the menu's buttons
 * from then. TOTALS opens the current world's page (its name on top, its
 * Hollow Honeycombs, notes, Jiggies, Jinjos, Mumbo Totems and the rest of
 * its own things), still ~115 frames later. The selected item is drawn
 * white, the others grey. */
#define BK_OBJS 0x02000188
#define BK_PAUSE 0x0203F4D8
#define BK_PAUSE_EU 0x0203F4DC
#define BK_AREA 0x02000004
/* the health bar (Banjo's head and his honeycombs) on the pause screen:
 * x 4 to ~123 with the 100% file's ten honeycombs, rows 12-27; the Totals
 * page's world name, rows 12-22 (its rows start at 36): BK_TOP rows, the
 * name's band and the health's */
#define BK_TOP_Y0 8
#define BK_TOP_Y1 31
#define BK_HEALTH_X1 150 /* (the notes start at x 184) */
/* the pause menu's block (its Jiggies at x 52, its words from x 72, to
 * ~170 in French and German) */
#define BK_MENU_X0 50
#define BK_MENU_X1 186
#define BK_MENU_Y0 34
#define BK_MENU_Y1 125

/* white pixels in a menu row (the selected item is white, the others grey) */
static int bk_white(const uint32_t* fb, int y0) {
	int n = 0;
	for (int y = y0; y < y0 + 16; ++y) {
		for (int x = 72; x < 150; ++x) {
			uint32_t c = fb[y * GBA_W + x];
			if ((c & 0xFF) > 0xE0 && ((c >> 8) & 0xFF) > 0xE0 && ((c >> 16) & 0xFF) > 0xE0) ++n;
		}
	}
	return n;
}

static bool bk_paused(Emu* s) {
	/* (the release's from its game code's last letter, in the ROM header) */
	return emu_read8(s, emu_read8(s, 0x080000AF) == 'E' ? BK_PAUSE : BK_PAUSE_EU) != 0;
}

/* frames (the level's layers off: only the pause screen's sprites, on
 * black) until the picture has held still for 3 with the pause screen up;
 * false if not within limit */
static bool bk_settle(Emu* s, int limit) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < limit; ++f) {
		emu_write16(s, 0x05000000, 0);
		emu_frame(s, 0);
		if (!bk_paused(s)) return false;
		still = f && !memcmp(prev, s->fb, sizeof(prev)) ? still + 1 : 0;
		if (still >= 12) return true; /* (the counts come in with pauses of a few frames) */
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}

static void bk_copy(Emu* s, uint32_t* out, int w, int h) {
	int cw = w < GBA_W ? w : GBA_W;
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (size_t) cw * 4);
}

/* Two pages, 5 seconds each (BK-2): the pause screen without its menu,
 * then the current world's Totals, its name left out and the pause
 * screen's health bar in its place, where the pause screen has it (held
 * still as the pages turn). Where START doesn't bring up the pause screen
 * (a conversation, a scene), the quick failure: the last pictures stay
 * (BK-4). The Totals page not as expected: the first page alone (GEN-3). */
static const char* bk_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	int max = *nOut;
	*nOut = 1;
	*step = 300;
	if (w < GBA_W || h < GBA_H) return "picture too small";
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (bk_paused(s)) return "screen already looks like the map";
	aw_run(s, K_START, 4);
	int f = 0;
	while (f < 20 && !bk_paused(s)) emu_frame(s, 0), ++f;
	if (!bk_paused(s)) return "map button did nothing";
	if (!bk_settle(s, 200)) return "the pause screen didn't settle";
	/* (the menu's first row selected: the pause screen as it opens) */
	if (bk_white(s->fb, 36) < 40 || bk_white(s->fb, 60) > 0) return "not the pause screen";
	bk_copy(s, out[0], w, h);
	/* the menu taken out: its Jiggies and words, the backdrop's black */
	uint32_t black = s->fb[(GBA_H - 1) * GBA_W];
	for (int y = BK_MENU_Y0; y <= BK_MENU_Y1; ++y) {
		for (int x = BK_MENU_X0; x <= BK_MENU_X1; ++x) out[0][y * w + x] = black;
	}
	if (max < 2) return NULL;
	/* TOTALS: down a row, then A */
	for (int tries = 0; tries < 3 && bk_white(s->fb, 60) < 40; ++tries) {
		aw_run(s, K_DOWN, 4);
		aw_run(s, 0, 8);
	}
	if (bk_white(s->fb, 60) < 40) return NULL; /* (the first page alone) */
	aw_run(s, K_A, 4);
	for (f = 0; f < 60 && bk_paused(s) && (bk_white(s->fb, 36) >= 40 || bk_white(s->fb, 60) >= 40); ++f) {
		emu_write16(s, 0x05000000, 0);
		emu_frame(s, 0);
	}
	if (!bk_settle(s, 240)) return NULL;
	/* (not the pause screen again: its menu's first row, gone) */
	if (bk_white(s->fb, 36) >= 40 || bk_white(s->fb, 60) >= 40) return NULL;
	bk_copy(s, out[1], w, h);
	/* the world's name out, the health bar from the first page in its
	 * place, where it is there */
	for (int y = BK_TOP_Y0; y <= BK_TOP_Y1; ++y) {
		for (int x = 0; x < GBA_W; ++x) out[1][y * w + x] = black;
		for (int x = 0; x < BK_HEALTH_X1; ++x) out[1][y * w + x] = out[0][y * w + x];
	}
	*nOut = 2;
	return NULL;
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
/* ---- the net's area maps (MMBN-6, an exception: see the rules file)
 *
 * The game keeps the whole area's layout (gBgDataManager: its width and
 * height in tiles, bytes +0 / +1; on the net BG2 and BG3, centred on the
 * camera's 0,0) and draws the screen from it, the whole screen when the
 * camera jumps. A copy of the game (its own Emu, on its own thread; inline
 * in exact runs and where there are no threads) is given the live game's
 * state once MegaMan has been in a new area of the net for half a second
 * (a warp changes the area's number before its layout is in), gCamera's
 * bounds opened up, and MegaMan (so the camera: cameraX = x + y, cameraY =
 * (y - x) / 2) set down a screen at a time over the layout; each frame of
 * BG2 and BG3 (no sprites, no BG0 messages, no BG1 sky) goes into the
 * area's picture where the camera was. The backdrop colour (palette 0) is
 * magenta in the copy, so what's transparent stays black; a layer with
 * nothing transparent on 9 screens over the area is a backdrop, left out.
 * A screen where the game stops walking about (a warp pad under him, a
 * battle) is left out on the first pass, taken from a little way off on
 * the second. Each area in 0.1-0.3 s on one of this machine's cores
 * (tools/scout/mmbnlayout.c; mmbnprobe.c and mmbnmap.c, the walking ways
 * it replaced). The picture is drawn on the second screen at a third of
 * its size around the live camera. */
#define MMBN_CAM 0x02004950     /* gCamera: cameraX (+4), cameraY (+6), playerRealPos (+0x10), bounds (+0x14..0x23) */
#define MMBN_BGDM 0x02009A50    /* gBgDataManager: the layout's width (+0) and height (+1) in tiles */
/* Mega Man Battle Network 2 (docs/games/mega-man-battle-network-2.md) is
 * the same engine, its structures elsewhere: the maps' addresses by game */
typedef struct {
	uint32_t state, area, fading, cam, bgdm;
	const char* title;
} MmbnNet;
static const MmbnNet mmbnNet[2] = {
	{ MMBN_STATE, MMBN_AREA, MMBN_FADING, MMBN_CAM, MMBN_BGDM, "Mega Man Battle Network" },
	{ 0x02009078, 0x02000DC4, 0x02009073, 0x02006370, 0x0200C1F0, "Mega Man Battle Network 2" },
};
static const MmbnNet* mmbn_net(const Emu* e) {
	return &mmbnNet[e->gameCode[2] == '2']; /* (AE2E, AM2P, AE2J; the first game AREx) */
}
static const MmbnNet* mmbnN = &mmbnNet[0]; /* the live game's, set as a map is asked for */
#define MMBN_SETTLE 4           /* frames from setting him down to the screen drawn */
typedef struct {
	uint16_t area;
	bool done, failed, early; /* early: the camera wasn't following him yet */
	int w, h, x0, y0; /* the picture's size, and its top left's screen position */
	int ex0, ey0, ex1, ey1; /* what's drawn on it: its edges (in the picture, ex1 / ey1 past them) */
	uint32_t head[24 * 16]; /* MegaMan's head, facing the camera (0: transparent) */
	int headW, headH, headDown; /* its size; from its top to his feet */
	uint32_t* px;     /* 0: nothing there */
} MmbnMap;
static MmbnMap mmbnMap;
static pthread_t mmbnThread;
static volatile bool mmbnBusy, mmbnStop;
static uint8_t* mmbnJob; /* the state it starts from */
static size_t mmbnJobSize;
static char mmbnRom[1024];
static Emu mmbnEmu;
static bool mmbnEmuOpen;

/* on the net */
static bool mmbn_area_net(uint16_t area) {
	return (area & 0xFF) >= 0x80;
}

/* the copy's camera brought to (cx, cy), as near as the game takes it, and
 * a frame of `layers` drawn there; false if the game stopped walking about */
static bool mmbn_look(Emu* e, uint16_t area, uint32_t pos, int cx, int cy, unsigned layers, int* camx, int* camy) {
	const MmbnNet* N = mmbnN;
	/* (layers take effect as a state's registers load) */
	emu_layers(e, layers);
	emu_state_load(e, mmbnJob);
	emu_write32(e, N->cam + 0x14, (uint32_t) (-30000 * 65536));
	emu_write32(e, N->cam + 0x18, (uint32_t) (30000 * 65536));
	emu_write32(e, N->cam + 0x1C, (uint32_t) (30000 * 65536));
	emu_write32(e, N->cam + 0x20, (uint32_t) (-30000 * 65536));
	int x = (cx - 2 * cy) / 2, y = (cx + 2 * cy) / 2;
	emu_write32(e, pos, (uint32_t) (x * 65536));
	emu_write32(e, pos + 4, (uint32_t) (y * 65536));
	emu_write32(e, pos + 8, 0); /* (on the ground: the camera follows his height too) */
	for (int f = 0; f < MMBN_SETTLE; ++f) emu_frame_blind(e, 0);
	emu_write16(e, 0x05000000, 0x7C1F); /* (magenta) */
	emu_frame(e, 0);
	*camx = (int16_t) emu_read16(e, N->cam + 4), *camy = (int16_t) emu_read16(e, N->cam + 6);
	return emu_read8(e, N->state) == 4 && emu_read16(e, N->area) == area;
}

/* MegaMan's head, the marker on the map: in the copy, set down in a corner
 * of the layout (nothing else there), a tap of DOWN (facing the camera),
 * standing for 20 frames, sprites alone; his sprite above his feet, from
 * its top to its narrowest row below the face (his chin; 16 rows at most) */
static void mmbn_head(Emu* e, uint16_t area, uint32_t pos, MmbnMap* m, uint32_t magenta) {
	const MmbnNet* N = mmbnN;
	m->headW = m->headH = 0;
	emu_layers(e, 0x10);
	emu_state_load(e, mmbnJob);
	emu_write32(e, N->cam + 0x14, (uint32_t) (-30000 * 65536));
	emu_write32(e, N->cam + 0x18, (uint32_t) (30000 * 65536));
	emu_write32(e, N->cam + 0x1C, (uint32_t) (30000 * 65536));
	emu_write32(e, N->cam + 0x20, (uint32_t) (-30000 * 65536));
	int cx = m->x0 + 40, cy = m->y0 + 60, x = (cx - 2 * cy) / 2, y = (cx + 2 * cy) / 2;
	emu_write32(e, pos, (uint32_t) (x * 65536));
	emu_write32(e, pos + 4, (uint32_t) (y * 65536));
	emu_write32(e, pos + 8, 0);
	for (int f = 0; f < 2; ++f) emu_frame_blind(e, K_DOWN);
	for (int f = 0; f < 20; ++f) emu_frame_blind(e, 0);
	emu_write16(e, 0x05000000, 0x7C1F);
	emu_frame(e, 0);
	emu_layers(e, 0x1F);
	if (emu_read8(e, N->state) != 4 || emu_read16(e, N->area) != area) return;
	x = (int32_t) emu_read32(e, pos) >> 16, y = (int32_t) emu_read32(e, pos + 4) >> 16;
	int camx = (int16_t) emu_read16(e, N->cam + 4), camy = (int16_t) emu_read16(e, N->cam + 6);
	int fx = x + y - camx + GBA_W / 2, fy = (y - x) / 2 - camy + GBA_H / 2; /* his feet on the screen */
	if (fx < 12 || fx > GBA_W - 12 || fy < 48 || fy > GBA_H) return;
	int top = -1, wid[16] = { 0 };
	for (int r = fy - 48; r < fy && top < 0; ++r)
		for (int c = fx - 12; c < fx + 12; ++c)
			if ((e->fb[r * GBA_W + c] & 0xFFFFFF) != magenta) top = r;
	if (top < 0) return;
	int h = 0, widest = 0;
	for (; h < 16 && top + h < fy; ++h) {
		for (int c = fx - 12; c < fx + 12; ++c) wid[h] += (e->fb[(top + h) * GBA_W + c] & 0xFFFFFF) != magenta;
		if (wid[h] > widest) widest = wid[h];
		/* (the chin: narrower than the widest, and the next row wider) */
		if (h > 4 && wid[h] < widest && h + 1 < 16) {
			int next = 0;
			for (int c = fx - 12; c < fx + 12; ++c) next += (e->fb[(top + h + 1) * GBA_W + c] & 0xFFFFFF) != magenta;
			if (next > wid[h]) {
				++h;
				break;
			}
		}
	}
	for (int r = 0; r < h; ++r)
		for (int c = 0; c < 24; ++c) {
			uint32_t px = e->fb[(top + r) * GBA_W + fx - 12 + c] & 0xFFFFFF;
			m->head[r * 24 + c] = px == magenta ? 0 : px | 0xFF000000;
		}
	m->headW = 24, m->headH = h, m->headDown = fy - top;
}

static void* mmbn_worker(void* arg) {
	const MmbnNet* N = mmbnN;
	(void) arg;
	Emu* e = &mmbnEmu;
	MmbnMap* m = &mmbnMap;
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	const char* why = NULL;
	if (!mmbnEmuOpen) mmbnEmuOpen = emu_open(e, mmbnRom, NULL, false);
	if (!mmbnEmuOpen && (why = "the ROM didn't open")) goto out;
	if (!emu_state_load(e, mmbnJob) && (why = "the game's moment didn't load")) goto out;
	uint16_t area = m->area;
	uint32_t pos = emu_read32(e, N->cam + 0x10);
	if ((pos < 0x02000000 || pos >= 0x02040000) && (why = "no position")) goto out;
	int W = m->w, H = m->h, X0 = m->x0, Y0 = m->y0, cx, cy;
	/* the copy's magenta, as its renderer puts it out */
	if (!mmbn_look(e, area, pos, X0 + W / 2, Y0 + H / 2, 0, &cx, &cy) && (why = "he can't be set down")) goto out;
	uint32_t magenta = e->fb[0] & 0xFFFFFF;
	/* (arriving on a warp pad, he steps off it by himself, the camera
	 * following that, not where he's set down: tried again later) */
	int hz = (int32_t) emu_read32(e, pos + 8) >> 16; /* (his height, if the floor there lifts him) */
	if ((cx < X0 + W / 2 - 16 || cx > X0 + W / 2 + 16 || cy < Y0 + H / 2 - hz - 16 || cy > Y0 + H / 2 - hz + 16) &&
	    (why = "the camera isn't following him yet"))
		m->early = true;
	if (why) goto out;
	/* the backdrop: a layer with nothing transparent */
	unsigned layers = 0;
	for (int L = 2; L <= 3 && !mmbnStop; ++L) {
		long clear = 0, drawn = 0;
		for (int k = 0; k < 9 && !mmbnStop; ++k) {
			if (!mmbn_look(e, area, pos, X0 + W * (1 + k % 3) / 4, Y0 + H * (1 + k / 3) / 4, 1u << L, &cx, &cy)) continue;
			for (int i = 0; i < GBA_W * GBA_H; ++i) {
				if ((e->fb[i] & 0xFFFFFF) == magenta) ++clear;
				else ++drawn;
			}
		}
		if (drawn && clear) layers |= 1u << L;
	}
	if (!layers && !mmbnStop && (why = "nothing drawn")) goto out;
	/* the screens, 8 pixels overlapping; the second pass a little way off
	 * from where the first left something out */
	for (int pass = 0; pass < 2 && !mmbnStop; ++pass)
		for (int ty0 = Y0 + GBA_H / 2; ty0 < Y0 + H + GBA_H / 2 && !mmbnStop; ty0 += GBA_H - 8)
			for (int tx0 = X0 + GBA_W / 2; tx0 < X0 + W + GBA_W / 2 && !mmbnStop; tx0 += GBA_W - 8) {
				int tx = tx0 < X0 + W - GBA_W / 2 ? tx0 : X0 + W - GBA_W / 2, ty = ty0 < Y0 + H - GBA_H / 2 ? ty0 : Y0 + H - GBA_H / 2;
				if (pass) {
					bool need = false;
					for (int y = ty - GBA_H / 2; y < ty + GBA_H / 2 && !need; y += 4)
						for (int x = tx - GBA_W / 2; x < tx + GBA_W / 2 && !need; x += 4)
							need = x >= X0 && y >= Y0 && x < X0 + W && y < Y0 + H && !m->px[(size_t) (y - Y0) * W + x - X0];
					if (!need) continue;
					tx += 24, ty += 16;
				}
				if (!mmbn_look(e, area, pos, tx, ty, layers, &cx, &cy)) {
					if (getenv("GBADS_DEBUG"))
						fprintf(stderr, "mmbn map: no screen at %d,%d (state %d, area %04X)\n", tx, ty, emu_read8(e, N->state),
						        emu_read16(e, N->area));
					continue;
				}
				for (int sy = 0; sy < GBA_H; ++sy) {
					int py = cy - GBA_H / 2 + sy - Y0;
					if (py < 0 || py >= H) continue;
					for (int sx = 0; sx < GBA_W; ++sx) {
						int qx = cx - GBA_W / 2 + sx - X0;
						if (qx < 0 || qx >= W) continue;
						uint32_t c = e->fb[sy * GBA_W + sx] & 0xFFFFFF;
						/* (transparent: black, but marked seen) */
						m->px[(size_t) py * W + qx] = c == magenta ? 0xFE000000 : c | 0xFF000000;
					}
				}
			}
	emu_layers(e, 0x1F);
	if (!mmbnStop) {
		/* the area's edges: its last paths and panels */
		m->ex0 = W, m->ey0 = H, m->ex1 = m->ey1 = 0;
		for (int y = 0; y < H; ++y)
			for (int x = 0; x < W; ++x)
				if (m->px[(size_t) y * W + x] >> 24 == 0xFF) {
					if (x < m->ex0) m->ex0 = x;
					if (x >= m->ex1) m->ex1 = x + 1;
					if (y < m->ey0) m->ey0 = y;
					if (y >= m->ey1) m->ey1 = y + 1;
				}
		if (m->ex1 <= m->ex0) m->ex0 = 0, m->ey0 = 0, m->ex1 = W, m->ey1 = H;
		mmbn_head(e, area, pos, m, magenta);
		m->done = true;
		struct timespec t1;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		fprintf(stderr, "%s: area %04X mapped (%dx%d, layers %X, %ld ms)\n", N->title, area, W, H, layers,
		        (long) ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000));
	}
out:
	if (why) {
		m->done = m->failed = true;
		/* (always logged: why the second screen keeps the logo on the net) */
		fprintf(stderr, "%s: area %04X not mapped: %s\n", N->title, m->area, why);
	}
	mmbnBusy = false;
	return NULL;
}

/* the live game's map of its area on the net, at a third of its size around its
 * camera (MMBN-6); a new area's map made beside the game */
static const uint32_t* mmbn_live_view(Emu* live, const char* rom) {
	static uint32_t out[GBA_W * GBA_H];
	const MmbnNet* N = mmbnN = mmbn_net(live);
	uint8_t st = emu_read8(live, N->state);
	uint16_t area = emu_read16(live, N->area);
	if ((st != 4 && st != 0x18) || !mmbn_area_net(area)) return NULL;
	/* a warp: the area's number changes before its layout is in; a new map
	 * waits for half a second of the same area, walking, no fade */
	static uint16_t settledArea;
	static int settledFor;
	if (area != settledArea) settledArea = area, settledFor = 0;
	else if (st == 4 && !emu_read8(live, N->fading)) ++settledFor;
	else settledFor = 0;
	MmbnMap* m = &mmbnMap;
	if (m->px && m->area != area) {
		/* another area: its map dropped (called off first if still being made) */
		if (mmbnBusy) {
			mmbnStop = true;
			return NULL;
		}
		if (mmbnThread) pthread_join(mmbnThread, NULL), mmbnThread = 0;
		free(m->px);
		memset(m, 0, sizeof(*m));
	}
	if (!m->px) {
		if (st != 4 || settledFor < 30 || mmbnBusy) return NULL;
		if (mmbnThread) pthread_join(mmbnThread, NULL), mmbnThread = 0;
		int tw = emu_read8(live, N->bgdm), th = emu_read8(live, N->bgdm + 1);
		if (tw < 30 || th < 20) return NULL;
		m->area = area;
		m->w = tw * 8, m->h = th * 8, m->x0 = -tw * 4, m->y0 = -th * 4;
		if (!(m->px = calloc((size_t) m->w * m->h, 4))) {
			memset(m, 0, sizeof(*m));
			return NULL;
		}
		size_t sz = emu_state_size(live);
		if (sz > mmbnJobSize) free(mmbnJob), mmbnJob = malloc(sz), mmbnJobSize = mmbnJob ? sz : 0;
		if (!mmbnJob || !emu_state_save(live, mmbnJob)) {
			free(m->px);
			memset(m, 0, sizeof(*m));
			return NULL;
		}
		snprintf(mmbnRom, sizeof(mmbnRom), "%s", rom);
		mmbnStop = false;
		mmbnBusy = true;
		/* in exact runs (the tests), and with no threads, made here */
		if (getenv("GBADS_SYNC") || pthread_create(&mmbnThread, NULL, mmbn_worker, NULL)) mmbnThread = 0, mmbn_worker(NULL);
	}
	if (m->failed && m->early) {
		/* again in a second, for at most ten seconds */
		static uint16_t earlyArea;
		static int early;
		if (earlyArea != area) earlyArea = area, early = 0;
		if (!mmbnBusy && ++early <= 10) {
			if (mmbnThread) pthread_join(mmbnThread, NULL), mmbnThread = 0;
			free(m->px);
			memset(m, 0, sizeof(*m));
			settledFor = -30;
		}
		return NULL;
	}
	if (m->failed || !m->done) return NULL; /* (the logo meanwhile: it's quick) */
	/* a third of the size: each pixel the mean of 3x3 of the picture's,
	 * centred on MegaMan (his screen position: x + y, (y - x) / 2 - z), but
	 * kept within the area's edges; an area smaller than the screen in
	 * its middle */
	enum { Z = 3 };
	int camx = (int16_t) emu_read16(live, N->cam + 4), camy = (int16_t) emu_read16(live, N->cam + 6);
	uint32_t pos = emu_read32(live, N->cam + 0x10);
	if (pos >= 0x02000000 && pos < 0x02040000) {
		int x = (int32_t) emu_read32(live, pos) >> 16, y = (int32_t) emu_read32(live, pos + 4) >> 16;
		int z = (int32_t) emu_read32(live, pos + 8) >> 16; /* (his height: on a raised floor, higher up) */
		camx = x + y, camy = (y - x) / 2 - z;
	}
	int left = camx - GBA_W * Z / 2 - m->x0, top = camy - GBA_H * Z / 2 - m->y0;
	if (m->ex1 - m->ex0 <= GBA_W * Z) left = (m->ex0 + m->ex1) / 2 - GBA_W * Z / 2;
	else if (left < m->ex0) left = m->ex0;
	else if (left > m->ex1 - GBA_W * Z) left = m->ex1 - GBA_W * Z;
	if (m->ey1 - m->ey0 <= GBA_H * Z) top = (m->ey0 + m->ey1) / 2 - GBA_H * Z / 2;
	else if (top < m->ey0) top = m->ey0;
	else if (top > m->ey1 - GBA_H * Z) top = m->ey1 - GBA_H * Z;
	for (int oy = 0; oy < GBA_H; ++oy) {
		int py = top + oy * Z;
		for (int ox = 0; ox < GBA_W; ++ox) {
			int qx = left + ox * Z;
			unsigned r = 0, g = 0, b = 0;
			for (int dy = 0; dy < Z; ++dy) {
				if (py + dy < 0 || py + dy >= m->h) continue;
				for (int dx = 0; dx < Z; ++dx) {
					if (qx + dx < 0 || qx + dx >= m->w) continue;
					uint32_t c = m->px[(size_t) (py + dy) * m->w + qx + dx];
					r += c & 0xFF, g += (c >> 8) & 0xFF, b += (c >> 16) & 0xFF;
				}
			}
			out[oy * GBA_W + ox] = 0xFF000000 | (b / (Z * Z)) << 16 | (g / (Z * Z)) << 8 | r / (Z * Z);
		}
	}
	/* his head where he stands, at the game's size: centred on where his
	 * sprite, at a third, would be */
	if (m->headH) {
		int hx = (camx - m->x0 - left) / Z - m->headW / 2, hy = (camy - m->y0 - top - m->headDown / 2) / Z - m->headH / 2;
		for (int r = 0; r < m->headH; ++r) {
			if (hy + r < 0 || hy + r >= GBA_H) continue;
			for (int c = 0; c < m->headW; ++c)
				if (m->head[r * 24 + c] && hx + c >= 0 && hx + c < GBA_W) out[(hy + r) * GBA_W + hx + c] = m->head[r * 24 + c];
		}
	}
	return out;
}

static const char* mmbn_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (w != GBA_W || h != GBA_H) return "not a GBA screen";
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	if (emu_read8(s, MMBN_STATE) != 4) return "not walking about";
	/* (on the net too: the map's shown there while walking about, MMBN-6,
	 * the status in its battles, MMBN-4) */
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

/* ---------------------------------------------------------- Mega Man Battle Network 2
 *
 * The first game's second screen (docs/games/mega-man-battle-network-2.md):
 * walking about, MegaMan's status and money in one frame (MMBN2-2),
 * mmbn2_compose on the shadow core: START's menu opened, the money from its
 * box at the top right (its HP line left out), then the MegaMan screen's
 * panel (rows 0-95) with its cursor (a sprite) left out; put together as
 * the first game's, the box's orange edge across the screen above and
 * below. On the net, the area's map (MMBN2-6): mmbn_live_view, shared with
 * the first game (mmbnNet). Every release has the same RAM addresses. */
#define MMBN2_STATE 0x02009078  /* gManager.gameState: 4 walking about, 8 battle, 0x18 a PET menu */
#define MMBN2_FADING 0x02009073 /* gManager: a fade going */
#define MMBN2_AREA 0x02000DC4   /* gWorld: the area (halfword) */
#define MMBN2_HP 0x02001130     /* HP, then its maximum (what the MegaMan screen prints; gWorld +0x20 a copy) */
#define MMBN2_ZENNY 0x02000E34  /* gWorld: the money (word) */
#define MMBN2_SMENU 0x0200D580  /* START's menu: state (+0, 4 open), cursor (+6, 3 MegaMan) */
#define MMBN2_MENU 0x02007EA0   /* the PET's menus: which (+0, 0x0C MegaMan), step (+1, 4 open) */
/* START's box (BG0): x 146-239, rows 0-47; its outline and orange frame
 * rows 0-3 and 44-47, columns 146-151; inside x 152-239: the dark band
 * rows 4-7, HP rows 10-22, the money rows 26-38 */
#define MMBN2_BOX_X 152
#define MMBN2_BAND_Y 4
#define MMBN2_BAND_H 4
#define MMBN2_MONEY_Y 23        /* the money's rows, 3 above it to 4 below */
#define MMBN2_MONEY_H 20
#define MMBN2_BOX_H 48
#define MMBN2_EDGE 4
#define MMBN2_GLOW_X 160        /* its edges' glow, x 160-231 (72 wide), with plain orange either side */
#define MMBN2_GLOW_W 72
#define MMBN2_PLAIN_X 154
static const Probe mmbn2Panel[8] = { { 5, 5, 0x5A6B6B }, { 120, 3, 0x5A6B6B }, { 238, 10, 0x5A6B6B }, { 2, 50, 0x313939 },
                                     { 236, 60, 0x5A6B6B }, { 230, 90, 0x313939 }, { 120, 92, 0x313939 }, { 150, 94, 0x313939 } };
static const Probe mmbn2Money[8] = { { 146, 0, 0x313939 }, { 146, 47, 0x313939 }, { 148, 2, 0xCE5A39 }, { 148, 45, 0xCE5A39 },
                                     { 200, 1, 0xF78C00 }, { 200, 45, 0xF78C00 }, { 180, 5, 0x4A5A63 }, { 160, 25, 0x5A6B6B } };
/* the MegaMan screen's cursor (a 16x16 sprite at x 88, Japan's at 96), by
 * the STYLE / ATTACK / RAPID / CHARGE rows; MegaMan's sprites left of it */
#define MMBN2_CURSOR_X0 72
#define MMBN2_CURSOR_X1 119
#define MMBN2_CURSOR_Y0 16
#define MMBN2_LIGHT 0x10AD21    /* the stats' lights' dark green (21AD10), as the frame buffer holds it */
static bool mmbn2_megaman_up(Emu* s) {
	return emu_read8(s, MMBN2_STATE) == 0x18 && emu_read8(s, MMBN2_MENU) == 0x0C && emu_read8(s, MMBN2_MENU + 1) == 4 &&
	       !emu_read8(s, MMBN2_FADING);
}
static const char* mmbn2_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (w != GBA_W || h != GBA_H) return "not a GBA screen";
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	if (emu_read8(s, MMBN2_STATE) != 4) return "not walking about";
	/* (on the net too: the map's shown there while walking about, MMBN2-6,
	 * the status in its battles, MMBN2-4) */
	mmbn_tap(s, K_START);
	int f = 0;
	while (f < 40 && emu_read8(s, MMBN2_SMENU) != 4) emu_frame_blind(s, 0), ++f;
	if (emu_cancelled(s)) return "called off";
	if (emu_read8(s, MMBN2_SMENU) != 4) return "START opened nothing";
	aw_run(s, 0, 3); /* (drawn) */
	if (emu_cancelled(s)) return "called off";
	/* the box, from a frame drawn with the menu's layer (BG0) alone */
	static uint8_t tmp[0x80000];
	if (emu_state_size(s) > sizeof(tmp) || !emu_state_save(s, tmp)) return "state save failed";
	emu_layers(s, 0x01);
	emu_state_load(s, tmp); /* (the layers take effect as the display registers are loaded) */
	emu_frame(s, 0);
	static uint32_t box[MMBN2_BOX_H * GBA_W];
	memcpy(box, s->fb, sizeof(box));
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	if (emu_cancelled(s)) return "called off";
	for (int i = 0; i < 8; ++i) {
		uint32_t c = box[mmbn2Money[i].y * GBA_W + mmbn2Money[i].x], want = mmbn2Money[i].rgb;
		if (abs((int) (c & 0xFF) - (int) (want >> 16)) > 8 || abs((int) ((c >> 8) & 0xFF) - (int) ((want >> 8) & 0xFF)) > 8 ||
		    abs((int) ((c >> 16) & 0xFF) - (int) (want & 0xFF)) > 8)
			return "no money box";
	}
	for (int i = 0; i < 10 && emu_read8(s, MMBN2_SMENU + 6) != 3; ++i)
		mmbn_tap(s, emu_read8(s, MMBN2_SMENU + 6) < 3 ? K_DOWN : K_UP);
	if (emu_read8(s, MMBN2_SMENU + 6) != 3) return "MegaMan not reached in the menu";
	mmbn_tap(s, K_A);
	f = 0;
	while (f < 90 && !mmbn2_megaman_up(s)) emu_frame_blind(s, 0), ++f;
	if (emu_cancelled(s)) return "called off";
	if (!mmbn2_megaman_up(s)) return "the MegaMan screen didn't open";
	aw_run(s, 0, 4); /* (its values printed) */
	if (emu_cancelled(s)) return "called off";
	if (!som_probes(s, mmbn2Panel, 8, 8, 8)) return "not the MegaMan screen";
	/* the panel, its flashing lights steady (MMBN2-2: with a PowerUP to use,
	 * some flash in a 16-frame cycle, 8 frames bright, 8 as they are): taken
	 * twice, 8 frames apart, the one with more of the lights' dark green
	 * kept; then the cursor (a sprite, by the rows) from a frame drawn
	 * without sprites there (MegaMan's are left of it) */
	static uint32_t panel[MMBN_PANEL_ROWS * GBA_W];
	static uint8_t tmp2[0x80000];
	memcpy(panel, s->fb, sizeof(panel));
	if (!emu_state_save(s, tmp)) return "state save failed";
	int dark[2] = { 0, 0 };
	for (int i = 0; i < MMBN_PANEL_ROWS * GBA_W; ++i) dark[0] += (panel[i] & 0xFFFFFF) == MMBN2_LIGHT;
	aw_run(s, 0, 8);
	if (emu_cancelled(s)) return "called off";
	for (int i = 0; i < MMBN_PANEL_ROWS * GBA_W; ++i) dark[1] += (s->fb[i] & 0xFFFFFF) == MMBN2_LIGHT;
	if (dark[1] > dark[0] && mmbn2_megaman_up(s) && som_probes(s, mmbn2Panel, 8, 8, 8)) {
		memcpy(panel, s->fb, sizeof(panel));
		if (!emu_state_save(s, tmp2)) return "state save failed";
		memcpy(tmp, tmp2, sizeof(tmp));
	}
	emu_layers(s, 0x0F);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	for (int y = MMBN2_CURSOR_Y0; y < MMBN_PANEL_ROWS; ++y)
		for (int x = MMBN2_CURSOR_X0; x <= MMBN2_CURSOR_X1; ++x) panel[y * GBA_W + x] = s->fb[y * GBA_W + x];
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	/* the frame: edge, money, panel, edge, centred top to bottom */
	for (int i = 0; i < GBA_W * GBA_H; ++i) out[i] = 0xFF000000;
	int all = 2 * MMBN2_EDGE + MMBN2_BAND_H + MMBN2_MONEY_H + MMBN_PANEL_ROWS, y0 = (GBA_H - all) / 2;
	int glow0 = (GBA_W - MMBN2_GLOW_W) / 2; /* (the glow in the middle) */
	for (int e = 0; e < 2; ++e) {
		int from = e ? MMBN2_BOX_H - MMBN2_EDGE : 0, to = e ? y0 + all - MMBN2_EDGE : y0;
		for (int r = 0; r < MMBN2_EDGE; ++r) {
			const uint32_t* src = &box[(from + r) * GBA_W];
			for (int x = 0; x < GBA_W; ++x) {
				int g = x - glow0;
				out[(to + r) * GBA_W + x] = g >= 0 && g < MMBN2_GLOW_W ? src[MMBN2_GLOW_X + g] : src[MMBN2_PLAIN_X];
			}
		}
	}
	/* the box's dark band carried across the screen, then the money's rows
	 * in the panel's top row's colour (the box's lighter grey too), so the
	 * two read as one piece */
	uint32_t head = panel[2 * GBA_W + 2], light = box[(MMBN2_MONEY_Y + 1) * GBA_W + MMBN2_BOX_X + 2];
	int y = y0 + MMBN2_EDGE;
	for (int r = 0; r < MMBN2_BAND_H; ++r, ++y) {
		const uint32_t* m = &box[(MMBN2_BAND_Y + r) * GBA_W];
		for (int x = 0; x < GBA_W; ++x) out[y * GBA_W + x] = m[x < MMBN2_BOX_X ? MMBN2_BOX_X : x];
	}
	for (int r = 0; r < MMBN2_MONEY_H; ++r, ++y) {
		const uint32_t* m = &box[(MMBN2_MONEY_Y + r) * GBA_W];
		for (int x = 0; x < GBA_W; ++x) out[y * GBA_W + x] = x < MMBN2_BOX_X || m[x] == light ? head : m[x];
	}
	memcpy(&out[y * GBA_W], panel, sizeof(panel));
	return NULL;
}

/* ------------------------------ Harry Potter and the Prisoner of Azkaban
 *
 * Walking about, the second screen is the party's summary from START's
 * Status/Equip screen above the task box from the bottom of START's menu
 * (HPA-1): hpa_compose, on the hidden copy. The screen the game is on,
 * HPA_MODE: 3 the title, 7 the file screens, 8 walking about, 0x18 a
 * conversation or a scene (START opens nothing), 9 a battle, 0x2C its
 * results; START's menu 0x0A (opened) or 0x0B (back from one of its
 * screens), Status/Equip's summary 0x0C or 0x0D, a character's page 0x0E,
 * Items 0x10, Folios 0x13, Save Game 0x14, Connectivity 0x16, Help 0x17.
 * HPA_CURSOR: the menu's cursor (0 Status/Equip). Japan (Harry Potter to
 * Azkaban no Shuujin): both 0x60 higher (by romxref), the rest the same. */
#define HPA_MODE 0x03003F18
#define HPA_CURSOR 0x03003F14
#define HPA_J(e) ((e)->gameCode[3] == 'J' ? 0x60u : 0u)
#define HPA_SP 0x030024F4 /* Harry's Stamina (SP), then his Magic (MP): halves */
#define HPA_V_LIVE 0xF0000040 /* walking about, or START's menu open (HPA-1, HPA-2) */
#define HPA_V_FORGET 0xF0000041 /* not walking about, in the menu or a scene: the title, a battle... (HPA-4, HPA-5) */
#define HPA_V_BATTLE 0xF0000042 /* a battle, to its results (HPA-4) */
/* the summary: rows 25-148 (each one's figure to the bottom of their box;
 * the heading bar above it left out); the task box: rows 122-157 (its frame,
 * the posts at its ends but for their outer rows) */
#define HPA_SUM_Y 25
#define HPA_SUM_H 124
#define HPA_TASK_Y 122
#define HPA_TASK_H 36
static bool hpa_menu_mode(int m) {
	return m >= 0x0A && m <= 0x17;
}
static int hpa_mode(Emu* e) {
	return emu_read8(e, HPA_MODE + HPA_J(e)) & 0x7F; /* (0x80 set for a frame or two as it changes) */
}
static uint32_t hpa_virt(Emu* e, uint32_t addr) {
	int m = hpa_mode(e);
	if (addr == HPA_V_LIVE) return m == 8 || hpa_menu_mode(m);
	if (addr == HPA_V_FORGET) return !(m == 8 || m == 0x18 || hpa_menu_mode(m));
	if (addr == HPA_V_BATTLE) return m == 9 || m == 0x2C;
	return 0;
}
/* the menu's task box (its frame's gold and cream); the Status/Equip
 * screen's heading bar and the parchment's left edge */
static const Probe hpaTask[6] = { { 5, 121, 0xB57B31 }, { 60, 123, 0xD6A54A }, { 60, 124, 0xF7E78C },
                                  { 120, 155, 0xF7E78C }, { 120, 156, 0xD6A54A }, { 5, 158, 0xB57B31 } };
static const Probe hpaStatus[6] = { { 5, 22, 0x845A31 }, { 60, 22, 0x845A31 }, { 0, 23, 0x8C8C84 },
                                    { 200, 23, 0x8C8C84 }, { 5, 60, 0x9C634A }, { 5, 146, 0x9C634A } };
static void hpa_tap(Emu* s, uint32_t keys) {
	aw_run(s, keys, 3);
	aw_run(s, 0, 8);
}
static bool hpa_wait_mode(Emu* s, int a, int b, int n) {
	for (int f = 0; f < n; ++f) {
		int m = hpa_mode(s);
		if (m == a || m == b) return true;
		emu_frame_blind(s, 0);
	}
	int m = hpa_mode(s);
	return m == a || m == b;
}
static const char* hpa_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (w != GBA_W || h != GBA_H) return "not a GBA screen";
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	/* to START's menu: from walking about, START; from one of its
	 * screens, B back to it (HPA-2) */
	int m = hpa_mode(s);
	for (int i = 0; i < 4 && hpa_menu_mode(m) && m != 0x0A && m != 0x0B; ++i) {
		hpa_tap(s, K_B);
		hpa_wait_mode(s, 0x0A, 0x0B, 30);
		m = hpa_mode(s);
	}
	if (m == 8) hpa_tap(s, K_START);
	else if (m != 0x0A && m != 0x0B) return "not walking about";
	if (!hpa_wait_mode(s, 0x0A, 0x0B, 60)) return "START opened nothing";
	aw_run(s, 0, 50); /* (faded in) */
	if (emu_cancelled(s)) return "called off";
	if (!som_probes(s, hpaTask, 6, 6, 8)) return "no task box";
	static uint32_t task[HPA_TASK_H * GBA_W];
	memcpy(task, &s->fb[HPA_TASK_Y * GBA_W], sizeof(task));
	/* Status/Equip, the menu's first line */
	for (int i = 0; i < 8 && emu_read8(s, HPA_CURSOR + HPA_J(s)) != 0; ++i) hpa_tap(s, K_UP);
	if (emu_read8(s, HPA_CURSOR + HPA_J(s)) != 0) return "Status/Equip not reached in the menu";
	hpa_tap(s, K_A);
	if (!hpa_wait_mode(s, 0x0C, 0x0D, 60)) return "Status/Equip didn't open";
	aw_run(s, 0, 50);
	if (emu_cancelled(s)) return "called off";
	if (!som_probes(s, hpaStatus, 6, 6, 8)) return "not the Status/Equip screen";
	/* drawn without its sprites: the wand cursor and its sparkles */
	static uint8_t tmp[0x80000];
	if (emu_state_size(s) > sizeof(tmp) || !emu_state_save(s, tmp)) return "state save failed";
	emu_layers(s, 0x0F);
	emu_state_load(s, tmp); /* (the layers take effect as the display registers are loaded) */
	emu_frame(s, 0);
	memcpy(out, &s->fb[HPA_SUM_Y * GBA_W], HPA_SUM_H * GBA_W * sizeof(uint32_t));
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp); /* every layer back on */
	memcpy(&out[HPA_SUM_H * GBA_W], task, sizeof(task));
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

/* ---------------------------------------------------------------- Eragon
 *
 * docs/games/eragon.md. Where L opens the map (the overworld), the map;
 * elsewhere (towns and dungeons), the Party screen (START, Party) with every member's card lit;
 * the logo in battles and on the title. Both releases use these addresses:
 *   ER_SCREEN 1 in the field, 3 the title, 6 a conversation, 7 the
 *             European language screen, 13 the START menu, 14 the Party
 *             screen, 16 a battle, 26 the overworld map (0x8x / 0x9x
 *             between)
 *   ER_PLACE  12 the overworld, 10 a town or dungeon, but also some wild
 *             places with a map (the desert); in a battle it runs through
 *             the battle's own steps: only for following the player (ER-9),
 *             L itself tells where there's a map
 *   ER_BATTLE 1 from a battle's start to its end (5 or 6 in the debug
 *             menu's battles): a battle is anything but 0
 * The Party screen dims every card but the chosen one (the first as it
 * opens); its cards are ER_CARD rows apart, so each band of rows is taken
 * from the picture with that card chosen (DOWN), ER-2. */
#define ER_SCREEN 0x03005660
#define ER_PLACE 0x03002330
#define ER_BATTLE 0x0200CAF1
#define ER_FIELD 1
#define ER_MENU 13
#define ER_PARTY 14
#define ER_MAP 26
#define ER_CARD 56
#define ER_V_TITLE 0xF0000080  /* the title or the language screen (ER-6) */
#define ER_V_FORGET 0xF0000081 /* the title or a battle: the pictures go (ER-5, ER-6) */
#define ER_V_PLACE 0xF0000082  /* the kind of place, 0 in a battle (ER-9) */
#define ER_V_BATTLE 0xF0000083 /* a battle: its plates' last picture kept (ER-5) */
/* the party's name plates and HP bars along the top in a battle (ER-5):
 * BG3's first map entry is the first plate's corner exactly while they're
 * up (the combo track has none there) */
#define ER_PLATES 0x0600E002
#define ER_PLATE 0x0212

static uint32_t er_virt(Emu* e, uint32_t addr) {
	int screen = emu_read8(e, ER_SCREEN);
	bool title = screen == 3 || screen == 7;
	bool battle = emu_read8(e, ER_BATTLE) != 0;
	if (addr == ER_V_TITLE) return title;
	if (addr == ER_V_BATTLE) return battle;
	if (addr == ER_V_FORGET) return title || battle;
	if (addr == ER_V_PLACE) return battle ? 0 : emu_read8(e, ER_PLACE);
	return 0;
}

/* a press the game takes (it misses the quickest taps) */
static void er_press(Emu* s, uint32_t key) {
	aw_run(s, key, 4);
	aw_run(s, 0, 4);
}

static bool er_on(Emu* s, int screen) {
	return emu_read8(s, ER_SCREEN) == screen;
}

/* runs up to max frames (not drawn) until the screen byte reads screen */
static bool er_wait(Emu* s, int screen, int max) {
	for (int f = 0; f < max && !er_on(s, screen); ++f) emu_frame_blind(s, 0);
	return er_on(s, screen);
}

/* runs frames until the picture has held still for `need` (past a slide
 * or a fade): 12 for the Party screen (its cards come in two steps, ~8
 * frames apart), 4 for the map (Eragon's figure steps in place every 6),
 * and not dark (the black between the field and the map); false if it
 * hasn't within max */
static bool er_settle(Emu* s, int need, int max) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	for (int f = 0; f < max; ++f) {
		emu_frame(s, 0);
		still = f && !memcmp(prev, s->fb, sizeof(prev)) && !sn_dark(s->fb) ? still + 1 : 0;
		if (still >= need) return true;
		memcpy(prev, s->fb, sizeof(prev));
	}
	return false;
}

/* back to walking about from the START menu or one of its screens, the
 * map too (B, each step) */
static bool er_to_field(Emu* s) {
	if (er_on(s, ER_FIELD)) return true;
	for (int i = 0; i < 5 && !er_on(s, ER_FIELD); ++i) {
		er_press(s, K_B);
		er_wait(s, ER_FIELD, 40);
	}
	/* (the field takes no START or L for ~120 frames after the menu,
	 * ~140 after the map) */
	aw_run(s, 0, 160);
	return er_on(s, ER_FIELD);
}

static void er_put(const uint32_t* px, uint32_t* out, int w, int h, int y0, int y1) {
	int cw = w < GBA_W ? w : GBA_W;
	for (int y = y0; y < y1 && y < h && y < GBA_H; ++y) memcpy(&out[y * w], &px[y * GBA_W], (size_t) cw * 4);
}

static const char* er_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	/* a battle: the logo (ER-5); elsewhere neither screen opens but from
	 * the field, its menu or the map (ER-4): quick failures */
	if (emu_read8(s, ER_BATTLE) != 0) return "map button did nothing";
	int screen = emu_read8(s, ER_SCREEN);
	if (screen != ER_FIELD && screen != ER_MENU && screen != ER_PARTY && screen != ER_MAP) return "map button did nothing";
	/* the map where L opens one (ER-3): the screen byte reads 0x9A (its
	 * way in) at once; in a town it stays at ER_FIELD. (ER_PLACE doesn't
	 * tell: the desert reads as a town there.) */
	bool map = er_on(s, ER_MAP);
	if (!map) {
		if (!er_to_field(s)) return "not walking about";
		er_press(s, K_L);
		map = (emu_read8(s, ER_SCREEN) & 0x7F) == ER_MAP;
		if (map && !er_wait(s, ER_MAP, 60)) return "the map didn't open";
	}
	if (map) {
		if (!er_settle(s, 4, 120)) return "the map didn't settle";
		if (emu_cancelled(s)) return "called off";
		er_put(s->fb, out, w, h, 0, GBA_H);
		return NULL;
	}
	/* no map here: the Party screen (ER-1), opened afresh so the first
	 * card is chosen */
	if (!er_on(s, ER_FIELD)) return "not walking about";
	er_press(s, K_START);
	if (!er_wait(s, ER_MENU, 60)) return "map button did nothing";
	/* the menu takes A ~50 frames after it's up (its eye keeps moving, so
	 * no still picture tells when): wait, then A until the Party screen
	 * opens (its cursor starts on Party) */
	aw_run(s, 0, 46);
	for (int i = 0; i < 4 && !er_on(s, ER_PARTY); ++i) {
		er_press(s, K_A);
		er_wait(s, ER_PARTY, 12);
	}
	if (!er_on(s, ER_PARTY)) return "the Party screen didn't open";
	if (!er_settle(s, 12, 150)) return "the Party screen didn't settle";
	/* each card from the picture with it chosen (ER-2): DOWN to the next,
	 * until DOWN changes nothing (fewer in the party: the empty slot stays
	 * as drawn) */
	static uint32_t last[GBA_W * GBA_H];
	memcpy(last, s->fb, sizeof(last));
	er_put(s->fb, out, w, h, 0, GBA_H);
	for (int k = 1; k * ER_CARD < GBA_H; ++k) {
		er_press(s, K_DOWN);
		if (!er_settle(s, 12, 60)) return "the Party screen didn't settle";
		if (!er_on(s, ER_PARTY)) return "the Party screen closed";
		if (!memcmp(last, s->fb, sizeof(last))) break;
		memcpy(last, s->fb, sizeof(last));
		er_put(s->fb, out, w, h, k * ER_CARD, (k + 1) * ER_CARD);
	}
	if (emu_cancelled(s)) return "called off";
	return NULL;
}

/* ---------------------------------------------------------------- MOTHER 3
 *
 * docs/games/mother-3.md. Walking about, the area's map where R opens one
 * (M3-2), else START's Status screen, a page per party member (M3-3); the
 * logo on the title and file screens (M3-6). The same addresses in the
 * Japanese release and the English translations:
 *   M3_MODE   0 walking about, 1 a text box (and scenes, and the story's own
 *             battles), 6 / 7 between rooms, 8 an enemy met (the swirl), 9 a
 *             battle (to the end of its results), 0x0A one
 *             of START's screens (Goods, Equip, PSI, Status), 0x0D the area
 *             map (R), 0x0E START's menu, 0x0F the unused debug menu
 *   M3_MAP    (half) the area (the debug menu's map number); 0 on the title,
 *             the file screens and before, and on START's screens
 *   M3_ICON   START's menu: its icon (0 Goods, 1 Equip, 2 PSI, 3 Status,
 *             4 Sleep)
 *   M3_SCREEN 1 while one of the menu's screens is open
 *   M3_WHO    the party member the Status shows (0 the first)
 *   M3_SCENE  bit 0x10 while a scene plays (also while walking about:
 *             between two rooms, M3_MODE 6 / 7, and a dash carried into the
 *             next room, M3_MODE 0, until it stops; not in a conversation the
 *             player starts)
 *   M3_SCREEN_PTR (word) 0x0202CD88 while the battle screen of a story
 *             battle (M3_MODE 1: the Prologue's Mole Cricket) is up, else
 *             0x0202CD48 (0 on the title)
 * Sprites: the area map's scroll arrows (tiles 916-919, at its top and
 * bottom edges), the Status's L / R badges with their orange arrows (tiles
 * 592-595, either end of the portraits). */
#define M3_MODE 0x02005C00
#define M3_MAP 0x0201963C
#define M3_ICON 0x020281EB
#define M3_SCREEN 0x02000024
#define M3_WHO 0x02018E92
#define M3_SCENE 0x0200DEB7
#define M3_SCREEN_PTR 0x02000008
#define M3_PAGES 8
#define M3_V_LOGO 0xF0000060 /* the title, a scene or a battle */
#define M3_HELP_Y 128 /* the Status's help window: rows 130-159, the gap above it from 128 */
static const SpriteRule m3MapArrows = { 0, -16, 240, 160, 916, 919, -1 };
static const SpriteRule m3StatusLR = { -16, -16, 240, 24, 592, 595, -1 };

/* the title and file screens: no area (START's screens clear it too, so
 * not on one of those) */
static bool m3_title(Emu* e) {
	return emu_read16(e, M3_MAP) == 0 && emu_read8(e, M3_MODE) != 0x0A;
}

/* a battle (M3-11) or a scene (M3-10): the logo */
static bool m3_battle(Emu* e) {
	int mode = emu_read8(e, M3_MODE);
	return mode == 0x08 || mode == 0x09 || emu_read32(e, M3_SCREEN_PTR) == 0x0202CD88;
}

static bool m3_scene(Emu* e) {
	int mode = emu_read8(e, M3_MODE);
	return (emu_read8(e, M3_SCENE) & 0x10) && mode != 0 && mode != 6 && mode != 7;
}

static uint32_t m3_virt(Emu* e, uint32_t addr) {
	if (addr == M3_V_LOGO) return m3_title(e) || m3_battle(e) || m3_scene(e);
	return 0;
}

static bool m3_menu(int mode) {
	return mode == 0x0A || mode == 0x0D || mode == 0x0E;
}

/* runs until the mode has held 12 frames (at most n) */
static void m3_settle_mode(Emu* s, int n) {
	int last = -1, held = 0;
	for (int f = 0; f < n && held < 12; ++f) {
		emu_frame_blind(s, 0);
		int m = emu_read8(s, M3_MODE);
		held = m == last ? held + 1 : 0;
		last = m;
	}
}

static bool m3_wait_mode(Emu* s, int mode, int n) {
	for (int f = 0; f < n && emu_read8(s, M3_MODE) != mode; ++f) emu_frame_blind(s, 0);
	return emu_read8(s, M3_MODE) == mode;
}

/* sprites shown that aren't the map's scroll arrows (its marker: blinking) */
static int m3_marks(Emu* s) {
	int n = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a2 = emu_read16(s, 0x07000004 + i * 8);
		if ((a0 & 0x0300) == 0x0200) continue; /* (off) */
		int t = a2 & 0x3FF;
		if (t >= 916 && t <= 919) continue;
		++n;
	}
	return n;
}

/* the Status faded in (its window's white left edge, x 3) and still */
static bool m3_settle(Emu* s) {
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < 120 && still < 3; ++f) {
		aw_run(s, 0, 4);
		bool lit = (s->fb[140 * GBA_W + 3] & 0xFFFFFF) == 0xFFFFFF;
		still = lit && !memcmp(last, s->fb, sizeof(last)) ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	return still >= 3;
}

static void m3_put(const uint32_t* src, uint32_t* dst, int w, int h) {
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&dst[y * w], &src[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
}

/* The Status without its help window (M3-3: "Press the A Button for skill
 * info.", a prompt): the menu's brown background there, as in the gap
 * above it. Found by its white left edge at x 3 under that gap; left as it
 * is if not found. */
static void m3_no_help(uint32_t* px, int w) {
	if (w < GBA_W) return;
	uint32_t bg = px[M3_HELP_Y * w + 3];
	if ((bg & 0xFFFFFF) != (px[M3_HELP_Y * w] & 0xFFFFFF) || (px[140 * w + 3] & 0xFFFFFF) != 0xFFFFFF) return;
	for (int y = M3_HELP_Y; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) px[y * w + x] = bg;
	}
}

static const char* m3_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[M3_PAGES][GBA_W * GBA_H];
	int max = *nOut < M3_PAGES ? *nOut : M3_PAGES;
	*nOut = 1;
	*step = 300; /* 5 seconds a party member (M3-3) */
	emu_hide_sprites(s, NULL, 0);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (m3_title(s)) return "the title"; /* (M3-6) */
	if (m3_battle(s)) return "a battle"; /* (M3-11) */
	if (m3_scene(s)) return "a scene"; /* (M3-10) */
	/* back to walking about from the menu or the map (M3-4) */
	for (int i = 0; i < 5 && m3_menu(emu_read8(s, M3_MODE)); ++i) {
		aw_run(s, K_B, 6);
		m3_settle_mode(s, 120); /* (a screen passes through 0 and 6 on its way back to the menu) */
	}
	m3_settle_mode(s, 60);
	if (emu_read8(s, M3_MODE) != 0) return "not walking about"; /* (M3-5) */

	/* the area's map (M3-2): R, where there's one */
	aw_run(s, K_R, 4);
	if (m3_wait_mode(s, 0x0D, 90)) {
		emu_hide_sprites(s, &m3MapArrows, 1);
		aw_run(s, 0, 30);
		/* the frame with the most of its marks shown (its marker blinks) */
		int best = -1;
		for (int f = 0; f < 60; ++f) {
			int n = m3_marks(s);
			emu_frame(s, 0);
			if (n > best) {
				best = n;
				memcpy(page[0], s->fb, sizeof(page[0]));
			}
		}
		if (emu_cancelled(s)) return "called off";
		if (emu_read8(s, M3_MODE) != 0x0D) return "the map closed";
		m3_put(page[0], out[0], w, h);
		return NULL;
	}
	if (emu_read8(s, M3_MODE) != 0) return "not walking about";

	/* no map here: START, Status, A (M3-3) */
	/* (waits generous: with the HQ audio mix the game lags, M3-9) */
	aw_run(s, K_START, 6);
	if (!m3_wait_mode(s, 0x0E, 120)) return "the menu didn't open";
	aw_run(s, 0, 16);
	for (int i = 0; i < 6 && emu_read8(s, M3_ICON) != 3; ++i) {
		aw_run(s, K_RIGHT, 4);
		aw_run(s, 0, 10);
	}
	if (emu_read8(s, M3_ICON) != 3) return "Status not reached";
	emu_hide_sprites(s, &m3StatusLR, 1);
	aw_run(s, K_A, 4);
	if (!m3_wait_mode(s, 0x0A, 120) || emu_read8(s, M3_SCREEN) != 1) return "the Status didn't open";
	if (!m3_settle(s)) return "the Status didn't fade in";
	int first = emu_read8(s, M3_WHO), n = 0;
	for (; n < max; ++n) {
		if (n) {
			aw_run(s, K_R, 4);
			aw_run(s, 0, 8);
			if (!m3_settle(s)) return "the Status didn't settle";
			if (emu_read8(s, M3_SCREEN) != 1 || emu_read8(s, M3_MODE) != 0x0A) return "the Status closed";
			if (emu_read8(s, M3_WHO) == first) break; /* (round to the first again) */
		}
		if (emu_cancelled(s)) return "called off";
		memcpy(page[n], s->fb, sizeof(page[n]));
	}
	*nOut = n;
	for (int k = 0; k < n; ++k) {
		m3_put(page[k], out[k], w, h);
		m3_no_help(out[k], w);
	}
	return NULL;
}

/* ------------------------------------------- Final Fantasy I & II: Dawn of Souls
 *
 * docs/games/final-fantasy-1-2-dawn-of-souls.md. Two games on one cartridge,
 * each with its own memory, set up from a base the release keeps a pointer
 * to (FfRegion: the pointers, by release; tools/romxref.py):
 *   game1     (word) Final Fantasy's base while it runs, else 0
 *   game2     (word) Final Fantasy II's base while it runs, else 0
 *   FF_BATTLE 1 in a battle (either game) and on the cartridge's own menus,
 *             0 walking about and in the games' menus (every release)
 *   base + 0  Final Fantasy's map number (0 the world map, also on its own
 *             title and New Game screens; 0x38 Castle Cornelia 1F...: the
 *             numbers of the game's event list)
 *   base + FF2_WORLD  Final Fantasy II: the map's tile set, FF2_WORLD_SET on
 *             the world map (also with its map or START's menu open), 5 the
 *             Rebel Hideout, 6 towns, 8 Pandaemonium, 2 in a battle (all
 *             three releases)
 * START's menu and the world map screen are told by their windows' frames
 * (ffMenu1 / ffMenu2, ffMap); the hand cursor is a 16x16 sprite, at the
 * menu's command list or over the map's parchment (where the party's
 * marker is 8x8). */
typedef struct {
	const char* code;
	uint32_t game1, game2;
	/* Final Fantasy's characters: their names (Shift-JIS) from base +
	 * FF_NAMES, this long, then two bytes ending in the class; one record
	 * every this many bytes (FF-12) */
	int nameLen, stride;
	/* Final Fantasy's battle block (FF-13): where its own address is, and
	 * how far apart the characters' HP are */
	uint32_t blkSelf, blkStride;
	/* Final Fantasy II's party HP in a battle, past its base (FF-13) */
	uint32_t ff2BatHp;
} FfRegion;
static const FfRegion ffRegions[] = {
	{ "BFFE", 0x02000584, 0x0200099C, 12, 0x48, 0x7E8, 0x60, 0x7250 },
	{ "BFFP", 0x020004D0, 0x0200080C, 12, 0x48, 0x7E8, 0x60, 0x7274 },
	{ "BFFJ", 0x020004F8, 0x02000904, 8, 0x44, 0x7D8, 0x5C, 0x7240 },
};
#define FF_NAMES 0x7AC
/* Final Fantasy II's characters: their names (Shift-JIS, up to 12 bytes,
 * ended by a 0) from base + FF2_NAMES, one every FF2_STRIDE bytes (seen in
 * the USA release; assumed elsewhere) (FF-12) */
#define FF2_NAMES 0x12
#define FF2_STRIDE 0x80
#define FF2_MARK_TILE 0x87 /* the marks' step with a one-pixel black outline (FF-11) */
#define FF_BATTLE 0x02000000
#define FF_V_LOGO 0xF0000080 /* outside the games (FF-2), or the party defeated (FF-13) */
/* the party's HP in a battle (FF-13): Final Fantasy's in a block the game
 * allocates for the battle (found by its word at FfRegion.blkSelf holding
 * its own address), from FF1_BLK_HP, one every FfRegion.blkStride bytes;
 * Final Fantasy II's at its memory's base + FfRegion.ff2BatHp, one every
 * 0x1F8 bytes (seen in all three releases) */
#define FF1_BLK_HP 0x100
#define FF2_BAT_STRIDE 0x1F8
#define FF_V_MAP 0xF0000084 /* Final Fantasy's map number */
#define FF_MAP_W 128 /* the map screen's map, at its left, below the help window */
#define FF2_WORLD 0x1192 /* Final Fantasy II: the map's tile set (past its base) */
#define FF2_WORLD_SET 7 /* (the world map's) */
/* START's menu, faded in: its windows' frames (x, y, 0xRRGGBB) */
/* (Final Fantasy: the command window's lower frame, in every release, and
 * the party window's left edge) */
static const int ffMenu1[][3] = { { 185, 69, 0xFFFFFF }, { 185, 71, 0x212121 }, { 185, 72, 0xFFFFFF },
                                  { 185, 74, 0x212121 }, { 222, 71, 0x212121 }, { 222, 72, 0xFFFFFF },
                                  { 1, 80, 0xCECECE }, { 2, 80, 0x212121 } };
static const int ffMenu2[][3] = { { 200, 78, 0xCECECE }, { 200, 82, 0x212121 }, { 200, 110, 0xCECECE },
                                  { 200, 114, 0x212121 }, { 144, 90, 0xFFFFFF } };
/* the world map screen (either game): the frame right of the map, top to
 * bottom */
static const int ffMap[][3] = { { 128, 40, 0xFFFFFF }, { 129, 40, 0xCECECE }, { 130, 40, 0x212121 },
                                { 128, 80, 0xFFFFFF }, { 129, 80, 0xCECECE }, { 130, 80, 0x212121 },
                                { 128, 140, 0xFFFFFF }, { 129, 140, 0xCECECE }, { 130, 140, 0x212121 } };
/* the hand at the menu's command list */
static const SpriteRule ffMenuHand = { 136, -16, 200, 100, 0, 1023, -1, 0, 0, 1u << 1 };
/* Final Fantasy's Enlarged Map, over the whole screen: every sprite but the
 * 8x8 marks on the map (the party's, the vehicles'): the hands and button
 * icons (16x16), the command list's scroll arrows (8x8, priority 0) */
static const SpriteRule ffEnlHide[] = { { -64, -64, 511, 255, 0, 1023, -1, 0, 0, ~1u },
                                        { -64, -64, 511, 255, 0, 1023, 0, 0, 0, 0 } };
/* the marker's place on the second screen: its middle at the screen's */
#define FF_ENL_X (GBA_W / 2 - 4)
#define FF_ENL_Y (GBA_H / 2 - 4)
#define FF_CMD_ROWS 72 /* START's menu: the command window's rows (FF-12) */
#define FF2_CMD_ROWS 80 /* (Final Fantasy II's) */
#define FF_PARTY_EVERY 4 /* on the world map, the party kept every 4th capture */
/* the party kept for battles (FF-12), and which party it is: the
 * composer's own (in the web player it runs apart from the game, so the
 * live side never looks at these) */
static uint32_t ffParty[GBA_W * GBA_H];
static bool ffPartyOk;
static uint32_t ffPartyId;
static int ffWorldCaptures;
/* the Final Fantasy II party whose map has opened (FF-11): the composer's */
static uint32_t ffMapParty;

static const FfRegion* ff_r(Emu* e) {
	for (size_t i = 0; i < sizeof(ffRegions) / sizeof(*ffRegions); ++i) {
		if (!strncmp(e->gameCode, ffRegions[i].code, 4)) return &ffRegions[i];
	}
	return &ffRegions[0];
}

static bool ff_ram(uint32_t a) {
	return a >= 0x02000000 && a < 0x02040000;
}

/* 1: Final Fantasy, 2: Final Fantasy II, 0: neither (the cartridge's menus) */
static int ff_game(Emu* e) {
	const FfRegion* r = ff_r(e);
	if (ff_ram(emu_read32(e, r->game1))) return 1;
	if (ff_ram(emu_read32(e, r->game2))) return 2;
	return 0;
}

/* Final Fantasy's battle block (FF-13): kept from one look to the next,
 * looked for again when it no longer holds its own address. (The live
 * side's own: the composer never looks at it) */
static uint32_t ff1_block(Emu* e) {
	static uint32_t blk;
	const FfRegion* r = ff_r(e);
	if (blk && emu_read32(e, blk + r->blkSelf) == blk) return blk;
	blk = 0;
	for (uint32_t a = 0x02000000; a + r->blkSelf + 4 <= 0x02040000; a += 4) {
		if (emu_read32(e, a + r->blkSelf) != a) continue;
		bool ok = true;
		for (int c = 0; c < 4 && ok; ++c) ok = emu_read16(e, a + FF1_BLK_HP + 2 + c * r->blkStride) != 0; /* (max HP) */
		if (ok) return blk = a;
	}
	return 0;
}

/* the whole party down in a battle: every HP 0 (FF-13) */
static bool ff_defeated(Emu* e, int game) {
	if (emu_read8(e, FF_BATTLE) != 1) return false;
	uint32_t a, stride;
	if (game == 2) {
		uint32_t base = emu_read32(e, ff_r(e)->game2);
		a = base + ff_r(e)->ff2BatHp, stride = FF2_BAT_STRIDE;
	} else {
		if (!(a = ff1_block(e))) return false;
		a += FF1_BLK_HP, stride = ff_r(e)->blkStride;
	}
	for (int c = 0; c < 4; ++c) {
		if (emu_read16(e, a + c * stride)) return false;
	}
	return true;
}

static uint32_t ff_virt(Emu* e, uint32_t addr) {
	if (addr == FF_V_LOGO) {
		/* outside the games: the logo (FF-2); the party defeated: the
		 * logo (FF-13); other battles: captures, the composer shows the
		 * party kept (FF-8) */
		int g = ff_game(e);
		return !g || ff_defeated(e, g);
	}
	/* Final Fantasy's map number, Final Fantasy II's tile set (7 on the world map)
	 * (FF-5, FF-10) */
	if (addr == FF_V_MAP) {
		int g = ff_game(e);
		if (g == 1) return emu_read8(e, emu_read32(e, ff_r(e)->game1));
		if (g == 2) return emu_read8(e, emu_read32(e, ff_r(e)->game2) + FF2_WORLD);
		return 0xFF;
	}
	return 0;
}

static bool ff_is(uint32_t px, int rgb) {
	int d[3] = { (int) (px & 0xFF) - (rgb >> 16 & 0xFF), (int) (px >> 8 & 0xFF) - (rgb >> 8 & 0xFF),
	             (int) (px >> 16 & 0xFF) - (rgb & 0xFF) };
	for (int i = 0; i < 3; ++i) {
		if (d[i] > 16 || d[i] < -16) return false;
	}
	return true;
}

static bool ff_probes(Emu* s, const int (*p)[3], int n) {
	for (int i = 0; i < n; ++i) {
		if (!ff_is(s->fb[p[i][1] * GBA_W + p[i][0]], p[i][2])) return false;
	}
	return true;
}

static bool ff_map_up(Emu* s) {
	return ff_probes(s, ffMap, sizeof(ffMap) / sizeof(*ffMap));
}

/* START's menu up: its windows' frames where they are */
static bool ff_menu_up(Emu* s, int game) {
	const int (*p)[3] = game == 2 ? ffMenu2 : ffMenu1;
	int n = game == 2 ? (int) (sizeof(ffMenu2) / sizeof(*ffMenu2)) : (int) (sizeof(ffMenu1) / sizeof(*ffMenu1));
	for (int i = 0; i < n; ++i) {
		if (!ff_is(s->fb[p[i][1] * GBA_W + p[i][0]], p[i][2])) return false;
	}
	return true;
}

/* runs until the picture has been still for 3 looks 4 frames apart (at most
 * n frames); false if it never was */
static bool ff_settle(Emu* s, int n) {
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < n && still < 3; f += 4) {
		aw_run(s, 0, 4);
		still = !memcmp(last, s->fb, sizeof(last)) ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	return still >= 3;
}

/* as ff_settle, looking only right of the map's parchment */
static bool ff_settle_right(Emu* s, int n) {
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < n && still < 3; f += 4) {
		aw_run(s, 0, 4);
		bool same = true;
		for (int y = 0; y < GBA_H && same; ++y) same = !memcmp(&last[y * GBA_W + FF_MAP_W], &s->fb[y * GBA_W + FF_MAP_W], (GBA_W - FF_MAP_W) * 4);
		still = same ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	return still >= 3;
}

/* a buffer for the composer's states */
static uint8_t* ff_tmp(Emu* s) {
	static uint8_t* tmp;
	static size_t tmpSize;
	size_t need = emu_state_size(s);
	if (need > tmpSize) {
		free(tmp);
		tmp = malloc(need);
		tmpSize = tmp ? need : 0;
	}
	return tmp;
}

/* the frame drawn again with only the given layers, then the state as it
 * was (the layers take effect as the display registers are written) */
static void ff_draw_layers(Emu* s, const uint8_t* tmp, unsigned layers, uint32_t* to) {
	emu_layers(s, layers);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	memcpy(to, s->fb, GBA_W * GBA_H * sizeof(uint32_t));
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp);
}

/* START's menu brought up (from one of its screens, B first, as often as it
 * takes: FF-6) and settled, its hand hidden */
static const char* ff_open_menu(Emu* s, int game) {
	emu_hide_sprites(s, &ffMenuHand, 1);
	bool up = false;
	for (int tries = 0; tries < 3 && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
		}
		aw_run(s, K_START, 4);
		for (int f = 0; f < 90 && !up; f += 2) {
			aw_run(s, 0, 2);
			up = ff_menu_up(s, game);
		}
	}
	if (!up) return "the menu didn't open"; /* (FF-7) */
	if (!ff_settle(s, 120)) return "the menu didn't settle";
	if (!ff_menu_up(s, game)) return "the menu closed";
	if (emu_cancelled(s)) return "called off";
	return NULL;
}

/* which party Final Fantasy has: its characters' names and classes, which
 * a battle doesn't change (FF-12) */
static uint32_t ff_party_id(Emu* s, int game) {
	const FfRegion* r = ff_r(s);
	uint32_t h = 2166136261u ^ (uint32_t) game;
	if (game == 2) {
		uint32_t base = emu_read32(s, r->game2);
		for (int c = 0; c < 4; ++c) {
			for (int i = 0; i < 12; ++i) {
				uint8_t b = emu_read8(s, base + FF2_NAMES + c * FF2_STRIDE + i);
				h = (h ^ b) * 16777619u;
				if (!b) break;
			}
		}
		return h;
	}
	uint32_t base = emu_read32(s, r->game1);
	for (int c = 0; c < 4; ++c) {
		for (int i = 0; i < r->nameLen + 2; ++i) h = (h ^ emu_read8(s, base + FF_NAMES + c * r->stride + i)) * 16777619u;
	}
	return h;
}

/* the party kept for battles from START's menu, up: its top rows without
 * BG1 (the command window), the party window under it (FF-12) */
static bool ff_keep_party(Emu* s, int game) {
	static uint32_t keep[GBA_W * GBA_H], bg0[GBA_W * GBA_H];
	uint8_t* tmp = ff_tmp(s);
	if (!tmp || !emu_state_save(s, tmp)) return false;
	memcpy(keep, s->fb, sizeof(keep));
	ff_draw_layers(s, tmp, 0x11, bg0);
	memcpy(keep, bg0, (game == 2 ? FF2_CMD_ROWS : FF_CMD_ROWS) * GBA_W * sizeof(uint32_t));
	if (emu_cancelled(s)) return false;
	memcpy(ffParty, keep, sizeof(ffParty));
	ffPartyId = ff_party_id(s, game);
	ffPartyOk = true;
	return true;
}

static bool ff_oam(Emu* s, int i, int* x, int* y, int* sz, int* prio) {
	uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a1 = emu_read16(s, 0x07000002 + i * 8);
	uint16_t a2 = emu_read16(s, 0x07000004 + i * 8);
	if ((a0 & 0x0300) == 0x0200) return false; /* (off) */
	*x = a1 & 0x1FF;
	*y = a0 & 0xFF;
	if (*x >= 256) *x -= 512;
	if (*y >= 160) *y -= 256;
	*sz = (a0 >> 14) * 4 + (a1 >> 14);
	*prio = a2 >> 10 & 3;
	return true;
}

/* the 8x8 mark (the party's, a vehicle's: not the scroll arrows, priority
 * 0) nearest (x, y), within r pixels; false if none */
static bool ff_mark_near(Emu* s, int x, int y, int r, int* mx, int* my) {
	int bestD = r * r + 1, sx, sy, sz, prio;
	for (int i = 0; i < 128; ++i) {
		if (!ff_oam(s, i, &sx, &sy, &sz, &prio) || sz != 0 || prio == 0) continue;
		int d = (sx - x) * (sx - x) + (sy - y) * (sy - y);
		if (d < bestD) bestD = d, *mx = sx, *my = sy;
	}
	return bestD <= r * r;
}

/* the party's mark on the Enlarged Map as it opens: the one the map's hand
 * cursor (16x16, priority 1; it opens on the party) points at, 15 pixels
 * right of the hand. (Scrolling moves the marks but not the hand, and the
 * game hands its sprites round as marks scroll in and out of view: the mark
 * is followed by where it is, not by the hand or a sprite slot.) */
static bool ff_party_mark(Emu* s, int game, int* mx, int* my) {
	int x, y, sz, prio;
	for (int i = 0; i < 128; ++i) {
		if (!ff_oam(s, i, &x, &y, &sz, &prio) || sz != 1) continue;
		/* (Final Fantasy: the map's hand at priority 1, the command list's
		 * at 0; Final Fantasy II draws both at 0: the one with a mark at
		 * its tip) */
		if (game == 1 && prio != 1) continue;
		if (ff_mark_near(s, x + 15, y + 1, 6, mx, my)) return true;
		if (game == 1) return false;
	}
	return false;
}

/* the tile the 8x8 mark at (x, y) shows (-1 if none there) */
static int ff_mark_tile(Emu* s, int x, int y) {
	int sx, sy, sz, prio;
	for (int i = 0; i < 128; ++i) {
		if (ff_oam(s, i, &sx, &sy, &sz, &prio) && sz == 0 && prio && sx == x && sy == y) return emu_read16(s, 0x07000004 + i * 8) & 0x3FF;
	}
	return -1;
}

/* the Enlarged Map up on the hidden copy (either game): BG2 reaches right
 * of the Full Map's parchment (the Full Map leaves it empty there) */
static bool ff_enlarged(Emu* s, int game, const uint8_t* tmp) {
	static uint32_t bg2[GBA_W * GBA_H];
	ff_draw_layers(s, tmp, 0x04, bg2);
	int differ = 0;
	/* (Final Fantasy II's Full Map isn't on BG2 at all, and Japan's
	 * Enlarged Map stops at the command window's edge: any picture) */
	int x0 = game == 2 ? 0 : FF_MAP_W + 8;
	for (int y = 40; y < GBA_H; y += 8) {
		for (int x = x0; x < GBA_W; ++x) differ += bg2[y * GBA_W + x] != bg2[40 * GBA_W + GBA_W - 1];
	}
	return differ > 40;
}

/* where the game leaves BG2 empty under its windows (FF-11: Japan's
 * Final Fantasy II): columns gx.. and rows ..gy-1 one colour */
static void ff_gaps(const uint32_t* bg2, int* gx, int* gy) {
	uint32_t c = bg2[GBA_H / 2 * GBA_W + GBA_W - 1];
	*gx = GBA_W;
	while (*gx > GBA_W / 2) {
		bool same = true;
		for (int y = 0; y < GBA_H && same; ++y) same = bg2[y * GBA_W + *gx - 1] == c;
		if (!same) break;
		--*gx;
	}
	c = bg2[0];
	*gy = 0;
	while (*gy < 40) {
		bool same = true;
		for (int x = 0; x < *gx && same; ++x) same = bg2[*gy * GBA_W + x] == c;
		if (!same) break;
		++*gy;
	}
}

/* the map scrolled by (dx, dy) pixels (view right / down), the party's
 * mark followed from (*mx, *my) */
static void ff_scroll(Emu* s, int game, int dx, int dy, int* mx, int* my) {
	int step = game == 2 ? 2 : 1;
	while ((dx || dy) && !emu_cancelled(s)) {
		unsigned keys = 0;
		int ex = *mx, ey = *my;
		if (dx >= step) keys |= K_RIGHT, ex -= step, dx -= step;
		else if (dx <= -step) keys |= K_LEFT, ex += step, dx += step;
		else dx = 0;
		if (dy >= step) keys |= K_DOWN, ey -= step, dy -= step;
		else if (dy <= -step) keys |= K_UP, ey += step, dy += step;
		else dy = 0;
		if (!keys) break;
		aw_run(s, K_R | keys, 1);
		if (game == 2) aw_run(s, K_R, 1);
		if (!ff_mark_near(s, ex, ey, 3, mx, my)) *mx = ex, *my = ey;
	}
	aw_run(s, 0, 4);
	ff_mark_near(s, *mx, *my, 3, mx, my);
}

/* the map with its marks (BG2 and the marks alone): Final Fantasy's where
 * the party's is largest (it pulses); Final Fantasy II's at their step
 * with a one-pixel black outline (they blink: FF-11) */
static void ff_snap(Emu* s, int game, uint8_t* tmp, const uint32_t* bare, bool found, int mx, int my, uint32_t* pic) {
	emu_hide_sprites(s, ffEnlHide, 2);
	emu_layers(s, 0x14);
	emu_state_load(s, tmp);
	int best = -1;
	for (int k = 0; k < (game == 2 ? 40 : 24); ++k) {
		emu_frame(s, 0);
		int n = 0;
		if (game == 2) n = found && ff_mark_tile(s, mx, my) == FF2_MARK_TILE;
		else for (int i = 0; i < GBA_W * GBA_H; ++i) n += s->fb[i] != bare[i];
		if (n > best) {
			best = n;
			memcpy(pic, s->fb, GBA_W * GBA_H * sizeof(uint32_t));
		}
		if (game == 2 && n) break;
	}
	emu_layers(s, 0x1F);
	emu_hide_sprites(s, NULL, 0);
	emu_state_load(s, tmp);
}

/* the world map: the Enlarged Map over the whole screen, the party's
 * marker in the middle (FF-4, FF-11); the map is up and settled */
static const char* ff_world(Emu* s, int game, uint32_t* out, int w, int h) {
	static uint32_t bare[GBA_W * GBA_H], pic[GBA_W * GBA_H];
	uint8_t* tmp = ff_tmp(s);
	if (!tmp || !emu_state_save(s, tmp)) return "state save failed";
	/* the Enlarged Map (the game opens whichever was used last) */
	for (int tries = 0; tries < 2 && !ff_enlarged(s, game, tmp); ++tries) {
		if (tries) aw_run(s, K_UP, 4); /* (its command cursor elsewhere) */
		aw_run(s, K_A, 4);
		aw_run(s, 0, 30);
		if (!ff_settle_right(s, 120)) return "the map didn't settle";
		if (!emu_state_save(s, tmp)) return "state save failed";
	}
	if (!ff_enlarged(s, game, tmp)) return "no Enlarged Map";
	if (emu_cancelled(s)) return "called off";
	/* the marker to the middle, as a player scrolls it (R + pad) */
	int mx, my;
	bool found = ff_party_mark(s, game, &mx, &my);
	/* (Final Fantasy scrolls a pixel a frame; Final Fantasy II two, a
	 * frame after the press: it's let settle before each step) */
	int step = game == 2 ? 2 : 1;
	for (int f = 0; f < 240 && found; ++f) {
		unsigned keys = 0;
		int ex = mx, ey = my; /* (where it should be next) */
		if (mx < FF_ENL_X - step) keys |= K_LEFT, ex += step;
		if (mx > FF_ENL_X + step) keys |= K_RIGHT, ex -= step;
		if (my < FF_ENL_Y - step) keys |= K_UP, ey += step;
		if (my > FF_ENL_Y + step) keys |= K_DOWN, ey -= step;
		if (!keys) break;
		aw_run(s, K_R | keys, 1);
		if (game == 2) aw_run(s, K_R, 1);
		if (!ff_mark_near(s, ex, ey, 3, &mx, &my)) mx = ex, my = ey;
	}
	aw_run(s, 0, 4);
	if (emu_cancelled(s)) return "called off";
	if (!emu_state_save(s, tmp)) return "state save failed";
	/* BG2 alone, then with the marks */
	ff_draw_layers(s, tmp, 0x04, bare);
	ff_snap(s, game, tmp, bare, found, mx, my, pic);
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? pic[y * GBA_W + x] : 0xFF000000;
	}
	/* where the game leaves BG2 empty under its windows (Japan's Final
	 * Fantasy II: right of x 128, above y 24), the map scrolled there and
	 * taken again, up to three times, each placed by how far the party's
	 * mark moved (FF-11) */
	int gx, gy;
	ff_gaps(bare, &gx, &gy);
	if (found && (gx < GBA_W || gy > 0)) {
		static uint32_t more[GBA_W * GBA_H];
		static bool done[GBA_W * GBA_H];
		for (int y = 0; y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) done[y * GBA_W + x] = x < gx && y >= gy;
		}
		int sx = gx < GBA_W ? (GBA_W - gx + 1) & ~1 : 0, sy = gy > 0 ? -((gy + 1) & ~1) : 0;
		const int at[3][2] = { { sx, 0 }, { sx, sy }, { 0, sy } };
		int cx = 0, cy = 0, kx = mx, ky = my;
		for (int k = 0; k < 3; ++k) {
			if ((!at[k][0] && !at[k][1]) || (k == 1 && (!sx || !sy))) continue;
			ff_scroll(s, game, at[k][0] - cx, at[k][1] - cy, &kx, &ky);
			cx = at[k][0], cy = at[k][1];
			if (emu_cancelled(s)) return "called off";
			if (!emu_state_save(s, tmp)) return "state save failed";
			ff_snap(s, game, tmp, bare, true, kx, ky, more);
			int ox = mx - kx, oy = my - ky; /* (how far the view moved) */
			for (int y = 0; y < GBA_H && y < h; ++y) {
				for (int x = 0; x < GBA_W && x < w; ++x) {
					int qx = x - ox, qy = y - oy;
					/* (not a scrolled picture's first 8 columns: a mark half off
					 * its edge is cut there; another picture has them) */
					if (done[y * GBA_W + x] || qx < (ox > 0 ? 8 : 0) || qx >= gx || qy < gy || qy >= GBA_H) continue;
					out[y * w + x] = more[qy * GBA_W + qx];
					done[y * GBA_W + x] = true;
				}
			}
		}
	}
	/* the party kept for battles, now and then (FF-12): the map closed,
	 * START's menu */
	if (!ffPartyOk || ffPartyId != ff_party_id(s, game) || ++ffWorldCaptures >= FF_PARTY_EVERY) {
		ffWorldCaptures = 0;
		aw_run(s, K_B, 4);
		aw_run(s, 0, 40);
		if (!ff_open_menu(s, game)) ff_keep_party(s, game);
	}
	return NULL;
}

static const char* ff_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	int game = ff_game(s);
	if (!game) return "not in a game"; /* (FF-2) */
	if (emu_read8(s, FF_BATTLE) == 1) {
		/* the party kept, if it's this party (FF-8, FF-12); else the
		 * last picture */
		if (!ffPartyOk || ffPartyId != ff_party_id(s, game)) return "a battle: no picture of this party kept";
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? ffParty[y * GBA_W + x] : 0xFF000000;
		}
		return NULL;
	}
	/* back to walking about from the menu or the map (FF-6) */
	for (int i = 0; i < 4; ++i) {
		bool map = ff_map_up(s);
		if (!map && !ff_menu_up(s, game)) break;
		aw_run(s, K_B, 4);
		aw_run(s, 0, 40);
	}
	if (emu_cancelled(s)) return "called off";

	/* the world map (FF-4, FF-11): hold B, then SELECT */
	if (ff_virt(s, FF_V_MAP) == (game == 1 ? 0u : (uint32_t) FF2_WORLD_SET)) {
		/* (a press mid-step can go unseen: tried again, up to three
		 * times) */
		bool map = false;
		for (int tries = 0; tries < 3 && !map; ++tries) {
			aw_run(s, K_B, 10);
			aw_run(s, K_B | K_SELECT, 6);
			for (int f = 0; f < 40 && !map; f += 2) {
				aw_run(s, 0, 2);
				map = ff_map_up(s);
			}
		}
		/* once the map has opened, the world map never shows the menu:
		 * the last picture stays instead (FF-4, FF-11). Final Fantasy II
		 * has no map early on: until it has opened for this party, the
		 * party screen (FF-11) */
		if (!map && (game == 1 || ffMapParty == ff_party_id(s, game))) return "the map didn't open";
		if (map) {
			if (game == 2) ffMapParty = ff_party_id(s, game);
			if (!ff_settle_right(s, 120)) return "the map didn't settle";
			if (emu_cancelled(s)) return "called off";
			return ff_world(s, game, out, w, h);
		}
		if (emu_cancelled(s)) return "called off";
	}

	/* START's menu (FF-5, FF-10) */
	const char* err = ff_open_menu(s, game);
	if (err) return err;
	/* the party screen, without the command window (FF-5, FF-10, FF-12) */
	if (!ff_keep_party(s, game)) return "called off";
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? ffParty[y * GBA_W + x] : 0xFF000000;
	}
	return NULL;
}

/* ---------------------------------------------------------------- Final Fantasy IV Advance
 *
 * docs/games/final-fantasy-iv-advance.md. The overworld's map is the one
 * Sight opens, made on the hidden copy with Rosa put in the party (FF4-8,
 * ff4_map); elsewhere the second screen is START's menu as a party screen (the party, the place,
 * the play time and gil), taken on the hidden copy without its command
 * list (FF4-3); in a battle, scenes and conversations the last one stays
 * (FF4-4, FF4-5); the logo on the title (FF4-2). All three releases (BZ4E,
 * BZ4P, BZ4J in both revisions) draw the same windows in the same places.
 *   FF4_V_LOGO  1 while the title is up: the title is video mode 1, the
 *               game's other screens modes 0 and 2
 */
#define FF4_V_LOGO 0xF0000090
/* START's menu, faded in: the command window's lower frame and the party
 * window's right one (x, y, 0xRRGGBB); the Load Game screen has the same
 * windows, told apart by its title window's lower frame at ff4LoadFrame */
static const int ff4Menu[][3] = { { 200, 85, 0xFFFFFF }, { 200, 86, 0xCECECE }, { 200, 87, 0x212121 },
                                  { 200, 88, 0xFFFFFF }, { 200, 90, 0x212121 }, { 152, 40, 0xFFFFFF },
                                  { 153, 40, 0xCECECE }, { 154, 40, 0x212121 } };
static const int ff4LoadFrame[2] = { 225, 21 };
/* a battle: its bottom windows' frames */
static const int ff4Battle[][3] = { { 100, 112, 0xFFFFFF }, { 100, 113, 0xCECECE }, { 100, 114, 0x212121 },
                                    { 200, 112, 0xFFFFFF }, { 200, 113, 0xCECECE }, { 200, 114, 0x212121 } };
/* the hand at the menu's command list */
static const SpriteRule ff4MenuHand = { 136, -16, 200, 100, 0, 1023, -1, 0, 0, 0 };

static uint32_t ff4_virt(Emu* e, uint32_t addr) {
	if (addr == FF4_V_LOGO) return (emu_io16(e, 0) & 7) == 1; /* (FF4-2) */
	return 0;
}

static bool ff4_menu_up(Emu* s) {
	if (!ff_probes(s, ff4Menu, sizeof(ff4Menu) / sizeof(*ff4Menu))) return false;
	return !ff_is(s->fb[ff4LoadFrame[1] * GBA_W + ff4LoadFrame[0]], 0xFFFFFF);
}

/* START's menu brought up on the hidden copy (B first, as often as it
 * takes to leave one of its screens, a dialogue: FF4-5) and settled, its
 * hand hidden */
static const char* ff4_open_menu(Emu* s) {
	emu_hide_sprites(s, &ff4MenuHand, 1);
	/* (already up, as in a state saved with the menu open: START would close it) */
	bool up = ff4_menu_up(s);
	for (int tries = 0; tries < 4 && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
		}
		aw_run(s, K_START, 4);
		for (int f = 0; f < 90 && !up; f += 2) {
			aw_run(s, 0, 2);
			up = ff4_menu_up(s);
		}
	}
	if (!up) return "the menu didn't open"; /* (FF4-5) */
	if (!ff_settle(s, 120)) return "the menu didn't settle";
	if (!ff4_menu_up(s)) return "the menu closed";
	if (emu_cancelled(s)) return "called off";
	return NULL;
}

/* Final Fantasy IV Advance in Europe: the language screen first (A:
 * English), then the health warning, both logos and the intro to skip with
 * START (FF4-1) */
static void ff4_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 200 }, { K_A, 4 }, { 0, 146 }, { K_START, 4 }, { 0, 146 }, { K_START, 4 }, { 0, 146 },
	                                   { K_START, 4 }, { 0, 146 }, { K_START, 4 }, { 0, 400 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

/* the "Switch Maps" screen Sight opens: its text window's lower frame at the
 * top left and the list window's left frame (x, y, 0xRRGGBB) */
static const int ff4MapUp[][3] = { { 128, 80, 0xFFFFFF }, { 129, 80, 0xCECECE }, { 130, 80, 0x212121 },
                                   { 60, 21, 0xFFFFFF }, { 60, 22, 0xCECECE }, { 60, 23, 0x212121 } };
/* its hand and its A / B prompts, right of the map (the map is 128 px wide) */
static const SpriteRule ff4MapUi = { 128, -16, 240, 160, 0, 1023, -1, 0, 0, 0 };
#define FF4_MAP_Y 27 /* the map's top row; it is 128 x 128 at x 0 */

/* the world map from Sight, on the hidden copy (FF4-8): Rosa first in the
 * party at level 99 with 800 MP (the party order table, and her record 0x12
 * after it: level +11, MP +4, maximum MP +6; the table's place depends on
 * the release), then START, Magic, Sight. Fills pic's 128 x 128 map at its
 * top left; NULL when the screen came up, else why not */
static const char* ff4_map(Emu* s, uint32_t* pic) {
	char region = (char) emu_read8(s, 0x080000AF);
	uint32_t table = region == 'E' ? 0x020069E6 : region == 'P' ? 0x02006066 : emu_read8(s, 0x080000BC) ? 0x0200634E : 0x020069F6;
	/* (the menu is up already: it reads the party when a character is picked) */
	emu_write8(s, table, 2);
	emu_write8(s, table + 0x12 + 0x48 * 2 + 11, 99);
	emu_write16(s, table + 0x12 + 0x48 * 2 + 4, 800);
	emu_write16(s, table + 0x12 + 0x48 * 2 + 6, 800);
	/* her white magic list in the order the game starts with (Cure, Hold,
	 * Scan, Slow, Sight), so that Sight is where the keys below land
	 * whatever order this save has it in: a save that never had Rosa set up
	 * (the hacked two-character one) lists the spells by number, with Sight
	 * near the end. The lists (24 bytes each, from table + 0x3DE) are set
	 * for every caster, as the one that is Rosa's isn't worked out */
	static const uint8_t sightFirst[5] = { 0x0E, 0x01, 0x0D, 0x07, 0x17 };
	for (int list = 0; list < 6; ++list) {
		for (int i = 0; i < 5; ++i) emu_write8(s, table + 0x3DE + 0x18 * list + i, sightFirst[i]);
	}
	emu_hide_sprites(s, &ff4MapUi, 1);
	static const struct { int key, wait; } path[] = { { K_DOWN, 16 }, { K_A, 26 }, { K_A, 26 }, { K_A, 26 }, { K_DOWN, 12 },
	                                                 { K_RIGHT, 12 }, { K_A, 26 }, { K_A, 0 } };
	for (size_t i = 0; i < sizeof(path) / sizeof(*path); ++i) {
		aw_run(s, path[i].key, 8);
		if (path[i].wait) aw_run(s, 0, path[i].wait);
	}
	bool up = false;
	for (int f = 0; f < 150 && !up; f += 5) {
		aw_run(s, 0, 5);
		up = ff_probes(s, ff4MapUp, sizeof(ff4MapUp) / sizeof(*ff4MapUp));
	}
	if (emu_cancelled(s)) return "called off";
	if (!up) return "no map"; /* Sight can't be cast here */
	aw_run(s, 0, 20);
	if (!ff_probes(s, ff4MapUp, sizeof(ff4MapUp) / sizeof(*ff4MapUp))) return "the map closed";
	uint8_t* tmp = ff_tmp(s);
	if (!tmp || !emu_state_save(s, tmp)) return "called off";
	static uint32_t lay[GBA_W * GBA_H];
	ff_draw_layers(s, tmp, 0x14, lay);
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < 128; ++y) memcpy(&pic[y * 128], &lay[(y + FF4_MAP_Y) * GBA_W], 128 * sizeof(uint32_t));
	return NULL;
}

/* the party screen last taken: shown in a battle (FF4-4) */
static uint32_t ff4Party[GBA_W * GBA_H];
static bool ff4HaveParty;

static void ff4_put_party(uint32_t* out, int w, int h, const uint32_t* pic) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? pic[y * GBA_W + x] : 0xFF000000;
	}
}

static const char* ff4_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	static uint32_t pic[GBA_W * GBA_H];
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	/* a battle: the party from before it (FF4-4) */
	if (ff_probes(s, ff4Battle, sizeof(ff4Battle) / sizeof(*ff4Battle))) {
		if (!ff4HaveParty) return "a battle";
		ff4_put_party(out, w, h, ff4Party);
		return NULL;
	}
	const char* err = ff4_open_menu(s);
	if (err) return err;
	/* the party screen: the menu without the command list (BG1) (FF4-3),
	 * kept for the battles */
	uint8_t* tmp = ff_tmp(s);
	if (!tmp || !emu_state_save(s, tmp)) return "called off";
	ff_draw_layers(s, tmp, 0x11, pic);
	if (emu_cancelled(s)) return "called off";
	memcpy(ff4Party, pic, sizeof(ff4Party));
	ff4HaveParty = true;
	/* on the overworld, the map instead (FF4-8), from this same menu */
	static uint32_t map[128 * 128];
	if (!ff4_map(s, map)) {
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				int mx = x - (w - 128) / 2, my = y - (h - 128) / 2;
				out[y * w + x] = mx >= 0 && mx < 128 && my >= 0 && my < 128 ? (map[my * 128 + mx] | 0xFF000000) : 0xFF000000;
			}
		}
		return NULL;
	}
	if (emu_cancelled(s)) return "called off";
	ff4_put_party(out, w, h, pic);
	return NULL;
}

/* ---------------------------------------------------------------- Final Fantasy V Advance
 *
 * docs/games/final-fantasy-v-advance.md. The game has no map screen: the
 * second screen is the party screen, START's menu opened on the hidden copy
 * without its command windows (FV-3, FV-4), kept for battles (FV-6, FV-7),
 * the logo outside the game and after a game over (FV-2, FV-8). START's menu
 * draws every window on BG0; the command windows (the top right) are left
 * black in the picture. Words, in all three releases:
 *   FFV_LOADED  0x02000924: the first character's pointer, 0 until a game
 *               is loaded or started (title, Load Game screen)
 *   FFV_BATTLE  0x02000EC0: non-zero from a battle's first frame to the end
 *               of its results
 *   FFV_SCREEN  (per release) what the game shows: 5 walking about, 7 START's
 *               menu, 0x0C the world map (R opens it), 0x0A a battle
 * Per release (FfvRegion): the party's records (character id, job at +8, +9,
 * one every 0x64 bytes) and the battle's block of HP (HP, maximum HP, MP,
 * maximum MP, 8 bytes a character). */
typedef struct {
	const char* code;
	uint32_t party, hp, screen, room;
} FfvRegion;
static const FfvRegion ffvRegions[] = {
	{ "BZ5E", 0x0200DAB4, 0x0201E1A0, 0x020096E0, 0x0200E2AE },
	{ "BZ5P", 0x0200D868, 0x0201DF54, 0x02009464, 0x0200E062 },
	{ "BZ5J", 0x0200DAB4, 0x0201E1A0, 0x020096E0, 0x0200E2AE },
};
#define FFV_SCREEN_MAP 0x0C /* the byte at `screen`: 5 walking about, 7 START's menu, 0x0C the world map, 0x0A a battle */
#define FFV_LOADED 0x02000924
#define FFV_BATTLE 0x02000EC0
#define FFV_V_LOGO 0xF00000A0  /* outside the game (FV-2), or the party defeated (FV-8) */
#define FFV_V_MAP 0xF00000A4   /* the map (FV-3) */
#define FFV_V_BATTLE 0xF00000A8 /* a battle (FV-6) */
#define FFV_CMD_X 152 /* START's menu: the command windows' place, top right (FV-3) */
#define FFV_CMD_ROWS 88 /* (the rows above the location window) */
#define FFV_WIN_X0 152 /* the commands' place: its left column, and the right edge's (x 236..239) */
#define FFV_WIN_X1 239
/* START's menu, up: its windows' frames (x, y, 0xRRGGBB) */
static const int ffvMenu[][3] = { { 152, 10, 0xFFFFFF }, { 153, 10, 0xCECECE }, { 154, 10, 0x212121 },
                                  { 152, 50, 0xFFFFFF }, { 237, 10, 0xFFFFFF }, { 190, 85, 0xFFFFFF },
                                  { 190, 86, 0xCECECE }, { 152, 100, 0xFFFFFF }, { 237, 140, 0xFFFFFF },
                                  { 1, 80, 0xCECECE }, { 2, 80, 0x212121 } };
/* the hand cursor: tiles 900 and up */
static const SpriteRule ffvHand = { -16, -16, 255, 255, 900, 1023, -1, 0, 0, 0 };
static uint32_t ffvParty[GBA_W * GBA_H];
static uint32_t ffvMap[GBA_W * GBA_H];
static unsigned ffvTakes;
static bool ffvPartyOk;
static uint32_t ffvPartyId;

static const FfvRegion* ffv_r(Emu* e) {
	for (size_t i = 0; i < sizeof(ffvRegions) / sizeof(*ffvRegions); ++i) {
		if (!strncmp(e->gameCode, ffvRegions[i].code, 4)) return &ffvRegions[i];
	}
	return &ffvRegions[0];
}

/* the whole party down in a battle: every HP 0 (FV-8) */
static bool ffv_defeated(Emu* e) {
	if (!emu_read32(e, FFV_BATTLE)) return false;
	for (int c = 0; c < 4; ++c) {
		uint32_t a = ffv_r(e)->hp + c * 8;
		if (emu_read16(e, a) || !emu_read16(e, a + 2)) return false; /* (alive, or no character) */
	}
	return true;
}

static uint32_t ffv_virt(Emu* e, uint32_t addr) {
	if (addr == FFV_V_LOGO) return !emu_read32(e, FFV_LOADED) || ffv_defeated(e);
	if (addr == FFV_V_MAP) return emu_read16(e, ffv_r(e)->room);
	if (addr == FFV_V_BATTLE) return emu_read32(e, FFV_BATTLE) != 0;
	return 0;
}

static bool ffv_menu_up(Emu* s) {
	for (size_t i = 0; i < sizeof(ffvMenu) / sizeof(*ffvMenu); ++i) {
		if (!ff_is(s->fb[ffvMenu[i][1] * GBA_W + ffvMenu[i][0]], ffvMenu[i][2])) return false;
	}
	return true;
}

/* which party: the characters' ids and jobs (FV-7) */
static uint32_t ffv_party_id(Emu* s) {
	uint32_t h = 2166136261u;
	for (int c = 0; c < 4; ++c) {
		uint32_t a = ffv_r(s)->party + c * 0x64 + 8;
		h = (h ^ emu_read8(s, a)) * 16777619u;
		h = (h ^ emu_read8(s, a + 1)) * 16777619u;
	}
	return h;
}

/* START's menu brought up (from one of its screens, B first, as often as it
 * takes: FV-4) and settled, its hand hidden */
static const char* ffv_open_menu(Emu* s) {
	emu_hide_sprites(s, &ffvHand, 1);
	bool up = false;
	for (int tries = 0; tries < 3 && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
		}
		aw_run(s, K_START, 4);
		for (int f = 0; f < 90 && !up; f += 2) {
			aw_run(s, 0, 2);
			up = ffv_menu_up(s);
		}
	}
	if (!up) return "the menu didn't open"; /* (FV-5) */
	if (!ff_settle(s, 120)) return "the menu didn't settle";
	if (!ffv_menu_up(s)) return "the menu closed";
	if (emu_cancelled(s)) return "called off";
	return NULL;
}

static uint8_t ffv_screen(Emu* s) { return emu_read8(s, ffv_r(s)->screen); }

/* the party screen out of START's menu, up: the commands' place isn't a
 * window of its own any more but the big party window going on (FV-10): its
 * top edge carried across from the left, its right edge straight down, and
 * the screen's own gradient inside (no frame between the party and the right) */
static void ffv_party_take(Emu* s) {
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = s->fb[y * GBA_W + x];
			if (x >= FFV_WIN_X0 && x <= FFV_WIN_X1 && y < FFV_CMD_ROWS) {
				bool corner = x > FFV_WIN_X1 - 4; /* the right edge: the game's own, straight down below its corner */
				if (y < 4) c = corner ? c : s->fb[y * GBA_W + FFV_WIN_X0 - 2];
				else c = corner ? s->fb[8 * GBA_W + x] : s->fb[y * GBA_W + 4];
			}
			ffvParty[y * GBA_W + x] = c | 0xFF000000;
		}
	}
	ffvPartyId = ffv_party_id(s);
	ffvPartyOk = true;
}

static void ffv_show(uint32_t* out, int w, int h, const uint32_t* pic) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? pic[y * GBA_W + x] : 0xFF000000;
	}
}

static const char* ffv_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (!emu_read32(s, FFV_LOADED)) return "not in a game"; /* (FV-2) */
	if (emu_read32(s, FFV_BATTLE)) {
		/* the party kept, if it's this party (FV-6, FV-7); else the last picture */
		if (!ffvPartyOk || ffvPartyId != ffv_party_id(s)) return "a battle: no picture of this party kept";
		ffv_show(out, w, h, ffvParty);
		return NULL;
	}
	/* back to walking about from the menu or the map (FV-4) */
	for (int i = 0; i < 4 && (ffv_menu_up(s) || ffv_screen(s) == FFV_SCREEN_MAP); ++i) {
		aw_run(s, K_B, 4);
		aw_run(s, 0, 40);
	}
	if (emu_cancelled(s)) return "called off";
	/* the world map, where R opens it (FV-3): the game's own picture of it */
	bool map = false;
	for (int tries = 0; tries < 2 && ffv_screen(s) != FFV_SCREEN_MAP; ++tries) {
		aw_run(s, 0, 30); /* (the menu or map just closed: the game takes a few frames to listen) */
		aw_run(s, K_R, 4);
		for (int f = 0; f < 60 && ffv_screen(s) != FFV_SCREEN_MAP; f += 2) aw_run(s, 0, 2);
	}
	if (ffv_screen(s) == FFV_SCREEN_MAP) {
		aw_run(s, 0, 90);
		if (ffv_screen(s) == FFV_SCREEN_MAP) {
			for (int i = 0; i < GBA_W * GBA_H; ++i) ffvMap[i] = s->fb[i] | 0xFF000000;
			map = true;
		}
		for (int i = 0; i < 3 && ffv_screen(s) == FFV_SCREEN_MAP; ++i) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
		}
	}
	if (emu_cancelled(s)) return "called off";
	/* the party screen: taken when it's what is shown, and for battles (FV-6)
	 * with the map: for a first capture, another party, and every fourth */
	if (!map || !ffvPartyOk || ffvPartyId != ffv_party_id(s) || ffvTakes % 4 == 0) {
		const char* err = ffv_open_menu(s);
		if (err) {
			if (!map) return err;
		} else {
			ffv_party_take(s);
		}
	}
	++ffvTakes;
	ffv_show(out, w, h, map ? ffvMap : ffvParty);
	return NULL;
}

/* ------------------------------------------------- Final Fantasy VI Advance
 *
 * docs/games/final-fantasy-vi-advance.md. The game keeps the SNES version's
 * memory layout in EWRAM (SNES WRAM address + 0x02000000), the same in all
 * three releases:
 *   0x1600  16 character records of 0x25 bytes: the character's number (0
 *           in the first record in a game), +2 the name (6 bytes), +8 the
 *           level, +9 HP, +0xB maximum HP (16 bits, the top two bits flags)
 *   0x1850  the party each character is in, by number (bits 0-2)
 *   0x82    the map's number (a halfword)
 *   0x02    a halfword: 0xFFFF outside battles, 0 in one
 *   0x3BF4  the party's HP in a battle, four halfwords, one a place; 0x3C1C
 *           their maximum HP (0: nobody there)
 * START's menu is told by the display registers' values it sets (DISPCNT
 * and the first three BG controls). */
#define FF6_ROSTER 0x02001600
#define FF6_REC 0x25
#define FF6_PARTY 0x02001850
#define FF6_MAP 0x02000082
#define FF6_BAT_HP 0x02003BF4
#define FF6_BAT_MAXHP 0x02003C1C
#define FF6_FLAGS 0x020011F6 /* bit 0: the corner minimap switched off (SELECT) */
#define FF6_V_LOGO 0xF0000090 /* outside the game (FF6-2) or the party defeated (FF6-9) */
#define FF6_V_BATTLE 0xF0000091 /* in a battle (FF6-7) */
/* the world map's minimap (FF6-10): sixteen 16x16 sprites, tiles 512 to 590
 * in palette 5, in a 64x64 square at 168, 92; the marker is tile 768 */
#define FF6_MM_X 168
#define FF6_MM_Y 92
#define FF6_MM_SIZE 64
#define FF6_MM_EVERY 4 /* on the world map, the party kept every 4th capture */
/* the command list's tiles on BG0 (FF6-4): columns and rows */
#define FF6_CMD_COL0 19
#define FF6_CMD_COL1 29
#define FF6_CMD_ROW1 9
static const SpriteRule ff6MenuHand = { 136, -16, 200, 100, 0, 1023, -1, 0, 0, 0 };
/* the minimap's sprites, off the game screen (FF6-11): the sixteen (16x16,
 * priority 3), and the marker (16x16, priority 0) */
#define FF6_HIDE_MINIMAP \
	{ FF6_MM_X, FF6_MM_Y, FF6_MM_X + 48, FF6_MM_Y + 48, 512, 591, 3, 1u << 5, 0, 1u << 1 }, \
	{ FF6_MM_X, FF6_MM_Y, FF6_MM_X + 48, FF6_MM_Y + 48, 768, 783, 0, 1u << 5, 0, 1u << 1 }
/* the party kept for battles (FF6-7), the composer's own */
static uint32_t ff6Party[GBA_W * GBA_H];
static bool ff6PartyOk;
static uint32_t ff6PartyId;
static int ff6WorldTakes;

/* in a game: the first character record is character 0, with a level
 * (0 before the title, 0xFF on it) */
static bool ff6_in_game(Emu* e) {
	return emu_read8(e, FF6_ROSTER) == 0 && emu_read8(e, FF6_ROSTER + 8) != 0;
}

/* in a battle: the display registers a battle sets (FF6-7). (The halfword at
 * 0x02000002 is 0 in battles, but stays 0 after the first one.) */
static bool ff6_battle(Emu* e) {
	return ff6_in_game(e) && emu_io16(e, 0x00) == 0x1600 && emu_io16(e, 0x08) == 0x560C &&
	       emu_io16(e, 0x0A) == 0x5408 && emu_io16(e, 0x0C) == 0x5A08;
}

/* the whole party down in a battle (FF6-9): every place that holds a
 * character at 0 HP */
static bool ff6_defeated(Emu* e) {
	if (!ff6_battle(e)) return false;
	bool any = false;
	for (int i = 0; i < 4; ++i) {
		if (!emu_read16(e, FF6_BAT_MAXHP + i * 2)) continue;
		any = true;
		if (emu_read16(e, FF6_BAT_HP + i * 2)) return false;
	}
	return any;
}

static uint32_t ff6_virt(Emu* e, uint32_t addr) {
	if (addr == FF6_V_LOGO) return !ff6_in_game(e) || ff6_defeated(e);
	if (addr == FF6_V_BATTLE) return ff6_battle(e);
	return 0;
}

/* START's menu up: the registers its screen sets */
static bool ff6_menu_up(Emu* s) {
	return emu_io16(s, 0x00) == 0x1540 && emu_io16(s, 0x08) == 0x0F49 && emu_io16(s, 0x0A) == 0x0E4E &&
	       emu_io16(s, 0x0C) == 0x0D43;
}

/* which party: the records' character numbers, names and parties, which a
 * battle doesn't change (FF6-7) */
static uint32_t ff6_party_id(Emu* s) {
	uint32_t h = 2166136261u;
	for (int c = 0; c < 16; ++c) {
		for (int i = 0; i < 8; ++i) h = (h ^ emu_read8(s, FF6_ROSTER + c * FF6_REC + i)) * 16777619u;
		h = (h ^ (emu_read8(s, FF6_PARTY + c) & 7)) * 16777619u;
	}
	return h;
}

/* the minimap's sprites all drawn (FF6-10) */
static bool ff6_minimap_up(Emu* s) {
	int n = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(s, 0x07000000 + i * 8), a1 = emu_read16(s, 0x07000002 + i * 8);
		uint16_t a2 = emu_read16(s, 0x07000004 + i * 8);
		if ((a0 & 0x0300) == 0x0200 || a2 >> 12 != 5 || (a2 & 0x3FF) < 512 || (a2 & 0x3FF) > 590) continue;
		int x = a1 & 0x1FF, y = a0 & 0xFF;
		if (x >= 256) x -= 512;
		if (y >= 160) y -= 256;
		if (x >= FF6_MM_X && x < FF6_MM_X + FF6_MM_SIZE && y >= FF6_MM_Y && y < FF6_MM_Y + FF6_MM_SIZE) ++n;
	}
	return n >= 16;
}

/* on the world maps (FF6-10) */
static bool ff6_world(Emu* s) {
	return emu_read16(s, FF6_MAP) <= 1;
}

/* the party kept for battles, taken on the hidden copy from the live state:
 * START's menu without its command list (FF6-4, FF6-7) */
static const char* ff6_party_take(Emu* s, const void* state) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	/* START's menu brought up; from one of its screens, B first, as often as
	 * it takes (FF6-5) */
	emu_hide_sprites(s, &ff6MenuHand, 1);
	bool up = ff6_menu_up(s);
	for (int tries = 0; tries < 6 && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
			if ((up = ff6_menu_up(s))) break;
		}
		aw_run(s, K_START, 4);
		for (int f = 0; f < 90 && !up; f += 2) {
			aw_run(s, 0, 2);
			up = ff6_menu_up(s);
		}
	}
	if (!up) return "the menu didn't open"; /* (FF6-6) */
	/* settled: still for 3 looks 4 frames apart */
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < 120 && still < 3; f += 4) {
		aw_run(s, 0, 4);
		still = !memcmp(last, s->fb, sizeof(last)) ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	if (still < 3) return "the menu didn't settle";
	if (!ff6_menu_up(s)) return "the menu closed";
	if (emu_cancelled(s)) return "called off";
	/* the command list's tiles cleared on the hidden copy (FF6-4) */
	uint32_t map = 0x06000000 + ((emu_io16(s, 0x08) >> 8) & 31) * 0x800;
	for (int r = 0; r <= FF6_CMD_ROW1; ++r) {
		for (int c = FF6_CMD_COL0; c <= FF6_CMD_COL1; ++c) emu_write16(s, map + (r * 32 + c) * 2, 0);
	}
	emu_frame(s, 0);
	if (emu_cancelled(s)) return "called off";
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) ff6Party[y * GBA_W + x] = s->fb[y * GBA_W + x];
	}
	ff6PartyId = ff6_party_id(s);
	ff6PartyOk = true;
	return NULL;
}



/* the world map's minimap, twice as big, in the middle of the second screen
 * on black (FF6-10): the game's sprites alone, with START's menu or a screen
 * closed first, and the corner minimap switched on if the player has it off */
static const char* ff6_minimap_take(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x10); /* (sprites alone) */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	bool up = ff6_minimap_up(s);
	for (int tries = 0; tries < 6 && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
			if ((up = ff6_minimap_up(s))) break;
		}
		if (emu_read8(s, FF6_FLAGS) & 1) {
			aw_run(s, K_SELECT, 4);
			aw_run(s, 0, 20);
			up = ff6_minimap_up(s);
		}
		if (emu_cancelled(s)) return "called off";
	}
	if (!up) return "no minimap";
	aw_run(s, 0, 2);
	if (!ff6_minimap_up(s)) return "the minimap closed";
	if (emu_cancelled(s)) return "called off";
	int ox = (w - 2 * FF6_MM_SIZE) / 2, oy = (h - 2 * FF6_MM_SIZE) / 2;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			int sx = (x - ox) / 2 + FF6_MM_X, sy = (y - oy) / 2 + FF6_MM_Y;
			bool in = x >= ox && y >= oy && x < ox + 2 * FF6_MM_SIZE && y < oy + 2 * FF6_MM_SIZE;
			out[y * w + x] = in ? s->fb[sy * GBA_W + sx] : 0xFF000000;
		}
	}
	return NULL;
}

static void ff6_party_out(uint32_t* out, int w, int h) {
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? ff6Party[y * GBA_W + x] : 0xFF000000;
	}
}

static const char* ff6_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (!ff6_in_game(s)) {
		ff6PartyOk = false;
		return "not in a game"; /* (FF6-2) */
	}
	if (ff6_battle(s)) {
		/* the party kept, if it's this party (FF6-7); else the last picture */
		if (!ff6PartyOk || ff6PartyId != ff6_party_id(s)) return "a battle: no picture of this party kept";
		ff6_party_out(out, w, h);
		return NULL;
	}
	if (ff6_world(s)) {
		/* the minimap (FF6-10), and now and then the party kept for
		 * battles (FF6-7); no minimap: the party screen */
		bool due = !ff6PartyOk || ff6PartyId != ff6_party_id(s) || ++ff6WorldTakes >= FF6_MM_EVERY;
		const char* err = ff6_minimap_take(s, state, out, w, h);
		if (!err) {
			if (due) {
				ff6WorldTakes = 0;
				ff6_party_take(s, state); /* (a failure keeps the one before) */
			}
			return emu_cancelled(s) ? "called off" : NULL;
		}
		if (emu_cancelled(s)) return "called off";
	}
	const char* perr = ff6_party_take(s, state);
	if (perr) return perr;
	ff6_party_out(out, w, h);
	return NULL;
}
/* ---------------------------------------------------------------- Baldur's Gate: Dark Alliance
 *
 * docs/games/baldurs-gate-dark-alliance.md. The game has no map: GBA DS
 * makes one of each area (BGD-10, an exception), shown by bgd_live_view
 * at a quarter of its size around the hero, a red X where he is (BGD-11); the logo on the
 * title (BGD-5) and at GAME OVER (BGD-9). (Until Nov 13, 2026 START's
 * character tab instead, BGD-1 to BGD-4 and BGD-8, retired.) Both releases
 * use these addresses:
 *   BGD_DISPCNT the display: mode 0 in play (walking, fights, conversations,
 *               a save book: BG0-BG2 hold the area), 3 on the title, the
 *               language list, the class choice, the abilities screen and
 *               START's menu, 4 in black scenes and GAME OVER
 *   BGD_MENU    0x40 while START's menu is up: the title is mode 3 without it.
 *               (0x030077D8, 1 in a game started new, stays 0x14 all
 *               through one continued from a save)
 *   BGD_CONTROL 1 walking about (or in a conversation), 2 in a fight (or
 *               a scene in the world, or dying), 0 from START on, a
 *               black-screen scene, GAME OVER
 *   BGD_HP      health (0 from the hero's death on)
 *   BGD_HEROX/Y where the hero's feet are in the area (1/256 px): the follow
 *               routine keeps the camera 120 px left of and 100 px above
 *               them (his feet at 120, 100 on the game screen)
 *   BGD_CAMX/Y  the camera (words, pixels): the screen's top left in the area
 *   BGD_DX/DY   the camera's pending move (halves): each frame the game's
 *               scroll (0x086A1600) adds them to the camera, draws the 16-px
 *               strips of each layer newly shown (one a frame: 16 px a step
 *               at most) and clears them. The follow routine (0x08690E60)
 *               sets them from the hero, with the three stores in bgdFollow;
 *               another pushes the camera back toward him once he's off
 *               screen, after the scroll
 *   BGD_LEVEL   the area's header: per layer a rectangle in 16-px blocks at
 *               header + 2 + 4 * the half at header + 0x10 + 2 * layer. The
 *               camera goes from its top left to a pixel short of its size
 *               less the screen's (exactly there, the scroll stops for good)
 * A copy of the game (its own Emu, on its own thread; inline in exact runs
 * and where there are no threads) is given the live game's state once the
 * hero has been in a new area under his control for half a second, the
 * follow routine's stores blanked in it (its own copy of the ROM: mGBA
 * copies a core's ROM before its first write), and its camera moved over
 * the area a screen at a time (the hero's health kept as it was, so that
 * nothing kills him on the way); at each stop BG0-BG2 alone (floors and
 * walls, furniture and wall pieces in front, lamps, upper edges and raised
 * floors; no sprites) go into the area's picture where the camera is
 * (tools/scout/bgdpan.c, the experiment). */
#define BGD_DISPCNT 0x04000000
#define BGD_MENU 0x03004161
#define BGD_CONTROL 0x03002C02
#define BGD_HP 0x03000B54
#define BGD_HEROX 0x03001520   /* where his feet are (words, 1/256 px; the camera's target) */
#define BGD_HEROY 0x03001524
#define BGD_CAMX 0x030017D0
#define BGD_CAMY 0x030016B0
#define BGD_DX 0x03003538
#define BGD_DY 0x03002BF0
#define BGD_LEVEL 0x030035B4
#define BGD_V_LOGO 0xF0000090  /* not in a game (BGD-5), or GAME OVER (BGD-9) */
#define BGD_V_NEVER 0xF0000091 /* (no captures: the map is drawn live) */
#define BGD_SETTLE 30          /* frames under control in an area before it's mapped */
#define BGD_MAX 8192           /* the largest area mapped, each way (pixels) */
#define BGD_Z 4                /* the map's scale: a quarter (BGD-10) */
#define BGD_SLOT 0x0844E0CA    /* the empty quick slot: 16x16, 4 bpp (BGD-11) */
#define BGD_SLOT_PAL 0x0860900C /* its palette; the X is entry 2 */
static const uint32_t bgdFollow[] = { 0x08690FD2, 0x0869108A, 0x0869109C };

/* the title, the language list, the class choice, the abilities screen
 * (BGD-5): the display in mode 3 without START's menu */
static bool bgd_title(Emu* e) {
	return (emu_read16(e, BGD_DISPCNT) & 7) == 3 && emu_read8(e, BGD_MENU) != 0x40;
}

/* the game showing the area itself (BG0-BG2 hold it): mode 0 */
static bool bgd_area_shown(Emu* e) {
	return (emu_read16(e, BGD_DISPCNT) & 7) == 0;
}

static uint32_t bgd_virt(Emu* e, uint32_t addr) {
	if (addr == BGD_V_LOGO) {
		if (bgd_title(e)) return 1;
		/* GAME OVER: the screen gone black after the hero's fall */
		return emu_read16(e, BGD_HP) == 0 && emu_read8(e, BGD_CONTROL) == 0;
	}
	return 0;
}

/* the area: its header's address and its rectangle (pixels); false if none */
static bool bgd_level(Emu* e, uint32_t* hdr, int* x0, int* y0, int* x1, int* y1) {
	uint32_t h = emu_read32(e, BGD_LEVEL);
	if (h < 0x02000000 || h >= 0x0203FF00) return false;
	uint32_t r = h + 2 + 4 * emu_read16(e, h + 0x10);
	if (r < 0x02000000 || r >= 0x0203FFF8) return false;
	*hdr = h;
	*x0 = 16 * emu_read16(e, r), *y0 = 16 * emu_read16(e, r + 2);
	*x1 = 16 * emu_read16(e, r + 4), *y1 = 16 * emu_read16(e, r + 6);
	return *x1 - *x0 >= GBA_W && *y1 - *y0 >= GBA_H && *x1 - *x0 <= BGD_MAX && *y1 - *y0 <= BGD_MAX;
}

/* which area: its rectangle and its header's first bytes */
static uint32_t bgd_area_key(Emu* e) {
	uint32_t h;
	int x0, y0, x1, y1;
	if (!bgd_level(e, &h, &x0, &y0, &x1, &y1)) return 0;
	uint32_t k = 2166136261u ^ h;
	for (int i = 0; i < 0x20; i += 2) k = (k ^ emu_read16(e, h + i)) * 16777619u;
	return (k ^ (uint32_t) (x1 << 16 | y1)) | 1;
}

typedef struct {
	uint32_t key;
	bool done, failed;
	int x0, y0, w, h;         /* the area's top left (camera coordinates) and size */
	int ex0, ey0, ex1, ey1;   /* what's drawn on it: its edges (ex1 / ey1 past them) */
	uint32_t* px;             /* 0: nothing there */
} BgdMap;
static BgdMap bgdMap;
static pthread_t bgdThread;
static volatile bool bgdBusy, bgdStop;
static uint8_t* bgdJob;
static size_t bgdJobSize;
static char bgdRom[1024];
static Emu bgdEmu;
static bool bgdEmuOpen;

/* the copy's camera moved to x, y, a step at most each way a frame (the
 * hero's health kept at hp); false if it stops short */
static bool bgd_go(Emu* e, int x, int y, uint16_t hp) {
	int stuck = 0, lx = (int32_t) emu_read32(e, BGD_CAMX), ly = (int32_t) emu_read32(e, BGD_CAMY);
	for (int f = 0; f < 4000 && !bgdStop; ++f) {
		int cx = (int32_t) emu_read32(e, BGD_CAMX), cy = (int32_t) emu_read32(e, BGD_CAMY);
		if (cx == x && cy == y) return true;
		if (cx != lx || cy != ly) stuck = 0;
		else if (++stuck > 30) return false;
		lx = cx, ly = cy;
		int dx = x - cx, dy = y - cy;
		dx = dx > 16 ? 16 : dx < -16 ? -16 : dx;
		dy = dy > 16 ? 16 : dy < -16 ? -16 : dy;
		/* (every frame: the push back toward the hero sets them after the scroll) */
		emu_write16(e, BGD_DX, (uint16_t) dx);
		emu_write16(e, BGD_DY, (uint16_t) dy);
		emu_write16(e, BGD_HP, hp);
		emu_frame_blind(e, 0);
	}
	return false;
}

static void* bgd_worker(void* arg) {
	(void) arg;
	Emu* e = &bgdEmu;
	BgdMap* m = &bgdMap;
	struct timespec t0;
	clock_gettime(CLOCK_MONOTONIC, &t0);
	const char* why = NULL;
	int stops = 0, missed = 0;
	void* st = NULL;
	if (!bgdEmuOpen) bgdEmuOpen = emu_open(e, bgdRom, NULL, false);
	if (!bgdEmuOpen && (why = "the ROM didn't open")) goto out;
	emu_layers(e, 0x1F);
	if (!emu_state_load(e, bgdJob) && (why = "the game's moment didn't load")) goto out;
	/* the camera held still: the follow routine's stores blanked (mov r8, r8) */
	for (size_t i = 0; i < sizeof(bgdFollow) / sizeof(bgdFollow[0]); ++i) emu_write16(e, bgdFollow[i], 0x46C0);
	uint16_t hp = emu_read16(e, BGD_HP);
	for (int i = 0; i < 2; ++i) emu_frame_blind(e, 0);
	int X1 = m->x0 + m->w - GBA_W - 1, Y1 = m->y0 + m->h - GBA_H - 1;
	st = malloc(emu_state_size(e));
	if (!st && (why = "out of memory")) goto out;
	/* a screen at a time, row by row, each row the other way */
	for (int y = m->y0, row = 0; !bgdStop; y += GBA_H, ++row) {
		if (y > Y1) y = Y1;
		int n = (X1 - m->x0 + GBA_W - 1) / GBA_W + 1;
		for (int k = 0; k < n && !bgdStop; ++k) {
			int x = m->x0 + (row & 1 ? n - 1 - k : k) * GBA_W;
			if (x > X1) x = X1;
			if (!bgd_go(e, x, y, hp)) {
				++missed;
				continue;
			}
			for (int i = 0; i < 2; ++i) emu_frame_blind(e, 0); /* (its last strips drawn) */
			int cx = (int32_t) emu_read32(e, BGD_CAMX), cy = (int32_t) emu_read32(e, BGD_CAMY);
			emu_state_save(e, st);
			emu_layers(e, 0x07);
			emu_state_load(e, st);
			emu_frame(e, 0);
			for (int r = 0; r < GBA_H; ++r) {
				int py = cy - m->y0 + r;
				if (py < 0 || py >= m->h) continue;
				for (int c = 0; c < GBA_W; ++c) {
					int qx = cx - m->x0 + c;
					if (qx >= 0 && qx < m->w) m->px[(size_t) py * m->w + qx] = e->fb[r * GBA_W + c] & 0xFFFFFF;
				}
			}
			emu_layers(e, 0x1F);
			emu_state_load(e, st);
			++stops;
		}
		if (y == Y1) break;
	}
	if (!stops && !bgdStop && (why = "the camera wouldn't move")) goto out;
	if (!bgdStop) {
		/* the area's edges: its last drawn pixels (the rest is black) */
		m->ex0 = m->w, m->ey0 = m->h, m->ex1 = m->ey1 = 0;
		for (int y = 0; y < m->h; ++y)
			for (int x = 0; x < m->w; ++x)
				if (m->px[(size_t) y * m->w + x]) {
					if (x < m->ex0) m->ex0 = x;
					if (x >= m->ex1) m->ex1 = x + 1;
					if (y < m->ey0) m->ey0 = y;
					if (y >= m->ey1) m->ey1 = y + 1;
				}
		if (m->ex1 <= m->ex0 && (why = "nothing drawn")) goto out;
		m->done = true;
		struct timespec t1;
		clock_gettime(CLOCK_MONOTONIC, &t1);
		fprintf(stderr, "Baldur's Gate: Dark Alliance: area mapped (%dx%d, %d screens%s, %ld ms)\n", m->w, m->h, stops,
		        missed ? ", some out of reach" : "", (long) ((t1.tv_sec - t0.tv_sec) * 1000 + (t1.tv_nsec - t0.tv_nsec) / 1000000));
	}
out:
	free(st);
	if (why) {
		m->done = m->failed = true;
		fprintf(stderr, "Baldur's Gate: Dark Alliance: area not mapped: %s\n", why);
	}
	bgdBusy = false;
	return NULL;
}

/* the map dropped (called off first if it's still being made: false until it's stopped) */
static bool bgd_drop(void) {
	BgdMap* m = &bgdMap;
	if (bgdBusy) {
		bgdStop = true;
		return false;
	}
	if (bgdThread) pthread_join(bgdThread, NULL), bgdThread = 0;
	free(m->px);
	memset(m, 0, sizeof(*m));
	return true;
}

/* the marker (BGD-11): the empty quick slot's picture as the cartridge holds
 * it, its X (the entry-2 pixels joined to its middle) and the pixels of its
 * dark face touching the X; the rest transparent (0). Read once */
static const uint32_t* bgd_x(Emu* e) {
	static uint32_t x[16 * 16];
	static bool made;
	if (made) return x;
	uint8_t px[16 * 16];
	for (int t = 0; t < 4; ++t)
		for (int r = 0; r < 8; ++r)
			for (int c = 0; c < 8; ++c) {
				uint8_t b = emu_read8(e, BGD_SLOT + t * 32 + r * 4 + c / 2);
				px[((t >> 1) * 8 + r) * 16 + (t & 1) * 8 + c] = c & 1 ? b >> 4 : b & 15;
			}
	/* the X alone: the entry-2 pixels joined to its middle (the cartridge's
	 * last byte, a transparent corner on screen, holds an entry 2 too) */
	bool isx[16 * 16] = { false };
	int stack[16 * 16], n = 0;
	if (px[7 * 16 + 7] == 2) isx[7 * 16 + 7] = true, stack[n++] = 7 * 16 + 7;
	while (n) {
		int i = stack[--n], r = i / 16, c = i % 16;
		for (int dy = -1; dy <= 1; ++dy)
			for (int dx = -1; dx <= 1; ++dx) {
				int y = r + dy, x = c + dx;
				if (y >= 0 && y < 16 && x >= 0 && x < 16 && px[y * 16 + x] == 2 && !isx[y * 16 + x]) isx[y * 16 + x] = true, stack[n++] = y * 16 + x;
			}
	}
	for (int i = 0; i < 16 * 16; ++i) {
		int r = i / 16, c = i % 16;
		bool keep = isx[i];
		for (int dy = -1; dy <= 1 && !keep && px[i]; ++dy)
			for (int dx = -1; dx <= 1; ++dx)
				if (r + dy >= 0 && r + dy < 16 && c + dx >= 0 && c + dx < 16 && isx[(r + dy) * 16 + c + dx]) keep = true;
		if (!keep) continue;
		uint16_t v = emu_read16(e, BGD_SLOT_PAL + 2 * px[i]);
		uint32_t R = v & 31, G = (v >> 5) & 31, B = (v >> 10) & 31;
		x[i] = 0xFF000000 | (B << 3 | B >> 2) << 16 | (G << 3 | G >> 2) << 8 | (R << 3 | R >> 2);
	}
	made = true;
	return x;
}

/* the area's map, at a quarter of its size around the hero, a red X where he
 * is (BGD-10, BGD-11); a new area's map made beside the game */
static const uint32_t* bgd_live_view(Emu* live, const char* rom) {
	static uint32_t out[GBA_W * GBA_H];
	BgdMap* m = &bgdMap;
	/* the title (BGD-5; the map kept for when its area comes back), GAME
	 * OVER (BGD-9): the logo */
	if (bgd_virt(live, BGD_V_LOGO)) return NULL;
	/* a new area is mapped once the game has shown it for half a second,
	 * from a moment it shows it (in START's menu, BG0-BG2 hold the menu's
	 * pictures; a conversation's words are sprites) */
	static uint32_t settledKey;
	static int settledFor;
	uint32_t key = bgd_area_key(live);
	bool shown = bgd_area_shown(live);
	if (key != settledKey) settledKey = key, settledFor = 0;
	else if (shown) ++settledFor;
	if ((m->px || bgdBusy) && m->key != key && key) {
		if (!bgd_drop()) return NULL;
	}
	if (!m->px) {
		if (!key || settledFor < BGD_SETTLE || !shown || bgdBusy) return NULL;
		if (bgdThread) pthread_join(bgdThread, NULL), bgdThread = 0;
		uint32_t h;
		int x0, y0, x1, y1;
		if (!bgd_level(live, &h, &x0, &y0, &x1, &y1)) return NULL;
		m->key = key;
		m->x0 = x0, m->y0 = y0, m->w = x1 - x0, m->h = y1 - y0;
		if (!(m->px = calloc((size_t) m->w * m->h, 4))) {
			memset(m, 0, sizeof(*m));
			return NULL;
		}
		size_t sz = emu_state_size(live);
		if (sz > bgdJobSize) free(bgdJob), bgdJob = malloc(sz), bgdJobSize = bgdJob ? sz : 0;
		if (!bgdJob || !emu_state_save(live, bgdJob)) {
			free(m->px);
			memset(m, 0, sizeof(*m));
			return NULL;
		}
		snprintf(bgdRom, sizeof(bgdRom), "%s", rom);
		bgdStop = false;
		bgdBusy = true;
		/* in exact runs (the tests), and with no threads, made here */
		if (getenv("GBADS_SYNC") || pthread_create(&bgdThread, NULL, bgd_worker, NULL)) bgdThread = 0, bgd_worker(NULL);
	}
	if (m->failed || !m->done) return NULL; /* (the logo meanwhile) */
	/* a quarter of the size: each pixel the mean of 4x4 of the picture's,
	 * centred on his feet, kept within the area's edges; an area smaller
	 * than the screen in its middle */
	enum { Z = BGD_Z };
	int camx = (int32_t) emu_read32(live, BGD_HEROX) / 256 - m->x0, camy = (int32_t) emu_read32(live, BGD_HEROY) / 256 - m->y0;
	int left = camx - GBA_W * Z / 2, top = camy - GBA_H * Z / 2;
	if (m->ex1 - m->ex0 <= GBA_W * Z) left = (m->ex0 + m->ex1) / 2 - GBA_W * Z / 2;
	else if (left < m->ex0) left = m->ex0;
	else if (left > m->ex1 - GBA_W * Z) left = m->ex1 - GBA_W * Z;
	if (m->ey1 - m->ey0 <= GBA_H * Z) top = (m->ey0 + m->ey1) / 2 - GBA_H * Z / 2;
	else if (top < m->ey0) top = m->ey0;
	else if (top > m->ey1 - GBA_H * Z) top = m->ey1 - GBA_H * Z;
	for (int oy = 0; oy < GBA_H; ++oy) {
		int py = top + oy * Z;
		for (int ox = 0; ox < GBA_W; ++ox) {
			int qx = left + ox * Z;
			unsigned r = 0, g = 0, b = 0;
			for (int dy = 0; dy < Z; ++dy) {
				if (py + dy < 0 || py + dy >= m->h) continue;
				for (int dx = 0; dx < Z; ++dx) {
					if (qx + dx < 0 || qx + dx >= m->w) continue;
					uint32_t px = m->px[(size_t) (py + dy) * m->w + qx + dx];
					r += px & 0xFF, g += (px >> 8) & 0xFF, b += (px >> 16) & 0xFF;
				}
			}
			out[oy * GBA_W + ox] = 0xFF000000 | (b / (Z * Z)) << 16 | (g / (Z * Z)) << 8 | r / (Z * Z);
		}
	}
	/* X marks the spot: the red X of an unusable quick slot, at the game's
	 * size, its middle where his feet are (BGD-11) */
	const uint32_t* x = bgd_x(live);
	int hx = (camx - left) / Z - 8, hy = (camy - top) / Z - 8;
	for (int r = 0; r < 16; ++r) {
		if (hy + r < 0 || hy + r >= GBA_H) continue;
		for (int c = 0; c < 16; ++c)
			if (x[r * 16 + c] && hx + c >= 0 && hx + c < GBA_W) out[(hy + r) * GBA_W + hx + c] = x[r * 16 + c];
	}
	return out;
}

/* Mario vs. Donkey Kong: the game's state (decomp camthesaxman/mariovsdk,
 * gMainState; Europe's 0x40 lower: mvdk_eur): 0x0D a level (its opening pan
 * too), 0x0E a life lost, 0x0F the mute after, 0x10 paused, 0x11 a help
 * screen, 0x21 the level viewer (MVDK-2 to MVDK-7) */
#define MVDK_STATE 0x030009C0u
#define MVDK_PLAY 0x0D
#define MVDK_PAUSE 0x10
#define MVDK_HELP 0x11
#define MVDK_VIEWER 0x21
/* the HUD's sprite shapes: all but 32x32 (shape 0 size 2) and 8x32 (shape
 * 2 size 1) */
#define MVDK_HUD_SHAPES (0xFFFu & ~(1u << 2 | 1u << 9))
/* the HUD along the second screen's top, from row MVDK_HUD_TOP (the
 * game's from row 3); the pause box closed up (MVDK-9: its frame and LEVEL
 * and HI SCORE lines, 26 rows, and its bottom edge, 5) in the middle */
#define MVDK_HUD_TOP 6
#define MVDK_BOX_ROWS 31
#define MVDK_BOX_TOP ((GBA_H - MVDK_BOX_ROWS) / 2)
#define MVDK_LEVEL 0x0300010Cu /* the world, the level's kind, Plus (every release) */
#define MVDK_V_PLAY 0xF0000080u  /* a level played with its HUD up (mvdk_hud_up), not paused (the box can be taken) */
#define MVDK_V_LOGO 0xF0000081u  /* no level shown: the logo, the box forgotten */
#define MVDK_V_LEVEL 0xF0000082u /* the level (MVDK_LEVEL's word) */

/* the state's address in this release (Europe's 0x40 lower) */
static uint32_t mvdk_state(Emu* e) {
	return e->gameCode[3] == 'P' ? MVDK_STATE - 0x40 : MVDK_STATE;
}

/* the game's own "playing" flag: 0 through a level's opening pan and
 * HERE WE GO, TIME'S UP and a life lost, 1 from the frame the HUD appears
 * (and straight on as a help box closes, where the HUD's sprites come back
 * a few frames after the state does) */
static bool mvdk_hud_up(Emu* e) {
	char r = e->gameCode[3];
	return emu_read8(e, r == 'P' ? 0x030009ACu : r == 'J' ? 0x030009ECu : 0x030009E8u) != 0;
}

static uint32_t mvdk_virt(Emu* e, uint32_t addr) {
	int st = emu_read8(e, mvdk_state(e));
	if (addr == MVDK_V_LEVEL) return emu_read32(e, MVDK_LEVEL);
	bool shown = ((st == MVDK_PLAY || st == MVDK_PAUSE || st == MVDK_VIEWER) && mvdk_hud_up(e)) ||
	             (st > MVDK_PLAY && st <= MVDK_HELP && st != MVDK_PAUSE); /* (a life lost, a help screen: kept) */
	if (addr == MVDK_V_PLAY) return st == MVDK_PLAY && mvdk_hud_up(e);
	if (addr == MVDK_V_LOGO) return !shown;
	return 0;
}

static bool mvdk_rgb(uint32_t c, uint32_t rgb) {
	/* (framebuffer ABGR: R in the low byte) */
	return (c & 0xFF) == (rgb >> 16 & 0xFF) && (c >> 8 & 0xFF) == (rgb >> 8 & 0xFF) && (c >> 16 & 0xFF) == (rgb & 0xFF);
}

/* The level's box (MVDK-9): START on the hidden copy, the pause box's
 * priority-0 sprites alone (the level, and its things behind the box,
 * left out, and the star), its frame and LEVEL and HI SCORE lines and then
 * its bottom edge, in the middle of a black GBA screen. Checked as it's cut
 * (GEN-3): the frame's black top (a run of 60 or more) and bottom, the two
 * lines' grey bars 6 in from its side with lavender between and after. */
static const char* mvdk_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (w != GBA_W || h != GBA_H) return "wrong size";
	if (!emu_state_load(s, state)) return "state load failed";
	uint32_t sa = mvdk_state(s);
	emu_frame(s, 0);
	if (emu_read8(s, sa) != MVDK_PLAY) return "not playing";
	aw_run(s, K_START, 4);
	for (int f = 0; f < 40 && emu_read8(s, sa) != MVDK_PAUSE && !emu_cancelled(s); ++f) aw_run(s, 0, 1);
	if (emu_read8(s, sa) != MVDK_PAUSE) return "START didn't pause";
	aw_run(s, 0, 6);
	size_t sz = emu_state_size(s);
	void* paused = malloc(sz);
	uint32_t* spr = malloc(GBA_W * GBA_H * 4);
	uint32_t* back = malloc(GBA_W * GBA_H * 4);
	const char* err = NULL;
	if (!paused || !spr || !back || !emu_state_save(s, paused)) {
		err = "out of memory";
		goto done;
	}
	/* the box's sprites alone, then the backdrop alone (layer switches
	 * take effect as the registers are written: loading the state again) */
	/* (and the star beside LEVEL once its high score is beaten, palette 10:
	 * the box itself is palette 6) */
	static const SpriteRule behind[4] = { { -512, -256, 511, 255, 0, 1023, 1 }, { -512, -256, 511, 255, 0, 1023, 2 },
	                                      { -512, -256, 511, 255, 0, 1023, 3 },
	                                      { -512, -256, 511, 255, 0, 1023, 0, 1u << 10 } };
	emu_hide_sprites(s, behind, 4);
	emu_layers(s, 0x10);
	emu_state_load(s, paused);
	emu_frame(s, 0);
	memcpy(spr, s->fb, GBA_W * GBA_H * 4);
	emu_layers(s, 0);
	emu_state_load(s, paused);
	emu_frame(s, 0);
	memcpy(back, s->fb, GBA_W * GBA_H * 4);
	emu_layers(s, 0x1F);
	emu_hide_sprites(s, NULL, 0);
	{
#define MVDK_SPR(x, y) (spr[(y) * GBA_W + (x)] != back[(y) * GBA_W + (x)])
#define MVDK_IS(x, y, rgb) (MVDK_SPR(x, y) && mvdk_rgb(spr[(y) * GBA_W + (x)], rgb))
		/* the frame's top: a row of 60 or more black pixels (82 in the
		 * box; it's centred, or in a Mini-Mario level more to the side) */
		int top = -1, bottom = -1, x0 = 0, x1 = 0;
		for (int y = 0; y < GBA_H - 60 && top < 0; ++y) {
			int run = 0;
			for (int x = 0; x <= GBA_W && top < 0; ++x) {
				if (x < GBA_W && MVDK_IS(x, y, 0x000000)) {
					++run;
					continue;
				}
				if (run >= 60) top = y, x0 = x - run - 5, x1 = x - 1 + 5;
				run = 0;
			}
		}
		int cx = (x0 + x1) / 2;
		for (int y = top + 40; top >= 0 && y < GBA_H - 1 && bottom < 0; ++y)
			if (MVDK_IS(cx, y, 0x000000) && !MVDK_SPR(cx, y + 1)) bottom = y;
		/* the two bars, 6 in from the box's side: grey, lavender, grey,
		 * lavender */
		int edges[4], n = 0;
		const uint32_t want[4] = { 0xC6C6C6, 0xE7CEFF, 0xC6C6C6, 0xE7CEFF };
		for (int y = top + 1; top >= 0 && x0 >= 0 && y < top + 30 && n < 4; ++y)
			if (MVDK_IS(x0 + 6, y, want[n])) edges[n++] = y;
		if (top < 0 || bottom < 0 || n < 4 || x0 < 0 || x1 >= GBA_W) {
			err = "no pause box";
			goto done;
		}
		/* down to the lavender row after HI SCORE and one more, then the
		 * box's bottom edge (its last 5 rows); centred */
		int upper = edges[3] + 2 - top;
		if (upper + 5 != MVDK_BOX_ROWS) {
			err = "pause box of another size";
			goto done;
		}
		int dx = (GBA_W - (x1 - x0 + 1)) / 2 - x0;
		for (int i = 0; i < w * h; ++i) out[i] = 0xFF000000;
		for (int r = 0; r < MVDK_BOX_ROWS; ++r) {
			int sy = r < upper ? top + r : bottom - (MVDK_BOX_ROWS - 1 - r);
			for (int x = x0; x <= x1; ++x)
				if (MVDK_SPR(x, sy)) out[(MVDK_BOX_TOP + r) * GBA_W + x + dx] = spr[sy * GBA_W + x] | 0xFF000000;
		}
#undef MVDK_SPR
#undef MVDK_IS
	}
done:
	free(paused);
	free(spr);
	free(back);
	return err;
}

/* ---------------------------------------------------------- Pokémon (preliminary: FireRed / LeafGreen, Ruby / Sapphire / Emerald)
 *
 * Experiment for the branch pokemon-preliminary (docs/pokemon-ideas.md). The
 * second screen is the game's own Party screen and region map (the Pokénav's
 * in Ruby / Sapphire / Emerald, the Town Map from the Bag in FireRed /
 * LeafGreen), opened on the shadow core through the START menu and shown in
 * turn. Nothing here is the game's data: no tables, no
 * addresses of a particular ROM, so a romhack built on the same engine
 * needs no work of its own.
 *
 *  - Where things are is found on the screen, not in memory: START is
 *    pressed on the hidden copy and the menu's panel looked for; the START
 *    menu remembers its cursor, so the cursor is first put on the first
 *    entry (DOWN until it wraps to the top: a cursor that moves up the
 *    panel's rows only changes pixels in two bands, far apart when it
 *    wraps), then moved by counting: POKéMON is the first or second entry
 *    (tried in turn; the screen that comes up is checked), BAG / POKéNAV
 *    are found by trying each row (a romhack drops SAVE or EXIT). FireRed / LeafGreen's Town
 *    Map is a key item whose row depends on what the player owns: each row
 *    of the Key Items pocket is tried (USE) until the map's registers come
 *    up, and the row that worked is tried first the next time.
 *  - Which screen came up is told by the display registers (DISPCNT and
 *    BG0-3CNT), which belong to the engine, not to a romhack's changes
 *    (three engines: FireRed / LeafGreen, Ruby / Sapphire, Emerald).
 *  - When to look again: pk_virt finds the party in RAM (records of 100
 *    bytes whose checksum is right: nothing to configure) and gives a value
 *    that changes with anyone's HP, status, level or species.
 */
typedef struct { uint16_t r[5]; } PkSig; /* DISPCNT, BG0CNT ... BG3CNT; r[0] 0: none */
typedef struct {
	PkSig party, nav, map, bag;
	/* the Party's message box and CANCEL: rows from here down are the
	 * background's, taken from the left edge */
	/* the START menu's panel ends here (FireRed has a description box below it) */
	int menuBottom;
	int partyClean, partyFrom;
	int partySide; /* a row above that where the box's bottom edge is: only the left of the boxes is cleaned (the stub of the message box), 0: none */
	/* the map's button prompts: each is a box x0 to x1, y0 to y1, filled row
	 * by row from the pixel at column fx of that row */
	int mapClean[4][5];
} PkFam;
static const PkFam pkFrlg = {
	.party = { { 0x1740, 0x1F01, 0x1E02, 0x5C00, 0x0000 } },
	.bag = { { 0x3340, 0x1F00, 0x1E0D, 0x0000, 0x0000 } },
	.map = { { 0x3B40, 0x1E02, 0x0F07, 0x0000, 0x1F0C } }, /* the Town Map */
	.partyClean = 130, .partyFrom = 182, .menuBottom = 112,
	.mapClean = { { 130, 239, 0, 10, 100 },     /* the move prompt on the blue strip (longer in other languages), and the end of its GUIDE text on the right */
	              { 130, 205, 11, 14, 100 },
	              { 206, 239, 11, 11, 100 },
	              { 194, 214, 131, 149, 192 } }, /* the exit button */
};
static const PkFam pkRs = {
	.party = { { 0x1F40, 0x1E06, 0x0703, 0x0F08, 0x0602 } },
	.nav = { { 0x3F40, 0x1F01, 0x1B0C, 0x1D0A, 0x1C0B } },
	.map = { { 0x1741, 0x1F01, 0x1B0C, 0xBC8A, 0x1C0B } },
	.partyClean = 129, .partyFrom = 193, .partySide = 128, .menuBottom = 158,
	.mapClean = { { 0, 118, 144, 159, 122 } },
};
/* Emerald: the Party is FireRed's screen, the Pokénav its own */
static const PkFam pkEm = {
	.party = { { 0x1740, 0x1F01, 0x1E02, 0x5C00, 0x0000 } },
	.nav = { { 0x3F40, 0x0500, 0x0F05, 0x170A, 0x1F0F } },
	.map = { { 0x1741, 0x0500, 0x1F05, 0xA68A, 0x1F0F } },
	.partyClean = 128, .partyFrom = 183, .menuBottom = 158,
	.mapClean = { { 0, 112, 144, 159, 118 } },
};
static const PkFam* pk_fam(const Emu* e) {
	const char* c = e->gameCode;
	if (!strncmp(c, "BPR", 3) || !strncmp(c, "BPG", 3)) return &pkFrlg;
	if (!strncmp(c, "AXP", 3) || !strncmp(c, "AXV", 3)) return &pkRs;
	if (!strncmp(c, "BPE", 3)) return &pkEm;
	return NULL;
}
static bool pk_is(Emu* s, const PkSig* g) {
	if (!g->r[0]) return false;
	/* (DISPCNT without the window bits: the Town Map turns window 1 on for the
	 * name of a place it labels, as Cinnabar's Pokémon Mansion) */
	return (emu_io16(s, 0x00) & 0x1FFF) == (g->r[0] & 0x1FFF) && emu_io16(s, 0x08) == g->r[1] && emu_io16(s, 0x0A) == g->r[2] &&
	       emu_io16(s, 0x0C) == g->r[3] && emu_io16(s, 0x0E) == g->r[4];
}
static bool pk_wait(Emu* s, const PkSig* g, int frames) {
	for (int i = 0; i < frames; ++i) {
		if (pk_is(s, g)) {
			aw_run(s, 0, 2);
			return true;
		}
		emu_frame_blind(s, 0);
	}
	return false;
}
/* the START menu's white panel (right side) is up: lots of white there and
 * the picture differs from before */
static bool pk_panel(Emu* s, const uint32_t* before) {
	int white = 0, changed = 0;
	for (int y = 4; y < 100; ++y) {
		for (int x = 172; x < 236; ++x) {
			uint32_t c = s->fb[y * GBA_W + x];
			white += (c & 0xFF) >= 0xF0 && ((c >> 8) & 0xFF) >= 0xF0 && ((c >> 16) & 0xFF) >= 0xF0;
			changed += c != before[y * GBA_W + x];
		}
	}
	return white >= 1200 && changed >= 400;
}
/* After a DOWN on the menu: 0 nothing moved in the panel, 1 the cursor went
 * down a row (its old and new places are bands side by side), 2 it went from
 * the last entry to the first (bands far apart) */
static int pk_moved(Emu* s, const uint32_t* before, int bottom) {
	int first = -1, last = -1, gap = 0, run = 0;
	for (int y = 0; y < bottom; ++y) {
		bool ch = false;
		for (int x = 172; x < 236 && !ch; ++x) ch = s->fb[y * GBA_W + x] != before[y * GBA_W + x];
		if (ch) {
			if (first < 0) first = y;
			else if (run > gap) gap = run;
			last = y;
			run = 0;
		} else if (first >= 0) {
			++run;
		}
	}
	if (first < 0) return 0;
	(void) last;
	return gap >= 18 ? 2 : 1;
}
/* The same for a bar of icons along the bottom (Unbound's START menu): RIGHT
 * moves along it, the picture changes only in that strip; 2 when the cursor
 * went from the last icon back to the first (the changes far apart) */
static int pk_moved_h(Emu* s, const uint32_t* before) {
	int first = -1, gap = 0, run = 0;
	for (int x = 0; x < GBA_W; ++x) {
		bool ch = false;
		for (int y = 112; y < GBA_H && !ch; ++y) ch = s->fb[y * GBA_W + x] != before[y * GBA_W + x];
		if (ch) {
			if (first < 0) first = x;
			else if (run > gap) gap = run;
			run = 0;
		} else if (first >= 0) {
			++run;
		}
	}
	if (first < 0) return 0;
	return gap >= 100 ? 2 : 1;
}
static uint32_t pkNext = K_DOWN; /* the key to the next entry: DOWN, or RIGHT along a bar */
/* a press these menus take every time: 3 frames down, 9 up (the last two drawn) */
static void pk_tap(Emu* s, uint32_t key) {
	aw_run(s, key, 3);
	aw_run(s, 0, 9);
}
static void pk_keep(Emu* s, uint32_t* to) { memcpy(to, s->fb, GBA_W * GBA_H * sizeof(uint32_t)); }
/* START pressed on the hidden copy, the menu up, its cursor on the first entry */
static const char* pk_menu_try(Emu* s, const PkFam* f) {
	static uint32_t before[GBA_W * GBA_H];
	{ /* the player's START menu already up (its panel's white): B puts it away */
		int white = 0;
		for (int y = 4; y < 100; ++y)
			for (int x = 172; x < 236; ++x) {
				uint32_t c = s->fb[y * GBA_W + x];
				white += (c & 0xFF) >= 0xF0 && ((c >> 8) & 0xFF) >= 0xF0 && ((c >> 16) & 0xFF) >= 0xF0;
			}
		if (white >= 1200) {
			pk_tap(s, K_B);
			aw_run(s, 0, 24);
		}
	}
	pk_keep(s, before);
	pk_tap(s, K_START);
	bool up = false;
	for (int i = 0; i < 24 && !(up = pk_panel(s, before)); ++i) emu_frame(s, 0);
	bool bar = false;
	if (!up) { /* a bar of icons along the bottom instead of a panel? */
		int changed = 0;
		for (int y = 112; y < GBA_H; ++y)
			for (int x = 0; x < GBA_W; ++x) changed += s->fb[y * GBA_W + x] != before[y * GBA_W + x];
		bar = changed >= 3000;
	}
	pkNext = bar ? K_RIGHT : K_DOWN;
	if (!up && !bar) return "map button did nothing"; /* a scene, a conversation, a menu, a battle: quick */
	aw_run(s, 0, 24); /* (the menu takes no input just as it opens) */
	for (int i = 0; i < 12; ++i) {
		pk_keep(s, before);
		pk_tap(s, pkNext);
		int m = bar ? pk_moved_h(s, before) : pk_moved(s, before, f->menuBottom);
		if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: next %d -> %d (io %04X %04X)\n", i, m, emu_io16(s, 0), emu_io16(s, 8));
		if (m == 2) return NULL;
		if (m == 0) return "map button did nothing (the menu's cursor didn't move)"; /* (quick to retry: game.c) */
	}
	return "map button did nothing (the menu's cursor never wrapped)";
}
/* the player's own Party screen or START menu may be up: the hidden copy
 * backs out of it (B) and opens its own (PK-6) */
static const char* pk_menu_top(Emu* s, const PkFam* f, const void* state) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	for (int i = 0; i < 4 && pk_is(s, &f->party); ++i) {
		pk_tap(s, K_B);
		aw_run(s, 0, 24);
	}
	const char* err = pk_menu_try(s, f);
	if (err && strstr(err, "cursor didn't move")) {
		/* (START closed a menu that was already up) */
		for (int i = 0; i < 2; ++i) {
			pk_tap(s, K_B);
			aw_run(s, 0, 20);
		}
		err = pk_menu_try(s, f);
	}
	return err;
}
/* from the first entry: n entries down (n > 0), or from the end up (n < 0:
 * -1 is the last) */
static void pk_go(Emu* s, int n) {
	for (int i = 0; i < (n < 0 ? -n : n); ++i) pk_tap(s, n < 0 ? K_UP : pkNext);
}
/* What the capture has learned about the game it is running (the START menu's
 * rows, where the Town Map is, the last map taken). All of it belongs to one
 * ROM: pk_pages starts it again when another game's capture arrives, or a
 * map from the game played before would be shown (and a search that found no
 * Town Map there would be believed here). */
static struct {
	uint32_t rom;
	bool known;
	int partyRow, navGood, first, goodBag, tick, wait;
	bool noMapKnown, have, partySent;
	uint32_t noMapHash;
	uint32_t keep[GBA_W * GBA_H];
} pkMem;
static const char* pk_party(Emu* s, const PkFam* f, const void* state, uint32_t* out, int w, int h) {
	int* const prow = &pkMem.partyRow; /* POKéMON is the second entry once there's a Pokédex */
	for (int t = 0; t < 2; ++t, *prow ^= 1) {
		int row = *prow;
		const char* err = pk_menu_top(s, f, state);
		if (err) return err;
		pk_go(s, row);
		pk_tap(s, K_A);
		if (!pk_wait(s, &f->party, 90)) continue;
		aw_run(s, 0, 60); /* (the screen builds itself and takes no input for a while) */
		/* UP from the first slot: the cursor goes to CANCEL (left out below),
		 * no Pokémon is lit */
		pk_tap(s, K_UP);
		aw_run(s, 0, 8);
		for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
		if (f->partySide) {
			for (int x = 0; x < 80 && x < w; ++x) out[f->partySide * w + x] = s->fb[f->partySide * GBA_W + 40];
		}
		/* the message box and CANCEL: the background's, taken from beside them */
		for (int y = f->partyClean; y < h && y < GBA_H; ++y) {
			for (int x = 0; x < w && x < GBA_W; ++x) out[y * w + x] = s->fb[y * GBA_W + f->partyFrom];
		}
		return NULL;
	}
	return "the Party didn't open";
}
/* A menu entry that opens the screen `sig` (the Bag, the Pokénav): its row
 * isn't counted from the end (a romhack drops SAVE or EXIT, adds a clock or
 * its own entries), each row is tried from the top, the screen that comes up
 * told by the registers, and the row that worked tried first next time.
 * Returns the row, or -1 (err says why); the screen is up when it returns. */
static int pk_open(Emu* s, const PkFam* f, const void* state, const PkSig* sig, int* good, int frames, const char** err) {
	enum { ROWS = 9 };
	for (int t = 0; t < ROWS; ++t) {
		int k = (*good + t) % ROWS;
		*err = pk_menu_top(s, f, state);
		if (*err) return -1;
		pk_go(s, k);
		pk_tap(s, K_A);
		if (pk_wait(s, sig, frames)) {
			*good = k;
			if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: row %d opened the screen\n", k);
			return k;
		}
		if (emu_cancelled(s)) break;
	}
	*err = "the menu has no such entry";
	return -1;
}
/* the clean-ups, then the picture */
static void pk_map_clean(Emu* s, const PkFam* f, uint32_t* out, int w, int h) {
	for (int k = 0; k < 4; ++k) {
		const int* c = f->mapClean[k];
		for (int y = c[2]; y <= c[3] && y < h; ++y) {
			for (int x = c[0]; x <= c[1] && x < w; ++x) out[y * w + x] = s->fb[y * GBA_W + c[4]];
		}
	}
}
static void pk_map_copy(Emu* s, const PkFam* f, uint32_t* out, int w, int h) {
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	pk_map_clean(s, f, out, w, h);
}
/* Ruby / Sapphire / Emerald: POKéNAV, whose first entry is the region map */
static const char* pk_nav_map(Emu* s, const PkFam* f, const void* state, uint32_t* out, int w, int h) {
	int* const good = &pkMem.navGood;
	const char* err;
	if (pk_open(s, f, state, &f->nav, good, 60, &err) < 0) return err;
	aw_run(s, 0, 30);
	pk_tap(s, K_A); /* its first entry, the region map */
	if (!pk_wait(s, &f->map, 120)) return "the map didn't open";
	aw_run(s, 0, 40);
	/* The map opens in the view the player left it in; the zoom view (BG2's
	 * scale, 0x80 against the full view's 0x100) is switched to the full
	 * view with A, which shows the whole region (PK-3). */
	if (emu_io16(s, 0x20) < 0x100) {
		pk_tap(s, K_A);
		aw_run(s, 0, 60);
		if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: zoom view -> full view: scale %04X\n", emu_io16(s, 0x20));
	}
	pk_map_copy(s, f, out, w, h);
	return NULL;
}
/* FireRed / LeafGreen: the Town Map is a key item. BAG (its row found by trying them),
 * the Key Items pocket (the pockets are in a row: all the way left, then
 * once right), the cursor to the top, then down row by row, USE on each
 * until the map's registers come up (the copy is loaded afresh each time,
 * so whatever another item does is gone). The row that worked is the first
 * to try next time. */
/* In a battle the game draws with the sprite window (DISPCNT bit 15), which
 * nothing else in these games does: the Party stays up (PK-4) */
static int pk_hold_page(Emu* live) {
	return (emu_io16(live, 0) & 0x8000) ? 0 : -1;
}
/* a hash of the Bag's list (the right of the screen: the bag's own picture
 * on the left moves) */
static uint32_t pk_list_hash(Emu* s) {
	uint32_t h = 2166136261u;
	for (int y = 0; y < 120; ++y)
		for (int x = 96; x < 232; ++x) h = (h ^ (s->fb[y * GBA_W + x] & 0xFFFFFF)) * 16777619u;
	return h;
}
#define PK_KEY_ROWS 30 /* the Key Items pocket's most (FireRed, LeafGreen) */
#define PK_MAP_EVERY 5 /* captures (about 15 s) between the map's */
/* a hash of the Bag's description box (the bottom of the screen): it changes
 * with the item the cursor is on */
static uint32_t pk_desc_hash(Emu* s) {
	uint32_t h = 2166136261u;
	for (int y = 118; y < GBA_H; ++y)
		for (int x = 0; x < GBA_W; ++x) h = (h ^ (s->fb[y * GBA_W + x] & 0xFFFFFF)) * 16777619u;
	return h;
}
/* how many rows the Key Items list has: the cursor's moved down until the
 * description stops changing (the list doesn't wrap). It's left on the last */
static int pk_key_rows(Emu* s) {
	int n = 1;
	uint32_t prev = pk_desc_hash(s);
	for (; n < PK_KEY_ROWS; ++n) {
		pk_tap(s, K_DOWN);
		uint32_t d = pk_desc_hash(s);
		if (d == prev) break;
		prev = d;
	}
	return n;
}
/* Where the Town Map is in the Key Items list, from the game's own memory
 * rather than a row-by-row search of the Bag (seconds of frames on a slow
 * device, and the second screen shows only the logo meanwhile): the pocket is
 * 30 slots of (item, quantity) halfwords, the Town Map is item 361, and what
 * follows it is the Poké Balls pocket. A guess only (a romhack may keep its
 * Bag otherwise): the search starts at this row and goes on from there if the
 * screen that opens isn't the map. -1: not found. */
static uint32_t pk_rd32(const uint8_t* m, size_t i);
static int pk_key_hint(Emu* s) {
	size_t size = 0;
	const uint8_t* m = emu_memory(s, 2, &size);
	if (!m) return -1;
	for (size_t i = 0; i + 4 <= size; i += 4) {
		if ((pk_rd32(m, i) & 0xFFFF) != 361) continue; /* (the quantities are scrambled with a key: only the item ids are read) */
		for (int k = 0; k < 30 && (size_t) k * 4 <= i; ++k) {
			size_t base = i - (size_t) k * 4;
			if (base + 43 * 4 > size) continue;
			bool ok = true, gap = false;
			for (int j = 0; j < 30 && ok; ++j) { /* a run of items, then empty slots */
				unsigned id = pk_rd32(m, base + j * 4) & 0xFFFF;
				if (!id) gap = true;
				else if (gap || id > 0x1FF) ok = false;
			}
			bool gapB = false;
			for (int j = 30; j < 43 && ok; ++j) { /* the Poké Balls: ids 1 to 12, then empty */
				unsigned id = pk_rd32(m, base + j * 4) & 0xFFFF;
				if (!id) gapB = true;
				else if (gapB || id > 12) ok = false;
			}
			if (ok && (pk_rd32(m, base + k * 4) & 0xFFFF) == 361) return k;
		}
	}
	return -1;
}
static const char* pk_town_map(Emu* s, const PkFam* f, const void* state, uint32_t* out, int w, int h) {
	uint32_t listHash = 0;
	int rows = PK_KEY_ROWS;
	int hint = pk_key_hint(s);
	if (hint >= 0) pkMem.first = hint;
	if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: Town Map row from memory: %d\n", hint);
	for (int t = 0; t < rows; ++t) {
		const char* err;
		if (pk_open(s, f, state, &f->bag, &pkMem.goodBag, 60, &err) < 0) return err;
		aw_run(s, 0, 90); /* (a romhack's Bag can take longer to take input) */
		pk_tap(s, K_LEFT);
		pk_tap(s, K_LEFT);
		pk_tap(s, K_RIGHT);
		aw_run(s, 0, 10);
		for (int i = 0; i < (t == 0 ? PK_KEY_ROWS : rows) + 2; ++i) pk_tap(s, K_UP);
		if (t == 0) {
			/* the Key Items list as it is: the same as when no Town Map was
			 * found last time means still none, without the row-by-row
			 * search (seconds of frames, taken again and again until the
			 * player has it) */
			aw_run(s, 0, 20);
			listHash = pk_list_hash(s);
			if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: key items list %08X (last none: %d %08X)\n", listHash, pkMem.noMapKnown, pkMem.noMapHash);
			if (pkMem.noMapKnown && listHash == pkMem.noMapHash) return "no Town Map in the Bag";
			rows = pk_key_rows(s);
			if (rows < 2) rows = 10; /* (no change seen: an empty pocket, or a Bag drawn another way: as before) */
			if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: key items: %d rows\n", rows);
			for (int i = 0; i < rows + 1; ++i) pk_tap(s, K_UP);
		}
		int row = (pkMem.first + t) % rows;
		for (int i = 0; i < row; ++i) pk_tap(s, K_DOWN);
		pk_tap(s, K_A); /* its menu: USE first */
		aw_run(s, 0, 10);
		pk_tap(s, K_A);
		if (getenv("GBADS_PKDBG")) fprintf(stderr, "pk: key row %d, io %04X %04X %04X %04X %04X\n", row, emu_io16(s, 0), emu_io16(s, 8), emu_io16(s, 10), emu_io16(s, 12), emu_io16(s, 14));
		if (!pk_wait(s, &f->map, 150)) continue;
		aw_run(s, 0, 110); /* (the registers are right while it's still fading in from white) */
		if (!pk_is(s, &f->map)) continue;
		pkMem.first = row;
		pkMem.noMapKnown = false;
		pk_map_copy(s, f, out, w, h);
		return NULL;
	}
	if (!emu_cancelled(s)) pkMem.noMapKnown = true, pkMem.noMapHash = listHash;
	return "no Town Map in the Bag";
}
static const char* pk_map(Emu* s, const PkFam* f, const void* state, uint32_t* out, int w, int h) {
	return f->bag.r[0] ? pk_town_map(s, f, state, out, w, h) : pk_nav_map(s, f, state, out, w, h);
}
static const char* pk_pages(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	/* The Party is taken every time (it's what changes). The map costs a
	 * menu's worth of frames or several (the Bag: a row at a time), so it's
	 * kept and taken again only now and then (where the player is changes
	 * slowly); when there's none (before the Pokénav or the Town Map, or a
	 * game that has none) it's not tried for a while. */
	const PkFam* f = pk_fam(s);
	if (!f) return "not a Pokémon game this knows";
	if (!pkMem.known || pkMem.rom != s->romCrc) {
		memset(&pkMem, 0, sizeof(pkMem));
		pkMem.partyRow = 1;
		pkMem.known = true;
		pkMem.rom = s->romCrc;
	}
	uint32_t* const keep = pkMem.keep;
	bool* const have = &pkMem.have;
	int* const tick = &pkMem.tick;
	int* const wait = &pkMem.wait;
	*step = 300; /* 5 seconds a page */
	const char* err = pk_party(s, f, state, out[0], w, h);
	if (err) return err; /* (no Party: no second screen to keep) */
	int n = 1;
	if (!pkMem.partySent) {
		/* the Party first, at once: the map's first look can take a Bag's worth
		 * of searching, and the second screen would be the logo till it's done */
		pkMem.partySent = true;
		*nOut = 1;
		return NULL;
	}
	if ((*tick)++ % (*have ? PK_MAP_EVERY : 3) == 0) *wait = 0; /* (looking for one is cheap now: the Key Items list is compared first) */
	if (!*wait && (*tick % PK_MAP_EVERY == 1 || !*have) && w == GBA_W && h == GBA_H) {
		if (!pk_map(s, f, state, keep, GBA_W, GBA_H)) *have = true;
		else if (!emu_cancelled(s)) *have = false, *wait = 1; /* (a capture called off says nothing about the map) */
	}
	if (*have && *nOut >= 2 && w == GBA_W && h == GBA_H) memcpy(out[n++], keep, sizeof(pkMem.keep));
	*nOut = n;
	return NULL;
}

/* Where the party is: 100-byte records (a Pokémon in a party: the personality
 * value and trainer ID, 48 bytes of data encrypted with their xor and summed
 * into a checksum) that check out, at 4-byte steps through EWRAM and IWRAM.
 * Each Gen 3 game and romhack keeps its party somewhere (and the enemy's
 * during a battle); the records are found whichever it is, once, and the
 * value pk_virt returns changes when any of them changes (HP, status, level,
 * species): the second screen's "look again". */
#define PK_VIRT 0xF0000010
#define PK_MAX 24
static struct { uint32_t addr[PK_MAX]; int n; unsigned long scanned; bool have; } pkLoc;
static uint32_t pk_rd32(const uint8_t* m, size_t i) { uint32_t v; memcpy(&v, m + i, 4); return v; }
static bool pk_record(const uint8_t* r) {
	uint32_t pers = pk_rd32(r, 0), key = pers ^ pk_rd32(r, 4);
	if (!pers && !pk_rd32(r, 4)) return false;
	unsigned sum = 0;
	for (int i = 0; i < 12; ++i) {
		uint32_t w = pk_rd32(r, 0x20 + i * 4) ^ key;
		sum += (w & 0xFFFF) + (w >> 16);
	}
	if ((sum & 0xFFFF) != (unsigned) (r[0x1C] | r[0x1D] << 8)) return false;
	unsigned lv = r[0x54], hp = r[0x56] | r[0x57] << 8, mhp = r[0x58] | r[0x59] << 8;
	return lv >= 1 && lv <= 100 && mhp > 0 && mhp < 1000 && hp <= mhp;
}
static void pk_scan(Emu* e) {
	pkLoc.n = 0;
	for (int region = 2; region <= 3; ++region) {
		size_t size = 0;
		const uint8_t* m = emu_memory(e, region, &size);
		if (!m) continue;
		for (size_t i = 0; i + 100 <= size && pkLoc.n < PK_MAX; i += 4) {
			if (pk_record(m + i)) pkLoc.addr[pkLoc.n++] = ((uint32_t) region << 24) + (uint32_t) i;
		}
	}
	pkLoc.scanned = e->frames;
	pkLoc.have = true;
	if (getenv("GBADS_PKDBG")) {
		fprintf(stderr, "pk: party scan found %d records:", pkLoc.n);
		for (int i = 0; i < pkLoc.n; ++i) fprintf(stderr, " %08X", pkLoc.addr[i]);
		fprintf(stderr, "\n");
	}
}
static uint32_t pk_virt(Emu* e, uint32_t addr) {
	if (addr != PK_VIRT) return 0;
	static uint32_t locRom;
	if (locRom != e->romCrc) memset(&pkLoc, 0, sizeof(pkLoc)), locRom = e->romCrc; /* (another game: its party is elsewhere) */
	/* (a record that stops checking out, or none yet: look again, at most
	 * once a second) */
	bool ok = pkLoc.have && pkLoc.n > 0;
	for (int i = 0; ok && i < pkLoc.n; ++i) {
		size_t size = 0;
		const uint8_t* m = emu_memory(e, pkLoc.addr[i] >> 24, &size);
		ok = m && pk_record(m + (pkLoc.addr[i] & 0xFFFFFF));
	}
	if (!ok && (!pkLoc.have || e->frames - pkLoc.scanned >= 60)) pk_scan(e);
	uint32_t h = 2166136261u;
	for (int i = 0; i < pkLoc.n; ++i) {
		size_t size = 0;
		const uint8_t* m = emu_memory(e, pkLoc.addr[i] >> 24, &size);
		if (!m) continue;
		const uint8_t* r = m + (pkLoc.addr[i] & 0xFFFFFF);
		/* species is encrypted; the party's stats outside it say as much */
		for (int b = 0x50; b < 0x5A; ++b) h = (h ^ r[b]) * 16777619u;
		h = (h ^ r[0x1C]) * 16777619u;
		h = (h ^ r[0x1D]) * 16777619u;
	}
	return h;
}

/* The title screen's logo (every release and romhack has its own, so the
 * logo is per ROM file: logoPerRom): START every 100 frames skips the intros
 * (some titles need it a number of times), then the title is up and holds
 * until START; the logo is BG0 in FireRed / LeafGreen (the version's name is
 * in it), BG2 and the sprites above row 100 in Ruby / Sapphire / Emerald
 * (the version's name is a sprite; PRESS START and the copyright line,
 * below, are left out). A romhack with a title of its own way gets a line in
 * pkLogos (by its ROM file's CRC32) */
#define PK_S3 { { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 } }
#define PK_S4 { { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 } }
#define PK_LOGO_FRLG { .steps = PK_S3, .wait = 100, .layer = 0, .rev = 1 }
#define PK_LOGO_RSE3 { .steps = PK_S3, .wait = 100, .layers = 0x14, .maxY = 100, .rev = 1 }
#define PK_LOGO_RS4 { .steps = PK_S4, .wait = 100, .layers = 0x14, .maxY = 100, .rev = 1 }
static const LogoFor pkLogos[] = {
	/* Pokémon Emerald Rogue EX: a splash and a logo screen before its title (six STARTs) */
	{ 0x4CC15B8D, "Pokemon Emerald Rogue EX v2.1.1", { .steps = { { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 }, { 0, 100 }, { K_START, 3 } }, .wait = 100, .layers = 0x14, .maxY = 100, .rev = 1 } },
	/* Saiph: one START, then its title fades in for about 400 frames */
	{ 0x32B2929C, "Pokemon Saiph", { .steps = { { 0, 100 }, { K_START, 3 } }, .wait = 450, .layer = 0, .rev = 1 } },
	/* Light Platinum: its subtitle sits lower */
	{ 0x41D07817, "Pokemon Light Platinum", { .steps = PK_S4, .wait = 100, .layers = 0x14, .maxY = 118, .rev = 1 } },
};


/* ---------------------------------------------- Boktai: The Sun Is in Your Hand
 *
 * Boktai's second screen (docs/games/boktai-the-sun-is-in-your-hand.md): the
 * Map page of SELECT's menu, opened on the shadow core from the live state at
 * every capture. The addresses (the USA release's):
 *   BT_MENU (byte) 0 while playing, 1 while SELECT's menu is up, 2 while
 *              START's pause menu is
 *   BT_PAGE (byte) the menu's page: 0 Item, 1 Weapon, 2 Map (the menu opens
 *              on the page last used, so the copy turns to the Map with L / R)
 * The Map page's sprites: the L and R ends of its title and the A + up /
 * down glyph beside the floor are button prompts, and the little arrows at
 * the map's edges say it scrolls: all left out (BT-2).
 */
#define BT_MENU 0x02001B3A
#define BT_PAGE 0x020034DC
static const SpriteRule btPrompts[] = {
	/* (by place and shape: the tiles and palette differ from area to area) */
	/* the title bar, its L and R ends and tabs, all sprites over the Map's own
	 * picture (BT-2): everything whose corner is in the top left */
	{ 0, 0, 116, 12, 0, 1023, -1, 0, 0, 0 },
	{ 188, 134, 196, 138, 0, 1023, -1, 0, 0, 0x2 },  /* the A + up / down glyph, its top (16x16) */
	{ 188, 150, 196, 154, 0, 1023, -1, 0, 0, 0x10 }, /* and its foot (16x8) */
	/* the arrows at the map's edges, which say it scrolls (8x8) */
	{ 0, 64, 12, 80, 0, 1023, -1, 0, 0, 0x1 },
	{ 216, 64, 232, 80, 0, 1023, -1, 0, 0, 0x1 },
	{ 112, 4, 128, 16, 0, 1023, -1, 0, 0, 0x1 },
	{ 112, 124, 128, 140, 0, 1023, -1, 0, 0, 0x1 },
};
static bool bt_map_up(Emu* s) {
	return emu_read8(s, BT_MENU) == 1 && emu_read8(s, BT_PAGE) == 2;
}
/* BT-8. The ITEM page is page 0; its clock, and with START the sunrise and
 * the sunset, are sprites in two colours only: the digits' and letters'
 * brown and their pale yellow outline (nothing else in their band is
 * either). */
static bool bt_item_up(Emu* s) { return emu_read8(s, BT_MENU) == 1 && emu_read8(s, BT_PAGE) == 0; }
#define BT_RGB(r, g, b) ((uint32_t) (b) << 16 | (uint32_t) (g) << 8 | (uint32_t) (r))
#define BT_BAND_X0 0
#define BT_BAND_X1 100 /* (the text box's ")" is beyond it) */
#define BT_BAND_Y0 119
#define BT_BAND_Y1 128
#define BT_PAD 4 /* the space round the lines on the second screen */
#define BT_REFRESH_MS 30000
typedef struct BtCut {
	int w, h;
	uint32_t c[(BT_BAND_X1 - BT_BAND_X0 + 1) * (BT_BAND_Y1 - BT_BAND_Y0 + 1)];
	uint8_t on[(BT_BAND_X1 - BT_BAND_X0 + 1) * (BT_BAND_Y1 - BT_BAND_Y0 + 1)];
} BtCut;
/* (kept from capture to capture: they change by the minute, so the hidden
 * copy visits the ITEM page every BT_REFRESH_MS, not with every Map) */
static BtCut btClock, btRise, btSet;
static long btCutAt = -1;
static long bt_ms(void) {
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (long) (ts.tv_sec * 1000L + ts.tv_nsec / 1000000L);
}
/* The glyph pixels in the band: false if there are fewer than minPix or
 * they are narrower than minW (the clock is 39 wide, the others 88). */
static bool bt_cut(const Emu* s, BtCut* o, int minPix, int minW) {
	const uint32_t brown = BT_RGB(156, 66, 24), edge = BT_RGB(255, 255, 140);
	int x0 = GBA_W, x1 = -1, y0 = GBA_H, y1 = -1, n = 0;
	for (int y = BT_BAND_Y0; y <= BT_BAND_Y1; ++y) {
		for (int x = BT_BAND_X0; x <= BT_BAND_X1; ++x) {
			uint32_t c = s->fb[y * GBA_W + x] & 0xFFFFFF; /* (the fourth byte is unused) */
			if (c != brown && c != edge) continue;
			++n;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	}
	if (n < minPix || x1 - x0 + 1 < minW) return false;
	o->w = x1 - x0 + 1;
	o->h = y1 - y0 + 1;
	memset(o->on, 0, sizeof(o->on));
	for (int y = y0; y <= y1; ++y) {
		for (int x = x0; x <= x1; ++x) {
			uint32_t c = s->fb[y * GBA_W + x], k = c & 0xFFFFFF;
			if (k == brown || k == edge) {
				o->c[(y - y0) * o->w + (x - x0)] = c;
				o->on[(y - y0) * o->w + (x - x0)] = 1;
			}
		}
	}
	return true;
}
static void bt_lay(const BtCut* o, uint32_t* out, int w, int h, int X, int Y) {
	if (o->w <= 0) return;
	for (int y = 0; y < o->h; ++y) {
		for (int x = 0; x < o->w; ++x) {
			int dx = X + x, dy = Y + y;
			if (o->on[y * o->w + x] && dx >= 0 && dx < w && dy >= 0 && dy < h) out[dy * w + dx] = o->c[y * o->w + x];
		}
	}
}
/* The clock at the top left, the sunrise and the sunset stacked at the top
 * right: BT_PAD from the left, the right and the top; the clock on the
 * sunrise's line (the bottoms of their digits level). */
static void bt_lines(uint32_t* out, int w, int h) {
	int lineH = btRise.w > 0 ? btRise.h : btClock.h;
	/* (the sunrise and the sunset keep the game's own columns: left-aligned
	 * with each other, the wider one BT_PAD from the right edge) */
	int blockW = btRise.w > btSet.w ? btRise.w : btSet.w;
	bt_lay(&btClock, out, w, h, BT_PAD, BT_PAD + (lineH > btClock.h ? lineH - btClock.h : 0));
	bt_lay(&btRise, out, w, h, w - BT_PAD - blockW, BT_PAD);
	bt_lay(&btSet, out, w, h, w - BT_PAD - blockW, BT_PAD + (btRise.w > 0 ? btRise.h : btSet.h) + 1);
}
/* From the ITEM page's picture, then START (the sunrise), START (the sunset). */
static void bt_visit_item(Emu* s) {
	for (int tries = 0; tries < 2 && emu_read8(s, BT_PAGE) != 0; ++tries) {
		aw_run(s, K_R, 3);
		for (int f = 0; f < 30 && emu_read8(s, BT_PAGE) != 0; ++f) emu_frame_blind(s, 0);
	}
	if (!aw_settle(s, bt_item_up, 240)) return;
	BtCut c;
	if (bt_cut(s, &c, 40, 30) && c.w < 60) btClock = c;
	/* (the two lines have a label as well: far more pixels than the clock) */
	BtCut* to[2] = { &btRise, &btSet };
	for (int i = 0; i < 2; ++i) {
		aw_run(s, K_START, 3);
		bool got = false;
		for (int f = 0; f < 8 && !got; ++f) {
			emu_frame(s, 0);
			got = bt_cut(s, &c, 150, 70);
		}
		if (!got) return;
		*to[i] = c;
	}
	btCutAt = bt_ms();
}
static const char* bt_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (emu_read8(s, BT_MENU) != 0) return "not playing";
	emu_hide_sprites(s, btPrompts, sizeof(btPrompts) / sizeof(btPrompts[0]));
	const char* err = NULL;
	aw_run(s, K_SELECT, 3);
	for (int f = 0; f < 60 && emu_read8(s, BT_MENU) != 1; ++f) emu_frame_blind(s, 0);
	if (emu_read8(s, BT_MENU) != 1) {
		err = "SELECT opened no menu";
	} else {
		/* (Item: L is the Map; Weapon: R is) */
		for (int tries = 0; tries < 3 && emu_read8(s, BT_PAGE) != 2; ++tries) {
			aw_run(s, emu_read8(s, BT_PAGE) == 0 ? K_L : K_R, 3);
			for (int f = 0; f < 30 && emu_read8(s, BT_PAGE) != 2; ++f) emu_frame_blind(s, 0);
		}
		if (!aw_settle(s, bt_map_up, 240)) err = "the Map did not open";
	}
	if (err) {
		emu_hide_sprites(s, NULL, 0);
		return err;
	}
	for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
	/* BT-8: the lines from the ITEM page beside the Map, now and then (the
	 * prompts' rules off for it) */
	emu_hide_sprites(s, NULL, 0);
	if (btCutAt < 0 || bt_ms() - btCutAt >= BT_REFRESH_MS) bt_visit_item(s);
	bt_lines(out, w, h);
	return NULL;
}

/* ---------------------------------------------------------- Rebelstar: Tactical Command
 *
 * In a battle, with the field cursor free (the player's turn, no menu), the
 * second screen shows the Status screen of the soldier under the cursor, its
 * three pages in turn (RS-3), or, on anything else, the mission's Objectives
 * (RS-4); both opened on the hidden copy from the field (A, Status; START,
 * Objectives). Everything the game does goes through a state object: the
 * word at `top` points to the one running, whose first word is its class
 * (a table of functions, copied to EWRAM: the same address in every
 * battle). The battle's own object keeps its class while the field is up
 * and changes it for the action menu, moving, aiming, the START menu, the
 * enemy's turn...; the screens opened from it (a Status, the Objectives,
 * help, options) are objects of their own pushed over it. The battle
 * object is told by its action list (the halfwords 3, 4 at +0x0C). On it:
 * +0x28 the unit under the field cursor (0xFF none, or one the player
 * can't see), +0x5C the action menu, +0x60 the START menu: a menu's
 * visible items at +0x0C, its cursor at +0x10 (START's: End Turn, Quit
 * Mission, Save Game, Options, Objectives, then Cancel in the story; the
 * action menu's Status always the last but one, before Cancel). Units:
 * 0x48 bytes each from `units`, the side at +0x1C (0 the player's).
 * Europe's release is the same game, its code and data moved (the classes
 * by 0x8E0-0x960, the variables by 0x960 or so) and its menus' labels
 * longer and in five languages, so nothing is looked up by its text.
 * Addresses found with the harness and tools/romxref.py
 * (docs/games/rebelstar-tactical-command.md, "Finding the game's state"). */
typedef struct {
	uint32_t top;    /* the running object */
	uint32_t cursor; /* the field cursor's object (0 outside battles and their deployment) */
	uint32_t units;
	/* classes: the field (the cursor free), a unit's action menu, START's
	 * menu, the enemy's turn, placing soldiers before a battle, a side's
	 * turn banner; pushed over the battle: a Status, the Objectives,
	 * SELECT's help, Options, Quit Mission's QUIT GAME */
	uint32_t field, action, start, enemy, deploy, banner, status, objectives, help, options, quit;
} RsRelease;
static const RsRelease rsUsa = { 0x02013368, 0x02002008, 0x0200F434, 0x02004EF0, 0x02005180, 0x02005F08, 0x02004C20,
                                 0x02004AE8, 0x02003934, 0x02003A78, 0x02005540, 0x020049E8, 0x02005580, 0x02003E38 };
static const RsRelease rsEur = { 0x02013CC8, 0x02001F8C, 0x0200FD94, 0x02005810, 0x02005AA0, 0x02006868, 0x02005540,
                                 0x02005408, 0x02004214, 0x02004358, 0x02005EA0, 0x02005308, 0x02005EE0, 0x02004718 };
#define RS_UNIT_SIZE 0x48
#define RS_OBJECTIVES_ITEM 4 /* START's menu: End Turn, Quit Mission, Save Game, Options, Objectives */
/* virtual addresses (rs_virt) */
#define RS_V_MODE 0xF00000A0  /* RS_OUT, RS_FIELD, RS_BUSY or RS_IDLE */
#define RS_V_KEY 0xF00000A1   /* what the second screen is about (the room key) */
#define RS_V_TUTOR 0xF00000A2 /* 1 while a training mission's green box is up */
enum { RS_OUT, RS_FIELD, RS_BUSY, RS_IDLE };
#define RS_OBJ_KEY 0xFE /* rs_subject: no soldier of the player's under the cursor */

static bool rs_ewram(uint32_t p) {
	return p >= 0x02000000 && p < 0x0203FF00;
}
static uint32_t rs_vt(const RsRelease* r, Emu* e) {
	uint32_t p = emu_read32(e, r->top);
	return rs_ewram(p) ? emu_read32(e, p) : 0;
}
static int rs_mode(const RsRelease* r, Emu* e) {
	uint32_t p = emu_read32(e, r->top);
	if (!rs_ewram(p)) return RS_OUT;
	uint32_t vt = emu_read32(e, p);
	/* the enemy's turn and the turn banners: the logo, the pictures kept
	 * for the player's turn (RS-6) */
	if (vt == r->enemy || vt == r->banner) return RS_IDLE;
	if (emu_read32(e, p + 0x0C) == 0x00040003) {
		if (vt == r->field) return RS_FIELD;
		return vt == r->deploy ? RS_OUT : RS_BUSY;
	}
	/* screens opened over the battle: the last pictures stay (RS-5) */
	if ((vt == r->status || vt == r->objectives || vt == r->help || vt == r->options || vt == r->quit) &&
	    rs_ewram(emu_read32(e, r->cursor)))
		return RS_BUSY;
	return RS_OUT;
}
/* the unit under the field cursor if it's one of the player's soldiers,
 * else RS_OBJ_KEY (an empty square, an enemy) */
static int rs_subject(const RsRelease* r, Emu* e) {
	uint32_t p = emu_read32(e, r->top);
	if (!rs_ewram(p)) return RS_OBJ_KEY;
	uint8_t u = emu_read8(e, p + 0x28);
	if (u >= 16) return RS_OBJ_KEY;
	return emu_read8(e, r->units + u * RS_UNIT_SIZE + 0x1C) == 0 ? u : RS_OBJ_KEY;
}
/* A training mission's instructions: a green box on BG1, its inside tiles
 * all colour 4 of palette 8 (green; the unit and menu boxes on the same
 * layer and palette are filled with colour 1, blue). Their tiles are
 * wherever the game puts them, so the tiles themselves are looked at. */
static bool rs_green_box(Emu* e) {
	if (!(emu_io16(e, 0x00) & 0x0200)) return false;
	uint16_t cnt = emu_io16(e, 0x0A);
	if (cnt & 0x80) return false; /* (256 colours: not the battle's boxes) */
	uint16_t green = emu_read16(e, 0x05000000 + (8 * 16 + 4) * 2);
	int r = green & 31, g = (green >> 5) & 31, b = (green >> 10) & 31;
	if (!(g > r + 4 && g > b + 4)) return false;
	uint32_t map = 0x06000000 + ((cnt >> 8) & 0x1F) * 0x800, chr = 0x06000000 + ((cnt >> 2) & 3) * 0x4000;
	int n = 0;
	for (int i = 0; i < 32 * 20; ++i) {
		uint16_t m = emu_read16(e, map + i * 2);
		if (m >> 12 != 8 || !(m & 0x3FF)) continue;
		uint32_t t = chr + (m & 0x3FF) * 32;
		bool fill = true;
		for (int k = 0; k < 32 && fill; k += 4) fill = emu_read32(e, t + k) == 0x44444444u;
		if (fill && ++n >= 4) return true;
	}
	return false;
}
static uint32_t rs_virt_for(const RsRelease* r, Emu* e, uint32_t addr) {
	if (addr == RS_V_MODE) return rs_mode(r, e);
	if (addr == RS_V_TUTOR) return rs_mode(r, e) == RS_FIELD && rs_green_box(e);
	if (addr == RS_V_KEY) {
		int m = rs_mode(r, e);
		return m == RS_FIELD ? (uint32_t) rs_subject(r, e) : 0x100u + m;
	}
	return 0;
}
static uint32_t rs_virt(Emu* e, uint32_t addr) {
	return rs_virt_for(&rsUsa, e, addr);
}
static uint32_t rs_virt_eur(Emu* e, uint32_t addr) {
	return rs_virt_for(&rsEur, e, addr);
}

static void rs_tap(Emu* s, uint32_t keys) {
	aw_run(s, keys, 2);
	aw_run(s, 0, 4);
}
/* up to n frames until the running object's class is vt */
static bool rs_wait_vt(const RsRelease* r, Emu* s, uint32_t vt, int n) {
	for (int f = 0; f < n && rs_vt(r, s) != vt; ++f) emu_frame_blind(s, 0);
	return rs_vt(r, s) == vt;
}
/* up to n frames until the picture has held still for 3 frames (a screen
 * fades in over a few) */
static bool rs_settle(Emu* s, int n) {
	static uint32_t prev[GBA_W * GBA_H];
	int still = 0;
	emu_frame(s, 0);
	for (int f = 0; f < n && still < 3; ++f) {
		memcpy(prev, s->fb, sizeof(prev));
		emu_frame(s, 0);
		still = memcmp(prev, s->fb, sizeof(prev)) ? 0 : still + 1;
	}
	return still >= 3;
}
/* a menu (at the battle object's +off) moved to item `want` */
static bool rs_menu_to(Emu* s, uint32_t m, int want) {
	for (int i = 0; i < 16 && emu_read16(s, m + 0x10) != want; ++i) {
		rs_tap(s, emu_read16(s, m + 0x10) < want ? K_DOWN : K_UP);
		if (emu_cancelled(s)) return false;
	}
	return emu_read16(s, m + 0x10) == want;
}
static void rs_copy(Emu* s, uint32_t* out, int w, int h) {
	for (int y = 0; y < h && y < GBA_H; ++y)
		for (int x = 0; x < w && x < GBA_W; ++x) out[y * w + x] = s->fb[y * GBA_W + x] | 0xFF000000;
}
/* the page as drawn, without BG1: the D-pad marks in its top corners that
 * turn the pages (RS-3), BG1's only tiles there. */
static void rs_copy_page(Emu* s, uint32_t* out, int w, int h) {
	/* (the renderer takes the layers on as the display registers are
	 * written: a state saved and loaded again writes them all) */
	size_t sz = emu_state_size(s);
	void* st = malloc(sz);
	if (!st || !emu_state_save(s, st)) {
		free(st);
		rs_copy(s, out, w, h);
		return;
	}
	emu_layers(s, 0x1D);
	emu_state_load(s, st);
	emu_frame(s, 0);
	rs_copy(s, out, w, h);
	emu_layers(s, 0x1F);
	emu_state_load(s, st);
	free(st);
}
/* RS-3: A on the soldier, Status (the action menu's last item but one),
 * then its pages: Equipment, Attributes (RIGHT), Skills (RIGHT) */
static const char* rs_status(const RsRelease* r, Emu* s, uint32_t* const* out, int max, int* nOut, int w, int h) {
	aw_run(s, K_A, 2);
	if (!rs_wait_vt(r, s, r->action, 30)) return "the action menu didn't open";
	/* (the menu's own object is set up a frame or two after the class
	 * changes) */
	uint32_t m = 0;
	int n = 0;
	for (int f = 0; f < 20; ++f) {
		m = emu_read32(s, emu_read32(s, r->top) + 0x5C);
		n = rs_ewram(m) ? emu_read16(s, m + 0x0C) : 0;
		if (n >= 3 && n <= 16) break;
		emu_frame_blind(s, 0);
	}
	if (!rs_ewram(m)) return "no action menu";
	if (n < 3 || n > 16) return "not an action menu";
	if (!rs_menu_to(s, m, n - 2)) return "the menu's cursor didn't move";
	aw_run(s, K_A, 2);
	if (!rs_wait_vt(r, s, r->status, 30)) return "the Status didn't open";
	if (!rs_settle(s, 40)) return "the Status didn't settle";
	if (emu_cancelled(s)) return "called off";
	int pages = max < 3 ? max : 3;
	rs_copy_page(s, out[0], w, h);
	for (int k = 1; k < pages; ++k) {
		aw_run(s, K_RIGHT, 2);
		aw_run(s, 0, 4);
		if (!rs_settle(s, 40)) return "a page didn't settle";
		if (rs_vt(r, s) != r->status) return "the Status closed";
		if (emu_cancelled(s)) return "called off";
		rs_copy_page(s, out[k], w, h);
	}
	*nOut = pages;
	return NULL;
}
/* RS-4: START, Objectives (its fifth item, in every language; the class
 * of the screen it opens says it was) */
static const char* rs_objectives(const RsRelease* r, Emu* s, uint32_t* out, int w, int h) {
	aw_run(s, K_START, 2);
	if (!rs_wait_vt(r, s, r->start, 30)) return "START's menu didn't open";
	uint32_t m = 0;
	for (int f = 0; f < 20; ++f) {
		m = emu_read32(s, emu_read32(s, r->top) + 0x60);
		if (rs_ewram(m) && emu_read16(s, m + 0x0C) > RS_OBJECTIVES_ITEM && emu_read16(s, m + 0x0C) <= 8) break;
		emu_frame_blind(s, 0);
	}
	if (!rs_ewram(m)) return "no START menu";
	if (emu_read16(s, m + 0x0C) <= RS_OBJECTIVES_ITEM) return "no Objectives in START's menu";
	if (!rs_menu_to(s, m, RS_OBJECTIVES_ITEM)) return "the menu's cursor didn't move";
	aw_run(s, K_A, 2);
	if (!rs_wait_vt(r, s, r->objectives, 30)) return "the Objectives didn't open";
	if (!rs_settle(s, 40)) return "the Objectives didn't settle";
	if (emu_cancelled(s)) return "called off";
	rs_copy(s, out, w, h);
	return NULL;
}
static const char* rs_compose_for(const RsRelease* r, Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	int max = *nOut;
	*nOut = 1;
	*step = 150; /* 2.5 seconds a page */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (rs_mode(r, s) != RS_FIELD) return "not the field";
	/* (a training mission's instructions: the game waits for what they
	 * ask, RS-7) */
	if (rs_green_box(s)) return "a training box is up";
	return rs_subject(r, s) == RS_OBJ_KEY ? rs_objectives(r, s, out[0], w, h) : rs_status(r, s, out, max, nOut, w, h);
}
static const char* rs_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	return rs_compose_for(&rsUsa, s, state, out, nOut, step, w, h);
}
static const char* rs_compose_eur(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	return rs_compose_for(&rsEur, s, state, out, nOut, step, w, h);
}

/* Shin Megami Tensei (Japan, and Gymzatan's translation of it): the game's
 * mode byte (0 title and file lists, 1 walking, 0xA the command bar and START
 * menu, 0x10 the map, 0x15 a fight, 0x19 the name entry), the party's place
 * (x, y; the facing 0 N, 1 E, 2 S, 3 W) and the area's number */
#define SMT_MODE   0x03004470
#define SMT_FACING 0x03004472
#define SMT_X      0x03004482
#define SMT_Y      0x03004484
#define SMT_AREA   0x030044EC
/* the automap's scroll: steps from where it opened (UP +1, DOWN -1, 8 rows
 * each; signed, 32 bits) */
#define SMT_SCROLL 0x0200BFCC

/* The second screen is the automap taller than the game's window of it: its view
 * (rows 0-87 of the screen, between the side bars) scrolled with the game's
 * own UP and DOWN on the hidden copy, the pieces put together by the
 * counter above, over the game's own backdrop (SMT-8).
 *
 * The screen's layers: BG3 the backdrop (blue grid, side bars, the party's
 * panels), BG2 the map itself, BG1 the compass's box, BG0 the floor's name
 * bar, sprites the map's markers (the party, the doors...) and all the text.
 * So a view with the layers BG2, BG3 and sprites on, minus the backdrop
 * (BG3 alone), is the map and its markers, nothing else. */
#define SMT_H      160  /* the picture's rows: a GBA screen, as the overlay's window is */
#define SMT_VIEW   88   /* the game's view: rows 0..87 */
#define SMT_AREA_H 145  /* the picture's map part; the floor's name bar (15 rows) under it */
#define SMT_ABOVE  29   /* rows above the game's own view (57 more to share with below) */
#define SMT_STEPS  4

static int32_t smt_scroll(Emu* s) { return (int32_t)emu_read32(s, SMT_SCROLL); }

/* the pixel at (x, y) is this colour (0xRRGGBB), give or take */
static bool smt_px(const uint32_t* fb, int x, int y, uint32_t rgb) {
	uint32_t c = fb[y * GBA_W + x];
	int r = c & 0xFF, g = (c >> 8) & 0xFF, b = (c >> 16) & 0xFF;
	return abs(r - (int)((rgb >> 16) & 0xFF)) <= 8 && abs(g - (int)((rgb >> 8) & 0xFF)) <= 8 && abs(b - (int)(rgb & 0xFF)) <= 8;
}

static void smt_layers(Emu* s, void* st, unsigned mask, uint32_t* dst);

/* the picture's view (map, markers and backdrop) the same twice running,
 * `gap` frames apart, after `wait` frames in which it can't be done yet:
 * drawn to the end, the markers at their places (they lagged a step behind
 * the map when a press came in mid-step) */
static bool smt_settle(Emu* s, void* st, uint32_t* a, uint32_t* b, int wait, int gap) {
	int same = 0;
	for (int i = 0; i < wait; ++i) emu_frame_blind(s, 0);
	for (int f = 0; f < 40 && same < 2; ++f) {
		if (f) for (int i = 0; i < gap; ++i) emu_frame_blind(s, 0);
		smt_layers(s, st, 0x1C, f & 1 ? a : b);
		if (f > 0 && !memcmp(a, b, (size_t)SMT_VIEW * GBA_W * sizeof(uint32_t))) ++same;
		else same = 0;
	}
	return same >= 2;
}

/* L from the first-person view, and the map drawn to its last tile: the map
 * layer (and the backdrop) the same twice running, 8 frames apart. (The game
 * draws it over about 52 frames, later when the press came in mid-step, so a
 * fixed wait gave half-drawn maps.) `st` is the state buffer, `a` and `b`
 * two spare frames. */
static const char* smt_open(Emu* s, const void* state, void* st, uint32_t* a, uint32_t* b) {
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame_blind(s, 0);
	if (emu_read8(s, SMT_MODE) != 0x01) return "not walking";
	/* a step or a turn still going on is let finish first (the copy gets no
	 * keys): the position and facing the same for 20 frames. Opened mid-step
	 * the markers moved between one piece and the next. */
	{
		uint32_t at = 0;
		int still = 0;
		for (int f = 0; f < 90 && still < 20; ++f) {
			emu_frame_blind(s, 0);
			uint32_t now = emu_read16(s, SMT_X) | (uint32_t)emu_read16(s, SMT_Y) << 16 | (uint32_t)emu_read8(s, SMT_FACING) << 8;
			still = f > 0 && now == at ? still + 1 : 0;
			at = now;
		}
		if (still < 20 || emu_read8(s, SMT_MODE) != 0x01) return "not standing still";
	}
	for (int i = 0; i < 4; ++i) emu_frame_blind(s, K_L);
	/* nothing by 20 frames: no automap here (the first dungeon, before the
	 * app; a town): a quick failure (SMT-4) */
	int f = 0;
	while (f < 20 && emu_read8(s, SMT_MODE) != 0x10) emu_frame_blind(s, 0), ++f;
	if (emu_read8(s, SMT_MODE) != 0x10) return "map button did nothing";
	if (!smt_settle(s, st, a, b, 36, 4)) return "map did not finish drawing";
	emu_frame(s, 0);
	if (!smt_px(s->fb, 216, 98, 0x73738C)) return "map screen did not appear";
	return NULL;
}

/* one step of the scroll, key UP or DOWN: pressed until the game's counter
 * moves (a press during the last step's slide is lost), then 12 frames for
 * the slide to end; false at the end of the map's scroll */
static bool smt_step(Emu* s, uint32_t key, void* st, uint32_t* a, uint32_t* b) {
	int32_t before = smt_scroll(s);
	for (int tries = 0; tries < 3; ++tries) {
		for (int i = 0; i < 4; ++i) emu_frame_blind(s, key);
		for (int i = 0; i < 24 && smt_scroll(s) == before; ++i) emu_frame_blind(s, 0);
		if (smt_scroll(s) != before) return smt_settle(s, st, a, b, 10, 3);
	}
	return false;
}

/* what the screen shows with only the layers in `mask` drawn, from the state
 * as it is (taken and put back) */
static void smt_layers(Emu* s, void* st, unsigned mask, uint32_t* dst) {
	emu_state_save(s, st);
	emu_layers(s, mask);
	emu_state_load(s, st);
	emu_frame(s, 0);
	memcpy(dst, s->fb, (size_t)GBA_W * GBA_H * sizeof(uint32_t));
	emu_layers(s, 0x1F);
	emu_state_load(s, st);
}

static const char* smt_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	const size_t n = (size_t)GBA_W * GBA_H;
	if (w != GBA_W || h != SMT_H) return "wrong picture size";
	void* st = malloc(emu_state_size(s)), *opened = malloc(emu_state_size(s));
	uint32_t* fr = malloc((2 + 1 + 2 * SMT_STEPS + 2) * n * sizeof(uint32_t));
	if (!st || !opened || !fr) { free(st); free(opened); free(fr); return "out of memory"; }
	uint32_t *full = fr, *back = fr + n, *views = fr + 2 * n; /* views[k]: shift[k] rows */
	uint32_t *sa = fr + (3 + 2 * SMT_STEPS) * n, *sb = sa + n; /* spare */
	int shift[1 + 2 * SMT_STEPS], nv = 0;
	const char* err = smt_open(s, state, st, sa, sb);
	if (!err) {
		int32_t c0 = smt_scroll(s);
		emu_state_save(s, opened); /* the map as it opened, for the way down */
		smt_layers(s, st, 0x1F, full);
		smt_layers(s, st, 0x08, back);
		smt_layers(s, st, 0x1C, views), shift[nv++] = 0;
		for (int k = 1; k <= SMT_STEPS && smt_step(s, K_UP, st, sa, sb); ++k) {
			smt_layers(s, st, 0x1C, views + (size_t)nv * n);
			shift[nv++] = 8 * (smt_scroll(s) - c0);
		}
		/* back to the start, and as far down */
		if (!emu_state_load(s, opened)) err = "state load failed";
		for (int k = 1; !err && k <= SMT_STEPS && smt_step(s, K_DOWN, st, sa, sb); ++k) {
			smt_layers(s, st, 0x1C, views + (size_t)nv * n);
			shift[nv++] = 8 * (smt_scroll(s) - c0);
		}
	}
	if (!err && emu_cancelled(s)) err = "cancelled";
	if (!err) {
		/* the backdrop: the game's own rows 0-87, and past them its top band
		 * (rows 0-7, the grid's cells are 8 rows) repeated above and its
		 * bottom band (rows 80-87) repeated below */
		for (int t = 0; t < SMT_AREA_H; ++t) {
			int r = t - SMT_ABOVE;
			if (r < 0) r = ((r % 8) + 8) % 8;
			else if (r >= SMT_VIEW) r = 80 + (r - SMT_VIEW) % 8;
			memcpy(&out[t * w], &back[r * GBA_W], GBA_W * sizeof(uint32_t));
		}
		/* the map, and its markers: the 8x8 tiles of each view with a pixel
		 * that isn't blue-dominant (the map's greens and teals, the markers),
		 * where they differ from the backdrop. The map layer's empty tiles are
		 * the backdrop's own grid, wrapped round from other rows, in shades
		 * all blue-dominant. The compass's corner is left to the compass. */
		for (int k = 0; k < nv; ++k) {
			const uint32_t* v = views + (size_t)k * n;
			for (int cy = 0; cy < SMT_VIEW / 8; ++cy) {
				for (int cx = 0; cx < GBA_W / 8; ++cx) {
					bool map = false;
					for (int y = 0; y < 8 && !map; ++y) {
						for (int x = 0; x < 8; ++x) {
							uint32_t c = v[(cy * 8 + y) * GBA_W + cx * 8 + x];
							if (((c >> 8) & 0xFF) >= ((c >> 16) & 0xFF) && c != back[(cy * 8 + y) * GBA_W + cx * 8 + x]) { map = true; break; }
						}
					}
					if (!map) continue;
					for (int y = 0; y < 8; ++y) {
						int r = cy * 8 + y, t = r - shift[k] + SMT_ABOVE;
						if (t < 0 || t >= SMT_AREA_H) continue;
						for (int x = cx * 8; x < cx * 8 + 8; ++x) {
							if (x >= 182 && x < 223 && r >= 3 && r < 42) continue;
							uint32_t c = v[r * GBA_W + x];
							if (c != back[r * GBA_W + x]) out[t * w + x] = c;
						}
					}
				}
			}
		}
		/* the compass, at the top; the floor's name under the map */
		for (int r = 4; r <= 40; ++r) memcpy(&out[r * w + 183], &full[r * GBA_W + 183], 39 * sizeof(uint32_t)); /* its box, exactly */
		for (int r = 0; r < SMT_H - SMT_AREA_H; ++r) memcpy(&out[(SMT_AREA_H + r) * w], &full[(SMT_VIEW + 2 + r) * GBA_W], GBA_W * sizeof(uint32_t));
	}
	free(st);
	free(opened);
	free(fr);
	return err;
}

/* ---------------------------------------------------------------- Breath of Fire
 *
 * docs/games/breath-of-fire.md. On the overworld the second screen is the
 * world map START opens there (BOF-3); everywhere else it's SELECT's menu
 * as a party overview, without its command list (BOF-4); both opened on the
 * hidden copy at every refresh. Battles, the title and its intro: the logo
 * (BOF-2, BOF-5); where neither key opens anything the last picture stays
 * (BOF-6). All four releases (ABFE, ABFP, ABFJ, ABFX) draw the same
 * screens in the same places, so nothing here reads the game's memory:
 * what the game is showing comes from the display's registers.
 *   display (DISPCNT)  0x1200 the intro and title (BG1 and sprites), 0x1F00
 *                      walking about and the menus, 0x3F00 a battle (window 0
 *                      on too), 0x1401 the map (mode 1)
 *   BG0CNT             0x4405 walking about, 0x0008 the menus (SELECT's
 *                      pages, the file menu, shops)
 */
#define BOF_DISP_TITLE 0x1200
#define BOF_DISP_FIELD 0x1F00
#define BOF_DISP_BATTLE 0x3F00
#define BOF_DISP_MAP 0x1401
#define BOF_BG0_FIELD 0x4405
#define BOF_BG0_MENU 0x0008
/* 1 on the title and its intro and in a battle: the logo (BOF-2, BOF-5) */
#define BOF_V_LOGO 0xF0000090
/* which screen it is, for a quick refresh when it changes: 1 walking about,
 * 2 a menu, 3 the map, 4 the title or a battle, 0 anything else (fades) */
#define BOF_V_SCREEN 0xF0000091
/* the party overview, faded in: the white of the frame between the place's
 * window and the gold's (only the first page of SELECT's menu has it), and
 * the frames of the command window and the top (x, y, 0xRRGGBB) */
static const int bofParty[][3] = { { 160, 2, 0xFFF9E7 }, { 160, 4, 0xFFFFE7 }, { 160, 8, 0xFFFFEA },
                                   { 159, 4, 0xF0D2AC }, { 48, 60, 0xFFF9E7 }, { 120, 0, 0xFFFFED } };
/* the party window's left edge (the command list is to its left) */
#define BOF_PARTY_X 47

/* the last party overview taken (BOF-5): a battle shows it, as the overview can't
 * be opened there, and the map's capture takes one too */
static struct { uint32_t* px; int w, h; volatile bool ok; } bofKept;
static void bof_keep(const uint32_t* out, int w, int h) {
	if (!bofKept.px || bofKept.w != w || bofKept.h != h) {
		bofKept.ok = false;
		free(bofKept.px);
		bofKept.px = malloc((size_t)w * h * sizeof(uint32_t));
		bofKept.w = w, bofKept.h = h;
	}
	if (!bofKept.px) return;
	memcpy(bofKept.px, out, (size_t)w * h * sizeof(uint32_t));
	bofKept.ok = true;
}

static uint32_t bof_virt(Emu* e, uint32_t addr) {
	uint16_t disp = emu_io16(e, 0x00);
	/* the logo on the title, which also forgets the party overview kept for battles
	 * (BOF-5); a battle is taken like the field (the kept overview) */
	if (addr == BOF_V_LOGO) {
		if (disp == BOF_DISP_TITLE) bofKept.ok = false;
		return disp == BOF_DISP_TITLE;
	}
	if (addr != BOF_V_SCREEN) return 0;
	if (disp == BOF_DISP_FIELD) return emu_io16(e, 0x08) == BOF_BG0_MENU ? 2 : emu_io16(e, 0x08) == BOF_BG0_FIELD ? 1 : 0;
	return disp == BOF_DISP_MAP ? 3 : disp == BOF_DISP_TITLE || disp == BOF_DISP_BATTLE ? 4 : 0;
}

static bool bof_bright(Emu* s) {
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		uint32_t c = s->fb[i];
		if ((c & 0xFF) > 0xC0 || ((c >> 8) & 0xFF) > 0xC0 || ((c >> 16) & 0xFF) > 0xC0) return true;
	}
	return false;
}

static bool bof_party_up(Emu* s) {
	return emu_io16(s, 0x00) == BOF_DISP_FIELD && ff_probes(s, bofParty, sizeof(bofParty) / sizeof(*bofParty));
}

/* runs until the picture has been still for 3 looks 4 frames apart, up
 * on the screen asked for (the map, or the party overview) and not black (the
 * map fades out and in again as it opens); false if it never was */
static bool bof_settled(Emu* s, bool map, int n) {
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < n && still < 3; f += 4) {
		aw_run(s, 0, 4);
		if (emu_cancelled(s)) return false;
		bool up = map ? emu_io16(s, 0x00) == BOF_DISP_MAP : bof_party_up(s);
		still = up && !memcmp(last, s->fb, sizeof(last)) && bof_bright(s) ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	return still >= 3;
}

/* the party overview, from SELECT's menu (B first, as often as it takes to leave one
 * of its pages or a conversation: BOF-6, BOF-7) */
static const char* bof_party(Emu* s, bool menuOpen, uint32_t* out, int w, int h) {
	bool up = bof_party_up(s);
	if (!up && menuOpen) {
		for (int f = 0; f < 150 && !up; f += 2) aw_run(s, 0, 2), up = bof_party_up(s);
	}
	for (int tries = 0, n = menuOpen ? 3 : 2; tries < n && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
			up = bof_party_up(s);
			if (up) break;
		}
		aw_run(s, K_SELECT, 4);
		for (int f = 0; f < 150 && !up; f += 2) aw_run(s, 0, 2), up = bof_party_up(s);
		if (emu_cancelled(s)) return "called off";
	}
	if (!up) return "the menu didn't open"; /* (BOF-6) */
	if (!bof_settled(s, false, 120)) return "the menu didn't settle";
	if (!bof_party_up(s)) return "the menu closed";
	/* the party window alone (BOF-4), in the middle of the screen */
	int cw = GBA_W - BOF_PARTY_X, left = (w - cw) / 2;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			out[y * w + x] = y < GBA_H && x >= left && x < left + cw ? s->fb[y * GBA_W + BOF_PARTY_X + x - left] : 0xFF000000;
		}
	}
	bof_keep(out, w, h);
	return NULL;
}


static const char* bof_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	uint16_t disp = emu_io16(s, 0x00);
	if (disp == BOF_DISP_TITLE) return "not walking about";
	/* a battle: the overview kept from before it (BOF-5) */
	if (disp == BOF_DISP_BATTLE) {
		if (!bofKept.ok || bofKept.w != w || bofKept.h != h) return "no party overview yet";
		memcpy(out, bofKept.px, (size_t)w * h * sizeof(uint32_t));
		return NULL;
	}
	/* the map open in the game: back to the field with B (BOF-7), which
	 * fades out and in again */
	bool wasMap = disp == BOF_DISP_MAP;
	if (wasMap) {
		aw_run(s, K_B, 4);
		for (int f = 0; f < 240 && !(emu_io16(s, 0x00) == BOF_DISP_FIELD && emu_io16(s, 0x08) == BOF_BG0_FIELD); f += 2) aw_run(s, 0, 2);
		if (emu_io16(s, 0x00) != BOF_DISP_FIELD) return "the map didn't close";
		aw_run(s, 0, 10);
	}
	bool menuOpen = emu_io16(s, 0x00) == BOF_DISP_FIELD && emu_io16(s, 0x08) == BOF_BG0_MENU;
	/* the world map: START, on the overworld (BOF-3); not with the map or
	 * a menu open in the game (BOF-7) */
	if (!wasMap && !menuOpen) {
		aw_run(s, K_START, 4);
		for (int f = 0; f < 90 && emu_io16(s, 0x00) != BOF_DISP_MAP; f += 2) aw_run(s, 0, 2);
		if (emu_io16(s, 0x00) == BOF_DISP_MAP) {
			if (!bof_settled(s, true, 300)) return "the map didn't settle";
			for (int y = 0; y < h && y < GBA_H; ++y) memcpy(&out[y * w], &s->fb[y * GBA_W], (w < GBA_W ? w : GBA_W) * sizeof(uint32_t));
			/* and the overview, for a battle that starts while the map is up
			 * (BOF-5): B, then SELECT; the map stays as it is if it fails */
			static uint32_t* scratch;
			static int scratchN;
			if (scratchN < w * h) {
				free(scratch);
				scratch = malloc((size_t)w * h * sizeof(uint32_t));
				scratchN = scratch ? w * h : 0;
			}
			aw_run(s, K_B, 4);
			for (int f = 0; f < 240 && !(emu_io16(s, 0x00) == BOF_DISP_FIELD && emu_io16(s, 0x08) == BOF_BG0_FIELD); f += 2) aw_run(s, 0, 2);
			if (scratch && !emu_cancelled(s) && emu_io16(s, 0x00) == BOF_DISP_FIELD) {
				aw_run(s, 0, 10);
				bof_party(s, false, scratch, w, h);
			}
			return NULL;
		}
	}
	if (emu_cancelled(s)) return "called off";
	return bof_party(s, menuOpen, out, w, h);
}

/* ---------------------------------------------------------------- Breath of Fire II
 *
 * docs/games/breath-of-fire-ii.md. The second screen is SELECT's menu as a
 * party overview, without its command list (BOF2-3), opened on the hidden copy at
 * every refresh. Battles: the last overview taken (BOF2-4); the title and its intro:
 * the logo (BOF2-2); where SELECT opens nothing the last picture stays (BOF2-5).
 * The three releases (AB2E, AB2P, AB2J) draw the same screens in the same places,
 * so nothing here reads the game's memory: what the game is showing comes from the
 * background layers' control registers (the display register changes with the map).
 *   BG0CNT  0x1C03 the title, its demo and the intro's story text, 0x4405 walking
 *           about (and a conversation, and START's world map), 0x4205 the overworld, 0x0008 the camp menu and its pages (and
 *           the file menu and the set-up screens), 0x020C a battle
 */
#define BOF2_BG0_TITLE 0x1C03
#define BOF2_BG0_FIELD 0x4405
/* the overworld: the same menu opens there (BOF2-3) */
#define BOF2_BG0_WORLD 0x4205
#define BOF2_BG0_MENU 0x0008
#define BOF2_BG0_BATTLE 0x020C
/* 1 on the title (BOF2-2) */
/* BG3CNT: the world map START opens (BOF2-3); walking about has 0x4207 */
#define BOF2_BG3_MAP 0x4204
#define BOF2_V_LOGO 0xF0000090
/* which screen it is, for a quick refresh when it changes: 1 walking about, 2 a menu,
 * 3 a battle, 4 the title, 5 the overworld, 6 the world map
 * open in the game, 0 anything else (fades, the intro) */
#define BOF2_V_SCREEN 0xF0000091
/* the party overview, faded in: the frame of the zenny's window at the top (x, y,
 * 0xRRGGBB), which only the first page of the menu has */
static const int bof2Party[][3] = { { 159, 4, 0xB7A5B9 }, { 159, 8, 0xD0B0B4 }, { 159, 12, 0xD0B0B4 },
                                    { 160, 4, 0xF5F2F6 }, { 160, 8, 0xFFFEF5 }, { 160, 10, 0xFFFFFB } };
/* the party window's left edge (the command list is to its left) */
#define BOF2_PARTY_X 48

/* the last party overview taken (BOF2-4): a battle shows it, as the overview can't be
 * opened there */
static struct { uint32_t* px; int w, h; volatile bool ok; } bof2Kept;
static void bof2_keep(const uint32_t* out, int w, int h) {
	if (!bof2Kept.px || bof2Kept.w != w || bof2Kept.h != h) {
		bof2Kept.ok = false;
		free(bof2Kept.px);
		bof2Kept.px = malloc((size_t)w * h * sizeof(uint32_t));
		bof2Kept.w = w, bof2Kept.h = h;
	}
	if (!bof2Kept.px) return;
	memcpy(bof2Kept.px, out, (size_t)w * h * sizeof(uint32_t));
	bof2Kept.ok = true;
}

static uint32_t bof2_virt(Emu* e, uint32_t addr) {
	uint16_t bg0 = emu_io16(e, 0x08);
	/* the logo on the title, which also forgets the party overview kept for battles
	 * (BOF2-4) */
	if (addr == BOF2_V_LOGO) {
		if (bg0 == BOF2_BG0_TITLE) bof2Kept.ok = false;
		return bg0 == BOF2_BG0_TITLE;
	}
	if (addr != BOF2_V_SCREEN) return 0;
	if (bg0 == BOF2_BG0_FIELD) return emu_io16(e, 0x0E) == BOF2_BG3_MAP ? 6 : 1;
	return bg0 == BOF2_BG0_WORLD ? 5 : bg0 == BOF2_BG0_MENU ? 2 : bg0 == BOF2_BG0_BATTLE ? 3 : bg0 == BOF2_BG0_TITLE ? 4 : 0;
}

static bool bof2_party_up(Emu* s) {
	return emu_io16(s, 0x08) == BOF2_BG0_MENU && ff_probes(s, bof2Party, sizeof(bof2Party) / sizeof(*bof2Party));
}

/* runs until the picture has been still for 3 looks 4 frames apart, with the overview up
 * and not black (the menu fades in); false if it never was */
static bool bof2_settled(Emu* s, int n) {
	/* only the party window is compared: the icon above the command list moves */
	static uint32_t last[GBA_W * GBA_H];
	int still = 0;
	memset(last, 0, sizeof(last));
	for (int f = 0; f < n && still < 3; f += 4) {
		aw_run(s, 0, 4);
		if (emu_cancelled(s)) return false;
		bool same = true;
		for (int y = 0; y < GBA_H && same; ++y) same = !memcmp(last + y * GBA_W + BOF2_PARTY_X, s->fb + y * GBA_W + BOF2_PARTY_X, (GBA_W - BOF2_PARTY_X) * sizeof(uint32_t));
		still = bof2_party_up(s) && same && bof_bright(s) ? still + 1 : 0;
		memcpy(last, s->fb, sizeof(last));
	}
	return still >= 3;
}

/* the world map START opens on the overworld: up when BG3 has the map's setup; it fades in
 * (the top left corner, sea, stops changing) and then only its marker pulses and the towns
 * twinkle */
static bool bof2_map_up(Emu* s) {
	return emu_io16(s, 0x0E) == BOF2_BG3_MAP;
}

/* the marker (a dark disc) grows and shrinks over about 24 frames; its pixels are the
 * darker ones the map otherwise has none of */
static int bof2_dark(const uint32_t* fb) {
	int n = 0;
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		uint32_t c = fb[i];
		uint32_t a = c & 0xFF, b = (c >> 8) & 0xFF, d = (c >> 16) & 0xFF;
		uint32_t mx = a > b ? a : b;
		if (d > mx) mx = d;
		n += mx < 0x80 && mx > 0x20;
	}
	return n;
}

/* the map once faded in, with the marker at its largest over a cycle (BOF2-3): into out
 * (w by h, the game's 240 by 160); false if it never faded in */
static bool bof2_map_take(Emu* s, uint32_t* out, int w, int h) {
	static uint32_t best[GBA_W * GBA_H];
	uint32_t corner = 0;
	int still = 0;
	for (int f = 0; f < 300 && still < 3; f += 4) {
		aw_run(s, 0, 4);
		if (emu_cancelled(s)) return false;
		still = bof2_map_up(s) && s->fb[0] == corner && corner != 0xFF000000 ? still + 1 : 0;
		corner = s->fb[0];
	}
	if (still < 3) return false;
	int bestDark = -1;
	for (int f = 0; f < 40; f += 4) {
		aw_run(s, 0, 4);
		if (emu_cancelled(s) || !bof2_map_up(s)) return false;
		int d = bof2_dark(s->fb);
		if (d > bestDark) bestDark = d, memcpy(best, s->fb, sizeof(best));
	}
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) out[y * w + x] = y < GBA_H && x < GBA_W ? best[y * GBA_W + x] : 0xFF000000;
	}
	return true;
}

/* the party overview, from SELECT's menu (B first, as often as it takes to leave one of its
 * pages or a conversation: BOF2-5, BOF2-6) */
static const char* bof2_party(Emu* s, bool menuOpen, uint32_t* out, int w, int h) {
	bool up = bof2_party_up(s);
	if (!up && menuOpen) {
		for (int f = 0; f < 150 && !up; f += 2) aw_run(s, 0, 2), up = bof2_party_up(s);
	}
	for (int tries = 0, n = menuOpen ? 3 : 2; tries < n && !up; ++tries) {
		if (tries) {
			aw_run(s, K_B, 4);
			aw_run(s, 0, 40);
			up = bof2_party_up(s);
			if (up) break;
		}
		aw_run(s, K_SELECT, 4);
		for (int f = 0; f < 150 && !up; f += 2) aw_run(s, 0, 2), up = bof2_party_up(s);
		if (emu_cancelled(s)) return "called off";
	}
	if (!up) return "the menu didn't open"; /* (BOF2-5) */
	if (!bof2_settled(s, 120)) return "the menu didn't settle";
	if (!bof2_party_up(s)) return "the menu closed";
	/* the party window alone (BOF2-3), in the middle of the screen */
	int cw = GBA_W - BOF2_PARTY_X, left = (w - cw) / 2;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			out[y * w + x] = y < GBA_H && x >= left && x < left + cw ? s->fb[y * GBA_W + BOF2_PARTY_X + x - left] : 0xFF000000;
		}
	}
	bof2_keep(out, w, h);
	return NULL;
}

static const char* bof2_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	uint16_t bg0 = emu_io16(s, 0x08);
	/* a battle: the overview kept from before it (BOF2-4) */
	if (bg0 == BOF2_BG0_BATTLE) {
		if (!bof2Kept.ok || bof2Kept.w != w || bof2Kept.h != h) return "no party overview yet";
		memcpy(out, bof2Kept.px, (size_t)w * h * sizeof(uint32_t));
		return NULL;
	}
	/* the title, the intro and the like (BOF2-2) press nothing */
	if (bg0 != BOF2_BG0_FIELD && bg0 != BOF2_BG0_WORLD && bg0 != BOF2_BG0_MENU) return "not walking about";
	/* the world map open in the game: back to the field with B (BOF2-6), which fades out
	 * and in again, then the overview as for a menu */
	if (bg0 == BOF2_BG0_FIELD && bof2_map_up(s)) {
		aw_run(s, K_B, 4);
		for (int f = 0; f < 240 && bof2_map_up(s); f += 2) aw_run(s, 0, 2);
		if (bof2_map_up(s)) return "the map didn't close";
		for (int f = 0; f < 240 && emu_io16(s, 0x08) != BOF2_BG0_FIELD && emu_io16(s, 0x08) != BOF2_BG0_WORLD; f += 2) aw_run(s, 0, 2);
		aw_run(s, 0, 10);
		bg0 = emu_io16(s, 0x08);
		return bof2_party(s, false, out, w, h);
	}
	/* the overworld: the world map, START (BOF2-3), where the game opens it; where it opens
	 * nothing, or in the camp menu, the overview */
	if (bg0 == BOF2_BG0_WORLD) {
		/* the game ignores START between two steps of the party's walk: the copy gets no
		 * keys, so it finishes the step first */
		aw_run(s, 0, 24);
		aw_run(s, K_START, 4);
		for (int f = 0; f < 150 && !bof2_map_up(s); f += 2) aw_run(s, 0, 2);
		if (bof2_map_up(s)) {
			if (!bof2_map_take(s, out, w, h)) return emu_cancelled(s) ? "called off" : "the map didn't settle";
			/* and the overview, for a battle that starts while the map is up (BOF2-4): B,
			 * then SELECT; the map stays as it is if it fails */
			static uint32_t* scratch;
			static int scratchN;
			if (scratchN < w * h) {
				free(scratch);
				scratch = malloc((size_t)w * h * sizeof(uint32_t));
				scratchN = scratch ? w * h : 0;
			}
			aw_run(s, K_B, 4);
			for (int f = 0; f < 240 && bof2_map_up(s); f += 2) aw_run(s, 0, 2);
			for (int f = 0; f < 240 && emu_io16(s, 0x08) != BOF2_BG0_WORLD; f += 2) aw_run(s, 0, 2);
			if (scratch && !emu_cancelled(s) && emu_io16(s, 0x08) == BOF2_BG0_WORLD) {
				aw_run(s, 0, 10);
				bof2_party(s, false, scratch, w, h);
			}
			return NULL;
		}
		if (emu_cancelled(s)) return "called off";
	}
	return bof2_party(s, bg0 == BOF2_BG0_MENU, out, w, h);
}

/* ------------------------------------------- The Legend of Zelda: The Minish Cap
 *
 * docs/games/zelda-the-minish-cap.md. The second screen is the game's Map
 * tab (START's menu, R twice from Items): the map of Hyrule, or a dungeon's
 * map in a dungeon (TMC-1, TMC-2). The same addresses in all three releases
 * (the decompilation's linker script, github.com/zeldaret/tmc):
 *   TMC_TASK  gMain's task: 0 the title, 1 the file select, 2 the game, 3 a
 *             game over, 4 the credits
 *   TMC_SUB   its substate in the game: 2 walking about, 7 a menu open (the
 *             menu is a subtask), 1 a room being loaded
 *   TMC_TAB   the menu's tab, the one last used (kept between openings, so
 *             it's set to the map before START): 1 Items, 2 Quest Status,
 *             4 the map of Hyrule, 5 a dungeon's map
 *   TMC_STEP  the menu's step: 2 open and idle (its fade-in done)
 *   TMC_META  the area's flags: bit 0 the overworld, bit 2 a dungeon
 *   TMC_MAPX / TMC_MAPY  Link's place on the map of Hyrule (pixels), only
 *             updated outdoors; the region the map marks is a box of
 *             gOverworldLocations on x >> 4, y >> 4
 *   TMC_ITEM_MAP  ITEM_MAP, bits 6-7 of this inventory byte: no Map tab
 *             without it */
#define TMC_TASK 0x03001002
#define TMC_SUB 0x03001004
#define TMC_TAB 0x02034491
#define TMC_STEP 0x020344A1
#define TMC_AREA 0x03000BF4 /* area, room (bytes) */
#define TMC_META 0x02033A90
#define TMC_MAPX 0x030010C0
#define TMC_MAPY 0x030010C2
#define TMC_ITEM_MAP 0x02002B43
#define TMC_V_LOGO 0xF00000C0  /* not in the game: the title, the file select, a game over, the credits (TMC-9) */
#define TMC_V_PLACE 0xF00000C1 /* the region of Hyrule, or a dungeon's room (TMC-6) */
#define TMC_FRAMES 16 /* the loop of frames recorded (the longest blink cycle is ~60 frames) */
#define TMC_FRAME_STEP 4

/* gOverworldLocations: the 17 regions of the map (the game takes the first
 * box a place is in; x and y in 16-pixel units) */
static const struct { uint8_t x0, y0, x1, y1; } tmc_regions[17] = {
	{ 0, 0, 62, 74 },    { 0, 75, 62, 104 },   { 0, 105, 62, 164 },  { 0, 165, 62, 197 },
	{ 63, 0, 92, 74 },   { 63, 75, 92, 134 },  { 63, 135, 92, 197 },
	{ 93, 0, 155, 44 },  { 93, 45, 155, 94 },  { 93, 95, 155, 154 }, { 93, 155, 155, 197 },
	{ 156, 0, 185, 74 }, { 156, 75, 200, 134 }, { 156, 135, 185, 197 },
	{ 186, 0, 248, 62 }, { 186, 63, 248, 134 }, { 186, 135, 248, 197 },
};

static uint32_t tmc_virt(Emu* e, uint32_t addr) {
	if (addr == TMC_V_LOGO) return emu_read8(e, TMC_TASK) != 2;
	if (addr != TMC_V_PLACE) return 0;
	/* a dungeon: its room (the floors are rooms too) */
	/* before the map (TMC-4) the key says so: getting it takes a new picture */
	uint32_t noMap = ((emu_read8(e, TMC_ITEM_MAP) >> 6) & 3) == 0 ? 0x20000 : 0;
	if (emu_read8(e, TMC_META) & 4) return noMap | 0x10000 | emu_read16(e, TMC_AREA);
	unsigned x = emu_read16(e, TMC_MAPX) >> 4, y = emu_read16(e, TMC_MAPY) >> 4;
	for (int i = 0; i < 17; ++i) {
		if (x >= tmc_regions[i].x0 && x <= tmc_regions[i].x1 && y >= tmc_regions[i].y0 && y <= tmc_regions[i].y1) return noMap | (uint32_t) i;
	}
	return noMap | 0xFF;
}

/* the menu open on the wanted tab (a map's, or Quest Status before the
 * map) and idle (its fade-in over) */
static bool tmc_tab_up(Emu* s, bool quest) {
	uint8_t tab = emu_read8(s, TMC_TAB);
	return emu_read8(s, TMC_SUB) == 7 && emu_read8(s, TMC_STEP) == 2 && (quest ? tab == 2 : (tab == 4 || tab == 5));
}

/* Link's head on the map blinks in the game; on the second screen it stays on
 * (TMC-7). The game builds its sprite table in RAM (0x03000020, 128 entries:
 * the table the next frame is drawn from, one frame ahead of the hardware's
 * OAM); the head is tile 576 on the map of Hyrule, 544 on a dungeon's, palette
 * 7. Called with the table as it is: if the head is in it, remember it (its
 * place in the table and its attributes); if it's out and was seen, put it
 * back there, the entries after it moved down one. */
#define TMC_OAM_BUF 0x03000020
typedef struct { bool seen; int idx; uint16_t a[3]; } TmcHead;
static bool tmc_head_fix(Emu* s, TmcHead* h) {
	uint16_t e[3];
	int last = -1;
	for (int i = 0; i < 128; ++i) {
		const uint32_t at = TMC_OAM_BUF + (uint32_t) i * 8;
		e[0] = emu_read16(s, at);
		e[1] = emu_read16(s, at + 2);
		e[2] = emu_read16(s, at + 4);
		const unsigned tile = e[2] & 0x3FF, pal = e[2] >> 12;
		if ((e[0] & 0x300) != 0x200) last = i;
		if ((tile == 576 || tile == 544) && pal == 7 && (e[0] & 0x300) != 0x200) {
			h->seen = true; h->idx = i; memcpy(h->a, e, sizeof e);
			return true;
		}
	}
	if (!h->seen || last < 0 || last >= 127) return false;
	for (int i = last; i >= h->idx; --i) { /* make room, from the end */
		const uint32_t from = TMC_OAM_BUF + (uint32_t) i * 8;
		for (int k = 0; k < 4; ++k) emu_write16(s, from + 8 + (uint32_t) k * 2, emu_read16(s, from + (uint32_t) k * 2));
	}
	const uint32_t at = TMC_OAM_BUF + (uint32_t) h->idx * 8;
	for (int k = 0; k < 3; ++k) emu_write16(s, at + (uint32_t) k * 2, h->a[k]);
	return true;
}

/* n frames, the last one drawn; on a map the head's put back before it */
static void tmc_run(Emu* s, int n, bool map, TmcHead* h) {
	if (n > 1) aw_run(s, 0, n - 1);
	if (map) tmc_head_fix(s, h);
	emu_frame(s, 0);
}

static const char* tmc_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	/* L and R's arrows (tiles 32 and 16, priority 1, at the sides, around
	 * y 56); "Choose" and its button (tiles 256-300, priority 0, top right) */
	static const SpriteRule hide[] = {
		{ 0, 48, 239, 72, 16, 32, 1 },
		{ 160, -16, 239, 24, 250, 300, 0 }, /* it bobs, partly above the screen */
		/* Quest Status only (TMC-4): the cursor's corners (tile 15) and
		 * the SLEEP and SAVE buttons (tiles 704-752, along y 111) */
		{ 30, 36, 80, 80, 15, 15, 2 },
		{ 115, 108, 236, 134, 704, 752, 2 },
	};
	int max = *nOut < TMC_FRAMES ? *nOut : TMC_FRAMES;
	*nOut = 1;
	*step = TMC_FRAME_STEP;
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (emu_read8(s, TMC_TASK) != 2 || emu_read8(s, TMC_SUB) != 2) return "map button did nothing";
	/* no Map tab without the map: Quest Status instead (TMC-4) */
	const bool quest = ((emu_read8(s, TMC_ITEM_MAP) >> 6) & 3) == 0;
	/* START, until the menu is up: it isn't taken during a fade or a
	 * message, which may pass in a few frames */
	for (int tries = 0; tries < 4 && !tmc_tab_up(s, quest); ++tries) {
		emu_write8(s, TMC_TAB, quest ? 2 : 4);
		aw_run(s, K_START, 4);
		aw_run(s, 0, 4);
		for (int f = 0; f < 50 && !tmc_tab_up(s, quest); ++f) emu_frame_blind(s, 0);
	}
	if (!tmc_tab_up(s, quest)) return "map button did nothing";
	if (emu_cancelled(s)) return "called off";
	emu_hide_sprites(s, hide, quest ? 4 : 2);
	/* Quest Status: BG0 holds only the name bar (of the item the cursor was
	 * left on); without it the scroll drawn behind shows, whole (TMC-5) */
	if (quest) emu_layers(s, 0x1E);
	TmcHead head = { false, 0, { 0, 0, 0 } };
	/* the head blinks: run until it's been seen, to know where it goes */
	for (int f = 0; f < 90 && !quest && !head.seen; ++f) {
		emu_frame_blind(s, 0);
		tmc_head_fix(s, &head);
	}
	tmc_run(s, 4, !quest, &head);
	int cw = w < GBA_W ? w : GBA_W, ch = h < GBA_H ? h : GBA_H;
	for (int y = 0; y < ch; ++y) memcpy(&out[0][y * w], &s->fb[y * GBA_W], (size_t) cw * 4);
	bool animated = false;
	for (int i = 1; i < max; ++i) {
		tmc_run(s, TMC_FRAME_STEP, !quest, &head);
		if (!tmc_tab_up(s, quest)) break;
		for (int y = 0; y < ch; ++y) memcpy(&out[i][y * w], &s->fb[y * GBA_W], (size_t) cw * 4);
		if (memcmp(out[i], out[0], (size_t) w * h * 4)) animated = true;
		*nOut = i + 1;
	}
	emu_hide_sprites(s, NULL, 0);
	emu_layers(s, 0x1F);
	if (emu_cancelled(s)) return "called off";
	if (!animated) *nOut = 1;
	return NULL;
}

/* The Minish Cap in Japan: the lettering runs lower (the subtitle is
 * Japanese, under the title), and there's no sword's pommel to cut away */
static void tmc_jpn(MapProfile* p) {
	p->listName = "Zelda no Densetsu - Fushigi no Boushi (Japan)";
	p->logo.layers = 0x12; /* the lettering is on BG1, the subtitle and the bird sprites */
	p->logo.maxY = 122;
}

static const MapProfile profiles[] = {
	/* PRELIMINARY (pokemon-preliminary): see docs/pokemon-ideas.md. One entry per engine (the USA and Europe releases) */
#define PK_PROFILE(ENGINE_LOGO, TITLE, ...) \
	{ \
		.codes = { __VA_ARGS__ }, \
		.title = TITLE, \
		.composePages = pk_pages, \
		.virt = pk_virt, \
		.holdPage = pk_hold_page, \
		.interval = 180, \
		.room = { { PK_VIRT, 0xFFFFFFFF, 0, 4 } }, \
		.crop_w = GBA_W, \
		.crop_h = GBA_H, \
		.logo = ENGINE_LOGO, \
		.logoPerRom = true, \
		.logoFor = pkLogos, \
		.nLogoFor = (int) (sizeof(pkLogos) / sizeof(pkLogos[0])), \
	},
	PK_PROFILE(PK_LOGO_FRLG, "Pokemon FireRed / LeafGreen (preliminary)", "BPRE", "BPGE", "BPRJ", "BPGJ")
	PK_PROFILE(PK_LOGO_FRLG, "Pokemon FireRed / LeafGreen (preliminary, German, French)", "BPRD", "BPGD", "BPRF", "BPGF")
	PK_PROFILE(PK_LOGO_FRLG, "Pokemon FireRed / LeafGreen (preliminary, Italian, Spanish)", "BPRI", "BPGI", "BPRS", "BPGS")
	PK_PROFILE(PK_LOGO_RS4, "Pokemon Ruby / Sapphire (preliminary)", "AXVE", "AXPE", "AXVJ", "AXPJ")
	PK_PROFILE(PK_LOGO_RS4, "Pokemon Ruby / Sapphire (preliminary, German, French)", "AXVD", "AXPD", "AXVF", "AXPF")
	PK_PROFILE(PK_LOGO_RS4, "Pokemon Ruby / Sapphire (preliminary, Italian, Spanish)", "AXVI", "AXPI", "AXVS", "AXPS")
	PK_PROFILE(PK_LOGO_RSE3, "Pokemon Emerald (preliminary)", "BPEE", "BPEJ", "BPED", "BPEF")
	PK_PROFILE(PK_LOGO_RSE3, "Pokemon Emerald (preliminary, Italian, Spanish)", "BPEI", "BPES")
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
		/* the game's Map tab, taken from a hidden copy: the map of Hyrule, or
		 * a dungeon's map; see tmc_compose (docs/games/zelda-the-minish-cap.md) */
		.codes = { "BZME" },
		.title = "The Legend of Zelda: The Minish Cap",
		.composePages = tmc_compose,
		.virt = tmc_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* captures only while Link is in play (not on the title, in his
		 * menu or while a room loads); the title, the file select, a game
		 * over and the credits: the logo, the map forgotten (TMC-8, TMC-9) */
		.live = { { TMC_V_LOGO, 0xFF, 0, 4 }, { TMC_SUB, 0xFF, 2, 1 } },
		.menu = { { TMC_V_LOGO, 0xFF, 1, 4 } },
		/* another region of Hyrule, another dungeon room or floor (TMC-6) */
		.room = { { TMC_V_PLACE, 0xFFFFFFFF, 0, 4 } },
		/* the title's lettering (sprites), above PRESS START and the
		 * copyright line: without the sword and ribbon (BG2) */
		.logo = { .wait = 900, .layers = 0x10, .maxY = 96 },
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
		 * 0, 96 (further left on a narrow course: Bianca City Ultimate
		 * Circuit's at -24), drawn every other frame, with the player's dot (675) on
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
			          /* the map (x from -64: a narrow course's further
			           * left, Bianca City Ultimate Circuit's at -24) and the
			           * dot */
			          { -64, 88, 64, 159, 884, 884, 0, 1u << 15 },
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
		/* GT Advance 3: Pro Concept Racing (docs/games/
		 * gt-advance-3-pro-concept-racing.md), the racing games' second
		 * screen. A race is display mode 1, its picture split by line: the
		 * sky on BG0 (lines 0-51), the scenery on BG1 at priority 1 and the
		 * road on BG2 (affine); from line 80 BG1 is the HUD's bottom bar
		 * (LAP-TIME or MAX-COMBO, the rev counter's dial and the speed box)
		 * at priority 0, so the frame ends with BG1CNT 0x5E48. Everything
		 * else of the HUD is priority-0 sprites in palettes 11-14 (the
		 * figures in 14, the words in 13, the course map 11, its dots 12):
		 * told by place, since the figures share their tiles. The
		 * countdown, the corner signs, WRONG WAY, GHOST OVER, GOAL!, a
		 * finished lap's time and the pause menu are sprites elsewhere on
		 * the screen: they stay on the game screen (GT3-2). */
		.codes = { "A2GE", "A2GP" },
		.title = "GT Advance 3: Pro Concept Racing",
		.logoOnly = true,
		/* the title's lettering, GT3 ADVANCE PRO CONCEPT RACING, painted
		 * into its picture over the highway (GT3-1): past the
		 * disclaimer (A) from power-on */
		.logo = { .steps = { { 0, 720 }, { K_START, 4 }, { 0, 68 }, { K_A, 4 }, { 0, 250 } },
		          .mask = &logoMaskGT3 },
		.hud = {
			/* a race: display mode 1 with the bottom bar's BG1 (GT3-2);
			 * the replay after the finish draws no HUD: the logo (GT3-7) */
			.on = { { 0x04000000, 0x0007, 1, 2 }, { 0x0400000A, 0xFFFF, 0x5E48, 2 } },
			.emptyIsOff = true,
			/* on black (GT3-4) */
			.black = true,
			/* paused (the pause flag): the HUD as it was before the game
			 * hid it (GT3-5) */
			.freeze = { { 0x0203EE58, 0x20, 0x20, 1 } },
			/* the bottom bar: BG1 where it's priority 0 (from line 80) */
			.bgPrioBelow = 1,
			.objs = { /* the top row: TOTAL TIME, POSITION, LAP */
			          { -8, -8, 239, 8, 800, 1023, 0, (1u << 13) | (1u << 14) },
			          /* the course map (also Practice's) and its dots,
			           * also as they slide off to the left at the finish
			           * (GT3-6) */
			          { -72, 8, 72, 80, 700, 799, 0, 1u << 11 },
			          { -72, 8, 72, 88, 800, 807, 0, 1u << 12 },
			          /* LAP-TIME's or MAX-COMBO's figures (the combo's
			           * count from tile 448) */
			          { -8, 130, 149, 168, 400, 1023, 0, (1u << 13) | (1u << 14) },
			          /* the rev counter's needle, the speed and the gear
			           * (moved back onto the game screen, GT3-3) */
			          { 150, 100, 239, 168, 800, 1023, 0, (1u << 13) | (1u << 14) } },
			/* the map's 64x64 sprite, twice as big in the middle (GT3-4) */
			.map = { 0, 16, 63, 79 },
			.mapAt = { 2, 120, 140 },
			/* POSITION back onto the game screen, into the top-left
			 * corner, where TOTAL TIME was; the rev counter with the speed
			 * and gear where they were (GT3-3) */
			.moves = { { 119, 0, 183, 20, 'L', 0, 0, 0, true },
			           { 150, 100, 239, 159, 0, 0, 0, 0, true } },
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
		 * variant). In a level the display is mode 0 with sprites (DISPCNT
		 * 0x1F40, every layer, the title's too; the Nightclub's 0x1340);
		 * SELECT brings up the
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
		/* in a level: mode 0 with sprites (the Nightclub's level has only
		 * BG0 and BG1 on, 0x1340, the others every layer, 0x1F40) */
		.live = { { 0x04000000, 0x1007, 0x1000, 2 }, { 0x03002170, 0xFFFF, 0x612C, 2 } },
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
		/* ... and only the HUD's own: the game puts its pieces first in the
		 * sprite table (level 2 puts Sia, in her own palette, among them),
		 * and a level's thing is several sprites in one palette, one after
		 * another. A HUD-shaped piece is hidden in the run at the start of
		 * the table or in a run of one palette that's all HUD pieces; a
		 * level's thing that passes a HUD piece's place with its shape (the
		 * first village's well, a column of rock in the caves: players' bug
		 * reports) has its other pieces beside it, so it stays (LS-6). */
		.hideGrouped = true,
		/* the pause screen's own pieces, the rest left out of the status
		 * picture: the game leaves some level things in the sprite table
		 * under its pause screen (a floating platform in the caves, in a
		 * player's bug report), drawn over the ring (LS-2). The arrows
		 * either side of the choice (they slide), the 64x64 text blocks of
		 * the time, Pause and the choice, then the HUD's places as above
		 * (the ring's glow pieces too: the probes see it as before). */
		.captureKeep = { { -10, 95, 220, 99, 0, 1023, -1, 0, 0, LS_SHAPE(1, 2) },
		                 { 0, 48, 192, 48, 0, 1023, -1, 0, 0, LS_SHAPE(0, 3) },
		                 { 2, 7, 2, 7, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 94, 3, 94, 3, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 193, 1, 193, 1, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 31, 10, 232, 10, 0, 1023, -1, 0, 0, LS_SHAPE(2, 0) },
		                 { 38, 124, 44, 129, 0, 1023, -1, 0, 0, LS_SHAPE(0, 2) },
		                 { 68, 125, 77, 132, 0, 1023, -1, 0, 0, LS_SHAPE(2, 2) | LS_SHAPE(2, 1) },
		                 { 51, 152, 53, 154, 0, 1023, -1, 0, 0, LS_SHAPE(1, 1) },
		                 { 164, 130, 171, 133, 0, 1023, -1, 0, 0, LS_SHAPE(0, 1) | LS_SHAPE(2, 0) },
		                 { 167, 143, 170, 146, 0, 1023, -1, 0, 0, LS_SHAPE(1, 0) | LS_SHAPE(0, 1) } },
	},
	{
		/* Banjo-Kazooie: Grunty's Revenge (USA and Europe; Europe's
		 * English, French and German BKZX, Italy's BKZI and Spain's BKZS
		 * are variants, the same RAM). See bk_compose. */
		.codes = { "BKZE" },
		.title = "Banjo-Kazooie: Grunty's Revenge",
		/* the title's lettering, BG1 (its glow blends with the scene
		 * behind; Press START is a sprite) (BK-1) */
		.logo = { .steps = { { 0, 1300 } }, .layer = 1 },
		/* the pause screen and the world's Totals, as pages, the health
		 * bar on both (BK-2, BK-3) */
		.composePages = bk_compose,
		.stillPick = STILL_FIRST,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		/* sprites only, on black: the level the pause screen darkens is
		 * the game screen's (BK-2) */
		.captureLayers = 0x10,
		/* about (BK_OBJS), not paused (the player's own pause screen
		 * stays on the game screen, the last picture kept: BK-5) */
		.live = { { BK_OBJS, 0xFF000000, 0x02000000, 4 }, { BK_PAUSE, 0xFF, 0, 1 } },
		/* the title, the file screen, the intro: the logo (BK-4) */
		.menu = { { BK_OBJS, 0xFFFFFFFF, 0, 4 } },
		/* another area: a capture at once (GEN-6) */
		.room = { { BK_AREA, 0xFF, 0, 1 } },
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
			 * game screen whole, the logo alone on the second screen
			 * (only the battle's own screen, not the mine carts' state
			 * 3: ML-7, issue #51) */
			.on = { { ML_STATE, 0xFFFF, ML_BATTLE, 2 }, { ML_RESULTS, 0xFFFF, 0, 2 } },
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
			 * from tile 112 only palettes 11 (the cursor and icons), 12
			 * (the green icons: Refreshing Herbs, Green Peppers) and 13
			 * (the BROS list's level marks): a Bros. Attack's big
			 * button prompts (7), the "!" over a brother's head (8) and
			 * a speech balloon's marker and edges (15) stay (issue #31).
			 * The
			 * meters' digits and backing (tiles 0-111) only in their
			 * corner (x 4-93, rows 130-142 seen): the damage numbers over
			 * an enemy or a brother are the same digits, and stay */
			.objs = { { -16, 120, 110, 159, 0, 111, 0, 0x7FFF }, { -256, -96, 255, 159, 112, 1023, 0, 0x3800 } },
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
		/* Dragon Ball Z: Buu's Fury (USA only). Walking about, the
		 * Scouter's area map: see bf_compose (BF-2) */
		.codes = { "BG3E" },
		.title = "Dragon Ball Z: Buu's Fury",
		.compose = bf_compose,
		/* (an empty Scouter, in a shop: the last map dimmed, BF-8) */
		.dimWhenUnavailable = true,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about (the story scenes and conversations too: there
		 * SELECT opens nothing, and the last picture or the logo stays,
		 * BF-1, BF-4) */
		.live = { { 0x04000000, 0xFFFF, BF_FIELD_DISP, 2 } },
		/* 0 on the title and the file menu, 0x32 flying over the world:
		 * the logo, the last map forgotten (BF-1, BF-5) */
		.menu = { { BF_ZONE, 0xFF, 0x00, 1, true, 0x32 } },
		/* the area: a new area's map at once (GEN-6) */
		.room = { { BF_AREA, 0xFFFFFFFF, 0, 4 } },
		/* flying over the world (DISPCNT 0x1741, zone 0x32): the inset map
		 * at the top right (BG0 rows 6-51, your arrow an 8x8 sprite) moves
		 * to the second screen, twice as big and in the middle (BF-5).
		 * BG0's haze over the horizon, from row 56 down, and the help bar
		 * (sprites at row 136) stay on the game screen */
		.hud = {
			.on = { { 0x04000000, 0xFFFF, 0x1741, 2 }, { BF_ZONE, 0xFF, 0x32, 1 } },
			.bgMask = 1,
			.rows = 56,
			.objs = { { 150, -8, 239, 55, 0, 1023, -1, 0, 0, 1u << 0 } },
			/* (grown from what's drawn: the outline, 63x46, centred at
			 * row 80; a mark past its edge grows it a little) */
			.map = { 150, 0, 239, 55 },
			.mapAt = { 2, 120, 125 },
		},
		/* the title, which comes up with no input: DRAGON BALL Z and BUU'S
		 * FURY inside its mask (BF-1) */
		.logo = { .steps = { { 0, 900 } }, .mask = &logoMaskBF },
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
		/* Japan (its only release; most play it with the FE6 Localization
		 * fan translation, a mod of it): as Fire Emblem (the same engine
		 * and second screen: see fe_compose; its addresses in feRegions) */
		.codes = { "AFEJ" },
		.title = "Fire Emblem: Fuuin no Tsurugi",
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
		/* the title (START past the intro): the lettering (BG0), its
		 * shadow (BG1) and the sword (BG2), with FIRE EMBLEM and the
		 * subtitle's ribbon (sprites, above Press start from row 121 and
		 * the copyright line) */
		.logo = { .wait = 300, .pressStart = true, .wait2 = 150, .layers = 0x17, .maxY = 119 },
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
		/* Yu-Gi-Oh! World Championship Tournament 2004 (USA; Europe, and
		 * Japan's Duel Monsters Expert 3, the same screens and addresses):
		 * the Card View of the card under the cursor, see wct_compose
		 * (WCT-1 to WCT-5) */
		.codes = { "BYWE" },
		.title = "Yu-Gi-Oh! World Championship Tournament 2004",
		.compose = wct_compose,
		.virt = wct_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.live = { { WCT_V_LIVE, 0xFF, 1, 4 } },
		/* out of the duels and the card screens, and through the
		 * opponent's turn: the logo, the card forgotten (WCT-6, WCT-9) */
		.menu = { { WCT_V_IDLE, 0xFF, 1, 4 } },
		.room = { { WCT_V_ROOM, 0, 0, 4 } },
		/* the title's YU-GI-OH! (BG2) over WORLD CHAMPIONSHIP TOURNAMENT 2004
		 * (BG0), above Push Start (WCT-7), from a fresh start: past the
		 * Konami screens, the language screen (A), then the title */
		.logo = { .steps = { { 0, 700 }, { K_A, 4 }, { 0, 300 } }, .layers = 0x05, .maxY = 115 },
	},
	{
		/* Yu-Gi-Oh! Ultimate Masters: World Championship Tournament 2006
		 * (USA; Europe, Ultimate Masters Edition): the Card View of the card
		 * under the cursor, see um6_compose (UM6-1 to UM6-5) */
		.codes = { "BY6E" },
		.title = "Yu-Gi-Oh! Ultimate Masters: World Championship Tournament 2006",
		.compose = um6_compose,
		.virt = um6_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.live = { { UM6_V_LIVE, 0xFF, 1, 4 } },
		/* out of the duels and Deck Edit, and through the opponent's turn:
		 * the logo, the card forgotten (UM6-4, UM6-5) */
		.menu = { { UM6_V_IDLE, 0xFF, 1, 4 } },
		.room = { { UM6_V_ROOM, 0, 0, 4 } },
		/* the title's emblem (BG0), above New Game and the copyright line
		 * (UM6-6) */
		.logo = { .wait = 700, .layers = 0x01, .maxY = 120 },
	},
	{
		/* Duel Masters: Kaijudo Showdown (USA; Europe): the Card Details of
		 * the card under the cursor, see dmk_compose (DMK-1 to DMK-4) */
		.codes = { "BD6E", "BD6P" },
		.title = "Duel Masters: Kaijudo Showdown",
		.compose = dmk_compose,
		.virt = dmk_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.live = { { DMK_V_LIVE, 0xFF, 1, 4 } },
		/* out of the duels and the card lists: the logo, the card
		 * forgotten (DMK-4) */
		.menu = { { DMK_V_IDLE, 0xFF, 1, 4 } },
		.room = { { DMK_V_ROOM, 0, 0, 4 } },
		/* the title's DUEL MASTERS lettering (BG0) and its KAIJUDO SHOWDOWN
		 * line (sprites above the menu), DMK-5 */
		.logo = { .wait = 2300, .layers = 0x11, .maxY = 114, .minX = 12,
			/* the lettering is BG0 and the subtitle sprites (palette 0); the
			 * orb, the glint across the M and the translucent rays are
			 * sprites of the other palettes (Europe also has its flags and PRESS START there) */
			.hide = { { -64, -64, 304, 224, 0, 1023, -1, 0xFFFE, 0, 0 } }, .rev = 3 },
	},
	{
		/* Yu-Gi-Oh! 7 Trials to Glory (USA): the Card View of the card under
		 * the cursor, see yw_compose (YWC-1 to YWC-4) */
		.codes = { "BY7E" },
		.title = "Yu-Gi-Oh! 7 Trials to Glory",
		.compose = yw_compose_us,
		.virt = yw_virt_us,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.live = { { YW_V_LIVE, 0xFF, 1, 4 } },
		.menu = { { YW_V_IDLE, 0xFF, 1, 4 } },
		.room = { { YW_V_ROOM, 0, 0, 4 } },
		/* the title's lettering and flame are sprites, over its hex-grid
		 * background (BG layers 1 to 3), above New Game / Continue (BG0):
		 * the sprites alone, once English is chosen on the Language
		 * Selection of a first start (YWC-5) */
		.logo = { .steps = { { 0, 700 }, { K_A, 4 }, { 0, 1100 } }, .layers = 0x10, .maxY = 112 },
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
		 * screen: see mmbn_compose (MMBN-2), kept through battles
		 * (MMBN-4); on the net the area's map (MMBN-6) */
		.codes = { "AREE" },
		.title = "Mega Man Battle Network",
		.compose = mmbn_compose,
		/* on the net, the area's map (MMBN-6) */
		.liveView = mmbn_live_view,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about (scenes too: START opens nothing there, the last
		 * picture stays, MMBN-3) */
		.live = { { MMBN_STATE, 0xFF, 4, 1 } },
		/* the title, game over (0x20): the logo, the status forgotten
		 * (MMBN-1); in a battle the status from before it stays (MMBN-4) */
		.menu = { { MMBN_STATE, 0xFF, 0, 1, true, 0x20 } },
		/* HP (back from a battle, a recovery), its maximum, the area, the money:
		 * taken again at once (MMBN-2) */
		.room = { { MMBN_HP, 0xFFFF, 0, 2 }, { MMBN_HP + 2, 0xFFFF, 0, 2 }, { MMBN_AREA, 0xFFFF, 0, 2 }, { MMBN_ZENNY, 0, 0, 4 } },
		/* the title's lettering and emblem (BG3), above PRESS START */
		.logo = { .wait = 400, .layers = 0x8, .maxY = 96 },
	},
	{
		/* Mega Man Battle Network 2 (USA, the Wii U's too; Europe AM2P,
		 * Japan Battle Network Rockman EXE 2: variants). As the first
		 * game: walking about, the PET's MegaMan screen with the money
		 * (mmbn2_compose, MMBN2-2), kept through battles (MMBN2-4); on the
		 * net the area's map (MMBN2-6) */
		.codes = { "AE2E" },
		.title = "Mega Man Battle Network 2",
		.compose = mmbn2_compose,
		.liveView = mmbn_live_view,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about (scenes too: START opens nothing there, the last
		 * picture stays, MMBN2-3) */
		.live = { { MMBN2_STATE, 0xFF, 4, 1 } },
		/* the title, game over (0x20): the logo, the status forgotten
		 * (MMBN2-1); in a battle the status from before it stays (MMBN2-4) */
		.menu = { { MMBN2_STATE, 0xFF, 0, 1, true, 0x20 } },
		/* HP, its maximum, the area, the money: taken again at once (MMBN2-2) */
		.room = { { MMBN2_HP, 0xFFFF, 0, 2 }, { MMBN2_HP + 2, 0xFFFF, 0, 2 }, { MMBN2_AREA, 0xFFFF, 0, 2 }, { MMBN2_ZENNY, 0, 0, 4 } },
		/* the title's lettering and emblem (BG3) above PRESS START, the
		 * grid of lines behind it on the same layer left out */
		.logo = { .wait = 420, .layers = 0x8, .maxY = 112, .edgeClear = { 0x006B00, 0x52B500, 0x84D600, 0xCEFF00 } },
	},
	{
		/* the overworld's map, or the Party screen in towns and dungeons:
		 * see er_compose (docs/games/eragon.md) */
		.codes = { "BENE" },
		.title = "Eragon",
		.compose = er_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.virt = er_virt,
		/* the title: the logo, no captures (ER-6) */
		.live = { { ER_V_TITLE, 0xFF, 0, 4 } },
		.menu = { { ER_V_FORGET, 0xFF, 1, 4 } }, /* (ER-5, ER-6: the logo until the plates) */
		/* a battle: its plates and HP bars, BG3's top three rows of
		 * tiles and the bars' sprites, on the second screen with the
		 * logo below; kept while a command is chosen (ER-5) */
		.hud = {
			.on = { { ER_PLATES, 0xFFFF, ER_PLATE, 2 }, { 0x0400000E, 0xFFFC, 0x1C08, 2 } },
			.keep = { { ER_V_BATTLE, 0xFF, 1, 4 } },
			.bgMask = 8,
			.objs = { { 0, -8, 239, 23, 0, 1023, 0 } },
			.rows = 25, /* (the plates' bottom edge is row 24; BG3 below is the command bar, rows 144-159, which stays) */
			.logoBelow = true,
		},
		/* into a place or out of one, or a battle over (ER-9) */
		.room = { { ER_V_PLACE, 0xFF, 0, 4 } },
		/* the carved "eragon" alone, cut out by the owner's outline of
		 * the word (ER-7; tools/make_logo_masks.py, er). rev 1: the boxed
		 * strip of stone before */
		.logo = { .wait = 1000, .mask = &logoMaskER, .rev = 1 },
	},
	{
		/* Final Fantasy VI Advance: walking about, START's menu as the party
		 * screen: see ff6_compose (docs/games/final-fantasy-vi-advance.md) */
		.codes = { "BZ6E" },
		.title = "Final Fantasy VI Advance",
		.compose = ff6_compose,
		.virt = ff6_virt,
		/* the corner minimap isn't drawn on the game screen: it's on the
		 * second (FF6-10, FF6-11) */
		.hideSprites = { FF6_HIDE_MINIMAP },
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* outside the game and after a game over: the logo, the pictures
		 * dropped, no captures (FF6-2, FF6-9) */
		.live = { { FF6_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { FF6_V_LOGO, 0xFF, 1, 4 } },
		/* another map, a battle over: taken again at once (FF6-4, FF6-7) */
		.room = { { FF6_MAP, 0xFFFF, 0, 2 }, { FF6_V_BATTLE, 0xFF, 0, 1 } },
		/* the title's lettering (BG0): past the health warning (A), the logos
		 * and the title's own animation */
		.logo = { .steps = { { 0, 500 }, { K_A, 4 }, { 0, 1500 }, { K_A, 4 }, { 0, 300 } },
		          .layers = 0x01, .maxY = 100, .lightEdgeClear = true },
	},
	{
		/* the same for Europe: A on its language screen (English) first */
		.codes = { "BENP" },
		.title = "Eragon",
		.compose = er_compose,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.virt = er_virt,
		.live = { { ER_V_TITLE, 0xFF, 0, 4 } },
		.menu = { { ER_V_FORGET, 0xFF, 1, 4 } },
		.hud = {
			.on = { { ER_PLATES, 0xFFFF, ER_PLATE, 2 }, { 0x0400000E, 0xFFFC, 0x1C08, 2 } },
			.keep = { { ER_V_BATTLE, 0xFF, 1, 4 } },
			.bgMask = 8,
			.objs = { { 0, -8, 239, 23, 0, 1023, 0 } },
			.rows = 25, /* (the plates' bottom edge is row 24; BG3 below is the command bar, rows 144-159, which stays) */
			.logoBelow = true,
		},
		.room = { { ER_V_PLACE, 0xFF, 0, 4 } },
		.logo = { .steps = { { 0, 100 }, { K_A, 4 }, { 0, 1100 } }, .mask = &logoMaskER, .rev = 1 },
	},
	{
		/* Baldur's Gate: Dark Alliance (USA, Europe: the same addresses).
		 * The area's map, made on a copy and drawn live: see
		 * bgd_live_view (docs/games/baldurs-gate-dark-alliance.md) */
		.codes = { "BGDE", "BGDP" },
		.title = "Baldur's Gate: Dark Alliance",
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		.virt = bgd_virt,
		.liveView = bgd_live_view,
		/* (no captures: the map is the live view's) */
		.live = { { BGD_V_NEVER, 0xFF, 1, 1 } },
		/* the title, the language list, the class choice, GAME OVER: the
		 * logo (BGD-5, BGD-9) */
		.menu = { { BGD_V_LOGO, 0xFF, 1, 4 } },
		/* the title's lettering cut out of its parchment map along the
		 * logo artwork's outline (BGD-6; the European language list, sprites
		 * below the lettering, is outside it) */
		.logo = { .wait = 1500, .mask = &logoMaskBGD, .rev = 1 },
	},
	{
		/* Harry Potter and the Prisoner of Azkaban (USA and Europe, one
		 * release in seven languages). Walking about, the party's summary
		 * and the task: see hpa_compose (HPA-1) */
		.codes = { "BHTE" },
		.title = "Harry Potter and the Prisoner of Azkaban",
		.compose = hpa_compose,
		.virt = hpa_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* walking about or in START's menu (HPA-1, HPA-2; in a
		 * conversation or a scene START opens nothing: the last picture
		 * stays, HPA-3) */
		.live = { { HPA_V_LIVE, 0xFF, 1, 4 } },
		/* the title, the file screens, a battle: the logo, the summary
		 * forgotten (HPA-4, HPA-5) */
		.menu = { { HPA_V_FORGET, 0xFF, 1, 4 } },
		.combat = { { HPA_V_BATTLE, 0xFF, 1, 4 } },
		/* the screen (a menu closed, a scene over), Harry's SP and MP:
		 * taken again at once */
		.room = { { HPA_MODE, 0x7F, 0, 1 }, { HPA_SP, 0xFFFF, 0, 2 }, { HPA_SP + 2, 0xFFFF, 0, 2 } },
		/* the title's lettering (BG2), past the language screen (A:
		 * English) and the company screens (START), before PRESS START */
		.logo = { .steps = { { 0, 600 }, { K_A, 4 }, { 0, 100 }, { K_START, 4 }, { 0, 90 }, { K_START, 4 }, { 0, 90 },
		                     { K_START, 4 }, { 0, 90 }, { K_START, 4 }, { 0, 90 }, { K_START, 4 }, { 0, 240 } },
		          .layer = 2, .maxY = 120 },
	},
	{
		/* Mario vs. Donkey Kong (USA and Australia; Europe and Japan:
		 * the BM5P and BM5J variants). The game has no status screen, so
		 * the HUD moves, as Ninja Five-O's, under the pause box's LEVEL
		 * and HI SCORE lines from the hidden copy: see mvdk_compose
		 * (MVDK-2, MVDK-9). */
		.codes = { "BM5E" },
		.title = "Mario vs. Donkey Kong",
		/* the title's lettering, cut out of the title by its sky
		 * (tools/make_logo_masks.py mvdk): START at 600 skips the
		 * opening film, the title up from about 120 frames on (MVDK-1).
		 * rev 1: the greys outside the black outline left out */
		.logo = { .steps = { { 0, 600 }, { K_START, 4 }, { 0, 300 } }, .mask = &logoMaskMVDK, .rev = 1 },
		.compose = mvdk_compose,
		.crop_w = GBA_W, /* a GBA screen: the box at its top, black below */
		.crop_h = GBA_H,
		.virt = mvdk_virt,
		/* the box taken while the HUD is up and not paused (MVDK-9) */
		.live = { { MVDK_V_PLAY, 0xFF, 1, 4 } },
		/* no level being shown (outside levels, a level's opening pan):
		 * the logo, the box forgotten (MVDK-3, MVDK-7) */
		.menu = { { MVDK_V_LOGO, 0xFF, 1, 4 } },
		.logoIf = { { MVDK_V_LOGO, 0xFF, 1, 4 } },
		/* another level or part (the HUD back after its pan): the box
		 * taken again at once (MVDK-9) */
		.room = { { MVDK_V_LEVEL, 0, 0, 4 }, { MVDK_V_PLAY, 0, 0, 4 } },
		.hud = {
			/* a level played (its pan and HERE WE GO too: no HUD sprites
			 * yet, so the logo, emptyIsOff; MVDK-3) or paused, its HUD
			 * held on the second screen while the box is up (MVDK-2,
			 * MVDK-6) */
			.on = { { MVDK_STATE, 0xFF, MVDK_PLAY, 1, true, MVDK_PAUSE } },
			.freeze = { { MVDK_STATE, 0xFF, MVDK_PAUSE, 1 } },
			/* the level viewer (L+R): the same, as in play (MVDK-5) */
			.alt = { .on = { { MVDK_STATE, 0xFF, MVDK_VIEWER, 1 } }, .bgMask = 0, .mapBelow = true },
			/* a life lost, the mute after it, a help sign's screen: the
			 * last HUD picture (MVDK-6) */
			.keep = { { MVDK_STATE, 0xFF, MVDK_PLAY + 1, 1, true, MVDK_HELP, true } },
			/* the HUD: priority-0 sprites with their top in rows 0-17
			 * (to 20 for slack); not the fights' rope (8x32, at the top
			 * too) or the viewer's arrows (32x32) (MVDK-4) */
			.objs = { { -32, -16, 239, 20, 0, 1023, 0, 0, 0, MVDK_HUD_SHAPES } },
			/* the HUD along the top: its top (the game's row 3) at row
			 * MVDK_HUD_TOP, over the box in the middle (MVDK-2) */
			.moves = { { 0, 0, 239, 40, 0, 0, 'T', MVDK_HUD_TOP } },
			/* the box (the map picture) behind it; black without one */
			.mapBelow = true,
			.black = true,
			.emptyIsOff = true,
		},
	},
	{
		/* MOTHER 3 (Japan only; the fan translations are mods of it):
		 * walking about, the area's map or the Status: see m3_compose */
		.codes = { "A3UJ" },
		.title = "MOTHER 3",
		.composePages = m3_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first page) */
		.virt = m3_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* the title and file screens, scenes and battles: the logo
		 * (the pictures dropped), no captures (M3-6, M3-10, M3-11) */
		.live = { { M3_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { M3_V_LOGO, 0xFF, 1, 4 } },
		/* another area: its map or the Status at once (M3-2) */
		.room = { { M3_MAP, 0xFFFF, 0, 2 } },
		/* the title's lettering (BG3, over black), past the health
		 * warning (A) and, in the translations, their title card (A) */
		.logo = { .steps = { { 0, 200 }, { K_A, 4 }, { 0, 400 }, { K_A, 4 }, { 0, 1800 } }, .layer = 3, .maxY = 160, .blackIsClear = true },
	},
	{
		/* Boktai: The Sun Is in Your Hand (USA; the other releases are
		 * variants): the Map page of SELECT's menu, see bt_compose */
		.codes = { "U3IE" },
		.title = "Boktai: The Sun Is in Your Hand",
		.compose = bt_compose,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* captures only while playing: not in the menus, the pause or on
		 * the title (BT-3) */
		.live = { { BT_MENU, 0xFF, 0, 1 } },
		/* back on the title or the file menu (0 there, 40 while in the game) */
		.menu = { { 0x02001B38, 0xFF, 0, 1 } },
		/* the title's lettering (BG3) and its banner (sprites), over the
		 * sun, above its PUSH START line; the first start asks for the sensor and the
		 * clock first (A through them, START to end), then the Konami
		 * logos and the caution take until the title (BT-5) */
		.logo = { .steps = { { 0, 600 }, { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 },
		                     { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 }, { K_A, 3 }, { 0, 90 },
		                     { K_START, 3 }, { 0, 1100 } },
		          .layers = 0x18, .maxY = 120,
		          /* the tan and brown blends round the yellow outline (the
		           * sun's colours dithered in) are clear, so the outline
		           * is the edge (BT-5) */
		          .edgeClear = { 0x8C634A, 0xC68C63, 0x7B524A, 0xDEA542 }, .rev = 1 },
	},
	{
		/* Final Fantasy I & II: Dawn of Souls (two games, each with its
		 * own memory): walking about, Final Fantasy's world map or START's
		 * menu: see ff_compose */
		.codes = { "BFFE" },
		.title = "Final Fantasy I & II: Dawn of Souls",
		.compose = ff_compose,
		.virt = ff_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* outside the games and in Final Fantasy II's battles: the
		 * logo, the pictures dropped, no captures (FF-2, FF-8) */
		.live = { { FF_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { FF_V_LOGO, 0xFF, 1, 4 } },
		/* another map, a battle over: taken again at once (FF-5, FF-8) */
		.room = { { FF_V_MAP, 0xFF, 0, 4 }, { FF_BATTLE, 0xFF, 0, 1 } },
		/* the title's lettering (BG1) and DAWN OF SOULS bar (BG3) */
		.logo = { .steps = { { 0, 1000 } }, .layers = 0x0A, .maxY = 130 },
	},
	{
		/* Final Fantasy IV Advance (BZ4E; BZ4P, BZ4J below): START's menu
		 * as a party screen: see ff4_compose */
		.codes = { "BZ4E" },
		.title = "Final Fantasy IV Advance",
		.compose = ff4_compose,
		.virt = ff4_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* the title: the logo, the pictures dropped, no captures (FF4-2) */
		.live = { { FF4_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { FF4_V_LOGO, 0xFF, 1, 4 } },
		/* the title's art and lettering (BG2) */
		.logo = { .steps = { { 0, 200 }, { K_START, 4 }, { 0, 146 }, { K_START, 4 }, { 0, 146 }, { K_START, 4 }, { 0, 146 },
		                     { K_START, 4 }, { 0, 400 } }, .layers = 0x04, .maxY = 110, .lightEdgeClear = true },
	},
	{
		/* Final Fantasy V Advance: the party screen, START's menu without
		 * its commands: see ffv_compose
		 * (docs/games/final-fantasy-v-advance.md) */
		.codes = { "BZ5E" },
		.title = "Final Fantasy V Advance",
		.compose = ffv_compose,
		.virt = ffv_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* outside the game and at a game over: the logo, the pictures
		 * dropped, no captures (FV-2, FV-8) */
		.live = { { FFV_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { FFV_V_LOGO, 0xFF, 1, 4 } },
		/* another map, a battle begun or over: taken again at once (FV-3, FV-6) */
		.room = { { FFV_V_MAP, 0xFFFF, 0, 4 }, { FFV_V_BATTLE, 0xFF, 0, 4 } },
		/* the title's lettering and dragon (BG2) on its white background */
		.logo = { .steps = { { 0, 300 }, { K_A, 4 }, { 0, 1000 }, { K_A, 4 }, { 0, 300 } }, .layer = 2, .maxY = 110, .lightEdgeClear = true },
	},
	{
		/* Rebelstar: Tactical Command (USA; Europe: below). In a battle, the
		 * soldier's Status or the mission's Objectives from the field:
		 * see rs_compose (docs/games/rebelstar-tactical-command.md) */
		.codes = { "BRLE" },
		.title = "Rebelstar: Tactical Command",
		.composePages = rs_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the first page) */
		.virt = rs_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* taken only from the field, no training box up (RS-3, RS-4,
		 * RS-7) */
		.live = { { RS_V_MODE, 0xFF, RS_FIELD, 4 }, { RS_V_TUTOR, 0xFF, 0, 4 } },
		/* out of the battles: the logo, the pictures dropped (RS-2) */
		.menu = { { RS_V_MODE, 0xFF, RS_OUT, 4 } },
		/* the enemy's turn, the turn banners: the logo, the pictures
		 * kept (RS-6) */
		.logoIf = { { RS_V_MODE, 0xFF, RS_IDLE, 4 } },
		/* another soldier under the cursor, or none, or the field back:
		 * taken again at once (RS-3, RS-4) */
		.room = { { RS_V_KEY, 0, 0, 4 } },
		/* the title's lettering and star, painted into its picture, cut
		 * out by the owner's copy of the logo (RS-1; tools/make_logo_masks.py,
		 * rs). rev 1: the strip of the picture before */
		.logo = { .wait = 400, .mask = &logoMaskRS, .rev = 1 },
	},
	{
		/* Rebelstar: Tactical Command (Europe, in English, French, German,
		 * Spanish and Italian): the same, at its own addresses (rsEur) */
		.codes = { "BRLP" },
		.title = "Rebelstar: Tactical Command",
		.composePages = rs_compose_eur,
		.stillPick = STILL_FIRST,
		.virt = rs_virt_eur,
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		.live = { { RS_V_MODE, 0xFF, RS_FIELD, 4 }, { RS_V_TUTOR, 0xFF, 0, 4 } },
		.menu = { { RS_V_MODE, 0xFF, RS_OUT, 4 } },
		.logoIf = { { RS_V_MODE, 0xFF, RS_IDLE, 4 } },
		.room = { { RS_V_KEY, 0, 0, 4 } },
		/* the same title picture, past the language list (A: English;
		 * without a battery save it comes first) */
		.logo = { .steps = { { 0, 60 }, { K_A, 4 }, { 0, 400 } }, .mask = &logoMaskRS, .rev = 1 },
	},
	{
		/* Shin Megami Tensei (Japan; played in English with Gymzatan's
		 * translation, the same addresses): the automap, L in the
		 * first-person view (docs/games/shin-megami-tensei.md). The game's
		 * mode byte: 0 title and file lists, 1 walking, 0xA the command bar
		 * and START menu, 0x10 the map, 0x15 a fight, 0x19 the name entry. */
		.codes = { "AAUJ" },
		.title = "Shin Megami Tensei",
		.open = { { K_L, 4 } },
		/* the map draws itself in steps with pauses between them, the floor's
		 * name box last, about 52 frames after L: the probe at its right
		 * end is the last piece (SMT-2) */
		.settleMax = 200, .openWithin = 140, .stableFrames = 4, .maxDiff = 700,
		/* the map's part of the screen: the grid, the compass and the floor's
		 * name; the party's panels under it stay on the game screen (SMT-2) */
		.crop_x = 0, .crop_y = 0, .crop_w = 240, .crop_h = SMT_H,
		.compose = smt_compose,
		.probes = { { 10, 3, 0x31316B }, { 10, 60, 0x42429C }, { 212, 6, 0x492BE7 }, { 205, 10, 0xE74200 },
		            { 216, 98, 0x73738C }, { 232, 60, 0x292929 } },
		.tolerance = 8,
		.mem = { { SMT_MODE, 0xFF, 0x10, 1 } },
		/* only while walking about (SMT-4); the title and file lists: the
		 * logo, the map dropped (SMT-5) */
		.live = { { SMT_MODE, 0xFF, 0x01, 1 } },
		.menu = { { SMT_MODE, 0xFF, 0x00, 1 } },
		/* a step, a turn, another floor: a new map at once (SMT-3) */
		.room = { { SMT_X, 0, 0, 1 }, { SMT_Y, 0, 0, 1 }, { SMT_FACING, 0, 0, 1 }, { SMT_AREA, 0, 0, 1 } },
		/* the title's lettering, BG1 above the menu (SMT-1) */
		.logo = { .steps = { { 0, 300 }, { K_START, 4 }, { 0, 60 }, { K_START, 4 }, { 0, 60 }, { K_START, 4 },
		                     { 0, 60 }, { K_START, 4 }, { 0, 200 } },
		          .layer = 1, .maxY = 100 },
	},
	{
		/* Breath of Fire (ABFE USA, ABFP Europe, ABFJ Japan, ABFX Europe in
		 * English, French and German): the world map on the overworld, the
		 * party overview elsewhere: see bof_compose
		 * (docs/games/breath-of-fire.md) */
		.codes = { "ABFE", "ABFP", "ABFJ" },
		.title = "Breath of Fire",
		.compose = bof_compose,
		.virt = bof_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* the title and its intro: the logo, the pictures dropped, no captures
		 * (BOF-2); battles are taken like the rest: the overview kept (BOF-5) */
		.live = { { BOF_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { BOF_V_LOGO, 0xFF, 1, 4 } },
		/* another screen (the field back from a battle, a menu or the map
		 * opened or left): taken again at once (BOF-5, BOF-7) */
		.room = { { BOF_V_SCREEN, 0xFF, 0, 4 } },
		/* the title's lettering, sprites above PRESS START (BOF-1) */
		.logo = { .steps = { { 0, 1700 } }, .layers = 0x10, .maxY = 112 },
	},
	{
		/* Breath of Fire II (AB2E USA, AB2P Europe, AB2J Japan): the party overview
		 * from SELECT's menu: see bof2_compose (docs/games/breath-of-fire-ii.md) */
		.codes = { "AB2E", "AB2P" },
		.title = "Breath of Fire II",
		.compose = bof2_compose,
		.virt = bof2_virt,
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* the title, its demo and the intro: the logo, the pictures dropped, no captures
		 * (BOF2-2); battles are taken like the rest: the overview kept (BOF2-4) */
		.live = { { BOF2_V_LOGO, 0xFF, 0, 4 } },
		.menu = { { BOF2_V_LOGO, 0xFF, 1, 4 } },
		/* another screen (the field back from a battle, a menu opened or left): taken again at
		 * once (BOF2-4, BOF2-6) */
		.room = { { BOF2_V_SCREEN, 0xFF, 0, 4 } },
		/* the title's lettering, the sprites, above the copyright lines (BOF2-1) */
		.logo = { .steps = { { 0, 1500 } }, .layers = 0x10, .maxY = 120 },
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

/* Banjo-Kazooie: Grunty's Revenge, Europe's English, French and German
 * release: the language list (A: English) before the title (BK-1) */
static void bk_eu(MapProfile* p) {
	/* the pause byte 4 further on (Europe, Italy, Spain) */
	p->live[1].addr = BK_PAUSE_EU;
}
static void bk_lang(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 1200 }, { K_A, 4 }, { 0, 150 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
	bk_eu(p);
}
/* Breath of Fire in Europe's English, French and German release (ABFX): a
 * language list (A: English) before the title, the same as the others (BOF-1) */
static void bof_lang(MapProfile* p) {
	/* the list takes no A until it has been up a while: press it a few times; then the
	 * Ubi Soft and Capcom logos and the title's fade-in */
	static const MacroStep steps[] = { { 0, 400 }, { K_A, 3 }, { 0, 25 }, { K_A, 3 }, { 0, 25 }, { K_A, 3 }, { 0, 25 },
	                                   { K_A, 3 }, { 0, 25 }, { K_A, 3 }, { 0, 25 }, { K_A, 3 }, { 0, 1500 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* Breath of Fire II in Japan: the story text first, the title only after it; its lettering
 * is sprites and BG3 (the small BREATH OF FIRE and the (R)), above the copyright (BOF2-1) */
static void bof2_jp(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 4100 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
	p->logo.layers = 0x18;
}
static void lang_menu(MapProfile* p) {
	insert_steps(p->logo.steps, 24, 0, LANGUAGE_A, 2);
}
/* Final Fantasy I & II in Europe: its language screen first (A:
 * English). In Japan: FINAL FANTASY I・II, the ADVANCE bar
 * (BG2) and the katakana under it (BG3) (FF-1) */
static void ff_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 300 }, { K_A, 4 }, { 0, 1000 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
/* Final Fantasy V Advance in Europe: the language menu first (A: English) */
static void ffv_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 300 }, { K_A, 4 }, { 0, 300 }, { K_A, 4 }, { 0, 1000 }, { K_A, 4 }, { 0, 300 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}
static void ff_jpn(MapProfile* p) {
	p->logo.layers = 0x0E;
}

/* Final Fantasy VI Advance in Europe: a language screen (A: English) before
 * the health warning */
static void ff6_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 400 }, { K_A, 4 }, { 0, 400 }, { K_A, 4 }, { 0, 1500 }, { K_A, 4 }, { 0, 300 } };
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

/* in Japan the title's copyright line starts at row 99 */
static void ff6_jpn(MapProfile* p) {
	p->logo.maxY = 97;
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

/* Advance GT2, GT Advance 3's Japanese release: its own title, Advance GT2,
 * up by itself from frame ~600 (no disclaimer to pass); the races the same
 * (GT3-1) */
static void gt3_jpn(MapProfile* p) {
	p->logo = (typeof(p->logo)) { .steps = { { 0, 750 } }, .mask = &logoMaskGT3J };
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

/* Yu-Gi-Oh! Duel Monsters Expert 3 (Japan), World Championship Tournament
 * 2004's original: the same screens and addresses; its title's logo is
 * the lettering of 遊戯王 デュエルモンスターズ (sprites) over エキスパート3 (BG2)
 * and the EX, above Push Start (WCT-7) */
static void wct_jpn(MapProfile* p) {
	p->logo.layers = 0x14;
	p->logo.objMaxY = 114;
	p->logo.maxY = 114;
}

/* Yu-Gi-Oh! Duel Monsters International 2 (Japan): the same engine and
 * screens, its own addresses (ywJP, YWC-7) and title (no Language Selection) */
static void ywc_jpn(MapProfile* p) {
	p->title = "Yu-Gi-Oh! Duel Monsters International 2";
	p->compose = yw_compose_jp;
	p->virt = yw_virt_jp;
	p->logo = (typeof(p->logo)) { .wait = 1700, .layers = 0x10, .maxY = 112 };
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
/* Battle Network Rockman EXE 2 (Japan; its Rev 1 the same): the title's
 * logo on BG3 over its own dark grid, which comes in later */
static void mmbn2_jpn(MapProfile* p) {
	p->logo.wait = 560;
	p->logo.maxY = 140;
	p->logo.edgeClear[0] = 0x080808, p->logo.edgeClear[1] = 0x214A29;
	p->logo.edgeClear[2] = 0x316329, p->logo.edgeClear[3] = 0x4A7B39;
}
static void mmbn_jpn(MapProfile* p) {
	p->logo.maxY = GBA_H;
	p->logo.edgeClear[0] = 0x52B5FF;
	p->logo.rev = 1;
}

/* Harry Potter to Azkaban no Shuujin (Japan): the screen's byte 0x60
 * higher (hpa_compose, hpa_virt: HPA_J); no language screen before the
 * company screens; its logo the title's lettering too (BG2) */
static void hpa_jpn(MapProfile* p) {
	p->room[0].addr += 0x60;
	p->logo = (typeof(p->logo)) { .steps = { { 0, 360 }, { K_START, 4 }, { 0, 86 }, { K_START, 4 }, { 0, 86 }, { K_START, 4 }, { 0, 286 } },
	                              .layer = 2, .maxY = 120 };
}

/* Mario vs. Donkey Kong in Europe: its state 0x40 lower (romxref), its
 * language list first (START: English), then START skips the film
 * (MVDK-1, MVDK-8) */
static void mvdk_eur(MapProfile* p) {
	p->hud.on[0].addr -= 0x40;
	p->hud.freeze[0].addr -= 0x40;
	p->hud.alt.on[0].addr -= 0x40;
	p->hud.keep[0].addr -= 0x40;
	p->logo.steps[1] = (MacroStep) { K_START, 4 };
	p->logo.steps[2] = (MacroStep) { 0, 200 };
	p->logo.steps[3] = (MacroStep) { K_START, 4 };
	p->logo.steps[4] = (MacroStep) { 0, 300 };
}

/* Mario vs. Donkey Kong in Japan: the same state address; its own title,
 * the katakana lettering (MVDK-1) */
static void mvdk_jpn(MapProfile* p) {
	p->logo.mask = &logoMaskMVDKJ;
}

/* Pokémon Pinball: Ruby & Sapphire in Japan (BPPJ; the original, CRC32
 * 8af1ec73, and its Rev 1, 925a5684): the same table, score bar and EWRAM
 * layout under S and gMain as BPPE, found by matching the code round BPPE's
 * stores and literal pools (docs/PINBALL.md); only the game's state pointer
 * (0x10A0 lower: gCurrentPinballGame) with the camera's limit after it, and
 * the ROM's camera stores (the main routine's and catch mode's) differ
 * (PP-14). Rev 1's code has moved again (`rev`), its RAM is the original's. */
static void pp_jpn(MapProfile* p) {
	p->tall.live.state = 0x02030440;
	p->tall.live.bound = 0x02030498;
	p->tall.live.nop[0] = 0x0804B896, p->tall.live.nop[1] = 0x0804B8A6;
	p->tall.live.nop[2] = 0x0804B5E8, p->tall.live.nop[3] = 0x0804B5F8;
	p->tall.live.rev.crc = 0x925a5684;
	p->tall.live.rev.nop[0] = 0x0804B5DE, p->tall.live.rev.nop[1] = 0x0804B5EE;
	p->tall.live.rev.nop[2] = 0x0804B330, p->tall.live.rev.nop[3] = 0x0804B340;
}

/* Pokémon Pinball: Ruby & Sapphire in Europe (BPPP, a 16 MB dump with the
 * same game and five languages): its state pointer 0x120 higher and its
 * camera stores elsewhere (PP-15) */
static void pp_eur(MapProfile* p) {
	p->tall.live.state = 0x02031600;
	p->tall.live.bound = 0x02031658;
	p->tall.live.nop[0] = 0x08053B26, p->tall.live.nop[1] = 0x08053B36;
	p->tall.live.nop[2] = 0x08053880, p->tall.live.nop[3] = 0x08053890;
	/* gMain has a field more before the scroll shadows: they and the string
	 * after them are 4 bytes higher (BG1 / 2 / 3 VOFS 0x0200B3B2 / B6 / BA);
	 * the stack slot with the row + 16 holds a pointer into a table at
	 * 0x0894BED4 in place of the USA ROM's 0x086B0000 */
	p->tall.live.view = 0x0200B3BA;
	p->tall.live.jump.scrollAt[0] = 0x0200B3B2, p->tall.live.jump.scrollAt[1] = 0x0200B3B6;
	p->tall.live.jump.scrollAt[2] = 0x0200B3BA;
	p->tall.live.jump.plus16Base = 0xBED4;
	/* the language menu comes first (English selected, A), then the title
	 * as the USA release's: its logo waits (PP-4, PP-15) */
	static const MacroStep steps[] = { { 0, 400 }, { K_A, 4 }, { 0, 1440 } };
	p->logo.wait = 0;
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

/* Yu-Gi-Oh! Ultimate Masters in Europe (BY6P): the language menu first
 * (A: English, once it is up, not before: the Konami logo takes no A), then
 * the title as the USA release's. Its duel and Deck Edit screens are the
 * same layout (UM6-6). */
static void um6_eur(MapProfile* p) {
	static const MacroStep steps[] = { { 0, 620 }, { K_A, 4 }, { 0, 700 } };
	p->logo.wait = 0;
	memset(p->logo.steps, 0, sizeof(p->logo.steps));
	memcpy(p->logo.steps, steps, sizeof(steps));
}

static const Variant variants[] = {
	{ "BY6P", "BY6E", 0, um6_eur },
	{ "AGSD", "AGSE", 0, gs_ger },
	/* Boktai: Europe and Japan (the USA release's addresses: bk_compose) */
	{ "U3IP", "U3IE", 0, NULL },
	{ "U3IJ", "U3IE", 0, NULL },
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
	{ "A2GJ", "A2GE", 0, gt3_jpn },
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
	{ "BZMP", "BZME", 0, NULL },
	{ "BZMJ", "BZME", 0, tmc_jpn },
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
	/* Banjo-Kazooie: Grunty's Revenge: the same RAM but the pause byte
	 * (bk_eu); Europe's English, French and German release asks for a
	 * language first (BK-1) */
	{ "BKZX", "BKZE", 0, bk_lang },
	/* Breath of Fire: Europe's English, French and German release */
	{ "ABFX", "ABFE", 0, bof_lang },
	/* Breath of Fire II in Japan: a long story text before the title (BOF2-1) */
	{ "AB2J", "AB2E", 0, bof2_jp },
	{ "BKZI", "BKZE", 0, bk_eu },
	{ "BKZS", "BKZE", 0, bk_eu },
	{ "ANXP", "ANXE", 0, nfo_eur },
	{ "AWAJ", "AWAE", 0, NULL },
	{ "BM5P", "BM5E", 0, mvdk_eur }, /* (its state moved: mvdk_eur, mvdk_state) */
	{ "BM5J", "BM5E", 0, mvdk_jpn },
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
	{ "BYIJ", "BY7E", 0, ywc_jpn },
	/* World Championship Tournament 2004: Europe (its title has the kanji above
	 * YU-GI-OH!) and Japan's Duel Monsters Expert 3 (WCT-7) */
	{ "BYWP", "BYWE", 0, NULL },
	{ "BY3J", "BYWE", 0, wct_jpn },
	/* (Zoids Saga II's addresses: ZlRegion) */
	{ "AZ2J", "AZ2E", 0, NULL },
	{ "A4NJ", "A4NE", 0, hm_jpn },
	/* (Mega Man Battle Network's RAM: the same in every release) */
	{ "AREP", "AREE", 0, NULL },
	{ "BHTJ", "BHTE", 0, hpa_jpn },
	{ "AREJ", "AREE", 0, mmbn_jpn },
	/* (Final Fantasy I & II's addresses: FfRegion) */
	{ "BFFP", "BFFE", 0, ff_eur },
	{ "BFFJ", "BFFE", 0, ff_jpn },
	/* (Final Fantasy IV Advance: the same addresses and windows in every release) */
	{ "BZ4P", "BZ4E", 0, ff4_eur },
	{ "BZ4J", "BZ4E", 0, NULL },
	/* (Final Fantasy V Advance's addresses: FfvRegion) */
	{ "BZ5P", "BZ5E", 0, ffv_eur },
	{ "BZ5J", "BZ5E", 0, NULL },
	/* (Final Fantasy VI Advance's RAM: the same in every release) */
	{ "BZ6P", "BZ6E", 0, ff6_eur },
	{ "BZ6J", "BZ6E", 0, ff6_jpn },
	{ "AM2P", "AE2E", 0, NULL },
	{ "AE2J", "AE2E", 0, mmbn2_jpn },
	/* (Pokémon Pinball's cheats: its state moved, so the memory shift is only
	 * for profile_ram_moved; the profile reads nothing at the shifted addresses) */
	{ "BPPJ", "BPPE", -0x10A0, pp_jpn },
	{ "BPPP", "BPPE", 0x120, pp_eur },
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

/* ---------------------------------------------------------- colour patches
 * A known patch's result (docs/patches.md) that changes colours the profile
 * looks for (the map screen's probes, the lettering map_crop erases...):
 * those colours, swapped for the ones the patched game draws. Found by
 * opening the screens on the clean and the patched ROM and pairing their
 * pixels. The game being played says which ROM it is (profile_set_rom). */
typedef struct {
	char code[5];
	uint32_t crc; /* the patched ROM */
	int n;
	uint32_t from[8], to[8];
} ColorSwap;
static const ColorSwap colorSwaps[] = {
	/* Metroid: Zero Mission's colour improvement v1 and v2: the pause
	 * screen's dark grey-green (its sides and the map's background) is black */
	{ "BMXE", 0x70409097, 1, { 0x212921 }, { 0x000000 } },
	{ "BMXE", 0x4c0e3216, 1, { 0x212921 }, { 0x000000 } },
};
static const ColorSwap* romSwap; /* the game being played's, or NULL */
static MapProfile swapped;        /* its profile with the colours swapped */

static void swap_colour(uint32_t* c) {
	for (int i = 0; romSwap && i < romSwap->n; ++i) {
		if (*c == romSwap->from[i]) {
			*c = romSwap->to[i];
			return;
		}
	}
}

static const MapProfile* find_unswapped(const char* gameCode);

void profile_set_rom(const char* gameCode, uint32_t crc) {
	romSwap = NULL;
	for (size_t i = 0; gameCode && i < sizeof(colorSwaps) / sizeof(colorSwaps[0]); ++i)
		if (!strncmp(colorSwaps[i].code, gameCode, 4) && colorSwaps[i].crc == crc) romSwap = &colorSwaps[i];
	const MapProfile* p = romSwap ? find_unswapped(gameCode) : NULL;
	if (!p) {
		romSwap = NULL;
		return;
	}
	swapped = *p;
	for (size_t i = 0; i < sizeof(swapped.probes) / sizeof(swapped.probes[0]); ++i) swap_colour(&swapped.probes[i].rgb);
	for (size_t i = 0; i < sizeof(swapped.glyphs) / sizeof(swapped.glyphs[0]); ++i)
		for (int k = 0; k < 8; ++k) swap_colour(&swapped.glyphs[i].rgb[k]);
	for (size_t i = 0; i < sizeof(swapped.recolor) / sizeof(swapped.recolor[0]); ++i)
		for (int k = 0; k < 16; ++k) swap_colour(&swapped.recolor[i].from[k]), swap_colour(&swapped.recolor[i].to[k]);
	for (int k = 0; k < 12; ++k) swap_colour(&swapped.tab.colors[k]);
	swap_colour(&swapped.bgColor);
	swap_colour(&swapped.rate.text);
	fprintf(stderr, "profile: colours swapped for the patched ROM %08x (%d)\n", crc, romSwap->n);
}

const MapProfile* profile_find(const char* gameCode) {
	if (romSwap && !strncmp(romSwap->code, gameCode, 4)) return &swapped;
	return find_unswapped(gameCode);
}

static const MapProfile* find_unswapped(const char* gameCode) {
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
	bool mod; /* a romhack or patch (a Mods row), not one of No-Intro's dumps */
	const char* name;
} KnownDump;
#include "dumps_data.h"

const LogoSpec* profile_logo_spec(const MapProfile* p, uint32_t fileCrc) {
	for (int i = 0; p && i < p->nLogoFor; ++i)
		if (p->logoFor[i].crc == fileCrc) return &p->logoFor[i].logo;
	return p ? &p->logo : NULL;
}

DumpKind profile_dump(const char* gameCode, uint32_t crc, const char** name) {
	bool listed = false;
	if (name) *name = NULL;
	for (size_t i = 0; i < sizeof(knownDumps) / sizeof(knownDumps[0]); ++i) {
		if (strncmp(knownDumps[i].code, gameCode, 4)) continue;
		if (!knownDumps[i].mod) listed = true; /* (only patches known for it: not checked) */
		if (knownDumps[i].crc != crc) continue;
		if (name) *name = knownDumps[i].name;
		if (knownDumps[i].mod) return knownDumps[i].tested ? DUMP_MOD_TESTED : DUMP_MOD;
		return knownDumps[i].tested ? DUMP_TESTED : DUMP_OFFICIAL;
	}
	return listed ? DUMP_MODIFIED : DUMP_UNLISTED;
}

uint32_t doCrc32(const void* buf, size_t size); /* (libmgba's: its header wants zlib.h) */
const KnownPatch* profile_patch(const uint8_t* file, size_t size, int n) {
	if (size <= 4) return NULL;
	uint32_t id = doCrc32(file, size - 4);
	for (size_t i = 0; i < sizeof(knownPatches) / sizeof(knownPatches[0]); ++i) {
		if (knownPatches[i].id != id || knownPatches[i].size != size) continue;
		if (n-- == 0) return &knownPatches[i];
	}
	return NULL;
}

/* The release's name as No-Intro gives it ("Mario & Luigi - Superstar Saga
 * (USA)"): its tested dump's, else its first official one's, without what
 * sets that dump apart ("(Rev 1)", "(Virtual Console)"...): the region and
 * languages kept. NULL for a release with no list of dumps. (GEN-9's
 * patches folder.) */
bool profile_release_name(const char* gameCode, char* out, size_t size) {
	const char* name = NULL;
	for (size_t i = 0; i < sizeof(knownDumps) / sizeof(knownDumps[0]); ++i) {
		if (strncmp(knownDumps[i].code, gameCode, 4) || knownDumps[i].mod) continue;
		if (!name || knownDumps[i].tested) name = knownDumps[i].name;
		if (knownDumps[i].tested) break;
	}
	if (!name) return false;
	/* the title, then "(Region)", then "(En,Fr,...)" if it's languages */
	const char* p = strchr(name, '(');
	size_t keep = p ? (size_t) (p - name) : strlen(name);
	while (keep && name[keep - 1] == ' ') --keep;
	const char* e = p ? strchr(p, ')') : NULL;
	if (e) {
		keep = (size_t) (e + 1 - name); /* the region */
		if (e[1] == ' ' && e[2] == '(') {
			const char* p2 = e + 2;
			const char* e2 = strchr(p2, ')');
			bool langs = e2 && e2 > p2 + 1;
			for (const char* q = p2 + 1; langs && q < e2; ++q) {
				if (!((*q >= 'A' && *q <= 'Z') || (*q >= 'a' && *q <= 'z') || *q == ',')) langs = false;
			}
			if (langs) keep = (size_t) (e2 + 1 - name);
		}
	}
	snprintf(out, size, "%.*s", (int) keep, name);
	return true;
}

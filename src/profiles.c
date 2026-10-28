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

typedef struct {
	uint32_t army, cursor, units, menuRow, minimapOpen; /* (minimapOpen: unused since the minimap went) */
	int intelItem; /* Intel's row in the SELECT menu */
	uint32_t intelPx[2][3]; /* the Battle Intel is up: x, y, colour (0xRRGGBB) */
	int intelRows;          /* the Battle Intel's panel: the rows above the map */
	uint32_t unitOpen[2];   /* this word holds this value (the screen's routine) while the unit intel (R) is up */
} AwRegion;

static const AwRegion awRegions[] = {
	{ AW_ARMY, AW_CURSOR, AW_UNITS, AW_MENU_ROW, AW_MINIMAP_OPEN, 1, { { 4, 4, 0xFFE7C6 }, { 200, 100, 0xFFE7C6 } }, 124, { 0x03000ED0, 0x082E3FD4 } },
	/* (its SELECT menu has no Unit item: CO, Intel, Options, Save, End) */
	{ AW2_ARMY, AW2_CURSOR, AW2_UNITS, AW2_MENU_ROW, AW2_MINIMAP_OPEN, 1, { { 200, 100, 0xFFEFEF }, { 120, 140, 0xFFEFEF } }, 152, { 0x03001530, 0x0849E2C0 } },
	/* Game Boy Wars Advance 1+2 (Japan): each game at its own addresses
	 * (game 1's found by the literal pools around its code: 0x240 lower
	 * than Advance Wars' above 0x03003000, 0x10 lower around 0x03000E00;
	 * the unit intel's routine by its shape; game 2's by watching them) */
	{ GBW1_ARMY, GBW1_CURSOR, GBW1_UNITS, 0x03000E40, 0x03000E1E, 1, { { 4, 4, 0xFFE7C6 }, { 200, 100, 0xFFE7C6 } }, 124, { 0x03000EC0, 0x08DEC9D8 } },
	{ GBW2_ARMY, GBW2_CURSOR, GBW2_UNITS, AW2_MENU_ROW, AW2_MINIMAP_OPEN, 1, { { 200, 100, 0xFFEFEF }, { 120, 140, 0xFFEFEF } }, 152, { 0x03001530, 0x0881A0A4 } },
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
	return &awRegions[s->gameCode[2] == '2' ? 1 : 0];
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
	static const SpriteRule rules[2] = { { -512, -256, 511, 255, 0, 1023, 3, 0, 0 }, AW_WEATHER };
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
	emu_hide_sprites(s, rules, 2);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	for (int i = 0; i < GBA_W * GBA_H; ++i) pan[i] = s->fb[i] == none[i] ? AW_BG : s->fb[i];
	emu_hide_sprites(s, &rules[1], 1);
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
		aw_tap(s, K_SELECT, 1);
		aw_run(s, 0, 8);
		aw_pick(s, r->intelItem); /* Intel */
		aw_tap(s, K_A, 1);
		aw_run(s, 0, 8);
		aw_pick(s, 0); /* Status (the Intel menu keeps its last choice: CO, Rules) */
		aw_tap(s, K_A, 1);
		if (!aw_settle(s, aw_intel_up, 90)) return "battle intel did not open";
	}
	return NULL;
}

/* The second screen (AW-1): the unit intel of the unit under the cursor
 * (R), where the game puts it, or else the Battle Intel's panel, centred;
 * on AW_BG. */
static const char* aw_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	const AwRegion* r = aw_r(s);
	static uint32_t pan[GBA_W * GBA_H], bg2[GBA_W * GBA_H];
	bool unitPanel = false;
	/* weather (rain falling over the menus) out of every picture: the
	 * menus settle, and it isn't kept in them */
	static const SpriteRule weather = AW_WEATHER;
	emu_hide_sprites(s, &weather, 1);
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

/* -------------------------------------------------- Summon Night: Swordcraft Story
 *
 * The second screen is the Status screen (START > Status), the whole
 * screen, with the menu's Place bar (where you are) over its bottom rows:
 * the box with the name across the whole width, its PLACE heading only as
 * far as the stats' lines start (clear of the item row under the stats).
 * Both are brought up on the shadow core: START (from walking about), DOWN
 * to Status, A; the Place bar is taken from the menu on the way. Where the
 * menu won't open (a scene, a fight), the last picture stays. (The Japanese
 * release, Craft Sword Monogatari, has the same addresses.) */
#define SN_MENU 0x03000120     /* 0 in the START menu, 4 on its Status screen */
#define SN_MENU_ROW 0x03000122 /* the START menu's cursor: Status is 5 */
#define SN_MENU_DISP 0x3340    /* DISPCNT in the menu and its screens */
#define SN_PLACE_Y 136         /* the Place bar: the menu's rows 136-159 */
#define SN_PLACE_HEAD 144      /* its heading (PLACE) above this row, the box with the name below */
#define SN_STATS_X 80          /* where the Status's lines between ATK, DEF... start */
#define SN_CAP_X0 233          /* the heading's end piece, columns 233-238 */
#define SN_CAP_X1 238





#define SN_V_TITLE 0xF0000020  /* 1 on the title screen (BG1-3 set up as there) */

static uint32_t sn_virt(Emu* e, uint32_t addr) {
	if (addr != SN_V_TITLE) return 0;
	return emu_read16(e, 0x0400000A) == 0x1F0B && emu_read16(e, 0x0400000C) == 0x0F06;
}

static bool sn_in(Emu* s, int menu) {
	return emu_read16(s, 0x04000000) == SN_MENU_DISP && emu_read8(s, SN_MENU) == menu;
}

static bool sn_wait(Emu* s, int menu, int frames) {
	for (int f = 0; f < frames && !sn_in(s, menu); ++f) emu_frame(s, 0);
	return sn_in(s, menu);
}

static const char* sn_compose(Emu* s, const void* state, uint32_t* out, int w, int h) {
	static uint32_t place[GBA_W * (GBA_H - SN_PLACE_Y)];
	static bool havePlace;
	static uint32_t status[GBA_W * GBA_H];
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (sn_in(s, 4)) {
		/* the Status open: back to the menu for the Place bar first */
		aw_tap(s, K_B, 1);
		if (!sn_wait(s, 0, 90)) return "the menu didn't come back";
	} else if (!sn_in(s, 0)) {
		aw_tap(s, K_START, 1);
		if (!sn_wait(s, 0, 60)) return "the menu didn't open";
	}
	aw_run(s, 0, 20); /* (past its fade-in) */
	memcpy(place, &s->fb[SN_PLACE_Y * GBA_W], sizeof(place));
	havePlace = true;
	for (int i = 0; i < 10 && emu_read8(s, SN_MENU_ROW) != 5; ++i) aw_tap(s, K_DOWN, 1);
	if (emu_read8(s, SN_MENU_ROW) != 5) return "Status not reached in the menu";
	aw_tap(s, K_A, 1);
	if (!sn_wait(s, 4, 90)) return "the Status didn't open";
	aw_run(s, 0, 40); /* (past its fade-in) */
	memcpy(status, s->fb, sizeof(status));
	int cw = w < GBA_W ? w : GBA_W;
	for (int y = 0; y < h && y < GBA_H; ++y) {
		uint32_t* o = &out[y * w];
		memcpy(o, &status[y * GBA_W], (size_t) cw * 4);
		if (!havePlace || y < SN_PLACE_Y) continue;
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
                              * order...; Japan: 0x30 lower) */
#define FT_IN_BATTLE 0x03000E4E /* 1 in a battle (its scenes, moves, menus too), else 0 */
#define FT_SCENE 0x0201639C /* 0 while a battle's fought; set while a scene plays on the
                             * battle map (the camera turning to the last enemy KO'd,
                             * the talk once it's won, the snowball fight's end), so
                             * the battle's over for the second screen (USA, Europe;
                             * Japan not known: not used there). (0x03000E08 stays set
                             * from the first battle won on; 0x0202F254 is set at the
                             * snowball fight's end only.) */
#define FT_V_BATTLE 0xF0000010 /* 1 in a battle, outside its scenes (FT_SCENE) */
#define FT_V_IDLE 0xF0000011   /* 1 on a battle's map or a unit's Status (not a move or an action) */

static uint32_t ft_virt(Emu* e, uint32_t addr) {
	/* (a scene on the battle map is out of it: the logo; above all the
	 * one once the battle's won) */
	bool in = emu_read8(e, FT_IN_BATTLE) == 1 && (e->gameCode[3] == 'J' || !emu_read32(e, FT_SCENE));
	if (addr == FT_V_BATTLE) return in;
	if (addr != FT_V_IDLE) return 0;
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

/* The Status screen's two pages into page[0], page[1], of the unit under
 * the cursor (or the one open already); false if there's none there or
 * it didn't open. */
static bool ft_status(Emu* s, const void* state, uint32_t page[2][GBA_W * GBA_H]) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_STATUS_CNT)) {
		if (!ft_bg0(s, FT_MAP_CNT) || !ft_back(s)) return false;
		/* no unit under the cursor: no Status (FT-9: the turn order) */
		uint32_t jp = s->gameCode[3] == 'J' ? 0x30 : 0, at = emu_read32(s, FT_AT_UNIT - jp);
		if (!at) return false;
		/* another unit than the one whose turn it is: A brings up its own
		 * menu (2); the judge has none, and A on it brings up the active
		 * unit's (0) instead: no Status for the judge (FT-9) */
		bool other = at != emu_read32(s, FT_ACTIVE - jp);
		uint32_t ui = FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0);
		bool sel = false;
		int moves = 0;
		for (int i = 0; i < 14 && !sel; ++i) {
			aw_run(s, 0, 8);
			int orange, white;
			ft_menu_row(s, &orange, &white);
			uint8_t u = emu_read8(s, ui);
			bool menu = u == 0 || u == 2; /* (the active unit's, another unit's) */
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
static bool ft_cut(Emu* s, void* tmp, uint32_t* out, int* ow, int* oh, int minW, int maxW, int minH, int maxH) {
	if (!emu_state_save(s, tmp)) return false;
	static uint32_t none[GBA_W * GBA_H];
	emu_layers(s, 0);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	memcpy(none, s->fb, sizeof(none));
	emu_layers(s, 0x0C);
	emu_state_load(s, tmp);
	emu_frame(s, 0);
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1;
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			if (s->fb[y * GBA_W + x] == none[y * GBA_W + x]) continue;
			x0 = x < x0 ? x : x0, x1 = x > x1 ? x : x1, y0 = y < y0 ? y : y0, y1 = y > y1 ? y : y1;
		}
	}
	int w = x1 - x0 + 1, h = y1 - y0 + 1;
	bool ok = x1 >= 0 && w >= minW && w <= maxW && h >= minH && h <= maxH;
	if (ok) {
		*ow = w, *oh = h;
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				uint32_t c = s->fb[(y0 + y) * GBA_W + x0 + x];
				out[y * w + x] = c == none[(y0 + y) * GBA_W + x0 + x] ? FT_HOLE : c | 0xFF000000;
			}
		}
	}
	emu_layers(s, 0x1F);
	emu_state_load(s, tmp);
	return ok;
}

/* The turn order list (L) cut out; false if it didn't open. */
static bool ft_list(Emu* s, const void* state, void* tmp, uint32_t* out, int* ow, int* oh) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_MAP_CNT) && !ft_bg0(s, FT_STATUS_CNT)) return false;
	if (!ft_back(s)) return false;
	aw_tap(s, K_L, 1);
	aw_run(s, 0, 40);
	return ft_cut(s, tmp, out, ow, oh, 100, 180, 100, GBA_H);
}

/* SYSTEM (Start on the map) > row (1 Mission, 2 Laws) and its window cut
 * out; false if it didn't open. The menu's rows are 16 lines apart from
 * line 48, the selected one lettered in orange. */
static bool ft_system(Emu* s, const void* state, void* tmp, int row, uint32_t* out, int* ow, int* oh) {
	if (!emu_state_load(s, state)) return false;
	emu_frame(s, 0);
	if (!ft_bg0(s, FT_MAP_CNT) && !ft_bg0(s, FT_STATUS_CNT)) return false;
	if (!ft_back(s)) return false;
	aw_tap(s, K_START, 1);
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
	return row == 1 ? ft_cut(s, tmp, out, ow, oh, 150, GBA_W, 100, GBA_H)
	                : ft_cut(s, tmp, out, ow, oh, 150, GBA_W, 24, 90);
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

static const char* ft_compose(Emu* s, const void* state, uint32_t* const* out, int* nOut, int* step, int w, int h) {
	static uint32_t page[2][GBA_W * GBA_H];
	static uint32_t win[2][GBA_W * GBA_H]; /* the turn order, the Laws */
	static int pages, ww[2], wh[2], age;
	static uint32_t key;
	int max = *nOut;
	*nOut = 1;
	*step = 300; /* 5 seconds a page */
	if (!emu_state_load(s, state)) return "state load failed";
	emu_frame(s, 0);
	if (!ft_virt(s, FT_V_IDLE)) return "not on the battle map";
	/* taken again when the cursor moves, or every 4th picture (the units
	 * act, the turn order turns) */
	uint32_t k = emu_read32(s, ft_cursor(s)) ^ (uint32_t) emu_read16(s, 0x04000008) << 16;
	/* (and what's open: A on the judge brings up the active unit's menu
	 * with the cursor where it was; that's when its Status shows) */
	k = (k ^ emu_read8(s, FT_UI - (s->gameCode[3] == 'J' ? 0x30 : 0))) * 16777619u;
	bool again = k != key || ++age >= 4 || (!pages && !ww[0]);
	void* tmp = malloc(emu_state_size(s));
	if (!tmp) return "out of memory";
	if (again) {
		static uint32_t np[2][GBA_W * GBA_H], nw[2][GBA_W * GBA_H];
		/* a unit under the cursor: its Status (FT-8); else the turn order
		 * and the Laws (FT-9, FT-12); kept only if the capture wasn't
		 * called off on the way (the cursor moved on) */
		bool st = ft_status(s, state, np), ok[2] = { false, false };
		int a[2] = { 0 }, b[2] = { 0 };
		for (int i = 0; !st && i < 2; ++i) {
			ok[i] = i == 0 ? ft_list(s, state, tmp, nw[i], &a[i], &b[i]) : ft_system(s, state, tmp, 2, nw[i], &a[i], &b[i]);
		}
		if (emu_cancelled(s)) {
			free(tmp);
			return "called off";
		}
		if (st) {
			memcpy(page, np, sizeof(page)), pages = 2;
		} else {
			pages = 0;
			for (int i = 0; i < 2; ++i) {
				if (ok[i]) memcpy(win[i], nw[i], (size_t) a[i] * b[i] * 4), ww[i] = a[i], wh[i] = b[i];
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
		/* the Laws above the turn order, each centred across, the two
		 * centred down the screen; where they don't both fit (the list's 8
		 * rows don't), the Laws at the top and the list's last rows off
		 * the bottom (its first ones, who acts next, matter most) */
		int lh = ww[1] ? wh[1] : 0, th = ww[0] ? wh[0] : 0;
		int y0 = lh + th <= h ? (h - lh - th) / 2 : 0;
		if (ww[1]) ft_put(o, w, h, win[1], ww[1], wh[1], (w - ww[1]) / 2, y0);
		if (ww[0]) ft_put(o, w, h, win[0], ww[0], wh[0], (w - ww[0]) / 2, y0 + lh);
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
	char region; /* the game code's last letter */
	uint32_t chapter; /* the chapter's data: 0 off the map (its play time) */
	uint32_t procs; /* the process slots */
	uint32_t cursor; /* the map cursor's x, y (16 bits each) */
	uint32_t units; /* every unit's data (0x48 bytes each) */
	uint32_t phase, event, battle, banner, status, status2; /* process scripts (Europe: a status screen per language) */
} FeRegion;
static const FeRegion feRegions[] = {
	{ 'E', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08B93374, 0x08B90D88, 0x08B9A99C, 0x08B937AC, 0x08CC1F5C },
	{ 'Y', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08C57730, 0x08C54ECC, 0x08C5ECE8, 0x08C57B8C, 0x08D88F1C, 0x08DC195C },
	{ 'X', 0x0202BBF8, 0x02024E28, 0x0202BBCC, 0x0202BD50, 0x08C8EBA0, 0x08C8C33C, 0x08C96158, 0x08C8EFFC, 0x08DC195C },
	{ 'J', 0x0202BBF4, 0x02024E20, 0x0202BBC8, 0x0202BD4C, 0x08C02630, 0x08BFFF78, 0x08C09CE8, 0x08C02A68, 0x08D8A520 },
};
static const FeRegion* fe_r = &feRegions[0];
static void fe_region(Emu* e) {
	for (size_t i = 0; i < sizeof(feRegions) / sizeof(*feRegions); ++i) {
		if (e->gameCode[3] == feRegions[i].region) fe_r = &feRegions[i];
	}
}
#define FE_CHAPTER (fe_r->chapter)
#define FE_TURN (fe_r->chapter + 0xF) /* whose turn: 0 yours, 0x40 other, 0x80 the enemy's */
#define FE_PROCS (fe_r->procs)
#define FE_CURSOR (fe_r->cursor)
#define FE_P_PHASE (fe_r->phase) /* the player phase */
/* (its positions, the same in each release) */
#define FE_AT_MINI 0x50  /* the minimap (and the map menu) */
#define FE_AT_IDLE 0x70  /* the cursor free */
#define FE_AT_MENU 0xD0  /* a unit's menu: Attack, Wait... */
#define FE_AT_FIGHT 0xF0 /* a fight */
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
		/* the Status screen with the menu's Place bar over its bottom:
		 * see sn_compose */
		.codes = { "AB4E", "AB4J" },
		.title = "Summon Night: Swordcraft Story",
		.compose = sn_compose,
		.virt = sn_virt,
		/* the GBA's own screen, scaled as the game screen is */
		.crop_w = GBA_W,
		.crop_h = GBA_H,
		/* the title screen: the logo again (and no captures there) */
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
			.moves = { { 104, 0, 135, 15, 0, 0, 0, 0, true }, { 0, 16, 239, 159, 0, 0, 0, 0, true } },
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
		/* the second screen is put together by ft_compose: a unit's Status,
		 * or the turn order with the Laws */
		.codes = { "AFXE" },
		.title = "Final Fantasy Tactics Advance",
		.virt = ft_virt,
		.composePages = ft_compose,
		.stillPick = STILL_FIRST, /* (Map Animations off: the Status's first page) */
		.crop_w = GBA_W, /* a GBA screen, scaled as the game is */
		.crop_h = GBA_H,
		/* taken on the battle map (or a unit's Status open); kept while a
		 * unit moves or acts, and through the battle's scenes */
		.live = { { FT_V_IDLE, 0xFF, 1, 4 } },
		.menu = { { FT_V_BATTLE, 0xFF, 0, 4 } },
		.room = { { FT_CURSOR, 0xFFFF, 0, 2 }, { FT_UI, 0xFF, 0, 1 } },
		.hud = {
			/* nothing moves off the game screen (FT-10); this is for the
			 * logo before the first picture and outside battles (FT-7) */
			.on = { { FT_V_BATTLE, 0xFF, 1, 4 } },
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
		.combat = { { FE_V_PHASE, 0xFFFFFFFF, FE_AT_FIGHT + 1, 4 } },
		.combatOr = { FE_IN_CHAPTER, { FE_V_TURN, 0xFF, 0x40, 1, true, 0xFF, true } },
		/* the title's lettering and sword (sprites), its shadow (BG1)
		 * and axe (BG2), not the copyright line */
		.logo = { .wait = 300, .pressStart = true, .wait2 = 64, .layers = 0x16, .maxY = 140 },
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
		.live = { { AW_ARMY, 0xFF, 1, 1, true, 4, true } },
		.menu = { { AW_ARMY, 0xFF, 0, 1 }, { 0x03004464, 0xFFFFFFFF, 0, 4 } },
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
		.live = { { AW2_ARMY, 0xFF, 1, 1, true, 4, true } },
		.menu = { { AW2_ARMY, 0xFF, 0, 1 }, { 0x030040D8, 0xFFFFFFFF, 0, 4 } },
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
static void gbw_set(MapProfile* p, uint32_t army, uint32_t cursor, uint32_t menu) {
	p->combat[0].addr = army;
	p->live[0].addr = army;
	p->menu[0].addr = army;
	p->menu[1].addr = menu;
	p->room[0].addr = cursor;
	memset(&p->logo, 0, sizeof(p->logo)); /* (the compilation's: BGWJ) */
}

static void gbw1(MapProfile* p) {
	p->title = "Game Boy Wars Advance 1+2: Game Boy Wars Advance";
	gbw_set(p, GBW1_ARMY, GBW1_CURSOR, 0x03004224);
}

static void gbw2(MapProfile* p) {
	p->title = "Game Boy Wars Advance 1+2: Game Boy Wars Advance 2";
	gbw_set(p, GBW2_ARMY, GBW2_CURSOR, 0x03004248);
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
	memset(p->room, 0, sizeof(p->room)); /* (not known in Japan: the refresh alone) */
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

/* Final Fantasy Tactics Advance in Europe: a language menu first (its RAM
 * is the USA release's); in Japan the cursor's 0x30 lower (ft_r) */
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

static const Variant variants[] = {
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
	{ "AT3F", "AT3E", 0, thps3_fra },
	{ "AT3D", "AT3E", 0, thps3_ger },
	/* (Fire Emblem's addresses: FeRegion) */
	{ "AE7X", "AE7E", 0, lang_menu },
	{ "AE7Y", "AE7E", 0, lang_menu },
	{ "AE7J", "AE7E", 0, fe_jpn },
	{ "AWRP", "AWRE", 0, aw_eur },
	/* (the same RAM as the USA release; a language menu first too) */
	{ "AW2P", "AW2E", 0, aw_eur },
	/* (the two games in Game Boy Wars Advance 1+2: BGWJ's pick) */
	{ "BGW1", "AWRE", 0, gbw1 },
	{ "BGW2", "AW2E", 0, gbw2 },
	/* (Sword of Mana's ring address: SomRegion) */
	{ "AVSY", "AVSE", 0, som_eur },
	{ "AVSJ", "AVSE", 0, som_jpn },
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

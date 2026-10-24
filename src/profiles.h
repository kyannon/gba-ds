/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef GBADS_PROFILES_H
#define GBADS_PROFILES_H

#include "emu.h"
#include "logo_masks.h"

typedef struct {
	uint32_t keys;
	int frames;
} MacroStep;

/* A screen pixel that must have a given colour while the map is up. */
typedef struct {
	int x, y;
	uint32_t rgb; /* 0xRRGGBB */
} Probe;

/* A memory location that must hold a given value while the map is up. */
typedef struct {
	uint32_t addr; /* 0 terminates the list */
	uint32_t mask;
	uint32_t value;
	int size; /* 1, 2 or 4 bytes */
	bool hasAlt; /* also accept `alt` (A Link to the Past: overworld OR dungeon) */
	uint32_t alt;
	bool range; /* with hasAlt: anything from value to alt */
} MemProbe;

/* A piece of a split-off HUD moved on the way to the screen: what's inside
 * the box (inclusive, HUD pixels), measured each frame, is placed by its
 * left edge / centre / right edge at x ('L' 'C' 'R'; 0 stays) and its top /
 * middle / bottom at y ('T' 'M' 'B'; 0 stays), on the second screen or, with
 * toGame, back onto the game picture. */
typedef struct {
	int x0, y0, x1, y1;
	char how; int x;
	char vhow; int y;
	bool toGame;
	/* hud.overMap: a box of this colour (0xRRGGBB; 0: none) behind the
	 * piece, `pad` pixels round it */
	uint32_t box;
	int pad;
	/* hud.overMap: the piece is taken again only when the byte at this
	 * address changes (once it has held still for 8 frames); 0: whenever
	 * it changes. Pieces stay while the HUD is hidden either way. */
	uint32_t latch;
	/* hud.overMap: the piece goes with move number `follow` (1-4): moved as
	 * far as that one, so it keeps its place beside it; drawn over
	 * everything, the capture's panels too; gone whenever that one is
	 * taken without it (Advance Wars' DAMAGE box over the info). */
	int follow;
	/* hud.overMap: taken only once its pixels have held still this many
	 * frames (Fire Emblem's windows lose their figures, then their
	 * pictures, over the frames before they go) */
	int settle;
	/* hud.overMap: dropped once it's been missing this many frames while
	 * the HUD is on (Fire Emblem's unit window: the cursor on an empty
	 * place); 0: kept */
	int dropAfter;
	/* hud.overMap: placed where it is on the game screen (the game screen
	 * centred in the map picture) instead of by how / vhow */
	bool asIs;
	/* hud.overMap: while it's there, the other pieces aren't drawn, and it
	 * goes over everything (Fire Emblem's conversations) */
	bool alone;
	/* hud.overMap: drawn over the map picture's own pixels too, not only
	 * where it has mapClear (Fire Emblem's windows over a unit's status) */
	bool top;
	/* with asIs: a piece that reaches the game screen's bottom edge goes
	 * against the map picture's bottom (Fire Emblem's portraits) */
	bool asIsBottom;
	/* hud.overMap: when it's taken, the other pieces are dropped (taken
	 * again as they show up): Fire Emblem's phase banner */
	bool clears;
} HudMove;

/* Bytes written into the ROM when the game starts, if the ROM holds `orig`
 * there (NULL: the ROM's 0xFF padding), so another revision is left alone. */
typedef struct {
	uint32_t addr; /* 0 terminates the list */
	int len;
	const uint8_t* orig;
	const uint8_t* bytes;
} RomPatch;

typedef struct MapProfile {
	const char* codes[4]; /* 4-char game codes this profile handles */
	const char* title;
	const char* listName; /* name in the game list instead of the file name */

	MacroStep open[8]; /* button sequence that brings up the map */
	/* Optional second step: once the map is up (mem probes), if open2If
	 * matches, wait open2Delay frames and press open2 (A Link to the Past:
	 * L again on the world map zooms out to the whole world). */
	MemProbe open2If[2];
	int open2Delay;
	MacroStep open2[4];
	int settleMax;     /* frames to wait after the macro for the map to settle */
	int openWithin;    /* give up if the memory probes don't match within this many frames (0 = settleMax) */
	/* Dim the last map while the map can't be brought up (A Link to the
	 * Past: L does nothing indoors without a dungeon map). Other games keep
	 * the last map at full brightness: their captures also fail briefly
	 * during room transitions. */
	bool dimWhenUnavailable;
	/* No map yet: the game is listed and the second screen shows its logo,
	 * but nothing is captured. */
	bool logoOnly;
	/* Logo-only games: screens worth copying to the second screen (Tony
	 * Hawk's goal sheets). When `show` matches (at least showMin probes, for
	 * `stable` frames) the game screen is copied there and stays through play,
	 * pauses and loading; it goes back to the logo when one of the `revert`
	 * screens (menus) matches. Probe colours within `tol` per channel. */
	struct {
		Probe show[8];
		int showMin;
		Probe revert[2][8];
		int revertMin[2];
		int stable, tol;
		/* sprites left out of the copy (the frame is rendered again
		 * without them): THPS2's CAREER / CASH counters and button prompts */
		SpriteRule hide[4];
	} snap;
	/* A second button layout for games ported from four-face-button
	 * consoles (Tony Hawk's: GBA L / R stand in for two face buttons). The
	 * four-face layout puts GBA A on the top face button, B on the bottom,
	 * L on the left and R on the right. A picker on the second screen
	 * chooses it, the GBA layout, or Auto: four-face only while the player
	 * controls the character: the display is in `mode` and there are
	 * sprites at each of the `hud` positions (the in-game HUD, which menus
	 * and the pause screen don't show). */
	struct {
		bool fourFace;
		int mode;
		struct { int x, y; } hud[4];
		/* Auto, instead of mode and hud: the player is skating while these
		 * match (THPS3: the display with no menu layer) */
		MemProbe playing[2];
		/* Games ported from the SNES (Super Mario Advance 2): an "SNES"
		 * layout instead of the four-face one. Face buttons by position give
		 * these GBA keys, and both shoulders give GBA L. The game pans the
		 * camera with L toward where the player faces; the SNES panned left
		 * with L and right with R. So while a shoulder is held where `when`
		 * matches (in a level, not paused; elsewhere the GBA layout), the
		 * byte at `pan` (the way the camera pans while L is held: 1 right,
		 * 2 left) is set before each frame. */
		struct {
			uint32_t right, bottom, top, left;
			uint32_t pan;
			MemProbe when[3];
		} snes;
	} controls;
	int stableFrames;  /* consecutive "quiet" frames required */
	int maxDiff;       /* pixels allowed to change between quiet frames (blinking markers) */
	int crop_x, crop_y, crop_w, crop_h;

	/* Optional tighter crop that drops the screen's bezel and title bar
	 * (used when map_crop is on), and rectangles (screen coords) to cover
	 * with empty map-grid tiles, e.g. button prompts drawn over the map.
	 * gridX/gridY/gridCell give the phase and size of the background grid. */
	int tight_x, tight_y, tight_w, tight_h;
	struct { int x, y, w, h; } masks[8];
	int gridX, gridY, gridCell;
	/* (Unused: removing the Metroid scroll arrows glitched once the map grew
	 * under them, so they're left in.) Scroll arrows drawn on top of the map: inside these rects, pixels of the
	 * arrow's colours (arrowKey 1 = magenta, 2 = yellow/orange/red) are
	 * replaced with the same pixel one or two grid cells to the side. */
	struct { int x, y, w, h; } arrows[2];
	int arrowKey;
	/* Lettering to erase (when map_crop is on): inside the rect (screen
	 * coords), pixels of exactly these n colours (0xRRGGBB, n = 0 means 1; n < 0
	 * paints the whole rect in rgb[0], for text that animates) are
	 * filled in from the pixels around them (or, with flat, all set to the most
	 * common other colour in the rect), which leaves the button or plate they
	 * sit on untouched. With grid, every pixel in the rect is rebuilt from the
	 * same spot 1-3 grid cells (gridCell) above and to the sides, outside any
	 * grid rect: for prompts that also clear the map grid behind them. */
	struct { int x, y, w, h; int n; uint32_t rgb[8]; bool flat, grid; } glyphs[10];
	/* Recolouring (when map_crop is on, after glyphs): inside the rect, each
	 * colour from[i] becomes to[i] (0xRRGGBB, n pairs). */
	struct { int x, y, w, h; int n; uint32_t from[16], to[16]; } recolor[6];

	Probe probes[8];
	int minProbes;    /* how many probes must match (0 = all) */
	int tolerance;    /* per-channel colour tolerance */
	uint32_t bgColor; /* map background; >= minForeground pixels must differ from it */
	int minForeground;

	MemProbe mem[4]; /* checked on the shadow core once the map should be up */
	MemProbe live[4]; /* checked on the live core before snapshotting (gameplay) */
	/* Back on the title / file menus (all must match): forget the map and
	 * show the logo again until the next capture. */
	MemProbe menu[2];

	/* Values that change when the player enters a new room; a change
	 * triggers an early capture. Only addr/size are used. */
	MemProbe room[4];

	/* Games whose HUD moves to the second screen (Mario Kart; Super Mario
	 * Advance 2 in its levels, with a map capture elsewhere):
	 * while every `on` probe matches (I/O registers or RAM, after each
	 * frame), the BG layers in bgMask and the sprites of priority objPrio are
	 * drawn there, on black, instead of on the game screen (emu_hud_split).
	 * While every `keep` probe matches (the pause menu), the last HUD picture
	 * stays up; anywhere else the logo comes back. Two screens only. */
	struct {
		MemProbe on[4];
		MemProbe keep[4];
		/* While it's on, it stays on while these match too (it isn't
		 * turned on by them): Super Mario Advance 2 fading out through a
		 * pipe, which the world map does too on the way into a level. */
		MemProbe stay[2];
		unsigned bgMask;
		/* The HUD on another layer where every alt.on probe matches (`on`
		 * not matching): Super Mario Advance 2's boss rooms, in display
		 * mode 1, have the status bar on BG0. Same sprites and moves. */
		struct {
			MemProbe on[4];
			unsigned bgMask;
			/* hud.overMap: in a piece that goes alone, its pure black
			 * (stretched from the game screen over the whole picture)
			 * darkens the second screen instead of being drawn (Fire
			 * Emblem's phase banner: its sparkles are holes in a black
			 * layer) */
			bool shade;
			/* the game screen dimmed while it's on */
			bool dimGame;
		} alt;
		SpriteRule objs[8]; /* the sprites that move with it */
		/* While every `layout` probe matches (racing, not the results):
		 * HUD pixels in `sign` stay on the game screen when the HUD has
		 * nothing else on those rows within 40 pixels either side (Mario
		 * Kart's turn signs, not the lap-time table); the HUD in `map` is
		 * measured as each race starts and drawn 2x (1.5x if that doesn't fit
		 * comfortably) in the middle of the second screen. */
		MemProbe layout[4];
		struct { int x0, y0, x1, y1; } sign, map;
		/* BG layers grabbed as each race starts, rows above backdropRows,
		 * scaled to fill the second screen and dimmed, behind the HUD (0:
		 * plain dark) */
		unsigned backdrop;
		int backdropRows;
		/* Another scene with the same `on` registers (Mario Kart's cup
		 * podium): while `when` also matches, these sprites move too and the
		 * backdrop is the game's logo, dimmed, instead of the grabbed
		 * layers (no sign or map handling). */
		struct {
			MemProbe when[2];
			SpriteRule objs[4];
			HudMove moves[4];
		} scene;
		/* moves while racing (with the `layout` probes; no layout probes:
		 * always). A move with toGame and no how / vhow copies its box back
		 * onto the game picture where it was. */
		HudMove moves[4];
		/* The second screen is the HUD on black with the game's logo below
		 * it, at half the usual size (Super Mario Advance 2), instead of a
		 * backdrop. */
		bool logoBelow;
		/* The same, the logo centred and dimmed, behind the HUD (Final
		 * Fantasy Tactics Advance). */
		bool logoBehind;
		/* While every `mirror` probe matches, or every `mirrorOr` one: the
		 * whole game screen goes to the second screen, and the game screen
		 * holds the last picture from before (menus over a paused world,
		 * a battle's results; unused since Mega Man Battle Network left). With `mirror`,
		 * while alt.on matches too, only the split's alt layers go (its
		 * START menu without the world behind it). */
		MemProbe mirror[2];
		MemProbe mirrorOr[2];
		/* Instead of bgMask: the BG layers drawn at a priority below this
		 * (BGxCNT bits 0-1), picked line by line as they're drawn. Final Fantasy Tactics
		 * Advance moves its map and menus between layers, but its menus,
		 * text and boxes are always priority 0-1, the map 2-3. */
		int bgPrioBelow;
		/* While these match (Final Fantasy Tactics Advance: its animation
		 * engine is running), a layer whose tiles mostly use palettes below
		 * effectsPal stays on the game screen: a spell's effects are drawn
		 * on the layers its text boxes use, in other palettes. */
		MemProbe effects[2];
		int effectsPal;
		/* The map layer, by its BGxCNT value (Final Fantasy Tactics
		 * Advance's battle maps, on whichever BG the game has it): the HUD
		 * is on only while a layer has it. The backdrop is then a still of
		 * the game screen without the HUD, taken as the HUD comes on
		 * (unless the map's tile graphics are the ones taken last) and
		 * after the game fades to black and back, once the brightness
		 * settles; in grey with backdropGrey. */
		uint16_t mapCnt;
		/* Another map layer (Final Fantasy Tactics Advance's world map):
		 * while a layer has it, these sprites move instead of objs, and
		 * the backdrop is the layers below the HUD's (bgPrioBelow) alone,
		 * without sprites. */
		struct {
			uint16_t mapCnt;
			SpriteRule objs[4];
		} world;
		bool backdropGrey;
		SpriteRule backdropHide[2]; /* sprites left out of that still (rendered again) */
		/* The HUD goes into the map picture instead of a screen of its own
		 * (Advance Wars): `moves` place its pieces in the map picture's
		 * coordinates, drawn only over pixels of colour mapClear
		 * (framebuffer order, alpha ignored), so the capture's panels stay
		 * on top of them. */
		bool overMap;
		uint32_t mapClear;
		/* register bits the renderers go without while it's on
		 * (emu_hud_game_reg) */
		struct { uint32_t addr; uint16_t bits; } gameReg;
		/* hud.overMap: where each move's piece is in this frame's HUD
		 * picture (box[m] = x0, y0, x1, y1; x1 < 0: not there), for HUDs
		 * whose pieces move round the screen (Fire Emblem's windows),
		 * instead of the moves' own boxes */
		void (*findPieces)(Emu* e, const uint32_t* hud, uint32_t clear, int box[4][4]);
		/* hud.overMap: the logo, small and dimmed, behind everything in
		 * the map picture (as in a fight: profile.combat) */
		bool logoUnder;
		/* with logoUnder: that picture (the logo, small and dimmed) also
		 * stands in for the big logo wherever there's no map picture,
		 * outside the game's chapters too (Fire Emblem) */
		bool logoAlways;
	} hud;

	/* Title-screen logo, pulled out of the running game: boot, wait, optionally
	 * press Start (to skip an intro), wait again, then keep one BG layer
	 * (0-3) above row maxY. blackIsClear: the layer has an opaque black fill. */
	struct {
		int wait, wait2, layer, maxY;
		unsigned layers; /* instead of layer: several BG layers (bit i: BG i) */
		bool pressStart, blackIsClear;
		MacroStep steps[24]; /* instead of wait/pressStart: button steps from power-on */
		int rev; /* bump when extraction changes, so cached logos are made again */
		/* The logo is painted into the whole title picture: render every
		 * layer and keep the pixels inside this mask (THPS2). */
		const LogoMask* mask;
	} logo;

	/* Border for the map screen, taken from one of the game's own menu screens
	 * (Castlevania): play `steps` from power-on, render BG `layer` alone, and
	 * read a frame of thickness t whose corners are at x0/x1, y0/y1. Edges are
	 * tiled from p-wide slices (the most common one along each edge, which
	 * skips one-off decorations). */
	struct {
		MacroStep steps[24];
		int layer, t, p;
		int x0, y0, x1, y1;
		int minLuma; /* pixels darker than this are dropped (translucent panel fills) */
	} frame;
	/* Metroid: fill the map screen with the map's own empty grid tile. */
	bool gridBackground;

	/* Text to copy from another screen onto the map (Circle of the Moon's map
	 * has no completion rate; its pause screen does): press `open` from the
	 * live state, wait, then take the pixels of colour `text` inside x/y/w/h
	 * (the digits only), outlined in the map's background colour. It goes in
	 * the first corner of the map (top right, then bottom right) with no rooms. */
	struct {
		MacroStep open[4];
		int wait;
		int x, y, w, h;
		uint32_t text;
	} rate;

	/* HUD sprites to hide on the game screen while the map is shown (the
	 * Metroids' corner mini-map). */
	SpriteRule hideSprites[4];
	/* Sprites left out of the map capture (when map_crop is on): prompts the
	 * game draws over the map as sprites (Fusion's "A OBJECTIVE"), so the map
	 * underneath shows instead. */
	SpriteRule captureHide[6];
	/* The capture shows only these BG layers (bit i: BG i) and sprites, over
	 * black, with captureHide always applied (0: the whole screen, sprites
	 * hidden only with map_crop): Tony Hawk's Pro Skater 3's goal list,
	 * its panel without the scene behind. */
	unsigned captureLayers;
	/* Parts of the map capture (screen rects, inclusive) taken from a render
	 * of only `underLayers` (bits 0-3 BG0-3, bit 4 sprites), made once per
	 * capture: bezel pieces drawn over the map, like Zero Mission's corner
	 * minimap, give way to the map underneath. */
	struct { int x0, y0, x1, y1; } underRect[2];
	unsigned underLayers;
	SpriteRule underHide[2]; /* also left out of that render (on top of captureHide) */

	/* A raised piece of bezel painted over a prompt that sits in a notch of
	 * the bottom bezel bar (Zero Mission's "SELECT / CHANGE AREAS"), when
	 * map_crop is on. The bar's top edge is at row `base`; its notch spans
	 * n0..n1 and is filled in. Above it the tab's flat top runs x0..x1 at row
	 * `top`, its ends slope down one pixel per row to the bar. Each pixel is
	 * coloured by its depth below that outline: colors[0] (outline),
	 * colors[1], ... the last colour repeats down to row `bottom`. */
	struct { int x0, x1, top, base, n0, n1, bottom, n; uint32_t colors[12]; } tab;

	int animFrames; /* frames to record for the blinking/pulsing marker loop */
	/* Shown still even with Map Animations on (the still frame, stillPick:
	 * Super Mario Advance 2's HELP! and the selected level) */
	bool mapStill;
	int animStep;   /* emulated frames between recorded frames */
	/* "Map Animations: Off": a marker loop longer than animFrames * animStep
	 * (the Castlevania glows) is recorded over this many frames instead, so
	 * the still frame can always come from the same point of the loop. */
	int animCycle;
	/* How the still frame is chosen for each animated spot: the brightest
	 * state (default), or the state that covers the most pixels (ties: the
	 * most contrast) for markers whose brightest state isn't the marker. */
	/* (STILL_FIRST: the first frame, for frames that are pages: Fire
	 * Emblem's unit status) */
	/* (STILL_RED: a spot with red in it at its reddest, others at their
	 * brightest: A Link to the Past's X, which fades red to white) */
	enum { STILL_BRIGHT, STILL_COVER, STILL_FIRST, STILL_RED } stillPick;
	/* ROM patches applied at the start (all or none), for the HUD split:
	 * FFTA tags its world sprites (SpriteRule.tagged). */
	RomPatch romPatch[3];
	/* Instead of opening a map: the profile builds the second screen's
	 * picture itself (crop_w x crop_h, into out) on the shadow core from
	 * the live game's state; NULL on success, else why not (Advance Wars:
	 * the unit list or the battle intel, with the minimap below). */
	const char* (*compose)(Emu* s, const void* state, uint32_t* out, int w, int h);
	/* Instead of compose: several pictures (pages shown in turn, `*step`
	 * emulated frames each); out[0..*n) on return, *n the most on the way in */
	const char* (*composePages)(Emu* s, const void* state, uint32_t* const* out, int* n, int* step, int w, int h);
	/* Values computed from the game's memory, read by MemProbes at
	 * addresses from 0xF0000000 (memprobe_virtual, mapcap.h): Fire
	 * Emblem's processes, which move between slots. */
	uint32_t (*virt)(Emu* e, uint32_t addr);
	/* While every probe matches (a fight), the second screen is the map
	 * picture's background colour (hud.mapClear) with the game's logo,
	 * small and dimmed, in the middle. */
	MemProbe combat[2];
	MemProbe combatOr[2]; /* or while every one of these matches (the enemy's turn) */
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
/* Applies p's romPatch list (all of it, or nothing if any part doesn't
 * match); true if there was one and it went in. */
bool profile_patch_rom(const MapProfile* p, Emu* e);
/* True for a regional release whose RAM layout differs from the USA one
 * (cheat codes made for other regions would write the wrong addresses). */
bool profile_ram_moved(const char* gameCode);
bool memprobes_match(const MemProbe* list, int n, Emu* e);
/* the profile's virt (set when a game's map capture starts): MemProbe
 * addresses from 0xF0000000 */
extern uint32_t (*memprobe_virtual)(Emu* e, uint32_t addr);

#endif

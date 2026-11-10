/* Per-game knowledge: how to open the map and how to recognise it. */
#ifndef GBADS_PROFILES_H
#define GBADS_PROFILES_H

#include "emu.h"
#include "logo_masks.h"

/* Behind the logo on the second screen, and around what a game shows
 * there that doesn't fill it (GEN-1): dark grey, so the GBA screen's
 * 240x160 shows against the black around it (ABGR8888) */
#define SCREEN_GREY 0xFF303030u
/* behind something live on the second screen (a HUD, a card's pieces, the
 * podium): black, the idle logo's dark grey being for idle (GEN-1) */
#define SCREEN_LIVE 0xFF000000u

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

/* How many pieces a split-off HUD can move (hud.moves, alt.moves,
 * scene.moves). hud.overMap's pieces are the first 4 only. (6 since F-Zero:
 * Maximum Velocity: BOOST under POW as well as GP Legend's four.) */
#define HUD_MOVES 6

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
	/* hud.overMap: drawn as wide as the map picture (scaled up by its
	 * width over the game screen's), at y (Advance Wars 2's conversations) */
	bool wide;
	/* racing moves: only while a sprite with this tile is shown (F-Zero:
	 * GP Legend's RANK, which Time Trial doesn't have); 0: always */
	int ifTile;
	/* racing moves: the piece goes under whatever stays in its corner:
	 * the HUD pixels above its box, within its columns, outside the
	 * other moving pieces' boxes; its left edge at their leftmost, its
	 * top `under` rows below their lowest (F-Zero: GP Legend's LAST under
	 * SPARE MACHINE or BEST TIME); with none there, by how / vhow */
	int under;
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
	/* Another sequence used instead when every openAltIf probe matches as
	 * the capture starts (Super Mario Advance 4's World-e: on a level's
	 * spot its info box takes no L; B twice, back to the map, first) */
	MemProbe openAltIf[2];
	MacroStep openAlt[8];
	/* Optional second step: once the map is up (mem probes), if open2If
	 * matches, wait open2Delay frames and press open2 (A Link to the Past:
	 * L again on the world map zooms out to the whole world). */
	MemProbe open2If[2];
	int open2Delay;
	MacroStep open2[4];
	int settleMax;     /* frames to wait after the macro for the map to settle */
	int openWithin;    /* give up if the memory probes don't match within this many frames (0 = settleMax) */
	int openTries;     /* with openWithin: press `open` again this many times in all before giving
	                    * up (0, 1: once): a game that ignores it for a while (Super Mario
	                    * Advance 2 just after Mario reaches a level) */
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
	/* Logo-only games: a pinball table twice as tall over both screens
	 * (Pokemon Pinball). While every `on` probe matches, the two screens
	 * show 320 rows of the table from row v = min(the game's scroll,
	 * rows - 320): the top screen the game as it is while v is its scroll,
	 * else a copy of the game held at row rows - 320; the bottom screen a
	 * second copy held at v + 160. The copies run in step with the game (the
	 * same keys every frame; game.c twin_*, update_tall). The BG layers in
	 * hudBg (the score bar) go to the bottom screen's bottom (from row
	 * hudRow). */
	struct {
		MemProbe on[2];
		unsigned hudBg;
		int rows;   /* the table's height */
		int hudRow; /* the score bar's first row */
		/* state: the word holding the game's state pointer; cam / camRow:
		 * offsets there of the camera (24.8 fixed point) and its row; nop:
		 * the camera's stores in the copy's code (made no-ops); bound: the
		 * game's lowest camera target, boundFrom on the tables the copy
		 * knows; ball: offset of the ball's pointer (x, y words at
		 * +0x34, +0x38 there), compared to keep them in step; oam: the
		 * game's shadow sprite table (what the next frame's sprites are
		 * copied from); view: the shadow of BG viewBg's VOFS (the table rows
		 * the next frame shows, from its top line), the register being this
		 * frame's: the camera's row plus the screen shake and offsets of
		 * the game's own (pans: they depend on where the camera is, so a
		 * copy's can differ from the game's); shake: the screen shake (a
		 * signed byte, S offset), which a copy adds alike to its camera's
		 * row; idle: the copies only run while these match (no sweep of the
		 * table going on) */
		struct {
			uint32_t state;
			uint16_t cam, camRow;
			uint32_t nop[4];
			uint32_t bound;
			uint16_t boundFrom;
			uint16_t ball;
			uint32_t oam;
			uint32_t view;
			int viewBg;
			uint16_t shake;
			/* a message box up: more than min entries of the tile map at
			 * map, rows row0 to row0 + rows - 1 (30 columns), not tile
			 * blank (game.c update_tall, PP-11) */
			struct { uint32_t map; int row0, rows; uint16_t blank; int min; } msg;
			MemProbe idle[2];
			/* Putting the copy's camera anywhere at once (game.c twin_jump):
			 * the game eases its scroll toward the camera (at most 8 rows a
			 * frame) and streams one tile row a frame of the table's tiles
			 * into a ring as the scroll moves. Everything that tracks the
			 * scroll is set to the camera's row, and the ring filled as if it
			 * had scrolled there. ring: the ring's VRAM address, slots of 1 KB
			 * (one tile row; table tile row R in slot (R + slotOff) % slots);
			 * src: the table's tiles, all of them (rows 0..split-1 from src[0],
			 * the rest from src[1], 1 KB a row), tileRows of them; last: S
			 * offset of the tile row the ring was last streamed for (the rows
			 * from it to it + slots - 1 are in the ring); scroll: S offsets,
			 * scrollAt: addresses, of copies of the scroll's row (0: none);
			 * scrollVofs: the BGs whose VOFS registers it is; plus16: an
			 * address holding the row + 16 */
			struct {
				uint32_t ring;
				int slots, slotOff;
				uint32_t src[2];
				int split, tileRows;
				uint16_t last;
				uint16_t scroll[4];
				uint32_t scrollAt[3];
				unsigned scrollVofs;
				uint32_t plus16;
			} jump;
			/* Instead of state/cam/jump (Sonic Pinball Party): the camera is a
			 * row at `cam` (absolute; view = its row - camOff + the shake, a
			 * signed half at shakeAt), the table's height a half at rowsAt
			 * (its visible rows: top to height - bottomLess). A copy can't be
			 * put anywhere at once: it walks its camera there (8 rows a
			 * frame) and, every frame it's taken from the game again, keeps
			 * its own BG tiles (VRAM ringLo..ringHi), its own copies of the
			 * vars below (the ring's counters, the scroll's shadows) and its
			 * own entries for the ring in the game's queue of copies made at
			 * the next vblank (count byte at qCount, 12-byte entries of
			 * destination, source, size at qEntries) */
			struct {
				uint32_t cam;
				int camOff;
				uint32_t rowsAt;
				int top, bottomLess;
				uint32_t shakeAt;
				uint32_t ringLo, ringHi;
				struct { uint32_t addr; int n; } vars[4];
				uint32_t qCount, qEntries;
				/* while this matches (in the game), the copies draw no sprites:
				 * the game moves its sprites with the view then instead of
				 * placing them afresh, so a copy's are the game's (Sonic
				 * Pinball Party's opening sweep) */
				MemProbe noObj;
				/* halfwords written over the copies' code (with nop) */
				struct { uint32_t addr; uint16_t v; } patch[16];
				/* (Muppet Pinball Mayhem) viewOff: `view` holds the view's
				 * row plus this (its BG VOFS is set from it at the vblank);
				 * gameFrom: the first line of the game's own picture with
				 * table rows (the lines above it show only the score bar's
				 * layers: the copies draw those rows); no rowsAt: the
				 * table's height is `rows`. The queue of copies at the next
				 * vblank as a ring: its start index a word at qIndex (slot
				 * (start + i) % qSlots), qWide: the count a word, entries
				 * qStride bytes (default 12) with the destination at
				 * qDst, the size in bytes at qSize (defaults 0 and 8) */
				int viewOff, gameFrom;
				/* while any of these matches (Muppet Pinball Mayhem paused:
				 * its menu of sprites over the table; its game over: the
				 * scroll to the top, the curtains), the game as it is and the
				 * logo on the second screen; the copies go on running, so
				 * they're at their rows again at once after */
				MemProbe plain[3];
				/* the copies keep all their sprites (none of the game's are
				 * fixed to the screen outside `plain`; its sprites are written
				 * in the vblank from its objects, so the lists twin_hide
				 * compares are a frame old) */
				bool keepObj;
				/* a dimmed screen (the game fading out, Muppet Pinball
				 * Mayhem's at the end of a game) stays on both screens, the
				 * copies dimmed alike, not the game's screen on the bottom
				 * one (PP-11's) */
				bool keepDim;
				/* while it doesn't match (in the copy, as taken from the
				 * game), the copy keeps nothing of its own: it starts again
				 * from the game's tiles (Muppet Pinball Mayhem's table being
				 * set up, its tiles not loaded yet) */
				MemProbe ready;
				uint32_t qIndex;
				int qSlots, qStride, qDst, qSize;
				int qReload; /* (an entry into the ring bigger than this, in bytes, is the table loaded afresh; default 0x400) */
				bool qWide;
			} carry;
		} live;
	} tall;
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
	/* frames between captures instead of map_interval when fewer (0: the
	 * setting's; Summon Night: Swordcraft Story, a half second) */
	int interval;
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
	/* While every one of these matches, the second screen shows the logo
	 * (as with no map yet), its map picture and HUD kept for when they
	 * stop matching: Super Mario Advance 4's World-e castle hall, which
	 * has no status bar (SM4-11) */
	MemProbe logoIf[2];
	MemProbe logoIfOr[2]; /* or while every one of these matches */

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
		/* Once every one of these has matched while `on` does, the HUD
		 * stays off (the logo) until on[0] stops matching, or a state is
		 * loaded: Mario & Luigi: Superstar Saga, from a battle's results
		 * through the level-ups after them to the battle's end (ML-5) */
		MemProbe offAfter[2];
		unsigned bgMask;
		/* The HUD on another layer where every alt.on probe matches (`on`
		 * not matching): Super Mario Advance 2's boss rooms, in display
		 * mode 1, have the status bar on BG0. Same sprites and moves. */
		struct {
			MemProbe on[5];
			unsigned bgMask;
			/* hud.overMap: in a piece that goes alone, its pure black
			 * (stretched from the game screen over the whole picture)
			 * darkens the second screen instead of being drawn (Fire
			 * Emblem's phase banner: its sparkles are holes in a black
			 * layer) */
			bool shade;
			/* the game screen dimmed while it's on */
			bool dimGame;
			/* moves used instead of the HUD's own while it's on this
			 * layer (none: the same moves): Super Mario Advance 4's world
			 * map status bar, from the bottom to the top */
			HudMove moves[HUD_MOVES];
			/* the second screen's picture behind it is the last map
			 * picture (a full GBA screen) instead of the logo (Super
			 * Mario Advance 4: its status bar over the inventory) */
			bool mapBelow;
		} alt;
		SpriteRule objs[12]; /* the sprites that move with it */
		/* While every `layout` probe matches (racing, not the results):
		 * HUD pixels in `sign` stay on the game screen when the HUD has
		 * nothing else on those rows within 40 pixels either side (Mario
		 * Kart's turn signs, not the lap-time table); the HUD in `map` is
		 * measured as each race starts and drawn 2x (1.5x if that doesn't fit
		 * comfortably) in the middle of the second screen. */
		MemProbe layout[4];
		struct { int x0, y0, x1, y1; } sign, map;
		/* The map's place instead (F-Zero: GP Legend): the `map` box itself
		 * (not measured), drawn `scale` second-screen pixels per HUD pixel
		 * (2: twice as big), its centre at cx, its bottom at bottom (GBA
		 * pixels of the second screen); scale 0: Mario Kart's way */
		struct {
			int scale, cx, bottom;
			/* the map is the sprite with tile holdTile, drawn every other
			 * frame, the racers' dots (tile dotTile) every frame: the map
			 * is held between (its pixels from the frames it's drawn in,
			 * but those under a dot), the dots laid on it as drawn; 0:
			 * shown as drawn */
			int holdTile, dotTile;
		} mapAt;
		/* While `on` matches but the HUD has drawn nothing for a third of a
		 * second, the logo instead (F-Zero: GP Legend's course fly-in, with
		 * the race's registers already set) */
		bool emptyIsOff;
		/* Racing's second screen on black (SCREEN_LIVE) instead of the
		 * plain dark red-grey (GT Advance 3: its fades match the game
		 * screen's) */
		bool black;
		/* While a sprite with this tile is shown, not racing (F-Zero: GP
		 * Legend's RESULTS, over the track as a race ends); 0: none */
		int offTile;
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
			HudMove moves[HUD_MOVES];
		} scene;
		/* A title card before each level (Sonic Advance's zone card): while
		 * a sprite with tile `tile` in palette `pal` is shown (and `on`
		 * matches), the `objs` sprites are drawn on their own (they stay on
		 * the game screen too), and the picture from the frame where what's
		 * dark on rows y0..y1 (its scrolling name) is most nearly centred,
		 * clear of both edges, is kept: the second screen's backdrop from
		 * then on, at its own size, black where the card has nothing. Till
		 * there's one (a state loaded in a level), the logo. */
		struct {
			int tile, pal;
			SpriteRule objs[4];
			int y0, y1;
			/* x0, y0, x1, y1: a part taken from its own frame and laid
			 * over the take (Sonic Advance's character name, which scrolls
			 * on its own): the frame where what's dark in it is whole,
			 * clear of its top and bottom, and most nearly centred top to
			 * bottom (none: x1 <= x0) */
			int side[4];
			/* The card is the game's whole picture instead (Sonic Advance
			 * 2: BG layers on the game's blue backdrop, and sprites):
			 * nothing is split off while it's up (objs, y0, y1 unused) and
			 * the game screen as drawn `at` frames after the card's sprite
			 * shows is the take. */
			bool whole;
			int at;
			/* any tile from `tile` to this one (0: `tile` alone): Sonic
			 * Advance 3's zone name starts at one of two tiles */
			int tileMax;
			/* the sprite's priority + 1 (0: any): Sonic Advance 3's zone
			 * name is at 3, the points for an enemy in the same tile and
			 * palette at 0 */
			int prio1;
			/* While these match (with `on`), no card: the take is forgotten
			 * and the logo is behind the HUD (Sonic Advance 3's hubs, which
			 * have no card of their own and lead to other zones) */
			MemProbe drop[2];
			/* with whole: the take is the frame drawn again with these
			 * sprites left out and this halfword written first (Sonic
			 * Advance 3: its brightness shadow, so all but the card's band
			 * goes black, as in the card's first moments); none: as drawn */
			SpriteRule hide[6];
			struct { uint32_t addr; uint16_t value; } poke;
			/* The HUD on neither screen from the card's start (its sprite,
			 * or `pre` matching a little before it) until its take, then
			 * on the second screen over the new take while the game's card
			 * is still up (Sonic Advance 3, SA3-3). `pre`: what shows the
			 * card is coming before its sprite does (its darkening); it
			 * counts for at most 30 frames without the sprite */
			bool hideTillTake;
			MemProbe pre[2];
		} card;
		/* moves while racing (with the `layout` probes; no layout probes:
		 * always). A move with toGame and no how / vhow copies its box back
		 * onto the game picture where it was. */
		HudMove moves[HUD_MOVES];
		/* The second screen is the HUD on dark grey with the game's logo
		 * below it, at its own size (Super Mario Advance 2), instead of a
		 * backdrop. */
		bool logoBelow;
		/* While every one of these matches, the HUD picture on the second
		 * screen isn't drawn again: the last one stays (Super Mario
		 * Advance 4: World-e's status bar sliding off and back as the
		 * inventory opens and closes, SM4-8) */
		MemProbe freeze[2];
		/* The top rows of the last map picture (the world map's level
		 * list: its title row with the level's name) kept above the HUD,
		 * which moves down that far (Super Mario Advance 2, SMA-7) */
		int mapTitleRows;
		/* The HUD is only these rows at the top (0: the whole screen):
		 * its layers stay in the game below them, drawn as the game
		 * draws them, and are left off the second screen there (Super
		 * Mario Advance 2: BG3 is the status bar in rows 0-15, and
		 * message boxes, or in some levels the scenery, below) */
		int rows;
		/* The same (unused). */
		bool logoBehind;
		/* With logoBelow: the logo goes in the room below this row
		 * instead of below `rows` (0: none), for a HUD that moves its
		 * lower pieces up under its top ones (Ninja Five-O) */
		int logoTop;
		/* With logoBelow: a picture the game shows on its own screen
		 * elsewhere, kept from the game screen and shown below the HUD
		 * instead of the logo (Super Mario Advance's course board, shown
		 * before each level). Taken from the first frame where every
		 * `when` probe matches and at least `min` of `probes` do (game
		 * screen pixels, within tol per channel; min 0: all), once each
		 * time it's shown; its rows y0..y1 go to the second screen from
		 * row `at`, on black. Until one is taken (a state loaded in a
		 * level, made before this), the logo. Kept with saved states. */
		struct {
			MemProbe when[3];
			Probe probes[8];
			int min, tol;
			int y0, y1, at;
			/* A number taken from another screen of the game laid over
			 * it (Super Mario Advance: the pause box's Ace Coin
			 * percentage, which counts the coins found in this level,
			 * over the board's, from before it): the map capture (its
			 * crop: that number alone) goes in place of the held
			 * picture's rect x0..x1, y0..y1 (game screen coordinates),
			 * which is first filled with `fill` (0xRRGGBB); the capture's
			 * pixels of colour `key` (its background) are left out, and
			 * its right edge goes at x1, its top at y0. Until there's a
			 * capture since the picture was taken, the picture's own. */
			struct { int x0, y0, x1, y1; uint32_t fill, key; } over;
		} held;
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
		/* The HUD only leaves the game screen: it isn't shown anywhere,
		 * and the second screen keeps its map picture or logo (River City
		 * Ransom EX's top bar, which its Status capture shows anyway) */
		bool drop;
		/* register bits the renderers go without while it's on
		 * (emu_hud_game_reg) */
		struct { uint32_t addr; uint16_t bits; } gameReg;
		/* hud.overMap: where each move's piece is in this frame's HUD
		 * picture (box[m] = x0, y0, x1, y1; x1 < 0: not there), for HUDs
		 * whose pieces move round the screen (Fire Emblem's windows),
		 * instead of the moves' own boxes */
		void (*findPieces)(Emu* e, const uint32_t* hud, uint32_t clear, int box[4][4]);
		/* Boxes drawn across part of the screen with their own layer
		 * settings and the sprites off on their lines (Advance Wars'
		 * conversations), found by emu_hud_band. BAND_MOVE: those layers
		 * go into the HUD there too, and leave the game screen once the
		 * second screen has a picture to put them on; BAND_WATCH: they
		 * stay on the game screen, only found (Advance Wars 2: the game
		 * turns the map off behind them, so moving them leaves a black
		 * strip). */
		enum { BAND_NONE, BAND_MOVE, BAND_WATCH } band;
		/* hud.overMap: the logo (GEN-1: as on any second screen) wherever
		 * the map picture has mapClear (as in a fight: profile.combat) */
		bool logoUnder;
		/* with logoUnder: that picture also stands in for the logo
		 * wherever there's no map picture, outside the game's chapters
		 * too (Fire Emblem) */
		bool logoAlways;
		/* While every `when` probe matches as a frame starts drawing (the
		 * game's vblank code done), these BG layers go into the HUD too,
		 * that same frame: what the game draws in the vblank it sets a
		 * flag (Dragon Ball Z: the counter's box on BG0) never shows on
		 * the game screen first (emu_hud_mem_layers) */
		struct {
			MemProbe when[2];
			unsigned bgMask;
			/* while any one `when` probe matches, instead of every one;
			 * probes may be in VRAM too (Mario & Luigi: BG2's text goes
			 * while either box's frame is on BG3, ML-5) */
			bool any;
			/* palette + 1 (0: none): on a line where one of those layers
			 * has a tile in this palette, the layer stays on the game
			 * screen there, out of the HUD (Mario & Luigi: a tutorial's
			 * speech balloon, palette 15, while a box's text, palette 9,
			 * is up: ML-5, issue #31) */
			int gamePal;
		} memLayers;
	} hud;

	/* Title-screen logo, pulled out of the running game: boot, wait, optionally
	 * press Start (to skip an intro), wait again, then keep one BG layer
	 * (0-3) above row maxY. blackIsClear: the layer has an opaque black fill. */
	struct {
		int wait, wait2, layer, maxY;
		unsigned layers; /* instead of layer: several BG layers (bit i: BG i) */
		int objMaxY; /* with layers: sprites only above this row (0: all rows); below it, the BG layers alone */
		int lowFrom; unsigned lowLayers; /* with layers: from this row down, only these layers (0: no change) */
		bool pressStart, blackIsClear;
		bool lightEdgeClear; /* light background around the logo, reached from the edges, is clear */
		/* pixels of these colours (0xRRGGBB) reached from outside the logo
		 * or the picture's edges through others of them are clear, then
		 * any left with at most one neighbour: the background showing at
		 * the lettering's dithered or translucent edge (Mario & Luigi's
		 * sky, its shadow's specks), or a layer's own background
		 * (Battle Network Rockman EXE's blue) */
		uint32_t edgeClear[4];
		/* a drop shadow of this colour (0xRRGGBB; either of a game's two
		 * colour sets) stays only below and right of the lettering:
		 * where there's lettering (or shadow kept) up to shadowReach rows
		 * above it, as far left as that; the rest of it (a rim of that
		 * colour along the lettering's upper and left sides, which the
		 * title's light background hides) is clear (Super Mario Bros. 3) */
		uint32_t shadow[2];
		int shadowReach;
		int minY, minX, maxX; /* with maxY: only this box (maxX 0: the whole width); a frame
		                       * round the logo left out (Super Mario World's) */
		MacroStep steps[24]; /* instead of wait/pressStart: button steps from power-on */
		int rev; /* bump when extraction changes, so cached logos are made again */
		/* The logo is painted into the whole title picture: render every
		 * layer and keep the pixels inside this mask (THPS2). */
		const LogoMask* mask;
		/* Bytes written into the ROM of the copy the logo is taken from
		 * (never the game played), if it holds `orig` there: Golden Sun:
		 * The Lost Age shows its title only with a save, so its save check
		 * answers "one" there */
		RomPatch patch[2];
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
	 * Metroids' corner mini-map; Lady Sia's HUD, piece by piece). At most
	 * 12, emu_hide_sprites' limit. */
	SpriteRule hideSprites[12];
	/* hideSprites only while these all match, checked every frame (none:
	 * always): Lady Sia's HUD only in play, so its menus, credits and
	 * own pause screen are drawn whole */
	MemProbe hideWhen[2];
	/* Sprites left out of the map capture (when map_crop is on): prompts the
	 * game draws over the map as sprites (Fusion's "A OBJECTIVE"), so the map
	 * underneath shows instead. */
	SpriteRule captureHide[6];
	/* The capture shows only these BG layers (bit i: BG i) and sprites, over
	 * black, with captureHide always applied (0: the whole screen, sprites
	 * hidden only with map_crop): Tony Hawk's Pro Skater 3's goal list,
	 * its panel without the scene behind. */
	unsigned captureLayers;
	/* with captureLayers: the colour behind them (GBA BGR555; 0: black):
	 * River City Ransom EX's Status screen on the game's pause blue */
	uint16_t captureBack;
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
	/* A compilation of games with their own RAM (Game Boy Wars Advance 1+2):
	 * the code of the profile for the game running now (one of the
	 * variants), NULL while none is (its own menus: this profile). Checked
	 * on the live game; the second screen is set up again when it changes. */
	const char* (*pick)(Emu* e);
} MapProfile;

const MapProfile* profile_find(const char* gameCode);
/* Applies p's romPatch list (all of it, or nothing if any part doesn't
 * match); true if there was one and it went in. */
bool profile_patch_rom(const MapProfile* p, Emu* e);
/* True for a regional release whose RAM layout differs from the USA one
 * (cheat codes made for other regions would write the wrong addresses). */
bool profile_ram_moved(const char* gameCode);
/* What a ROM is, by its CRC32 (emu.romCrc) against every known dump of its
 * release (the rules files' Dumps rows, src/dumps_data.h): */
typedef enum {
	DUMP_UNLISTED, /* a release with no list of dumps (no profile): not checked */
	DUMP_TESTED, /* the dump the regression tests run on */
	DUMP_OFFICIAL, /* another official dump (a revision, Virtual Console...), untested */
	DUMP_MODIFIED, /* none of them: patched, hacked, trimmed or a bad dump */
} DumpKind;
/* name: No-Intro's name of the dump (NULL unless tested or official) */
DumpKind profile_dump(const char* gameCode, uint32_t crc, const char** name);
bool memprobes_match(const MemProbe* list, int n, Emu* e);
/* the profile's virt (set when a game's map capture starts): MemProbe
 * addresses from 0xF0000000 */
extern uint32_t (*memprobe_virtual)(Emu* e, uint32_t addr);

#endif

/* The second screen without SDL (libretro core, discussion #75): its state
 * (sec), what a core calls (open, step, states, the logo, close) and what the
 * app's other parts use. See second.c. */
#ifndef SECOND_H
#define SECOND_H
#include "config.h"
#include "emu.h"
#include "mapcap.h"
#include "profiles.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* "Map Animations: Off" shows the composed still (sec.stillPx) as this index */
#define STILL_IDX MAP_MAX_FRAMES
/* the logo drawn idle on the second screen: at half strength */
#define LOGO_IDLE 128

/* What the app does for the second screen (set by the app, once per game):
 * present puts the picture (sec.mapW x sec.mapH) on the screen, presentGba
 * one of the game's own size (GBA_W x GBA_H); logoLoad / logoFree find and
 * load the game's logo, which logo_draw puts on a picture. */
/* The game's logo as the app finds it (the player's own, a scraped or the
 * extracted one): RGBA32 bytes (R, G, B, A), w x h, pitch bytes a row; own:
 * the app's, for logoFree */
typedef struct {
	const uint8_t* px;
	int w, h, pitch;
	void* own;
} LogoPic;

typedef struct {
	void (*present)(const uint32_t* px);
	void (*presentGba)(const uint32_t* px); /* a picture GBA_W x GBA_H (the screen copy) */
	/* the game's logo (sub: its pixels to a GBA pixel, a scraped logo's
	 * more than 1); false: none. Released with logoFree. */
	bool (*logoLoad)(LogoPic* p, int* sub);
	void (*logoFree)(LogoPic* p);
} SecondHooks;

/* ---- The second screen's state (libretro core, discussion #75: the module's
 * interface is being gathered here). Everything the second screen keeps
 * between frames, in one place, so the app and a core hold it the same way.
 * What it reads from the app (the emulator, the settings) it holds as
 * pointers, set once per game. */
typedef struct {
	/* the capture's pictures and which is up */
	MapResult mapRes;
	int mapW, mapH;         /* the second screen's picture (the capture's size: mapcap_size) */
	int mapShownIdx;        /* the picture on the second screen (STILL_IDX: the still), -1 none (the logo) */
	/* "Map Animations: Off" shows this composed still instead of mapRes.frames[] */
	uint32_t* stillPx;      /* mapW * mapH */
	int stillW, stillH;
	bool stillValid;
	/* The map button did nothing at the last attempt (A Link to the Past indoors
	 * without a map): keep showing the last map, dimmed. */
	bool mapInactive;
	bool logoForced;        /* the profile's logoIf matches (or held): the logo for now */
	int logoHolding;        /* frames logoIf's logo is held after it stops matching */
	bool overOn, overDirty; /* the HUD's pieces over the map (hud.overMap); the picture to be uploaded again */
	bool liveOn;            /* a profile's own live picture is up (liveView): drawn as it is */
	bool logoOnly;          /* the game has a profile but no map: the logo on the second screen (or a screen copy) */
	uint64_t mapAnimStart;
	/* the contextual toggle (GEN-14, issue #37): where the pages' count starts
	 * (0 with the toggle off: then nothing here is used), the picture's
	 * brightness while a turn that waits fades (256: as it is), and whether
	 * the capture under way is for a new subject (a room, a unit, a loaded
	 * state: the pages start again at the first) */
	int pageOffset;
	int fadeLevel;
	bool fadeUpload;        /* the picture is uploaded again this frame for the fade, one picture or not */
	bool capSubject;
	/* ONPRESS views (GEN-14): the other view is up (viewAlt), a capture of the view wanted is to be asked for now (pressCap), how many captures have come in (capDone), and which one the press waits for (tgCapWant; 0: not asked for yet) */
	bool viewAlt, pressCap, viewWait;
	unsigned capDone, tgCapWant;
	/* the toggle's state machine (toggle.c) */
	int tgState, tgFrames, tgTarget, tgWait;
	/* the map refresh (map_refresh_step), set up again for each game */
	struct {
		uint32_t lastRoomKey;
		uint32_t seenRoomKey; /* the room key as it is now, and for how many frames it's held */
		int seenRoomFor;
		int sinceMapRequest;
		int lastFails;
		bool forceMap, wasDark;
		long refreshAt;
	} refresh;
	/* owned by the second screen: the hidden copy that captures the map
	 * (mapcap), and the frame count the app advances once a frame */
	MapCapture* mc;
	uint64_t frame;
	/* a profile's hideSprites switched by its hideWhen (NULL: none, or always on) */
	const MapProfile* hideWhenProf;
	int hideWhenN;
	bool hideWhenOn;
	/* what it reads from the app, set once per game: the game's emulator,
	 * the settings, GBADS_DEBUG's logging */
	Emu* emu;
	const Config* cfg;
	bool debug;
	const char* romPath; /* the game's ROM (a profile's liveView reads it) */
	char subCode[5];      /* the profile in use: the game's code, or what pickProf picked */
	/* a logo-only game with screens copied to the second screen (profile .snap:
	 * THPS2's goal sheet): consecutive frames matching show / a revert screen,
	 * and the copy shown (kept with save states) */
	/* a logo-only game whose levels are climbs, the level above the game's
	 * screen on the other one (profile .climb, climb.c); in a level: the copy runs */
	const MapProfile* climbProf;
	bool climbOn;
	const MapProfile* snapProf;
	int snapShow, snapRevert;
	uint32_t snapShown[GBA_W * GBA_H];
	SecondHooks hooks;
} SecondState;
extern SecondState sec;

/* ---- What a core calls (the app's game.c does the same, beside its own parts).
 * Once the game is open and sec.emu / cfg / debug / romPath, sec.hooks and
 * hud.hooks are set: second_open (code: the profile in use, the game's or for
 * a compilation the game running in it, sec.subCode; screens: how many there
 * are). A compilation whose game changes: second_drop, then second_open for the
 * new one. Each frame: second_run_frame (the game's frame), then second_step.
 * As the game ends, before its emulator is closed: second_close. */
/* a new game, before second_open; and once its first state is loaded, before
 * the first frame (see second.c) */
void second_game_start(void);
void second_loop_start(void);
void second_open(const char* rom, const char* code, int screens);
void second_drop(void);
/* the game's frame (instead of emu_frame: the copies run around it), then
 * second_step */
void second_run_frame(uint32_t keys);
void second_step(int screens);
void second_close(void);

/* A new subject: a state loaded, a rewind. The map is captured again as soon
 * as it can be (and the toggle's pages and views start again). */
void second_new_subject(void);
/* The game jumped (a state loaded, a rewind, a restart): the copies start
 * again. A state loaded (after second_jump): its GBA DS block (malloc'd,
 * taken over; NULL: none) is put back by the next second_frame_start, called
 * at the start of each frame before the game's. Then second_new_subject. */
void second_jump(void);
/* the game restarted (after second_jump), and the logo changed (the host's
 * logoLoad now gives another: extracted at last, or switched) */
void second_restarted(void);
void second_logo_changed(void);
void second_state_loaded(uint8_t* block, size_t n);
void second_frame_start(void);
/* The second screen kept with save states (GBA DS's block inside the state:
 * EXTDATA_GBADS): made now (malloc'd, *n bytes; NULL if nothing), and put
 * back from a loaded state's block (b may be NULL: none; the title card and
 * the level name kept from before go either way) */
void* second_state_save(size_t* n);
void second_state_load(const uint8_t* b, size_t n);

/* what the second screen shows now, and whether the map's dimmed over it
 * (see second.c) */
enum { SHOWN_OFF, SHOWN_COMBAT, SHOWN_HUD, SHOWN_MAP, SHOWN_LOGO };
int second_shown(bool* dim);
/* the idle second screen's picture (the logo on grey, or plain grey for a
 * Game Boy game): IDLE_PIC with *px (malloc'd, *w x *h, *sub pixels per GBA
 * pixel); IDLE_NOPIC a logo but no picture (too big: *sub set); IDLE_NONE no logo */
enum { IDLE_NONE, IDLE_NOPIC, IDLE_PIC };
int second_idle_picture(uint32_t** px, int* w, int* h, int* sub);
/* the game's logo onto a cw x ch canvas, k canvas pixels to a logo pixel,
 * blended at alpha (LOGO_IDLE: idle), centred below `top` rows; false if
 * there's no logo (or no room) */
bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha);

/* ---- Used by the app's other parts (and hud.c, twins.c) */
/* the map's frames animate (Map Animations, not a still map) */
bool map_anim(void);
/* picture idx of the second screen (STILL_IDX: the still), NULL if none */
const uint32_t* map_px(int idx);
/* put picture idx up (unless it is up already and nothing changed: overDirty) */
void upload_map_frame(int idx);
/* the frame is one colour nearly all over (a blank screen) */
bool frame_is_flat(const uint32_t* fb);
/* how many of the probes (up to 8) the frame matches, each colour within tol */
int probes_hit(const uint32_t* fb, const Probe* p, int tol);
/* the frame drawn again without these sprites (kept: ones already hidden),
 * into out; the game put back exactly as it was. _poke: a halfword written
 * into the game's memory first, for that one frame (pokeAt 0: none) */
void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV);
void snapshot_hiding_sprites(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out);

/* ---- Tests: the host's calls into the module traced into dir, and what the
 * screens show at a screenshot (tools/second_driver.c replays a trace; see
 * second.c) */
bool second_trace_begin(const char* dir);
void second_trace_call(const char* what);
void second_trace_state_file(const char* path);
void second_trace_shot(const char* name);

#endif

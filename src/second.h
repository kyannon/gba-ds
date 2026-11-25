/* The second screen's picture: the pure pixel steps (see second.c). */
#ifndef SECOND_H
#define SECOND_H
#include "config.h"
#include "emu.h"
#include "mapcap.h"
#include "profiles.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* the still map for "Map Animations: Off" into out (r->w * r->h), the pixels
 * that animate marked in mask; pick is the profile's stillPick (STILL_*) */
void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask);
/* the picture faded to level (0 .. 256) */
const uint32_t* second_fade(const uint32_t* px, size_t n, int level);

/* "Map Animations: Off" shows the composed still (sec.stillPx) as this index */
#define STILL_IDX MAP_MAX_FRAMES
/* the logo drawn idle on the second screen: at half strength */
#define LOGO_IDLE 128

/* What the app does for the second screen (set by the app, once per game):
 * hudNew and overMap are the HUD split's part, still the app's for now (what
 * it keeps of a new map picture; its pieces laid over the picture, returning
 * the picture to show, px itself if none); present puts the picture (sec.mapW
 * x sec.mapH) on the screen, presentGba one of the game's own size. */
typedef struct {
	void (*hudNew)(bool fresh);
	const uint32_t* (*overMap)(const uint32_t* px);
	void (*present)(const uint32_t* px);
	void (*presentGba)(const uint32_t* px); /* a picture GBA_W x GBA_H (the screen copy) */
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
	/* the map refresh (game.c map_refresh_step), set up again for each game */
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
	const MapProfile* snapProf;
	int snapShow, snapRevert;
	uint32_t snapShown[GBA_W * GBA_H];
	SecondHooks hooks;
} SecondState;
extern SecondState sec;

/* A new subject: a state loaded, a rewind. The map is captured again as soon
 * as it can be (and the toggle's pages and views start again). */
void second_new_subject(void);
/* the map's frames animate (Map Animations, not a still map) */
bool map_anim(void);
/* once a frame, after the game's frame: when to capture the map, picking up a
 * finished capture, the pages, the profile's hideWhen / logoIf / liveView */
void map_refresh_step(void);
/* picture idx of the second screen (STILL_IDX: the still), NULL if none */
const uint32_t* map_px(int idx);
/* put picture idx up (unless it is up already and nothing changed: overDirty) */
void upload_map_frame(int idx);
/* a new map picture is in sec.mapRes (fresh: a capture; else kept with a state) */
void map_show_new(bool fresh);
/* a logo-only game's screen copy (sec.snapProf), once a frame */
void update_snapshot(void);
/* The second screen kept with save states (GBA DS's block inside the state:
 * EXTDATA_GBADS): made now (malloc'd, *n bytes; NULL if nothing), and put
 * back from a loaded state's block (b may be NULL: none; the title card and
 * the level name kept from before go either way) */
void* second_state_save(size_t* n);
/* A game's set-up (game.c ss_setup calls these, in this order, with its own
 * set-up between): second_reset first; once the capture exists (sec.mc),
 * hud_setup (hud.h), then second_map_setup */
void second_reset(void);
void second_map_setup(const char* code, bool logoOnly);
void second_state_load(const uint8_t* b, size_t n);
/* the frame is mostly dark / one colour nearly all over (a fade, a blank screen) */
bool frame_is_dark(const uint32_t* fb);
bool frame_is_flat(const uint32_t* fb);
/* how many of the probes (up to 8) the frame matches, each colour within tol */
int probes_hit(const uint32_t* fb, const Probe* p, int tol);
/* the frame drawn again without these sprites (kept: ones already hidden),
 * into out; the game put back exactly as it was. _poke: a halfword written
 * into the game's memory first, for that one frame (pokeAt 0: none) */
void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV);
void snapshot_hiding_sprites(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out);

#endif

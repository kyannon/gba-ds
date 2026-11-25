/* The second screen's picture: the pure pixel steps (see second.c). */
#ifndef SECOND_H
#define SECOND_H
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

/* ---- The second screen's state (libretro core, discussion #75: the module's
 * interface is being gathered here). Everything the second screen keeps
 * between frames, in one place, so the app and a core hold it the same way.
 * What it reads from outside (the emulator, the settings, the frame count,
 * the capture) is still game.c's for now (toggle.h): the next step. */
typedef struct {
	/* the capture's pictures and which is up */
	MapResult mapRes;
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
} SecondState;
extern SecondState sec;

/* A new subject: a state loaded, a rewind. The map is captured again as soon
 * as it can be (and the toggle's pages and views start again). */
void second_new_subject(void);

#endif

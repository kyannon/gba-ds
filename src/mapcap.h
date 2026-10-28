/* Shadow-instance map capture.
 *
 * A second, silent emulator core is loaded with a snapshot of the live game,
 * fed the button presses that open the in-game map, and run until the map
 * screen is up and stable. The map area is cropped out and handed back.
 * The live game is never paused or touched.
 */
#ifndef GBADS_MAPCAP_H
#define GBADS_MAPCAP_H

#include "emu.h"

typedef struct MapCapture MapCapture;

#define MAP_MAX_FRAMES 16

typedef struct MapResult {
	bool ok;
	const char* why; /* failure reason when !ok */
	int w, h;
	int nFrames;                 /* >1 when the map has an animated marker */
	int frameStep;               /* emulated frames between animation frames */
	uint32_t* pixels;            /* == frames[0] */
	uint32_t* frames[MAP_MAX_FRAMES];
	uint32_t serial;             /* increments with each successful capture */
	/* the map's empty background grid cell, when the profile has one:
	 * map pixel (u,v) of an empty cell = tile[((v-tileY)%k)*k + (u-tileX)%k] */
	bool hasTile;
	int tileK, tileX, tileY;
	uint32_t tile[16 * 16];
} MapResult;

/* Returns NULL when no profile matches gameCode. */
/* tight: use the profile's bezel-free crop and prompt masks when it has one. */
MapCapture* mapcap_create(const char* romPath, const char* gameCode, bool tight);
void mapcap_destroy(MapCapture* mc);
const char* mapcap_title(MapCapture* mc);

/* Synchronous capture (used by tools). Result valid until next call. */
MapResult mapcap_capture_sync(MapCapture* mc, Emu* live);

/* Asynchronous API for the frontend. request() snapshots the live core if
 * the worker is idle and returns true if a capture was started. poll() copies
 * the newest successful result into *out (caller-owned buffers are managed
 * internally) and returns true if it is newer than lastSerial. */
bool mapcap_request(MapCapture* mc, Emu* live);
bool mapcap_busy(MapCapture* mc);
/* Calls off the capture running, if any (the game moved on: a newer one's
 * wanted): it stops at once and publishes nothing; mapcap_busy is false
 * again as soon as it has. */
void mapcap_cancel(MapCapture* mc);
/* "Map Animations: Off": later captures record the profile's whole marker loop. */
void mapcap_set_still(MapCapture* mc, bool still);
const struct MapProfile* mapcap_profile(MapCapture* mc);
bool mapcap_poll(MapCapture* mc, uint32_t lastSerial, MapResult* out);
/* Consecutive failed captures since the last success (and the last reason). */
int mapcap_failures(MapCapture* mc, const char** why);

/* Cheap check on the live core: is this a moment where capturing makes sense?
 * (in gameplay, not in a menu). Profiles without a check return true. */
bool mapcap_live_ok(MapCapture* mc, Emu* live);
/* The game is back on its title / file menus (profiles without a check: never). */
bool mapcap_in_menu(MapCapture* mc, Emu* live);

/* Returns a value that changes when the player moves to a new map cell
 * (or 0 if the profile cannot tell). Used to trigger captures early. */
uint32_t mapcap_room_key(MapCapture* mc, Emu* live);

/* Size of the map image this profile produces. */
void mapcap_size(MapCapture* mc, int* w, int* h);

#endif

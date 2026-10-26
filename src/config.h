#ifndef GBADS_CONFIG_H
#define GBADS_CONFIG_H

#include <stdbool.h>

/* Overlays: "primary" is the game screen, "secondary" the map screen (they
 * follow the picture when the screens are swapped). Secondary also covers the
 * generated map surrounds. */
enum { OVL_PRIMARY = 1, OVL_SECONDARY = 2, OVL_BOTH = 3 };

typedef struct Config {
	char layout[16];   /* auto | span-h | span-v | dual | single */
	bool swap;         /* map on the first (left/top) screen instead of the second */
	char scale[16];    /* game: integer | fit | stretch */
	char mapScale[16]; /* map:  integer | fit | stretch */
	bool smooth;       /* plain linear filtering when scaling (blurry) */
	char borderColor[16]; /* RRGGBB behind the map screen (outside the map image) */
	int overlays;      /* OVL_PRIMARY | OVL_SECONDARY: game-screen / map-screen overlays */
	bool lcdGrid;      /* draw an LCD pixel grid over game and map */
	int lcdStrength;   /* grid darkness, 0..100 */
	bool vsync;
	bool frameBuffer; /* emulate a frame ahead on a second thread: smoother, a frame more input delay */
	bool pinCores; /* Android: the game thread on a core of its own, the map capture off it (off by default) */
	bool map;          /* enable the second-screen map */
	bool mapCrop;      /* crop map bezels/prompts where the profile knows how */
	bool tallLive;     /* Pokemon Pinball: a second copy of the game draws the bottom screen live */
	bool mapAnim;      /* cycle the map's animation frames (off: one still frame) */
	bool rewind;       /* keep a rewind buffer (hold L2 to rewind) */
	int rewindSeconds; /* how far back */
	bool matchDisplay; /* run at the display's refresh rate (if close to the GBA's) instead of 59.73 Hz */
	bool showLogo;     /* the game's logo on the second screen when it has nothing else (off: blank) */
	int mapInterval;   /* frames between periodic map refreshes */
	bool abSwap;       /* GBA A on the bottom face button instead of the right one */
	char hotkey[16];   /* guide | back */
	char menuCombo[16]; /* select+start | none: buttons held together that open the pause menu */
	int homeButton;    /* raw joystick button number that acts as HOME (-1 = off) */
	bool showFps;
	bool sway;         /* ask sway to fullscreen the window across all outputs */
	int audioLatency;  /* ms */
	int ffSpeed;       /* fast-forward multiplier */
	char saveDir[512]; /* battery saves; default: next to the ROM */
	char stateDir[512]; /* save states, thumbnails, prefs; default: <app>/states */
	char romDir[1024]; /* library folders, ':'-separated */
	char screenshotDir[512]; /* "Take Screenshot" saves here */
	char cheatDir[1024]; /* RetroArch .cht folders, ':'-separated (searched two levels deep) */
	char libraryView[8]; /* game list: "list" (names) or "grid" (logos) */
	char esConfigDir[512]; /* EmulationStation's config folder (Settings: GBA DS in EmulationStation) */
	char assetDir[512]; /* logos etc.; default: next to the binary */
	char touchScreen[16]; /* first | second: which screen has the touch panel */
	char touchMap[16];    /* window | screen: how touch coordinates map */
	/* Read the touchscreen straight from this input device (/dev/input/eventN)
	 * instead of SDL's touches (the RG DS Plus's Stock Linux OS: weston hands
	 * GBA DS none); its coordinates are fractions of the touch screen.
	 * touch_rotate: 0, 90, 180 or 270 degrees; -1: by the panel's shape. */
	char touchDevice[128];
	int touchRotate;
	bool autoResume;      /* save state on exit, load it on launch */
	bool nameTags;        /* library keeps (USA), (En,Fr) etc. in game names */
	bool showAll;         /* library lists games without a map profile too */
	/* RetroAchievements (casual mode): auto = on when ROCKNIX has an account
	 * signed in (EmulationStation's RetroAchievements settings), 1, 0 */
	char raMode[8];
	char raUser[128], raPassword[256]; /* override ROCKNIX's account */
	char raSystemCfg[512]; /* ROCKNIX's system.cfg (global.retroachievements.*) */
	char raHost[256];     /* testing: another server */
	bool updateCheck;     /* look for a new release once a day (Linux; the game list's Settings can switch it) */
	char updateUrl[256];  /* testing: the release API to ask instead of GitHub's */
	char rom[512];        /* launch straight into this game (skips the library) */

	/* testing / automation */
	char loadState[512];
	int quitAfter;     /* UI ticks */
	char inputScript[512];
} Config;

void config_defaults(Config* c);
/* Loads key=value lines; missing file is not an error. */
void config_load_file(Config* c, const char* path);
/* Parses --key=value / --flag arguments and an optional ROM path. Returns false on error. */
bool config_parse_args(Config* c, int argc, char** argv);
void config_usage(const char* argv0);

#endif

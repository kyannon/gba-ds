#ifndef GBADS_CONFIG_H
#define GBADS_CONFIG_H

#include <stdbool.h>

/* Display > Overlays: off, GBA DS's own bezel drawn by the app in the
 * player's colours (bezel.c), or the player's PNGs in overlays/ (a screen
 * without one: the bezel in its default colours). OVM_LEGACY: a config from
 * before (overlays = top / both...), settled at start (main.c): custom if
 * overlays/ has PNGs, else generated. */
enum { OVM_OFF, OVM_GENERATED, OVM_CUSTOM, OVM_LEGACY };
/* ...on which physical screens (they stay put when the screens are swapped) */
enum { OVL_TOP = 1, OVL_BOTTOM = 2, OVL_BOTH = 3 };
/* Display > Shader, in menu order. Each is a fixed pattern multiplied over
 * every GBA pixel (a texture built once, ui.c), so none costs more than the
 * LCD grid; the subpixel presets also brighten the colours to make up for
 * their stripes (the colour table, ui.c). */
enum { SH_OFF, SH_GRID, SH_SCANLINES, SH_SOFT, SH_SUB_LIGHT, SH_SUB, SH_SUB_STRONG, SH_COUNT };
extern const char* const shaderNames[SH_COUNT]; /* gba-ds.cfg's names: off, grid, ... */
/* Hotkeys: the hotkey button held with an action's button does the action
 * (game.c). gba-ds.cfg: hotkey_<name> = a button (a, b, x, y, l, r, l2, r2,
 * select, start, l3, up, down, left, right) or none. */
enum { HK_QUIT, HK_SAVE, HK_LOAD, HK_BAR, HK_SWAP, HK_SHADER, HK_COUNT };
extern const char* const hotkeyNames[HK_COUNT]; /* quit, save_state, ... */

/* custom_logos (GEN-10): the player's own and scraped logos shrunk to GBA
 * pixels, at full detail (the same size), or not used. */
enum { CL_PIXELATED, CL_FULL, CL_OFF };

typedef struct Config {
	char layout[16];   /* auto | span-h | span-v | dual | single */
	bool swap;         /* map on the first (left/top) screen instead of the second */
	char scale[16];    /* game: integer | fit | stretch */
	char mapScale[16]; /* map:  integer | fit | stretch */
	bool smooth;       /* plain linear filtering when scaling (blurry) */
	char borderColor[16]; /* RRGGBB behind the map screen (outside the map image) */
	int overlayMode;   /* OVM_*: overlays */
	int overlays;      /* OVL_TOP | OVL_BOTTOM: overlay_screens, the screens that have them */
	bool bezelLogo;    /* the GBA DS logo in the generated bezel */
	char bezelInner[8], bezelOuter[8]; /* its colours, RRGGBB */
	int shader;        /* SH_*: a pattern over every pixel on both screens (Display > Shader) */
	bool colorCorrection; /* the GBA's screen colours (mGBA's GBA Color shader) */
	bool ghosting;     /* LCD ghosting: each frame of the game shown blended with the one before */
	int lcdStrength;   /* grid, scanline and soft-cell darkness, 0..100 */
	bool vsync;
	bool frameBuffer; /* emulate a frame ahead on a second thread: smoother, a frame more input delay */
	bool pinCores; /* Android: the game thread on a core of its own, the map capture off it (off by default) */
	bool map;          /* enable the second-screen map */
	bool mapCrop;      /* crop map bezels/prompts where the profile knows how */
	bool tallLive;     /* Pokemon Pinball: a second copy of the game draws the bottom screen live */
	bool mapAnim;      /* cycle the map's animation frames (off: one still frame) */
	bool rewind;       /* keep a rewind buffer (hold L2 to rewind) */
	int rewindSeconds; /* how far back */
	bool fastForward;  /* hold R2 to fast-forward */
	bool matchDisplay; /* run at the display's refresh rate (if close to the GBA's) instead of 59.73 Hz */
	bool showLogo;     /* the game's logo on the second screen when it has nothing else (off: blank) */
	int mapInterval;   /* frames between periodic map refreshes */
	bool abSwap;       /* GBA A on the bottom face button instead of the right one */
	char hotkey[16];   /* home | select | start | l3 (older names: guide = home, back = select) */
	char hotkeyBtn[HK_COUNT][8]; /* each hotkey action's button (hotkey_quit ...), or none */
	int shaderLast;    /* the shader the shader hotkey turns back on (shader_last) */
	char menuCombo[16]; /* select+start | none: buttons held together that open the pause menu */
	int homeButton;    /* raw joystick button number that acts as HOME (-1 = off) */
	bool showFps;
	bool sway;         /* ask sway to fullscreen the window across all outputs */
	int audioLatency;  /* ms */
	int ffSpeed;       /* fast-forward multiplier */
	char saveDir[512]; /* battery saves; default: next to the ROM */
	char saveExt[8];   /* their extension: sav, or srm (RetroArch's: BananaOS's GBA saves, shared) */
	char saveFrom[512]; /* where GBA DS kept them before (<game>.sav), carried over: BananaOS */
	char stateDir[512]; /* save states, thumbnails, prefs; default: <app>/states */
	char romDir[1024]; /* library folders, ':'-separated */
	char screenshotDir[512]; /* "Take Screenshot" saves here */
	char cheatDir[1024]; /* RetroArch .cht folders, ':'-separated (searched two levels deep) */
	char patchDir[1024]; /* patches, a folder per game in it (GEN-9); default: <assetDir>/patches */
	char libraryView[8]; /* game list: "list" (names) or "grid" (logos) */
	char esConfigDir[512]; /* EmulationStation's config folder (Settings: GBA DS in EmulationStation) */
	char assetDir[512]; /* logos etc.; default: the data folder */
	char logoFolder[512]; /* scraped logos, found by the ROM's file name (GEN-10); empty: none */
	int customLogos;      /* the player's own and scraped logos (GEN-10): CL_PIXELATED, CL_FULL or CL_OFF */
	char dataDir[512]; /* data_dir: GBA DS's data folder (states/, cheats/...); default: the app's folder (paths.c data_dir_setup) */
	char touchScreen[16]; /* first | second: which screen has the touch panel */
	char topScreen[16];   /* Android: auto | app | second: which display is the top screen */
	char screenMatch[8];  /* keep | aspect | size: the wider screen's picture area (ui.c layout_pictures) */
	char screenPpi[32];   /* "top,bottom": the screens' pixels per inch, instead of what the system says */
	char touchMap[16];    /* window | screen: how touch coordinates map */
	/* Read the touchscreen straight from this input device (/dev/input/eventN)
	 * instead of SDL's touches (the RG DS Plus's Stock Linux OS: weston hands
	 * GBA DS none); its coordinates are fractions of the touch screen.
	 * touch_rotate: 0, 90, 180 or 270 degrees; -1: by the panel's shape. */
	char touchDevice[128];
	int touchRotate;
	bool autoSave;        /* save a resume point on quitting the game (save_on_quit) */
	bool autoLoad;        /* load it on launching the game (auto_load) */
	bool barPinned;       /* the quick bar stays up (quick_bar_pinned) */
	bool barHint;         /* "Swipe up for quick menu" at a game's start (quick_bar_hint) */
	bool nameTags;        /* library keeps (USA), (En,Fr) etc. in game names */
	bool showAll;         /* library lists games without a map profile too */
	/* RetroAchievements (casual mode): auto = on when ROCKNIX has an account
	 * signed in (EmulationStation's RetroAchievements settings), 1, 0 */
	char raMode[8];
	char raUser[128], raPassword[256]; /* override ROCKNIX's account */
	char raSystemCfg[512]; /* ROCKNIX's system.cfg (global.retroachievements.*) */
	char raHost[256];     /* testing: another server */
	bool updateCheck;     /* look for a new release once a day (the game list's Settings can switch it) */
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

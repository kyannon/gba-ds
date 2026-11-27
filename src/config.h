#ifndef GBADS_CONFIG_H
#define GBADS_CONFIG_H

#include <stdbool.h>
#include <stddef.h>

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
/* Display > Scaling filter (gba-ds.cfg's "smooth"; issue #89): the upscalers
 * run on the GPU (upscale.c) */
enum { FILTER_SHARP, FILTER_SMOOTH, FILTER_SCALE2X, FILTER_SCALE4X, FILTER_HQ2X, FILTER_MMPX, FILTER_XBR_LV2, FILTER_XBR_LV3, FILTER_OMNISCALE, FILTER_COUNT };
extern const char* const filterNames[FILTER_COUNT];
extern const char* const shaderNames[SH_COUNT]; /* gba-ds.cfg's names: off, grid, ... */
/* the fast-forward speeds the pause menu goes through (ff_speed) */
#define FF_SPEED_MIN 2
#define FF_SPEED_MAX 5
/* Hotkeys: the hotkey button held with an action's button does the action
 * (game.c). gba-ds.cfg: hotkey_<name> = a button (a, b, x, y, l, r, l2, r2,
 * select, start, l3, up, down, left, right) or none. HK_REWIND_TOGGLE and
 * HK_FF_TOGGLE turn rewinding / fast-forwarding on until pressed again
 * (issue #88; no button at first). */
enum { HK_QUIT, HK_GAME, HK_SAVE, HK_LOAD, HK_BAR, HK_SWAP, HK_SHADER, HK_TOGGLE, HK_REWIND_TOGGLE, HK_FF_TOGGLE, HK_COUNT };
extern const char* const hotkeyNames[HK_COUNT]; /* quit (close the emulator), close_game, save_state, ... */
/* Held buttons (issue #88): rewind and fast forward while a button is held,
 * on its own or with the hotkey button (rewind_button = l2,
 * fast_forward_button = r2 at first; or none). */
enum { HOLD_REWIND, HOLD_FF, HOLD_COUNT };
extern const char* const holdNames[HOLD_COUNT]; /* rewind_button, fast_forward_button */

/* custom_logos (GEN-11): the player's own and scraped logos shrunk to GBA
 * pixels, at full detail (the same size), or not used. */
enum { CL_PIXELATED, CL_FULL, CL_OFF };

typedef struct Config {
	char layout[16];   /* auto | span-h | span-v | dual | single */
	bool swap;         /* map on the first (left/top) screen instead of the second */
	char scale[16];    /* game: integer | fit | stretch */
	char mapScale[16]; /* map:  integer | fit | stretch */
	char gbSize[8];    /* Game Boy games (GB-2): gba = the picture in the GBA's 240x160 frame | native = the 160x144 picture is the frame */
	char gbCorrection[8]; /* GB-8: same | off | gbc | gba */
	char gbShader[12]; /* GB-10: same | off | grid | scanlines | soft | dots */
	char gbGhosting[8]; /* GB-9: same | off | light | smart | strong */
	char gbPalette[8]; /* GB-6: own | dmg | pocket | light | gray | sgb: a monochrome game's shades (from the next start) */
	char gbBorder[8];  /* GB-12: none | sgb: a Super Game Boy game with its own border (from the next start) */
	char gbDual[8];    /* GB-7: colour | mono: a dual-mode game in colour, or as on a plain Game Boy (from the next start) */
	char gbScale[16];  /* ...and how that frame fills the screen (GB-2a): integer | fit | stretch */
	int filter;        /* FILTER_* (upscale.h): sharp, smooth (linear) or an upscaler; gba-ds.cfg's "smooth" */
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
	bool tallLive;     /* Pokemon Pinball: a second copy of the game draws the bottom screen live */
	bool toggle;       /* the contextual toggle (GEN-14, issue #37): the second screen's pages or view turned by hand; off */
	bool autoRotate;   /* with the toggle on: timed pages keep turning by themselves (a press moves them on); on */
	char toggleButton[8]; /* where the toggle's button sits on the second screen: left | center | right | off */
	bool rewind;       /* keep a rewind buffer (hold L2 to rewind) */
	int rewindSeconds; /* how far back */
	bool fastForward;  /* hold R2 to fast-forward */
	bool ffMute;       /* no sound while fast-forwarding (ff_mute, issue #109); on */
	bool matchDisplay; /* run at the display's refresh rate (if close to the GBA's) instead of 59.73 Hz */
	int mapInterval;   /* frames between periodic map refreshes */
	int solarLevel;    /* the sunlight a sun sensor (Boktai) reads, 0-10 */
	bool abSwap;       /* GBA A on the bottom face button instead of the right one */
	char hotkey[16];   /* home | select | start | l3 (older names: guide = home, back = select) */
	char hotkeyBtn[HK_COUNT][8]; /* each hotkey action's button (hotkey_quit ...), or none */
	char holdBtn[HOLD_COUNT][8]; /* the buttons held to rewind / fast-forward (rewind_button ...), or none */
	int shaderLast;    /* the shader the shader hotkey turns back on (shader_last) */
	char menuCombo[16]; /* select+start | none: buttons held together that open the pause menu */
	int homeButton;    /* raw joystick button number that acts as HOME (-1 = off) */
	bool showFps;
	bool sway;         /* ask sway to fullscreen the window across all outputs */
	int audioLatency;  /* ms */
	bool audioAuto;    /* raise the sound delay a step when the sound keeps running dry (game.c; the setting itself is left alone) */
	bool keepSound;    /* the game and its sound go on while the system holds up a frame's present (game.c keeper) */
	bool perfMax;      /* Linux: CPU, memory and GPU clocks held at their fastest while GBA DS runs (clocks.c); off: the system decides */
	int ffSpeed;       /* fast-forward multiplier (the menu cycles FF_SPEED_MIN..MAX) */
	char saveDir[512]; /* battery saves; default: next to the ROM */
	char saveExt[8];   /* their extension: sav, or srm (RetroArch's: BananaOS's GBA saves, shared) */
	char saveFrom[512]; /* where GBA DS kept them before (<game>.sav), carried over: BananaOS */
	char stateDir[512]; /* save states, thumbnails, prefs; default: <app>/states */
	char stateFolder[12]; /* slots 0-9 and the automatic state: "gbads" (stateDir) or "retroarch" (RetroArch's own, shared: issue #39) */
	char raStateDir[512]; /* RetroArch's state folder when it isn't found from its settings (retroarch_state_dir) */
	char raCfg[512];      /* RetroArch's retroarch.cfg (retroarch_cfg; default: looked for) */
	char romDir[1024]; /* library folders, ':'-separated */
	char screenshotDir[512]; /* "Take Screenshot" saves here */
	char cheatDir[1024]; /* RetroArch .cht folders, ':'-separated (searched two levels deep) */
	char patchDir[1024]; /* patches, a folder per game in it (GEN-9); default: <assetDir>/patches */
	char libraryView[8]; /* game list: "list" (names), "grid" (two columns of logos) or "grid3" (three) */
	char librarySort[8]; /* game list: "alpha" (A to Z, the default) or "recent" (last played first); favorites come first in both */
	char esConfigDir[512]; /* EmulationStation's config folder (Settings: GBA DS in EmulationStation) */
	char assetDir[512]; /* logos etc.; default: the data folder */
	char logoFolder[512]; /* scraped logos, found by the ROM's file name (GEN-11); empty: none */
	int customLogos;      /* the player's own and scraped logos (GEN-11): CL_PIXELATED, CL_FULL or CL_OFF */
	char dataDir[512]; /* data_dir: GBA DS's data folder (states/, cheats/...); default: the app's folder (paths.c data_dir_setup) */
	char touchScreen[16]; /* first | second: which screen has the touch panel */
	char topScreen[16];   /* Android: auto | app | second: which display is the top screen */
	char screenMatch[8];  /* keep | aspect | size: the wider screen's picture area (ui.c ui_layout_pictures) */
	char singleLayout[8]; /* off | auto | side | stacked: with one display, both screens on it (issue #84; ui.c layout_screens) */
	char singleSize[8];   /* equal | medium | small: the second screen's share of it (50 / 40 / 30 %) */
	char padCover[8];     /* auto | on | off: a controller folded over the bottom of a phone held upright (8BitDo FlipPad, issue #107): the screens kept above it (ui.c pad_cover) */
	int padCoverMm;       /* how much of the display it covers, from the bottom edge (mm) */
	char screenPlace[8];  /* center | no_gap: two screens' pictures centred, or against the edges nearest each other (issue #46) */
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
	int brightTop, brightBottom; /* this app's brightness for each screen, 20..100 (brightness_top / _bottom, issue #56): a dimming layer, not the system's */
	char accent[16];      /* the menus' colour: purple | blue | teal | green | red | orange | pink (accent) */
	bool lightTheme;      /* the light theme for the menus' buttons (theme = light) */
	bool leftStickDpad;   /* the left stick also presses the D-pad in games (left_stick_dpad, issue #57) */
	int rumbleStrength;   /* how hard the device's motor runs when a game rumbles, 0..100 %, 0 off (rumble_strength, issue #83) */
	int turboRate;        /* Turbo A / Turbo B: presses a second, 10, 15 or 20 (turbo_rate, issue #55) */
	char faceBtn[10][12]; /* what each button gives the game (button_right, _bottom, _top, _left, _l, _r, _dpad_up, _dpad_down, _dpad_left, _dpad_right: a, b, l, r, up, down, left, right, start, select, turbo_a, turbo_b, none); empty: the GBA's own (ab_swap's for A and B) */
	/* RetroAchievements (casual mode): auto = on when ROCKNIX has an account
	 * signed in (EmulationStation's RetroAchievements settings), 1, 0 */
	char raMode[8];
	char raUser[128], raPassword[256]; /* override ROCKNIX's account */
	char raSystemCfg[512]; /* ROCKNIX's system.cfg (global.retroachievements.*) */
	bool raHardcore;      /* RetroAchievements hardcore (ra_hardcore; the pause menu's switch, kept in prefs.cfg) */
	char raHost[256];     /* testing: another server */
	bool gbaBios;         /* run games with the player's own GBA BIOS (<data folder>/bios/gba_bios.bin; issue #78; Settings > System) */
	int romPath;          /* the selected game's full path and file name under the game list's other screen: 0 off, else its font size 1x-4x (show_rom_path; Settings > System; GEN-21) */
	bool updateCheck;     /* look for a new release once a day (the game list's Settings can switch it) */
	char updateUrl[256];  /* testing: the release API to ask instead of GitHub's */
	char rom[512];        /* launch straight into this game (skips the library) */
	char language[16];    /* the interface's language (i18n.h): auto (the system's), en or a catalogue's code */
	bool languageAsked;   /* language_asked (prefs.cfg): 0 on a first install until the player has confirmed the
	                       * language (library_language_ask, issue #92); absent (every older install) is 1 */

	/* testing / automation */
	char loadState[512];
	int quitAfter;     /* UI ticks */
	char inputScript[512];
} Config;

void config_defaults(Config* c);

/* ---- the settings the menus offer (docs/MENUS.md): the game list's Settings
 * (for all games) and, later, the pause menu (this game or all) show the same
 * rows, from this table. A setting is a bool, an int or a string in Config
 * with a list of choices (each with the text gba-ds.cfg uses), or a number
 * in a range; fast forward is two keys in one row. */
enum { SF_BOOL, SF_INT, SF_STR, SF_FF };
typedef struct {
	const char* value; /* as gba-ds.cfg writes it */
	const char* label; /* as the menu says it */
	int ival;          /* SF_BOOL / SF_INT: the field's value for it */
} SettingChoice;
typedef struct {
	const char* key;
	const char* label;
	int field;
	size_t off, size; /* the field in Config */
	const SettingChoice* choices;
	int nChoices;     /* 0: a number from min to max in steps */
	int min, max, step;
	const char* unit; /* after a number: "%" or " s" */
	const char* note; /* one line, for the selected row */
} Setting;
const Setting* config_setting(const char* key);
int config_setting_count(void);
const Setting* config_setting_at(int i);
/* copy the setting's field(s) from src to dst (fast forward: both of its fields) */
void config_setting_copy(Config* dst, const Config* src, const Setting* s);
/* the choice the setting is at, or -1 (a number off the list, or none) */
int config_setting_index(const Config* c, const Setting* s);
/* the value as the row shows it ("Pixel Perfect", "On", "30 s") */
void config_setting_text(const Config* c, const Setting* s, char* out, size_t n);
/* step by dir (+1 / -1), round at the ends; false if nothing changed */
bool config_setting_step(Config* c, const Setting* s, int dir);
/* the lines gba-ds.cfg would hold for the setting's value: [n][2] key, value; returns n */
int config_setting_lines(const Config* c, const Setting* s, char lines[2][2][24]);
/* Updates (or adds) key = value lines in a file of the app's own (prefs.cfg), keeping
 * every other line. */
void config_kv_update(const char* path, const char* header, int n, const char* const kv[][2]);
/* Takes the lines for these keys out of the file (it's removed when nothing but a comment is left) */
void config_kv_remove(const char* path, int n, const char* const keys[]);
/* The keys a file holds, up to max of size 40: returns how many */
int config_kv_keys(const char* path, int max, char keys[][40]);
/* Loads key=value lines; missing file is not an error. */
void config_load_file(Config* c, const char* path);
/* A game's own settings (issue #53): the lines of its file (states/<game>.cfg)
 * that are for one game only (config_key_per_game), put over the settings
 * as they are, except what was given on the command line. Returns how many
 * lines were used. */
int config_load_game_file(Config* c, const char* path);
bool config_key_per_game(const char* key);
/* Parses --key=value / --flag arguments and an optional ROM path. Returns false on error. */
bool config_parse_args(Config* c, int argc, char** argv);
void config_usage(const char* argv0);

#endif

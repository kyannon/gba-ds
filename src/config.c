#include "config.h"
#include "i18n.h"
#include "i18n_data.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <strings.h>

const char* const shaderNames[SH_COUNT] = { N_("off"), "grid", "scanlines", "soft", "subpixels-light", "subpixels", "subpixels-strong" };
const char* const hotkeyNames[HK_COUNT] = { "quit", "close_game", "save_state", "load_state", "quick_menu", "swap_screens", "shader", "toggle" };
static const char* const hotkeyDefaults[HK_COUNT] = { "start", "b", "r", "l", "x", "up", "down", "a" };

void config_defaults(Config* c) {
	memset(c, 0, sizeof(*c));
	strcpy(c->layout, "auto");
	strcpy(c->saveExt, "sav");
	strcpy(c->stateFolder, "gbads");
	strcpy(c->scale, "integer");
	strcpy(c->mapScale, "integer");
	strcpy(c->accent, "purple");
	strcpy(c->hotkey, "home");
	for (int i = 0; i < HK_COUNT; ++i) strcpy(c->hotkeyBtn[i], hotkeyDefaults[i]);
	c->shaderLast = SH_GRID;
	strcpy(c->menuCombo, "none");
	strcpy(c->romDir, "/storage/roms/gba");
	strcpy(c->screenshotDir, "/storage/roms/screenshots");
	strcpy(c->cheatDir, "/config/retroarch/cheats:/storage/.config/retroarch/cheats");
	strcpy(c->esConfigDir, "/storage/.config/emulationstation");
	strcpy(c->libraryView, "list");
	strcpy(c->touchScreen, "second");
	strcpy(c->topScreen, "auto");
	strcpy(c->screenMatch, "keep");
	strcpy(c->screenPlace, "center");
	strcpy(c->touchMap, "window");
	c->touchRotate = -1;
	c->map = true;
	c->tallLive = true;
	c->autoRotate = true;
	strcpy(c->toggleButton, "left");
	c->customLogos = CL_PIXELATED;
	c->rewind = true;
	c->rewindSeconds = 30;
	c->overlayMode = OVM_GENERATED;
	c->overlays = OVL_BOTH;
	c->bezelLogo = true;
	strcpy(c->bezelInner, "1A1A1A");
	strcpy(c->bezelOuter, "102060");
	c->mapInterval = 60;
	c->solarLevel = 6;
	c->sway = true;
	c->audioLatency = 64;
	c->audioAuto = true;
	c->keepSound = true;
	c->ffSpeed = 3;
	c->fastForward = true;
	c->pinCores = false; /* tried on the RG DS Plus: no better (docs/ANDROID.md) */
	c->autoSave = true;
	c->autoLoad = true;
	c->barHint = true;
	c->updateCheck = true;
	c->showAll = false;
	c->nameTags = false;
	strcpy(c->raMode, "auto");
	strcpy(c->raSystemCfg, "/storage/.config/system/configs/system.cfg");
	c->homeButton = -1;
	c->leftStickDpad = true;
	c->turboRate = 15;
	c->rumbleStrength = 100;
	c->brightTop = c->brightBottom = 100;
	c->lcdStrength = 35;
	strcpy(c->gbSize, "gba");
	strcpy(c->gbScale, "integer");
	strcpy(c->borderColor, "000000");
	strcpy(c->language, "auto");
}

static bool parse_bool(const char* v) {
	return !strcmp(v, "1") || !strcasecmp(v, "true") || !strcasecmp(v, "yes") || !strcasecmp(v, "on");
}

static bool set_option(Config* c, const char* key, const char* val) {
#define STR(name, field) \
	if (!strcmp(key, name)) { \
		snprintf(c->field, sizeof(c->field), "%s", val); \
		return true; \
	}
#define BOOL(name, field) \
	if (!strcmp(key, name)) { \
		c->field = parse_bool(val); \
		return true; \
	}
#define INT(name, field) \
	if (!strcmp(key, name)) { \
		c->field = atoi(val); \
		return true; \
	}
	STR("layout", layout)
	BOOL("swap", swap)
	STR("scale", scale)
	STR("map_scale", mapScale)
	STR("gb_size", gbSize)
	STR("gb_scale", gbScale)
	BOOL("smooth", smooth)
	if (!strcmp(key, "shader")) {
		for (int i = 0; i < SH_COUNT; ++i) {
			if (!strcasecmp(val, shaderNames[i])) c->shader = i;
		}
		return true;
	}
	if (!strcmp(key, "lcd_grid")) {
		/* (before v1.4.12: the grid on or off) */
		if (parse_bool(val)) c->shader = SH_GRID;
		else if (c->shader == SH_GRID) c->shader = SH_OFF;
		return true;
	}
	BOOL("color_correction", colorCorrection)
	BOOL("lcd_ghosting", ghosting)
	if (!strcmp(key, "overlays")) {
		/* off / generated / custom. From before v1.5: none / 0, or the screens
		 * (top / bottom / both / 1; primary / secondary, the game / map screen,
		 * as the screens are unswapped), the player's PNGs: OVM_LEGACY */
		if (!strcmp(val, "off") || !strcmp(val, "none") || !strcmp(val, "0") || !strcasecmp(val, "false")) c->overlayMode = OVM_OFF;
		else if (!strcmp(val, "generated")) c->overlayMode = OVM_GENERATED;
		else if (!strcmp(val, "custom")) c->overlayMode = OVM_CUSTOM;
		else {
			c->overlayMode = OVM_LEGACY;
			c->overlays = !strcmp(val, "top") || !strcmp(val, "primary")      ? OVL_TOP :
			              !strcmp(val, "bottom") || !strcmp(val, "secondary") ? OVL_BOTTOM : OVL_BOTH;
		}
		return true;
	}
	if (!strcmp(key, "overlay_screens")) {
		c->overlays = !strcmp(val, "top") ? OVL_TOP : !strcmp(val, "bottom") ? OVL_BOTTOM : OVL_BOTH;
		return true;
	}
	BOOL("bezel_logo", bezelLogo)
	if (!strcmp(key, "bezel_inner") || !strcmp(key, "bezel_outer")) {
		/* RRGGBB (a # in front is fine); anything else keeps the default */
		const char* h = val[0] == '#' ? val + 1 : val;
		bool ok = strlen(h) == 6;
		for (int i = 0; ok && i < 6; ++i) ok = isxdigit((unsigned char) h[i]);
		if (ok) {
			char* f = key[6] == 'i' ? c->bezelInner : c->bezelOuter;
			for (int i = 0; i < 6; ++i) f[i] = (char) toupper((unsigned char) h[i]);
			f[6] = 0;
		}
		return true;
	}
	STR("border_color", borderColor)
	STR("language", language)
	INT("lcd_strength", lcdStrength)
	BOOL("vsync", vsync)
	BOOL("pin_cores", pinCores)
	BOOL("frame_buffer", frameBuffer)
	BOOL("map", map)
	BOOL("pinball_live", tallLive)
	BOOL("toggle", toggle)
	BOOL("auto_rotate", autoRotate)
	STR("toggle_button", toggleButton)
	STR("logo_folder", logoFolder)
	if (!strcmp(key, "custom_logos")) {
		c->customLogos = !strcmp(val, "off") ? CL_OFF : !strcmp(val, "full") ? CL_FULL : CL_PIXELATED;
		return true;
	}
	if (!strcmp(key, "logo_pixelate")) { /* (the test builds before custom_logos) */
		c->customLogos = parse_bool(val) ? CL_PIXELATED : CL_FULL;
		return true;
	}
	BOOL("match_display", matchDisplay)
	BOOL("rewind", rewind)
	BOOL("keep_sound", keepSound)
	BOOL("audio_auto", audioAuto)
	INT("rewind_seconds", rewindSeconds)
	INT("map_interval", mapInterval)
	INT("solar_level", solarLevel)
	BOOL("ab_swap", abSwap)
	STR("hotkey", hotkey)
	if (!strcmp(key, "hotkey_fps")) return true; /* (a test build's: the quick menu has the FPS counter) */
	if (!strncmp(key, "hotkey_", 7)) {
		for (int i = 0; i < HK_COUNT; ++i) {
			if (strcmp(key + 7, hotkeyNames[i])) continue;
			snprintf(c->hotkeyBtn[i], sizeof(c->hotkeyBtn[i]), "%s", val);
			for (char* p = c->hotkeyBtn[i]; *p; ++p) *p = (char) tolower((unsigned char) *p);
			return true;
		}
	}
	if (!strcmp(key, "shader_last")) {
		for (int i = 1; i < SH_COUNT; ++i) {
			if (!strcasecmp(val, shaderNames[i])) c->shaderLast = i;
		}
		return true;
	}
	STR("menu_combo", menuCombo)
	INT("home_button", homeButton)
	BOOL("show_fps", showFps)
	BOOL("sway", sway)
	INT("audio_latency", audioLatency)
	INT("ff_speed", ffSpeed)
	BOOL("fast_forward", fastForward)
	STR("save_dir", saveDir)
	STR("save_ext", saveExt)
	STR("save_from", saveFrom)
	STR("state_dir", stateDir)
	STR("state_folder", stateFolder)
	STR("retroarch_state_dir", raStateDir)
	STR("retroarch_cfg", raCfg)
	STR("rom_dir", romDir)
	STR("cheat_dir", cheatDir)
	STR("patch_dir", patchDir)
	STR("screenshot_dir", screenshotDir)
	STR("asset_dir", assetDir)
	STR("data_dir", dataDir)
	if (!strcmp(key, "performance")) {
		c->perfMax = !strcmp(val, "max");
		return true;
	}
	if (!strcmp(key, "pause_services")) return true; /* (read by launch.sh on ROCKNIX: services in the way stopped for the run) */
	if (!strcmp(key, "watch_system")) return true; /* (read by launch.sh on ROCKNIX: kernel, journal and sway events in the log) */
	if (!strcmp(key, "cpu_performance")) return true; /* (v1.5.3, launch.sh's: always on unless 0; now performance, off by default) */
	if (!strcmp(key, "data_dir_now")) return true; /* (data-folder.cfg: where the data is, written by the app) */
	STR("touch_screen", touchScreen)
	STR("top_screen", topScreen)
	STR("screen_match", screenMatch)
	STR("screen_placement", screenPlace)
	STR("screen_ppi", screenPpi)
	STR("touch_map", touchMap)
	STR("touch_device", touchDevice)
	INT("touch_rotate", touchRotate)
	if (!strcmp(key, "auto_resume")) {
		/* older configs: both at once */
		c->autoSave = c->autoLoad = parse_bool(val);
		return true;
	}
	BOOL("save_on_quit", autoSave)
	BOOL("auto_load", autoLoad)
	BOOL("quick_bar_pinned", barPinned)
	BOOL("quick_bar_hint", barHint)
	BOOL("show_all", showAll)
	BOOL("left_stick_dpad", leftStickDpad)
	STR("accent", accent)
	if (!strcmp(key, "theme")) {
		c->lightTheme = !strcmp(val, "light") || parse_bool(val);
		return true;
	}
	INT("turbo_rate", turboRate)
	INT("rumble_strength", rumbleStrength)
	INT("brightness_top", brightTop)
	INT("brightness_bottom", brightBottom)
	if (!strncmp(key, "button_", 7)) {
		static const char* const faces[10] = { "right", "bottom", "top", "left", "l", "r", "dpad_up", "dpad_down", "dpad_left", "dpad_right" };
		for (int i = 0; i < 10; ++i) {
			if (strcmp(key + 7, faces[i])) continue;
			snprintf(c->faceBtn[i], sizeof(c->faceBtn[i]), "%s", val);
			for (char* q = c->faceBtn[i]; *q; ++q) *q = (char) tolower((unsigned char) *q);
			return true;
		}
	}
	BOOL("name_tags", nameTags)
	STR("retroachievements", raMode)
	BOOL("ra_hardcore", raHardcore)
	STR("ra_username", raUser)
	STR("ra_password", raPassword)
	STR("ra_system_cfg", raSystemCfg)
	STR("ra_host", raHost)
	BOOL("gba_bios", gbaBios)
	BOOL("update_check", updateCheck)
	STR("update_url", updateUrl)
	STR("load_state", loadState)
	INT("quit_after", quitAfter)
	STR("es_config_dir", esConfigDir)
	STR("library_view", libraryView)
	STR("input_script", inputScript)
	return false;
#undef STR
#undef BOOL
#undef INT
}

/* the keys given on the command line: they win over a game's own file too */
static char cliKeys[48][64];
static int nCliKeys;

/* Settings a game's own file (states/<game>.cfg, issue #53) may hold: the
 * ones that are read when a game starts or while it plays. Everything else
 * (folders, accounts, updates, how the screens are found, hotkeys, the
 * touch panel) is for all games. */
static const char* const perGameKeys[] = {
	"swap", "scale", "map_scale", "gb_size", "gb_scale", "smooth", "border_color", "overlays", "overlay_screens", "bezel_logo", "bezel_inner",
	"bezel_outer", "shader", "lcd_strength", "color_correction", "lcd_ghosting", "frame_buffer", "match_display", "map",
	"map_interval", "custom_logos", "pinball_live", "toggle", "auto_rotate", "toggle_button", "rewind", "rewind_seconds",
	"fast_forward", "ff_speed", "ab_swap", "show_fps", "audio_latency", "screen_match", "screen_placement",
	"save_on_quit", "auto_load", "left_stick_dpad", "turbo_rate", "rumble_strength", "brightness_top", "brightness_bottom", "button_right", "button_bottom", "button_top", "button_left",
	"button_l", "button_r", "button_dpad_up", "button_dpad_down", "button_dpad_left", "button_dpad_right",
};

bool config_key_per_game(const char* key) {
	for (size_t i = 0; i < sizeof(perGameKeys) / sizeof(perGameKeys[0]); ++i)
		if (!strcmp(key, perGameKeys[i])) return true;
	return false;
}

static bool cli_has(const char* key) {
	for (int i = 0; i < nCliKeys; ++i)
		if (!strcmp(cliKeys[i], key)) return true;
	return false;
}

static int load_file(Config* c, const char* path, bool game) {
	int applied = 0;
	FILE* f = fopen(path, "r");
	if (!f) {
		return 0;
	}
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		char* p = line;
		while (isspace((unsigned char) *p)) ++p;
		if (!*p || *p == '#' || *p == ';') continue;
		char* eq = strchr(p, '=');
		if (!eq) continue;
		*eq = 0;
		char* key = p;
		char* val = eq + 1;
		char* end = eq - 1;
		while (end >= key && isspace((unsigned char) *end)) *end-- = 0;
		while (isspace((unsigned char) *val)) ++val;
		end = val + strlen(val) - 1;
		while (end >= val && isspace((unsigned char) *end)) *end-- = 0;
		if (game && !config_key_per_game(key)) {
			fprintf(stderr, "%s: '%s' is for all games: ignored here\n", path, key);
			continue;
		}
		if (game && cli_has(key)) continue; /* (given on the command line: that wins) */
		if (!set_option(c, key, val)) {
			fprintf(stderr, "%s: unknown option '%s'\n", path, key);
		} else {
			++applied;
		}
	}
	fclose(f);
	return applied;
}

void config_load_file(Config* c, const char* path) {
	load_file(c, path, false);
}

int config_load_game_file(Config* c, const char* path) {
	return load_file(c, path, true);
}

bool config_parse_args(Config* c, int argc, char** argv) {
	for (int i = 1; i < argc; ++i) {
		const char* a = argv[i];
		if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
			return false;
		}
		if (!strncmp(a, "--", 2)) {
			char key[64];
			const char* eq = strchr(a, '=');
			const char* val = "1";
			size_t len = eq ? (size_t) (eq - a - 2) : strlen(a + 2);
			if (len >= sizeof(key)) len = sizeof(key) - 1;
			memcpy(key, a + 2, len);
			key[len] = 0;
			for (char* k = key; *k; ++k) {
				if (*k == '-') *k = '_';
			}
			if (eq) {
				val = eq + 1;
			} else if (!strncmp(key, "no_", 3)) {
				memmove(key, key + 3, strlen(key + 3) + 1);
				val = "0";
			}
			if (!set_option(c, key, val)) {
				fprintf(stderr, "unknown option %s\n", a);
				return false;
			}
			if (nCliKeys < (int) (sizeof(cliKeys) / sizeof(cliKeys[0]))) snprintf(cliKeys[nCliKeys++], sizeof(cliKeys[0]), "%s", key);
		} else {
			snprintf(c->rom, sizeof(c->rom), "%s", a);
		}
	}
	return true;
}

void config_usage(const char* argv0) {
	fprintf(stderr,
	        "GBA DS " GBADS_VERSION "\n"
	        "usage: %s [options] [game.gba]\n"
	        "\n"
	        "Without a game, opens the game library (games found in --rom-dir).\n"
	        "\n"
	        "Options (also accepted as key=value lines in gba-ds.cfg):\n"
	        "  --rom-dir=DIR[:DIR]    library folders (/storage/roms/gba)\n"
	        "  --cheat-dir=DIR[:DIR]  RetroArch .cht folders (/config/retroarch/cheats:...)\n"
	        "  --patch-dir=DIR        patches, a folder per game (default: patches beside the app)\n"
	        "  --screenshot-dir=DIR   where Take Screenshot saves (/storage/roms/screenshots)\n"
	        "  --layout=auto|span-h|span-v|dual|single   screen arrangement (auto)\n"
	        "  --swap                 show the map on the first screen\n"
	        "  --touch-screen=first|second   which screen has the touch panel (second)\n"
	        "  --touch-map=window|screen     touch coordinates span the window or one screen\n"
	        "  --scale=integer|fit|stretch      game scaling (integer)\n"
	        "  --map-scale=integer|fit|stretch  map scaling (integer)\n"
	        "  --scale=fit            fill the screen width (keeps the aspect ratio)\n"
	        "  --shader=NAME          a pattern over every pixel: off, grid, scanlines, soft,\n"
	        "                         subpixels-light, subpixels, subpixels-strong\n"
	        "                         (--lcd-strength=0..100 for the first three)\n"
	        "  --lcd-ghosting         each frame blended with the one before, as on a GBA's LCD\n"
	        "  --smooth               plain linear filtering\n"
	        "  --vsync                sync presentation to the display\n"
	        "  --no-map               disable the map screen\n"
	        "  --map-interval=N       frames between map refreshes (90)\n"
	        "  --ab-swap              GBA A on the bottom face button\n"
	        "  --hotkey=home|select|start|l3   the hotkey button, held with another for a shortcut\n"
	        "  --hotkey-quit=BUTTON (and -close-game, -save-state, -load-state, -quick-menu, -swap-screens, -shader)\n"
	        "                         each shortcut's button (a, b, x, y, l, r, l2, r2, select,\n"
	        "                         start, l3, up, down, left, right) or none\n"
	        "  --menu-combo=select+start|none   buttons that open the menu together (none)\n"
	        "  --home-button=N        raw joystick button that acts as HOME (see GBADS_DEBUG=1 log)\n"
	        "  --no-auto-resume       don't save on exit / resume on launch\n"
	        "  --no-save-on-quit      don't save a resume point on quitting (--no-auto-load: don't load it)\n"
	        "  --show-fps\n"
	        "  --no-sway              don't ask sway to span both outputs\n"
	        "  --audio-latency=MS     (64)\n"
	        "  --audio-auto=0|1       raise the sound delay when the sound keeps running dry (1)\n"
	        "  --ff-speed=N|max       fast-forward multiplier (3)\n"
	        "  --save-dir=DIR         battery saves (next to the ROM)\n"
	        "  --state-dir=DIR        save states and thumbnails (<data folder>/states)\n"
	        "  --data-dir=DIR         GBA DS's data folder: states, cheats, overlays, logos...\n"
	        "                         (the app's folder; best set in data-folder.cfg there)\n"
	        "\n"
	        "Testing: --load-state=FILE --quit-after=TICKS --input-script=FILE\n"
	        "  script lines: 'TICK TOKEN' with TOKEN a button combo held until the next\n"
	        "  line (A+RIGHT, -), HOME, TAP:x,y (0..1 window coords), SHOT:file.bmp, QUIT\n",
	        argv0);
}

/* ---------------------------------------------------------------- settings table */

#define OFFB(f) offsetof(Config, f), sizeof(((Config*) 0)->f)
#define ARR(a) a, (int) (sizeof(a) / sizeof(a[0]))
static const SettingChoice chTheme[] = { { "dark", "Dark", 0 }, { "light", "Light", 1 } };
static const SettingChoice chAccent[] = {
	{ "purple", "Purple", 0 }, { "blue", "Blue", 0 }, { "teal", "Teal", 0 }, { "green", "Green", 0 }, { "red", "Red", 0 }, { "orange", "Orange", 0 }, { "pink", "Pink", 0 },
};
static const SettingChoice chOnOff[] = { { "0", "Off", 0 }, { "1", "On", 1 } };
static const SettingChoice chShown[] = { { "0", "Supported games only", 0 }, { "1", "All games", 1 } };
static const SettingChoice chTags[] = { { "0", "Plain", 0 }, { "1", "With tags, like (USA)", 1 } };
static const SettingChoice chLogos[] = { { "pixelated", "Pixelated", CL_PIXELATED }, { "full", "Full detail", CL_FULL }, { N_("off"), "Not used", CL_OFF } };
static const SettingChoice chScale[] = { { "integer", "Pixel Perfect", 0 }, { "fit", "Full Width", 0 }, { "zoom", "Zoom", 0 }, { "stretch", "Stretch", 0 } };
static const SettingChoice chGbSize[] = { { "gba", "GBA frame", 0 }, { "native", "Game Boy screen", 0 } };
static const SettingChoice chGbScale[] = { { "integer", "Pixel Perfect", 0 }, { "fit", "Fit", 0 }, { "stretch", "Stretch", 0 } };
static const SettingChoice chShader[] = {
	{ N_("off"), "Off", SH_OFF }, { "grid", "LCD Grid", SH_GRID }, { "scanlines", "Scanlines", SH_SCANLINES }, { "soft", "Soft Cells", SH_SOFT },
	{ "subpixels-light", "Subpixels (Light)", SH_SUB_LIGHT }, { "subpixels", "Subpixels", SH_SUB }, { "subpixels-strong", "Subpixels (Strong)", SH_SUB_STRONG },
};
static const SettingChoice chSwap[] = { { "0", "Game on top", 0 }, { "1", "Game below", 1 } };
static const SettingChoice chMatch[] = { { "keep", "Keep device shapes", 0 }, { "aspect", "Match aspect ratios", 0 }, { "size", "Match sizes and shapes", 0 } };
static const SettingChoice chToggleBtn[] = { { "left", "Left", 0 }, { "center", "Center", 0 }, { "right", "Right", 0 }, { "off", "Off", 0 } };
static const SettingChoice chPlace[] = { { "center", "Centered", 0 }, { "no_gap", "Closest to gap", 0 } };
static const SettingChoice chOverlays[] = { { N_("off"), "Off", OVM_OFF }, { "generated", "Generated", OVM_GENERATED }, { "custom", "Custom", OVM_CUSTOM } };
static const SettingChoice chOvlScreens[] = { { "both", "Both screens", OVL_BOTH }, { "top", "Top screen", OVL_TOP }, { "bottom", "Bottom screen", OVL_BOTTOM } };
static const SettingChoice chAB[] = { { "0", "Like a GBA", 0 }, { "1", "Swapped", 1 } };
static const SettingChoice chFace[] = {
	{ "a", "GBA A", 0 }, { "b", "GBA B", 0 }, { "l", "GBA L", 0 }, { "r", "GBA R", 0 },
	{ "up", "GBA Up", 0 }, { "down", "GBA Down", 0 }, { "left", "GBA Left", 0 }, { "right", "GBA Right", 0 }, { "start", "Start", 0 },
	{ "select", "Select", 0 }, { "turbo_a", "Turbo A", 0 }, { "turbo_b", "Turbo B", 0 }, { "none", "Nothing", 0 },
};
static const SettingChoice chTurbo[] = { { "10", "Slow (10 a second)", 10 }, { "15", "Medium (15 a second)", 15 }, { "20", "Fast (20 a second)", 20 } };
static const SettingChoice chFF[] = { { N_("off"), "Off", 0 }, { "2", "2x", 2 }, { "3", "3x", 3 }, { "4", "4x", 4 }, { "5", "5x", 5 }, { "max", "As fast as it goes", 0 } };
static const SettingChoice chRewind[] = { { "10", "10 s", 10 }, { "15", "15 s", 15 }, { "30", "30 s", 30 }, { "60", "1 minute", 60 }, { "120", "2 minutes", 120 }, { "300", "5 minutes", 300 } };
static const SettingChoice chLatency[] = { { "32", "32 ms", 32 }, { "64", "64 ms", 64 }, { "96", "96 ms", 96 }, { "128", "128 ms", 128 }, { "192", "192 ms", 192 } };
static const SettingChoice chPerf[] = { { "system", "Let the System Decide", 0 }, { "max", "Set to Max", 1 } };
/* Automatic, English, then every catalogue by its own name (i18n_data.h: made from assets/i18n/*.po) */
static const SettingChoice chLanguage[] = { { "auto", "Automatic", 0 }, { "en", "English", 0 }, I18N_CHOICES };
static const SettingChoice chInterval[] = { { "30", "Every 0.5 s", 30 }, { "60", "Every second", 60 }, { "90", "Every 1.5 s", 90 }, { "180", "Every 3 s", 180 } };

static const Setting settingsTable[] = {
	{ "language", "Language", SF_STR, OFFB(language), ARR(chLanguage), 0, 0, 0, NULL, "Automatic follows the system. Text that isn't translated yet stays in English." },
	{ "show_all", "Games shown", SF_BOOL, OFFB(showAll), ARR(chShown), 0, 0, 0, NULL, "Games without second-screen support play on one screen." },
	{ "name_tags", "Game names", SF_BOOL, OFFB(nameTags), ARR(chTags), 0, 0, 0, NULL, "Tags in a ROM's file name, like (USA) or (En,Fr,De)." },
	{ "save_on_quit", "Save on quit", SF_BOOL, OFFB(autoSave), ARR(chOnOff), 0, 0, 0, NULL, "Closing a game saves where you are." },
	{ "auto_load", "Auto-load", SF_BOOL, OFFB(autoLoad), ARR(chOnOff), 0, 0, 0, NULL, "Starting a game picks up where you closed it." },
	{ "custom_logos", "Custom logos", SF_INT, OFFB(customLogos), ARR(chLogos), 0, 0, 0, NULL, "Your own and scraped logos on the second screen." },
	{ "quick_bar_hint", "Quick bar hint", SF_BOOL, OFFB(barHint), ARR(chOnOff), 0, 0, 0, NULL, "\"Swipe up for quick menu\" at a game's start." },

	{ "scale", "Game screen scale", SF_STR, OFFB(scale), ARR(chScale), 0, 0, 0, NULL, "How the game's picture fills its screen." },
	{ "map_scale", "Second screen scale", SF_STR, OFFB(mapScale), ARR(chScale), 0, 0, 0, NULL, "How the map or logo fills its screen." },
	{ "gb_size", "Game Boy size", SF_STR, OFFB(gbSize), ARR(chGbSize), 0, 0, 0, NULL, "Game Boy games: in a GBA's frame, or the Game Boy's own screen." },
	{ "gb_scale", "Game Boy scale", SF_STR, OFFB(gbScale), ARR(chGbScale), 0, 0, 0, NULL, "For the Game Boy screen: how its picture fills the screen." },
	{ "shader", "Shader", SF_INT, OFFB(shader), ARR(chShader), 0, 0, 0, NULL, "A pattern over every pixel, on both screens." },
	{ "lcd_strength", "Pattern strength", SF_INT, OFFB(lcdStrength), NULL, 0, 0, 100, 5, "%", "How dark the grid, scanlines and soft cells are." },
	{ "color_correction", "Color correction", SF_BOOL, OFFB(colorCorrection), ARR(chOnOff), 0, 0, 0, NULL, "Colors as a Game Boy Advance's screen showed them." },
	{ "lcd_ghosting", "LCD ghosting", SF_BOOL, OFFB(ghosting), ARR(chOnOff), 0, 0, 0, NULL, "Moving things leave a short trail, as on a GBA." },
	{ "smooth", "Smooth scaling", SF_BOOL, OFFB(smooth), ARR(chOnOff), 0, 0, 0, NULL, "Blurry instead of sharp: usually leave off." },

	{ "swap", "Screens", SF_BOOL, OFFB(swap), ARR(chSwap), 0, 0, 0, NULL, "Which screen the game is on." },
	{ "screen_match", "Screen shapes", SF_STR, OFFB(screenMatch), ARR(chMatch), 0, 0, 0, NULL, "Only when your two screens differ in shape. Experimental." },
	{ "screen_placement", "Screen placement", SF_STR, OFFB(screenPlace), ARR(chPlace), 0, 0, 0, NULL, "Pictures in the middle of their screens, or close together." },
	{ "overlays", "Overlays", SF_INT, OFFB(overlayMode), ARR(chOverlays), 0, 0, 0, NULL, "GBA DS's bezel, or your own PNGs, round the picture." },
	{ "overlay_screens", "Overlays on", SF_INT, OFFB(overlays), ARR(chOvlScreens), 0, 0, 0, NULL, "Which screens have them." },
	{ "bezel_logo", "Bezel logo", SF_BOOL, OFFB(bezelLogo), ARR(chOnOff), 0, 0, 0, NULL, "The GBA DS logo in the generated bezel." },
	{ "show_fps", "Show FPS", SF_BOOL, OFFB(showFps), ARR(chOnOff), 0, 0, 0, NULL, "Frames a second above the game, all the time." },

	{ "theme", "Light/dark mode", SF_BOOL, OFFB(lightTheme), ARR(chTheme), 0, 0, 0, NULL, "The menus' buttons: dark grey, or light grey like the web player's." },
	{ "accent", "Color", SF_STR, OFFB(accent), ARR(chAccent), 0, 0, 0, NULL, "The menus' color for what's selected, with a dark and a light version of each." },
	{ "brightness_top", "Top screen brightness", SF_INT, OFFB(brightTop), NULL, 0, 20, 100, 10, "%", "Darkens the top screen, in this app only: the device's own brightness is left alone (and this can't go above it)." },
	{ "brightness_bottom", "Bottom screen brightness", SF_INT, OFFB(brightBottom), NULL, 0, 20, 100, 10, "%", "Darkens the bottom screen, in this app only: the device's own brightness is left alone (and this can't go above it)." },
	{ "left_stick_dpad", "Left stick as D-pad", SF_BOOL, OFFB(leftStickDpad), ARR(chOnOff), 0, 0, 0, NULL, "Off: the left stick does nothing in games." },
	{ "ab_swap", "A and B buttons", SF_BOOL, OFFB(abSwap), ARR(chAB), 0, 0, 0, NULL, "GBA A on the right button, or on the bottom one." },
	{ "button_right", "A (right) button", SF_STR, OFFB(faceBtn[0]), ARR(chFace), 0, 0, 0, NULL, "What the right face button gives the game." },
	{ "button_bottom", "B (bottom) button", SF_STR, OFFB(faceBtn[1]), ARR(chFace), 0, 0, 0, NULL, "What the bottom face button gives the game." },
	{ "button_top", "X (top) button", SF_STR, OFFB(faceBtn[2]), ARR(chFace), 0, 0, 0, NULL, "What the top face button gives the game." },
	{ "button_left", "Y (left) button", SF_STR, OFFB(faceBtn[3]), ARR(chFace), 0, 0, 0, NULL, "What the left face button gives the game." },
	{ "button_l", "L button", SF_STR, OFFB(faceBtn[4]), ARR(chFace), 0, 0, 0, NULL, "What the L shoulder button gives the game." },
	{ "button_r", "R button", SF_STR, OFFB(faceBtn[5]), ARR(chFace), 0, 0, 0, NULL, "What the R shoulder button gives the game." },
	{ "button_dpad_up", "D-pad up", SF_STR, OFFB(faceBtn[6]), ARR(chFace), 0, 0, 0, NULL, "What D-pad up gives the game." },
	{ "button_dpad_down", "D-pad down", SF_STR, OFFB(faceBtn[7]), ARR(chFace), 0, 0, 0, NULL, "What D-pad down gives the game." },
	{ "button_dpad_left", "D-pad left", SF_STR, OFFB(faceBtn[8]), ARR(chFace), 0, 0, 0, NULL, "What D-pad left gives the game." },
	{ "button_dpad_right", "D-pad right", SF_STR, OFFB(faceBtn[9]), ARR(chFace), 0, 0, 0, NULL, "What D-pad right gives the game." },
	{ "turbo_rate", "Turbo speed", SF_INT, OFFB(turboRate), ARR(chTurbo), 0, 0, 0, NULL, "How fast Turbo A and Turbo B press." },
	{ "rumble_strength", "Rumble strength", SF_INT, OFFB(rumbleStrength), NULL, 0, 0, 100, 10, "%", "How hard the device's motor runs in games that rumble. 0% turns it off." },

	{ "match_display", "Match display rate", SF_BOOL, OFFB(matchDisplay), ARR(chOnOff), 0, 0, 0, NULL, "Run at the display's rate: no small regular stutter." },
	{ "frame_buffer", "Frame buffer", SF_BOOL, OFFB(frameBuffer), ARR(chOnOff), 0, 0, 0, NULL, "Smoother, one frame more input delay." },
	{ "rewind", "Rewind", SF_BOOL, OFFB(rewind), ARR(chOnOff), 0, 0, 0, NULL, "Hold L2 to go back in time." },
	{ "rewind_seconds", "Rewind length", SF_INT, OFFB(rewindSeconds), ARR(chRewind), 0, 0, 0, N_(" s"), "How far back it goes." },
	{ "fast_forward", "Fast forward", SF_FF, OFFB(fastForward), ARR(chFF), 0, 0, 0, NULL, "Hold R2 to play faster." },
	{ "audio_latency", "Sound delay", SF_INT, OFFB(audioLatency), ARR(chLatency), 0, 0, 0, N_(" ms"), "Raise it if the sound crackles." },
	{ "performance", "Performance", SF_BOOL, OFFB(perfMax), ARR(chPerf), 0, 0, 0, NULL, "Max: full speed, a warmer device and a shorter battery." },

	{ "map", "Second screen", SF_BOOL, OFFB(map), ARR(chOnOff), 0, 0, 0, NULL, "Off: every game plays like an ordinary GBA game." },
	{ "map_interval", "Map refresh", SF_INT, OFFB(mapInterval), ARR(chInterval), 0, 0, 0, NULL, "Shorter can make the sound crackle on a slow device." },
	{ "toggle", "Contextual toggle", SF_BOOL, OFFB(toggle), ARR(chOnOff), 0, 0, 0, NULL, "Turn the second screen's pages or view by hand: HOME + A, or its button." },
	{ "auto_rotate", "Auto-rotate", SF_BOOL, OFFB(autoRotate), ARR(chOnOff), 0, 0, 0, NULL, "Timed pages keep turning by themselves. Off: a page stays until you press." },
	{ "toggle_button", "Toggle button", SF_STR, OFFB(toggleButton), ARR(chToggleBtn), 0, 0, 0, NULL, "Where it sits on the second screen, in games with something to turn." },
	{ "pinball_live", "Pinball two-screen view", SF_BOOL, OFFB(tallLive), ARR(chOnOff), 0, 0, 0, NULL, "Off for slower devices: the table on one screen. Close and restart GBA DS to apply." },
};

const Setting* config_setting(const char* key) {
	for (size_t i = 0; i < sizeof(settingsTable) / sizeof(settingsTable[0]); ++i)
		if (!strcmp(settingsTable[i].key, key)) return &settingsTable[i];
	return NULL;
}

/* what a face button gives the game with nothing chosen: ab_swap's layout */
static const char* face_default(const Config* c, int i) {
	static const char* const own[10] = { "a", "b", "none", "none", "l", "r", "up", "down", "left", "right" };
	return i == 0 ? (c->abSwap ? "b" : "a") : i == 1 ? (c->abSwap ? "a" : "b") : own[i];
}

int config_setting_index(const Config* c, const Setting* s) {
	const char* p = (const char*) c + s->off;
	if (s->field == SF_FF) {
		if (!c->fastForward) return 0;
		return c->ffSpeed <= 0 ? 5 : c->ffSpeed < 2 ? 1 : c->ffSpeed > 5 ? 5 : c->ffSpeed - 1;
	}
	if (s->field == SF_STR) {
		const char* v = p;
		if (!strncmp(s->key, "button_", 7) && !*v) {
			v = face_default(c, (int) ((s->off - offsetof(Config, faceBtn)) / sizeof(c->faceBtn[0])));
		}
		for (int i = 0; i < s->nChoices; ++i)
			if (!strcasecmp(v, s->choices[i].value)) return i;
		return -1;
	}
	int v = s->field == SF_BOOL ? (int) *(const bool*) p : *(const int*) p;
	for (int i = 0; i < s->nChoices; ++i)
		if (s->choices[i].ival == v) return i;
	return -1;
}

void config_setting_text(const Config* c, const Setting* s, char* out, size_t n) {
	int i = config_setting_index(c, s);
	if (i >= 0) {
		const char* label = s->choices[i].label;
		/* (a machine-translated language's name ends with " (machine translated)", tools/i18n.py: that
		 * note is said in the language in use) */
		static const char note[] = " (machine translated)";
		size_t len = strlen(label), nl = sizeof(note) - 1;
		if (len > nl && !strcmp(label + len - nl, note))
			snprintf(out, n, "%.*s (%s)", (int) (len - nl), label, tr("machine translated"));
		else
			snprintf(out, n, "%s", label);
	} else if (s->field == SF_INT) {
		snprintf(out, n, "%d%s", *(const int*) ((const char*) c + s->off), s->unit ? s->unit : "");
	} else {
		snprintf(out, n, "?");
	}
}

static void setting_set_index(Config* c, const Setting* s, int i) {
	char* p = (char*) c + s->off;
	const SettingChoice* ch = &s->choices[i];
	if (s->field == SF_BOOL) *(bool*) p = ch->ival != 0;
	else if (s->field == SF_INT) *(int*) p = ch->ival;
	else if (s->field == SF_STR) snprintf(p, s->size, "%s", ch->value);
	else if (s->field == SF_FF) {
		c->fastForward = i != 0;
		if (i) c->ffSpeed = i == 5 ? 0 : i + 1;
	}
}

bool config_setting_step(Config* c, const Setting* s, int dir) {
	if (!s->nChoices) {
		int* v = (int*) ((char*) c + s->off);
		int to = *v + dir * s->step;
		if (to < s->min) to = s->max;
		if (to > s->max) to = s->min;
		if (to == *v) return false;
		*v = to;
		return true;
	}
	int i = config_setting_index(c, s), to;
	if (i < 0 && s->field == SF_INT) {
		/* a number gba-ds.cfg gave that isn't on the list: the next one up or down */
		int v = *(const int*) ((const char*) c + s->off);
		to = dir > 0 ? 0 : s->nChoices - 1;
		for (int k = 0; k < s->nChoices; ++k) {
			if (dir > 0 && s->choices[k].ival > v) { to = k; break; }
			if (dir < 0 && s->choices[s->nChoices - 1 - k].ival < v) { to = s->nChoices - 1 - k; break; }
		}
	} else {
		to = i < 0 ? 0 : (i + dir + s->nChoices) % s->nChoices;
	}
	if (to == i) return false;
	setting_set_index(c, s, to);
	return true;
}

int config_setting_lines(const Config* c, const Setting* s, char lines[2][2][24]) {
	snprintf(lines[0][0], 24, "%s", s->key);
	if (s->field == SF_FF) {
		int i = config_setting_index(c, s);
		snprintf(lines[0][1], 24, "%d", i != 0);
		snprintf(lines[1][0], 24, "ff_speed");
		if (c->ffSpeed <= 0) snprintf(lines[1][1], 24, "max");
		else snprintf(lines[1][1], 24, "%d", c->ffSpeed);
		return 2;
	}
	int i = config_setting_index(c, s);
	if (i >= 0) snprintf(lines[0][1], 24, "%s", s->choices[i].value);
	else snprintf(lines[0][1], 24, "%d", *(const int*) ((const char*) c + s->off));
	return 1;
}

void config_kv_update(const char* path, const char* header, int n, const char* const kv[][2]) {
	/* the file's other lines, as they are */
	char* kept = calloc(1, 1);
	size_t keptLen = 0;
	bool any = false;
	FILE* f = fopen(path, "r");
	if (f) {
		char line[1100];
		while (fgets(line, sizeof(line), f)) {
			char key[64] = "";
			const char* q = line;
			while (isspace((unsigned char) *q)) ++q;
			if (*q && *q != '#' && *q != ';') {
				size_t k = strcspn(q, "= \t\r\n");
				snprintf(key, sizeof(key), "%.*s", (int) (k < 63 ? k : 63), q);
			}
			bool replaced = false;
			for (int i = 0; i < n && key[0]; ++i)
				if (!strcmp(key, kv[i][0])) replaced = true;
			if (replaced) continue;
			size_t l = strlen(line);
			kept = realloc(kept, keptLen + l + 2);
			memcpy(kept + keptLen, line, l);
			keptLen += l;
			if (!l || line[l - 1] != '\n') kept[keptLen++] = '\n';
			kept[keptLen] = 0;
			any = true;
		}
		fclose(f);
	}
	f = fopen(path, "w");
	if (f) {
		if (!any && header) fprintf(f, "%s\n", header);
		fputs(kept, f);
		for (int i = 0; i < n; ++i) fprintf(f, "%s = %s\n", kv[i][0], kv[i][1]);
		fclose(f);
	}
	free(kept);
}

int config_setting_count(void) {
	return (int) (sizeof(settingsTable) / sizeof(settingsTable[0]));
}

const Setting* config_setting_at(int i) {
	return i >= 0 && i < config_setting_count() ? &settingsTable[i] : NULL;
}

void config_setting_copy(Config* dst, const Config* src, const Setting* s) {
	if (s->field == SF_FF) {
		dst->fastForward = src->fastForward;
		dst->ffSpeed = src->ffSpeed;
		return;
	}
	memcpy((char*) dst + s->off, (const char*) src + s->off, s->size);
}

int config_kv_keys(const char* path, int max, char keys[][40]) {
	int n = 0;
	FILE* f = fopen(path, "r");
	if (!f) return 0;
	char line[1100];
	while (n < max && fgets(line, sizeof(line), f)) {
		const char* q = line;
		while (isspace((unsigned char) *q)) ++q;
		if (!*q || *q == '#' || *q == ';') continue;
		size_t k = strcspn(q, "= \t\r\n");
		snprintf(keys[n++], 40, "%.*s", (int) (k < 39 ? k : 39), q);
	}
	fclose(f);
	return n;
}

void config_kv_remove(const char* path, int n, const char* const keys[]) {
	char kept[8192] = "";
	size_t len = 0;
	bool real = false;
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[1100];
	while (fgets(line, sizeof(line), f)) {
		const char* q = line;
		while (isspace((unsigned char) *q)) ++q;
		bool drop = false;
		if (*q && *q != '#' && *q != ';') {
			size_t k = strcspn(q, "= \t\r\n");
			for (int i = 0; i < n; ++i)
				if (strlen(keys[i]) == k && !strncmp(q, keys[i], k)) drop = true;
			if (!drop) real = true;
		}
		if (drop) continue;
		size_t l = strlen(line);
		if (len + l < sizeof(kept)) memcpy(kept + len, line, l), len += l, kept[len] = 0;
	}
	fclose(f);
	if (!real) {
		remove(path); /* nothing of the game's own left */
		return;
	}
	f = fopen(path, "w");
	if (f) {
		fputs(kept, f);
		fclose(f);
	}
}

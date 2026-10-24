#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void config_defaults(Config* c) {
	memset(c, 0, sizeof(*c));
	strcpy(c->layout, "auto");
	strcpy(c->scale, "integer");
	strcpy(c->mapScale, "integer");
	strcpy(c->hotkey, "guide");
	strcpy(c->menuCombo, "none");
	strcpy(c->romDir, "/storage/roms/gba");
	strcpy(c->screenshotDir, "/storage/roms/screenshots");
	strcpy(c->cheatDir, "/config/retroarch/cheats:/storage/.config/retroarch/cheats");
	strcpy(c->esConfigDir, "/storage/.config/emulationstation");
	strcpy(c->libraryView, "list");
	strcpy(c->touchScreen, "second");
	strcpy(c->touchMap, "window");
	c->touchRotate = -1;
	c->map = true;
	c->mapCrop = true;
	c->mapAnim = true;
	c->showLogo = true;
	c->rewind = true;
	c->rewindSeconds = 30;
	c->overlays = OVL_BOTH;
	c->mapInterval = 60;
	c->sway = true;
	c->audioLatency = 64;
	c->ffSpeed = 3;
	c->pinCores = false; /* tried on the RG DS Plus: no better (docs/ANDROID.md) */
	c->autoResume = true;
	c->showAll = false;
	c->nameTags = false;
	strcpy(c->raMode, "auto");
	strcpy(c->raSystemCfg, "/storage/.config/system/configs/system.cfg");
	c->homeButton = -1;
	c->lcdStrength = 35;
	strcpy(c->borderColor, "000000");
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
	BOOL("smooth", smooth)
	BOOL("lcd_grid", lcdGrid)
	if (!strcmp(key, "overlays")) {
		/* none / primary / secondary / both (older configs: 0 / 1, top / bottom) */
		c->overlays = !strcmp(val, "primary") || !strcmp(val, "top")      ? OVL_PRIMARY :
		              !strcmp(val, "secondary") || !strcmp(val, "bottom") ? OVL_SECONDARY :
		              !strcmp(val, "both")   ? OVL_BOTH :
		              !strcmp(val, "none")   ? 0 :
		              parse_bool(val)        ? OVL_BOTH : 0;
		return true;
	}
	STR("border_color", borderColor)
	INT("lcd_strength", lcdStrength)
	BOOL("vsync", vsync)
	BOOL("pin_cores", pinCores)
	BOOL("frame_buffer", frameBuffer)
	BOOL("map", map)
	BOOL("map_crop", mapCrop)
	BOOL("map_animations", mapAnim)
	BOOL("show_logo", showLogo)
	BOOL("match_display", matchDisplay)
	BOOL("rewind", rewind)
	INT("rewind_seconds", rewindSeconds)
	INT("map_interval", mapInterval)
	BOOL("ab_swap", abSwap)
	STR("hotkey", hotkey)
	STR("menu_combo", menuCombo)
	INT("home_button", homeButton)
	BOOL("show_fps", showFps)
	BOOL("sway", sway)
	INT("audio_latency", audioLatency)
	INT("ff_speed", ffSpeed)
	STR("save_dir", saveDir)
	STR("state_dir", stateDir)
	STR("rom_dir", romDir)
	STR("cheat_dir", cheatDir)
	STR("screenshot_dir", screenshotDir)
	STR("asset_dir", assetDir)
	STR("touch_screen", touchScreen)
	STR("touch_map", touchMap)
	STR("touch_device", touchDevice)
	INT("touch_rotate", touchRotate)
	BOOL("auto_resume", autoResume)
	BOOL("show_all", showAll)
	BOOL("name_tags", nameTags)
	STR("retroachievements", raMode)
	STR("ra_username", raUser)
	STR("ra_password", raPassword)
	STR("ra_system_cfg", raSystemCfg)
	STR("ra_host", raHost)
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

void config_load_file(Config* c, const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		return;
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
		if (!set_option(c, key, val)) {
			fprintf(stderr, "%s: unknown option '%s'\n", path, key);
		}
	}
	fclose(f);
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
	        "  --screenshot-dir=DIR   where Take Screenshot saves (/storage/roms/screenshots)\n"
	        "  --layout=auto|span-h|span-v|dual|single   screen arrangement (auto)\n"
	        "  --swap                 show the map on the first screen\n"
	        "  --touch-screen=first|second   which screen has the touch panel (second)\n"
	        "  --touch-map=window|screen     touch coordinates span the window or one screen\n"
	        "  --scale=integer|fit|stretch      game scaling (integer)\n"
	        "  --map-scale=integer|fit|stretch  map scaling (integer)\n"
	        "  --scale=fit            fill the screen width (keeps the aspect ratio)\n"
	        "  --lcd-grid             LCD pixel grid on game and map (--lcd-strength=0..100)\n"
	        "  --smooth               plain linear filtering\n"
	        "  --vsync                sync presentation to the display\n"
	        "  --no-map               disable the map screen\n"
	        "  --map-interval=N       frames between map refreshes (90)\n"
	        "  --ab-swap              GBA A on the bottom face button\n"
	        "  --hotkey=guide|back    menu / hotkey button (guide = HOME)\n"
	        "  --menu-combo=select+start|none   buttons that open the menu together (none)\n"
	        "  --home-button=N        raw joystick button that acts as HOME (see GBADS_DEBUG=1 log)\n"
	        "  --no-auto-resume       don't save on exit / resume on launch\n"
	        "  --show-fps\n"
	        "  --no-sway              don't ask sway to span both outputs\n"
	        "  --audio-latency=MS     (64)\n"
	        "  --ff-speed=N           fast-forward multiplier (3)\n"
	        "  --save-dir=DIR         battery saves (next to the ROM)\n"
	        "  --state-dir=DIR        save states and thumbnails (<app>/states)\n"
	        "\n"
	        "Testing: --load-state=FILE --quit-after=TICKS --input-script=FILE\n"
	        "  script lines: 'TICK TOKEN' with TOKEN a button combo held until the next\n"
	        "  line (A+RIGHT, -), HOME, TAP:x,y (0..1 window coords), SHOT:file.bmp, QUIT\n",
	        argv0);
}

/* GBA DS: a GBA emulator for dual-screen handhelds that keeps the game's own
 * map on the second screen.
 *
 *   library.c  game list (rescans the ROM folder every time it opens)
 *   game.c     play session, pause menu, auto save/resume
 *   mapcap.c   map capture with a shadow emulator core; profiles.c per game
 *   ui.c       windows, screens, touch/buttons, drawing
 */
#include "android.h"
#include "config.h"
#include "esinteg.h"
#include "game.h"
#include "library.h"
#include "logo.h"
#include "paths.h"
#include "ui.h"
#include "ra.h"
#include "update.h"
#ifdef __wasi__
#include "mapcap.h"
#endif
#include "emu.h"
#include "profiles.h"

#include <dirent.h>
#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

/* true if the folder has a .png in it */
static bool dir_has_png(const char* path) {
	DIR* d = opendir(path);
	if (!d) return false;
	bool found = false;
	struct dirent* e;
	while (!found && (e = readdir(d))) {
		size_t n = strlen(e->d_name);
		found = n > 4 && !strcasecmp(e->d_name + n - 4, ".png");
	}
	closedir(d);
	return found;
}

static void on_signal(int sig) {
	(void) sig;
	uiQuit = 1;
}

#if defined(__linux__) && !defined(__ANDROID__) && defined(__GLIBC__)
#include <execinfo.h>
/* A crash: where it was, into the log (for reports from systems we can't
 * run ourselves); then the default action. */
static void on_crash(int sig) {
	char msg[64];
	int n = snprintf(msg, sizeof(msg), "crashed: signal %d; backtrace:\n", sig);
	if (write(2, msg, n) < 0) { /* (nothing to do) */ }
	void* frames[48];
	int k = backtrace(frames, 48);
	backtrace_symbols_fd(frames, k, 2);
	signal(sig, SIG_DFL);
	raise(sig);
}
#endif

int main(int argc, char** argv) {
#ifdef __wasi__
	/* the web player's capture worker: GBADS_CAPTURE_SERVE=CODE, the ROM last,
	 * GBADS_CAPTURE_TIGHT=1 for the tight crop (mapcap.c) */
	if (getenv("GBADS_CAPTURE_SERVE") && argc > 1) {
		const char* t = getenv("GBADS_CAPTURE_TIGHT");
		return mapcap_web_serve(argv[argc - 1], getenv("GBADS_CAPTURE_SERVE"), t && *t == '1');
	}
#endif
	for (int i = 1; i < argc; ++i) {
		if (!strcmp(argv[i], "--version")) {
			printf("GBA DS %s\n", GBADS_VERSION);
			return 0;
		}
		/* --supported ROM: exit 0 if GBA DS has second-screen support for the
		 * game (a profile for its code), 1 if not, 2 if it can't be read
		 * (BananaOS's banana-play.sh: other games go to RetroArch) */
		if (!strcmp(argv[i], "--supported") && i + 1 < argc) {
			uint8_t h[0xC0];
			if (!emu_rom_header(argv[i + 1], h, sizeof(h))) return 2;
			char code[5] = { (char) h[0xAC], (char) h[0xAD], (char) h[0xAE], (char) h[0xAF], 0 };
			bool yes = profile_find(code) != NULL;
			printf("%s: %s\n", code, yes ? "supported" : "not supported");
			return yes ? 0 : 1;
		}
	}
#ifdef __ANDROID__
	/* GBA DS's folder where the player chose it (Settings > Folders), its
	 * files moved there first if it's new: before the log, which goes in it */
	android_data_setup();
	android_init_log();
#endif
	fprintf(stderr, "GBA DS %s\n", GBADS_VERSION);
	Config cfg;
	config_defaults(&cfg);
#ifdef __ANDROID__
	android_config_defaults(&cfg);
#endif

	char exeDir[1024] = ".";
	char exe[1024];
	ssize_t n = readlink("/proc/self/exe", exe, sizeof(exe) - 1);
	if (n > 0) {
		exe[n] = 0;
		snprintf(exeDir, sizeof(exeDir), "%s", dirname(exe));
	}
	/* GBADS_HOME: the app's folder when the executable runs from a copy
	 * elsewhere (launch.sh, where the card won't run programs) */
	if (getenv("GBADS_HOME") && getenv("GBADS_HOME")[0]) snprintf(exeDir, sizeof(exeDir), "%s", getenv("GBADS_HOME"));
#ifdef __ANDROID__
	/* the executable is Android's app_process: use the app's own folder */
	if (android_home()) snprintf(exeDir, sizeof(exeDir), "%s", android_home());
#endif

	/* defaults < gba-ds.cfg < states/prefs.cfg (choices made in the menu),
	 * library.cfg and hotkeys.cfg (the game list's Settings) < command line */
	char path[1100];
	if (getenv("GBADS_CONFIG")) {
		config_load_file(&cfg, getenv("GBADS_CONFIG"));
	} else {
		snprintf(path, sizeof(path), "%s/gba-ds.cfg", exeDir);
		config_load_file(&cfg, path);
		if (getenv("HOME")) {
			snprintf(path, sizeof(path), "%s/.config/gba-ds/gba-ds.cfg", getenv("HOME"));
			config_load_file(&cfg, path);
		}
#if !defined(__ANDROID__) && !defined(__wasi__)
		/* data-folder.cfg: data_dir, where GBA DS keeps its own files (in a
		 * file of its own, which neither the zip nor an update has) */
		snprintf(path, sizeof(path), "%s/data-folder.cfg", exeDir);
		config_load_file(&cfg, path);
#endif
	}
	if (!config_parse_args(&cfg, argc, argv)) {
		config_usage(argv[0]);
		return 1;
	}
	/* GBA DS's data folder (states/, cheats/, overlays/, logos/...): the
	 * app's folder, or data_dir, moved there when that changes (paths.c) */
	char dataDir[480];
	snprintf(dataDir, sizeof(dataDir), "%s", exeDir);
#if !defined(__ANDROID__) && !defined(__wasi__)
	snprintf(path, sizeof(path), "%s/data-folder.cfg", exeDir);
	data_dir_setup(exeDir, getenv("GBADS_CONFIG") ? NULL : path, cfg.dataDir, false, dataDir, sizeof(dataDir));
#endif
	if (strcmp(dataDir, exeDir) || data_dir_note()[0])
		fprintf(stderr, "data folder: %s%s%s\n", dataDir, data_dir_note()[0] ? " - " : "", data_dir_note());
	if (!cfg.assetDir[0]) snprintf(cfg.assetDir, sizeof(cfg.assetDir), "%s", dataDir);
	if (!cfg.patchDir[0]) snprintf(cfg.patchDir, sizeof(cfg.patchDir), "%s/patches", cfg.assetDir); /* GEN-9 */
	if (!cfg.stateDir[0]) snprintf(cfg.stateDir, sizeof(cfg.stateDir), "%s/states", dataDir);
	mkdirs(cfg.stateDir);
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg.stateDir);
	{
		/* map_animations, map_interval, show_fps and show_logo are no longer in the pause menu:
		 * gba-ds.cfg decides, whatever an older prefs.cfg says */
		bool mapAnim = cfg.mapAnim;
		int mapInterval = cfg.mapInterval;
		bool showFps = cfg.showFps; /* (and show_fps, since v1.4.1: the quick bar shows the FPS) */
		bool showLogo = cfg.showLogo; /* (and show_logo, since v1.4.1) */
		config_load_file(&cfg, path);
		cfg.mapAnim = mapAnim;
		cfg.mapInterval = mapInterval;
		cfg.showFps = showFps;
		cfg.showLogo = showLogo;
	}
	/* the game list's Settings (Android: the ROM folder) */
	snprintf(path, sizeof(path), "%s/library.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	/* Settings > Hotkeys */
	snprintf(path, sizeof(path), "%s/hotkeys.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	config_parse_args(&cfg, argc, argv);
	if (cfg.overlayMode == OVM_LEGACY) {
		/* overlays = top / both... from before v1.5 (the player's PNGs, which
		 * had no bezel to fall back on): Custom if overlays/ has any, else
		 * the generated bezel */
		snprintf(path, sizeof(path), "%s/overlays", cfg.assetDir);
		cfg.overlayMode = dir_has_png(path) ? OVM_CUSTOM : OVM_GENERATED;
	}

#ifndef __ANDROID__
	/* keep EmulationStation's entry for GBA DS current, if it's switched on */
	es_integration_refresh(&cfg);
	banana_integration_refresh();
#endif

#if defined(__linux__) && !defined(__ANDROID__) && defined(__GLIBC__)
	signal(SIGSEGV, on_crash);
	signal(SIGBUS, on_crash);
	signal(SIGILL, on_crash);
	signal(SIGFPE, on_crash);
	signal(SIGABRT, on_crash);
#endif
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	signal(SIGHUP, on_signal);

	if (!ui_init(&cfg, true)) return 1;
	ra_init(&cfg); /* signs in to RetroAchievements in the background */
	update_init(&cfg, exeDir, argc, argv);
#ifdef __ANDROID__
	fprintf(stderr, "startup: screens ready\n"); /* logcat's timestamps time the start-up steps */
#endif
	if (cfg.inputScript[0]) ui_load_script(cfg.inputScript);

	if (cfg.rom[0]) {
		while (game_run(&cfg, cfg.rom, false) == GAME_RELOAD && !uiQuit) fprintf(stderr, "restarting %s\n", cfg.rom);
	} else {
		char rom[1024];
		while (!uiQuit && library_run(&cfg, rom, sizeof(rom))) {
			fprintf(stderr, "starting %s\n", rom);
			enum GameExit x;
			while ((x = game_run(&cfg, rom, true)) == GAME_RELOAD && !uiQuit) fprintf(stderr, "restarting %s\n", rom);
			if (x == GAME_QUIT_APP) break;
		}
	}
	logo_shutdown();
	ra_shutdown();
	ui_shutdown();
	update_restart_if_installed(); /* an update was installed: start it */
	return 0;
}

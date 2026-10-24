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
#include "emu.h"
#include "profiles.h"

#include <libgen.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

	/* defaults < gba-ds.cfg < states/prefs.cfg (choices made in the menu) < command line */
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
	}
	if (!config_parse_args(&cfg, argc, argv)) {
		config_usage(argv[0]);
		return 1;
	}
	if (!cfg.assetDir[0]) snprintf(cfg.assetDir, sizeof(cfg.assetDir), "%s", exeDir);
	if (!cfg.stateDir[0]) snprintf(cfg.stateDir, sizeof(cfg.stateDir), "%s/states", exeDir);
	mkdirs(cfg.stateDir);
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg.stateDir);
	{
		/* map_animations and map_interval are no longer in the pause menu:
		 * gba-ds.cfg decides, whatever an older prefs.cfg says */
		bool mapAnim = cfg.mapAnim;
		int mapInterval = cfg.mapInterval;
		config_load_file(&cfg, path);
		cfg.mapAnim = mapAnim;
		cfg.mapInterval = mapInterval;
	}
	/* the game list's Settings (Android: the ROM folder) */
	snprintf(path, sizeof(path), "%s/library.cfg", cfg.stateDir);
	config_load_file(&cfg, path);
	config_parse_args(&cfg, argc, argv);

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
#ifdef __ANDROID__
	fprintf(stderr, "startup: screens ready\n"); /* logcat's timestamps time the start-up steps */
#endif
	if (cfg.inputScript[0]) ui_load_script(cfg.inputScript);

	if (cfg.rom[0]) {
		game_run(&cfg, cfg.rom, false);
	} else {
		char rom[1024];
		while (!uiQuit && library_run(&cfg, rom, sizeof(rom))) {
			fprintf(stderr, "starting %s\n", rom);
			if (game_run(&cfg, rom, true) == GAME_QUIT_APP) break;
		}
	}
	logo_shutdown();
	ra_shutdown();
	ui_shutdown();
	return 0;
}

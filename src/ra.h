/* RetroAchievements, casual (softcore) mode, through rcheevos' rc_client.
 *
 * The account is ROCKNIX's (EmulationStation's RetroAchievements settings,
 * global.retroachievements.* in system.cfg) unless gba-ds.cfg names one; on
 * Android, the one signed in in the game list's Settings (ra_sign_in), kept
 * as a token in <state_dir>/ra/login.cfg. Server requests run in a
 * background thread (the system's curl; Android: Java's HttpURLConnection);
 * their results are handed to rc_client on the main thread. Unlocks and the
 * game's progress show as popups on the second screen.
 *
 * Built where GBADS_RA is defined; elsewhere these are no-ops. */
#ifndef GBADS_RA_H
#define GBADS_RA_H

#include "config.h"
#include "emu.h"

#include <SDL.h>

#include <stdbool.h>

/* ra_account's answer */
enum { RA_ACCOUNT_NONE, RA_ACCOUNT_BUSY, RA_ACCOUNT_OK, RA_ACCOUNT_FAILED, RA_ACCOUNT_OFF };

#ifdef GBADS_RA
/* At start-up: reads the account and starts signing in. */
void ra_init(const Config* cfg);
void ra_shutdown(void);
/* A game started / ended (the core running it stays valid in between). */
void ra_game_start(Emu* e, const char* romPath);
/* A game played with a patch (GEN-9): no achievements (RetroAchievements
 * doesn't allow patched games), the banner says so instead */
void ra_game_patched(void);
void ra_game_end(void);
/* After each emulated frame. */
void ra_frame(void);
/* Each loop iteration without emulation (paused, menus). */
void ra_idle(void);
/* The game was restarted. */
void ra_reset(void);
/* Save states: the achievement progress goes in <state>.ra beside them. */
void ra_state_saved(const char* statePath);
void ra_state_loaded(const char* statePath);
/* Popups, at the top of the given screen. */
void ra_draw(int screen);
/* Signed in (or trying): the pause menu offers the achievement list. */
bool ra_enabled(void);
/* The achievement list, drawn in a screen-local area; scrolls in pixels. */
void ra_list_open(void);
void ra_list_close(void);
void ra_list_draw(int screen, SDL_Rect area);
void ra_list_scroll(int screen, int dy);
int ra_list_step(int screen); /* one achievement's height */
int ra_list_scroll_pos(void);
void ra_list_set_scroll(int screen, int y);
/* The Settings screen's sign-in (Android): the account's state, its user
 * name and the last error; signing in with a password (in the background:
 * watch ra_account, and call ra_idle meanwhile); signing out (forgets the
 * token). Both do nothing while a sign-in is under way. */
int ra_account(char* user, size_t userSize, char* msg, size_t msgSize);
void ra_sign_in(const char* user, const char* password);
void ra_sign_out(void);
#else
static inline void ra_init(const Config* cfg) { (void) cfg; }
static inline void ra_shutdown(void) {}
static inline void ra_game_start(Emu* e, const char* romPath) { (void) e, (void) romPath; }
static inline void ra_game_patched(void) {}
static inline void ra_game_end(void) {}
static inline void ra_frame(void) {}
static inline void ra_idle(void) {}
static inline void ra_reset(void) {}
static inline void ra_state_saved(const char* statePath) { (void) statePath; }
static inline void ra_state_loaded(const char* statePath) { (void) statePath; }
static inline void ra_draw(int screen) { (void) screen; }
static inline bool ra_enabled(void) { return false; }
static inline void ra_list_open(void) {}
static inline void ra_list_close(void) {}
static inline void ra_list_draw(int screen, SDL_Rect area) { (void) screen, (void) area; }
static inline void ra_list_scroll(int screen, int dy) { (void) screen, (void) dy; }
static inline int ra_list_step(int screen) { (void) screen; return 1; }
static inline int ra_list_scroll_pos(void) { return 0; }
static inline void ra_list_set_scroll(int screen, int y) { (void) screen, (void) y; }
static inline int ra_account(char* user, size_t userSize, char* msg, size_t msgSize) {
	if (userSize) user[0] = 0;
	if (msgSize) msg[0] = 0;
	return RA_ACCOUNT_OFF;
}
static inline void ra_sign_in(const char* user, const char* password) { (void) user, (void) password; }
static inline void ra_sign_out(void) {}
#endif

#endif

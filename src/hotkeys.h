/* Hotkeys: the hotkey button (HOME, SELECT, START or L3) held with an
 * action's button does the action in a game (game.c). Set in gba-ds.cfg
 * (hotkey, hotkey_quit ...) or with Settings > Hotkeys (hotkeys_run), which
 * keeps its choices in <state_dir>/hotkeys.cfg, read after gba-ds.cfg. */
#ifndef GBADS_HOTKEYS_H
#define GBADS_HOTKEYS_H

#include "config.h"

/* gba-ds.cfg's name for a button (B_*: "a", "start", "l3" ...) */
extern const char* const hotkeyBtnNames[];
/* The hotkey button and each action's button (B_*, or -1 for none), as the
 * game uses them: unknown names are none, HOME and the hotkey button itself
 * are no action's, a button is only the first action's that has it, and
 * Quit on START with START as the hotkey is SELECT. Logs what it drops when
 * log is set. */
void hotkeys_resolve(const Config* cfg, int* mod, int btn[HK_COUNT], bool log);
/* The buttons held to rewind / fast-forward (B_*, or -1 for none): unknown
 * names, HOME and the hotkey button (mod) are none, and a button is only the
 * first one's. A held button may also be an action's: the hotkey button with
 * it does the action, the button alone (or with the hotkey, when no action
 * has it) is held. */
void hotkeys_resolve_holds(const Config* cfg, int mod, int hold[HOLD_COUNT], bool log);
/* Settings > Hotkeys: a screen of its own, until Done. */
void hotkeys_run(Config* cfg);

#endif

/* The Pokemon companion on the second screen (proof of concept): PokeDaisy's
 * companion core (third_party/pokedaisy, GPL-3.0, "Based on PokeDaisy by
 * Lidor Itzhari") reads the running FireRed / LeafGreen / Emerald and draws
 * the party, bag, battle, map, guide, Pokedex and trainer card into a
 * 320x240 canvas, shown enlarged by a whole number; taps on it go to the
 * companion. Built where GBADS_POKEDAISY is defined (Makefile); elsewhere
 * these do nothing. */
#ifndef GBADS_COMPANION_H
#define GBADS_COMPANION_H

#include <stdbool.h>

#include "emu.h"
#include "ui.h"

enum {
	COMP_NONE,
	COMP_SAVE_STATE,
	COMP_LOAD_STATE,
	COMP_LEAVE,
	COMP_TOGGLE_FF,
};

/* After the game is loaded: true if it's a game the companion supports
 * (it's then on until comp_close). settingsPath: where its settings live. */
bool comp_open(Emu* e, const char* settingsPath);
void comp_close(void);
bool comp_active(void);
/* After each emulated frame (reads the game every few frames). */
void comp_frame(Emu* e);
/* Draws it on screen s (the second screen). */
void comp_draw(int s, bool hasState, bool ffOn);
/* A pointer on screen s: true if the companion took it. */
bool comp_pointer(int s, const Pointer* p);
/* What the player asked for from its SETTINGS (once), COMP_NONE otherwise. */
int comp_take_action(void);

#endif

/* The contextual toggle's state machine and second_page(): see toggle.c. */
#ifndef TOGGLE_H
#define TOGGLE_H
#include "config.h"
#include "emu.h"
#include "mapcap.h"
#include "profiles.h"
#include <stdbool.h>
#include <stdint.h>

/* (its state, and what it reads from outside: second.h's sec) */

/* the toggle (state machine: TG_IDLE, TG_FADE_OUT, TG_HOLD, TG_FADE_IN) */
enum { TG_IDLE, TG_FADE_OUT, TG_HOLD, TG_FADE_IN };
int second_page(void);
bool toggle_ready(void);
void toggle_press(void);
void toggle_new_subject(void);
void toggle_step(void);

#endif

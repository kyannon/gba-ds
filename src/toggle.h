/* The contextual toggle's state machine and second_page(): see toggle.c. */
#ifndef TOGGLE_H
#define TOGGLE_H
#include "config.h"
#include "emu.h"
#include "mapcap.h"
#include "profiles.h"
#include <stdbool.h>
#include <stdint.h>

/* game.c's state the toggle reads and writes (to become one struct when the
 * second-screen module's interface is defined) */
extern Emu emu;
extern Config* cfg;
extern bool debug;
extern uint64_t frameCount;
extern MapCapture* mc;
extern MapResult mapRes;
extern int mapShownIdx;
extern bool mapInactive, logoForced, liveOn;
extern bool overOn, overDirty;
extern uint64_t mapAnimStart;
extern int pageOffset;
extern int fadeLevel;
extern bool fadeUpload;
extern bool capSubject;
extern bool viewAlt, pressCap, viewWait;
extern unsigned capDone, tgCapWant;
bool map_anim(void);

/* the toggle (state machine: TG_IDLE, TG_FADE_OUT, TG_HOLD, TG_FADE_IN) */
enum { TG_IDLE, TG_FADE_OUT, TG_HOLD, TG_FADE_IN };
extern int tgState, tgFrames, tgTarget, tgWait;
int second_page(void);
bool tg_view_mode(const MapProfile* hp);
bool toggle_ready(void);
void toggle_apply(int page);
void toggle_press(void);
void toggle_new_subject(void);
void toggle_step(void);

#endif

/* The Pinball copies (profile tall.live): the two extra copies of the game
 * that draw the table's other rows, run from the app's frame by twins_before /
 * twins_second_start / twins_after, and the tall view's composer (update_tall,
 * from second_step). Moved out of game.c unchanged (libretro core, discussion
 * #75); no SDL in here. What only twins.c uses is static there. */
#ifndef TWINS_H
#define TWINS_H
#include "emu.h"
#include "profiles.h"
#include "workers.h"
#include <stdbool.h>
#include <stdint.h>

#define TWINS 2
_Static_assert(TWINS == WORKER_SLOTS, "one worker per copy");
typedef struct {
	Emu e;
	bool need;    /* its picture is on a screen next frame (twins[0]: the lower view) */
	bool up;      /* it ran this frame: pic is this frame's */
	int row;      /* where it's held for this frame's picture: its view's row less the shake */
	int panStep;  /* how far its own pans moved its view in its last first frame */
	int hidden;   /* sprites left out this frame */
	bool havePic;
	int picRow;   /* the table row on pic's first line */
	uint32_t pic[GBA_W * GBA_H];
	/* (profile tall.live.carry) its camera's row, walked to where it's held;
	 * what it keeps of its own from its last first frame (carried): its BG
	 * tiles, vars and queued copies into them */
	int cam;
	bool carried;
	uint64_t seq; /* the run_frame its first frame last ran in */
	uint8_t* ring;
	uint8_t vars[4][32];
	uint32_t q[64][4]; /* (entries of up to 16 bytes) */
	int nq;
	uint16_t oam[512];   /* its sprite table as it last drew, and */
	uint8_t shadow[1024]; /* the game's shadow of it, as the copy left it: its own sprites, for a frame whose logic runs long */
} Twin;


extern Twin twins[TWINS];
extern bool twinOpen;
extern uint64_t twinSeq;
void twin_close(void);
void twin_open(const char* rom, const MapProfile* p);

/* update_tall's inputs (second_step makes them from sec), and its one output:
 * the second screen's picture, handed to show() when it's made (the app's
 * sec.hooks.presentGba) */
typedef struct {
	int nScreens;
	uint64_t frame;               /* sec.frame, for the log */
	bool swap;                    /* the second screen is the top picture */
	uint32_t* hudPx;              /* the HUD split's picture (emu_hud_split) */
	int* mapShownIdx;             /* sec.mapShownIdx: set to -1 / 0 to have the second screen shown again */
	void (*show)(const uint32_t* second);
} TallEnv;

extern bool tallOn, tallLive;
extern const MapProfile* tallProf;
extern uint32_t tallTop[GBA_W * GBA_H], tallBot[GBA_W * GBA_H];
#define TALL_LOG 600
extern char tallLog[TALL_LOG][176];
extern unsigned tallLogN;
void update_tall(const TallEnv* env);

/* the copies' part of the app's frame (game.c run_frame), in its order */
bool twins_before(uint32_t keys);
void twins_second_start(uint32_t keys);
void twins_after(bool live);

#endif

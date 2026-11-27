/* The Pinball copies: see twins.h. */
#include "twins.h"
#include "second.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clamp.h"

/* (only this file's: declared here so their order in the file doesn't matter) */
static bool twinNoObj;
static bool twinRun;
static void* twinStates[2];
static uint32_t twinKeys[2];
static int twinHave, twinCur;
static const MapProfile* twinProf;
static bool tallTrace;
static int twinShakeNow, twinShakeNext;
static bool tallMsg, tallGameTop;
static bool tallTopLogo;
static bool tallFixedBot;
static int tallFixedHold;
static uint32_t lastTop[GBA_W * GBA_H];
static bool haveLastTop;
static bool twin_ball(Emu* e, uint32_t S, int32_t* x, int32_t* y);
static bool tall_carry(const MapProfile* p);
static int tall_rows_of(const MapProfile* p, Emu* e);
static int tall_top_of(const MapProfile* p);
static int tall_view(const MapProfile* p, Emu* e);
static int tall_shake(const MapProfile* p, Emu* e);
static int obj_height(uint16_t a0, uint16_t a1);
static int obj_width(uint16_t a0, uint16_t a1);
static uint16_t tallFixed[128][3];
static int nTallFixed;
static int tallBandY0, tallBandY1;
static uint16_t tallBandFixed[128][3];
static int nTallBandFixed;
static void game_oam_take(void);
static void twin_hide(Twin* tw);
static void twin_first(void* arg);
static uint32_t twinFrameKeys;
static void twin_second(void* arg);
static void twin_step(void);



/* profile tall.live: two more copies of the game drawing the table's rows
 * the game's camera doesn't (update_tall). twins[0] is held at the top of
 * the lower view (row tall.rows - 320), twins[1] 160 rows below the view's
 * top (the game's view, at most rows - 320). Every frame each copy is
 * taken from the game's state from before the frame before (run_frame
 * keeps the last two), held at its row and run that frame with the game's
 * keys, then this frame, whose picture it shows: its logic is always the
 * game's, but its picture is of its own rows, with its own sprites and
 * scroll for them. (A copy left running on its own fell behind the game:
 * the game counts some things only when they're on its own screen, such as
 * the bumper hits that uncover catch mode's portrait.) The copies' camera
 * stores are no-ops: they're moved by setting their camera. The copies run
 * on two worker threads, the second frame while the game runs its own. */

Twin twins[TWINS];
bool twinOpen;
uint64_t twinSeq; /* counts run_frame calls; jumps on a load or rewind (bug_reset) */
static bool twinNoObj; /* (carry) the copies draw no sprites (profile carry.noObj, in the game) */
static bool twinRun;       /* the copies are to run (a table they know, its sweep over, a ball) */
static int twinArmed;      /* frames on a table they know, with no sweep of the table going on */
static void* twinStates[2]; /* the game's state from before each of the last two frames */
static uint32_t twinKeys[2];
static int twinHave, twinCur; /* how many of them there are; the latest */
static const MapProfile* twinProf;
static bool tallTrace; /* GBADS_TALL_TRACE: a line a frame about the live view */
static int twinShakeNow, twinShakeNext; /* the game's screen shake in this frame's picture / the next's */
static bool tallMsg, tallGameTop; /* (for the log: PP-11's screen, the top screen's picture) */
static bool tallTopLogo; /* PP-11's screen up with no table rows above it: the logo on the top screen */
static bool tallFixedBot; /* PP-10: the game's banners on the bottom screen (the ball's there) */
static int tallFixedHold; /* frames the banners stay where they went with none shown */
static uint32_t lastTop[GBA_W * GBA_H]; /* the top screen before PP-11's screen came up */
static bool haveLastTop;

static bool twin_ball(Emu* e, uint32_t S, int32_t* x, int32_t* y) {
	uint32_t b = emu_read32(e, S + twinProf->tall.live.ball);
	if (b < 0x02000000 || b >= 0x02040000) return false;
	*x = (int32_t) emu_read32(e, b + 0x34), *y = (int32_t) emu_read32(e, b + 0x38);
	return true;
}

/* The table's rows: all of them, or (carry) the visible ones of the table
 * up now; the view's top row at its highest; the screen shake */
static bool tall_carry(const MapProfile* p) { return p && p->tall.live.carry.cam; }
static int tall_rows_of(const MapProfile* p, Emu* e) {
	const typeof(p->tall.live.carry)* C = &p->tall.live.carry;
	if (!C->cam || !C->rowsAt) return p->tall.rows;
	int h = emu_read16(e, C->rowsAt) - C->bottomLess;
	return h > 2 * GBA_H + C->top && h < 1024 ? h : 2 * GBA_H + C->top;
}
static int tall_top_of(const MapProfile* p) { return p->tall.live.carry.cam ? p->tall.live.carry.top : 0; }
/* the view's row as the game keeps it (the next frame's BG VOFS; carry:
 * less viewOff, Muppet Pinball Mayhem's camera being 16 rows below it) */
static int tall_view(const MapProfile* p, Emu* e) {
	return (int16_t) emu_read16(e, p->tall.live.view) - p->tall.live.carry.viewOff;
}
static int tall_shake(const MapProfile* p, Emu* e) {
	const typeof(p->tall.live)* L = &p->tall.live;
	if (L->carry.cam) return L->carry.shakeAt ? (int16_t) emu_read16(e, L->carry.shakeAt) : 0;
	uint32_t S = emu_read32(e, L->state);
	return S >= 0x02000000 && S < 0x02040000 ? (int8_t) emu_read8(e, S + L->shake) : 0;
}

/* A copy just taken from the game: its scroll and table tiles set for
 * camera row `row` at once, as if it had scrolled there (profile
 * tall.live.jump). */
static void twin_jump(Emu* e, uint32_t T, int row) {
	const typeof(twinProf->tall.live.jump)* J = &twinProf->tall.live.jump;
	if (!J->ring) return;
	int nt = row >> 3;
	size_t ewSize;
	const uint8_t* ew = emu_memory(e, 2, &ewSize); /* (EWRAM, 0x02000000) */
	for (int R = S_MAX(nt, 0); R < nt + J->slots && R < J->tileRows; ++R) {
		uint32_t src = R < J->split ? J->src[0] + R * 0x400 : J->src[1] + (R - J->split) * 0x400;
		uint32_t dst = J->ring + ((R + J->slotOff) % J->slots) * 0x400;
		if (ew && src - 0x02000000 + 0x400 <= ewSize) emu_vram_write(e, dst - 0x06000000, ew + (src - 0x02000000), 0x400);
	}
	emu_write16(e, T + J->last, (uint16_t) nt);
	for (int i = 0; i < 4; ++i) if (J->scroll[i]) emu_write16(e, T + J->scroll[i], (uint16_t) row);
	for (int i = 0; i < 3; ++i) if (J->scrollAt[i]) emu_write16(e, J->scrollAt[i], (uint16_t) row);
	for (int bg = 0; bg < 4; ++bg) if ((J->scrollVofs >> bg) & 1) emu_write16(e, 0x04000012 + bg * 4, (uint16_t) row);
	if (J->plus16) emu_write16(e, J->plus16, (uint16_t) (J->plus16Base + row + 16));
}

/* A sprite's height from its attribute 0 and 1 (double for an affine one
 * drawn double size). */
static int obj_height(uint16_t a0, uint16_t a1) {
	static const uint8_t h[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
	int s = a0 >> 14;
	int v = s < 3 ? h[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
static int obj_width(uint16_t a0, uint16_t a1) {
	static const uint8_t w[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
	int s = a0 >> 14;
	int v = s < 3 ? w[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
/* the game's sprites at the same place on the screen whatever the camera,
 * this frame (found against twins[0]; drawn by the HUD split, to be laid
 * over the top screen) */
static uint16_t tallFixed[128][3];
static int nTallFixed;
/* (carry) the rows where the score bar's layers drew something over the
 * table last frame (a box, a banner; -1: none): only the fixed sprites
 * there go to the top screen with it */
static int tallBandY0 = -1, tallBandY1 = -1;
static uint16_t tallBandFixed[128][3];
static int nTallBandFixed;

static uint8_t gameOam[1024]; /* the game's shadow sprite table and view before this frame (run_frame) */
static int gameView;
static void game_oam_take(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	for (int i = 0; i < 1024; i += 2) { uint16_t v = emu_read16(sec.emu, L->oam + i); gameOam[i] = v & 0xFF, gameOam[i + 1] = v >> 8; }
	gameView = tall_view(twinProf, sec.emu);
}
#define GAME_OAM16(off) ((uint16_t) (gameOam[(off)] | gameOam[(off) + 1] << 8))

/* The sprites each copy leaves out this frame, from the shadow sprite
 * tables (what this frame shows) and the views each shows them with
 * (PP-10). A copy's sprite alike one of the game's sprites on the game's
 * screen (attributes 1 and 2, the high byte of 0) is either the same thing
 * on the table, seen from the copy's camera: kept if that puts it on the
 * copy's screen, left out if it's only there because the hardware wraps its
 * position round (a message's letters); or at the same place on the screen
 * whatever the camera (a banner, the Mart's items): left out, the game's
 * picture shows it. */
static void twin_hide(Twin* tw) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	uint32_t o = L->oam;
	int gs = gameView, cs = tall_view(twinProf, &tw->e);
	static uint16_t gA[128][4]; /* the game's sprites on its screen: attributes 0-2, y on its screen + 256 */
	int ng = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = GAME_OAM16(i * 8);
		if ((a0 & 0x0300) == 0x0200 || (a0 >> 14) == 3) continue;
		uint16_t a1 = GAME_OAM16(i * 8 + 2);
		int y = a0 & 0xFF, h = obj_height(a0, a1);
		if (y >= GBA_H) y -= 256;
		if (y >= GBA_H || y + h <= 0) continue; /* (off its screen) */
		gA[ng][0] = a0, gA[ng][1] = a1, gA[ng][2] = GAME_OAM16(i * 8 + 4), gA[ng][3] = (uint16_t) (y + 256), ++ng;
	}
	static uint16_t out[128][3];
	static bool gFixed[128];
	memset(gFixed, 0, sizeof(gFixed));
	int n = 0;
	for (int j = 0; j < 128 && n < 128; ++j) {
		uint16_t b0 = emu_read16(&tw->e, o + j * 8);
		if ((b0 & 0x0300) == 0x0200 || (b0 >> 14) == 3) continue;
		uint16_t b1 = emu_read16(&tw->e, o + j * 8 + 2), b2 = emu_read16(&tw->e, o + j * 8 + 4);
		bool table = false, fixed = false;
		int h = obj_height(b0, b1);
		for (int i = 0; i < ng && !table; ++i) {
			if (gA[i][1] != b1 || gA[i][2] != b2 || (gA[i][0] & 0xFF00) != (b0 & 0xFF00)) continue;
			int yt = (int) gA[i][3] - 256 + gs - cs; /* where the table puts it on the copy's screen */
			if (((yt + 256) & 0xFF) == (b0 & 0xFF)) {
				if (yt < GBA_H && yt + h > 0) table = true;
				else fixed = true; /* (wrapped round) */
			} else if ((gA[i][0] & 0xFF) == (b0 & 0xFF)) {
				fixed = gFixed[i] = true;
			}
		}
		/* (and those found fixed to the screen against twins[0], for one at
		 * the game's own camera, which can't tell) */
		for (int i = 0; i < nTallFixed && tw != &twins[0] && !fixed; ++i) {
			fixed = tallFixed[i][0] == b0 && tallFixed[i][1] == b1 && tallFixed[i][2] == b2;
			if (fixed) table = false;
		}
		if (fixed && !table) out[n][0] = b0, out[n][1] = b1, out[n][2] = b2, ++n;
	}
	emu_hide_exact(&tw->e, out, n);
	tw->hidden = n;
	if (tw == &twins[0]) {
		nTallFixed = 0;
		for (int i = 0; i < ng; ++i) {
			if (!gFixed[i]) continue;
			tallFixed[nTallFixed][0] = gA[i][0], tallFixed[nTallFixed][1] = gA[i][1], tallFixed[nTallFixed][2] = gA[i][2];
			++nTallFixed;
		}
	}
}

/* A copy's camera held so that its view (less the shake) is `target`: its
 * view is its camera's row plus the shake and its own pans' offsets, which
 * depend on where its camera is, so its camera is held that much higher
 * (as they are now, and as they moved last frame) */
static uint32_t twin_hold(Twin* tw, int target, int step) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	uint32_t T = emu_read32(&tw->e, L->state);
	if (T < 0x02000000 || T >= 0x02040000) return 0;
	int shake = (int8_t) emu_read8(&tw->e, T + L->shake);
	int own = (int16_t) emu_read16(&tw->e, L->view) - (int16_t) emu_read16(&tw->e, T + L->camRow) - shake;
	int row = target - own - step;
	emu_write32(&tw->e, T + L->cam, (uint32_t) row << 8);
	emu_write16(&tw->e, T + L->camRow, (uint16_t) row);
	return T;
}

/* (carry) the game's queue of copies at the next vblank: its entries into
 * the copy's BG tiles left out, the copy's own put in. Entries of qStride
 * bytes (12 by default: Sonic Pinball Party's destination, source, size;
 * Muppet Pinball Mayhem's 16: source, destination, size, kind), the count a
 * byte (or a word: qWide), from slot 0 (or a ring of qSlots from the start
 * index at qIndex) */
#define CARRY_Q 64
static int carry_q_stride(const typeof(twinProf->tall.live.carry)* C) { return C->qStride ? C->qStride : 12; }
static int carry_q_count(const typeof(twinProf->tall.live.carry)* C, Emu* e) {
	int n = C->qWide ? (int) emu_read32(e, C->qCount) : emu_read8(e, C->qCount);
	return n < 0 ? 0 : n > CARRY_Q ? CARRY_Q : n;
}
static void carry_q_set_count(const typeof(twinProf->tall.live.carry)* C, Emu* e, int n) {
	if (C->qWide) emu_write32(e, C->qCount, (uint32_t) n);
	else emu_write8(e, C->qCount, (uint8_t) n);
}
/* the address of the queue's i-th entry, from its start */
static uint32_t carry_q_at(const typeof(twinProf->tall.live.carry)* C, Emu* e, int i) {
	int slot = i;
	if (C->qIndex) {
		int slots = C->qSlots ? C->qSlots : CARRY_Q;
		slot = (int) ((emu_read32(e, C->qIndex) + (uint32_t) i) % (uint32_t) slots);
	}
	return C->qEntries + (uint32_t) (slot * carry_q_stride(C));
}
static bool carry_ring(const typeof(twinProf->tall.live.carry)* C, uint32_t d) {
	return d >= 0x06000000 + C->ringLo && d < 0x06000000 + C->ringHi;
}
static bool carry_q_ring(const typeof(twinProf->tall.live.carry)* C, Emu* e, uint32_t at) {
	return carry_ring(C, emu_read32(e, at + (uint32_t) C->qDst));
}
/* (carry) the game's queue holds more than a row's copy into the ring (the
 * whole table's tiles loaded afresh) */
static bool carry_reload(Emu* e) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	if (!C->qCount) return false;
	int n = carry_q_count(C, e);
	uint32_t big = C->qReload ? (uint32_t) C->qReload : 0x400;
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (carry_q_ring(C, e, at) && emu_read32(e, at + (uint32_t) (C->qSize ? C->qSize : 8)) > big) return true;
	}
	return false;
}
static void carry_put(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	Emu* e = &tw->e;
	emu_vram_write(e, C->ringLo, tw->ring, C->ringHi - C->ringLo);
	for (int k = 0; k < 4 && C->vars[k].addr; ++k)
		for (int j = 0; j < C->vars[k].n; ++j) emu_write8(e, C->vars[k].addr + j, tw->vars[k][j]);
	if (!C->qCount) return;
	int n = carry_q_count(C, e), m = 0, st = carry_q_stride(C);
	/* (the game's other entries kept, in order, then the copy's own; read
	 * them all first: they're written back from the same start) */
	static uint32_t keep[2][CARRY_Q][4];
	uint32_t (*kp)[4] = keep[tw == &twins[0] ? 0 : 1];
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (carry_q_ring(C, e, at)) continue;
		for (int j = 0; j < st / 4; ++j) kp[m][j] = emu_read32(e, at + (uint32_t) j * 4);
		++m;
	}
	for (int i = 0; i < m; ++i)
		for (int j = 0; j < st / 4; ++j) emu_write32(e, carry_q_at(C, e, i) + (uint32_t) j * 4, kp[i][j]);
	for (int i = 0; i < tw->nq && m < CARRY_Q; ++i, ++m)
		for (int j = 0; j < st / 4; ++j) emu_write32(e, carry_q_at(C, e, m) + (uint32_t) j * 4, tw->q[i][j]);
	carry_q_set_count(C, e, m);
}
static void carry_take(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	Emu* e = &tw->e;
	memcpy(tw->ring, (const uint8_t*) emu_vram(e) + C->ringLo, C->ringHi - C->ringLo);
	for (int k = 0; k < 4 && C->vars[k].addr; ++k)
		for (int j = 0; j < C->vars[k].n; ++j) tw->vars[k][j] = emu_read8(e, C->vars[k].addr + j);
	tw->nq = 0;
	int n = C->qCount ? carry_q_count(C, e) : 0, st = carry_q_stride(C);
	for (int i = 0; i < n; ++i) {
		uint32_t at = carry_q_at(C, e, i);
		if (!carry_q_ring(C, e, at)) continue;
		for (int j = 0; j < st / 4; ++j) tw->q[tw->nq][j] = emu_read32(e, at + (uint32_t) j * 4);
		++tw->nq;
	}
	tw->carried = true;
}
/* (carry) the copy's first frame: taken from the game, its own tiles kept,
 * its camera walked toward its row (up to 16 rows a frame: the game
 * streams two tile rows a frame at most) */
static void twin_first_carry(Twin* tw) {
	const typeof(twinProf->tall.live.carry)* C = &twinProf->tall.live.carry;
	/* (the layers before the state: a change takes effect as the display
	 * registers are written, which loading a state does) */
	emu_layers(&tw->e, (twinNoObj ? 0x0F : 0x1F) & ~twinProf->tall.hudBg);
	if (!emu_state_load(&tw->e, twinStates[twinCur])) return;
	if (tw->seq + 1 != twinSeq) tw->carried = false; /* (not run last frame, or a load since: its tiles are stale) */
	if (tw->carried && carry_reload(&tw->e)) {
		/* the game is loading its table's tiles afresh (a table or a ball
		 * starting): what the copy kept is from before; it starts again
		 * from the game's */
		tw->carried = false;
		if (tallTrace) fprintf(stderr, "copy %d: the table's tiles reloaded, starting again\n", (int) (tw - twins));
	}
	tw->seq = twinSeq;
	if (C->ready.addr && !memprobes_match(&C->ready, 1, &tw->e)) tw->carried = false;
	if (tw->carried) carry_put(tw);
	/* (its own shadow sprite table: a frame whose logic runs long leaves
	 * part of it as it was, which would be the game's, placed for the
	 * game's view, and copied to the sprite table at the vblank all the
	 * same) */
	if (tw->carried) for (int i = 0; i < 1024; i += 4) emu_write32(&tw->e, twinProf->tall.live.oam + i, (uint32_t) tw->shadow[i] | tw->shadow[i + 1] << 8 | tw->shadow[i + 2] << 16 | (uint32_t) tw->shadow[i + 3] << 24);
	/* (from where its view is, not where its camera was put: the game
	 * moves the view only when its camera code runs, which it doesn't in
	 * a table's intro, and streams at most two tile rows a frame: a view
	 * jumping further leaves its tiles behind for good) */
	int at = tall_view(twinProf, &tw->e) - tall_shake(twinProf, &tw->e) + C->camOff;
	int want = tw->row + C->camOff;
	tw->cam = want > at ? S_MIN(at + 16, want) : S_MAX(at - 16, want);

	emu_write16(&tw->e, C->cam, (uint16_t) tw->cam);
	bool fresh = !tw->carried;
	/* (not drawn: only its second frame's picture is shown) */
	emu_frame_blind(&tw->e, twinKeys[twinCur] & 0x3FF);
	carry_take(tw);
	/* (starting again: its own sprites are the ones it just drew, not
	 * what it kept from when it last ran, on another table or before a
	 * state was loaded) */
	if (fresh) emu_oam_read(&tw->e, tw->oam);
	tw->up = true;
}

/* Before the game's frame: the frame before again, from the game's state
 * then, held at its row (which gives it this frame's sprites and scroll) */
static void twin_first(void* arg) {
	Twin* tw = arg;
	if (tall_carry(twinProf)) {
		tw->up = false;
		twin_first_carry(tw);
		return;
	}
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	tw->up = false;
	/* (its pans' step is guessed from last frame's; when the game's view
	 * starts or stops moving (the ball let go from a trap) the guess is a
	 * frame out, the copy's picture a few rows off its place and the rows
	 * between the screens wrong: then the frame is run again with the step
	 * it really took) */
	for (int again = 0; again < 2; ++again) {
		if (!emu_state_load(&tw->e, twinStates[twinCur])) return;
		uint32_t T = twin_hold(tw, tw->row, tw->panStep);
		if (!T) return;
		twin_jump(&tw->e, T, tw->row + (int8_t) emu_read8(&tw->e, T + L->shake));
		/* not drawn: its picture is never shown (twin_second's is), and
		 * drawing is about half of a frame's work, on the frame's critical
		 * path (twins[1]'s runs before the game's frame, on its thread) */
		emu_frame_blind(&tw->e, twinKeys[twinCur] & 0x3FF);
		/* (how far its pans moved it: put right) */
		int at = (int16_t) emu_read16(&tw->e, L->view) - (int8_t) emu_read8(&tw->e, T + L->shake);
		int miss = at - tw->row;
		tw->panStep += miss;
		if (tw->panStep > 8 || tw->panStep < -8) tw->panStep = 0;
		if (!miss) break;
		if (tallTrace || sec.debug) fprintf(stderr, "copy %d: %d row(s) off, run again\n", (int) (tw - twins), miss);
	}
	tw->up = true;
}
/* With the game's frame: this frame, whose picture it shows */
static uint32_t twinFrameKeys;
static void twin_second(void* arg) {
	Twin* tw = arg;
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	/* (carry: its own sprites as it last drew them, not the game's. The
	 * game copies its sprites to the sprite table at a vblank only once
	 * its frame's logic is done; after a frame whose logic ran long it
	 * shows its last frame's sprites again, and so would a copy, but the
	 * last ones a copy has (from the game's state) are the game's, placed
	 * for the game's view) */
	if (tall_carry(twinProf) && tw->carried) emu_oam_write(&tw->e, tw->oam);
	emu_frame(&tw->e, twinFrameKeys & 0x3FF);
	memcpy(tw->pic, tw->e.fb, sizeof(tw->pic));
	if (tall_carry(twinProf)) {
		emu_oam_read(&tw->e, tw->oam);
		for (int i = 0; i < 1024; ++i) tw->shadow[i] = emu_read8(&tw->e, twinProf->tall.live.oam + i);
	}
	tw->picRow = emu_io16(&tw->e, 0x10 + 4 * L->viewBg + 2) & 0x1FF;
	if (tw->picRow >= tall_rows_of(twinProf, &tw->e)) tw->picRow -= 512;
	/* (carry: a copy still walking to its row isn't shown) */
	int off = abs(tw->picRow - tall_shake(twinProf, &tw->e) - tw->row);
	tw->havePic = !tall_carry(twinProf) || off == 0 || (tw->havePic && off <= 16);
}

/* After this frame of the game: whether the copies run next frame, and
 * where they're held for its picture: twins[0] at rows - 320, twins[1] 160
 * rows below the game's view (its pans included; less the shake k, which
 * the copies add alike), or below rows - 320 once the game's is further
 * down. The game itself is never changed. */
static void twin_step(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	if (tall_carry(twinProf)) {
		/* (the same, for a game whose camera is a row and whose table's
		 * height is known from the game) */
		int k = tall_shake(twinProf, sec.emu);
		twinShakeNow = twinShakeNext, twinShakeNext = k;
		int top = tall_top_of(twinProf), vmax = tall_rows_of(twinProf, sec.emu) - 2 * GBA_H;
		int u = tall_view(twinProf, sec.emu) - k;
		twins[0].row = vmax, twins[1].row = S_MAX(S_MIN(u, vmax), top) + GBA_H;
		/* (both always: a copy walking to its row from the game's isn't
		 * shown for a few frames, so the top screen's copy is kept at its
		 * row while the game itself is there too) */
		twins[0].need = twins[1].need = true;
		/* (and for two frames after: the game's first frame after the sweep
		 * still shows the sprites where the sweep moved them) */
		static int noObjHold;
		if (L->carry.noObj.addr && memprobes_match(&L->carry.noObj, 1, sec.emu)) noObjHold = 3;
		twinNoObj = noObjHold > 0;
		if (noObjHold > 0) --noObjHold;
		twinRun = true;
		return;
	}
	uint32_t S = emu_read32(sec.emu, L->state);
	twinRun = false;
	/* (states saved by v1.3.2's earlier test builds have the camera limit
	 * those lowered to 186 for the table: put back) */
	if (emu_read16(sec.emu, L->bound) == 186) emu_write16(sec.emu, L->bound, L->boundFrom);
	if (S < 0x02000000 || S >= 0x02040000 || emu_read16(sec.emu, L->bound) != L->boundFrom) {
		twinArmed = 0; /* not on a table we know */
		return;
	}
	twinArmed = memprobes_match(L->idle, 2, sec.emu) ? twinArmed + 1 : 0;
	int32_t ax, ay;
	/* (no ball: the same. A couple of frames after the sweep: the copies are
	 * taken from the game every frame, so there's nothing to wait for) */
	if (twinArmed < 2 || !twin_ball(sec.emu, S, &ax, &ay)) return;
	int k = (int8_t) emu_read8(sec.emu, S + L->shake);
	twinShakeNow = twinShakeNext, twinShakeNext = k;
	int u = (int16_t) emu_read16(sec.emu, L->view) - k, vmax = twinProf->tall.rows - 2 * GBA_H;
	int want[TWINS] = { vmax, S_MAX(S_MIN(u, vmax), 0) + GBA_H };
	/* (PP-11: the game's whole screen is on the bottom one; the top screen
	 * shows the rows just above it, from twins[0] held there, when the
	 * table has them) */
	bool above = tallMsg && u - GBA_H >= 0;
	if (above) want[0] = u - GBA_H;
	for (int i = 0; i < TWINS; ++i) {
		if (twins[i].row != want[i] && !twins[i].up) twins[i].panStep = 0;
		twins[i].row = want[i];
	}
	/* (in the table's upper part the game itself is on the top screen:
	 * twins[0] isn't needed, and since the copies are taken from the game
	 * every frame, it's back at once when it is) */
	twins[0].need = u > vmax || above, twins[1].need = true;
	twinRun = true;
}

void twin_close(void) {
	worker_stop_all();
	for (int i = 0; i < TWINS; ++i) {
		if (twinOpen) emu_close(&twins[i].e);
		twins[i].up = twins[i].havePic = false;
		twins[i].carried = false;
		free(twins[i].ring), twins[i].ring = NULL;
	}
	twinOpen = twinRun = false;
	for (int i = 0; i < 2; ++i) free(twinStates[i]), twinStates[i] = NULL;
	twinHave = 0;
}

void twin_open(const char* rom, const MapProfile* p) {
	twin_close();
	if (!p || (!p->tall.live.state && !tall_carry(p))) return;
	if (tall_carry(p)) {
		for (int i = 0; i < TWINS; ++i) {
			twins[i].ring = malloc(p->tall.live.carry.ringHi - p->tall.live.carry.ringLo);
			if (!twins[i].ring) return;
		}
	}
	size_t sz = emu_state_size(sec.emu);
	twinStates[0] = malloc(sz), twinStates[1] = malloc(sz);
	int opened = 0;
	while (twinStates[0] && twinStates[1] && opened < TWINS && emu_open(&twins[opened].e, rom, NULL, false)) ++opened;
	if (opened < TWINS) {
		for (int i = 0; i < opened; ++i) emu_close(&twins[i].e);
		for (int i = 0; i < 2; ++i) free(twinStates[i]), twinStates[i] = NULL;
		return;
	}
	/* The camera's stores (a dump of its own: its Rev's set, by CRC32) are Thumb
	 * STR / STRH with an immediate offset (top bits 6 / 8). A ROM whose code is
	 * elsewhere (an untried dump of the release) has something else there: no
	 * copies (the logo on the other screen), not a copy with its code broken. */
	const uint32_t* nops = p->tall.live.rev.crc && p->tall.live.rev.crc == sec.emu->romCrc ? p->tall.live.rev.nop : p->tall.live.nop;
	for (int i = 0; i < 4 && nops[i]; ++i) {
		unsigned op = emu_read16(&twins[0].e, nops[i]) >> 12;
		if (op == 6 || op == 8) continue;
		fprintf(stderr, "live view: not this ROM's camera store at %08x (%04x), copies off\n", nops[i], emu_read16(&twins[0].e, nops[i]));
		for (int j = 0; j < opened; ++j) emu_close(&twins[j].e);
		for (int j = 0; j < 2; ++j) free(twinStates[j]), twinStates[j] = NULL;
		return;
	}
	for (int k = 0; k < TWINS; ++k) {
		for (int i = 0; i < 4 && nops[i]; ++i) emu_write16(&twins[k].e, nops[i], 0x46C0); /* mov r8, r8 */
		for (int i = 0; i < 16 && p->tall.live.carry.patch[i].addr; ++i) emu_write16(&twins[k].e, p->tall.live.carry.patch[i].addr, p->tall.live.carry.patch[i].v);
		emu_layers(&twins[k].e, 0x1F & ~p->tall.hudBg); /* (its score bar's layers: fixed to the screen, the game's are shown) */
	}
	twinProf = p;
	twinOpen = true;
	tallTrace = getenv("GBADS_TALL_TRACE") != NULL;
	fprintf(stderr, "live view: two more copies of the game for the table's other rows\n");
}

/* ---- the tall view's composer (was game.c's update_tall) ---- */
/* the state second.c and game.c read: whether the table is up and the live
 * view on, the profile, and the two screens' pictures */
bool tallOn;        /* the table's up: the screens show it */
const MapProfile* tallProf; /* logo-only game with a table over both screens (profile.tall) */
uint32_t tallTop[GBA_W * GBA_H], tallBot[GBA_W * GBA_H];
bool tallLive;      /* the live view: the table over both screens (update_tall), the level over both (second.c update_climb) */

/* profile.tall: the table over both screens (see profiles.h, PP-1). The
 * view's top row v is the game's scroll, at most rows - 320: the top screen
 * shows rows v to v + 159, the bottom one the 160 below. Each line comes
 * from the first picture that has its row: on the top screen the game's
 * while v is its scroll, else twins[0]'s (held at rows - 320) with what the
 * game puts on its screen laid over it; on the bottom screen twins[1]'s
 * (held at v + 160). Rows none has are black (only until the copies are
 * up). The score bar goes to the bottom of the bottom screen. Called after
 * each frame. */
typedef struct { const uint32_t* px; int row; int from, safe; } TallSrc;
/* (from: its first line with table rows; safe: its first line whose rows
 * are sure to be right: a carry copy's lines above gameFrom - 1 are the
 * rows its game hides under the score bar, whose tiles the game doesn't
 * keep (Muppet Pinball Mayhem's, moving down the table): used only when
 * no other picture has the row lower down) */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);
static bool tallKeep; /* (carry) a row no picture has keeps what it showed last */
/* the live view's last 10 seconds, a line a frame, for bug reports
 * (GBADS_TALL_TRACE's lines, kept whether it's set or not) */
char tallLog[TALL_LOG][176];
unsigned tallLogN;
static bool tall_line(uint32_t* dst, int row, const TallSrc* src, int n) {
	for (int pass = 0; pass < 2; ++pass)
		for (int i = 0; i < n; ++i) {
			int y = row - src[i].row;
			int lo = pass ? src[i].from : S_MAX(src[i].from, src[i].safe);
			if (src[i].px && y >= lo && y < GBA_H) {
				memcpy(dst, &src[i].px[y * GBA_W], sizeof(uint32_t) * GBA_W);
				return true;
			}
		}
	if (!tallKeep) for (int x = 0; x < GBA_W; ++x) dst[x] = 0xFF000000;
	return false;
}
/* Whether the game's BG0 has a pixel at screen (x, y) above the score bar
 * (its map from the profile's msg.map, row0 being the map row of screen row
 * 0; no horizontal scroll): the catch timer's digits. PP-10: the banner
 * boxes take only what isn't BG0's, so a banner flying across the timer
 * doesn't carry part of it to the bottom screen. */
static bool tall_bg0_pixel(uint32_t map, int row0, int x, int y) {
	uint16_t cnt = emu_io16(sec.emu, 0x08);
	uint32_t chr = 0x06000000 + ((cnt >> 2) & 3) * 0x4000;
	uint16_t e = emu_read16(sec.emu, map + (((row0 + (y >> 3)) & 31) * 32 + (x >> 3)) * 2);
	int px = x & 7, py = y & 7;
	if (e & 0x400) px = 7 - px;
	if (e & 0x800) py = 7 - py;
	uint32_t t = e & 0x3FF;
	if (cnt & 0x80) return emu_read8(sec.emu, chr + t * 64 + py * 8 + px) != 0;
	uint8_t b = emu_read8(sec.emu, chr + t * 32 + py * 4 + px / 2);
	return (px & 1 ? b >> 4 : b & 15) != 0;
}

void update_tall(const TallEnv* env) {
	const typeof(tallProf->tall)* t = &tallProf->tall;
	const typeof(t->live)* L = &t->live;
	bool on = env->nScreens > 1 && memprobes_match(t->on, 2, sec.emu);
	static bool copied; /* the score bar left in the game's picture */
	if (on != tallOn) {
		emu_hud_split(sec.emu, on, t->hudBg, NULL, 0, env->hudPx);
		emu_hud_copy_above(0);
		tallOn = on;
		copied = false;
		tallMsg = haveLastTop = false;
		tallTopLogo = tallFixedBot = false, tallFixedHold = 0;
		tallLive = false;
		(*env->mapShownIdx) = -1;
		/* (carry: the copies start again from the game's: what they kept,
		 * running on through the menus, is the last table's tiles, which
		 * a new table's (Muppet Pinball Mayhem's, after its curtains)
		 * doesn't reload in one go) */
		for (int i = 0; i < TWINS; ++i) twins[i].carried = false;
		if (sec.debug) fprintf(stderr, "frame %llu: table %s\n", (unsigned long long) env->frame, on ? "up" : "gone");
		return;
	}
	if (!on) return;
	bool up = twinOpen && twins[1].up && twins[1].havePic;
	/* (carry: and the top screen's copy, when its rows are on the top
	 * screen: one still walking to its row would leave them blank) */
	if (up && tall_carry(tallProf)) {
		int s0 = emu_io16(sec.emu, 0x10 + 4 * L->viewBg + 2) & 0x1FF, r0 = tall_rows_of(tallProf, sec.emu);
		if (s0 >= r0) s0 -= 512;
		if (s0 - twinShakeNow > r0 - 2 * GBA_H && !(twins[0].up && twins[0].havePic)) up = false;
		for (int i = 0; i < 3 && L->carry.plain[i].addr; ++i)
			if (memprobes_match(&L->carry.plain[i], 1, sec.emu)) up = false;
	}
	if (!up) {
		/* the copies aren't running (the table's opening sweep, a table they
		 * don't know, no ball): the game as it is, the logo on the second screen */
		if (!copied) emu_hud_copy(sec.emu, true), copied = true;
		emu_hud_copy_above(0);
		if (tallLive && sec.debug) fprintf(stderr, "frame %llu: live view off\n", (unsigned long long) env->frame);
		tallLive = false;
		(*env->mapShownIdx) = -1;
		return;
	}
	/* the game's picture: what's over the table (the catch timer, messages)
	 * kept, the score bar not (it goes below) */
	bool firstLive = copied; /* (the game's picture has its score bar until the next frame) */
	if (copied) emu_hud_copy(sec.emu, false), copied = false;
	/* (carry: the game's picture has none of the score bar's layers on any
	 * row, so it can go on either screen; they're laid over the top screen
	 * below) */
	bool carry = tall_carry(tallProf);
	emu_hud_copy_above(carry ? 0 : t->hudRow);
	int rows = tall_rows_of(tallProf, sec.emu), vmax = rows - 2 * GBA_H, top = tall_top_of(tallProf);
	int s = emu_io16(sec.emu, 0x10 + 4 * L->viewBg + 2) & 0x1FF; /* this frame's */
	if (s >= rows) s -= 512;
	/* (the screen shake moves the whole view, as it does the game's screen;
	 * the copies shake alike, by themselves) */
	int k = twinShakeNow;
	int v = S_MAX(S_MIN(s - k, vmax), top) + k;
	int safe = carry && L->carry.gameFrom ? L->carry.gameFrom - 1 : 0;
	TallSrc game = { sec.emu->fb, s, carry ? L->carry.gameFrom : 0, 0 };
	TallSrc a = { twins[0].up && twins[0].havePic ? twins[0].pic : NULL, twins[0].picRow, 0, safe }; /* (this frame's only) */
	TallSrc b = { twins[1].pic, twins[1].picRow, 0, safe };
	TallSrc topSrc[3] = { game, a, b }, botSrc[3] = { b, a, game };
	bool gameOnTop = s == v;
	if (!gameOnTop) topSrc[0] = a, topSrc[1] = game;
	/* (carry: wherever the game's own picture has the rows, it's used: the
	 * copies only draw the rows it doesn't) */
	if (carry) topSrc[0] = game, topSrc[1] = a, botSrc[0] = game, botSrc[1] = b, botSrc[2] = a;
	tallKeep = carry && tallLive;
	int missing = 0;
	/* (carry with gameFrom: the top screen's lines above gameFrom - 1 are
	 * the game's own, the score bar's alone, where the table doesn't show:
	 * its layers are laid over them below, on what the game has behind
	 * them. The line between is the score bar's black edge, drawn black:
	 * the game's own has it black but for a sprite passing under the bar
	 * (or, in its picture without the score bar's layers, a row from under
	 * its own view, which with the lower view is another part of the
	 * table); the copies' have the table) */
	int gf = carry && L->carry.gameFrom ? L->carry.gameFrom - 1 : 0;
	/* (what the top screen showed last frame, for a frame whose rows aren't
	 * all there: below) */
	static uint32_t prevTop[GBA_W * GBA_H];
	memcpy(prevTop, tallTop, sizeof(prevTop));
	bool havePrevTop = tallLive && !firstLive;
	int topMissing = 0;
	for (int y = 0; y < GBA_H; ++y) {
		if (y < gf) memcpy(&tallTop[y * GBA_W], &sec.emu->fb[y * GBA_W], sizeof(uint32_t) * GBA_W);
		else if (gf && y == gf) for (int x = 0; x < GBA_W; ++x) tallTop[y * GBA_W + x] = 0xFF000000u;
		else topMissing += !tall_line(&tallTop[y * GBA_W], v + y, topSrc, 3);
		missing += !tall_line(&tallBot[y * GBA_W], v + GBA_H + y, botSrc, 3);
	}
	missing += topMissing;
	if (carry) topMissing = 0; /* (carry keeps its own rows: tallKeep) */
	/* PP-11: the game dims its screen (paused, the Mart: brightness down)
	 * or shows a message box: its whole screen on the bottom one */
	uint16_t bld = emu_io16(sec.emu, 0x50);
	bool msg = (bld & 0xC0) == 0xC0 && (emu_io16(sec.emu, 0x54) & 0x1F) && !(carry && L->carry.keepDim);
	if (!msg && L->msg.map) {
		int n = 0;
		for (int r = L->msg.row0; r < L->msg.row0 + L->msg.rows; ++r) {
			for (int c = 0; c < 30; ++c) n += (emu_read16(sec.emu, L->msg.map + (r * 32 + c) * 2) & 0x3FF) != L->msg.blank;
		}
		msg = n > L->msg.min;
	}
	if (msg && !carry) {
		/* (the top screen: the table's rows just above the game's screen,
		 * from twins[0] held there (twin_step); none above it (the game's
		 * view near the table's top): the logo, as the rows it would show
		 * are the game's own again. Until the copy is there, what the top
		 * screen showed last) */
		if (s - k - GBA_H < top) {
			for (int i = 0; i < GBA_W * GBA_H; ++i) tallTop[i] = SCREEN_GREY;
			logo_draw(tallTop, GBA_W, GBA_H, 1, 0, LOGO_IDLE);
			tallTopLogo = true;
		} else {
			int off = 0;
			for (int y = 0; y < GBA_H; ++y) off += !tall_line(&tallTop[y * GBA_W], s - GBA_H + y, &a, 1);
			if (off) memcpy(tallTop, prevTop, sizeof(tallTop));
			else tallTopLogo = false;
		}
	} else {
		/* (the top screen: the table's rows, or while that would be the game's
		 * screen again, as they were before it) */
		if (msg && gameOnTop && haveLastTop) memcpy(tallTop, lastTop, sizeof(tallTop));
		/* (a copy not yet back at its place (just after PP-11's screen, which
		 * held it elsewhere): what the top screen showed last, for the frame
		 * or two it takes) */
		if (!msg && topMissing && havePrevTop) memcpy(tallTop, prevTop, sizeof(tallTop));
		else if (!msg) tallTopLogo = false;
	}
	if (msg) memcpy(tallBot, sec.emu->fb, sizeof(tallBot)); /* (and its score bar, below) */
	if (msg != tallMsg && sec.debug) fprintf(stderr, "frame %llu: %s\n", (unsigned long long) env->frame, msg ? "the game's screen on the bottom one (dimmed or a message)" : "the table on both screens again");
	tallMsg = msg;
	if (gameOnTop != tallGameTop && sec.debug) fprintf(stderr, "frame %llu: top screen: %s\n", (unsigned long long) env->frame, gameOnTop ? "the game" : "a copy (the lower view)");
	tallGameTop = gameOnTop;
	int laid = 0;
	if ((!gameOnTop || carry) && !firstLive && !msg) {
		/* What the game puts on its screen rather than on the table, which
		 * the copies leave out (PP-10), laid over the top screen where it is
		 * on the game's: what the HUD split draws above the score bar, its
		 * BG0 (the catch timer, messages) and the game's sprites at the same
		 * place on the screen whatever the camera (banners, the Mart's items:
		 * run_frame). (Drawn on their own: the copies' pictures can differ
		 * a little from the game's, so it can't be found by comparing.) */
		uint32_t clear = emu_hud_clear();
		/* (the game's sprites among it (banners such as BALL SAVE): on the
		 * screen with the ball, at their place on the game's screen. Which
		 * screen is settled as they come up, from where the ball is then,
		 * and kept while they're up: they never move between the screens) */
		static uint8_t box[GBA_W * GBA_H];
		bool anyFixed = false;
		if (!carry) {
			memset(box, 0, sizeof(box));
			for (int f = 0; f < nTallFixed; ++f) {
				uint16_t a0 = tallFixed[f][0], a1 = tallFixed[f][1];
				int y0 = a0 & 0xFF, x0 = a1 & 0x1FF, h = obj_height(a0, a1), w = obj_width(a0, a1);
				if (y0 >= GBA_H) y0 -= 256;
				if (x0 >= GBA_W) x0 -= 512;
				for (int y = S_MAX(y0, 0); y < S_MIN(y0 + h, t->hudRow); ++y)
					for (int x = S_MAX(x0, 0); x < S_MIN(x0 + w, GBA_W); ++x) {
						if ((env->hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
						if (L->msg.map && tall_bg0_pixel(L->msg.map, L->msg.row0, x, y)) continue; /* (BG0's: the timer) */
						box[y * GBA_W + x] = 1, anyFixed = true;
					}
			}
			if (anyFixed) {
				if (!tallFixedHold) {
					uint32_t S = emu_read32(sec.emu, L->state);
					int32_t bx, by;
					bool bot = twin_ball(sec.emu, S, &bx, &by) && (by >> 8) >= v + GBA_H - k;
					if (bot != tallFixedBot && sec.debug) fprintf(stderr, "frame %llu: banners on the %s screen (the ball's there)\n", (unsigned long long) env->frame, bot ? "bottom" : "top");
					tallFixedBot = bot;
				}
				tallFixedHold = 20;
			} else if (tallFixedHold) {
				--tallFixedHold;
			}
		}
		for (int i = 0; i < GBA_W * t->hudRow; ++i) {
			if ((env->hudPx[i] & 0xFFFFFF) == clear) continue;
			if (!carry && box[i] && tallFixedBot) tallBot[i] = env->hudPx[i] | 0xFF000000;
			else tallTop[i] = env->hudPx[i] | 0xFF000000;
			++laid;
		}
	}
	if (carry) {
		/* (the rows the score bar's layers drew over the table: below the
		 * score bar itself, above the message line) */
		uint32_t cl = emu_hud_clear();
		tallBandY0 = tallBandY1 = -1;
		for (int y = 24; y < t->hudRow; ++y) {
			int n = 0;
			for (int x = 0; x < GBA_W; ++x) n += (env->hudPx[y * GBA_W + x] & 0xFFFFFF) != cl;
			if (n > 8) tallBandY1 = y, tallBandY0 = tallBandY0 < 0 ? y : tallBandY0;
		}
	}
	/* the score bar at the bottom of the bottom screen */
	uint32_t clear = emu_hud_clear();
	/* (carry: but what comes down into its rows from above, part of
	 * something over the table (the zone card's ribbon down the screen's
	 * left edge, a banner sliding up through them), stays with the rest of
	 * it on the top screen: the columns with something in the two rows just
	 * above, and 8 either side) */
	static bool down[GBA_W];
	bool anyDown = false;
	memset(down, 0, sizeof(down));
	if (carry && !firstLive && !msg && t->hudRow >= 2) {
		for (int x = 0; x < GBA_W; ++x) {
			if ((env->hudPx[(t->hudRow - 1) * GBA_W + x] & 0xFFFFFF) == clear && (env->hudPx[(t->hudRow - 2) * GBA_W + x] & 0xFFFFFF) == clear) continue;
			for (int d = S_MAX(0, x - 8); d <= S_MIN(GBA_W - 1, x + 8); ++d) down[d] = true;
			anyDown = true;
		}
	}
	for (int i = GBA_W * t->hudRow; i < GBA_W * GBA_H; ++i) {
		if ((env->hudPx[i] & 0xFFFFFF) == clear) continue;
		if (anyDown && down[i % GBA_W]) tallTop[i] = env->hudPx[i] | 0xFF000000;
		else tallBot[i] = env->hudPx[i] | 0xFF000000;
	}
	if (!msg) memcpy(lastTop, tallTop, sizeof(lastTop)), haveLastTop = true;
	uint32_t* second = env->swap ? tallTop : tallBot;
	env->show(second);
	int same = -1; /* (trace: where a copy shows the game's own rows, how many pixels differ) */
	if (tallTrace && twins[1].picRow == s) {
		same = 0;
		for (int i = GBA_W * 24; i < GBA_W * t->hudRow; ++i) same += (twins[1].pic[i] & 0xFFFFFF) != (sec.emu->fb[i] & 0xFFFFFF);
	}
	if (tallTrace && carry) {
		/* (trace: the game's picture against the copies' at the same table
		 * rows: under the score bar, the middle, under the message line) */
		int d[3] = { 0, 0, 0 }, n[3] = { 0, 0, 0 };
		for (int k = 0; k < 2; ++k) {
			const Twin* tw = &twins[k];
			if (!tw->up || !tw->havePic) continue;
			for (int y = 0; y < GBA_H; ++y) {
				int cy = s + y - tw->picRow;
				if (cy < 0 || cy >= GBA_H) continue;
				int r = y < 24 ? 0 : y < 150 ? 1 : 2;
				++n[r];
				for (int x = 0; x < GBA_W; ++x) d[r] += (tw->pic[cy * GBA_W + x] & 0xFFFFFF) != (sec.emu->fb[y * GBA_W + x] & 0xFFFFFF);
			}
		}
		if (n[0] + n[1] + n[2]) fprintf(stderr, "cmp %d/%d %d/%d %d/%d\n", d[0], n[0], d[1], n[1], d[2], n[2]);
	}
	if (getenv("GBADS_TALL_AT") && env->frame == (unsigned long long) atoll(getenv("GBADS_TALL_AT"))) {
		static uint32_t sh[GBA_W * 3 * GBA_H];
		for (int y = 0; y < GBA_H; ++y) for (int x = 0; x < GBA_W; ++x) {
			sh[y * GBA_W * 3 + x] = sec.emu->fb[y * GBA_W + x] | 0xFF000000;
			sh[y * GBA_W * 3 + GBA_W + x] = twins[0].pic[y * GBA_W + x] | 0xFF000000;
			sh[y * GBA_W * 3 + 2 * GBA_W + x] = twins[1].pic[y * GBA_W + x] | 0xFF000000;
		}
		stbi_write_png(getenv("GBADS_TALL_PNG"), GBA_W * 3, GBA_H, 4, sh, GBA_W * 3 * 4);
		char sp[1200];
		snprintf(sp, sizeof sp, "%s.game.ss", getenv("GBADS_TALL_PNG")); emu_state_save_file(sec.emu, sp);
		snprintf(sp, sizeof sp, "%s.b.ss", getenv("GBADS_TALL_PNG")); emu_state_save_file(&twins[1].e, sp);
		FILE* of = fopen(getenv("GBADS_TALL_PNG"), "ab"); if (of) fclose(of);
		for (int w = 0; w < 2; ++w) {
			snprintf(sp, sizeof sp, "%s.%s.oam", getenv("GBADS_TALL_PNG"), w ? "b" : "game");
			FILE* f = fopen(sp, "wb");
			for (int i = 0; f && i < 512; ++i) { uint16_t v = emu_read16(w ? &twins[1].e : sec.emu, 0x07000000 + i * 2); fwrite(&v, 2, 1, f); }
			if (f) fclose(f);
		}
	}
	char* tl = tallLog[tallLogN++ % TALL_LOG];
	snprintf(tl, sizeof(tallLog[0]), "T %llu s %d v %d top %s a %d%s%s b %d%s%s hid %d/%d missing %d laid %d band %d-%d fixed %d/%d same %d noobj %d%s%s",
	         (unsigned long long) env->frame, s, v, gameOnTop ? "game" : "copy", twins[0].picRow, twins[0].havePic ? "" : " away",
	         twins[0].up ? "" : " down", twins[1].picRow, twins[1].havePic ? "" : " away", twins[1].up ? "" : " down", twins[0].hidden,
	         twins[1].hidden, missing, laid, tallBandY0, tallBandY1, nTallBandFixed, nTallFixed, same, (int) twinNoObj,
	         tallFixedHold && tallFixedBot ? " banners below" : "", msg && tallTopLogo ? " logo above" : "");
	if (tallTrace) fprintf(stderr, "%s\n", tl);
	if (!tallLive) fprintf(stderr, "frame %llu: live view\n", (unsigned long long) env->frame);
	tallLive = true;
	(*env->mapShownIdx) = 0;
}

/* ---- the copies' part of one game frame (was in game.c's run_frame) ----
 * twins_before: the copies' first frame and what to leave out, the game's
 * state kept for the next frame's copies; returns whether the live view is
 * on. twins_second_start: their second frame (this one) alongside the game's.
 * twins_after: once the game's frame has run, wait for them and step. */
bool twins_before(uint32_t keys) {
	bool live = twinOpen && tallOn;
	bool run = live && twinRun && twinHave > 0;
	++twinSeq;
	if (!run) for (int i = 0; i < TWINS; ++i) twins[i].up = false;
	nTallFixed = 0;
	if (run) {
		/* the copies' first frame (the game's frame before again, at their
		 * own cameras), then which of their sprites to leave out, and the
		 * game's fixed to the screen, for the HUD split to draw */
		game_oam_take();
		for (int i = 0; i < TWINS; ++i) twins[i].up = false;
		if (twins[0].need) worker_start(0, twin_first, &twins[0]);
		twin_first(&twins[1]);
		if (twins[0].need) worker_wait(0);
		if (!twinProf->tall.live.carry.keepObj)
			for (int i = 0; i < TWINS; ++i) if (twins[i].up) twin_hide(&twins[i]);
		/* (only while a copy's rows are on the top screen this frame) */
		bool topCopy = tall_view(twinProf, sec.emu) - tall_shake(twinProf, sec.emu) > tall_rows_of(twinProf, sec.emu) - 2 * GBA_H;
		emu_hud_keep_exact(tallFixed, topCopy ? nTallFixed : 0);
		/* (carry: and left out of the game's picture, which can be on the
		 * bottom screen) */
		if (tall_carry(twinProf)) {
			nTallBandFixed = 0;
			/* (whichever is on the top screen: the game's own picture is
			 * laid over by the boxes too, which would cover a sprite drawn
			 * on them, such as the pause menu's arrow) */
			for (int i = 0; i < nTallFixed; ++i) {
				int y = tallFixed[i][0] & 0xFF, h = obj_height(tallFixed[i][0], tallFixed[i][1]);
				if (y >= GBA_H) y -= 256;
				if (tallBandY0 >= 0 && y < tallBandY1 + 1 && y + h > tallBandY0) memcpy(tallBandFixed[nTallBandFixed++], tallFixed[i], sizeof(tallFixed[i]));
			}
			emu_hud_keep_exact(tallBandFixed, nTallBandFixed);
			emu_hide_exact(sec.emu, tallBandFixed, nTallBandFixed);
		}
	} else if (live) {
		emu_hud_keep_exact(tallFixed, 0);
		if (tall_carry(twinProf)) emu_hide_exact(sec.emu, tallFixed, 0);
	}
	if (live) {
		/* the game's state from before this frame, for next frame's copies */
		twinCur ^= 1;
		if (emu_state_save(sec.emu, twinStates[twinCur])) twinKeys[twinCur] = keys, twinHave = 1;
		else twinHave = 0;
	}
	return live;
}

void twins_second_start(uint32_t keys) {
	twinFrameKeys = keys;
	for (int i = 0; i < TWINS; ++i) if (twins[i].up) worker_start(i, twin_second, &twins[i]);
}

void twins_after(bool live) {
	for (int i = 0; i < TWINS; ++i) if (twins[i].up) worker_wait(i);
	if (live) twin_step();
}

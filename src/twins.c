/* The Pinball copies: see twins.h. */
#include "twins.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define debug twinDebug
static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

Emu* twinGame;
bool twinDebug;

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
bool twinNoObj; /* (carry) the copies draw no sprites (profile carry.noObj, in the game) */
bool twinRun;       /* the copies are to run (a table they know, its sweep over, a ball) */
static int twinArmed;      /* frames on a table they know, with no sweep of the table going on */
void* twinStates[2]; /* the game's state from before each of the last two frames */
uint32_t twinKeys[2];
int twinHave, twinCur; /* how many of them there are; the latest */
const MapProfile* twinProf;
bool tallTrace; /* GBADS_TALL_TRACE: a line a frame about the live view */
int twinShakeNow, twinShakeNext; /* the game's screen shake in this frame's picture / the next's */
bool tallMsg, tallGameTop; /* (for the log: PP-11's screen, the top screen's picture) */
bool tallTopLogo; /* PP-11's screen up with no table rows above it: the logo on the top screen */
bool tallFixedBot; /* PP-10: the game's banners on the bottom screen (the ball's there) */
int tallFixedHold; /* frames the banners stay where they went with none shown */
uint32_t lastTop[GBA_W * GBA_H]; /* the top screen before PP-11's screen came up */
bool haveLastTop;

bool twin_ball(Emu* e, uint32_t S, int32_t* x, int32_t* y) {
	uint32_t b = emu_read32(e, S + twinProf->tall.live.ball);
	if (b < 0x02000000 || b >= 0x02040000) return false;
	*x = (int32_t) emu_read32(e, b + 0x34), *y = (int32_t) emu_read32(e, b + 0x38);
	return true;
}

/* The table's rows: all of them, or (carry) the visible ones of the table
 * up now; the view's top row at its highest; the screen shake */
bool tall_carry(const MapProfile* p) { return p && p->tall.live.carry.cam; }
int tall_rows_of(const MapProfile* p, Emu* e) {
	const typeof(p->tall.live.carry)* C = &p->tall.live.carry;
	if (!C->cam || !C->rowsAt) return p->tall.rows;
	int h = emu_read16(e, C->rowsAt) - C->bottomLess;
	return h > 2 * GBA_H + C->top && h < 1024 ? h : 2 * GBA_H + C->top;
}
int tall_top_of(const MapProfile* p) { return p->tall.live.carry.cam ? p->tall.live.carry.top : 0; }
/* the view's row as the game keeps it (the next frame's BG VOFS; carry:
 * less viewOff, Muppet Pinball Mayhem's camera being 16 rows below it) */
int tall_view(const MapProfile* p, Emu* e) {
	return (int16_t) emu_read16(e, p->tall.live.view) - p->tall.live.carry.viewOff;
}
int tall_shake(const MapProfile* p, Emu* e) {
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
	for (int R = imax(nt, 0); R < nt + J->slots && R < J->tileRows; ++R) {
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
int obj_height(uint16_t a0, uint16_t a1) {
	static const uint8_t h[3][4] = { { 8, 16, 32, 64 }, { 8, 8, 16, 32 }, { 16, 32, 32, 64 } };
	int s = a0 >> 14;
	int v = s < 3 ? h[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
int obj_width(uint16_t a0, uint16_t a1) {
	static const uint8_t w[3][4] = { { 8, 16, 32, 64 }, { 16, 32, 32, 64 }, { 8, 8, 16, 32 } };
	int s = a0 >> 14;
	int v = s < 3 ? w[s][a1 >> 14] : 8;
	return (a0 & 0x0300) == 0x0300 ? v * 2 : v;
}
/* the game's sprites at the same place on the screen whatever the camera,
 * this frame (found against twins[0]; drawn by the HUD split, to be laid
 * over the top screen) */
uint16_t tallFixed[128][3];
int nTallFixed;
/* (carry) the rows where the score bar's layers drew something over the
 * table last frame (a box, a banner; -1: none): only the fixed sprites
 * there go to the top screen with it */
int tallBandY0 = -1, tallBandY1 = -1;
uint16_t tallBandFixed[128][3];
int nTallBandFixed;

static uint8_t gameOam[1024]; /* the game's shadow sprite table and view before this frame (run_frame) */
static int gameView;
void game_oam_take(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	for (int i = 0; i < 1024; i += 2) { uint16_t v = emu_read16(&(*twinGame), L->oam + i); gameOam[i] = v & 0xFF, gameOam[i + 1] = v >> 8; }
	gameView = tall_view(twinProf, &(*twinGame));
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
void twin_hide(Twin* tw) {
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
	tw->cam = want > at ? imin(at + 16, want) : imax(at - 16, want);

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
void twin_first(void* arg) {
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
		if (tallTrace || debug) fprintf(stderr, "copy %d: %d row(s) off, run again\n", (int) (tw - twins), miss);
	}
	tw->up = true;
}
/* With the game's frame: this frame, whose picture it shows */
uint32_t twinFrameKeys;
void twin_second(void* arg) {
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
void twin_step(void) {
	const typeof(twinProf->tall.live)* L = &twinProf->tall.live;
	if (tall_carry(twinProf)) {
		/* (the same, for a game whose camera is a row and whose table's
		 * height is known from the game) */
		int k = tall_shake(twinProf, &(*twinGame));
		twinShakeNow = twinShakeNext, twinShakeNext = k;
		int top = tall_top_of(twinProf), vmax = tall_rows_of(twinProf, &(*twinGame)) - 2 * GBA_H;
		int u = tall_view(twinProf, &(*twinGame)) - k;
		twins[0].row = vmax, twins[1].row = imax(imin(u, vmax), top) + GBA_H;
		/* (both always: a copy walking to its row from the game's isn't
		 * shown for a few frames, so the top screen's copy is kept at its
		 * row while the game itself is there too) */
		twins[0].need = twins[1].need = true;
		/* (and for two frames after: the game's first frame after the sweep
		 * still shows the sprites where the sweep moved them) */
		static int noObjHold;
		if (L->carry.noObj.addr && memprobes_match(&L->carry.noObj, 1, &(*twinGame))) noObjHold = 3;
		twinNoObj = noObjHold > 0;
		if (noObjHold > 0) --noObjHold;
		twinRun = true;
		return;
	}
	uint32_t S = emu_read32(&(*twinGame), L->state);
	twinRun = false;
	/* (states saved by v1.3.2's earlier test builds have the camera limit
	 * those lowered to 186 for the table: put back) */
	if (emu_read16(&(*twinGame), L->bound) == 186) emu_write16(&(*twinGame), L->bound, L->boundFrom);
	if (S < 0x02000000 || S >= 0x02040000 || emu_read16(&(*twinGame), L->bound) != L->boundFrom) {
		twinArmed = 0; /* not on a table we know */
		return;
	}
	twinArmed = memprobes_match(L->idle, 2, &(*twinGame)) ? twinArmed + 1 : 0;
	int32_t ax, ay;
	/* (no ball: the same. A couple of frames after the sweep: the copies are
	 * taken from the game every frame, so there's nothing to wait for) */
	if (twinArmed < 2 || !twin_ball(&(*twinGame), S, &ax, &ay)) return;
	int k = (int8_t) emu_read8(&(*twinGame), S + L->shake);
	twinShakeNow = twinShakeNext, twinShakeNext = k;
	int u = (int16_t) emu_read16(&(*twinGame), L->view) - k, vmax = twinProf->tall.rows - 2 * GBA_H;
	int want[TWINS] = { vmax, imax(imin(u, vmax), 0) + GBA_H };
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
	size_t sz = emu_state_size(&(*twinGame));
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
	const uint32_t* nops = p->tall.live.rev.crc && p->tall.live.rev.crc == (*twinGame).romCrc ? p->tall.live.rev.nop : p->tall.live.nop;
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

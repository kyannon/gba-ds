/* The second screen's contextual toggle (GEN-14, issue #37) and which of its
 * pictures is up: moved unchanged out of game.c (libretro core, discussion
 * #75). No SDL. The state it works on is game.c's, shared through toggle.h. */
#include "toggle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

/* which of the second screen's pictures shows now: the map's frames turn
 * every frameStep frames from mapAnimStart (a marker's blinking, or pages:
 * mapRes.pages); never out of range, even with mapAnimStart ahead of
 * frameCount (it once came from the game played before: frames[-1]).
 * The one place that says which page is up (the toggle, GEN-14, changes
 * it from here). */
int second_page(void) {
	if (mapRes.nFrames < 1 || frameCount < mapAnimStart) return 0;
	const MapProfile* hp = mc ? mapcap_profile(mc) : NULL;
	if (hp && hp->holdPage) {
		int hold = hp->holdPage(&emu); /* (a page kept up: Pokémon's Party in a battle) */
		if (hold >= 0 && hold < mapRes.nFrames) return hold;
	}
	if (hp && hp->views) return cfg->toggle ? pageOffset % mapRes.nFrames : 0; /* (views: the first until the toggle turns to another; no timer) */
	bool by_hand = cfg->toggle && mapRes.pages;
	if (by_hand && !cfg->autoRotate) return pageOffset % mapRes.nFrames; /* (auto-rotate off: a page stays until you press) */
	return (int) ((((frameCount - mapAnimStart) / (uint64_t) imax(1, mapRes.frameStep)) + (uint64_t) (by_hand ? pageOffset : 0)) % (uint64_t) mapRes.nFrames);
}

/* ---- the contextual toggle (GEN-14, issue #37). Turning the second screen
 * by hand: this is the one gate (toggle_ready: the button's active state
 * and the hotkey both read it) and the one state machine. A page is always
 * held, so it is shown at once (a cut); a turn that has to wait fades the
 * picture to black (5 frames), holds on black until the next picture is
 * ready (2 s at most, then the old picture comes back and nothing
 * changes), and fades back (5 frames). Presses outside IDLE are ignored,
 * not queued. Step 2 turns pages only; views are for the profiles (step 4),
 * and GBADS_TOGGLE_WAIT=N (frames, for tests) makes a page turn wait too. */
#define TG_FADE_FRAMES 5
#define TG_HOLD_MAX 120
#define TG_HOLD_PRESS_MAX 600 /* (capture on press: a capture can take seconds) */
int tgState, tgFrames, tgTarget, tgWait = -1;

/* a press turns the view (captured on the press) rather than a page: the profile's views, unless its picture is pages (Mother 3's Status where there's no map) and the other view isn't up */
bool tg_view_mode(const MapProfile* hp) {
	if (!hp || !hp->onPress) return false;
	const bool paged = mapRes.pages && mapRes.nFrames >= 2;
	if (!paged) return true;
	/* the picture is pages: they turn as pages do, and from the other view's last page a press brings the game's own view back (map, Status p1, p2, p3, map) */
	return viewAlt && second_page() == mapRes.nFrames - 1;
}

bool toggle_ready(void) {
	if (!cfg->toggle || tgState != TG_IDLE) return false;
	const MapProfile* hp = mc ? mapcap_profile(mc) : NULL;
	if (tg_view_mode(hp)) {
		/* the other view is captured on the press: ready when there is one here (or this is it, and the game's own is to come back), a picture is up and the hidden copy is free */
		if (mapShownIdx < 0 || logoForced || liveOn || mapInactive || pressCap || (capSubject && mapcap_busy(mc))) return false; /* (a refresh still running doesn't count: a press calls it off) */
		return viewAlt || !hp->altHere || hp->altHere(&emu);
	}
	if (!mapRes.pages || mapRes.nFrames < 2 || mapShownIdx < 0 || !map_anim()) return false; /* (one picture, the logo, Map Animations off) */
	if (logoForced || liveOn || mapInactive) return false;
	if (hp && hp->holdPage && hp->holdPage(&emu) >= 0) return false; /* (a page kept up: Pokémon's Party in a battle) */
	if (capSubject && mapcap_busy(mc)) return false; /* (the next subject's pictures aren't here yet: GEN-6) */
	return true;
}

/* the page shows, the timer (auto-rotate) starting again from it */
void toggle_apply(int page) {
	if (cfg->autoRotate) mapAnimStart = frameCount;
	pageOffset = page;
	overDirty = true;
	if (debug) fprintf(stderr, "frame %llu: toggle: page %d of %d\n", (unsigned long long) frameCount, page + 1, mapRes.nFrames);
}

void toggle_press(void) {
	if (!toggle_ready()) {
		if (debug) fprintf(stderr, "frame %llu: toggle: not ready, ignored\n", (unsigned long long) frameCount);
		return;
	}
	if (tgWait < 0) {
		const char* e = getenv("GBADS_TOGGLE_WAIT");
		tgWait = e ? imax(0, atoi(e)) : 0;
	}
	const MapProfile* pp = mc ? mapcap_profile(mc) : NULL;
	if (tg_view_mode(pp)) {
		/* capture on press: the other view is asked for now; the picture fades out, stays black until it is here (or it isn't, in time: the old one comes back) */
		viewAlt = !viewAlt, viewWait = true, tgCapWant = 0;
		pageOffset = 0, mapAnimStart = frameCount; /* (a view's pages start at the first: coming round again, not from the page the last turn ended on) */
		if (mc && mapcap_busy(mc)) mapcap_cancel(mc); /* (a refresh of the old view: the new one starts as soon as it has stopped) */
		tgState = TG_FADE_OUT, tgFrames = 0, tgTarget = -1;
		if (debug) fprintf(stderr, "frame %llu: toggle: view %s asked for\n", (unsigned long long) frameCount, viewAlt ? "2" : "1");
		return;
	}
	int next = (second_page() + 1) % mapRes.nFrames;
	if (tgWait > 0) tgState = TG_FADE_OUT, tgFrames = 0, tgTarget = next;
	else toggle_apply(next);
}

/* a new subject (a unit, a room, a loaded state): the pages start again at
 * the first; a turn under way ends, no fade. Nothing at all with the toggle
 * off: the pages go on turning through new captures as they always did. */
void toggle_new_subject(void) {
	if (tgState != TG_IDLE) tgState = TG_IDLE, fadeLevel = 256, overDirty = true;
	if (!cfg->toggle) return;
	pageOffset = 0;
	if (mapRes.pages) mapAnimStart = frameCount;
}

/* each frame */
void toggle_step(void) {
	if (!cfg->toggle && viewAlt) viewAlt = false, pressCap = true; /* (switched off while the other view was up: the game's own comes back) */
	if (!cfg->toggle && tgState != TG_IDLE) tgState = TG_IDLE, fadeLevel = 256, overDirty = true; /* (switched off meanwhile) */
	if (tgState == TG_IDLE) return;
	overDirty = true, fadeUpload = true; /* (the picture is drawn again each frame of a turn) */
	++tgFrames;
	switch (tgState) {
	case TG_FADE_OUT:
		fadeLevel = 256 * (TG_FADE_FRAMES - tgFrames) / TG_FADE_FRAMES;
		if (tgFrames >= TG_FADE_FRAMES) tgState = TG_HOLD, tgFrames = 0, fadeLevel = 0, pressCap = viewWait; /* (capture on press: asked for now that the old view is dark) */
		break;
	case TG_HOLD:
		if (tgTarget < 0) {
			/* (capture on press) the view is in: fade up; not in time: the old one stays, and the capture is called off */
			if (tgCapWant && capDone >= tgCapWant) {
				tgState = TG_FADE_IN, tgFrames = 0, viewWait = false;
				if (debug) fprintf(stderr, "frame %llu: toggle: view %s\n", (unsigned long long) frameCount, viewAlt ? "2" : "1");
			} else if (tgFrames >= TG_HOLD_PRESS_MAX) {
				viewAlt = !viewAlt, pressCap = false, viewWait = false, tgCapWant = 0;
				if (mc && mapcap_busy(mc)) mapcap_cancel(mc);
				tgState = TG_FADE_IN, tgFrames = 0;
				if (debug) fprintf(stderr, "frame %llu: toggle: the view didn't come in time, the old one stays\n", (unsigned long long) frameCount);
			}
			break;
		}
		if (tgFrames >= tgWait) {
			toggle_apply(tgTarget);
			tgState = TG_FADE_IN, tgFrames = 0;
		} else if (tgFrames >= TG_HOLD_MAX) {
			tgState = TG_FADE_IN, tgFrames = 0; /* (not found in time: the old picture comes back) */
		}
		break;
	case TG_FADE_IN:
		fadeLevel = 256 * tgFrames / TG_FADE_FRAMES;
		if (tgFrames >= TG_FADE_FRAMES) tgState = TG_IDLE, fadeLevel = 256;
		break;
	}
}


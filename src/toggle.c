/* The second screen's contextual toggle (GEN-14, issue #37) and which of its
 * pictures is up: moved unchanged out of game.c (libretro core, discussion
 * #75). No SDL. The state it works on is sec's (second.h). */
#include "toggle.h"
#include "second.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clamp.h"

/* (only this file's) */
static bool tg_view_mode(const MapProfile* hp);
static void toggle_apply(int page);


/* which of the second screen's pictures shows now: the map's frames turn
 * every frameStep frames from mapAnimStart (a marker's blinking, or pages:
 * mapRes.pages); never out of range, even with mapAnimStart ahead of
 * sec.frame (it once came from the game played before: frames[-1]).
 * The one place that says which page is up (the toggle, GEN-14, changes
 * it from here). */
int second_page(void) {
	if (sec.mapRes.nFrames < 1 || sec.frame < sec.mapAnimStart) return 0;
	const MapProfile* hp = sec.mc ? mapcap_profile(sec.mc) : NULL;
	if (hp && hp->holdPage) {
		int hold = hp->holdPage(sec.emu); /* (a page kept up: Pokémon's Party in a battle) */
		if (hold >= 0 && hold < sec.mapRes.nFrames) return hold;
	}
	if (hp && hp->views) return sec.cfg->toggle ? sec.pageOffset % sec.mapRes.nFrames : 0; /* (views: the first until the toggle turns to another; no timer) */
	bool by_hand = sec.cfg->toggle && sec.mapRes.pages;
	if (by_hand && !sec.cfg->autoRotate) return sec.pageOffset % sec.mapRes.nFrames; /* (auto-rotate off: a page stays until you press) */
	return (int) ((((sec.frame - sec.mapAnimStart) / (uint64_t) S_MAX(1, sec.mapRes.frameStep)) + (uint64_t) (by_hand ? sec.pageOffset : 0)) % (uint64_t) sec.mapRes.nFrames);
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

/* a press turns the view (captured on the press) rather than a page: the profile's views, unless its picture is pages (Mother 3's Status where there's no map) and the other view isn't up */
static bool tg_view_mode(const MapProfile* hp) {
	if (!hp || !hp->onPress) return false;
	const bool paged = sec.mapRes.pages && sec.mapRes.nFrames >= 2;
	if (!paged) return true;
	/* the picture is pages: they turn as pages do, and from the other view's last page a press brings the game's own view back (map, Status p1, p2, p3, map) */
	return sec.viewAlt && second_page() == sec.mapRes.nFrames - 1;
}

bool toggle_ready(void) {
	if (!sec.cfg->toggle || sec.tgState != TG_IDLE) return false;
	const MapProfile* hp = sec.mc ? mapcap_profile(sec.mc) : NULL;
	if (tg_view_mode(hp)) {
		/* the other view is captured on the press: ready when there is one here (or this is it, and the game's own is to come back), a picture is up and the hidden copy is free */
		if (sec.mapShownIdx < 0 || sec.logoForced || sec.liveOn || sec.mapInactive || sec.pressCap || (sec.capSubject && mapcap_busy(sec.mc))) return false; /* (a refresh still running doesn't count: a press calls it off) */
		return sec.viewAlt || !hp->altHere || hp->altHere(sec.emu);
	}
	if (!sec.mapRes.pages || sec.mapRes.nFrames < 2 || sec.mapShownIdx < 0 || !map_anim()) return false; /* (one picture, the logo, Map Animations off) */
	if (sec.logoForced || sec.liveOn || sec.mapInactive) return false;
	if (hp && hp->holdPage && hp->holdPage(sec.emu) >= 0) return false; /* (a page kept up: Pokémon's Party in a battle) */
	if (sec.capSubject && mapcap_busy(sec.mc)) return false; /* (the next subject's pictures aren't here yet: GEN-6) */
	return true;
}

/* the page shows, the timer (auto-rotate) starting again from it */
static void toggle_apply(int page) {
	if (sec.cfg->autoRotate) sec.mapAnimStart = sec.frame;
	sec.pageOffset = page;
	sec.overDirty = true;
	if (sec.debug) fprintf(stderr, "frame %llu: toggle: page %d of %d\n", (unsigned long long) sec.frame, page + 1, sec.mapRes.nFrames);
}

void toggle_press(void) {
	second_trace_call("toggle_press");
	if (!toggle_ready()) {
		if (sec.debug) fprintf(stderr, "frame %llu: toggle: not ready, ignored\n", (unsigned long long) sec.frame);
		return;
	}
	if (sec.tgWait < 0) {
		const char* e = getenv("GBADS_TOGGLE_WAIT");
		sec.tgWait = e ? S_MAX(0, atoi(e)) : 0;
	}
	const MapProfile* pp = sec.mc ? mapcap_profile(sec.mc) : NULL;
	if (tg_view_mode(pp)) {
		/* capture on press: the other view is asked for now; the picture fades out, stays black until it is here (or it isn't, in time: the old one comes back) */
		sec.viewAlt = !sec.viewAlt, sec.viewWait = true, sec.tgCapWant = 0;
		sec.pageOffset = 0, sec.mapAnimStart = sec.frame; /* (a view's pages start at the first: coming round again, not from the page the last turn ended on) */
		if (sec.mc && mapcap_busy(sec.mc)) mapcap_cancel(sec.mc); /* (a refresh of the old view: the new one starts as soon as it has stopped) */
		sec.tgState = TG_FADE_OUT, sec.tgFrames = 0, sec.tgTarget = -1;
		if (sec.debug) fprintf(stderr, "frame %llu: toggle: view %s asked for\n", (unsigned long long) sec.frame, sec.viewAlt ? "2" : "1");
		return;
	}
	int next = (second_page() + 1) % sec.mapRes.nFrames;
	if (sec.tgWait > 0) sec.tgState = TG_FADE_OUT, sec.tgFrames = 0, sec.tgTarget = next;
	else toggle_apply(next);
}

/* a new subject (a unit, a room, a loaded state): the pages start again at
 * the first; a turn under way ends, no fade. Nothing at all with the toggle
 * off: the pages go on turning through new captures as they always did. */
void toggle_new_subject(void) {
	if (sec.tgState != TG_IDLE) sec.tgState = TG_IDLE, sec.fadeLevel = 256, sec.overDirty = true;
	if (!sec.cfg->toggle) return;
	sec.pageOffset = 0;
	if (sec.mapRes.pages) sec.mapAnimStart = sec.frame;
}

/* each frame */
void toggle_step(void) {
	if (!sec.cfg->toggle && sec.viewAlt) sec.viewAlt = false, sec.pressCap = true; /* (switched off while the other view was up: the game's own comes back) */
	if (!sec.cfg->toggle && sec.tgState != TG_IDLE) sec.tgState = TG_IDLE, sec.fadeLevel = 256, sec.overDirty = true; /* (switched off meanwhile) */
	if (sec.tgState == TG_IDLE) return;
	sec.overDirty = true, sec.fadeUpload = true; /* (the picture is drawn again each frame of a turn) */
	++sec.tgFrames;
	switch (sec.tgState) {
	case TG_FADE_OUT:
		sec.fadeLevel = 256 * (TG_FADE_FRAMES - sec.tgFrames) / TG_FADE_FRAMES;
		if (sec.tgFrames >= TG_FADE_FRAMES) sec.tgState = TG_HOLD, sec.tgFrames = 0, sec.fadeLevel = 0, sec.pressCap = sec.viewWait; /* (capture on press: asked for now that the old view is dark) */
		break;
	case TG_HOLD:
		if (sec.tgTarget < 0) {
			/* (capture on press) the view is in: fade up; not in time: the old one stays, and the capture is called off */
			if (sec.tgCapWant && sec.capDone >= sec.tgCapWant) {
				sec.tgState = TG_FADE_IN, sec.tgFrames = 0, sec.viewWait = false;
				if (sec.debug) fprintf(stderr, "frame %llu: toggle: view %s\n", (unsigned long long) sec.frame, sec.viewAlt ? "2" : "1");
			} else if (sec.tgFrames >= TG_HOLD_PRESS_MAX) {
				sec.viewAlt = !sec.viewAlt, sec.pressCap = false, sec.viewWait = false, sec.tgCapWant = 0;
				if (sec.mc && mapcap_busy(sec.mc)) mapcap_cancel(sec.mc);
				sec.tgState = TG_FADE_IN, sec.tgFrames = 0;
				if (sec.debug) fprintf(stderr, "frame %llu: toggle: the view didn't come in time, the old one stays\n", (unsigned long long) sec.frame);
			}
			break;
		}
		if (sec.tgFrames >= sec.tgWait) {
			toggle_apply(sec.tgTarget);
			sec.tgState = TG_FADE_IN, sec.tgFrames = 0;
		} else if (sec.tgFrames >= TG_HOLD_MAX) {
			sec.tgState = TG_FADE_IN, sec.tgFrames = 0; /* (not found in time: the old picture comes back) */
		}
		break;
	case TG_FADE_IN:
		sec.fadeLevel = 256 * sec.tgFrames / TG_FADE_FRAMES;
		if (sec.tgFrames >= TG_FADE_FRAMES) sec.tgState = TG_IDLE, sec.fadeLevel = 256;
		break;
	}
}


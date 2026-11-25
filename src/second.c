/* The second screen, without SDL (libretro core, discussion #75): its state
 * (sec, second.h), the per-frame step that decides when to capture the map
 * and picks up what comes in (map_refresh_step), and the pure pixel steps.
 * Moved out of game.c without changing what it does; what still puts the
 * pictures on screen is the app's, through sec.hooks. */
#include "second.h"
#include "toggle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

SecondState sec = { .mapW = GBA_W, .mapH = GBA_H, .mapShownIdx = -1, .fadeLevel = 256, .tgWait = -1 };

void second_new_subject(void) {
	sec.refresh.forceMap = true;
}

/* the map's frames animate (not a profile whose map is still: mapStill) */
bool map_anim(void) {
	const MapProfile* p = sec.mc ? mapcap_profile(sec.mc) : NULL;
	return !(p && p->mapStill);
}

static inline int imax(int a, int b) { return a > b ? a : b; }
static inline int imin(int a, int b) { return a < b ? a : b; }

static int cmp_u32(const void* a, const void* b) {
	uint32_t x = *(const uint32_t*) a, y = *(const uint32_t*) b;
	return x < y ? -1 : x > y;
}

/* Builds the still map for "Map Animations: Off" into out (w * h), and marks
 * the pixels that animate in mask. Each animated spot (pixels that change,
 * grouped when they're within a few pixels of each other) is taken from the
 * frame where it is most visible: its brightest state (a marker that blinks
 * on and off, a pulsing glow at its peak), with STILL_RED its reddest if it
 * has red in it, or with STILL_COVER the state that
 * covers the most pixels unlike the spot's surroundings, ties going to the
 * one with the most contrast. */
void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
	int n = r->nFrames, w = r->w, h = r->h, wh = w * h;
	memcpy(out, r->frames[0], (size_t) wh * 4);
	memset(mask, 0, (size_t) wh);
	if (n < 2 || pick == STILL_FIRST) return;
	for (int i = 0; i < wh; ++i) {
		uint32_t p = r->frames[0][i] & 0xFFFFFF;
		for (int f = 1; f < n && !mask[i]; ++f) mask[i] = (r->frames[f][i] & 0xFFFFFF) != p;
	}
	int* label = calloc((size_t) wh, sizeof(int));
	int* queue = malloc((size_t) wh * sizeof(int));
	if (!label || !queue) {
		free(label);
		free(queue);
		return;
	}
	static uint32_t sample[4096];
	const int R = 3; /* animated pixels this close belong to the same spot */
	int spots = 0;
	for (int seed = 0; seed < wh; ++seed) {
		if (!mask[seed] || label[seed]) continue;
		/* flood-fill one spot; list its pixels in queue[0..len) */
		int id = ++spots, len = 0, head = 0;
		int x0 = w, y0 = h, x1 = 0, y1 = 0;
		label[seed] = id;
		queue[len++] = seed;
		while (head < len) {
			int p = queue[head++], px = p % w, py = p / w;
			x0 = imin(x0, px), x1 = imax(x1, px), y0 = imin(y0, py), y1 = imax(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = imax(x0 - 2, 0), y0 = imax(y0 - 2, 0), x1 = imin(x1 + 2, w - 1), y1 = imin(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) (sizeof(sample) / sizeof(sample[0])); ++x) {
					if (!mask[y * w + x]) sample[ns++] = r->frames[0][y * w + x] & 0xFFFFFF;
				}
			}
			qsort(sample, ns, sizeof(sample[0]), cmp_u32);
			for (int i = 0, run = 0, top = 0; i < ns; ++i) {
				run = i && sample[i] == sample[i - 1] ? run + 1 : 1;
				if (run > top) top = run, bg = sample[i];
			}
		}
		int best = 0;
		long bestA = -1, bestB = -1;
		/* STILL_RED: a spot with real red in any frame is judged by how red it
		 * is (red above the mean of green and blue, summed); else brightest */
		bool red = false;
		for (int f = 0; f < n && pick == STILL_RED && !red; ++f) {
			for (int i = 0; i < len && !red; ++i) {
				uint32_t p = r->frames[f][queue[i]];
				int pr = p & 0xFF, pg = (p >> 8) & 0xFF, pb = (p >> 16) & 0xFF;
				red = pr - (pg > pb ? pg : pb) > 80;
			}
		}
		for (int f = 0; f < n; ++f) {
			const uint32_t* fr = r->frames[f];
			long a = 0, b = 0;
			for (int i = 0; i < len; ++i) {
				uint32_t p = fr[queue[i]];
				if (pick == STILL_COVER) a += (p & 0xFFFFFF) != bg;
				else if (red) a += imax(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
				else a += (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			}
			if (pick == STILL_COVER) {
				for (int y = y0; y <= y1; ++y) {
					for (int x = x0; x <= x1; ++x) {
						uint32_t p = fr[y * w + x];
						if (x < x1) {
							uint32_t q = fr[y * w + x + 1];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
						if (y < y1) {
							uint32_t q = fr[(y + 1) * w + x];
							for (int c = 0; c < 24; c += 8) b += abs((int) ((p >> c) & 0xFF) - (int) ((q >> c) & 0xFF));
						}
					}
				}
			}
			if (a > bestA || (a == bestA && b > bestB)) best = f, bestA = a, bestB = b;
		}
		for (int i = 0; i < len; ++i) out[queue[i]] = r->frames[best][queue[i]];
	}
	free(label);
	free(queue);
}

/* A turn that waits (GEN-14): the picture at fade level (0 black .. 256 as
 * it is), in a buffer of its own kept between calls; px itself if there's
 * no room for one. (Was inline in game.c's upload_map_frame.) */
const uint32_t* second_fade(const uint32_t* px, size_t n, int level) {
	static uint32_t* fadeBuf;
	static size_t fadeN;
	if (n > fadeN) fadeBuf = realloc(fadeBuf, n * 4), fadeN = fadeBuf ? n : 0;
	if (!fadeBuf) return px;
	for (size_t i = 0; i < n; ++i) {
		uint32_t c = px[i];
		uint32_t r = (c & 0xFF) * (uint32_t) level >> 8, g = ((c >> 8) & 0xFF) * (uint32_t) level >> 8, b = ((c >> 16) & 0xFF) * (uint32_t) level >> 8;
		fadeBuf[i] = (c & 0xFF000000u) | r | g << 8 | b << 16;
	}
	return fadeBuf;
}

/* One colour all over, nearly: a fade to black or to white, a blank screen. */
bool frame_is_flat(const uint32_t* fb) {
	long sum = 0, n = 0;
	static int l[GBA_W * GBA_H / 7 + 1];
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		l[n] = (int) ((c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF));
		sum += l[n++];
	}
	long mean = sum / n, off = 0;
	for (long i = 0; i < n; ++i) off += labs(l[i] - mean) > 36;
	return off * 20 < n; /* 95% within 12 a channel of the average */
}

bool frame_is_dark(const uint32_t* fb) {
	int bright = 0, total = 0;
	for (int i = 0; i < GBA_W * GBA_H; i += 7) {
		uint32_t c = fb[i];
		int lum = ((c & 0xFF) + ((c >> 8) & 0xFF) * 2 + ((c >> 16) & 0xFF)) >> 2;
		bright += lum > 40;
		++total;
	}
	return bright * 40 < total;
}

/* put picture idx up: the HUD's pieces over it, faded during a turn that
 * waits (GEN-14), handed to the app (unless it is up already) */
void upload_map_frame(int idx) {
	const uint32_t* px = map_px(idx);
	if ((idx == sec.mapShownIdx && !sec.overDirty) || !px) return;
	sec.overDirty = false;
	px = sec.hooks.overMap(px); /* (the HUD's pieces over it: the app's, for now) */
	if (sec.fadeLevel < 256) {
		/* (a turn that waits, GEN-14: only the picture fades, so the
		 * shader and the bezel stay as they are) */
		px = second_fade(px, (size_t) sec.mapW * sec.mapH, sec.fadeLevel);
	}
	sec.hooks.present(px);
	sec.mapShownIdx = idx;
}

/* stillPx is from an older map (built only when it's shown) */
static bool stillStale;
static bool update_still(void) {
	int w = sec.mapRes.w, h = sec.mapRes.h, wh = w * h;
	uint32_t* px = malloc((size_t) wh * 4);
	unsigned char* mask = malloc((size_t) wh);
	if (!px || !mask) {
		free(px);
		free(mask);
		return false;
	}
	const MapProfile* prof = sec.mc ? mapcap_profile(sec.mc) : NULL;
	compose_still(&sec.mapRes, prof ? (int) prof->stillPick : STILL_BRIGHT, px, mask);
	if (sec.stillValid && sec.stillW == w && sec.stillH == h) {
		bool same = true;
		for (int i = 0; i < wh && same; ++i) same = px[i] == sec.stillPx[i] || mask[i];
		if (same) {
			free(px);
			free(mask);
			return false;
		}
	}
	free(sec.stillPx);
	free(mask);
	sec.stillPx = px, sec.stillW = w, sec.stillH = h, sec.stillValid = true;
	return true;
}

/* A new map picture in mapRes (a capture; or one kept with a save state,
 * `fresh` false: the level name above the HUD isn't taken from it, it's
 * kept on its own) */
void map_show_new(bool fresh) {
	sec.mapInactive = false;
	if (!fresh || sec.capSubject) sec.capSubject = false, toggle_new_subject();
	sec.hooks.hudNew(fresh); /* (what the HUD split keeps of it: the app's, for now) */
	sec.mapShownIdx = -1;
	/* (pages, a few seconds each: Fire Emblem's unit status; they
	 * keep turning at their pace through new captures) */
	bool pages = sec.mapRes.pages;
	if (!pages) sec.mapAnimStart = sec.frame;
	/* the still (each marker frozen) is only shown with map animations
	 * off, and building it is real work: only then */
	bool stillChanged = false;
	stillStale = map_anim();
	if (!stillStale) stillChanged = update_still();
	int first = pages ? second_page() : 0;
	upload_map_frame(map_anim() ? first : STILL_IDX);
	if (sec.debug) fprintf(stderr, "frame %llu: map %s (%d frame(s) %d apart%s)\n", (unsigned long long) sec.frame, fresh ? "updated" : "from the save state",
	                   sec.mapRes.nFrames, sec.mapRes.frameStep, stillStale ? "" : stillChanged ? ", still changed" : ", still unchanged");
}

/* a picture of the second screen by its index (STILL_IDX: the still) */
const uint32_t* map_px(int idx) {
	return idx == STILL_IDX ? (sec.stillValid ? sec.stillPx : NULL) : sec.mapRes.frames[idx];
}

void map_refresh_step(void) {
	if (sec.mc) {
		++sec.refresh.sinceMapRequest;
		/* the profile's sprite hiding, where it's only for some moments
		 * (hideWhen: Lady Sia's HUD in play, not its credits or pause
		 * screen); from the next frame drawn */
		if (sec.hideWhenProf && memprobes_match(sec.hideWhenProf->hideWhen, 2, &(*sec.emu)) != sec.hideWhenOn) {
			sec.hideWhenOn = !sec.hideWhenOn;
			emu_hide_sprites(&(*sec.emu), sec.hideWhenOn ? sec.hideWhenProf->hideSprites : NULL, sec.hideWhenOn ? sec.hideWhenN : 0);
			if (sec.debug) fprintf(stderr, "frame %llu: sprite hiding %s\n", (unsigned long long) sec.frame, sec.hideWhenOn ? "on" : "off");
		}
		{
			/* (logoIf: the logo for now, SM4-11) */
			const MapProfile* lp = mapcap_profile(sec.mc);
			bool lf = lp && ((lp->logoIf[0].addr && memprobes_match(lp->logoIf, 2, &(*sec.emu))) ||
			                 (lp->logoIfOr[0].addr && memprobes_match(lp->logoIfOr, 2, &(*sec.emu))));
			if (lf) sec.logoHolding = lp->logoIfLast ? 0 : 60;
			else if (sec.logoHolding > 0) --sec.logoHolding;
			lf = lf || sec.logoHolding > 0;
			if (sec.debug && lf != sec.logoForced) fprintf(stderr, "frame %llu: logo only %s\n", (unsigned long long) sec.frame, lf ? "on" : "off");
			sec.logoForced = lf;
		}
		bool dark = frame_is_dark((*sec.emu).fb);
		if (sec.refresh.wasDark && !dark) sec.refresh.refreshAt = (long) sec.frame + 6;
		sec.refresh.wasDark = dark;
		if (sec.refresh.refreshAt >= 0 && (long) sec.frame >= sec.refresh.refreshAt) {
			sec.refresh.forceMap = true;
			sec.refresh.refreshAt = -1;
			if (sec.debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) sec.frame);
		}
		if (sec.mapShownIdx >= 0 && sec.frame % 30 == 0 && mapcap_in_menu(sec.mc, &(*sec.emu))) {
			/* quit to the title: the map belongs to the old session */
			sec.mapShownIdx = -1;
			sec.mapInactive = false;
			sec.refresh.forceMap = true;
			/* (always logged: what sends the second screen back to the logo) */
			fprintf(stderr, "frame %llu: second screen back to the logo (the profile's menu probes matched)\n", (unsigned long long) sec.frame);
		}
		uint32_t rk = mapcap_room_key(sec.mc, &(*sec.emu));
		if (rk != sec.refresh.seenRoomKey) sec.refresh.seenRoomKey = rk, sec.refresh.seenRoomFor = 0;
		else ++sec.refresh.seenRoomFor;
		/* a change counts once it has held for 3 frames (a cursor
		 * gliding to its place, Mario walking a path: not each step);
		 * a capture still running for the old one is called off, so
		 * the new one starts as soon as it has stopped */
		bool roomChanged = rk && rk != sec.refresh.lastRoomKey && sec.refresh.seenRoomFor >= 2;
		if (roomChanged && mapcap_busy(sec.mc)) mapcap_cancel(sec.mc);
		/* Back off while captures keep failing (e.g. the game says "playing"
		 * but the map can't open: intro cutscenes, briefings). Each failed
		 * attempt emulates a few seconds in the background, which starves
		 * the game and its audio on slower devices. */
		const char* why = NULL;
		int fails = mapcap_failures(sec.mc, &why);
		if (fails != sec.refresh.lastFails) {
			if (sec.debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
			                            (unsigned long long) sec.frame, fails, why ? why : "?");
			const MapProfile* dp = mapcap_profile(sec.mc);
			if (fails && why && dp && dp->dimWhenUnavailable &&
			    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
				sec.mapInactive = true;
			}
			sec.refresh.lastFails = fails;
		}
		/* quick "no map here" failures (A Link to the Past indoors) cost little
		 * and are expected, so they don't back off */
		bool cheapFail = why && !strncmp(why, "map button did nothing", 22); /* (Pokémon's menu failures say why in brackets) */
		/* (a profile's own interval, when it's quicker: Swordcraft Story) */
		const MapProfile* ip = mapcap_profile(sec.mc);
		int every = ip && ip->interval > 0 && ip->interval < sec.cfg->mapInterval ? ip->interval : sec.cfg->mapInterval;
		int interval = cheapFail ? every : every << imin(fails, 3);
		/* after failures, a room change still triggers a try, but no more
		 * often than the normal interval */
		bool due = sec.refresh.sinceMapRequest >= interval || sec.pressCap ||
		           ((sec.refresh.forceMap || roomChanged) && (!fails || sec.refresh.sinceMapRequest >= every));
		if (due && !mapcap_busy(sec.mc) && mapcap_live_ok(sec.mc, &(*sec.emu)) && !(sec.viewWait && sec.tgState == TG_FADE_OUT)) { /* (not while the old view fades out: its picture is still the one up) */
			/* a new subject (a room, a loaded state) shows the game's own view again */
			if (roomChanged || sec.refresh.forceMap) sec.viewAlt = false;
			mapcap_set_alt(sec.viewAlt && sec.cfg->toggle);
			if (mapcap_request(sec.mc, &(*sec.emu))) {
				sec.refresh.sinceMapRequest = 0;
				sec.refresh.lastRoomKey = rk;
				sec.capSubject = roomChanged || sec.refresh.forceMap; /* (not a refresh of the same subject) */
				sec.refresh.forceMap = false;
				if (sec.pressCap) sec.pressCap = false, sec.tgCapWant = sec.capDone + 1;
			}
		}
		toggle_step();
		bool fresh = mapcap_poll(sec.mc, sec.mapRes.serial, &sec.mapRes);
		if (fresh && mapcap_in_menu(sec.mc, &(*sec.emu))) {
			/* a capture that finished after quitting to the title: drop it */
		} else if (fresh) {
			map_show_new(true);
			++sec.capDone;
		} else if (((map_anim() && sec.mapRes.nFrames > 1) || sec.fadeUpload) && sec.mapShownIdx >= 0) {
			int idx = second_page();
			sec.fadeUpload = false;
			upload_map_frame(idx);
		}
		/* a profile's own live picture (Mega Man Battle Network's maps on
		 * the net), drawn every frame over whatever was captured; when it
		 * stops, the captured picture again */
		const MapProfile* lvp = mapcap_profile(sec.mc);
		const uint32_t* lv = lvp && lvp->liveView && sec.romPath ? lvp->liveView(&(*sec.emu), sec.romPath) : NULL;
		if (lv) {
			sec.hooks.present(lv);
			if (sec.mapShownIdx < 0) sec.mapShownIdx = 0;
			if (!sec.liveOn && sec.debug) fprintf(stderr, "frame %llu: live view on\n", (unsigned long long) sec.frame);
			sec.liveOn = true;
		} else if (sec.liveOn) {
			sec.liveOn = false;
			if (sec.debug) fprintf(stderr, "frame %llu: live view off\n", (unsigned long long) sec.frame);
			if (sec.mapRes.ok && map_px(sec.mapShownIdx)) {
				sec.overDirty = true;
				upload_map_frame(sec.mapShownIdx);
			} else {
				sec.mapShownIdx = -1;
			}
		}
	}
}

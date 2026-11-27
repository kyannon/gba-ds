/* The second screen, without SDL (libretro core, discussion #75): its state
 * (sec, second.h); a game's set-up and teardown (second_open, second_drop,
 * second_close); the per-frame step (second_step: when to capture the map and
 * picking up what comes in, the screen copy, the tall and climb views, the
 * HUD, the fights); the states' block; the idle logo; the pixel steps. Moved
 * out of game.c without changing what it does; what puts the pictures on
 * screen is the app's (or a core's), through sec.hooks and hud.hooks. */
#include "second.h"
#include "toggle.h"
#include "hud.h"
#include "twins.h"
#include "rgba.h"
#include "climb.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clamp.h"

/* (the trace: tests, second_trace_begin below) */
static void trace(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void trace_open(int screens);
static void trace_loaded(const uint8_t* block, size_t n);
static void trace_step_end(void);

/* (only this file's: the steps second_open / second_step / second_close run) */
/* the still map for "Map Animations: Off" into out (r->w * r->h), the pixels
 * that animate marked in mask; pick is the profile's stillPick (STILL_*) */
static void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask);
/* the picture faded to level (0 .. 256) */
static const uint32_t* second_fade(const uint32_t* px, size_t n, int level);
/* once a frame, after the game's frame: when to capture the map, picking up a
 * finished capture, the pages, the profile's hideWhen / logoIf / liveView */
static void map_refresh_step(void);
/* a new map picture is in sec.mapRes (fresh: a capture; else kept with a state) */
static void map_show_new(bool fresh);
/* a logo-only game's screen copy (sec.snapProf), once a frame */
static void update_snapshot(void);
/* A game's set-up, in second_open: second_reset first; once the capture
 * exists (sec.mc), hud_setup (hud.h), then second_map_setup */
static void second_reset(void);
static void second_map_setup(const char* code, bool logoOnly);
/* the frame is mostly dark (a fade) */
static bool frame_is_dark(const uint32_t* fb);

SecondState sec = { .mapW = GBA_W, .mapH = GBA_H, .mapShownIdx = -1, .fadeLevel = 256, .tgWait = -1 };

void second_new_subject(void) {
	trace("new_subject");
	sec.refresh.forceMap = true;
}

/* the map's frames animate (not a profile whose map is still: mapStill) */
bool map_anim(void) {
	const MapProfile* p = sec.mc ? mapcap_profile(sec.mc) : NULL;
	return !(p && p->mapStill);
}


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
static void compose_still(const MapResult* r, int pick, uint32_t* out, unsigned char* mask) {
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
			x0 = S_MIN(x0, px), x1 = S_MAX(x1, px), y0 = S_MIN(y0, py), y1 = S_MAX(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = S_MAX(x0 - 2, 0), y0 = S_MAX(y0 - 2, 0), x1 = S_MIN(x1 + 2, w - 1), y1 = S_MIN(y1 + 2, h - 1);
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
				else if (red) a += S_MAX(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
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
static const uint32_t* second_fade(const uint32_t* px, size_t n, int level) {
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

static bool frame_is_dark(const uint32_t* fb) {
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
	px = hud_over_map(px); /* (the HUD's pieces over it: hud.c) */
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
static void map_show_new(bool fresh) {
	sec.mapInactive = false;
	if (!fresh || sec.capSubject) sec.capSubject = false, toggle_new_subject();
	hud_new_map(fresh); /* (what the HUD split keeps of it: hud.c) */
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

static void map_refresh_step(void) {
	{
		/* (logoIf: the logo for now, SM4-11; a logo-only game's from its HUD's profile) */
		const MapProfile* lp = sec.mc ? mapcap_profile(sec.mc) : hud.hudProf;
		bool lf = lp && ((lp->logoIf[0].addr && memprobes_match(lp->logoIf, 2, sec.emu)) ||
		                 (lp->logoIfOr[0].addr && memprobes_match(lp->logoIfOr, 2, sec.emu)));
		if (lf) sec.logoHolding = lp->logoIfLast ? 0 : 60;
		else if (sec.logoHolding > 0) --sec.logoHolding;
		lf = lf || sec.logoHolding > 0;
		if (sec.debug && lf != sec.logoForced) fprintf(stderr, "frame %llu: logo only %s\n", (unsigned long long) sec.frame, lf ? "on" : "off");
		sec.logoForced = lf;
	}
	if (sec.mc) {
		++sec.refresh.sinceMapRequest;
		/* the profile's sprite hiding, where it's only for some moments
		 * (hideWhen: Lady Sia's HUD in play, not its credits or pause
		 * screen); from the next frame drawn */
		if (sec.hideWhenProf && memprobes_match(sec.hideWhenProf->hideWhen, 2, sec.emu) != sec.hideWhenOn) {
			sec.hideWhenOn = !sec.hideWhenOn;
			emu_hide_sprites(sec.emu, sec.hideWhenOn ? sec.hideWhenProf->hideSprites : NULL, sec.hideWhenOn ? sec.hideWhenN : 0);
			if (sec.debug) fprintf(stderr, "frame %llu: sprite hiding %s\n", (unsigned long long) sec.frame, sec.hideWhenOn ? "on" : "off");
		}
		bool dark = frame_is_dark(sec.emu->fb);
		if (sec.refresh.wasDark && !dark) sec.refresh.refreshAt = (long) sec.frame + 6;
		sec.refresh.wasDark = dark;
		if (sec.refresh.refreshAt >= 0 && (long) sec.frame >= sec.refresh.refreshAt) {
			sec.refresh.forceMap = true;
			sec.refresh.refreshAt = -1;
			if (sec.debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) sec.frame);
		}
		if (sec.mapShownIdx >= 0 && sec.frame % 30 == 0 && mapcap_in_menu(sec.mc, sec.emu)) {
			/* quit to the title: the map belongs to the old session */
			sec.mapShownIdx = -1;
			sec.mapInactive = false;
			sec.refresh.forceMap = true;
			/* (always logged: what sends the second screen back to the logo) */
			fprintf(stderr, "frame %llu: second screen back to the logo (the profile's menu probes matched)\n", (unsigned long long) sec.frame);
		}
		uint32_t rk = mapcap_room_key(sec.mc, sec.emu);
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
		int interval = cheapFail ? every : every << S_MIN(fails, 3);
		/* after failures, a room change still triggers a try, but no more
		 * often than the normal interval */
		bool due = sec.refresh.sinceMapRequest >= interval || sec.pressCap ||
		           ((sec.refresh.forceMap || roomChanged) && (!fails || sec.refresh.sinceMapRequest >= every));
		if (due && !mapcap_busy(sec.mc) && mapcap_live_ok(sec.mc, sec.emu) && !(sec.viewWait && sec.tgState == TG_FADE_OUT)) { /* (not while the old view fades out: its picture is still the one up) */
			/* a new subject (a room, a loaded state) shows the game's own view again */
			if (roomChanged || sec.refresh.forceMap) sec.viewAlt = false;
			mapcap_set_alt(sec.viewAlt && sec.cfg->toggle);
			if (mapcap_request(sec.mc, sec.emu)) {
				sec.refresh.sinceMapRequest = 0;
				sec.refresh.lastRoomKey = rk;
				sec.capSubject = roomChanged || sec.refresh.forceMap; /* (not a refresh of the same subject) */
				sec.refresh.forceMap = false;
				if (sec.pressCap) sec.pressCap = false, sec.tgCapWant = sec.capDone + 1;
			}
		}
		toggle_step();
		bool fresh = mapcap_poll(sec.mc, sec.mapRes.serial, &sec.mapRes);
		if (fresh && mapcap_in_menu(sec.mc, sec.emu)) {
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
		const uint32_t* lv = lvp && lvp->liveView && sec.romPath ? lvp->liveView(sec.emu, sec.romPath) : NULL;
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

/* how many of the probes (up to 8) the frame matches, each colour within tol */
int probes_hit(const uint32_t* fb, const Probe* p, int tol) {
	int n = 0;
	for (int i = 0; i < 8 && (p[i].x || p[i].y || p[i].rgb); ++i) {
		uint32_t c = fb[p[i].y * GBA_W + p[i].x];
		int dr = (int) (c & 0xFF) - (int) ((p[i].rgb >> 16) & 0xFF);
		int dg = (int) ((c >> 8) & 0xFF) - (int) ((p[i].rgb >> 8) & 0xFF);
		int db = (int) ((c >> 16) & 0xFF) - (int) (p[i].rgb & 0xFF);
		n += abs(dr) <= tol && abs(dg) <= tol && abs(db) <= tol;
	}
	return n;
}

/* Renders the current frame again without these sprites (on top of the
 * ones already hidden: `kept`, put back afterwards) into out, then puts the
 * game back exactly as it was (state, picture, sound). */
void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV);
void snapshot_hiding_sprites(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out) {
	snapshot_hiding_sprites_poke(hide, n, kept, nKept, out, 0, 0);
}
/* The same with a halfword written into the game's memory first (pokeAt 0:
 * none), for that one frame */
void snapshot_hiding_sprites_poke(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out, uint32_t pokeAt, uint16_t pokeV) {
	static void* st;
	static size_t stSize;
	static uint32_t raw[GBA_W * GBA_H];
	size_t sz = emu_state_size(sec.emu);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st || !emu_state_save(sec.emu, st)) {
		memcpy(out, sec.emu->fb, sizeof(raw));
		return;
	}
	memcpy(raw, sec.emu->fb, sizeof(raw));
	SpriteRule all[12];
	int na = 0;
	for (int i = 0; i < nKept && na < 12; ++i) all[na++] = kept[i];
	for (int i = 0; i < n && na < 12; ++i) all[na++] = hide[i];
	emu_hide_sprites(sec.emu, all, na);
	emu_state_load(sec.emu, st);
	if (pokeAt) emu_write16(sec.emu, pokeAt, pokeV);
	emu_frame(sec.emu, 0);
	memcpy(out, sec.emu->fb, sizeof(raw));
	emu_hide_sprites(sec.emu, nKept ? kept : NULL, nKept);
	emu_state_load(sec.emu, st);
	int16_t drop[2048];
	while (emu_audio_read(sec.emu, drop, 1024) > 0) {}
	memcpy(sec.emu->fb, raw, sizeof(raw));
}

/* The second screen kept with save states (inside the state file: emu.c
 * emu_state_file_*_extra): what it holds that the loaded game can't give
 * back at once. A block "GDS2", version, the game's code, then sections
 * (a 4-letter tag, a size, the data):
 *   MAP  the map or status pictures shown (w, h, frames, step, frames' pixels)
 *   SNAP the screen copied there (THPS2's goal sheet)
 *   TITL the level name kept above the HUD (Super Mario Advance 2)
 *   CARD the title card taken (Sonic Advance)
 * Put back at the next frame after the load (screen_restore_apply), where
 * the game's code is the same; anything else of the second screen comes
 * back from the loaded game as usual. */
typedef struct { uint8_t* p; size_t n, cap; } Blob;
static void blob_add(Blob* b, const void* d, size_t n) {
	if (b->n + n > b->cap) {
		size_t c = S_MAX(b->cap * 2, b->n + n + 4096);
		uint8_t* q = realloc(b->p, c);
		if (!q) return;
		b->p = q, b->cap = c;
	}
	memcpy(b->p + b->n, d, n), b->n += n;
}
static void blob_u32(Blob* b, uint32_t v) { blob_add(b, &v, 4); }
static void blob_section(Blob* b, const char* tag, const void* d, size_t n) {
	blob_add(b, tag, 4), blob_u32(b, (uint32_t) n), blob_add(b, d, n);
}
void* second_state_save(size_t* n) {
	Blob b = { 0 };
	blob_add(&b, "GDS2", 4), blob_u32(&b, 1), blob_add(&b, sec.subCode, 4);
	if (sec.mapShownIdx >= 0 && !sec.snapProf && sec.mapRes.ok && sec.mapRes.frames[0] && sec.mapRes.nFrames > 0 && sec.mapRes.w > 0 && sec.mapRes.h > 0) {
		Blob m = { 0 };
		size_t px = (size_t) sec.mapRes.w * sec.mapRes.h * 4;
		blob_u32(&m, (uint32_t) sec.mapRes.w), blob_u32(&m, (uint32_t) sec.mapRes.h), blob_u32(&m, (uint32_t) sec.mapRes.nFrames), blob_u32(&m, (uint32_t) sec.mapRes.frameStep);
		for (int i = 0; i < sec.mapRes.nFrames; ++i) blob_add(&m, sec.mapRes.frames[i], px);
		if (m.p) blob_section(&b, "MAP ", m.p, m.n);
		free(m.p);
	}
	if (sec.snapProf && sec.mapShownIdx >= 0) blob_section(&b, "SNAP", sec.snapShown, sizeof(sec.snapShown));
	if (hud.mapTitleN > 0) {
		Blob t = { 0 };
		blob_u32(&t, (uint32_t) hud.mapTitleN), blob_add(&t, hud.mapTitle, (size_t) GBA_W * hud.mapTitleN * 4);
		if (t.p) blob_section(&b, "TITL", t.p, t.n);
		free(t.p);
	}
	if (hud.hudProf && hud.hudProf->hud.held.y1 && hud.heldN > 0) {
		Blob t = { 0 };
		blob_u32(&t, (uint32_t) hud.heldN), blob_add(&t, hud.heldPic, (size_t) GBA_W * hud.heldN * 4);
		if (t.p) blob_section(&b, "HELD", t.p, t.n);
		free(t.p);
		if (hud.heldNumW > 0) {
			Blob u = { 0 };
			blob_u32(&u, (uint32_t) hud.heldNumW), blob_u32(&u, (uint32_t) hud.heldNumH), blob_add(&u, hud.heldNum, (size_t) hud.heldNumW * hud.heldNumH * 4);
			if (u.p) blob_section(&b, "HNUM", u.p, u.n);
			free(u.p);
		}
	}
	if (hud.hudProf && hud.hudProf->hud.card.tile && hud.hudBackValid && hud.cardScore >= 0) {
		Blob c = { 0 };
		blob_u32(&c, (uint32_t) hud.cardScore), blob_add(&c, hud.cardBest, sizeof(hud.cardBest));
		if (c.p) blob_section(&b, "CARD", c.p, c.n);
		free(c.p);
	}
	*n = b.n;
	return b.p;
}

static const uint8_t* blob_find(const uint8_t* b, size_t n, const char* tag, uint32_t* size) {
	for (size_t at = 12; at + 8 <= n;) {
		uint32_t sz;
		memcpy(&sz, b + at + 4, 4);
		if (at + 8 + sz > n) break;
		if (!memcmp(b + at, tag, 4)) return *size = sz, b + at + 8;
		at += 8 + sz;
	}
	return NULL;
}
static uint32_t rd32(const uint8_t* p) { uint32_t v; memcpy(&v, p, 4); return v; }
/* a loaded state's frames are pages when this game's profile says its
 * frames are (a state keeps the frames and their pace, not what they are) */
static bool state_pages(int nFrames) {
	const MapProfile* hp = sec.mc ? mapcap_profile(sec.mc) : NULL;
	return nFrames > 1 && hp && (hp->pages || hp->views);
}

void second_state_load(const uint8_t* b, size_t n) {
	bool ours = b && n >= 12 && !memcmp(b, "GDS2", 4) && rd32(b + 4) == 1 && !memcmp(b + 8, sec.subCode, 4);
	/* what was kept from before the load is another moment's: the title
	 * card and the level name go (back from the state, if it has them) */
	if (hud.hudProf && hud.hudProf->hud.card.tile) hud.hudBackValid = false, hud.cardScore = -1, hud.sideScore = -1;
	hud.mapTitleN = 0, hud.hudLogoBelowDone = false;
	hud.heldN = 0, hud.heldNumW = 0, hud.heldArmed = true;
	sec.logoForced = false, sec.logoHolding = 0;
	if (!ours) {
		if (b && sec.debug) fprintf(stderr, "load state: its second screen is for another game or version: not used\n");
		return;
	}
	uint32_t sz;
	const uint8_t* d;
	if ((d = blob_find(b, n, "MAP ", &sz)) && sz >= 16) {
		int w = (int) rd32(d), h = (int) rd32(d + 4), nf = (int) rd32(d + 8), step = (int) rd32(d + 12);
		size_t px = (size_t) w * h * 4;
		/* (only at the size this game's captures are: frames are that big) */
		if (sec.mapRes.frames[0] && w == sec.mapRes.w && h == sec.mapRes.h && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			for (int i = 0; i < nf; ++i) memcpy(sec.mapRes.frames[i], d + 16 + px * i, px);
			sec.mapRes.nFrames = nf, sec.mapRes.frameStep = step, sec.mapRes.pages = state_pages(nf), sec.mapRes.ok = true, sec.mapRes.pixels = sec.mapRes.frames[0];
			map_show_new(false);
		} else if (!sec.mapRes.frames[0] && w > 0 && h > 0 && nf >= 1 && nf <= MAP_MAX_FRAMES && sz == 16 + px * nf) {
			/* (no capture yet this session: the frames made here, as mapcap_poll does) */
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) sec.mapRes.frames[i] = malloc(px);
			bool ok = true;
			for (int i = 0; i < MAP_MAX_FRAMES; ++i) ok = ok && sec.mapRes.frames[i];
			if (ok) {
				for (int i = 0; i < nf; ++i) memcpy(sec.mapRes.frames[i], d + 16 + px * i, px);
				sec.mapRes.w = w, sec.mapRes.h = h, sec.mapRes.nFrames = nf, sec.mapRes.frameStep = step, sec.mapRes.pages = state_pages(nf), sec.mapRes.ok = true, sec.mapRes.pixels = sec.mapRes.frames[0];
				map_show_new(false);
			}
		}
	}
	if (sec.snapProf && (d = blob_find(b, n, "SNAP", &sz)) && sz == sizeof(sec.snapShown)) {
		memcpy(sec.snapShown, d, sz);
		sec.hooks.presentGba(sec.snapShown);
		sec.mapShownIdx = 0;
	}
	if ((d = blob_find(b, n, "TITL", &sz)) && sz >= 4) {
		int rows = (int) rd32(d);
		if (rows > 0 && rows <= 32 && sz == 4 + (size_t) GBA_W * rows * 4) memcpy(hud.mapTitle, d + 4, sz - 4), hud.mapTitleN = rows;
	}
	if (hud.hudProf && hud.hudProf->hud.held.y1 && (d = blob_find(b, n, "HELD", &sz)) && sz >= 4) {
		int rows = (int) rd32(d);
		if (rows > 0 && rows <= GBA_H && sz == 4 + (size_t) GBA_W * rows * 4) memcpy(hud.heldPic, d + 4, sz - 4), hud.heldN = rows;
	}
	if (hud.hudProf && hud.hudProf->hud.held.over.x1 && (d = blob_find(b, n, "HNUM", &sz)) && sz >= 8) {
		int w = (int) rd32(d), hh = (int) rd32(d + 4);
		if (w > 0 && hh > 0 && w * hh <= GBA_W * GBA_H && sz == 8 + (size_t) w * hh * 4) memcpy(hud.heldNum, d + 8, sz - 8), hud.heldNumW = w, hud.heldNumH = hh;
	}
	if (hud.hudProf && hud.hudProf->hud.card.tile && (d = blob_find(b, n, "CARD", &sz)) && sz == 4 + sizeof(hud.cardBest)) {
		hud.cardScore = (int) rd32(d);
		memcpy(hud.cardBest, d + 4, sizeof(hud.cardBest));
		card_backdrop();
	}
	if (sec.debug) fprintf(stderr, "load state: the second screen from it (%s%s%s%s%s)\n", blob_find(b, n, "MAP ", &sz) ? " map" : "",
	                   blob_find(b, n, "SNAP", &sz) ? " copy" : "", blob_find(b, n, "TITL", &sz) ? " title" : "", blob_find(b, n, "CARD", &sz) ? " card" : "",
	                   blob_find(b, n, "HELD", &sz) ? " held" : "");
}

static void update_snapshot(void) {
	const typeof(sec.snapProf->snap)* sn = &sec.snapProf->snap;
	int tol = sn->tol ? sn->tol : 12;
	if (probes_hit(sec.emu->fb, sn->show, tol) >= sn->showMin) {
		sec.snapRevert = 0;
		if (++sec.snapShow >= S_MAX(1, sn->stable)) {
			const uint32_t* px = sec.emu->fb;
			if (sn->hide[0].x1) {
				/* only when the picture changes (the sheets are still) */
				static uint32_t last[GBA_W * GBA_H], clean[GBA_W * GBA_H];
				if (sec.mapShownIdx < 0 || memcmp(last, sec.emu->fb, sizeof(last))) {
					memcpy(last, sec.emu->fb, sizeof(last));
					int n = 0;
					while (n < 4 && sn->hide[n].x1) ++n;
					snapshot_hiding_sprites(sn->hide, n, NULL, 0, clean);
					memcpy(sec.snapShown, clean, sizeof(sec.snapShown));
					sec.hooks.presentGba(clean);
				}
				px = NULL;
			}
			if (px) {
				memcpy(sec.snapShown, px, sizeof(sec.snapShown));
				sec.hooks.presentGba(px);
			}
			if (sec.mapShownIdx < 0 && sec.debug) fprintf(stderr, "frame %llu: screen copied to the second screen\n", (unsigned long long) sec.frame);
			sec.mapShownIdx = 0;
		}
		return;
	}
	sec.snapShow = 0;
	if (sec.mapShownIdx < 0) return;
	bool menu = false;
	for (int k = 0; k < 2; ++k) {
		if (sn->revertMin[k] && probes_hit(sec.emu->fb, sn->revert[k], tol) >= sn->revertMin[k]) menu = true;
	}
	if (!menu) {
		sec.snapRevert = 0;
	} else if (++sec.snapRevert >= S_MAX(1, sn->stable)) {
		sec.mapShownIdx = -1;
		sec.snapRevert = 0;
		if (sec.debug) fprintf(stderr, "frame %llu: back at the menus, logo\n", (unsigned long long) sec.frame);
	}
}

/* The second screen as it is before a game's set-up: no picture, no map,
 * no screen copy, no HUD. (Was at the top of game.c's ss_setup.) */
static void second_reset(void) {
	sec.mapW = GBA_W;
	sec.mapH = GBA_H;
	memset(&sec.mapRes, 0, sizeof(sec.mapRes));
	sec.mapShownIdx = -1;
	sec.stillValid = false;
	sec.mapInactive = false;
	sec.snapProf = NULL;
	hud.hudProf = NULL;
	hud.hudOn = false;
	hud.onWithAt = 0;
}

/* The map's set-up for a game once its capture exists (code: the profile in
 * use; logoOnly: a game that shows only its logo, or a screen copy): which of
 * the game's sprites to hide (its corner mini-map: the full map is always up),
 * switched by hideWhen or not, and the picture's size. (Was inline in game.c's
 * ss_setup.) */
static void second_map_setup(const char* code, bool logoOnly) {
	sec.hideWhenProf = NULL;
	if (sec.mc) {
		fprintf(stderr, "map profile: %s\n", mapcap_title(sec.mc));
		mapcap_size(sec.mc, &sec.mapW, &sec.mapH);
		/* the full map is always up, so hide the game's own corner mini-map */
		const MapProfile* prof = profile_find(code);
		int nRules = 0;
		while (prof && nRules < (int) (sizeof(prof->hideSprites) / sizeof(prof->hideSprites[0])) && prof->hideSprites[nRules].x1) ++nRules;
		/* (with hideWhen: off until the frame loop finds it matching) */
		sec.hideWhenProf = prof && nRules && prof->hideWhen[0].addr ? prof : NULL;
		sec.hideWhenN = nRules, sec.hideWhenOn = false;
		emu_hide_sprites(sec.emu, prof && !sec.hideWhenProf ? prof->hideSprites : NULL, sec.hideWhenProf ? 0 : nRules);
		emu_hide_grouped(sec.emu, prof && prof->hideGrouped);
	} else if (!logoOnly) {
		fprintf(stderr, "no map profile for %s\n", code);
	}
}

/* The second screen's set-up for a game, from nothing (code: the profile in
 * use: the game's, or for a compilation the game running in it, sec.subCode):
 * the capture, the HUD split, the fights' picture, a logo-only game's screen
 * copy, tall view or climb. What a core calls once the game is open and sec.emu,
 * sec.cfg, sec.romPath, sec.hooks and hud.hooks are set; the app calls it from
 * ss_setup beside its own set-up (controls, WarioWare's sensor, Android's
 * pinning). screens: how many there are (the tall view and climbs need two).
 * With the second screen off (cfg->map) only the reset. (Was inline in game.c's
 * ss_setup.) */
void second_open(const char* rom, const char* code, int screens) {
	if (code != sec.subCode) snprintf(sec.subCode, sizeof(sec.subCode), "%.4s", code); /* (the app's callers set it already) */
	trace_open(screens);
	second_reset();
	sec.logoOnly = false;
	tallProf = NULL;
	tallOn = false;
	tallLive = false;
	sec.climbProf = NULL;
	sec.climbOn = false;
	climb_close();
	if (!sec.cfg->map) return;
	sec.mc = mapcap_create(rom, code, true);
	const MapProfile* lp = profile_find(code);
	memprobe_virtual = lp ? lp->virt : NULL;
	sec.logoOnly = lp && lp->logoOnly;
	sec.snapProf = sec.logoOnly && lp->snap.showMin ? lp : NULL;
	tallProf = sec.logoOnly && lp->tall.on[0].addr && sec.cfg->tallLive ? lp : NULL;
	if (tallProf && screens > 1) twin_open(rom, tallProf);
	else twin_close();
	sec.climbProf = sec.logoOnly && lp->climb.on[0].addr && sec.cfg->tallLive && screens > 1 && climb_open(rom, lp, sec.emu, !getenv("GBADS_CLIMB_INSTEP")) ? lp : NULL;
	sec.overOn = false;
	hud_setup(lp, screens); /* (hud.c) */
	sec.snapShow = sec.snapRevert = 0;
	if (sec.mc) mapcap_set_still(sec.mc, !map_anim());
	if (sec.logoOnly) fprintf(stderr, "logo only: %s\n", lp->title);
	second_map_setup(code, sec.logoOnly);
}

/* the capture's pictures freed (second_drop, second_close) */
static void map_frames_free(void) {
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(sec.mapRes.frames[i]);
		sec.mapRes.frames[i] = NULL;
	}
}

/* One frame of the game (keys: the GBA's, plus the app's bits above them,
 * which only the copies read), with the Pinball copies (twins.c) and the climb
 * copy (climb.c) run around it: what a core calls instead of emu_frame. (Was
 * in game.c's run_frame, with the app's own parts before it: swallowed keys,
 * the SNES layout's camera pan, WarioWare's dial, the bug log.) */
void second_run_frame(uint32_t keys) {
	trace("run %x", (unsigned) keys);
	bool live = twins_before(keys);
	twins_second_start(keys);
	if (sec.climbProf) climb_begin(sec.emu, keys, sec.climbOn);
	emu_frame(sec.emu, keys & 0x3FF);
	if (sec.climbProf) climb_end(sec.emu);
	twins_after(live);
}

/* the game jumped (a state loaded, a rewind, a restart): what the Pinball
 * copies and the climb copy kept is another moment's. (Was in game.c's
 * bug_reset.) */
void second_jump(void) {
	trace("jump");
	twinSeq += 2;
	climb_reset();
}

/* A state was loaded into the game (after second_jump): its second-screen
 * block (block, n bytes, malloc'd, taken over; NULL: none) is put back by the
 * next second_frame_start, and the HUD's pictures from before are dropped.
 * (Was in game.c's state_load and screen_restore_apply.) */
static uint8_t* restoreBlock;
static size_t restoreN;
static bool restorePending;
void second_state_loaded(uint8_t* block, size_t n) {
	trace_loaded(block, n);
	free(restoreBlock);
	restoreBlock = block, restoreN = n;
	restorePending = true;
	/* HUD pieces from another moment: taken again from the loaded game */
	for (int m = 0; m < 4; ++m) hud.pieces[m].stale = true, hud.pieces[m].still = 0;
	hud.mirrorHave = false; /* (hud.mirror: the picture from before is another moment's) */
	hud.hudOffLatch = false; /* (hud.offAfter: found again from the loaded game) */
	hud.onWithAt = 0; /* (hud.onWith: a sprite seen before the load doesn't hold the HUD on after it) */
	if (hud.hudProf && hud.hudProf->hud.place) hud.hudProf->hud.place(sec.emu, sec.romPath, false); /* (hud.place: worked out again from the loaded game) */
}

/* At the start of a frame, before the game's (and outside second_run_frame,
 * which the app may run on another thread): a loaded state's block put back. */
void second_frame_start(void) {
	trace("frame_start");
	if (!restorePending) return;
	restorePending = false;
	second_state_load(restoreBlock, restoreN);
}

/* What the second screen shows now (the app draws it; a core puts it in its
 * frame): SHOWN_OFF none (no capture and not a logo-only game, or the second
 * screen switched off: the game alone, or the other screen black); else the
 * first that holds of SHOWN_COMBAT (the fights' picture, sec.mapW x mapH, from
 * presentCombat; only where the app has a place for it: hasCombat), SHOWN_HUD
 * (HUD_CW x HUD_CH, from presentHud), SHOWN_MAP (from present, sec.mapW x
 * mapH, or presentGba, GBA_W x GBA_H: the tall and climb views) and
 * SHOWN_LOGO (the idle logo, second_idle_picture, or none). *dim: the map
 * dimmed over whatever shows (the map button did nothing: sec.mapInactive).
 * WarioWare's dial and wheel come before all of these (the app's). (Was
 * inline in game.c's draw_game.) */
int second_shown(bool* dim) {
	*dim = !hud.hudShown && sec.mapShownIdx >= 0 && sec.mapInactive && !sec.logoForced;
	if (!((sec.mc || sec.logoOnly) && sec.cfg->map)) return SHOWN_OFF;
	/* (and with hud.logoUnder, on the map before there's a map picture) */
	bool logoBack = hud.combatProf && hud.combatProf->hud.logoUnder && sec.mapShownIdx < 0 && (hud.combatInGame || hud.combatProf->hud.logoAlways);
	if (hud.overAlone && sec.mapShownIdx >= 0) logoBack = false; /* (the phase banner, on the other armies' turn too) */
	if (((hud.combatOn && !(hud.overAlone && sec.mapShownIdx >= 0)) || logoBack) && hud.hooks.hasCombat && hud.hooks.hasCombat()) return SHOWN_COMBAT;
	if (hud.hudShown && !sec.logoForced) return SHOWN_HUD;
	if (sec.mapShownIdx >= 0 && !sec.logoForced) return SHOWN_MAP;
	return SHOWN_LOGO;
}

/* The game was restarted (after second_jump): the logo until the next map.
 * (Was in game.c's restart_game.) */
void second_restarted(void) {
	trace("restarted"); /* (the host reset the game, emu_reset: a replay resets it here) */
	sec.mapShownIdx = -1; /* back to the logo until the next map */
}

/* The logo changed (it finished extracting, or the player's own or a scraped
 * one was switched: GEN-11): every picture it's in is made again, the HUD's,
 * the card's, the fights' and the map's logo behind. (Was in game.c's
 * logo_refresh, after the app's own logo textures.) */
void second_logo_changed(void) {
	trace("logo_changed");
	hud.hudLogoBelowDone = hud.hudLogoDone = false; /* the HUD's pictures with it */
	hud.cardLogoMade = false;
	if (hud.combatMade) {
		/* and the fight picture (and the map picture's logo behind) */
		make_combat_canvas();
		sec.overDirty = true;
		if (sec.mapShownIdx >= 0) upload_map_frame(sec.mapShownIdx);
	}
}

/* The game's second screen taken down before second_open sets it up for
 * another game (a compilation: the game running in it changed, game.c's
 * ss_switch): the old game's HUD split and hidden sprites off, its capture and
 * pictures gone. */
void second_drop(void) {
	trace("drop");
	if (hud.hudOn) emu_hud_split(sec.emu, false, 0, NULL, 0, hud.hudPx); /* (the old game's HUD split off) */
	emu_hide_sprites(sec.emu, NULL, 0);
	sec.hideWhenProf = NULL;
	mapcap_destroy(sec.mc);
	sec.mc = NULL;
	map_frames_free();
}

/* The game's second screen taken down as the game ends: its capture, the
 * Pinball copies and the climb copy (their own cores and threads), the
 * pictures and the still. Call it before the game's emulator is closed: the
 * copies are taken from it. (Was inline in game.c's end of a game.) */
void second_close(void) {
	trace("close");
	mapcap_destroy(sec.mc);
	sec.mc = NULL;
	twin_close();
	climb_close();
	map_frames_free();
	free(sec.stillPx);
	sec.stillPx = NULL;
	sec.stillValid = false;
}

/* The game's logo at its own size (GEN-1) onto a cw x ch canvas: k canvas
 * pixels to a logo pixel (one GBA pixel each for an extracted logo),
 * centred, blended over what's there at `alpha` (255: opaque; the logo
 * alone, with nothing live to show, at half: LOGO_IDLE); with `top` (a HUD
 * above it), centred in the rows below that. A user's own logo too big
 * for that is shrunk to fit. False if there's no logo. (Was game.c's; the
 * picture comes from the app: sec.hooks.logoLoad.) */
bool logo_draw(uint32_t* px, int cw, int ch, int k, int top, int alpha) {
	int sub = 1;
	LogoPic lg;
	if (!sec.hooks.logoLoad(&lg, &sub)) return false;
	int room = ch - top;
	if (room <= 0) {
		/* (no room below a HUD that fills the screen: Mega Man Battle
		 * Network's custom screen, hud.rows GBA_H) */
		sec.hooks.logoFree(&lg);
		return false;
	}
	/* what's drawn: the logo as loaded, or averaged down to the canvas */
	const uint8_t* lpx = lg.px;
	int lw = lg.w, lh = lg.h, lpitch = lg.pitch;
	uint8_t* sm = NULL;
	int w = S_MAX(1, lw * k / sub), h = S_MAX(1, lh * k / sub);
	if (sub > k) {
		/* a scraped logo at full resolution, onto a canvas with fewer
		 * pixels per GBA pixel: averaged down to them (GEN-11) */
		sm = malloc((size_t) w * h * 4);
		if (sm) rgba_resample(lpx, lw, lh, lpitch, sm, w, h, w * 4), lpx = sm, lw = w, lh = h, lpitch = w * 4;
	}
	if (w > cw || h > room) {
		float sc = S_MIN((float) cw / lw, (float) room / lh);
		w = S_MAX(1, (int) (lw * sc)), h = S_MAX(1, (int) (lh * sc));
	}
	/* centred on the canvas, or with a HUD above it in the room below */
	int x0 = (cw - w) / 2, y0 = top ? top + (room - h) / 2 : (ch - h) / 2;
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = lpx + (size_t) (y * lh / h) * lpitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lw / w) * 4; /* RGBA32: bytes R, G, B, A */
			int a = p[3] * alpha / 255;
			if (!a) continue;
			uint32_t* o = &px[(y0 + y) * cw + x0 + x];
			uint32_t out = 0xFF000000;
			for (int c = 0; c < 3; ++c) {
				int bg = (*o >> (c * 8)) & 0xFF;
				out |= (uint32_t) (bg + (p[c] - bg) * a / 255) << (c * 8); /* ABGR8888 */
			}
			*o = out;
		}
	}
	free(sm);
	sec.hooks.logoFree(&lg);
	return true;
}

/* the game's screen is darker than `max` (of 255) on average: a fade, or black while a room loads */
static bool screen_under(const uint32_t* fb, int max) {
	long sum = 0, n = 0;
	for (int y = 0; y < GBA_H; y += 4)
		for (int x = 0; x < GBA_W; x += 4, ++n) {
			uint32_t v = fb[y * GBA_W + x];
			sum += ((v >> 16) & 0xFF) + ((v >> 8) & 0xFF) + (v & 0xFF);
		}
	return sum < (long) max * 3 * n;
}

/* profile.climb: the game's picture on the bottom screen and the level above
 * it on the top one, from the copy (climb.c; DKKS-3). Called after each frame
 * by second_step; the picture goes up through sec.hooks.presentGba. */
static void update_climb(void) {
	static int missing; /* frames in a level with no picture from the copy */
	static int between; /* frames of black since a level went, with the view still up */
	static bool blackTop;
	bool on = memprobes_match(sec.climbProf->climb.on, 3, sec.emu);
	if (on != sec.climbOn) {
		sec.climbOn = on;
		climb_reset();
		missing = between = 0;
		if (sec.debug) fprintf(stderr, "frame %llu: level %s\n", (unsigned long long) sec.frame, on ? "up" : "gone");
	}
	const uint32_t* top = on ? climb_picture() : NULL;
	if (top) {
		memcpy(tallTop, top, sizeof(tallTop));
		missing = between = 0;
		blackTop = false;
	} else if (on && (++missing <= 4 || (missing <= 90 && (screen_under(sec.emu->fb, 100) || climb_waiting())))) {
		/* (a level starting or a state loaded: the copy has none for a frame or two, longer while the camera
		 * settles in a room's fade-in; black meanwhile, the view kept) */
		if (!tallLive || blackTop) memset(tallTop, 0, sizeof(tallTop));
	} else if (!on && tallLive && ++between <= 120 && screen_under(sec.emu->fb, 6)) {
		/* (the game is black between two rooms: the view stays, not the layout flipping to the logo and back) */
		memset(tallTop, 0, sizeof(tallTop));
		blackTop = true;
	} else {
		if (tallLive) sec.mapShownIdx = -1;
		tallLive = false;
		blackTop = false;
		return;
	}
	memcpy(tallBot, sec.emu->fb, sizeof(tallBot));
	uint32_t* second = sec.cfg->swap ? tallTop : tallBot;
	sec.hooks.presentGba(second);
	if (!tallLive) fprintf(stderr, "frame %llu: level over both screens\n", (unsigned long long) sec.frame);
	tallLive = true;
	sec.mapShownIdx = 0;
}

/* The second screen's part of a frame, once the game's frame has run, in
 * this order: the map refresh, a logo-only game's screen copy, the Pinball
 * games' tall view (screens: how many there are now), the climb view (profile
 * climb), the HUD split, the fights' picture. (Was inline in game.c's frame loop.) */
void second_step(int screens) {
	trace("step %d %llu", screens, (unsigned long long) sec.frame);
	map_refresh_step();

	if (sec.snapProf) update_snapshot();
	if (tallProf) {
		TallEnv env = { screens, sec.frame, sec.cfg->swap, hud.hudPx, &sec.mapShownIdx, sec.hooks.presentGba };
		update_tall(&env);
	}
	if (sec.climbProf) update_climb();
	if (hud.hudProf) update_hud();
	if (hud.combatProf) hud_combat_step();
	trace_step_end();
}

/* The game's logo as a GBA screen would show it, with nothing live to show:
 * centred on dark grey, at half strength (LOGO_IDLE). A Game Boy / Game Boy
 * Color game: plain dark grey, no logo (docs/GAME-BOY.md, GB-3). A scraped
 * logo at full resolution (GEN-11): the same picture at sub pixels per GBA
 * pixel (GBA_W * sub x GBA_H * sub), to be scaled smoothly to the GBA
 * picture's place. A user's own logo bigger than that: no picture (the app
 * shows it as a high-resolution image). *px is malloc'd, the caller's to
 * free. IDLE_NONE: no logo (*sub as it was); IDLE_NOPIC: a logo, *sub set, no
 * picture; IDLE_PIC: the picture. (Was the pixel part of game.c's
 * make_logo_canvas.) */
int second_idle_picture(uint32_t** px, int* w, int* h, int* sub) {
	*px = NULL;
	if (emu_is_gb(sec.emu)) {
		uint32_t* grey = malloc((size_t) GBA_W * GBA_H * 4);
		*sub = 1;
		if (!grey) return IDLE_NOPIC;
		for (int i = 0; i < GBA_W * GBA_H; ++i) grey[i] = SCREEN_GREY;
		*px = grey, *w = GBA_W, *h = GBA_H;
		return IDLE_PIC;
	}
	LogoPic lg;
	int s = 1;
	if (!sec.hooks.logoLoad(&lg, &s)) return IDLE_NONE;
	*sub = s;
	bool big = lg.w > GBA_W * s || lg.h > GBA_H * s;
	sec.hooks.logoFree(&lg);
	if (big) return IDLE_NOPIC;
	int cw = GBA_W * s, ch = GBA_H * s;
	uint32_t* pic = malloc((size_t) cw * ch * 4);
	if (!pic) return IDLE_NOPIC;
	for (int i = 0; i < cw * ch; ++i) pic[i] = SCREEN_GREY;
	logo_draw(pic, cw, ch, s, 0, LOGO_IDLE);
	*px = pic, *w = cw, *h = ch;
	return IDLE_PIC;
}

/* ---- A game's start, as the app's game_run_inner does it (and a core's
 * retro_load_game): second_game_start before second_open (the frame count
 * and the map's animation start again), second_loop_start once the game's
 * first state is loaded, before the first frame (the map refresh starts
 * again: a capture as soon as it can be). sec.frame is the host's count,
 * one a pass of its loop. (Were inline in game_run_inner.) */
void second_game_start(void) {
	trace("game_start");
	sec.mc = NULL;
	sec.frame = 0;
	sec.mapAnimStart = 0; /* (the last game's count was further on) */
}

void second_loop_start(void) {
	trace("loop_start");
	sec.refresh = (typeof(sec.refresh)) { .sinceMapRequest = 1 << 20, .forceMap = true, .refreshAt = -1 }; /* (the map refresh starts again for this game) */
}

/* ---- The trace (tests, discussion #75): with second_trace_begin(dir), every
 * call the host makes into the module is written to dir/trace.txt in order,
 * with the files a replay needs beside it (the settings at each open, the
 * game's state and GBA DS's block at each load), and second_trace_shot(name)
 * writes what the second screen shows then (name.second: kind, dim, w, h and
 * the picture's RGBA bytes) and the game's picture (name.game: w, h, pixels),
 * both as of the last second_step. tools/second_driver.c replays a trace with
 * only the module and writes the same files: regress.py's "driver" cases
 * compare the two. The hooks are wrapped to keep the last picture each was
 * handed; what the host's hooks do is unchanged. */
#include <stdarg.h>
#ifndef __wasi__
#include <pthread.h>
static pthread_mutex_t traceLock = PTHREAD_MUTEX_INITIALIZER; /* (the frame may run on the app's frame-buffer thread) */
#define TRACE_LOCK() pthread_mutex_lock(&traceLock)
#define TRACE_UNLOCK() pthread_mutex_unlock(&traceLock)
#else
#define TRACE_LOCK()
#define TRACE_UNLOCK()
#endif
static FILE* traceF;
static char traceDir[700];
static int traceFiles;
static SecondHooks traceReal;
static HudHooks traceRealHud;
static uint32_t* traceMap;
static int traceMapW, traceMapH;
static uint32_t traceHud[HUD_CW * HUD_CH];
static uint32_t* traceCombat;
static int traceCombatW, traceCombatH;
static uint32_t traceGame[GBA_W * GBA_H];

static void trace(const char* fmt, ...) {
	if (!traceF) return;
	TRACE_LOCK();
	va_list ap;
	va_start(ap, fmt);
	vfprintf(traceF, fmt, ap);
	va_end(ap);
	fputc('\n', traceF);
	fflush(traceF);
	TRACE_UNLOCK();
}

static bool trace_write(const char* name, const void* a, size_t an, const void* b, size_t bn) {
	char path[800];
	snprintf(path, sizeof(path), "%s/%s", traceDir, name);
	FILE* f = fopen(path, "wb");
	if (!f) return false;
	bool ok = fwrite(a, 1, an, f) == an && (!bn || fwrite(b, 1, bn, f) == bn);
	return fclose(f) == 0 && ok;
}

static void trace_keep(uint32_t** dst, int* dw, int* dh, const uint32_t* px, int w, int h) {
	if (*dw != w || *dh != h) {
		free(*dst);
		*dst = malloc((size_t) w * h * 4);
		*dw = *dst ? w : 0, *dh = *dst ? h : 0;
	}
	if (*dst) memcpy(*dst, px, (size_t) w * h * 4);
}
static void trace_present(const uint32_t* px) {
	trace_keep(&traceMap, &traceMapW, &traceMapH, px, sec.mapW, sec.mapH);
	if (traceReal.present) traceReal.present(px);
}
static void trace_present_gba(const uint32_t* px) {
	trace_keep(&traceMap, &traceMapW, &traceMapH, px, GBA_W, GBA_H);
	if (traceReal.presentGba) traceReal.presentGba(px);
}
static void trace_present_hud(const uint32_t* canvas) {
	memcpy(traceHud, canvas, sizeof(traceHud));
	if (traceRealHud.presentHud) traceRealHud.presentHud(canvas);
}
static void trace_present_combat(const uint32_t* px) {
	trace_keep(&traceCombat, &traceCombatW, &traceCombatH, px, sec.mapW, sec.mapH);
	if (traceRealHud.presentCombat) traceRealHud.presentCombat(px);
}

/* a module call made outside second.c (toggle.c's toggle_press), for the trace */
void second_trace_call(const char* what) {
	trace("%s", what);
}

/* Start tracing into dir (made by the caller), once the hooks are set. */
bool second_trace_begin(const char* dir) {
	char path[800];
	snprintf(traceDir, sizeof(traceDir), "%s", dir);
	snprintf(path, sizeof(path), "%s/trace.txt", dir);
	traceF = fopen(path, "w");
	if (!traceF) return false;
	traceReal = sec.hooks, traceRealHud = hud.hooks;
	sec.hooks.present = trace_present, sec.hooks.presentGba = trace_present_gba;
	hud.hooks.presentHud = trace_present_hud, hud.hooks.presentCombat = trace_present_combat;
	fprintf(stderr, "module trace: %s\n", path);
	return true;
}

static void trace_open(int screens) {
	if (!traceF) return;
	char name[40];
	snprintf(name, sizeof(name), "config-%d.bin", ++traceFiles);
	trace_write(name, sec.cfg, sizeof(*sec.cfg), NULL, 0);
	trace("open %.4s %d %s %zu", sec.subCode[0] ? sec.subCode : "-", screens, name, sizeof(*sec.cfg)); /* (a Game Boy game has no code: "-") */
}

/* The game's state as the host loaded it: a copy of the file (a replay
 * loads that, with emu_state_load_file: a state from another mGBA version
 * doesn't come back byte for byte from emu_state_save's), else a snapshot. */
static char traceStateFile[60];
void second_trace_state_file(const char* path) {
	if (!traceF) return;
	FILE* f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	void* b = len > 0 ? malloc((size_t) len) : NULL;
	bool ok = b && fread(b, 1, (size_t) len, f) == (size_t) len;
	fclose(f);
	snprintf(traceStateFile, sizeof(traceStateFile), "state-%d.ss", ++traceFiles);
	if (!ok || !trace_write(traceStateFile, b, (size_t) len, NULL, 0)) traceStateFile[0] = 0;
	free(b);
}

static void trace_loaded(const uint8_t* block, size_t n) {
	if (!traceF) return;
	char sname[60], bname[40];
	if (traceStateFile[0]) {
		snprintf(sname, sizeof(sname), "%s", traceStateFile);
		traceStateFile[0] = 0;
	} else {
		size_t sz = emu_state_size(sec.emu);
		void* st = malloc(sz);
		if (!st || !emu_state_save(sec.emu, st)) {
			free(st);
			trace("loaded ? (the game's state not saved)");
			return;
		}
		snprintf(sname, sizeof(sname), "state-%d.bin", ++traceFiles);
		trace_write(sname, st, sz, NULL, 0);
		free(st);
	}
	snprintf(bname, sizeof(bname), "block-%d.bin", ++traceFiles);
	if (block) trace_write(bname, block, n, NULL, 0);
	trace("loaded %s %s %zu", sname, block ? bname : "-", block ? n : 0);
}

static void trace_step_end(void) {
	if (traceF && sec.emu && sec.emu->fb) memcpy(traceGame, sec.emu->fb, sizeof(traceGame));
}

/* What the screens show now (as of the last second_step), into dir/name.second
 * and dir/name.game (name: a screenshot's file name, its folder and extension
 * dropped). */
void second_trace_shot(const char* name) {
	if (!traceF) return;
	char base[200];
	const char* slash = strrchr(name, '/');
	snprintf(base, sizeof(base), "%s", slash ? slash + 1 : name);
	char* dot = strrchr(base, '.');
	if (dot) *dot = 0;
	bool dim = false;
	int kind = second_shown(&dim);
	const uint32_t* px = NULL;
	uint32_t* idle = NULL;
	int w = 0, h = 0;
	if (kind == SHOWN_COMBAT) px = traceCombat, w = traceCombatW, h = traceCombatH;
	else if (kind == SHOWN_HUD) px = traceHud, w = HUD_CW, h = HUD_CH;
	else if (kind == SHOWN_MAP) px = traceMap, w = traceMapW, h = traceMapH;
	else if (kind == SHOWN_LOGO) {
		int sub = 1;
		if (second_idle_picture(&idle, &w, &h, &sub) == IDLE_PIC) px = idle;
		else w = h = 0;
	}
	if (!px) w = h = 0;
	int32_t head[4] = { kind, dim, w, h };
	char fname[240];
	snprintf(fname, sizeof(fname), "%s.second", base);
	trace_write(fname, head, sizeof(head), px, (size_t) w * h * 4);
	free(idle);
	int32_t ghead[2] = { GBA_W, GBA_H };
	snprintf(fname, sizeof(fname), "%s.game", base);
	trace_write(fname, ghead, sizeof(ghead), traceGame, sizeof(traceGame));
	trace("shot %s", base);
}


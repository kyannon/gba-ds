/* The HUD split (profile .hud: part of the live game screen moved over to
 * the second screen, Mario Kart's race HUD, Super Mario Advance's status bar,
 * Advance Wars' conversations...) and the fights' picture (profile .combat),
 * without SDL (libretro core, discussion #75): moved out of game.c without
 * changing what they do. Their state is hud (hud.h); what puts their pictures
 * on screen, and the logo, are the app's (hud.hooks). */
#include "hud.h"
#include "second.h"
#include "toggle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "clamp.h"


HudState hud = { .cardScore = -1, .cardAway = 1000, .sideScore = -1, .heldArmed = true };

/* How bright each of the first 8 BG palettes is (the map's; the menus use
 * the others): games that fade the screen by scaling their palettes. */
static void bg_pal_levels(long out[8]) {
	for (int p = 0; p < 8; ++p) {
		out[p] = 0;
		for (int i = 0; i < 16; ++i) {
			uint16_t c = emu_read16(sec.emu, 0x05000000 + (p * 16 + i) * 2);
			out[p] += (c & 31) + ((c >> 5) & 31) + ((c >> 10) & 31);
		}
	}
}

/* How far the palettes have faded since ref, in 64ths: negative to black,
 * positive to white; the median over the palettes (one replaced, not
 * faded, doesn't count) */
static int bg_pal_fade(const long ref[8]) {
	long cur[8];
	bg_pal_levels(cur);
	int v[8], n = 0;
	for (int p = 0; p < 8; ++p) {
		if (ref[p] <= 0) continue;
		long full = 16 * 93;
		if (cur[p] < ref[p]) v[n++] = (int) -(64 - cur[p] * 64 / ref[p]);
		else v[n++] = ref[p] < full ? (int) ((cur[p] - ref[p]) * 64 / (full - ref[p])) : 0;
	}
	if (!n) return 0;
	for (int i = 1; i < n; ++i) {
		for (int j = i; j > 0 && v[j - 1] > v[j]; --j) {
			int t = v[j];
			v[j] = v[j - 1], v[j - 1] = t;
		}
	}
	return v[n / 2];
}

/* A checksum of a BG layer's tile graphics (its char block, the first 8 KB):
 * the same through a battle however the camera moves (its tile map, though,
 * is rewritten as the camera goes), another map's differ. */
static uint32_t map_tiles_sum(int bg) {
	uint16_t cnt = emu_read16(sec.emu, 0x04000008 + bg * 2);
	uint32_t base = 0x06000000 + ((cnt >> 2) & 3) * 0x4000, sum = 2166136261u;
	for (uint32_t a = 0; a < 0x2000; a += 4) sum = (sum ^ emu_read32(sec.emu, base + a)) * 16777619u;
	return sum;
}

static void hud_make_backdrop(void) {
	int h = S_CLAMP(hud.hudProf->hud.backdropRows ? hud.hudProf->hud.backdropRows : GBA_H, 1, GBA_H);
	if (hud.hudProf->hud.mapCnt) {
		/* a map layer taken mid-fade, or with the camera on the sky, makes
		 * a poor picture: try again in half a second, until it's mostly
		 * there and not dark (then it stays) */
		uint32_t clear = emu_hud_clear();
		long drawn = 0, luma = 0;
		for (int i = 0; i < GBA_W * GBA_H; ++i) {
			uint32_t c = hud.hudGrab[i];
			if ((c & 0xFFFFFF) == clear) continue;
			++drawn;
			luma += (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
		}
		long level = luma / S_MAX(drawn, 1) / 3;
		bool good = drawn > GBA_W * GBA_H / 2 && level > 60 && !frame_is_flat(hud.hudGrab);
		/* after a fade, only once the brightness has stopped rising */
		static long lastLevel;
		if (hud.hudSettle && good && labs(level - lastLevel) > 3) good = false;
		lastLevel = level;
		hud.hudSettle = !good; /* (a poor take: the next must also hold steady) */
		hud.hudRegrab = good ? 0 : hud.hudFrames + (hud.hudSettle ? 5 : 30);
		if (sec.debug) fprintf(stderr, "frame %llu: backdrop taken: %ld%% drawn, brightness %ld%s\n", (unsigned long long) sec.frame,
		                   drawn * 100 / (GBA_W * GBA_H), luma / S_MAX(drawn, 1) / 3, good ? "" : " (again later)");
		if (!good) return; /* keep the last good one meanwhile (or none: black) */
	}
	if (!hud.hudBackValid && hud.hudFadeEff) hud.hudFadeIn = 16; /* (after a fade out) */
	hud.hudBackValid = true;
	bg_pal_levels(hud.hudPalRef);
	if (sec.debug) {
		char path[1200];
		snprintf(path, sizeof(path), "%s/hud-backdrop.bmp", sec.cfg->stateDir);
		hud.hooks.saveBmp(hud.hudGrab, GBA_W, GBA_H, path);
		fprintf(stderr, "frame %llu: backdrop from %d rows (%s)\n", (unsigned long long) sec.frame, h, path);
	}
	if (!hud.hudBackValid) return;
	/* where the layers drew nothing: black, not the 'nothing here' colour */
	uint32_t clear = emu_hud_clear();
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		if ((hud.hudGrab[i] & 0xFFFFFF) == clear) hud.hudGrab[i] = 0xFF000000;
	}
	bool grey = hud.hudProf->hud.backdropGrey;
	float sc = S_MAX((float) HUD_CW / GBA_W, (float) HUD_CH / h);
	float ox = (GBA_W - HUD_CW / sc) / 2, oy = (h - HUD_CH / sc) / 2;
	for (int y = 0; y < HUD_CH; ++y) {
		float fy = oy + (y + 0.5f) / sc - 0.5f;
		int y0 = S_CLAMP((int) fy, 0, h - 1), y1 = S_MIN(y0 + 1, h - 1);
		float wy = S_CLAMP(fy - y0, 0.0f, 1.0f);
		for (int x = 0; x < HUD_CW; ++x) {
			float fx = ox + (x + 0.5f) / sc - 0.5f;
			int x0 = S_CLAMP((int) fx, 0, GBA_W - 1), x1 = S_MIN(x0 + 1, GBA_W - 1);
			float wx = S_CLAMP(fx - x0, 0.0f, 1.0f);
			uint32_t a = hud.hudGrab[y0 * GBA_W + x0], b = hud.hudGrab[y0 * GBA_W + x1];
			uint32_t c = hud.hudGrab[y1 * GBA_W + x0], d = hud.hudGrab[y1 * GBA_W + x1];
			uint32_t out = 0xFF000000;
			for (int sh = 0; sh < 24; sh += 8) {
				float top = ((a >> sh) & 0xFF) * (1 - wx) + ((b >> sh) & 0xFF) * wx;
				float bot = ((c >> sh) & 0xFF) * (1 - wx) + ((d >> sh) & 0xFF) * wx;
				float v = (top * (1 - wy) + bot * wy) * 0.32f; /* dimmed */
				out |= (uint32_t) v << sh;
			}
			if (grey) {
				unsigned r = out & 0xFF, g = (out >> 8) & 0xFF, b = (out >> 16) & 0xFF;
				unsigned l = (r * 77 + g * 150 + b * 29) >> 8;
				out = 0xFF000000u | l << 16 | l << 8 | l;
			}
			hud.hudBack[y * HUD_CW + x] = out;
		}
	}
}

/* The scene's backdrop (Mario Kart's podium): black (GEN-1; until v1.4.11
 * dark grey). (Until v1.3.2's
 * later test builds the logo was behind it, as large as fitted and
 * dimmed; at its own size, as GEN-1 has it, it's right behind the place
 * and the cup table, and they can't be read over it.) */
static void hud_make_logo_back(void) {
	hud.hudLogoDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hud.hudLogoBack[i] = SCREEN_LIVE;
}

/* Profile hud.logoBelow: the logo as on any other second screen (GEN-1),
 * centred, kept below the HUD's rows. */
static void hud_make_logo_below(void) {
	hud.hudLogoBelowDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hud.hudLogoBelow[i] = SCREEN_LIVE; /* (under the HUD: GEN-1) */
	const typeof(hud.hudProf->hud.held)* hp = &hud.hudProf->hud.held;
	if (hp->y1 && hud.heldN > 0) {
		/* (hud.held: the kept picture instead of the logo, at its own size) */
		for (int y = 0; y < hud.heldN && hp->at + y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t* o = &hud.hudLogoBelow[(hp->at + y) * 2 * HUD_CW + x * 2];
				o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = hud.heldPic[y * GBA_W + x] | 0xFF000000;
			}
		}
		/* (hud.held.over: the number from the capture over its own) */
		const typeof(hp->over)* ov = &hp->over;
		if (ov->x1 && hud.heldNumW > 0) {
			uint32_t fill = 0xFF000000 | (ov->fill >> 16 & 0xFF) | (ov->fill & 0xFF00) | (ov->fill & 0xFF) << 16;
			uint32_t key = (ov->key >> 16 & 0xFF) | (ov->key & 0xFF00) | (ov->key & 0xFF) << 16;
			for (int gy = ov->y0; gy <= ov->y1; ++gy) {
				for (int gx = ov->x0; gx <= ov->x1; ++gx) {
					int y = gy - hp->y0 + hp->at;
					if (gy < hp->y0 || y < 0 || y >= GBA_H || gx < 0 || gx >= GBA_W) continue;
					uint32_t c = fill;
					int sx = gx - (ov->x1 - hud.heldNumW + 1), sy = gy - ov->y0;
					if (sx >= 0 && sx < hud.heldNumW && sy >= 0 && sy < hud.heldNumH && (hud.heldNum[sy * hud.heldNumW + sx] & 0xFFFFFF) != key)
						c = hud.heldNum[sy * hud.heldNumW + sx] | 0xFF000000;
					uint32_t* o = &hud.hudLogoBelow[y * 2 * HUD_CW + gx * 2];
					o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
				}
			}
		}
		return;
	}
	int top = 0; /* below the HUD: its lowest row on the canvas */
	for (int m = 0; m < HUD_MOVES; ++m) {
		const HudMove* mv = &hud.hudProf->hud.moves[m];
		if (mv->toGame && !mv->how && !mv->vhow && mv->x0 == 0 && mv->x1 >= GBA_W - 1 && mv->y0 > 0) top = mv->y0 * 2;
	}
	if (hud.hudProf->hud.rows) top = hud.hudProf->hud.rows * 2;
	if (hud.hudProf->hud.logoTop) top = hud.hudProf->hud.logoTop * 2;
	if (hud.hudProf->hud.mapTitleRows && hud.mapTitleN) top += (hud.mapTitleN + 2) * 2; /* (and the level's name above it) */
	logo_draw(hud.hudLogoBelow, HUD_CW, HUD_CH, HUD_CW / GBA_W, top, 255);
}

static int rule_count(const SpriteRule* r, int max) {
	int n = 0;
	while (n < max && (r[n].x1 || r[n].tileMax)) ++n;
	return n;
}

/* The second screen from this frame's HUD (hudPx). While racing, the turn
 * sign goes back onto the game picture and the map is drawn larger. */
/* A sprite with this tile (the 1D character number) is shown */
static bool oam_has_tile(int tile) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(sec.emu, 0x07000000 + i * 8);
		if ((a0 & 0x300) == 0x200) continue; /* hidden */
		if ((emu_read16(sec.emu, 0x07000004 + i * 8) & 0x3FF) == tile) return true;
	}
	return false;
}

/* A sprite with a tile from tile to tileMax (0: tile alone) in this
 * palette is shown */
static bool oam_has_tile_pal(int tile, int tileMax, int pal, int prio1) {
	if (tileMax < tile) tileMax = tile;
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(sec.emu, 0x07000000 + i * 8);
		if ((a0 & 0x300) == 0x200) continue;
		uint16_t a2 = emu_read16(sec.emu, 0x07000004 + i * 8);
		if ((a2 & 0x3FF) >= tile && (a2 & 0x3FF) <= tileMax && a2 >> 12 == pal
		    && (!prio1 || ((a2 >> 10) & 3) == prio1 - 1)) return true;
	}
	return false;
}

/* A shown sprite matching r's box, tiles, priority, palettes and shapes (not the sprite window; `tagged` isn't read) */
static bool oam_rule_shown(const SpriteRule* r) {
	for (int i = 0; i < 128; ++i) {
		uint16_t a0 = emu_read16(sec.emu, 0x07000000 + i * 8), a1 = emu_read16(sec.emu, 0x07000002 + i * 8);
		uint16_t a2 = emu_read16(sec.emu, 0x07000004 + i * 8);
		if ((a0 & 0x300) == 0x200 || a0 >> 14 == 3 || ((a0 >> 10) & 3) == 2) continue;
		int x = (int) ((uint32_t) a1 << 23) >> 23, y = a0 & 0xFF, tile = a2 & 0x3FF, prio = (a2 >> 10) & 3, pal = a2 >> 12;
		if (y >= 160) y -= 256;
		if (x < r->x0 || x > r->x1 || y < r->y0 || y > r->y1 || tile < r->tileMin || tile > r->tileMax ||
		    (r->prio >= 0 && prio != r->prio) || (r->palMask && !(r->palMask >> pal & 1)) ||
		    (r->shapes && !(r->shapes >> ((a0 >> 14) * 4 + (a1 >> 14)) & 1))) continue;
		return true;
	}
	return false;
}

/* hud.card: this frame's card (hudPx) as a take: kept if its name (dark
 * pixels on rows y0..y1) is clear of both edges and nearer the centre
 * than the best so far. True once it's centred (within a pixel). */
static bool card_take(void) {
	const typeof(hud.hudProf->hud.card)* c = &hud.hudProf->hud.card;
	uint32_t clear = emu_hud_clear();
	int lo = GBA_W, hi = -1;
	for (int y = S_MAX(c->y0, 0); y <= S_MIN(c->y1, GBA_H - 1); ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t p = hud.hudPx[y * GBA_W + x];
			if ((p & 0xFFFFFF) == clear) continue;
			int luma = (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			if (luma < 3 * 96) lo = S_MIN(lo, x), hi = S_MAX(hi, x);
		}
	}
	if (hi < 0 || lo < 2 || hi > GBA_W - 3) return false;
	int off = abs(lo - (GBA_W - 1 - hi));
	if (hud.cardScore < 0 || off < hud.cardScore) {
		hud.cardScore = off;
		memcpy(hud.cardBest, hud.hudPx, sizeof(hud.cardBest));
	}
	return off <= 1;
}
/* hud.card.side: this frame's side part (hudPx) as its take: kept if
 * what's dark in it is clear of its top and bottom and nearer the centre
 * than the best so far. True if it was kept. */
static bool side_take(void) {
	const int* r = hud.hudProf->hud.card.side;
	uint32_t clear = emu_hud_clear();
	int top = -1, bot = -1;
	for (int y = S_MAX(r[1], 0); y <= S_MIN(r[3], GBA_H - 1); ++y) {
		for (int x = S_MAX(r[0], 0); x <= S_MIN(r[2], GBA_W - 1); ++x) {
			uint32_t p = hud.hudPx[y * GBA_W + x];
			if ((p & 0xFFFFFF) == clear) continue;
			int luma = (p & 0xFF) + ((p >> 8) & 0xFF) + ((p >> 16) & 0xFF);
			if (luma < 3 * 96) {
				if (top < 0) top = y;
				bot = y;
				break;
			}
		}
	}
	if (top < 0 || top < r[1] + 2 || bot > r[3] - 2) return false;
	int off = abs((top - r[1]) - (r[3] - bot));
	if (hud.sideScore >= 0 && off >= hud.sideScore) return false;
	hud.sideScore = off;
	memcpy(hud.sideBest, hud.hudPx, sizeof(hud.sideBest));
	if (sec.debug) fprintf(stderr, "frame %llu: title card's side part taken (rows %d-%d, %d off centre)\n", (unsigned long long) sec.frame, top, bot, off);
	return true;
}

/* hud.card: the best take as the backdrop (at its own size, black where
 * the card has nothing), with the side part's best take over it */
void card_backdrop(void) {
	uint32_t clear = emu_hud_clear();
	const int* r = hud.hudProf->hud.card.side;
	if (hud.sideScore >= 0) {
		for (int y = S_MAX(r[1], 0); y <= S_MIN(r[3], GBA_H - 1); ++y) {
			for (int x = S_MAX(r[0], 0); x <= S_MIN(r[2], GBA_W - 1); ++x) hud.cardBest[y * GBA_W + x] = hud.sideBest[y * GBA_W + x];
		}
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = hud.cardBest[y * GBA_W + x];
			c = (c & 0xFFFFFF) == clear && !hud.hudProf->hud.card.whole ? 0xFF000000 : c | 0xFF000000;
			uint32_t* o = &hud.hudBack[y * 2 * HUD_CW + x * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	hud.hudBackValid = true;
	hud.hudFadeIn = 0;
	bg_pal_levels(hud.hudPalRef);
	if (sec.debug) fprintf(stderr, "frame %llu: title card taken (%d pixels off centre)\n", (unsigned long long) sec.frame, hud.cardScore);
}

/* hud.card: no card taken yet: the logo, below the HUD's pieces; for a
 * whole-screen card, Sonic Advance 2's, centred, above the HUD along the
 * bottom */
static const uint32_t* card_logo(void) {
	static uint32_t cardLogo[HUD_CW * HUD_CH];
	if (!hud.cardLogoMade) {
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) cardLogo[i] = SCREEN_LIVE; /* (under the HUD: GEN-1) */
		hud.cardLogoMade = logo_draw(cardLogo, HUD_CW, HUD_CH, HUD_CW / GBA_W, hud.hudProf->hud.card.whole ? 0 : 48 * 2, 255);
	}
	return cardLogo;
}


/* An OAM entry's pixel (px, py inside it, as on the screen) is opaque */
static bool obj_opaque(const uint16_t* a, int w, int h, int px, int py) {
	if (a[1] & 0x1000) px = w - 1 - px; /* (flips) */
	if (a[1] & 0x2000) py = h - 1 - py;
	bool b8 = a[0] & 0x2000;
	bool oneD = emu_read16(sec.emu, 0x04000000) & 0x40;
	int tile = a[2] & 0x3FF, step = b8 ? 2 : 1;
	tile += (py / 8) * (oneD ? (w / 8) * step : 32) + (px / 8) * step;
	const uint8_t* v = (const uint8_t*) emu_vram(sec.emu) + 0x10000 + (tile & 0x3FF) * 32;
	int x = px & 7, y = py & 7;
	if (b8) return v[y * 8 + x] != 0;
	uint8_t byte = v[y * 4 + x / 2];
	return ((x & 1) ? byte >> 4 : byte & 15) != 0;
}

static void obj_size(const uint16_t* a, int* w, int* h) {
	static const uint8_t sz[3][4][2] = { { { 8, 8 }, { 16, 16 }, { 32, 32 }, { 64, 64 } },
	                                     { { 16, 8 }, { 32, 8 }, { 32, 16 }, { 64, 32 } },
	                                     { { 8, 16 }, { 8, 32 }, { 16, 32 }, { 32, 64 } } };
	int shape = S_MIN((a[0] >> 14) & 3, 2), size = (a[1] >> 14) & 3;
	*w = sz[shape][size][0], *h = sz[shape][size][1];
}

/* marks a sprite's opaque pixels inside the box (x0, y0, w x h) */
static void obj_mark(const uint16_t* a, uint8_t* mask, int x0, int y0, int bw, int bh) {
	int w, h;
	obj_size(a, &w, &h);
	int ox = a[1] & 0x1FF, oy = a[0] & 0xFF;
	if (ox >= 256) ox -= 512;
	if (oy >= 160) oy -= 256;
	for (int y = S_MAX(oy, y0); y < S_MIN(oy + h, y0 + bh); ++y) {
		for (int x = S_MAX(ox, x0); x < S_MIN(ox + w, x0 + bw); ++x) {
			if (obj_opaque(a, w, h, x - ox, y - oy)) mask[(y - y0) * bw + (x - x0)] = 1;
		}
	}
}

/* hud.mapAt.holdTile: after each racing frame. The game writes the sprites
 * for the next frame after one, so this frame's dots are the ones read
 * after the last frame; to be safe, those read now are left out too. A
 * frame with anything else in the map's box drew the map: its pixels
 * there, but the dots', go into mapHeld. */
static void map_hold_update(const typeof(hud.hudProf->hud)* h, uint32_t clear) {
	if (!hud.mapHeldOk) {
		for (int i = 0; i < GBA_W * GBA_H; ++i) hud.mapHeld[i] = clear;
		hud.mapHeldOk = true;
	}
	int x0 = S_MAX(h->map.x0, 0), y0 = S_MAX(h->map.y0, 0);
	int bw = S_MIN(h->map.x1, GBA_W - 1) - x0 + 1, bh = S_MIN(h->map.y1, GBA_H - 1) - y0 + 1;
	if (bw <= 0 || bh <= 0) return;
	static uint8_t mask[GBA_W * GBA_H];
	memset(mask, 0, (size_t) bw * bh);
	for (int d = 0; d < hud.nMapDots; ++d) obj_mark(hud.mapDots[d], mask, x0, y0, bw, bh);
	hud.nMapDots = 0;
	for (int i = 0; i < 128; ++i) {
		uint16_t a[3];
		for (int k = 0; k < 3; ++k) a[k] = emu_read16(sec.emu, 0x07000000 + i * 8 + k * 2);
		if ((a[0] & 0x300) == 0x200 || (a[0] & 0x100)) continue; /* hidden, rotated */
		if ((a[2] & 0x3FF) != h->mapAt.dotTile || hud.nMapDots >= 32) continue;
		memcpy(hud.mapDots[hud.nMapDots++], a, sizeof(a));
		obj_mark(a, mask, x0, y0, bw, bh);
	}
	int other = 0;
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			if (!mask[y * bw + x] && (hud.hudPx[(y + y0) * GBA_W + x + x0] & 0xFFFFFF) != clear) ++other;
		}
	}
	if (other < 16) { /* (the dots alone: the map isn't drawn this frame) */
		++hud.mapUndrawn;
		return;
	}
	hud.mapUndrawn = 0;
	for (int y = 0; y < bh; ++y) {
		for (int x = 0; x < bw; ++x) {
			if (!mask[y * bw + x]) hud.mapHeld[(y + y0) * GBA_W + x + x0] = hud.hudPx[(y + y0) * GBA_W + x + x0];
		}
	}
}

static void hud_compose(void) {
	const typeof(hud.hudProf->hud)* h = &hud.hudProf->hud;
	uint32_t clear = emu_hud_clear();
	bool racing = !hud.hudScene && memprobes_match(h->layout, 4, sec.emu);
	{
		static int lastRacing = -1;
		if (sec.debug && racing != lastRacing) fprintf(stderr, "frame %llu: HUD layout %s\n", (unsigned long long) sec.frame, racing ? "racing" : "plain");
		lastRacing = racing;
	}
	/* the map's box: grown over the race's first frames, then kept */
	if (racing && h->map.x1 && (hud.hudFrames <= 8 || h->mapAt.scale)) {
		for (int y = h->map.y0; y <= h->map.y1; ++y) {
			for (int x = h->map.x0; x <= h->map.x1; ++x) {
				if ((hud.hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (!hud.mapFound) hud.mapX0 = hud.mapX1 = x, hud.mapY0 = hud.mapY1 = y, hud.mapFound = true;
				hud.mapX0 = S_MIN(hud.mapX0, x), hud.mapX1 = S_MAX(hud.mapX1, x);
				hud.mapY0 = S_MIN(hud.mapY0, y), hud.mapY1 = S_MAX(hud.mapY1, y);
			}
		}
	}
	bool bigMap = racing && hud.mapFound;
	int bx0 = S_MAX(hud.mapX0 - 2, h->map.x0), by0 = S_MAX(hud.mapY0 - 2, h->map.y0);
	int bx1 = S_MIN(hud.mapX1 + 2, h->map.x1), by1 = S_MIN(hud.mapY1 + 2, h->map.y1);
	if (racing && h->map.x1 && h->mapAt.scale) {
		/* (hud.mapAt: what the map has drawn so far this race, grown frame
		 * by frame, as it may blink; the whole box until then) */
		bigMap = true;
		if (hud.mapFound) bx0 = hud.mapX0, by0 = hud.mapY0, bx1 = hud.mapX1, by1 = hud.mapY1;
		else bx0 = h->map.x0, by0 = h->map.y0, bx1 = h->map.x1, by1 = h->map.y1;
	}
	/* (hud.mapAt.holdTile: the map held between the frames it's drawn in) */
	bool hold = bigMap && h->mapAt.holdTile;
	if (hold) map_hold_update(h, clear);
	/* the sign: only when nothing else of the HUD is on its rows nearby */
	bool sign = false;
	if (racing && h->sign.x1) {
		bool inside = false, beside = false;
		for (int y = h->sign.y0; y <= h->sign.y1 && !beside; ++y) {
			for (int x = S_MAX(0, h->sign.x0 - 40); x <= S_MIN(GBA_W - 1, h->sign.x1 + 40); ++x) {
				if ((hud.hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (x >= h->sign.x0 && x <= h->sign.x1) inside = true;
				else beside = true;
			}
		}
		sign = inside && !beside;
	}
	if (sign) {
		for (int y = h->sign.y0; y <= h->sign.y1; ++y) {
			for (int x = h->sign.x0; x <= h->sign.x1; ++x) {
				uint32_t c = hud.hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) != clear) sec.emu->fb[y * GBA_W + x] = c;
			}
		}
	}
	uint32_t plain = h->mapCnt ? (hud.hudFadeEff == 2 ? 0xFFFFFFFF : 0xFF000000) : h->black ? SCREEN_LIVE : 0xFF1A1010;
	if (hud.hudScene) {
		if (!hud.hudLogoDone) hud_make_logo_back();
		memcpy(hud.hudCanvas, hud.hudLogoBack, sizeof(hud.hudCanvas));
	} else if (hud.hudAlt && h->alt.rows && hud.hudHeldOk) {
		/* (hud.alt.rows: the last picture below the live rows) */
		memcpy(hud.hudCanvas, hud.hudHeld, sizeof(hud.hudCanvas));
		for (int i = 0; i < h->alt.rows * 2 * HUD_CW; ++i) hud.hudCanvas[i] = plain;
	} else if ((hud.hudAlt ? h->alt.mapBelow : h->mapBelow) && sec.mapShownIdx >= 0 && sec.mapW == GBA_W && sec.mapH == GBA_H && map_px(sec.mapShownIdx)) {
		/* (hud.mapBelow, hud.alt.mapBelow: the map picture behind, at the game's size) */
		const uint32_t* mp = map_px(sec.mapShownIdx);
		for (int y = 0; y < GBA_H; ++y) {
			for (int x = 0; x < GBA_W; ++x) {
				uint32_t* o = &hud.hudCanvas[y * 2 * HUD_CW + x * 2];
				o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = mp[y * GBA_W + x] | 0xFF000000;
			}
		}
	} else if (h->logoBelow || h->logoBehind) {
		if (!hud.hudLogoBelowDone) hud_make_logo_below();
		memcpy(hud.hudCanvas, hud.hudLogoBelow, sizeof(hud.hudCanvas));
	} else if (hud.hudBackValid) {
		memcpy(hud.hudCanvas, hud.hudBack, sizeof(hud.hudCanvas));
		/* the game fading the whole screen to black or white: the
		 * backdrop too. By BLDCNT (every BG layer, darken or brighten, by
		 * BLDY, in 16ths) */
		uint16_t bld = emu_read16(sec.emu, 0x04000050);
		int fade = S_MIN(emu_read16(sec.emu, 0x04000054) & 31, 16), eff = (bld >> 6) & 3;
		if (!((bld & 0x0F) == 0x0F && (eff == 2 || eff == 3) && fade)) fade = 0;
		/* (not behind a whole-screen card: it stays lit through the act.
		 * Sonic Advance 2 darkens the level with BLDCNT under the card's
		 * windows as it wipes off, then brings in the level's palettes) */
		if (h->card.whole) fade = 0;
		/* or by scaling its palettes (to black, or white): against how
		 * bright they were as the backdrop was taken (in 64ths) */
		int pf = fade || h->card.whole ? 0 : bg_pal_fade(hud.hudPalRef);
		if (pf <= -2) eff = 3, fade = -pf;
		else if (pf >= 2) eff = 2, fade = pf;
		else fade *= 4;
		fade = S_CLAMP(fade, 0, 64);
		/* faded out all the way: that backdrop is done (the next scene's
		 * comes in as the game fades in, or after: then fading in too) */
		if (fade >= 62 && h->mapCnt) {
			hud.hudBackValid = false;
			hud.hudFadeEff = eff;
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) hud.hudCanvas[i] = eff == 2 ? 0xFFFFFFFF : 0xFF000000;
			fade = 0;
		} else if (hud.hudFadeIn > 0) {
			/* a new backdrop, faded in over 16 frames from how the last
			 * scene faded out, or less if the game's own fade is less */
			int in = hud.hudFadeIn-- * 4;
			if (in > fade || eff != hud.hudFadeEff) eff = hud.hudFadeEff, fade = in;
		}
		if (fade) {
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) {
				uint32_t c = hud.hudCanvas[i], o = 0xFF000000;
				for (int k = 0; k < 24; k += 8) {
					int v = (c >> k) & 0xFF;
					v = eff == 3 ? v - v * fade / 64 : v + (255 - v) * fade / 64;
					o |= (uint32_t) v << k;
				}
				hud.hudCanvas[i] = o;
			}
		}
	} else if (h->card.tile) {
		memcpy(hud.hudCanvas, card_logo(), sizeof(hud.hudCanvas));
	} else {
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) hud.hudCanvas[i] = plain;
	}
	/* moved pieces (the scene's, or racing's): how far each goes this frame */
	const HudMove* movesIn = hud.hudScene ? h->scene.moves : racing ? h->moves : NULL;
	/* (hud.alt.moves: its own, on the other layer) */
	if (movesIn == h->moves && hud.hudAlt && (h->alt.moves[0].how || h->alt.moves[0].vhow || h->alt.moves[0].toGame)) movesIn = h->alt.moves;
	if (hud.hudAlt && h->alt.rows) movesIn = NULL; /* (hud.alt.rows: the game draws the rest itself) */
	/* (hud.moves' `always` pieces, where no other moves are) */
	HudMove movesA[HUD_MOVES];
	if (!movesIn && !hud.hudScene) {
		int n = 0;
		for (int m = 0; m < HUD_MOVES; ++m)
			if (h->moves[m].always) movesA[n++] = h->moves[m];
		if (n) {
			memset(&movesA[n], 0, sizeof(HudMove) * (HUD_MOVES - n));
			movesIn = movesA;
		}
	}
	/* with a BG mosaic on (fading out through a pipe), the boxes' top and
	 * bottom go to the blocks' edges: a block across a box's edge is drawn
	 * from its first row, so it goes with the box above */
	HudMove movesM[HUD_MOVES];
	const HudMove* moves = movesIn;
	int mosV = ((emu_io16(sec.emu, 0x4C) >> 4) & 15) + 1;
	if (movesIn && mosV > 1) {
		for (int m = 0; m < HUD_MOVES; ++m) {
			movesM[m] = movesIn[m];
			if (movesM[m].y0 > 0) movesM[m].y0 = (movesM[m].y0 + mosV - 1) / mosV * mosV;
			if (movesM[m].y1 < GBA_H - 1) movesM[m].y1 = (movesM[m].y1 + mosV) / mosV * mosV - 1;
		}
		moves = movesM;
	}
	/* hud.mapTitleRows: the level's name from the map picture on top, the
	 * HUD that far down */
	int titleDy = h->mapTitleRows && hud.mapTitleN ? hud.mapTitleN + 2 : 0; /* (2 rows apart) */
	for (int y = 0; y < titleDy; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t* o = &hud.hudCanvas[y * 2 * HUD_CW + x * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = hud.mapTitle[y * GBA_W + x];
		}
	}
	int rowsNow = hud.hudAlt && h->alt.rows ? h->alt.rows : h->rows;
	int moveDx[HUD_MOVES] = { 0 }, moveDy[HUD_MOVES] = { 0 };
	bool moving[HUD_MOVES] = { false };
	int pieceLo[HUD_MOVES] = { 0 }, pieceTop[HUD_MOVES] = { 0 };
	for (int m = 0; moves && m < HUD_MOVES; ++m) {
		const HudMove* mv = &moves[m];
		if (!mv->how && !mv->vhow && !mv->toGame) continue;
		if (mv->ifTile && !oam_has_tile(mv->ifTile)) continue;
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = S_MAX(mv->y0, 0); y <= S_MIN(mv->y1, GBA_H - 1); ++y) {
			for (int x = S_MAX(mv->x0, 0); x <= S_MIN(mv->x1, GBA_W - 1); ++x) {
				if ((hud.hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = S_MIN(lo, x), hi = S_MAX(hi, x), top = S_MIN(top, y), bot = S_MAX(bot, y);
			}
		}
		if (hi < 0) continue;
		moving[m] = true;
		moveDx[m] = mv->how == 'C' ? mv->x - (lo + hi) / 2 : mv->how == 'R' ? mv->x - hi : mv->how == 'L' ? mv->x - lo : 0;
		moveDy[m] = mv->vhow == 'M' ? mv->y - (top + bot) / 2 : mv->vhow == 'B' ? mv->y - bot : mv->vhow == 'T' ? mv->y - top : 0;
		pieceLo[m] = lo, pieceTop[m] = top;
	}
	/* `under`: below whatever stays in its corner (what's above its box in
	 * its columns, outside the other moving pieces), lined up with it */
	for (int m = 0; moves && m < HUD_MOVES; ++m) {
		const HudMove* mv = &moves[m];
		if (!moving[m] || !mv->under) continue;
		int ulo = GBA_W, ubot = -1;
		for (int y = 0; y < S_MIN(mv->y0, GBA_H); ++y) {
			for (int x = S_MAX(mv->x0, 0); x <= S_MIN(mv->x1, GBA_W - 1); ++x) {
				if ((hud.hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				bool other = false;
				for (int k = 0; k < HUD_MOVES && !other; ++k) {
					const HudMove* o = &moves[k];
					other = k != m && moving[k] && x >= o->x0 && x <= o->x1 && y >= o->y0 && y <= o->y1;
				}
				if (!other) ulo = S_MIN(ulo, x), ubot = S_MAX(ubot, y);
			}
		}
		if (ubot >= 0) moveDx[m] = ulo - pieceLo[m], moveDy[m] = ubot + 1 + mv->under - pieceTop[m];
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = hud.hudPx[y * GBA_W + x];
			if ((c & 0xFFFFFF) == clear) continue;
			int nx = x, ny = y;
			bool toGame = false;
			for (int m = 0; m < HUD_MOVES; ++m) {
				const HudMove* mv = &moves[m];
				if (moving[m] && x >= mv->x0 && x <= mv->x1 && y >= mv->y0 && y <= mv->y1) {
					nx = x + moveDx[m], ny = y + moveDy[m];
					toGame = mv->toGame;
					break;
				}
			}
			if (nx < 0 || nx >= GBA_W || ny < 0 || ny >= GBA_H) continue;
			if (toGame) {
				sec.emu->fb[ny * GBA_W + nx] = c; /* back onto the game picture */
				continue;
			}
			if (rowsNow && y >= rowsNow) continue; /* (hud.rows: the game draws the rest) */
			if (sign && x >= h->sign.x0 && x <= h->sign.x1 && y >= h->sign.y0 && y <= h->sign.y1) continue;
			if (bigMap && x >= bx0 && x <= bx1 && y >= by0 && y <= by1) continue;
			ny += titleDy;
			if (ny >= GBA_H) continue;
			uint32_t* o = &hud.hudCanvas[ny * 2 * HUD_CW + nx * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	if (bigMap) {
		/* 2x if it fits comfortably right of the faces and below the top
		 * row (about 200 x 128 HUD pixels), else 1.5x; centred there */
		int w = bx1 - bx0 + 1, hh = by1 - by0 + 1;
		int q = (w * 2 <= 180 && hh * 2 <= 120) ? 4 : 3; /* canvas pixels per HUD pixel */
		int cx = 139 * 2, cy = 94 * 2;
		if (h->mapAt.scale) q = h->mapAt.scale * 2, cx = h->mapAt.cx * 2, cy = h->mapAt.bottom * 2 + 2 - hh * q / 2;
		int tx = S_CLAMP(cx - w * q / 2, 0, HUD_CW - w * q), ty = S_CLAMP(cy - hh * q / 2, 0, HUD_CH - hh * q);
		for (int y = by0; y <= by1; ++y) {
			for (int x = bx0; x <= bx1; ++x) {
				uint32_t c = hud.hudPx[y * GBA_W + x];
				/* (the dots on the map held; not once the game's stopped
				 * drawing it, as a Grand Prix ends) */
				if (hold && hud.mapUndrawn <= 3 && (c & 0xFFFFFF) == clear) c = hud.mapHeld[y * GBA_W + x];
				if ((c & 0xFFFFFF) == clear) continue;
				for (int j = 0; j < q; ++j) {
					uint32_t* o = &hud.hudCanvas[(ty + (y - by0) * q + j) * HUD_CW + tx + (x - bx0) * q];
					for (int i = 0; i < q; ++i) o[i] = c;
				}
			}
		}
	}
}

/* The BG layers whose tile maps mostly use palettes below pal (shown or
 * not: one turned on next frame is filled already) */
static unsigned effect_bgs(int pal) {
	const uint16_t* vram = emu_vram(sec.emu);
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) {
		uint16_t cnt = emu_read16(sec.emu, 0x04000008 + i * 2);
		int n = 1024 * ((cnt >> 14) == 0 ? 1 : (cnt >> 14) == 3 ? 4 : 2);
		const uint16_t* map = vram + ((cnt >> 8) & 31) * 1024;
		int low = 0, high = 0;
		for (int j = 0; j < n && ((cnt >> 8) & 31) * 1024 + j < 0x8000; ++j) {
			if (!(map[j] & 0x3FF)) continue;
			if ((map[j] >> 12) < pal) ++low;
			else ++high;
		}
		if (low > high) m |= 1u << i;
	}
	return m;
}

/* hud.overMap: this frame's HUD pieces into pieces[]; true if one changed. */
static bool hud_take_pieces(const typeof(hud.hudProf->hud)* h) {
	uint32_t clear = emu_hud_clear();
	bool changed = false, seen[4] = { false };
	int found[4][4];
	if (h->findPieces) h->findPieces(sec.emu, hud.hudPx, clear, found);
	for (int m = 0; m < 4; ++m) {
		const HudMove* mv = &h->moves[m];
		Piece* pc = &hud.pieces[m];
		if (!mv->how && !mv->vhow && !mv->asIs) continue;
		int bx0 = mv->x0, by0 = mv->y0, bx1 = mv->x1, by1 = mv->y1;
		if (h->findPieces) bx0 = found[m][0], by0 = found[m][1], bx1 = found[m][2], by1 = found[m][3];
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = S_MAX(by0, 0); y <= S_MIN(by1, GBA_H - 1); ++y) {
			for (int x = S_MAX(bx0, 0); x <= S_MIN(bx1, GBA_W - 1); ++x) {
				if ((hud.hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = S_MIN(lo, x), hi = S_MAX(hi, x), top = S_MIN(top, y), bot = S_MAX(bot, y);
			}
		}
		if (hi < 0) {
			/* not there: the last one stays (one that follows another goes
			 * when that one is there without it) */
			if (mv->follow && pc->valid && seen[mv->follow - 1]) pc->valid = false, changed = true;
			if (mv->settle) pc->still = 0;
			if (mv->dropAfter && pc->valid && ++pc->gone >= mv->dropAfter) pc->valid = false, changed = true;
			continue;
		}
		seen[m] = true;
		pc->gone = 0;
		if (!mv->latch && (pc->sx != lo || pc->sy != top)) changed = true; /* (its followers move) */
		if (!mv->latch) pc->sx = lo, pc->sy = top;
		if (pc->atBottom != (bot == GBA_H - 1)) pc->atBottom = bot == GBA_H - 1, changed = true;
		static uint32_t cut[GBA_W * GBA_H];
		int w = hi - lo + 1, hh = bot - top + 1;
		for (int y = 0; y < hh; ++y) {
			for (int x = 0; x < w; ++x) {
				uint32_t c = hud.hudPx[(top + y) * GBA_W + lo + x];
				cut[y * w + x] = (c & 0xFFFFFF) == clear ? 0 : c | 0xFF000000;
			}
		}
		size_t n = (size_t) w * hh * 4;
		if (mv->latch) {
			/* only for a new value, once it's held still for 8 frames (in
			 * the same place, the same size: it may animate, as the CO
			 * power meter does when it's full) */
			uint32_t key = emu_read8(sec.emu, mv->latch);
			if (pc->valid && key == pc->key && !pc->stale) continue;
			bool same = pc->cw == w && pc->ch == hh && pc->cx == lo && pc->cy == top;
			pc->still = same ? pc->still + 1 : 0;
			pc->cw = w, pc->ch = hh, pc->cx = lo, pc->cy = top;
			if (pc->still < 8) continue;
			pc->key = key;
		} else if (pc->valid && pc->w == w && pc->h == hh && !memcmp(pc->px, cut, n)) {
			continue;
		} else if (mv->settle) {
			bool same = pc->cw == w && pc->ch == hh && !memcmp(pc->cand, cut, n);
			pc->still = same ? pc->still + 1 : 0;
			pc->cw = w, pc->ch = hh;
			memcpy(pc->cand, cut, n);
			if (pc->still < mv->settle) continue;
		}
		pc->valid = true;
		pc->stale = false;
		pc->w = w, pc->h = hh;
		memcpy(pc->px, cut, n);
		changed = true;
		if (mv->clears) {
			for (int o = 0; o < 4; ++o) {
				if (o != m) hud.pieces[o].valid = false, hud.pieces[o].still = 0;
			}
		}
	}
	return changed;
}

/* After each frame: show the HUD it drew, and decide whether the next
 * one is split. A frame is shown only if it was split and still looks like
 * racing (the first pause-menu frame is split too, and is dropped). */

void update_hud(void) {
	const typeof(hud.hudProf->hud)* h = &hud.hudProf->hud;
	/* hud.held: taken from the game screen the first frame it's shown in place */
	if (h->held.y1) {
		if (!memprobes_match(h->held.when, 3, sec.emu)) {
			hud.heldArmed = true;
		} else if (hud.heldArmed) {
			int n = 0;
			while (n < 8 && (h->held.probes[n].x || h->held.probes[n].y || h->held.probes[n].rgb)) ++n;
			if (probes_hit(sec.emu->fb, h->held.probes, h->held.tol) >= (h->held.min ? h->held.min : n)) {
				hud.heldArmed = false;
				hud.heldN = S_MIN(h->held.y1, GBA_H - 1) - h->held.y0 + 1;
				memcpy(hud.heldPic, sec.emu->fb + h->held.y0 * GBA_W, (size_t) hud.heldN * GBA_W * 4);
				hud.heldNumW = 0; /* (its own number is the latest till the next capture) */
				hud.hudLogoBelowDone = false;
				if (sec.debug) fprintf(stderr, "frame %llu: picture kept for under the HUD\n", (unsigned long long) sec.frame);
			}
		}
	}
	/* a conversation leaves the game screen only once there's a picture
	 * on the second screen to put it on */
	if (h->band) emu_hud_band(true, sec.mapShownIdx < 0, h->band == BAND_WATCH);
	if (h->mirror[0].addr || h->mirrorOr[0].addr) {
		bool mirMain = h->mirror[0].addr && memprobes_match(h->mirror, 2, sec.emu);
		bool mir = mirMain || (h->mirrorOr[0].addr && memprobes_match(h->mirrorOr, 2, sec.emu));
		if (mir) {
			/* here, the picture from before (none after loading a state in
			 * the middle of it: left as it is; shown instead of emu.fb,
			 * which can't be written over: the renderer redraws only the
			 * lines that change) */
			if (!hud.mirrorOn && sec.debug) fprintf(stderr, "frame %llu: the screen mirrored\n", (unsigned long long) sec.frame);
			hud.mirrorOn = hud.hudShown = true;
			if (mirMain && h->alt.on[0].addr && memprobes_match(h->alt.on, (int) (sizeof h->alt.on / sizeof h->alt.on[0]), sec.emu)) {
				/* hud.alt's probes too (a menu over the paused world): its
				 * layers alone, split as usual below; until the split's
				 * on, the background */
				if (!hud.hudOn || !hud.hudAlt) {
					if (h->logoBelow || h->logoBehind) {
						if (!hud.hudLogoBelowDone) hud_make_logo_below();
						memcpy(hud.hudCanvas, hud.hudLogoBelow, sizeof(hud.hudCanvas));
					} else {
						for (int i = 0; i < HUD_CW * HUD_CH; ++i) hud.hudCanvas[i] = 0xFF000000;
					}
					hud.hooks.presentHud(hud.hudCanvas);
				}
			} else {
				/* the whole screen over there */
				for (int y = 0; y < GBA_H; ++y) {
					for (int x = 0; x < GBA_W; ++x) {
						uint32_t* o = &hud.hudCanvas[y * 2 * HUD_CW + x * 2];
						o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = sec.emu->fb[y * GBA_W + x] | 0xFF000000;
					}
				}
				hud.hooks.presentHud(hud.hudCanvas);
				/* (and the split off, if it was on) */
				if (hud.hudOn) emu_hud_split(sec.emu, false, 0, NULL, 0, hud.hudPx), hud.hudOn = false;
				return;
			}
		} else {
			memcpy(hud.mirrorHeld, sec.emu->fb, sizeof(hud.mirrorHeld));
			hud.mirrorHave = true;
			if (hud.mirrorOn) {
				hud.mirrorOn = hud.hudShown = false;
				if (sec.debug) fprintf(stderr, "frame %llu: the screen mirrored: off\n", (unsigned long long) sec.frame);
			}
		}
	}
	bool main = memprobes_match(h->on, 4, sec.emu);
	if (rule_count(&h->onWith, 1)) {
		bool with = main && oam_rule_shown(&h->onWith);
		if (with) hud.onWithAt = sec.frame;
		if (main && (!hud.hudOn || hud.hudAlt)) main = with;
		else if (main && sec.frame - hud.onWithAt > (unsigned long long) h->onLost) {
			main = false;
			if (sec.debug) fprintf(stderr, "frame %llu: HUD off: no onWith sprite\n", (unsigned long long) sec.frame);
		}
	}
	bool altOn = !main && h->alt.on[0].addr && memprobes_match(h->alt.on, (int) (sizeof h->alt.on / sizeof h->alt.on[0]), sec.emu);
	bool race = main || altOn || (hud.hudOn && h->stay[0].addr && memprobes_match(h->stay, 2, sec.emu));
	/* hud.offAfter: off from when it matches (a battle's results) until
	 * on[0] stops matching (the battle's over) */
	if (h->offAfter[0].addr) {
		if (!memprobes_match(h->on, 1, sec.emu)) {
			if (hud.hudOffLatch && sec.debug) fprintf(stderr, "frame %llu: HUD held off: done\n", (unsigned long long) sec.frame);
			hud.hudOffLatch = false;
		} else if (!hud.hudOffLatch && memprobes_match(h->offAfter, 2, sec.emu)) {
			hud.hudOffLatch = true;
			if (sec.debug) fprintf(stderr, "frame %llu: HUD held off\n", (unsigned long long) sec.frame);
		}
		if (hud.hudOffLatch) race = altOn = false, main = false;
	}
	/* the HUD's other layer (alt): kept while it stays on through a fade */
	bool alt = main ? false : altOn ? true : hud.hudAlt;
	/* the map layer: which BG has it (none: not on a map) */
	int mapBg = -1;
	bool world = false;
	for (int i = 0; h->mapCnt && i < 4; ++i) {
		uint16_t cnt = emu_read16(sec.emu, 0x04000008 + i * 2);
		if (cnt == h->mapCnt) mapBg = i;
		else if (h->world.mapCnt && cnt == h->world.mapCnt) mapBg = i, world = true;
	}
	if (h->mapCnt && mapBg < 0) race = false;
	if (race && h->offTile && oam_has_tile(h->offTile)) race = false;
	if (h->drop) {
		/* (only left out of the game screen: the second screen keeps
		 * its map picture or logo) */
	} else if (h->overMap) {
		/* into the map picture: each piece as last seen, kept while the
		 * HUD is hidden (menus, moves, fights) until it shows again; all
		 * gone off the battle map (the first `on` probe) */
		bool changed = false;
		if (!memprobes_match(h->on, 1, sec.emu)) {
			for (int m = 0; m < 4; ++m) changed |= hud.pieces[m].valid, hud.pieces[m].valid = false;
		} else if (hud.hudOn && race) {
			changed = hud_take_pieces(h);
		} else {
			/* the split's off: a piece that goes alone (a conversation)
			 * isn't up any more */
			for (int m = 0; m < 4; ++m) {
				if (h->moves[m].alone && hud.pieces[m].valid) hud.pieces[m].valid = false, changed = true;
			}
		}
		bool any = hud.pieces[0].valid || hud.pieces[1].valid || hud.pieces[2].valid || hud.pieces[3].valid;
		if (changed || any != sec.overOn) {
			sec.overOn = any;
			sec.overDirty = true;
			if (sec.mapShownIdx >= 0) {
				upload_map_frame(sec.mapShownIdx);
			} else if (h->logoUnder && hud.combatPx && hud.hooks.hasCombat()) {
				/* no map picture yet (the other armies' turn, after loading
				 * a state): a piece that goes alone (a conversation) on the
				 * logo picture itself */
				const uint32_t* px = hud_over_map(hud.combatPx);
				if (!hud.overAlone) px = hud.combatPx;
				hud.hooks.presentCombat(px);
			}
		}
	} else if (hud.hudOn && race && hud.hudCard) {
		/* the title card: a take a frame (once the split draws the card),
		 * the backdrop once it's centred; on the second screen from then */
		if (h->card.whole) {
			/* (the whole game screen, once the card has settled) */
			if (hud.cardScore < 0 && hud.hudFrames >= S_MAX(2, h->card.at)) {
				/* (card.hide / card.poke: drawn again, Sonic Advance 3's
				 * band on black without the HUD and the characters) */
				int nh = rule_count(h->card.hide, 6);
				if (nh || h->card.poke.addr) snapshot_hiding_sprites_poke(h->card.hide, nh, NULL, 0, hud.cardBest, h->card.poke.addr, h->card.poke.value);
				else memcpy(hud.cardBest, sec.emu->fb, sizeof(hud.cardBest));
				hud.cardScore = 0;
				card_backdrop();
				hud.cardTaken = h->card.hideTillTake; /* (the HUD over it from the next frame) */
			}
		} else {
			/* (the side part first, so the first backdrop has it if it's
			 * been seen; a better one later makes the backdrop again) */
			bool side = h->card.side[2] > h->card.side[0] && hud.hudFrames >= 2 && side_take();
			if (hud.hudFrames >= 2 && hud.cardScore != 0 && card_take() && !hud.hudBackValid) card_backdrop();
			else if (side && hud.hudBackValid) card_backdrop();
		}
		if (hud.hudBackValid) {
			memcpy(hud.hudCanvas, hud.hudBack, sizeof(hud.hudCanvas));
			hud.hooks.presentHud(hud.hudCanvas);
			hud.hudShown = true;
		} else if (h->card.drop[0].addr && hud.hudFrames == 1) {
			/* (hud.card.drop, Sonic Advance 3, no take yet: its HUD stays
			 * on the game screen over the card, so not on the second
			 * screen too till the take: the logo alone) */
			memcpy(hud.hudCanvas, card_logo(), sizeof(hud.hudCanvas));
			hud.hooks.presentHud(hud.hudCanvas);
			hud.hudShown = true;
		}
	} else if (hud.hudOn && race) {
		/* hud.emptyIsOff: nothing drawn for 20 frames: the logo */
		static int emptyFor;
		bool empty = false;
		if (h->emptyIsOff) {
			uint32_t clear = emu_hud_clear();
			empty = true;
			for (int i = 0; i < GBA_W * GBA_H && empty; ++i) empty = (hud.hudPx[i] & 0xFFFFFF) == clear;
			emptyFor = empty ? emptyFor + 1 : 0;
		}
		if (h->emptyIsOff && emptyFor >= 20) {
			if (hud.hudShown && sec.debug) fprintf(stderr, "frame %llu: HUD empty: the logo\n", (unsigned long long) sec.frame);
			hud.hudShown = false;
		} else if (h->freeze[0].addr && memprobes_match(h->freeze, 2, sec.emu)) {
			/* hud.freeze: the picture as it was (World-e's status bar
			 * sliding away as the inventory opens, and back, SM4-8) */
		} else if (!(empty && !hud.hudShown)) {
			if (emu_hud_grabbed()) hud_make_backdrop(); /* (that frame drew no HUD: the last one is reused) */
			hud_compose();
			hud.hooks.presentHud(hud.hudCanvas);
			if (sec.logoForced && !(hud.hudProf->logoIf[0].addr && memprobes_match(hud.hudProf->logoIf, 2, sec.emu)) &&
			    !(hud.hudProf->logoIfOr[0].addr && memprobes_match(hud.hudProf->logoIfOr, 2, sec.emu))) {
				/* (a new HUD picture: the logo's hold is over) */
				sec.logoForced = false, sec.logoHolding = 0;
				if (sec.debug) fprintf(stderr, "frame %llu: logo only off: a new HUD picture\n", (unsigned long long) sec.frame);
			}
			if (!hud.hudShown && sec.debug) fprintf(stderr, "frame %llu: HUD on the second screen\n", (unsigned long long) sec.frame);
			hud.hudShown = true;
			if (!hud.hudAlt && h->alt.rows && memprobes_match(h->alt.heldWhen, 2, sec.emu)) memcpy(hud.hudHeld, hud.hudCanvas, sizeof(hud.hudHeld)), hud.hudHeldOk = true;
		}
	}
	bool scene = race && h->scene.when[0].addr && memprobes_match(h->scene.when, 2, sec.emu);
	bool card = race && h->card.tile && oam_has_tile_pal(h->card.tile, h->card.tileMax, h->card.pal, h->card.prio1);
	if (h->card.hideTillTake) {
		/* (Sonic Advance 3, SA3-3: the card's start, its sprite or a
		 * little before it, until the take; a start seen again within 10
		 * frames is the same card) */
		bool pre = race && h->card.pre[0].addr && memprobes_match(h->card.pre, 2, sec.emu);
		hud.cardPreFor = pre && !card ? hud.cardPreFor + 1 : 0;
		bool starting = card || (pre && hud.cardPreFor <= 30);
		if (starting && hud.cardAway >= 10) hud.cardTaken = false;
		hud.cardAway = starting ? 0 : hud.cardAway + 1;
		card = starting && !hud.cardTaken;
	}
	/* hud.card.drop: no card here (Sonic Advance 3's hubs): the take is
	 * forgotten, the logo behind the HUD */
	if (race && h->card.drop[0].addr && (hud.hudBackValid || hud.cardScore >= 0) && memprobes_match(h->card.drop, 2, sec.emu)) {
		hud.hudBackValid = false, hud.cardScore = -1, hud.sideScore = -1;
		if (sec.debug) fprintf(stderr, "frame %llu: title card dropped\n", (unsigned long long) sec.frame);
	}
	if (card != hud.hudCard) {
		/* a card coming up: a new take; going: the best one, if not yet */
		/* (hud.card.drop, Sonic Advance 3: the last take stays till the
		 * new one, without the HUD, which stays on the game's card) */
		if (card) hud.cardScore = -1, hud.sideScore = -1, hud.hudBackValid = hud.hudBackValid && h->card.drop[0].addr;
		if (card && h->card.hideTillTake) {
			/* (this frame was drawn before the split was on: drawn again
			 * without the HUD, so it isn't on the game screen for a frame,
			 * SA3-3) */
			static uint32_t noHud[GBA_W * GBA_H];
			snapshot_hiding_sprites(hud.hudObjs, rule_count(hud.hudObjs, 12), NULL, 0, noHud);
			memcpy(sec.emu->fb, noHud, sizeof(noHud));
		}
		else if (hud.cardScore >= 0 && !hud.hudBackValid) card_backdrop();
		if (sec.debug) fprintf(stderr, "frame %llu: title card %s\n", (unsigned long long) sec.frame, card ? "up" : "gone");
	}
	if (race != hud.hudOn || scene != hud.hudScene || world != hud.hudWorld || alt != hud.hudAlt || card != hud.hudCard) {
		/* (hud.card.hideTillTake: the HUD split off as usual, but not shown
		 * till the take) */
		bool cardObjs = card && !h->card.hideTillTake;
		const SpriteRule* objs = cardObjs ? h->card.objs : scene ? h->scene.objs : world ? hud.hudWorldObjs : hud.hudObjs;
		unsigned mask = alt ? h->alt.bgMask : h->bgMask;
		int nObjs = rule_count(objs, cardObjs || scene || world ? 4 : 12);
		/* (nothing split off: only the logo behind the map picture, as
		 * for Fire Emblem and Final Fantasy Tactics Advance; no second
		 * renderer) */
		static bool splitting;
		bool some = mask || nObjs || h->band;
		if (alt != hud.hudAlt && h->alt.rows) emu_hud_rows(alt ? h->alt.rows : h->rows);
		if (some || splitting) emu_hud_split(sec.emu, race && some, mask, objs, nObjs, hud.hudPx);
		splitting = race && some;
		if (race && some) emu_hud_copy(sec.emu, cardObjs); /* (the card stays on the game screen too) */
		hud.hudCard = card;
		if (sec.debug && race && alt != hud.hudAlt) fprintf(stderr, "frame %llu: HUD on its other layer%s\n", (unsigned long long) sec.frame, alt ? "" : ": off");
		hud.hudAlt = alt;
		/* (by priority: the renderer picks the layers line by line) */
		emu_hud_by_priority(h->bgPrioBelow);
		{
			uint32_t a[2], m[2], v[2];
			int sz[2], n = 0;
			for (; n < 2 && h->memLayers.when[n].addr; ++n) {
				a[n] = h->memLayers.when[n].addr, m[n] = h->memLayers.when[n].mask, v[n] = h->memLayers.when[n].value, sz[n] = h->memLayers.when[n].size;
			}
			emu_hud_mem_layers(a, m, v, sz, race ? n : 0, h->memLayers.any, h->memLayers.bgMask, h->memLayers.gamePal);
		}
		hud.hudKeep = 0;
		if (sec.debug && race && scene != hud.hudScene) fprintf(stderr, "frame %llu: HUD scene %s\n", (unsigned long long) sec.frame, scene ? "on" : "off");
		hud.hudOn = race;
		hud.hudScene = scene;
		hud.hudWorld = world;
		hud.hudFrames = 0;
		hud.hudRegrab = 0;
		hud.mapFound = false;
		hud.mapHeldOk = false, hud.nMapDots = 0, hud.mapUndrawn = 0; /* (hud.mapAt.holdTile: none held yet) */
	}
	/* hud.place: the profile's own places for the HUD's sprites, worked out
	 * each frame (Mario Pinball Land: where the game has them paused) */
	if (h->place) h->place(sec.emu, sec.romPath, hud.hudOn && !hud.hudAlt);
	/* a spell or other animation: its effects are on the text box layers,
	 * in other palettes than the text boxes; those layers stay (from the
	 * next frame) */
	unsigned keep = 0;
	if (hud.hudOn && h->effects[0].addr && memprobes_match(h->effects, 2, sec.emu)) keep = effect_bgs(h->effectsPal);
	if (keep != hud.hudKeep) {
		emu_hud_keep_bgs(keep);
		if (sec.debug) fprintf(stderr, "frame %llu: effect layers %X\n", (unsigned long long) sec.frame, keep);
		hud.hudKeep = keep;
	}
	if (h->retile) {
		int idx[8];
		uint16_t tile[8];
		emu_hud_retile(idx, tile, hud.hudOn ? h->retile(sec.emu, idx, tile) : 0);
	}
	/* the course's backdrop, as each race starts (not for the scene) */
	if (hud.hudOn && ++hud.hudFrames == 2 && h->backdrop && !hud.hudScene) emu_hud_grab(sec.emu, h->backdrop, hud.hudGrab);
	/* the backdrop, the game screen (map, units, no HUD): once per map (its tiles' checksum),
	 * when the HUD comes on, not again as it comes and goes in a battle */
	if (hud.hudOn && h->mapCnt && mapBg >= 0 && (hud.hudFrames == 2 || hud.hudFrames == hud.hudRegrab)) {
		uint32_t sum = map_tiles_sum(mapBg);
		if (hud.hudFrames == hud.hudRegrab || !hud.hudBackValid || sum != hud.hudMapSum) {
			hud.hudMapSum = sum;
			/* the game screen as it is now: the scene, without the HUD (nor
			 * the sprites the profile leaves out of it, rendered again) */
			int nh = rule_count(h->backdropHide, 2);
			if (world) {
				/* the world map: its layers below the HUD's (the land and the
				 * map, no sprites), drawn next frame */
				unsigned m = 0;
				for (int i = 0; i < 4; ++i) {
					if ((emu_read16(sec.emu, 0x04000008 + i * 2) & 3) >= h->bgPrioBelow) m |= 1u << i;
				}
				emu_hud_grab(sec.emu, m, hud.hudGrab);
			} else {
				if (nh) snapshot_hiding_sprites(h->backdropHide, nh, hud.hudObjs, rule_count(hud.hudObjs, 12), hud.hudGrab);
				else memcpy(hud.hudGrab, sec.emu->fb, sizeof(hud.hudGrab));
				hud_make_backdrop();
			}
		}
	}
	/* a scene change: the game fades to black (or white) and back (to
	 * another map, or the same one elsewhere); once the picture's back,
	 * take the scene again (retried until its brightness settles) */
	static int darkRun;
	if (h->mapCnt) {
		bool dark = frame_is_flat(sec.emu->fb);
		if (dark) {
			++darkRun;
		} else {
			if (darkRun >= 6 && hud.hudBackValid) {
				if (sec.debug) fprintf(stderr, "frame %llu: faded in: the backdrop again\n", (unsigned long long) sec.frame);
				if (hud.hudOn) hud.hudRegrab = hud.hudFrames + 10;
				hud.hudSettle = true;
				hud.hudMapSum = 0; /* (and when the HUD comes back on) */
			}
			darkRun = 0;
		}
	}
	/* menus: the logo (or the map), once it's been gone for a third of a
	 * second (not for a moment between rooms); no keep probes: never kept */
	static int goneFor;
	/* (not while the game screen is blank between scenes: the second
	 * screen stays as dark, or as white, as the fade left it) */
	bool gone = !race && hud.hudShown && !(h->keep[0].addr && memprobes_match(h->keep, 4, sec.emu)) &&
	            !(h->mapCnt && frame_is_flat(sec.emu->fb));
	goneFor = gone ? goneFor + 1 : 0;
	if (goneFor >= 20) {
		hud.hudShown = false;
		hud.hudHeldOk = false;
		goneFor = 0;
		if (sec.debug) fprintf(stderr, "frame %llu: HUD gone\n", (unsigned long long) sec.frame);
	}
}


/* hud.alt.shade: a pixel darkened (to a third) */
static uint32_t shade_px(uint32_t c) {
	return (c & 0xFF000000) | (((c >> 2) & 0x3F3F3F) + ((c >> 3) & 0x1F1F1F));
}

/* Profile hud.overMap: the map picture with the HUD's pieces in it (where
 * the picture has the profile's mapClear colour): each piece as last seen
 * (hud_take_pieces). */
const uint32_t* hud_over_map(const uint32_t* px) {
	static uint32_t* over;
	static int overSize;
	if (!hud.hudProf || (!sec.overOn && !hud.hudProf->hud.logoUnder)) return px;
	const typeof(hud.hudProf->hud)* h = &hud.hudProf->hud;
	if (h->logoUnder && hud.combatProf && !hud.combatMade) make_combat_canvas();
	if (overSize != sec.mapW * sec.mapH) {
		free(over);
		over = malloc((size_t) sec.mapW * sec.mapH * 4);
		overSize = over ? sec.mapW * sec.mapH : 0;
		if (!over) return px;
	}
	uint32_t mapClr = h->mapClear & 0xFFFFFF;
	/* a piece that goes alone (a conversation): on the plain background,
	 * the others waiting */
	int alone = -1;
	for (int m = 0; m < 4; ++m) {
		if (h->moves[m].alone && hud.pieces[m].valid) alone = m;
	}
	hud.overAlone = alone >= 0;
	memcpy(over, alone >= 0 && h->logoUnder && hud.combatPx ? hud.combatPx : px, (size_t) sec.mapW * sec.mapH * 4);
	int px0[4] = { 0 }, py0[4] = { 0 };
	for (int k = 0; k < 8; ++k) {
		int m = k & 3;
		const HudMove* mv = &h->moves[m];
		const Piece* pc = &hud.pieces[m];
		/* the pieces, then the ones that follow them (over everything) */
		if (!pc->valid || (k < 4) == (mv->follow > 0 || mv->alone)) continue;
		if (alone >= 0 && m != alone) continue;
		int x0 = mv->how == 'C' ? mv->x - (pc->w - 1) / 2 : mv->how == 'R' ? mv->x - pc->w + 1 : mv->x;
		int y0 = mv->vhow == 'M' ? mv->y - (pc->h - 1) / 2 : mv->vhow == 'B' ? mv->y - pc->h + 1 : mv->y;
		if (mv->asIs) {
			x0 = pc->sx + (sec.mapW - GBA_W) / 2, y0 = pc->sy + (sec.mapH - GBA_H) / 2;
			if (mv->asIsBottom && pc->atBottom && !hud.hudAlt) y0 = sec.mapH - pc->h;
		}
		if (mv->wide) {
			/* as wide as the picture: each pixel from the piece's, scaled */
			int sw = pc->w * sec.mapW / GBA_W, sh = pc->h * sec.mapW / GBA_W;
			x0 = pc->sx * sec.mapW / GBA_W;
			for (int y = 0; y < sh; ++y) {
				for (int x = 0; x < sw; ++x) {
					uint32_t c = pc->px[(y * GBA_W / sec.mapW) * pc->w + x * GBA_W / sec.mapW];
					int nx = x0 + x, ny = y0 + y;
					if (!c || nx < 0 || ny < 0 || nx >= sec.mapW || ny >= sec.mapH) continue;
					over[ny * sec.mapW + nx] = c;
				}
			}
			px0[m] = x0, py0[m] = y0;
			continue;
		}
		if (mv->follow) {
			int a = mv->follow - 1;
			if (!hud.pieces[a].valid) continue;
			x0 = pc->sx + px0[a] - hud.pieces[a].sx;
			y0 = pc->sy + py0[a] - hud.pieces[a].sy;
		}
		px0[m] = x0, py0[m] = y0;
		uint32_t bc = mv->box ? (mv->box & 0xFF) << 16 | (mv->box & 0xFF00) | (mv->box >> 16 & 0xFF) : 0x1000000;
		if (mv->box) {
			/* the box goes round the piece */
			for (int y = y0 - mv->pad; y < y0 + pc->h + mv->pad; ++y) {
				for (int x = x0 - mv->pad; x < x0 + pc->w + mv->pad; ++x) {
					if (x < 0 || y < 0 || x >= sec.mapW || y >= sec.mapH) continue;
					uint32_t* o = &over[y * sec.mapW + x];
					if ((*o & 0xFFFFFF) == mapClr) *o = 0xFF000000u | bc;
				}
			}
		}
		bool shade = mv->alone && hud.hudAlt && h->alt.shade;
		if (shade) {
			static uint32_t* orig;
			static int origSize;
			if (origSize != sec.mapW * sec.mapH) free(orig), orig = malloc((size_t) sec.mapW * sec.mapH * 4), origSize = orig ? sec.mapW * sec.mapH : 0;
			if (orig) {
				/* the black (the game screen's, stretched over the whole
				 * picture) darkens; the piece's other pixels (the lettering)
				 * go where it is */
				memcpy(orig, over, (size_t) sec.mapW * sec.mapH * 4);
				for (int ny = 0; ny < sec.mapH; ++ny) {
					int ly = ny * GBA_H / sec.mapH - pc->sy;
					for (int nx = 0; nx < sec.mapW; ++nx) {
						int lx = nx * GBA_W / sec.mapW - pc->sx;
						uint32_t c = lx >= 0 && ly >= 0 && lx < pc->w && ly < pc->h ? pc->px[ly * pc->w + lx] : 0;
						if (c && (c & 0xFFFFFF) == 0) over[ny * sec.mapW + nx] = shade_px(orig[ny * sec.mapW + nx]);
					}
				}
				for (int y = 0; y < pc->h; ++y) {
					for (int x = 0; x < pc->w; ++x) {
						uint32_t c = pc->px[y * pc->w + x];
						int nx = x0 + x, ny = y0 + y;
						if (!c || (c & 0xFFFFFF) == 0 || nx < 0 || ny < 0 || nx >= sec.mapW || ny >= sec.mapH) continue;
						over[ny * sec.mapW + nx] = c;
					}
				}
				continue;
			}
		}
		for (int y = 0; y < pc->h; ++y) {
			for (int x = 0; x < pc->w; ++x) {
				uint32_t c = pc->px[y * pc->w + x];
				int nx = x0 + x, ny = y0 + y;
				if (!c || nx < 0 || ny < 0 || nx >= sec.mapW || ny >= sec.mapH) continue;
				uint32_t* o = &over[ny * sec.mapW + nx];
				if (mv->follow || mv->alone || mv->top || (*o & 0xFFFFFF) == mapClr || (*o & 0xFFFFFF) == bc) *o = c;
			}
		}
	}
	if (h->logoUnder && hud.combatPx) {
		/* the logo behind: where nothing else is: last */
		for (int i = 0; i < sec.mapW * sec.mapH; ++i) {
			if ((over[i] & 0xFFFFFF) == mapClr) over[i] = hud.combatPx[i];
		}
	}
	return over;
}

/* (second.h hook) a new map picture is in: what the HUD split keeps of it (the
 * held number, the level list's title row) */
void hud_new_map(bool fresh) {
	if (fresh && hud.hudProf && hud.hudProf->hud.held.over.x1 && sec.mapRes.frames[0] && sec.mapRes.w * sec.mapRes.h <= GBA_W * GBA_H) {
		/* hud.held.over: the number, laid over the held picture */
		hud.heldNumW = sec.mapRes.w, hud.heldNumH = sec.mapRes.h;
		memcpy(hud.heldNum, sec.mapRes.frames[0], (size_t) sec.mapRes.w * sec.mapRes.h * 4);
		hud.hudLogoBelowDone = false;
	}
	if (fresh && hud.hudProf && hud.hudProf->hud.mapTitleRows && sec.mapRes.w == GBA_W && sec.mapRes.frames[0]) {
		/* the level list's title row (SMA-7), held for the level */
		if (!hud.mapTitleN) hud.hudLogoBelowDone = false; /* (the logo moves down under it) */
		hud.mapTitleN = S_MIN(hud.hudProf->hud.mapTitleRows, 32);
		for (int i = 0; i < GBA_W * hud.mapTitleN; ++i) hud.mapTitle[i] = sec.mapRes.frames[0][i] | 0xFF000000;
	}
}

/* profile.combat's picture (and hud.logoUnder's): the logo as on any
 * other second screen (GEN-1), at a GBA pixel per GBA pixel of the map
 * picture. */
void make_combat_canvas(void) {
	hud.combatMade = true;
	uint32_t* px = malloc((size_t) sec.mapW * sec.mapH * 4);
	if (!px) return;
	for (int i = 0; i < sec.mapW * sec.mapH; ++i) px[i] = SCREEN_GREY;
	logo_draw(px, sec.mapW, sec.mapH, S_MAX(1, sec.mapW / GBA_W), 0, LOGO_IDLE);
	hud.hooks.presentCombat(px);
	free(hud.combatPx);
	hud.combatPx = px; /* (hud.logoUnder) */
}

/* The HUD split's and the fights' picture's set-up for a game (lp: its
 * profile, or NULL), from ss_setup once the capture exists: the HUD moves
 * only when there's another screen to put it on (screens > 1). (Was inline
 * in game.c's ss_setup.) */
void hud_setup(const MapProfile* lp, int screens) {
	/* the HUD moves only when there's another screen to put it on */
	hud.hudProf = lp && lp->hud.on[0].addr && screens > 1 ? lp : NULL;
	emu_hud_game_reg(hud.hudProf ? hud.hudProf->hud.gameReg.addr : 0, hud.hudProf ? hud.hudProf->hud.gameReg.bits : 0);
	emu_hud_rows(hud.hudProf ? hud.hudProf->hud.rows : 0);
	emu_hud_band(hud.hudProf && hud.hudProf->hud.band, true, hud.hudProf && hud.hudProf->hud.band == BAND_WATCH);
	hud.combatProf = sec.mc && lp && lp->combat[0].addr && screens > 1 ? lp : NULL;
	hud.combatOn = hud.combatMade = false;
	for (int m = 0; m < 4; ++m) hud.pieces[m].valid = false, hud.pieces[m].still = 0;
	if (hud.hudProf) {
		/* the sprites that move; tagged ones only if the tags go in */
		memcpy(hud.hudObjs, hud.hudProf->hud.objs, sizeof(hud.hudObjs));
		bool patched = profile_patch_rom(hud.hudProf, sec.emu);
		if (patched) fprintf(stderr, "HUD split: ROM patched\n");
		for (int i = 0; !patched && i < 12; ++i) hud.hudObjs[i].tagged = 0;
		memcpy(hud.hudWorldObjs, hud.hudProf->hud.world.objs, sizeof(hud.hudWorldObjs));
		for (int i = 0; !patched && i < 4; ++i) hud.hudWorldObjs[i].tagged = 0;
	}
	hud.hudOn = false;
	hud.hudShown = false;
	hud.hudHeldOk = false;
	hud.mirrorHave = hud.mirrorOn = false;
	hud.hudScene = false;
	hud.hudCard = false, hud.cardScore = -1, hud.sideScore = -1;
	hud.hudFadeEff = hud.hudFadeIn = 0;
	memset(hud.hudPalRef, 0, sizeof(hud.hudPalRef));
	hud.hudWorld = false;
	hud.hudAlt = false;
	hud.hudOffLatch = false;
	hud.hudLogoDone = false;
	hud.hudLogoBelowDone = false;
	hud.mapTitleN = 0;
	hud.hudBackValid = false;
	hud.hudRegrab = 0;
}

/* A game with fights shown as their own picture (profile .combat), once a
 * frame: the fights' picture made when the game starts a fight (or always,
 * logoAlways), and fights on or off after a few frames of it, not on one odd
 * frame. (Was inline in game.c's frame loop.) */
void hud_combat_step(void) {
		/* a fight: after a few frames of it, not on one odd frame */
		static int streak;
		hud.combatInGame = hud.combatProf->hud.on[0].addr && memprobes_match(hud.combatProf->hud.on, 1, sec.emu);
		if ((hud.combatInGame || hud.combatProf->hud.logoAlways) && hud.combatProf->hud.logoUnder && !hud.combatMade) make_combat_canvas();
		bool c = memprobes_match(hud.combatProf->combat, 2, sec.emu) ||
		         (hud.combatProf->combatOr[0].addr && memprobes_match(hud.combatProf->combatOr, 2, sec.emu));
		streak = c != hud.combatOn ? streak + 1 : 0;
		if (streak >= 3) {
			hud.combatOn = c;
			streak = 0;
			if (hud.combatOn && !hud.combatMade) make_combat_canvas();
			fprintf(stderr, "frame %llu: fight %s (the second screen's logo %s)\n", (unsigned long long) sec.frame, hud.combatOn ? "on" : "off",
			        hud.combatOn ? "up" : "gone");
		}
}

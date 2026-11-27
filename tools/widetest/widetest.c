/* GBA DS widescreen test ROM (docs/WIDESCREEN.md, tools/widetest/README.md).
 *
 * A GBA program that reads the GBA DS widescreen marker at 0x08000200 (32
 * bytes of 0xFF until a patch writes one) and draws its scenes for the
 * picture the marker asks for: 240 x 160 without it, 284 x 160 for 16:9,
 * 256 x 160 for 16:10, 240 x 180 for 4:3. It behaves like a real game with
 * and without a widescreen patch: unpatched, the background past the screen's
 * edges is junk (a red hatch) and the sprites turn round at the screen's
 * edges; patched, the scene is drawn out to the picture's edges and the
 * sprites travel the whole width (or height).
 *
 * Scene 1 (mode 0): a circle and a square grid, both centred, so a
 * stretched picture shows at a glance (an oval, oblong squares); a yellow
 * frame round the whole picture, dashed cyan lines just outside the GBA's own
 * 240 x 160, the margins in green. Scene 2 (mode 1, A to switch): a rotating
 * affine background with two windows: a band across the top (which should
 * span the whole width) and a hole in the middle (which should stay
 * centred). Built by build.py with clang; no libraries. */
#include <stdint.h>
#include "font.h"
#include "sine.h"

typedef volatile uint16_t vu16;
typedef volatile uint32_t vu32;
#define REG16(a) (*(vu16*) (a))
#define REG32(a) (*(vu32*) (a))
#define DISPCNT REG16(0x04000000)
#define VCOUNT REG16(0x04000006)
#define BGCNT(n) REG16(0x04000008 + 2 * (n))
#define BGHOFS(n) REG16(0x04000010 + 4 * (n))
#define BGVOFS(n) REG16(0x04000012 + 4 * (n))
#define BG2PA REG16(0x04000020)
#define BG2PB REG16(0x04000022)
#define BG2PC REG16(0x04000024)
#define BG2PD REG16(0x04000026)
#define BG2X REG32(0x04000028)
#define BG2Y REG32(0x0400002C)
#define WIN0H REG16(0x04000040)
#define WIN1H REG16(0x04000042)
#define WIN0V REG16(0x04000044)
#define WIN1V REG16(0x04000046)
#define WININ REG16(0x04000048)
#define WINOUT REG16(0x0400004A)
#define KEYS REG16(0x04000130)
#define BGPAL ((vu16*) 0x05000000)
#define OBJPAL ((vu16*) 0x05000200)
#define VRAM ((vu32*) 0x06000000)
#define OAM ((vu16*) 0x07000000)

#define RGB(r, g, b) ((uint16_t) ((r) | (g) << 5 | (b) << 10))

/* the marker (docs/WIDESCREEN.md) */
__attribute__((section(".widemarker"), used)) const uint8_t wideMarker[32] = {
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
	0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
};

void* memset(void* d, int c, unsigned n) {
	uint8_t* p = d;
	while (n--) *p++ = (uint8_t) c;
	return d;
}
/* (clang turns put_num's loop into a division) */
unsigned __aeabi_uidiv(unsigned n, unsigned d) {
	unsigned q = 0;
	for (int i = 31; i >= 0; --i)
		if ((n >> i) >= d) n -= d << i, q |= 1u << i;
	return q;
}
void* memcpy(void* d, const void* s, unsigned n) {
	uint8_t* p = d;
	const uint8_t* q = s;
	while (n--) *p++ = *q++;
	return d;
}

/* the picture: view coordinates run from -mx to 240 + mx - 1 across and
 * -my to 160 + my - 1 down; (0, 0) is the GBA screen's top left */
static int W, H, mx, my, rnum, rden; /* (no .data: set in main) */
static int patched;

/* the canvas: one tile per 8 x 8 of the picture, drawn in EWRAM, then copied */
#define CANVAS ((uint8_t*) 0x02000000)
static int tilesW, tilesH;

static void read_marker(void) {
	const volatile uint8_t* m = (const volatile uint8_t*) 0x08000200;
	static const char magic[16] = "GBADS-WIDESCREEN";
	for (int i = 0; i < 16; ++i)
		if (m[i] != (uint8_t) magic[i]) return;
	if (m[16] != 1) return;
	int w = m[20] | m[21] << 8, h = m[22] | m[23] << 8;
	if (w < 240 || h < 160 || w > 368 || h > 224 || ((w - 240) & 1) || ((h - 160) & 1)) return;
	if (w > 240 && h > 160) return;
	W = w, H = h, rnum = m[17], rden = m[18];
	mx = (W - 240) >> 1, my = (H - 160) >> 1;
	patched = 1;
}

static void pset(int vx, int vy, int c) {
	int x = vx + mx, y = vy + my;
	if (x < 0 || y < 0 || x >= W || y >= H) return;
	int t = 1 + (y >> 3) * tilesW + (x >> 3);
	uint8_t* b = CANVAS + t * 32 + (y & 7) * 4 + ((x & 7) >> 1);
	*b = (x & 1) ? (uint8_t) ((*b & 0x0F) | c << 4) : (uint8_t) ((*b & 0xF0) | c);
}
static int in_margin(int vx, int vy) {
	return vx < 0 || vx >= 240 || vy < 0 || vy >= 160;
}

static void rect(int x0, int y0, int w, int h, int c) {
	for (int y = y0; y < y0 + h; ++y)
		for (int x = x0; x < x0 + w; ++x) pset(x, y, c);
}

static void glyph(int x, int y, char ch, int c, int s) {
	if (ch < 32 || ch > 126) return;
	const uint8_t* g = font5x7[ch - 32];
	for (int r = 0; r < 7; ++r)
		for (int k = 0; k < 5; ++k)
			if (g[r] & (0x10 >> k))
				for (int a = 0; a < s; ++a)
					for (int b = 0; b < s; ++b) pset(x + k * s + b, y + r * s + a, c);
}
static int text_len(const char* t) {
	int n = 0;
	while (t[n]) ++n;
	return n;
}
/* centred on cx, with a black box behind */
static void text(int cx, int y, const char* t, int c, int s) {
	int w = text_len(t) * 6 * s - s;
	int x = cx - (w >> 1);
	rect(x - 2, y - 2, w + 4, 7 * s + 4, 9);
	for (int i = 0; t[i]; ++i) glyph(x + i * 6 * s, y, t[i], c, s);
}
/* one letter a row, centred on cx */
static void text_down(int cx, int y, const char* t, int c) {
	for (int i = 0; t[i]; ++i) glyph(cx - 2, y + i * 9, t[i], c, 1);
}
static char* put_num(char* p, int v) {
	char d[6];
	int n = 0;
	do {
		int q = 0, r = v;
		while (r >= 10) r -= 10, ++q;
		d[n++] = (char) ('0' + r);
		v = q;
	} while (v);
	while (n) *p++ = d[--n];
	return p;
}
static char* put_str(char* p, const char* s) {
	while (*s) *p++ = *s++;
	return p;
}

static void circle(int cx, int cy, int r, int c) {
	/* two pixels thick */
	for (int rr = r - 1; rr <= r; ++rr) {
		int x = rr, y = 0, err = 1 - rr;
		while (x >= y) {
			pset(cx + x, cy + y, c), pset(cx - x, cy + y, c), pset(cx + x, cy - y, c), pset(cx - x, cy - y, c);
			pset(cx + y, cy + x, c), pset(cx - y, cy + x, c), pset(cx + y, cy - x, c), pset(cx - y, cy - x, c);
			++y;
			if (err < 0) err += 2 * y + 1;
			else --x, err += 2 * (y - x) + 1;
		}
	}
}

/* palette indices (BG bank 0) */
enum { C_CLEAR, C_GRID, C_WHITE, C_YELLOW, C_CYAN, C_TEXT, C_MARGIN, C_HATCH, C_MGRID, C_BLACK, C_HATCH2, C_ORANGE, C_GREEN };

static void draw_canvas(int scene) {
	memset(CANVAS, 0, (unsigned) (1 + tilesW * tilesH) * 32);
	/* tile 0: the junk past the canvas (what an unpatched game leaves there) */
	for (int y = 0; y < 8; ++y)
		for (int x = 0; x < 8; x += 2) {
			int c0 = ((x + y) & 7) < 2 ? C_HATCH : C_HATCH2, c1 = ((x + 1 + y) & 7) < 2 ? C_HATCH : C_HATCH2;
			CANVAS[y * 4 + (x >> 1)] = (uint8_t) (c0 | c1 << 4);
		}
	int x0 = -mx, x1 = 240 + mx, y0 = -my, y1 = 160 + my;
	if (scene == 0) {
		for (int y = y0; y < y1; ++y)
			for (int x = x0; x < x1; ++x) {
				int m = in_margin(x, y);
				int g = (((x - 120) & 15) == 0) || (((y - 80) & 15) == 0);
				if (g) pset(x, y, m ? C_MGRID : C_GRID);
				else if (m) pset(x, y, C_MARGIN);
			}
		circle(120, 80, 70, C_WHITE);
		/* a small circle in each margin's middle, where there's room */
		if (mx >= 16) circle(-(mx >> 1), 80, (mx >> 1) - 3, C_WHITE), circle(239 + (mx >> 1) + 1, 80, (mx >> 1) - 3, C_WHITE);
	}
	/* the GBA screen's own edges, dashed, just outside it */
	if (mx)
		for (int y = y0; y < y1; ++y)
			if ((y & 7) < 4) pset(-1, y, C_CYAN), pset(240, y, C_CYAN);
	if (my)
		for (int x = x0; x < x1; ++x)
			if ((x & 7) < 4) pset(x, -1, C_CYAN), pset(x, 160, C_CYAN);
	/* the whole picture's frame */
	for (int x = x0; x < x1; ++x) pset(x, y0, C_YELLOW), pset(x, y1 - 1, C_YELLOW);
	for (int y = y0; y < y1; ++y) pset(x0, y, C_YELLOW), pset(x1 - 1, y, C_YELLOW);

	char line[48], *p = line;
	if (patched) {
		p = put_num(p, rnum), *p++ = ':', p = put_num(p, rden);
		p = put_str(p, " PATCH  ");
		p = put_num(p, W), p = put_str(p, " X "), p = put_num(p, H);
	} else {
		p = put_str(p, "NO PATCH  240 X 160");
	}
	*p = 0;
	if (scene == 0) {
		text(120, 4, "GBA DS WIDESCREEN TEST", C_ORANGE, 1);
		text(120, 16, line, patched ? C_GREEN : C_TEXT, 1);
		text(120, 134, "ROUND CIRCLE = RIGHT SHAPE", C_TEXT, 1);
		text(120, 146, "A: NEXT SCENE", C_TEXT, 1);
	} else {
		text(120, 4, "ROTATION AND WINDOWS", C_ORANGE, 1);
		text(120, 134, "BAND: WHOLE WIDTH. HOLE: CENTRED", C_TEXT, 1);
		text(120, 146, line, patched ? C_GREEN : C_TEXT, 1);
	}
	if (mx >= 8) text_down(-(mx >> 1), 12, "WIDE", C_GREEN), text_down(240 + (mx >> 1), 12, "WIDE", C_GREEN);
	if (my >= 9) text(120, -my + 1, "EXTRA ROWS", C_GREEN, 1), text(120, 160 + my - 8, "EXTRA ROWS", C_GREEN, 1);

	/* to VRAM: tiles from 0x06000000, the 64 x 32 map at screen block 28 */
	unsigned words = (unsigned) (1 + tilesW * tilesH) * 8;
	const uint32_t* src = (const uint32_t*) CANVAS;
	for (unsigned i = 0; i < words; ++i) VRAM[i] = src[i];
	vu16* map = (vu16*) 0x0600E000;
	for (int ty = 0; ty < 32; ++ty)
		for (int tx = 0; tx < 64; ++tx) {
			uint16_t t = tx < tilesW && ty < tilesH ? (uint16_t) (1 + ty * tilesW + tx) : 0;
			map[(tx >= 32 ? 0x400 : 0) + ty * 32 + (tx & 31)] = t;
		}
}

/* scene 2's affine background: 128 x 128, 8 bpp, wrapping: a checkerboard
 * with a ring, at char block 2, its map at screen block 30 */
static void draw_affine(void) {
	uint8_t* px = CANVAS + 0x10000;
	for (int y = 0; y < 128; ++y)
		for (int x = 0; x < 128; ++x) {
			int dx = x - 64, dy = y - 64, d2 = dx * dx + dy * dy;
			int c = (((x >> 4) ^ (y >> 4)) & 1) ? 16 : 17;
			if (d2 >= 40 * 40 && d2 < 46 * 46) c = 18;
			if (x == 0 || y == 0) c = 19;
			int t = (y >> 3) * 16 + (x >> 3);
			px[t * 64 + (y & 7) * 8 + (x & 7)] = (uint8_t) c;
		}
	const uint32_t* s = (const uint32_t*) px;
	for (int i = 0; i < 0x4000 / 4; ++i) VRAM[0x8000 / 4 + i] = s[i];
	/* the map: 16 x 16 entries, one byte each, written two at a time */
	vu16* map = (vu16*) 0x0600F000;
	for (int i = 0; i < 128; ++i) map[i] = (uint16_t) ((2 * i) | (2 * i + 1) << 8);
}

static void make_ball(void) {
	/* 16 x 16, 4 bpp, 1D: tiles 0-3 at 0x06010000 */
	uint8_t px[256];
	for (int y = 0; y < 16; ++y)
		for (int x = 0; x < 16; ++x) {
			int dx = 2 * x - 15, dy = 2 * y - 15, d2 = dx * dx + dy * dy;
			int c = d2 < 15 * 15 ? (d2 < 9 * 9 && dx < 0 && dy < 0 ? 2 : 1) : d2 < 16 * 16 ? 3 : 0;
			px[y * 16 + x] = (uint8_t) c;
		}
	vu16* t = (vu16*) 0x06010000;
	for (int ty = 0; ty < 2; ++ty)
		for (int tx = 0; tx < 2; ++tx)
			for (int y = 0; y < 8; ++y)
				for (int x = 0; x < 8; x += 4) {
					const uint8_t* q = &px[(ty * 8 + y) * 16 + tx * 8 + x];
					t[(ty * 2 + tx) * 16 + y * 2 + (x >> 2)] = (uint16_t) (q[0] | q[1] << 4 | q[2] << 8 | q[3] << 12);
				}
}

static void vsync(void) {
	while (VCOUNT >= 160) {}
	while (VCOUNT < 160) {}
}

static void set_scene(int scene) {
	DISPCNT = 0x80; /* (forced blank while it's drawn) */
	draw_canvas(scene);
	BGCNT(0) = (uint16_t) (0 << 2 | 28 << 8 | 1 << 14 | 0); /* 4 bpp, 64 x 32, priority 0 */
	BGHOFS(0) = (uint16_t) mx;
	BGVOFS(0) = (uint16_t) my;
	if (scene == 0) {
		DISPCNT = 0x0000 | 0x0100 | 0x1000 | 0x0040; /* mode 0, BG0, OBJ, 1D */
	} else {
		BGCNT(2) = (uint16_t) (2 << 2 | 1 << 7 | 30 << 8 | 1 << 13 | 0 << 14 | 1); /* 8 bpp, wraps, 128 x 128, priority 1 */
		WIN0H = (uint16_t) (0 << 8 | 240); /* the band: the whole width */
		WIN0V = (uint16_t) (0 << 8 | 26);
		WIN1H = (uint16_t) (96 << 8 | 144); /* the hole: centred */
		WIN1V = (uint16_t) (56 << 8 | 104);
		WININ = (uint16_t) (0x01 | 0x11 << 8); /* band: BG0 alone; hole: BG0 and sprites */
		WINOUT = 0x15; /* elsewhere: BG0, BG2, sprites */
		DISPCNT = 0x0001 | 0x0100 | 0x0400 | 0x1000 | 0x0040 | 0x2000 | 0x4000;
	}
}

int main(void) {
	W = 240, H = 160;
	read_marker();
	tilesW = (W + 7) >> 3, tilesH = (H + 7) >> 3;
	static const uint16_t bg[16] = {
		RGB(3, 5, 12), RGB(8, 10, 20), RGB(31, 31, 31), RGB(31, 27, 0),
		RGB(0, 26, 31), RGB(28, 28, 28), RGB(2, 9, 4), RGB(24, 3, 3),
		RGB(6, 17, 8), RGB(0, 0, 0), RGB(10, 1, 1), RGB(31, 17, 0),
		RGB(8, 31, 10), 0, 0, 0,
	};
	for (int i = 0; i < 16; ++i) BGPAL[i] = bg[i];
	BGPAL[16] = RGB(6, 8, 18), BGPAL[17] = RGB(12, 14, 26), BGPAL[18] = RGB(31, 20, 4), BGPAL[19] = RGB(20, 20, 24);
	OBJPAL[1] = RGB(31, 14, 2), OBJPAL[2] = RGB(31, 28, 20), OBJPAL[3] = RGB(10, 3, 0);
	make_ball();
	draw_affine();
	int scene = 0;
	set_scene(scene);
	/* the balls: one across, one up and down; unpatched they turn at the
	 * screen's edges, as a game's sprites would */
	int bx = 0, bdx = 1, by = 0, bdy = 1, angle = 0, frame = 0;
	uint16_t prev = 0x3FF;
	for (;;) {
		vsync();
		int xmin = -mx, xmax = 240 + mx - 16, ymin = -my, ymax = 160 + my - 16;
		OAM[0] = (uint16_t) ((100) & 0xFF);
		OAM[1] = (uint16_t) (0x4000 | (bx & 0x1FF));
		OAM[2] = 0;
		OAM[4] = (uint16_t) (by & 0xFF);
		OAM[5] = (uint16_t) (0x4000 | (196 & 0x1FF));
		OAM[6] = 0;
		for (int i = 2; i < 128; ++i) OAM[i * 4] = 0x0200; /* (the rest hidden) */
		if (scene == 1) {
			int s = sine[angle & 255], c = sine[(angle + 64) & 255];
			int pa = c >> 4, pb = -(s >> 4), pc = s >> 4, pd = c >> 4;
			BG2PA = (uint16_t) pa, BG2PB = (uint16_t) pb, BG2PC = (uint16_t) pc, BG2PD = (uint16_t) pd;
			BG2X = (uint32_t) ((64 << 8) - (pa * 120 + pb * 80));
			BG2Y = (uint32_t) ((64 << 8) - (pc * 120 + pd * 80));
		}
		bx += bdx;
		if (bx <= xmin || bx >= xmax) bdx = -bdx;
		by += bdy;
		if (by <= ymin || by >= ymax) bdy = -bdy;
		if (++frame & 1) ++angle; /* (a turn in about 8 seconds) */
		uint16_t k = KEYS;
		if (!(k & 1) && (prev & 1)) scene ^= 1, set_scene(scene);
		prev = k;
	}
}

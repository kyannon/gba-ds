/* One play session: the emulator, the second-screen map, and the pause menu. */
#include "game.h"
#ifdef __wasi__
extern void web_sleep(unsigned ms) __attribute__((import_module("env"), import_name("web_sleep")));
#endif
#include "android.h"
#include "brand.h"
#include "emu.h"
#include "mapcap.h"
#include "paths.h"
#include "ui.h"
#include "ra.h"
#include "logo.h"
#include <dirent.h>
#include <math.h>
#include "mapframe.h"
#include "profiles.h"
#include "cheats.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#ifdef __linux__
#include <sys/resource.h>
#include <sys/syscall.h>
#include <unistd.h>
#endif

/* Times the game thread was taken off its core (involuntary context
 * switches): another thread got it, so the frame ran late through no work
 * of ours. Linux only; 0 elsewhere. */
static long thread_preemptions(void) {
#if defined(__linux__) && defined(RUSAGE_THREAD)
	struct rusage ru;
	if (getrusage(RUSAGE_THREAD, &ru) == 0) return ru.ru_nivcsw;
#endif
	return 0;
}
static double thread_cpu_ms(void) {
	struct timespec t;
#ifdef __wasi__
	if (clock_gettime(CLOCK_MONOTONIC, &t) != 0) return 0;
#else
	if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t) != 0) return 0;
#endif
	return t.tv_sec * 1000.0 + t.tv_nsec / 1e6;
}
static int cpu_mhz(int cpu) {
#ifdef __linux__
	if (cpu < 0) return 0;
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_cur_freq", cpu);
	FILE* f = fopen(path, "r");
	long k = 0;
	if (f) {
		if (fscanf(f, "%ld", &k) != 1) k = 0;
		fclose(f);
	}
	return (int) (k / 1000);
#else
	(void) cpu;
	return 0;
#endif
}
static int current_cpu(void) {
#if defined(__linux__) && defined(SYS_getcpu)
	unsigned cpu = 0;
	return syscall(SYS_getcpu, &cpu, NULL, NULL) == 0 ? (int) cpu : -1;
#else
	return -1;
#endif
}

#define GBA_FPS (16777216.0 / 280896.0)

static Config* cfg;
static Emu emu;

/* ------------------------------------------------- frame buffer (option)
 * With frame_buffer on, the next frame is emulated on this thread while the
 * game loop draws and shows the one before: a frame that runs long (the
 * game thread kept off the CPU for a few ms, common on Android) no longer
 * misses the screen's refresh, at the cost of one frame of input delay.
 * The worker only runs emu_frame; everything else that touches the core
 * (sound, RetroAchievements, map, probes, states, cheats, the menu) stays
 * on the game loop and runs while the worker is idle: the loop waits for
 * it at the top of each iteration and starts it again just before drawing. */
static struct {
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, busy, quit;
	uint32_t keys;
} ahead = { .lock = PTHREAD_MUTEX_INITIALIZER, .cond = PTHREAD_COND_INITIALIZER };
static bool aheadInFlight; /* the worker is emulating the next frame */
static bool aheadReady;    /* it has: that frame is emulated but not yet used */
static uint32_t aheadFb[GBA_W * GBA_H]; /* the frame on screen, while the worker writes the next */

/* Bits above the GBA keys, for run_frame: the SNES layout's camera pan
 * (profile controls.snes) left / right while a shoulder is held. */
#define KX_PAN_L (1u << 20)
#define KX_PAN_R (1u << 21)
static bool debug; /* GBADS_DEBUG (declared again below) */
static const struct MapProfile* ctlProf; /* NULL: no alternate layout for this game */

/* One frame of the game with these keys (on the worker too, while the main
 * thread leaves the core alone). */
static void run_frame(uint32_t keys) {
	uint32_t want = keys & (KX_PAN_L | KX_PAN_R);
	if (want && ctlProf && ctlProf->controls.snes.pan) {
		const typeof(ctlProf->controls.snes)* sn = &ctlProf->controls.snes;
		if (memprobes_match(sn->when, 3, &emu)) {
			emu_write8(&emu, sn->pan, want & KX_PAN_L ? 2 : 1);
			static bool told;
			if (!told) told = true, fprintf(stderr, "camera pan: first pan (%s, game mode %02x)\n", want & KX_PAN_L ? "left" : "right", emu_read8(&emu, sn->when[0].addr));
		} else {
			static bool told; /* (for reports: once) */
			if (!told) told = true, fprintf(stderr, "camera pan: not now (game mode %02x, paused %02x)\n", emu_read8(&emu, sn->when[0].addr), emu_read8(&emu, sn->when[2].addr));
		}
	}
	emu_frame(&emu, keys & 0x3FF);
}

static void* ahead_worker(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	int nice = android_raise_thread();
#else
	int nice = 0;
#endif
	fprintf(stderr, "frame buffer: emulation thread started%s\n", nice ? " (priority raised)" : "");
	pthread_mutex_lock(&ahead.lock);
	for (;;) {
		while (!ahead.busy && !ahead.quit) pthread_cond_wait(&ahead.cond, &ahead.lock);
		if (ahead.quit) break;
		uint32_t keys = ahead.keys;
		pthread_mutex_unlock(&ahead.lock);
		run_frame(keys);
		pthread_mutex_lock(&ahead.lock);
		ahead.busy = false;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return NULL;
}

/* false: no thread (emulate inline instead) */
static bool ahead_start(uint32_t keys) {
	pthread_mutex_lock(&ahead.lock);
	if (!ahead.started) {
		ahead.quit = false;
		ahead.started = pthread_create(&ahead.thread, NULL, ahead_worker, NULL) == 0;
	}
	bool ok = ahead.started;
	if (ok) {
		ahead.keys = keys;
		ahead.busy = true;
		pthread_cond_broadcast(&ahead.cond);
	}
	pthread_mutex_unlock(&ahead.lock);
	return ok;
}

/* Wait for the frame being emulated, if any (then the core is ours again). */
static void ahead_sync(void) {
	if (!aheadInFlight) return;
	pthread_mutex_lock(&ahead.lock);
	while (ahead.busy) pthread_cond_wait(&ahead.cond, &ahead.lock);
	pthread_mutex_unlock(&ahead.lock);
	aheadInFlight = false;
	aheadReady = true;
}

static void ahead_stop(void) {
	ahead_sync();
	aheadReady = false;
	pthread_mutex_lock(&ahead.lock);
	bool started = ahead.started;
	ahead.quit = true;
	pthread_cond_broadcast(&ahead.cond);
	pthread_mutex_unlock(&ahead.lock);
	if (started) pthread_join(ahead.thread, NULL);
	ahead.started = false;
}
static MapCapture* mc;
static GamePaths gp;

static SDL_Texture* gameTex[2];
static SDL_Texture* mapTex[2];
/* profile.combat: in a fight, the second screen is the map's background
 * with the logo small and dimmed (combatTex, mapW x mapH, made once) */
static const MapProfile* combatProf;
static SDL_Texture* combatTex[2];
static bool combatOn, combatMade;
static uint32_t* combatPx; /* its pixels */
static bool combatInGame; /* (hud.logoUnder) the HUD's first probe matches: in a chapter */
static SDL_Texture* hudTex[2]; /* the HUD's second screen (HUD_CW x HUD_CH) */
static SDL_Texture* logoTex[2];
/* the logo centred in a GBA-sized picture, so it's scaled (and gridded)
 * like the game; NULL for a user's high-resolution logo */
static SDL_Texture* logoCanvasTex[2];
/* user overlay PNGs: [0] game screen ("primary"), [1] map screen ("secondary") */
static SDL_Texture* overlayTex[2][2];
static int overlayW[2], overlayH[2];
static char overlayMode[2][16];
static int logoW, logoH;
static bool logoPixel;
static MapResult mapRes;
static int mapW = GBA_W, mapH = GBA_H;
static int mapShownIdx = -1;
/* "Map Animations: Off" shows this composed still instead of mapRes.frames[] */
#define STILL_IDX MAP_MAX_FRAMES
static uint32_t* stillPx;        /* mapW * mapH */
static int stillW, stillH;
static bool stillValid;
/* The map button did nothing at the last attempt (A Link to the Past indoors
 * without a map): keep showing the last map, dimmed. */
static bool mapInactive;
static bool logoOnly; /* the game has a profile but no map: logo on the second screen */
static const MapProfile* snapProf; /* logo-only game with screens copied to the second screen */
/* Mario Kart: the race HUD on the second screen (profile .hud) */
static const MapProfile* hudProf;
static uint32_t hudPx[GBA_W * GBA_H];
static bool hudOn; /* the frame being emulated is split */
/* the second screen's picture: the HUD at 2x over the backdrop, its map
 * larger; the backdrop, made from the grabbed layers as each race starts */
#define HUD_CW (GBA_W * 2)
#define HUD_CH (GBA_H * 2)
static uint32_t hudCanvas[HUD_CW * HUD_CH], hudBack[HUD_CW * HUD_CH], hudGrab[GBA_W * GBA_H];
static bool hudBackValid;
static int hudFrames; /* split frames since the race (re)started */
static int mapX0, mapY0, mapX1, mapY1; /* the map's box on the HUD, measured over the first frames */
static bool mapFound;
static SpriteRule hudWorldObjs[4]; /* hud.world.objs, the same way */
static SpriteRule hudObjs[8]; /* hudProf's hud.objs, as used (see romPatch) */
static int hudFadeEff, hudFadeIn; /* the last full fade (2: white, 3: black); frames to fade the next backdrop in */
static long hudPalRef[8]; /* bg_pal_levels() as the backdrop was taken */
static unsigned hudKeep; /* BG layers kept on the game screen (FFTA's spells) */
static bool hudWorld; /* FFTA's world map (hud.world) */
static bool hudScene; /* the profile's other scene (Mario Kart's podium): logo backdrop, its own sprites */
static bool hudAlt; /* the HUD on its other layer (hud.alt: SMA2's boss rooms) */
static uint32_t hudLogoBack[HUD_CW * HUD_CH];
static bool hudLogoDone;
static bool hudShown; /* the second screen shows the HUD picture (hudTex) */
/* profile hud.overMap: this frame's HUD is drawn into the map picture;
 * the map picture needs uploading again (the HUD changed) */
static bool overOn, overDirty;
static bool overAlone; /* a piece that goes alone is in the map picture (shown even in a fight) */
/* the HUD's pieces as last seen (hud.overMap): the pixels inside each
 * move's box, cut to what's there (0: nothing); latched pieces wait for
 * a new value at their latch address, then for the piece to hold still */
typedef struct {
	bool valid;
	int w, h;
	uint32_t px[GBA_W * GBA_H];
	uint32_t key;
	int cw, ch, cx, cy, still; /* a latched piece's size and place (a settling one's size), and for how long */
	uint32_t cand[GBA_W * GBA_H]; /* a settling piece's pixels */
	int gone; /* frames missing while the HUD's on (HudMove.dropAfter) */
	bool stale; /* a state was loaded: taken again (latched or not) as soon as it holds still */
	int sx, sy; /* where it was on the game screen (its top left) */
	bool atBottom; /* it reached the game screen's bottom edge */
} Piece;
static Piece pieces[4];
static void upload_map_frame(int idx);
static const uint32_t* hud_over_map(const uint32_t* px);
static void make_combat_canvas(void);
static uint32_t hudLogoBelow[HUD_CW * HUD_CH]; /* profile hud.logoBelow: black, the logo at half size */
static bool hudLogoBelowDone;
static int snapShow, snapRevert;   /* consecutive frames matching show / a revert screen */

static uint64_t mapAnimStart, frameCount;
static bool singleShowsMap;
static double fps, speedPct;
static Uint64 emuTicks, drawTicks; /* time spent emulating / drawing, for the 5 s report */

/* Slow frames (work over a frame's time): the worst each second, broken down,
 * logged on Android (where frame time is tight) or with GBADS_DEBUG. */
typedef struct {
	double total, input, emulate, ra, map, draw, top, bottom;
	int redraws;

	unsigned long long frame;
	long preempted; /* times the thread was put off its core during the frame */
	int cpu, mhz;   /* where it ended, and that core's clock then */
	double cpuMs;   /* time the thread actually ran (the rest it waited) */
	double waitEmu, waitDraw; /* of the waiting: during emulation, during drawing */
	double audio;   /* of emulating: handing the sound to SDL (its lock is shared with the audio thread) */
} SlowFrame;
static SlowFrame slowWorst;
static int slowCount;

static void slow_frame_note(SlowFrame f) {
	++slowCount;
	if (f.total > slowWorst.total) slowWorst = f;
}

static void slow_frame_report(bool debug) {
#ifndef __ANDROID__
	if (!debug) {
		slowCount = 0;
		slowWorst.total = 0;
		return;
	}
#endif
	if (!slowCount) return;
	const SlowFrame* w = &slowWorst;
	fprintf(stderr,
	        "slow frames: %d in the last second; worst (frame %llu) %.1f ms: input/menu %.1f, emulating %.1f (sound hand-off %.1f), "
	        "RetroAchievements %.1f, map %.1f, drawing %.1f (top screen %s %.1f, bottom %s %.1f); "
	        "ran %.1f ms of it (waited %.1f: %.1f while emulating, %.1f while drawing), preempted %ld time(s), on cpu %d at %d MHz\n",
	        slowCount, w->frame, w->total, w->input, w->emulate, w->audio, w->ra, w->map, w->draw,
	        w->redraws & 1 ? "redrawn" : "kept", w->top, w->redraws & 2 ? "redrawn" : "kept", w->bottom, w->cpuMs, w->total - w->cpuMs, w->waitEmu, w->waitDraw, w->preempted, w->cpu, w->mhz);
	slowCount = 0;
	slowWorst.total = 0;
}
static bool debug;

/* ---------------------------------------------------------------- menu */

enum MenuItem { M_RESUME, M_CHEATS, M_ACH, M_SHOT, M_SWAP, M_SCALE, M_LCD, M_OVERLAY, M_FPS, M_SAVE, M_LOAD, M_RESTART, M_LIBRARY, M_EXIT, M_FRAMEBUF, M_LOGO, M_MATCH, M_REWIND };
static const char* menuLabels[] = {
	/* title case: drawn in Futura from the built-in images (tools/make_brand.py) */
	[M_RESUME] = "Resume", [M_SWAP] = "Swap Screens", [M_SAVE] = "Save State", [M_SCALE] = "", [M_LCD] = "", [M_FPS] = "", [M_FRAMEBUF] = "", [M_OVERLAY] = "", [M_LOGO] = "", [M_MATCH] = "", [M_REWIND] = "", [M_RESTART] = "Restart Game",
	[M_LOAD] = "Load State", [M_CHEATS] = "Cheats", [M_ACH] = "Achievements", [M_SHOT] = "Take Screenshot", [M_LIBRARY] = "Close Game", [M_EXIT] = "Close GBA DS",
};
static int menuItems[12], nMenuItems;
/* two pages, switched with L1/R1 or the tabs */
#define N_PAGES 3
static int pages[N_PAGES][12], pageLen[N_PAGES], curPage;
/* Buttons still held when the menu closes (or after a HOME combo) are kept
 * from the game until released, so e.g. the A that picked "Save state"
 * doesn't make Samus jump. */
static bool suppress[B_COUNT];
#define HELD(b) (btnNow[b] && !suppress[b])
static void suppress_held(void) {
	for (int b = 0; b < B_COUNT; ++b) suppress[b] = btnNow[b];
}
static const char* pageNames[N_PAGES] = { "Game", "Display", "Extras" };
static bool restartArmed;
static bool menuOpen;
static int menuSel;
static int menuPressed = -1;      /* item under a finger that went down in the menu */
static bool ignorePointerUp;      /* the tap that opened the menu */
static char menuNote[64];

static const SDL_Color C_BTN = { 34, 30, 58, 240 };
static const SDL_Color C_BTN_SEL = { 92, 52, 140, 250 };
static const SDL_Color C_BTN_EDGE = { 150, 120, 210, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIS = { 100, 100, 120, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };

static Cheats cheats;
static SDL_Rect tab_rect(int scr, int t);
static SDL_Rect ach_area(int scr);

/* Every label a pause-menu button can show, for sizing the text so the
 * widest fits (item_label gives the current one). */
static const char* const allLabels[] = {
	"Resume", "Restart Game", "Tap Again to Restart", "Save State", "Load State", "Cheats", "Achievements",
	"Take Screenshot", "Close Game", "Close GBA DS", "Swap Screens", "Scale: Pixel Perfect", "Scale: Full Width",
	"Scale: Zoom", "Scale: Stretch", "LCD Grid: On", "LCD Grid: Off", "Overlays: None", "Overlays: Primary",
	"Overlays: Secondary", "Overlays: Both", "No Second Screen: Show Logo", "No Second Screen: Keep Blank",
	"Map Animations: On", "Map Animations: Off", "Map Refresh: Every 3 s", "Map Refresh: Every 1.5 s",
	"Map Refresh: Every 1 s", "Map Refresh: Every 0.5 s", "Map Refresh: Custom", "Frame Buffer: On",
	"Frame Buffer: Off", "Match Display Rate: On", "Match Display Rate: Off", "Rewind: On (Hold L2)", "Rewind: Off",
	"Show FPS: On", "Show FPS: Off",
};

/* The pause menu's layout: buttons across the whole screen (a small margin
 * at the edges), all the same size on every page, as tall as the page with
 * the most buttons allows (with the page tabs above and a line for notes
 * below), and the text as big as the button height and the widest label
 * allow. */
typedef struct {
	int margin, w, h, gap; /* buttons: x = margin, width w, height h, gap between them */
	int tabY, top;         /* the tab row's y; the first button's y */
	int cap, tabCap, noteCap, pad, rad;
	int prompt;            /* L1 / R1 prompt width, beside the tabs */
} MenuGeom;

static MenuGeom menu_geom(int scr) {
	static MenuGeom cache;
	static int cw = -1, ch = -1, cn = -1;
	SDL_Rect r = screens[scr].rect;
	int n = 1;
	for (int p = 0; p < N_PAGES; ++p) n = SDL_max(n, pageLen[p]);
	if (r.w == cw && r.h == ch && n == cn) return cache;
	MenuGeom g;
	float px = ui_px(scr);
	g.margin = px * 4;
	g.w = r.w - g.margin * 2;
	/* margin, tab row (h), gap (h * 0.4), n rows (h) with gaps (h * 0.2),
	 * the note line (h * 0.7), margin */
	g.h = (int) ((r.h - g.margin * 2) / (1.0 + 0.4 + n + 0.2 * (n - 1) + 0.7));
	g.gap = g.h / 5;
	g.tabY = g.margin;
	g.top = g.tabY + g.h + g.h * 2 / 5;
	g.pad = g.h / 3;
	g.rad = SDL_max(2, g.h / 5);
	/* text: capitals at most 40% of the button, and the widest label fits */
	int ref = 100, widest = 1;
	for (size_t i = 0; i < sizeof(allLabels) / sizeof(*allLabels); ++i)
		widest = SDL_max(widest, ui_label_width(scr, ref, allLabels[i]));
	g.cap = SDL_min(g.h * 2 / 5, (g.w - g.pad * 2) * ref / widest);
	g.noteCap = g.cap * 4 / 5;
	/* the tabs share the row between the L1 / R1 prompts, each as wide as its name needs */
	g.prompt = g.h * 9 / 10;
	int tabsW = g.w - (g.prompt + g.gap) * 2 - g.gap * (N_PAGES - 1), names = 0;
	for (int t = 0; t < N_PAGES; ++t) names += ui_label_width(scr, ref, pageNames[t]);
	/* each tab: its name plus pad on both sides */
	g.tabCap = SDL_min(g.cap, (tabsW - g.pad * 2 * N_PAGES) * ref / SDL_max(1, names));
	g.rad = SDL_max(2, g.h / 5);
	cache = g, cw = r.w, ch = r.h, cn = n;
	return g;
}


static bool item_enabled(int item) {
	if (item == M_CHEATS) return cheats.n > 0;
	return item != M_LOAD || file_exists(gp.state);
}

/* ---------------------------------------------------------------- cheats screen
 *
 * "Cheats" on the Game page opens a list of the game's cheats with an On/Off
 * pill each: D-pad / A to toggle, L/R a page at a time, B (or the Back box)
 * back to the menu. Touch: tap a cheat to toggle it, drag to scroll. */
static bool cheatsOpen;
/* "Achievements" (RetroAchievements signed in): the list from ra.c, under
 * the same Back / title boxes. D-pad scrolls, L/R a page, B back. */
static bool achOpen;
static int achPressY, achPressScroll;
static bool achPressed;
static int cheatSel;                 /* row index */
static int cheatScroll;              /* pixels */
static int cheatRows[CHEAT_MAX_FILES + 1024], nCheatRows; /* >= 0 cheat index, < 0 file header (-1 - file) */
static int cheatPressRow = -1, cheatPressY, cheatPressScroll;
static bool cheatDragging;

static void build_cheat_rows(void) {
	nCheatRows = 0;
	for (int f = 0; f < cheats.nFiles; ++f) {
		if (cheats.nFiles > 1) cheatRows[nCheatRows++] = -1 - f;
		for (int i = 0; i < cheats.n && nCheatRows < (int) SDL_arraysize(cheatRows); ++i) {
			if (cheats.items[i].file == f) cheatRows[nCheatRows++] = i;
		}
	}
}

typedef struct {
	float px;
	int tpx, top, rowH, gap, x, w, listH, footY;
} CheatGeom;

static CheatGeom cheat_geom(int scr) {
	CheatGeom g;
	SDL_Rect r = screens[scr].rect;
	g.px = ui_px(scr);
	g.tpx = SDL_max(2, g.px * 3 / 4);
	g.top = g.px * 26;
	g.rowH = g.tpx * 7 + g.px * 5;
	g.gap = g.px * 2;
	g.x = g.px * 4; /* the pause menu's margin */
	g.w = r.w - g.px * 8;
	g.footY = r.h - g.tpx * 9 * 2 - g.px * 5;
	g.listH = g.footY - g.px * 6 - g.top;
	return g;
}

static int cheat_max_scroll(const CheatGeom* g) {
	return SDL_max(0, nCheatRows * (g->rowH + g->gap) - g->gap - g->listH);
}

static void cheat_keep_visible(int scr) {
	CheatGeom g = cheat_geom(scr);
	int y = cheatSel * (g.rowH + g.gap);
	if (y < cheatScroll) cheatScroll = y;
	if (y + g.rowH > cheatScroll + g.listH) cheatScroll = y + g.rowH - g.listH;
	cheatScroll = SDL_clamp(cheatScroll, 0, cheat_max_scroll(&g));
}

static void cheat_move(int dir) {
	int i = cheatSel;
	for (int k = 0; k < nCheatRows; ++k) {
		i += dir;
		if (i < 0 || i >= nCheatRows) return;
		if (cheatRows[i] >= 0) {
			cheatSel = i;
			cheat_keep_visible(ui_touch_screen());
			return;
		}
	}
}

static void open_cheats(void) {
	build_cheat_rows();
	cheatsOpen = true;
	cheatSel = 0;
	cheatScroll = 0;
	if (nCheatRows && cheatRows[0] < 0) cheat_move(1);
	cheatPressRow = -1;
}

static void toggle_cheat(int row) {
	if (row < 0 || row >= nCheatRows || cheatRows[row] < 0) return;
	Cheat* c = &cheats.items[cheatRows[row]];
	c->on = !c->on;
	cheats_apply(&cheats, &emu);
	cheats_save(&cheats);
}

/* row under screen-local y, or -1 */
static int cheat_row_at(int scr, int x, int y) {
	CheatGeom g = cheat_geom(scr);
	if (y < g.top || y >= g.top + g.listH || x < g.x || x >= g.x + g.w) return -1;
	int ly = y - g.top + cheatScroll;
	int row = ly / (g.rowH + g.gap);
	if (ly % (g.rowH + g.gap) >= g.rowH || row >= nCheatRows) return -1;
	return row;
}

/* fits s into maxChars, ending in "..." when cut */
/* s in Jost, cut short with "..." if it's wider than maxW. */
static void fit_text(char* out, size_t size, int font, int capPx, const char* s, int maxW) {
	snprintf(out, size, "%s", s);
	if (ui_font_width(font, capPx, out) <= maxW) return;
	size_t n = strlen(out);
	while (n > 0) {
		do --n; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80); /* whole UTF-8 characters */
		snprintf(out, size, "%.*s...", (int) n, s);
		if (ui_font_width(font, capPx, out) <= maxW) return;
	}
}

static void draw_cheats(int scr) {
	CheatGeom g = cheat_geom(scr);
	SDL_Rect r = screens[scr].rect;
	float px = g.px;
	int rad = menu_geom(scr).rad, cap = menu_geom(scr).cap;
	/* top: Back box (where the Game tab is) and the title */
	SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
	ui_fill_round(scr, b, C_BTN, rad);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
	ui_fill_round(scr, t, C_BTN_EDGE, rad);
	ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Cheats", (SDL_Color) { 20, 16, 36, 255 });
	SDL_Rect clip = { screens[scr].rect.x, screens[scr].rect.y + g.top, r.w, g.listH };
	ui_clip(scr, &clip);
	int tcap = g.tpx * 5; /* Jost capitals, about the pixel font's height */
	int pillW = ui_label_width(scr, tcap, "Off") + px * 8;
	int nameW = g.w - pillW - px * 10;
	for (int i = 0; i < nCheatRows; ++i) {
		int y = g.top + i * (g.rowH + g.gap) - cheatScroll;
		if (y + g.rowH <= g.top || y >= g.top + g.listH) continue;
		char line[300];
		if (cheatRows[i] < 0) {
			const CheatFile* f = &cheats.files[-1 - cheatRows[i]];
			fit_text(line, sizeof(line), TF_BOLD, tcap, f->label, g.w - px * 4);
			ui_font_text(scr, TF_BOLD, g.x + px * 2, y + g.rowH / 2, tcap, line, C_GOLD, -1);
			continue;
		}
		const Cheat* c = &cheats.items[cheatRows[i]];
		bool sel = i == cheatSel;
		SDL_Rect row = { g.x, y, g.w, g.rowH };
		if (sel) ui_fill_round(scr, (SDL_Rect) { row.x - px / 2, row.y - px / 2, row.w + px, row.h + px }, C_BTN_EDGE, rad);
		ui_fill_round(scr, row, sel ? C_BTN_SEL : C_BTN, rad);
		fit_text(line, sizeof(line), TF_BODY, tcap, c->desc, nameW);
		ui_font_text(scr, TF_BODY, g.x + px * 4, y + g.rowH / 2, tcap, line, C_TEXT, -1);
		SDL_Rect pill = { g.x + g.w - pillW - px * 3, y + px * 2, pillW, g.rowH - px * 4 };
		ui_fill_round(scr, pill, c->on ? (SDL_Color) { 64, 168, 112, 255 } : (SDL_Color) { 60, 56, 84, 255 }, px * 2);
		ui_label(scr, pill.x + pill.w / 2, pill.y + pill.h / 2, tcap, c->on ? "On" : "Off",
		         c->on ? C_TEXT : (SDL_Color) { 160, 156, 184, 255 });
	}
	ui_clip(scr, NULL);
	/* scroll bar */
	int maxS = cheat_max_scroll(&g);
	if (maxS > 0) {
		int total = g.listH + maxS;
		int bh = SDL_max(px * 6, g.listH * g.listH / total);
		int by = g.top + (g.listH - bh) * cheatScroll / maxS;
		ui_fill_round(scr, (SDL_Rect) { r.w - px * 4, by, px * 2, bh }, C_BTN_EDGE, px);
	}
	/* footer: the selected cheat's whole name when the row had to cut it */
	if (cheatSel >= 0 && cheatSel < nCheatRows && cheatRows[cheatSel] >= 0 &&
	    ui_font_width(TF_BODY, tcap, cheats.items[cheatRows[cheatSel]].desc) > nameW) {
		ui_font_wrap(scr, TF_BODY, px * 6, g.footY - tcap / 2, r.w - px * 12, tcap, cheats.items[cheatRows[cheatSel]].desc,
		             C_GOLD);
	}
	char count[48];
	snprintf(count, sizeof(count), "%d on", cheats_on(&cheats));
	ui_font_text(scr, TF_BODY, r.w - px * 4, r.h - g.tpx * 5, tcap, count, C_DIS, 1);
}

static SDL_Rect item_rect(int scr, int i) {
	MenuGeom g = menu_geom(scr);
	return (SDL_Rect) { g.margin, g.top + i * (g.h + g.gap), g.w, g.h };
}

/* box t of n across the top of the menu: the page tabs (n = N_PAGES), or
 * Back and the title on the cheats / achievements screens (n = 2) */
static SDL_Rect top_box(int scr, int t, int n) {
	MenuGeom g = menu_geom(scr);
	if (n == N_PAGES) {
		/* the page tabs between the L1 / R1 prompts: each as wide as its name needs, sharing the width */
		int x = g.margin + g.prompt + g.gap, avail = g.w - (g.prompt + g.gap) * 2 - g.gap * (n - 1);
		int need[N_PAGES], sum = 0;
		for (int i = 0; i < n; ++i) sum += need[i] = ui_label_width(scr, g.tabCap, pageNames[i]) + g.pad * 2;
		int used = 0;
		for (int i = 0; i < t; ++i) used += need[i];
		int x0 = x + used * avail / sum + t * g.gap, x1 = x + (used + need[t]) * avail / sum + t * g.gap;
		return (SDL_Rect) { x0, g.tabY, x1 - x0, g.h };
	}
	int tw = (g.w - g.gap * (n - 1)) / n;
	return (SDL_Rect) { g.margin + t * (tw + g.gap), g.tabY, tw, g.h };
}

static SDL_Rect tab_rect(int scr, int t) {
	return top_box(scr, t, 2);
}

/* The device's battery, 0-100, or -1 if unknown: the battery in
 * /sys/class/power_supply (ROCKNIX), else SDL's (Android). Read at most
 * every 5 seconds. */
static int battery_percent(void) {
	static int pct = -1;
	static Uint32 last;
	static bool read;
	Uint32 now = SDL_GetTicks();
	if (read && now - last < 5000) return pct;
	read = true;
	last = now;
	pct = -1;
#ifndef __ANDROID__
	DIR* d = opendir("/sys/class/power_supply");
	if (d) {
		struct dirent* de;
		while (pct < 0 && (de = readdir(d))) {
			if (de->d_name[0] == '.') continue;
			char path[512], type[32] = "";
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/type", de->d_name);
			FILE* f = fopen(path, "r");
			if (!f) continue;
			if (!fgets(type, sizeof(type), f)) type[0] = 0;
			fclose(f);
			if (strncmp(type, "Battery", 7)) continue;
			snprintf(path, sizeof(path), "/sys/class/power_supply/%s/capacity", de->d_name);
			f = fopen(path, "r");
			if (!f) continue;
			int v;
			if (fscanf(f, "%d", &v) == 1 && v >= 0 && v <= 100) pct = v;
			fclose(f);
		}
		closedir(d);
	}
#endif
	if (pct < 0) {
		int p = -1;
		SDL_PowerState st = SDL_GetPowerInfo(NULL, &p);
		if (st != SDL_POWERSTATE_UNKNOWN && st != SDL_POWERSTATE_NO_BATTERY && p >= 0) pct = p;
	}
	return pct;
}

/* Status text on the top (first) screen while paused, with a drop shadow:
 * align -1 at the top left, 1 at the top right. */
static void draw_status(const char* text, int align) {
	int s = 0; /* the top screen, whatever is on it */
	float px = ui_px(s);
	int cap = px * 5;
	int x = align < 0 ? (int) (px * 5) : screens[s].rect.w - (int) (px * 5), cy = px * 7;
	int sh = SDL_max(1, (int) (px / 2));
	ui_font_text(s, TF_BOLD, x + sh, cy + sh, cap, text, (SDL_Color) { 0, 0, 0, 255 }, align);
	ui_font_text(s, TF_BOLD, x, cy, cap, text, C_TEXT, align);
}

/* While paused: the time (24-hour HH:MM) at the top left of the top screen
 * and the battery level at the top right. */
static void draw_battery(void) {
	char buf[16];
	time_t now = time(NULL);
	struct tm tm;
	if (localtime_r(&now, &tm)) {
		snprintf(buf, sizeof(buf), "%02d:%02d", tm.tm_hour, tm.tm_min);
		draw_status(buf, -1);
	}
	int pct = battery_percent();
	if (pct >= 0) {
		snprintf(buf, sizeof(buf), "%d%%", pct);
		draw_status(buf, 1);
	}
}

static void set_page(int page) {
	curPage = page;
	nMenuItems = pageLen[page];
	memcpy(menuItems, pages[page], sizeof(int) * nMenuItems);
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	restartArmed = false;
}

static int tab_at(int scr, int x, int y, int n) {
	for (int t = 0; t < n; ++t) {
		SDL_Rect r = top_box(scr, t, n);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return t;
	}
	return -1;
}

static int item_at(int scr, int x, int y) {
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		if (x >= r.x && x < r.x + r.w && y >= r.y && y < r.y + r.h) return i;
	}
	return -1;
}

static const char* scale_label(const char* scale) {
	return !strcmp(scale, "fit")       ? "Scale: Full Width" :
	       !strcmp(scale, "zoom")      ? "Scale: Zoom" :
	       !strcmp(scale, "stretch")   ? "Scale: Stretch" : "Scale: Pixel Perfect";
}

static void draw_menu(void) {
	static SDL_Texture* shoulderTex[2][2]; /* L1, R1 prompts, per view */
	static int shoulderW, shoulderH;
	if (!shoulderTex[0][0] && !shoulderTex[0][1]) {
		ui_load_brand(BRAND_BTN_L1, shoulderTex[0], &shoulderW, &shoulderH);
		ui_load_brand(BRAND_BTN_R1, shoulderTex[1], &shoulderW, &shoulderH);
	}
	for (int s = 0; s < nScreens; ++s) ui_dim(s, 170);
	draw_battery();
	int scr = ui_touch_screen();
	float px = ui_px(scr);
	int cap = px * 11 / 2;   /* Futura capital height */
	int rad = px * 3;        /* box corner radius */
	if (achOpen) {
		cap = menu_geom(scr).cap;
		rad = menu_geom(scr).rad;
		SDL_Rect b = tab_rect(scr, 0), t = tab_rect(scr, 1);
		ui_fill_round(scr, b, C_BTN, rad);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, cap, "Back", C_TEXT);
		ui_fill_round(scr, t, C_BTN_EDGE, rad);
		ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, cap, "Achievements", (SDL_Color) { 20, 16, 36, 255 });
		ra_list_draw(scr, ach_area(scr));
		if (nScreens > 1) {
			int o = ui_other_screen();
			ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
		}
		return;
	}
	if (cheatsOpen) {
		draw_cheats(scr);
		if (nScreens > 1) {
			int o = ui_other_screen();
			ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
		}
		return;
	}
	MenuGeom mg = menu_geom(scr);
	cap = mg.cap;
	rad = mg.rad;
	for (int t = 0; t < N_PAGES; ++t) {
		SDL_Rect r = top_box(scr, t, N_PAGES);
		bool on = t == curPage;
		ui_fill_round(scr, r, on ? C_BTN_EDGE : C_BTN, rad);
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, mg.tabCap, pageNames[t], on ? (SDL_Color) { 20, 16, 36, 255 } : C_DIS);
	}
	SDL_Rect t0 = top_box(scr, 0, N_PAGES), t1 = top_box(scr, N_PAGES - 1, N_PAGES);
	/* L1 / R1 button prompts beside the tabs (the prompt art is 25:16) */
	int gw = mg.prompt, gh = gw * 16 / 25;
	SDL_Rect g0 = { mg.margin, t0.y + (t0.h - gh) / 2, gw, gh };
	SDL_Rect g1 = { mg.margin + mg.w - gw, t1.y + (t1.h - gh) / 2, gw, gh };
	if (shoulderTex[0][0] || shoulderTex[0][1]) {
		ui_image(scr, shoulderTex[0], shoulderW, shoulderH, "stretch", &g0);
		ui_image(scr, shoulderTex[1], shoulderW, shoulderH, "stretch", &g1);
	} else {
		ui_text(scr, t0.x - px * 14, t0.y + (t0.h - px * 7) / 2, px, "L1", C_DIS);
		ui_text(scr, t1.x + t1.w + px * 3, t1.y + (t1.h - px * 7) / 2, px, "R1", C_DIS);
	}
	for (int i = 0; i < nMenuItems; ++i) {
		SDL_Rect r = item_rect(scr, i);
		bool sel = i == menuSel;
		int e = SDL_max(1, mg.h / 28); /* the selected button's edge */
		if (sel) ui_fill_round(scr, (SDL_Rect) { r.x - e, r.y - e, r.w + e * 2, r.h + e * 2 }, C_BTN_EDGE, rad + e);
		ui_fill_round(scr, r, sel ? C_BTN_SEL : C_BTN, rad);
		bool en = item_enabled(menuItems[i]);
		const char* label = menuLabels[menuItems[i]];
		if (menuItems[i] == M_SCALE) label = scale_label(cfg->scale);
		if (menuItems[i] == M_LCD) label = cfg->lcdGrid ? "LCD Grid: On" : "LCD Grid: Off";
		if (menuItems[i] == M_FPS) label = cfg->showFps ? "Show FPS: On" : "Show FPS: Off";
		if (menuItems[i] == M_FRAMEBUF) label = cfg->frameBuffer ? "Frame Buffer: On" : "Frame Buffer: Off";
		if (menuItems[i] == M_REWIND) label = cfg->rewind ? "Rewind: On (Hold L2)" : "Rewind: Off";
		if (menuItems[i] == M_MATCH) label = cfg->matchDisplay ? "Match Display Rate: On" : "Match Display Rate: Off";
		if (menuItems[i] == M_LOGO) label = cfg->showLogo ? "No Second Screen: Show Logo" : "No Second Screen: Keep Blank";
		if (menuItems[i] == M_OVERLAY) {
			static const char* const ovl[4] = { "Overlays: None", "Overlays: Primary", "Overlays: Secondary", "Overlays: Both" };
			label = ovl[cfg->overlays & OVL_BOTH];
		}
		if (menuItems[i] == M_RESTART && restartArmed) label = "Tap Again to Restart";
		ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, cap, label, en ? C_TEXT : C_DIS);
	}
	if (menuNote[0]) {
		SDL_Rect last = item_rect(scr, nMenuItems - 1);
		int nc = SDL_min(mg.noteCap, mg.noteCap * mg.w / SDL_max(1, ui_font_width(TF_BOLD, mg.noteCap, menuNote)));
		ui_font_text(scr, TF_BOLD, screens[scr].rect.w / 2, last.y + last.h + mg.h * 3 / 5, nc, menuNote, C_GOLD, 0);
	}
	if (nScreens > 1) {
		int o = ui_other_screen();
		ui_label(o, screens[o].rect.w / 2, screens[o].rect.h / 2, (ui_px(o) + 1) * 7 * 34 / 35, "Paused", C_GOLD);
	}
}

/* ---------------------------------------------------------------- helpers */

static int probes_hit(const uint32_t* fb, const Probe* p, int tol) {
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
static void snapshot_hiding_sprites(const SpriteRule* hide, int n, const SpriteRule* kept, int nKept, uint32_t* out) {
	static void* st;
	static size_t stSize;
	static uint32_t raw[GBA_W * GBA_H];
	size_t sz = emu_state_size(&emu);
	if (sz > stSize) {
		free(st);
		st = malloc(sz);
		stSize = st ? sz : 0;
	}
	if (!st || !emu_state_save(&emu, st)) {
		memcpy(out, emu.fb, sizeof(raw));
		return;
	}
	memcpy(raw, emu.fb, sizeof(raw));
	SpriteRule all[8];
	int na = 0;
	for (int i = 0; i < nKept && na < 8; ++i) all[na++] = kept[i];
	for (int i = 0; i < n && na < 8; ++i) all[na++] = hide[i];
	emu_hide_sprites(&emu, all, na);
	emu_state_load(&emu, st);
	emu_frame(&emu, 0);
	memcpy(out, emu.fb, sizeof(raw));
	emu_hide_sprites(&emu, nKept ? kept : NULL, nKept);
	emu_state_load(&emu, st);
	int16_t drop[2048];
	while (emu_audio_read(&emu, drop, 1024) > 0) {}
	memcpy(emu.fb, raw, sizeof(raw));
}

/* Logo-only games: copy a recognised screen (THPS2 goal sheet) to the second
 * screen; back to the logo at the menus. */
/* The grabbed sky and scenery (the rows above the horizon), scaled up to
 * cover the canvas (centred), and dimmed well back. */
static bool frame_is_dark(const uint32_t* fb);
/* One colour all over, nearly: a fade to black or to white, a blank screen. */
static bool frame_is_flat(const uint32_t* fb) {
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
static int hudRegrab; /* hudFrames at which to grab the map layer again (0: kept) */
static bool hudSettle; /* after a fade: wait for the brightness to settle */
static uint32_t hudMapSum; /* the grabbed map's tiles (map_tiles_sum) */

/* How bright each of the first 8 BG palettes is (the map's; the menus use
 * the others): games that fade the screen by scaling their palettes. */
static void bg_pal_levels(long out[8]) {
	for (int p = 0; p < 8; ++p) {
		out[p] = 0;
		for (int i = 0; i < 16; ++i) {
			uint16_t c = emu_read16(&emu, 0x05000000 + (p * 16 + i) * 2);
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
	uint16_t cnt = emu_read16(&emu, 0x04000008 + bg * 2);
	uint32_t base = 0x06000000 + ((cnt >> 2) & 3) * 0x4000, sum = 2166136261u;
	for (uint32_t a = 0; a < 0x2000; a += 4) sum = (sum ^ emu_read32(&emu, base + a)) * 16777619u;
	return sum;
}

static void hud_make_backdrop(void) {
	int h = SDL_clamp(hudProf->hud.backdropRows ? hudProf->hud.backdropRows : GBA_H, 1, GBA_H);
	if (hudProf->hud.mapCnt) {
		/* a map layer taken mid-fade, or with the camera on the sky, makes
		 * a poor picture: try again in half a second, until it's mostly
		 * there and not dark (then it stays) */
		uint32_t clear = emu_hud_clear();
		long drawn = 0, luma = 0;
		for (int i = 0; i < GBA_W * GBA_H; ++i) {
			uint32_t c = hudGrab[i];
			if ((c & 0xFFFFFF) == clear) continue;
			++drawn;
			luma += (c & 0xFF) + ((c >> 8) & 0xFF) + ((c >> 16) & 0xFF);
		}
		long level = luma / SDL_max(drawn, 1) / 3;
		bool good = drawn > GBA_W * GBA_H / 2 && level > 60 && !frame_is_flat(hudGrab);
		/* after a fade, only once the brightness has stopped rising */
		static long lastLevel;
		if (hudSettle && good && labs(level - lastLevel) > 3) good = false;
		lastLevel = level;
		hudSettle = !good; /* (a poor take: the next must also hold steady) */
		hudRegrab = good ? 0 : hudFrames + (hudSettle ? 5 : 30);
		if (debug) fprintf(stderr, "frame %llu: backdrop taken: %ld%% drawn, brightness %ld%s\n", (unsigned long long) frameCount,
		                   drawn * 100 / (GBA_W * GBA_H), luma / SDL_max(drawn, 1) / 3, good ? "" : " (again later)");
		if (!good) return; /* keep the last good one meanwhile (or none: black) */
	}
	if (!hudBackValid && hudFadeEff) hudFadeIn = 16; /* (after a fade out) */
	hudBackValid = true;
	bg_pal_levels(hudPalRef);
	if (debug) {
		char path[1200];
		snprintf(path, sizeof(path), "%s/hud-backdrop.bmp", cfg->stateDir);
		SDL_Surface* bs = SDL_CreateRGBSurfaceWithFormatFrom(hudGrab, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
		if (bs) SDL_SaveBMP(bs, path), SDL_FreeSurface(bs);
		fprintf(stderr, "frame %llu: backdrop from %d rows (%s)\n", (unsigned long long) frameCount, h, path);
	}
	if (!hudBackValid) return;
	/* where the layers drew nothing: black, not the 'nothing here' colour */
	uint32_t clear = emu_hud_clear();
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		if ((hudGrab[i] & 0xFFFFFF) == clear) hudGrab[i] = 0xFF000000;
	}
	bool grey = hudProf->hud.backdropGrey;
	float sc = SDL_max((float) HUD_CW / GBA_W, (float) HUD_CH / h);
	float ox = (GBA_W - HUD_CW / sc) / 2, oy = (h - HUD_CH / sc) / 2;
	for (int y = 0; y < HUD_CH; ++y) {
		float fy = oy + (y + 0.5f) / sc - 0.5f;
		int y0 = SDL_clamp((int) fy, 0, h - 1), y1 = SDL_min(y0 + 1, h - 1);
		float wy = SDL_clamp(fy - y0, 0.0f, 1.0f);
		for (int x = 0; x < HUD_CW; ++x) {
			float fx = ox + (x + 0.5f) / sc - 0.5f;
			int x0 = SDL_clamp((int) fx, 0, GBA_W - 1), x1 = SDL_min(x0 + 1, GBA_W - 1);
			float wx = SDL_clamp(fx - x0, 0.0f, 1.0f);
			uint32_t a = hudGrab[y0 * GBA_W + x0], b = hudGrab[y0 * GBA_W + x1];
			uint32_t c = hudGrab[y1 * GBA_W + x0], d = hudGrab[y1 * GBA_W + x1];
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
			hudBack[y * HUD_CW + x] = out;
		}
	}
}

/* The scene's backdrop: the game's logo on dark blue, as large as fits
 * (whole pixels for the extracted one), dimmed like the race backdrop. */
static void hud_make_logo_back(void) {
	hudLogoDone = true;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudLogoBack[i] = 0xFF1A1010;
	bool pixelArt;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (!lg) return;
	int mw = HUD_CW - 40, mh = HUD_CH - 40;
	int w = lg->w, h = lg->h;
	if (pixelArt) {
		int k = SDL_max(1, SDL_min(mw / lg->w, mh / lg->h));
		w = lg->w * k, h = lg->h * k;
	} else {
		float sc = SDL_min((float) mw / lg->w, (float) mh / lg->h);
		w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
	}
	int x0 = (HUD_CW - w) / 2, y0 = (HUD_CH - h) / 2;
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + (size_t) (y * lg->h / h) * lg->pitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lg->w / w) * 4;
			uint32_t* o = &hudLogoBack[(y0 + y) * HUD_CW + x0 + x];
			uint32_t out = 0xFF000000;
			for (int c = 0; c < 3; ++c) {
				int bg = (*o >> (c * 8)) & 0xFF;
				int v = bg + (int) ((p[c] * 0.32f - bg) * p[3] / 255.0f);
				out |= (uint32_t) SDL_clamp(v, 0, 255) << (c * 8);
			}
			*o = out;
		}
	}
	SDL_FreeSurface(lg);
}

/* Profile hud.logoBelow: black, with the game's logo at half the size the
 * second screen shows it otherwise (a pixel of an extracted logo per canvas
 * pixel), centred in the space below the HUD's rows. */
static void hud_make_logo_below(void) {
	hudLogoBelowDone = true;
	bool behind = hudProf->hud.logoBehind;
	for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudLogoBelow[i] = 0xFF000000;
	bool pixelArt;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (!lg) return;
	int top = 0; /* below the HUD: its lowest row on the canvas */
	for (int m = 0; m < 4; ++m) {
		const HudMove* mv = &hudProf->hud.moves[m];
		if (mv->toGame && !mv->how && !mv->vhow && mv->x0 == 0 && mv->x1 >= GBA_W - 1 && mv->y0 > 0) top = mv->y0 * 2;
	}
	int mw = GBA_W, mh = SDL_min(GBA_H, HUD_CH - top - 16);
	int w = lg->w, h = lg->h;
	if (w > mw || h > mh) {
		float sc = SDL_min((float) mw / lg->w, (float) mh / lg->h);
		w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
	}
	int x0 = (HUD_CW - w) / 2, y0 = top + (HUD_CH - top - h) / 2;
	unsigned dim = behind ? 90 : 255; /* behind the HUD: dimmed well back */
	SDL_LockSurface(lg);
	for (int y = 0; y < h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + (size_t) (y * lg->h / h) * lg->pitch;
		for (int x = 0; x < w; ++x) {
			const uint8_t* p = row + (size_t) (x * lg->w / w) * 4; /* RGBA32 */
			unsigned a = p[3] * dim / 255;
			hudLogoBelow[(y0 + y) * HUD_CW + x0 + x] = 0xFF000000u | (p[2] * a / 255) << 16 | (p[1] * a / 255) << 8 | p[0] * a / 255;
		}
	}
	SDL_UnlockSurface(lg);
	SDL_FreeSurface(lg);
}

static int rule_count(const SpriteRule* r, int max) {
	int n = 0;
	while (n < max && (r[n].x1 || r[n].tileMax)) ++n;
	return n;
}

/* The second screen from this frame's HUD (hudPx). While racing, the turn
 * sign goes back onto the game picture and the map is drawn larger. */
static void hud_compose(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	uint32_t clear = emu_hud_clear();
	bool racing = !hudScene && memprobes_match(h->layout, 4, &emu);
	{
		static int lastRacing = -1;
		if (debug && racing != lastRacing) fprintf(stderr, "frame %llu: HUD layout %s\n", (unsigned long long) frameCount, racing ? "racing" : "plain");
		lastRacing = racing;
	}
	/* the map's box: grown over the race's first frames, then kept */
	if (racing && h->map.x1 && hudFrames <= 8) {
		for (int y = h->map.y0; y <= h->map.y1; ++y) {
			for (int x = h->map.x0; x <= h->map.x1; ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (!mapFound) mapX0 = mapX1 = x, mapY0 = mapY1 = y, mapFound = true;
				mapX0 = SDL_min(mapX0, x), mapX1 = SDL_max(mapX1, x);
				mapY0 = SDL_min(mapY0, y), mapY1 = SDL_max(mapY1, y);
			}
		}
	}
	bool bigMap = racing && mapFound;
	int bx0 = SDL_max(mapX0 - 2, h->map.x0), by0 = SDL_max(mapY0 - 2, h->map.y0);
	int bx1 = SDL_min(mapX1 + 2, h->map.x1), by1 = SDL_min(mapY1 + 2, h->map.y1);
	/* the sign: only when nothing else of the HUD is on its rows nearby */
	bool sign = false;
	if (racing && h->sign.x1) {
		bool inside = false, beside = false;
		for (int y = h->sign.y0; y <= h->sign.y1 && !beside; ++y) {
			for (int x = SDL_max(0, h->sign.x0 - 40); x <= SDL_min(GBA_W - 1, h->sign.x1 + 40); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				if (x >= h->sign.x0 && x <= h->sign.x1) inside = true;
				else beside = true;
			}
		}
		sign = inside && !beside;
	}
	if (sign) {
		for (int y = h->sign.y0; y <= h->sign.y1; ++y) {
			for (int x = h->sign.x0; x <= h->sign.x1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) != clear) emu.fb[y * GBA_W + x] = c;
			}
		}
	}
	if (hudScene) {
		if (!hudLogoDone) hud_make_logo_back();
		memcpy(hudCanvas, hudLogoBack, sizeof(hudCanvas));
	} else if (h->logoBelow || h->logoBehind) {
		if (!hudLogoBelowDone) hud_make_logo_below();
		memcpy(hudCanvas, hudLogoBelow, sizeof(hudCanvas));
	} else if (hudBackValid) {
		memcpy(hudCanvas, hudBack, sizeof(hudCanvas));
		/* the game fading the whole screen to black or white: the
		 * backdrop too. By BLDCNT (every BG layer, darken or brighten, by
		 * BLDY, in 16ths) */
		uint16_t bld = emu_read16(&emu, 0x04000050);
		int fade = SDL_min(emu_read16(&emu, 0x04000054) & 31, 16), eff = (bld >> 6) & 3;
		if (!((bld & 0x0F) == 0x0F && (eff == 2 || eff == 3) && fade)) fade = 0;
		/* or by scaling its palettes (to black, or white): against how
		 * bright they were as the backdrop was taken (in 64ths) */
		int pf = fade ? 0 : bg_pal_fade(hudPalRef);
		if (pf <= -2) eff = 3, fade = -pf;
		else if (pf >= 2) eff = 2, fade = pf;
		else fade *= 4;
		fade = SDL_clamp(fade, 0, 64);
		/* faded out all the way: that backdrop is done (the next scene's
		 * comes in as the game fades in, or after: then fading in too) */
		if (fade >= 62 && h->mapCnt) {
			hudBackValid = false;
			hudFadeEff = eff;
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = eff == 2 ? 0xFFFFFFFF : 0xFF000000;
			fade = 0;
		} else if (hudFadeIn > 0) {
			/* a new backdrop, faded in over 16 frames from how the last
			 * scene faded out, or less if the game's own fade is less */
			int in = hudFadeIn-- * 4;
			if (in > fade || eff != hudFadeEff) eff = hudFadeEff, fade = in;
		}
		if (fade) {
			for (int i = 0; i < HUD_CW * HUD_CH; ++i) {
				uint32_t c = hudCanvas[i], o = 0xFF000000;
				for (int k = 0; k < 24; k += 8) {
					int v = (c >> k) & 0xFF;
					v = eff == 3 ? v - v * fade / 64 : v + (255 - v) * fade / 64;
					o |= (uint32_t) v << k;
				}
				hudCanvas[i] = o;
			}
		}
	} else {
		uint32_t plain = h->mapCnt ? (hudFadeEff == 2 ? 0xFFFFFFFF : 0xFF000000) : 0xFF1A1010;
		for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = plain;
	}
	/* moved pieces (the scene's, or racing's): how far each goes this frame */
	const HudMove* movesIn = hudScene ? h->scene.moves : racing ? h->moves : NULL;
	/* with a BG mosaic on (fading out through a pipe), the boxes' top and
	 * bottom go to the blocks' edges: a block across a box's edge is drawn
	 * from its first row, so it goes with the box above */
	HudMove movesM[4];
	const HudMove* moves = movesIn;
	int mosV = ((emu_io16(&emu, 0x4C) >> 4) & 15) + 1;
	if (movesIn && mosV > 1) {
		for (int m = 0; m < 4; ++m) {
			movesM[m] = movesIn[m];
			if (movesM[m].y0 > 0) movesM[m].y0 = (movesM[m].y0 + mosV - 1) / mosV * mosV;
			if (movesM[m].y1 < GBA_H - 1) movesM[m].y1 = (movesM[m].y1 + mosV) / mosV * mosV - 1;
		}
		moves = movesM;
	}
	int moveDx[4] = { 0 }, moveDy[4] = { 0 };
	bool moving[4] = { false };
	for (int m = 0; moves && m < 4; ++m) {
		const HudMove* mv = &moves[m];
		if (!mv->how && !mv->vhow && !mv->toGame) continue;
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = SDL_max(mv->y0, 0); y <= SDL_min(mv->y1, GBA_H - 1); ++y) {
			for (int x = SDL_max(mv->x0, 0); x <= SDL_min(mv->x1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = SDL_min(lo, x), hi = SDL_max(hi, x), top = SDL_min(top, y), bot = SDL_max(bot, y);
			}
		}
		if (hi < 0) continue;
		moving[m] = true;
		moveDx[m] = mv->how == 'C' ? mv->x - (lo + hi) / 2 : mv->how == 'R' ? mv->x - hi : mv->how == 'L' ? mv->x - lo : 0;
		moveDy[m] = mv->vhow == 'M' ? mv->y - (top + bot) / 2 : mv->vhow == 'B' ? mv->y - bot : mv->vhow == 'T' ? mv->y - top : 0;
	}
	for (int y = 0; y < GBA_H; ++y) {
		for (int x = 0; x < GBA_W; ++x) {
			uint32_t c = hudPx[y * GBA_W + x];
			if ((c & 0xFFFFFF) == clear) continue;
			int nx = x, ny = y;
			bool toGame = false;
			for (int m = 0; m < 4; ++m) {
				const HudMove* mv = &moves[m];
				if (moving[m] && x >= mv->x0 && x <= mv->x1 && y >= mv->y0 && y <= mv->y1) {
					nx = x + moveDx[m], ny = y + moveDy[m];
					toGame = mv->toGame;
					break;
				}
			}
			if (nx < 0 || nx >= GBA_W || ny < 0 || ny >= GBA_H) continue;
			if (toGame) {
				emu.fb[ny * GBA_W + nx] = c; /* back onto the game picture */
				continue;
			}
			if (sign && x >= h->sign.x0 && x <= h->sign.x1 && y >= h->sign.y0 && y <= h->sign.y1) continue;
			if (bigMap && x >= bx0 && x <= bx1 && y >= by0 && y <= by1) continue;
			uint32_t* o = &hudCanvas[ny * 2 * HUD_CW + nx * 2];
			o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = c;
		}
	}
	if (bigMap) {
		/* 2x if it fits comfortably right of the faces and below the top
		 * row (about 200 x 128 HUD pixels), else 1.5x; centred there */
		int w = bx1 - bx0 + 1, hh = by1 - by0 + 1;
		int q = (w * 2 <= 180 && hh * 2 <= 120) ? 4 : 3; /* canvas pixels per HUD pixel */
		int cx = 139 * 2, cy = 94 * 2;
		int tx = SDL_clamp(cx - w * q / 2, 0, HUD_CW - w * q), ty = SDL_clamp(cy - hh * q / 2, 0, HUD_CH - hh * q);
		for (int y = by0; y <= by1; ++y) {
			for (int x = bx0; x <= bx1; ++x) {
				uint32_t c = hudPx[y * GBA_W + x];
				if ((c & 0xFFFFFF) == clear) continue;
				for (int j = 0; j < q; ++j) {
					uint32_t* o = &hudCanvas[(ty + (y - by0) * q + j) * HUD_CW + tx + (x - bx0) * q];
					for (int i = 0; i < q; ++i) o[i] = c;
				}
			}
		}
	}
}

/* The BG layers whose tile maps mostly use palettes below pal (shown or
 * not: one turned on next frame is filled already) */
static unsigned effect_bgs(int pal) {
	const uint16_t* vram = emu_vram(&emu);
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) {
		uint16_t cnt = emu_read16(&emu, 0x04000008 + i * 2);
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
static bool hud_take_pieces(const typeof(hudProf->hud)* h) {
	uint32_t clear = emu_hud_clear();
	bool changed = false, seen[4] = { false };
	int found[4][4];
	if (h->findPieces) h->findPieces(&emu, hudPx, clear, found);
	for (int m = 0; m < 4; ++m) {
		const HudMove* mv = &h->moves[m];
		Piece* pc = &pieces[m];
		if (!mv->how && !mv->vhow && !mv->asIs) continue;
		int bx0 = mv->x0, by0 = mv->y0, bx1 = mv->x1, by1 = mv->y1;
		if (h->findPieces) bx0 = found[m][0], by0 = found[m][1], bx1 = found[m][2], by1 = found[m][3];
		int lo = GBA_W, hi = -1, top = GBA_H, bot = -1;
		for (int y = SDL_max(by0, 0); y <= SDL_min(by1, GBA_H - 1); ++y) {
			for (int x = SDL_max(bx0, 0); x <= SDL_min(bx1, GBA_W - 1); ++x) {
				if ((hudPx[y * GBA_W + x] & 0xFFFFFF) == clear) continue;
				lo = SDL_min(lo, x), hi = SDL_max(hi, x), top = SDL_min(top, y), bot = SDL_max(bot, y);
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
				uint32_t c = hudPx[(top + y) * GBA_W + lo + x];
				cut[y * w + x] = (c & 0xFFFFFF) == clear ? 0 : c | 0xFF000000;
			}
		}
		size_t n = (size_t) w * hh * 4;
		if (mv->latch) {
			/* only for a new value, once it's held still for 8 frames (in
			 * the same place, the same size: it may animate, as the CO
			 * power meter does when it's full) */
			uint32_t key = emu_read8(&emu, mv->latch);
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
				if (o != m) pieces[o].valid = false, pieces[o].still = 0;
			}
		}
	}
	return changed;
}

/* After each frame: show the HUD it drew, and decide whether the next
 * one is split. A frame is shown only if it was split and still looks like
 * racing (the first pause-menu frame is split too, and is dropped). */
/* hud.mirror: the game screen's last picture from before */
static uint32_t mirrorHeld[GBA_W * GBA_H];
static bool mirrorHave, mirrorOn;

static void update_hud(void) {
	const typeof(hudProf->hud)* h = &hudProf->hud;
	/* a conversation leaves the game screen only once there's a picture
	 * on the second screen to put it on */
	if (h->band) emu_hud_band(true, mapShownIdx < 0, h->band == BAND_WATCH);
	if (h->mirror[0].addr || h->mirrorOr[0].addr) {
		bool mirMain = h->mirror[0].addr && memprobes_match(h->mirror, 2, &emu);
		bool mir = mirMain || (h->mirrorOr[0].addr && memprobes_match(h->mirrorOr, 2, &emu));
		if (mir) {
			/* here, the picture from before (none after loading a state in
			 * the middle of it: left as it is; shown instead of emu.fb,
			 * which can't be written over: the renderer redraws only the
			 * lines that change) */
			if (!mirrorOn && debug) fprintf(stderr, "frame %llu: the screen mirrored\n", (unsigned long long) frameCount);
			mirrorOn = hudShown = true;
			if (mirMain && h->alt.on[0].addr && memprobes_match(h->alt.on, 4, &emu)) {
				/* hud.alt's probes too (a menu over the paused world): its
				 * layers alone, split as usual below; until the split's
				 * on, the background */
				if (!hudOn || !hudAlt) {
					if (h->logoBelow || h->logoBehind) {
						if (!hudLogoBelowDone) hud_make_logo_below();
						memcpy(hudCanvas, hudLogoBelow, sizeof(hudCanvas));
					} else {
						for (int i = 0; i < HUD_CW * HUD_CH; ++i) hudCanvas[i] = 0xFF000000;
					}
					for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
				}
			} else {
				/* the whole screen over there */
				for (int y = 0; y < GBA_H; ++y) {
					for (int x = 0; x < GBA_W; ++x) {
						uint32_t* o = &hudCanvas[y * 2 * HUD_CW + x * 2];
						o[0] = o[1] = o[HUD_CW] = o[HUD_CW + 1] = emu.fb[y * GBA_W + x] | 0xFF000000;
					}
				}
				for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
				/* (and the split off, if it was on) */
				if (hudOn) emu_hud_split(&emu, false, 0, NULL, 0, hudPx), hudOn = false;
				return;
			}
		} else {
			memcpy(mirrorHeld, emu.fb, sizeof(mirrorHeld));
			mirrorHave = true;
			if (mirrorOn) {
				mirrorOn = hudShown = false;
				if (debug) fprintf(stderr, "frame %llu: the screen mirrored: off\n", (unsigned long long) frameCount);
			}
		}
	}
	bool main = memprobes_match(h->on, 4, &emu);
	bool altOn = !main && h->alt.on[0].addr && memprobes_match(h->alt.on, 4, &emu);
	bool race = main || altOn || (hudOn && h->stay[0].addr && memprobes_match(h->stay, 2, &emu));
	/* the HUD's other layer (alt): kept while it stays on through a fade */
	bool alt = main ? false : altOn ? true : hudAlt;
	/* the map layer: which BG has it (none: not on a map) */
	int mapBg = -1;
	bool world = false;
	for (int i = 0; h->mapCnt && i < 4; ++i) {
		uint16_t cnt = emu_read16(&emu, 0x04000008 + i * 2);
		if (cnt == h->mapCnt) mapBg = i;
		else if (h->world.mapCnt && cnt == h->world.mapCnt) mapBg = i, world = true;
	}
	if (h->mapCnt && mapBg < 0) race = false;
	if (h->overMap) {
		/* into the map picture: each piece as last seen, kept while the
		 * HUD is hidden (menus, moves, fights) until it shows again; all
		 * gone off the battle map (the first `on` probe) */
		bool changed = false;
		if (!memprobes_match(h->on, 1, &emu)) {
			for (int m = 0; m < 4; ++m) changed |= pieces[m].valid, pieces[m].valid = false;
		} else if (hudOn && race) {
			changed = hud_take_pieces(h);
		} else {
			/* the split's off: a piece that goes alone (a conversation)
			 * isn't up any more */
			for (int m = 0; m < 4; ++m) {
				if (h->moves[m].alone && pieces[m].valid) pieces[m].valid = false, changed = true;
			}
		}
		bool any = pieces[0].valid || pieces[1].valid || pieces[2].valid || pieces[3].valid;
		if (changed || any != overOn) {
			overOn = any;
			overDirty = true;
			if (mapShownIdx >= 0) {
				upload_map_frame(mapShownIdx);
			} else if (h->logoUnder && combatPx && (combatTex[0] || combatTex[1])) {
				/* no map picture yet (the other armies' turn, after loading
				 * a state): a piece that goes alone (a conversation) on the
				 * logo picture itself */
				const uint32_t* px = hud_over_map(combatPx);
				if (!overAlone) px = combatPx;
				for (int i = 0; i < nViews; ++i) {
					if (combatTex[i]) ui_update_texture(combatTex[i], px, mapW * 4);
				}
			}
		}
	} else if (hudOn && race) {
		if (emu_hud_grabbed()) hud_make_backdrop(); /* (that frame drew no HUD: the last one is reused) */
		hud_compose();
		for (int i = 0; i < nViews; ++i) ui_update_texture(hudTex[i], hudCanvas, HUD_CW * 4);
		if (!hudShown && debug) fprintf(stderr, "frame %llu: HUD on the second screen\n", (unsigned long long) frameCount);
		hudShown = true;
	}
	bool scene = race && h->scene.when[0].addr && memprobes_match(h->scene.when, 2, &emu);
	if (race != hudOn || scene != hudScene || world != hudWorld || alt != hudAlt) {
		const SpriteRule* objs = scene ? h->scene.objs : world ? hudWorldObjs : hudObjs;
		emu_hud_split(&emu, race, alt ? h->alt.bgMask : h->bgMask, objs, rule_count(objs, scene ? 4 : world ? 4 : 8), hudPx);
		if (debug && race && alt != hudAlt) fprintf(stderr, "frame %llu: HUD on its other layer%s\n", (unsigned long long) frameCount, alt ? "" : ": off");
		hudAlt = alt;
		/* (by priority: the renderer picks the layers line by line) */
		emu_hud_by_priority(h->bgPrioBelow);
		hudKeep = 0;
		if (debug && race && scene != hudScene) fprintf(stderr, "frame %llu: HUD scene %s\n", (unsigned long long) frameCount, scene ? "on" : "off");
		hudOn = race;
		hudScene = scene;
		hudWorld = world;
		hudFrames = 0;
		hudRegrab = 0;
		mapFound = false;
	}
	/* a spell or other animation: its effects are on the text box layers,
	 * in other palettes than the text boxes; those layers stay (from the
	 * next frame) */
	unsigned keep = 0;
	if (hudOn && h->effects[0].addr && memprobes_match(h->effects, 2, &emu)) keep = effect_bgs(h->effectsPal);
	if (keep != hudKeep) {
		emu_hud_keep_bgs(keep);
		if (debug) fprintf(stderr, "frame %llu: effect layers %X\n", (unsigned long long) frameCount, keep);
		hudKeep = keep;
	}
	/* the course's backdrop, as each race starts (not for the scene) */
	if (hudOn && ++hudFrames == 2 && h->backdrop && !hudScene) emu_hud_grab(&emu, h->backdrop, hudGrab);
	/* the backdrop, the game screen (map, units, no HUD): once per map (its tiles' checksum),
	 * when the HUD comes on, not again as it comes and goes in a battle */
	if (hudOn && h->mapCnt && mapBg >= 0 && (hudFrames == 2 || hudFrames == hudRegrab)) {
		uint32_t sum = map_tiles_sum(mapBg);
		if (hudFrames == hudRegrab || !hudBackValid || sum != hudMapSum) {
			hudMapSum = sum;
			/* the game screen as it is now: the scene, without the HUD (nor
			 * the sprites the profile leaves out of it, rendered again) */
			int nh = rule_count(h->backdropHide, 2);
			if (world) {
				/* the world map: its layers below the HUD's (the land and the
				 * map, no sprites), drawn next frame */
				unsigned m = 0;
				for (int i = 0; i < 4; ++i) {
					if ((emu_read16(&emu, 0x04000008 + i * 2) & 3) >= h->bgPrioBelow) m |= 1u << i;
				}
				emu_hud_grab(&emu, m, hudGrab);
			} else {
				if (nh) snapshot_hiding_sprites(h->backdropHide, nh, hudObjs, rule_count(hudObjs, 8), hudGrab);
				else memcpy(hudGrab, emu.fb, sizeof(hudGrab));
				hud_make_backdrop();
			}
		}
	}
	/* a scene change: the game fades to black (or white) and back (to
	 * another map, or the same one elsewhere); once the picture's back,
	 * take the scene again (retried until its brightness settles) */
	static int darkRun;
	if (h->mapCnt) {
		bool dark = frame_is_flat(emu.fb);
		if (dark) {
			++darkRun;
		} else {
			if (darkRun >= 6 && hudBackValid) {
				if (debug) fprintf(stderr, "frame %llu: faded in: the backdrop again\n", (unsigned long long) frameCount);
				if (hudOn) hudRegrab = hudFrames + 10;
				hudSettle = true;
				hudMapSum = 0; /* (and when the HUD comes back on) */
			}
			darkRun = 0;
		}
	}
	/* menus: the logo (or the map), once it's been gone for a third of a
	 * second (not for a moment between rooms); no keep probes: never kept */
	static int goneFor;
	/* (not while the game screen is blank between scenes: the second
	 * screen stays as dark, or as white, as the fade left it) */
	bool gone = !race && hudShown && !(h->keep[0].addr && memprobes_match(h->keep, 4, &emu)) &&
	            !(h->mapCnt && frame_is_flat(emu.fb));
	goneFor = gone ? goneFor + 1 : 0;
	if (goneFor >= 20) {
		hudShown = false;
		goneFor = 0;
		if (debug) fprintf(stderr, "frame %llu: HUD gone\n", (unsigned long long) frameCount);
	}
}

static void update_snapshot(void) {
	const typeof(snapProf->snap)* sn = &snapProf->snap;
	int tol = sn->tol ? sn->tol : 12;
	if (probes_hit(emu.fb, sn->show, tol) >= sn->showMin) {
		snapRevert = 0;
		if (++snapShow >= SDL_max(1, sn->stable)) {
			const uint32_t* px = emu.fb;
			if (sn->hide[0].x1) {
				/* only when the picture changes (the sheets are still) */
				static uint32_t last[GBA_W * GBA_H], clean[GBA_W * GBA_H];
				if (mapShownIdx < 0 || memcmp(last, emu.fb, sizeof(last))) {
					memcpy(last, emu.fb, sizeof(last));
					int n = 0;
					while (n < 4 && sn->hide[n].x1) ++n;
					snapshot_hiding_sprites(sn->hide, n, NULL, 0, clean);
					for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], clean, GBA_W * 4);
				}
				px = NULL;
			}
			if (px) for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, GBA_W * 4);
			if (mapShownIdx < 0 && debug) fprintf(stderr, "frame %llu: screen copied to the second screen\n", (unsigned long long) frameCount);
			mapShownIdx = 0;
		}
		return;
	}
	snapShow = 0;
	if (mapShownIdx < 0) return;
	bool menu = false;
	for (int k = 0; k < 2; ++k) {
		if (sn->revertMin[k] && probes_hit(emu.fb, sn->revert[k], tol) >= sn->revertMin[k]) menu = true;
	}
	if (!menu) {
		snapRevert = 0;
	} else if (++snapRevert >= SDL_max(1, sn->stable)) {
		mapShownIdx = -1;
		snapRevert = 0;
		if (debug) fprintf(stderr, "frame %llu: back at the menus, logo\n", (unsigned long long) frameCount);
	}
}

/* ---------------------------------------------------------- control scheme
 * Games with a four-face-button layout (profile `controls`): a picker on the
 * second screen (touch only) chooses the GBA layout, the four-face one, or
 * Auto (four-face while the player controls the character). Saved per game
 * in <state_dir>/<game>.controls. */
static int game_screen(void);
enum { CTL_GBA, CTL_FOUR, CTL_AUTO };
static const char* const ctlNames[] = { "gba", "four", "auto" };
static bool snesHere; /* SNES layout: the game is where it applies (controls.snes.when: in a level) */
static int ctlMode;
static bool ctlPlaying; /* Auto: the player is controlling the character */
static char ctlPath[1400];

/* The game's own HUD is up (display mode and a sprite at each position). */
static bool ctl_playing_now(void) {
	const typeof(ctlProf->controls)* c = &ctlProf->controls;
	if (c->playing[0].addr) return memprobes_match(c->playing, 2, &emu);
	if ((emu_read16(&emu, 0x04000000) & 7) != c->mode) return false;
	int want = 0, found = 0;
	while (want < 4 && (c->hud[want].x || c->hud[want].y)) ++want;
	for (int i = 0; i < 128 && found != (1 << want) - 1; ++i) {
		uint16_t a0 = emu_read16(&emu, 0x07000000 + i * 8);
		if ((a0 & 0x0300) == 0x0200) continue; /* disabled */
		int y = a0 & 0xFF, x = emu_read16(&emu, 0x07000002 + i * 8) & 0x1FF;
		for (int k = 0; k < want; ++k) {
			if (x == c->hud[k].x && y == c->hud[k].y) found |= 1 << k;
		}
	}
	return want && found == (1 << want) - 1;
}

/* tiles in the picker: Auto only where the profile can tell when the player
 * is skating (its display mode and HUD sprites) */
static int ctl_tiles(void) {
	return ctlProf && (ctlProf->controls.hud[0].x || ctlProf->controls.hud[0].y || ctlProf->controls.playing[0].addr) ? 3 : 2;
}

static bool ctl_four(void) {
	return ctlProf && (ctlMode == CTL_FOUR || (ctlMode == CTL_AUTO && ctlPlaying));
}

/* the alternate layout is the SNES one (profile controls.snes), not four-face */
static bool ctl_snes(void) {
	return ctlProf && ctlProf->controls.snes.bottom;
}

static const char* ctl_name(int i) {
	return i == CTL_FOUR && ctl_snes() ? "snes" : ctlNames[i];
}

static void ctl_load(void) {
	ctlMode = CTL_GBA;
	ctlPlaying = false;
	snprintf(ctlPath, sizeof(ctlPath), "%s/%s.controls", cfg->stateDir, gp.base);
	FILE* f = fopen(ctlPath, "r");
	if (!f) return;
	char w[16] = "";
	if (fscanf(f, "%15s", w) == 1) {
		for (int i = 0; i < 3; ++i) {
			if (!strcmp(w, ctl_name(i))) ctlMode = i;
		}
	}
	fclose(f);
	if (ctlMode >= ctl_tiles()) ctlMode = CTL_GBA;
}

static void ctl_save(void) {
	FILE* f = fopen(ctlPath, "w");
	if (!f) return;
	fprintf(f, "%s\n", ctl_name(ctlMode));
	fclose(f);
}

/* the picker is on the second screen, when that's the touchscreen */
static bool ctl_shown(int s) {
	return ctlProf && cfg->map && nScreens == 2 && s == ui_touch_screen() && s != game_screen();
}

typedef struct {
	SDL_Rect tile[3];
	SDL_Rect all; /* label and tiles: taps here never open the menu */
	int labelX, labelY, cap;
} CtlGeom;

/* A row along the bottom: "Controls" and three square tiles, in the band
 * below the picture when there's room for it, else over the picture. */
static CtlGeom ctl_geom(int s) {
	CtlGeom g;
	SDL_Rect r = screens[s].rect;
	float u = ui_px(s);
	int t = r.h * 23 / 200, gap = u * 3;
	g.cap = t / 5;
	int lw = ui_label_width(s, g.cap, "Controls");
	int n = ctl_tiles();
	int total = lw + gap * 3 + t * n + gap * (n - 1);
	int x = (r.w - total) / 2;
	SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
	int below = r.h - (img.y + img.h);
	int y = below >= t + u * 2 ? img.y + img.h + (below - t) / 2 : r.h - t - u * 2;
	g.all = (SDL_Rect) { x - gap, y - u, total + gap * 2, t + u * 2 };
	g.labelX = x + lw / 2;
	g.labelY = y + t / 2;
	x += lw + gap * 3;
	for (int i = 0; i < 3; ++i) g.tile[i] = (SDL_Rect) { x + i * (t + gap), y, i < n ? t : 0, i < n ? t : 0 };
	return g;
}

static void ctl_button(int s, int id, const SDL_Rect* t, int cx, int cy, int w) {
	static SDL_Texture* tex[BRAND_COUNT][2];
	static int tw[BRAND_COUNT], th[BRAND_COUNT];
	if (!tex[id][0] && !tex[id][1]) ui_load_brand(id, tex[id], &tw[id], &th[id]);
	if (!tw[id]) return;
	int h = w * th[id] / tw[id];
	SDL_Rect a = { t->x + t->w * cx / 100 - w / 2, t->y + t->h * cy / 100 - h / 2, w, h };
	ui_image(s, tex[id], tw[id], th[id], "fit", &a);
}

static void ctl_draw(int s) {
	CtlGeom g = ctl_geom(s);
	float u = ui_px(s);
	int active = ctl_four() ? CTL_FOUR : CTL_GBA;
	ui_fill_round(s, g.all, (SDL_Color) { 0, 0, 0, 150 }, u * 4);
	ui_label(s, g.labelX, g.labelY, g.cap, "Controls", C_TEXT);
	for (int i = 0; i < ctl_tiles(); ++i) {
		SDL_Rect t = g.tile[i];
		int rad = t.w / 6;
		bool sel = ctlMode == i;
		/* chosen: filled; in Auto, the layout in use right now: outlined */
		if (sel || (ctlMode == CTL_AUTO && i == active)) {
			ui_fill_round(s, t, C_BTN_EDGE, rad);
			int e = sel ? u : SDL_max(1, u / 2);
			t = (SDL_Rect) { t.x + e, t.y + e, t.w - e * 2, t.h - e * 2 };
		}
		ui_fill_round(s, t, sel ? C_BTN_SEL : C_BTN, rad);
		t = g.tile[i];
		int d = t.w * 30 / 100;
		if (ctl_snes()) {
			ui_label(s, t.x + t.w / 2, t.y + t.h / 2, g.cap, i == CTL_GBA ? "GBA" : "SNES", C_TEXT);
		} else if (i == CTL_GBA) {
			ctl_button(s, BRAND_PILL_L, &t, 32, 27, t.w * 34 / 100);
			ctl_button(s, BRAND_PILL_R, &t, 68, 27, t.w * 34 / 100);
			d = t.w * 28 / 100;
			ctl_button(s, BRAND_BTN_B, &t, 34, 69, d);
			ctl_button(s, BRAND_BTN_A, &t, 66, 60, d);
		} else if (i == CTL_FOUR) {
			d = t.w * 28 / 100;
			ctl_button(s, BRAND_BTN_A, &t, 50, 27, d);
			ctl_button(s, BRAND_BTN_L, &t, 27, 50, d);
			ctl_button(s, BRAND_BTN_R, &t, 73, 50, d);
			ctl_button(s, BRAND_BTN_B, &t, 50, 73, d);
		} else {
			ui_label(s, t.x + t.w / 2, t.y + t.h / 2, g.cap, "Auto", C_TEXT);
		}
	}
}

/* A tap on the picker: true if it was there (it then doesn't open the menu). */
static bool ctl_tap(int s, int x, int y) {
	if (!ctl_shown(s)) return false;
	CtlGeom g = ctl_geom(s);
	SDL_Point pt = { x, y };
	if (!SDL_PointInRect(&pt, &g.all)) return false;
	for (int i = 0; i < ctl_tiles(); ++i) {
		if (SDL_PointInRect(&pt, &g.tile[i]) && ctlMode != i) {
			ctlMode = i;
			ctl_save();
			if (debug) fprintf(stderr, "frame %llu: controls %s\n", (unsigned long long) frameCount, ctl_name(i));
		}
	}
	return true;
}

static void save_thumbs(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(emu.fb, GBA_W, GBA_H, 32, GBA_W * 4, SDL_PIXELFORMAT_XBGR8888);
	if (s) {
		SDL_SaveBMP(s, gp.thumb);
		SDL_FreeSurface(s);
	}
	const uint32_t* still = stillValid ? stillPx : mapRes.frames[0];
	if (mapShownIdx >= 0 && still) {
		s = SDL_CreateRGBSurfaceWithFormatFrom((void*) still, mapW, mapH, 32, mapW * 4, SDL_PIXELFORMAT_XBGR8888);
		if (s) {
			SDL_SaveBMP(s, gp.mapThumb);
			SDL_FreeSurface(s);
		}
	}
}

/* Save states, with the RetroAchievements progress beside them. */
static bool state_save(const char* path) {
	bool ok = emu_state_save_file(&emu, path);
	if (ok) ra_state_saved(path);
	return ok;
}

static bool state_load(const char* path) {
	bool ok = emu_state_load_file(&emu, path);
	if (ok) ra_state_loaded(path);
	/* HUD pieces from another moment: taken again from the loaded game */
	for (int m = 0; ok && m < 4; ++m) pieces[m].stale = true, pieces[m].still = 0;
	if (ok) aheadReady = false; /* a frame emulated before the load: emulate afresh */
	if (ok) mirrorHave = false; /* (hud.mirror: the picture from before is another moment's) */
	return ok;
}

#ifdef __wasi__
/* State Saver's GBA DS tab: states saved and loaded from the page, between frames */
static bool webPlaying;
__attribute__((export_name("web_save_state"))) int web_save_state(const char* path) {
	return webPlaying && state_save(path);
}
__attribute__((export_name("web_load_state"))) int web_load_state(const char* path) {
	return webPlaying && state_load(path);
}
#endif

static void auto_save(void) {
	if (!cfg->autoResume) return;
	if (state_save(gp.autoState)) {
		save_thumbs();
		fprintf(stderr, "auto-saved %s\n", gp.autoState);
	} else {
		fprintf(stderr, "auto-save failed\n");
	}
}

/* The app is being put in the background (Android may kill it there):
 * keep a resume point. Called between frames, from ui_events. */
static void on_background(void) {
	auto_save();
}

static void open_menu(void);

/* Back from Home / the app switcher: paused, rather than straight into the action. */
static void on_foreground(void) {
	if (!menuOpen) open_menu();
}

/* generated surround (built-in overlay), see mapframe.c */
static MapFrame mf;
static SDL_Texture* canvasTex[2];
static int canvasTexW, canvasTexH;
static bool canvasDirty;
static char framePath[1200];


static const uint32_t* map_px(int idx) {
	return idx == STILL_IDX ? (stillValid ? stillPx : NULL) : mapRes.frames[idx];
}

/* hud.alt.shade: a pixel darkened (to a third) */
static uint32_t shade_px(uint32_t c) {
	return (c & 0xFF000000) | (((c >> 2) & 0x3F3F3F) + ((c >> 3) & 0x1F1F1F));
}

/* Profile hud.overMap: the map picture with the HUD's pieces in it (where
 * the picture has the profile's mapClear colour): each piece as last seen
 * (hud_take_pieces). */
static const uint32_t* hud_over_map(const uint32_t* px) {
	static uint32_t* over;
	static int overSize;
	if (!hudProf || (!overOn && !hudProf->hud.logoUnder)) return px;
	const typeof(hudProf->hud)* h = &hudProf->hud;
	if (h->logoUnder && combatProf && !combatMade) make_combat_canvas();
	if (overSize != mapW * mapH) {
		free(over);
		over = malloc((size_t) mapW * mapH * 4);
		overSize = over ? mapW * mapH : 0;
		if (!over) return px;
	}
	uint32_t mc = h->mapClear & 0xFFFFFF;
	/* a piece that goes alone (a conversation): on the plain background,
	 * the others waiting */
	int alone = -1;
	for (int m = 0; m < 4; ++m) {
		if (h->moves[m].alone && pieces[m].valid) alone = m;
	}
	overAlone = alone >= 0;
	memcpy(over, alone >= 0 && h->logoUnder && combatPx ? combatPx : px, (size_t) mapW * mapH * 4);
	int px0[4] = { 0 }, py0[4] = { 0 };
	for (int k = 0; k < 8; ++k) {
		int m = k & 3;
		const HudMove* mv = &h->moves[m];
		const Piece* pc = &pieces[m];
		/* the pieces, then the ones that follow them (over everything) */
		if (!pc->valid || (k < 4) == (mv->follow > 0 || mv->alone)) continue;
		if (alone >= 0 && m != alone) continue;
		int x0 = mv->how == 'C' ? mv->x - (pc->w - 1) / 2 : mv->how == 'R' ? mv->x - pc->w + 1 : mv->x;
		int y0 = mv->vhow == 'M' ? mv->y - (pc->h - 1) / 2 : mv->vhow == 'B' ? mv->y - pc->h + 1 : mv->y;
		if (mv->asIs) {
			x0 = pc->sx + (mapW - GBA_W) / 2, y0 = pc->sy + (mapH - GBA_H) / 2;
			if (mv->asIsBottom && pc->atBottom && !hudAlt) y0 = mapH - pc->h;
		}
		if (mv->wide) {
			/* as wide as the picture: each pixel from the piece's, scaled */
			int sw = pc->w * mapW / GBA_W, sh = pc->h * mapW / GBA_W;
			x0 = pc->sx * mapW / GBA_W;
			for (int y = 0; y < sh; ++y) {
				for (int x = 0; x < sw; ++x) {
					uint32_t c = pc->px[(y * GBA_W / mapW) * pc->w + x * GBA_W / mapW];
					int nx = x0 + x, ny = y0 + y;
					if (!c || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
					over[ny * mapW + nx] = c;
				}
			}
			px0[m] = x0, py0[m] = y0;
			continue;
		}
		if (mv->follow) {
			int a = mv->follow - 1;
			if (!pieces[a].valid) continue;
			x0 = pc->sx + px0[a] - pieces[a].sx;
			y0 = pc->sy + py0[a] - pieces[a].sy;
		}
		px0[m] = x0, py0[m] = y0;
		uint32_t bc = mv->box ? (mv->box & 0xFF) << 16 | (mv->box & 0xFF00) | (mv->box >> 16 & 0xFF) : 0x1000000;
		if (mv->box) {
			/* the box goes round the piece */
			for (int y = y0 - mv->pad; y < y0 + pc->h + mv->pad; ++y) {
				for (int x = x0 - mv->pad; x < x0 + pc->w + mv->pad; ++x) {
					if (x < 0 || y < 0 || x >= mapW || y >= mapH) continue;
					uint32_t* o = &over[y * mapW + x];
					if ((*o & 0xFFFFFF) == mc) *o = 0xFF000000u | bc;
				}
			}
		}
		bool shade = mv->alone && hudAlt && h->alt.shade;
		if (shade) {
			static uint32_t* orig;
			static int origSize;
			if (origSize != mapW * mapH) free(orig), orig = malloc((size_t) mapW * mapH * 4), origSize = orig ? mapW * mapH : 0;
			if (orig) {
				/* the black (the game screen's, stretched over the whole
				 * picture) darkens; the piece's other pixels (the lettering)
				 * go where it is */
				memcpy(orig, over, (size_t) mapW * mapH * 4);
				for (int ny = 0; ny < mapH; ++ny) {
					int ly = ny * GBA_H / mapH - pc->sy;
					for (int nx = 0; nx < mapW; ++nx) {
						int lx = nx * GBA_W / mapW - pc->sx;
						uint32_t c = lx >= 0 && ly >= 0 && lx < pc->w && ly < pc->h ? pc->px[ly * pc->w + lx] : 0;
						if (c && (c & 0xFFFFFF) == 0) over[ny * mapW + nx] = shade_px(orig[ny * mapW + nx]);
					}
				}
				for (int y = 0; y < pc->h; ++y) {
					for (int x = 0; x < pc->w; ++x) {
						uint32_t c = pc->px[y * pc->w + x];
						int nx = x0 + x, ny = y0 + y;
						if (!c || (c & 0xFFFFFF) == 0 || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
						over[ny * mapW + nx] = c;
					}
				}
				continue;
			}
		}
		for (int y = 0; y < pc->h; ++y) {
			for (int x = 0; x < pc->w; ++x) {
				uint32_t c = pc->px[y * pc->w + x];
				int nx = x0 + x, ny = y0 + y;
				if (!c || nx < 0 || ny < 0 || nx >= mapW || ny >= mapH) continue;
				uint32_t* o = &over[ny * mapW + nx];
				if (mv->follow || mv->alone || mv->top || (*o & 0xFFFFFF) == mc || (*o & 0xFFFFFF) == bc) *o = c;
			}
		}
	}
	if (h->logoUnder && combatPx) {
		/* the logo behind: where nothing else is: last */
		for (int i = 0; i < mapW * mapH; ++i) {
			if ((over[i] & 0xFFFFFF) == mc) over[i] = combatPx[i];
		}
	}
	return over;
}

static void upload_map_frame(int idx) {
	const uint32_t* px = map_px(idx);
	if ((idx == mapShownIdx && !overDirty) || !px) return;
	overDirty = false;
	px = hud_over_map(px);
	for (int i = 0; i < nViews; ++i) ui_update_texture(mapTex[i], px, mapW * 4);
	mapShownIdx = idx;
	canvasDirty = true;
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
			x0 = SDL_min(x0, px), x1 = SDL_max(x1, px), y0 = SDL_min(y0, py), y1 = SDL_max(y1, py);
			for (int dy = -R; dy <= R; ++dy) {
				for (int dx = -R; dx <= R; ++dx) {
					int nx = px + dx, ny = py + dy;
					if (nx < 0 || ny < 0 || nx >= w || ny >= h) continue;
					int q = ny * w + nx;
					if (mask[q] && !label[q]) label[q] = id, queue[len++] = q;
				}
			}
		}
		x0 = SDL_max(x0 - 2, 0), y0 = SDL_max(y0 - 2, 0), x1 = SDL_min(x1 + 2, w - 1), y1 = SDL_min(y1 + 2, h - 1);
		/* the surroundings' colour: most common still pixel around the spot */
		uint32_t bg = 0;
		if (pick == STILL_COVER) {
			int ns = 0;
			for (int y = y0; y <= y1; ++y) {
				for (int x = x0; x <= x1 && ns < (int) SDL_arraysize(sample); ++x) {
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
				else if (red) a += SDL_max(0, (int) (p & 0xFF) - (int) (((p >> 8) & 0xFF) + ((p >> 16) & 0xFF)) / 2);
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

/* After a fresh capture: rebuild the still map. A capture that only differs
 * from the one on screen where the markers animate (a pulse caught at
 * another point of its loop) keeps the old still, so it doesn't jump around
 * every time the map is refreshed. Returns true if the still changed. */
static bool stillStale; /* stillPx is from an older map (built only when it's shown) */
/* The map animates (Map Animations: On), unless the game's is always still */
static bool map_anim(void) {
	const MapProfile* p = mc ? mapcap_profile(mc) : NULL;
	return cfg->mapAnim && !(p && p->mapStill);
}

static bool update_still(void) {
	int w = mapRes.w, h = mapRes.h, wh = w * h;
	uint32_t* px = malloc((size_t) wh * 4);
	unsigned char* mask = malloc((size_t) wh);
	if (!px || !mask) {
		free(px);
		free(mask);
		return false;
	}
	const MapProfile* prof = mc ? mapcap_profile(mc) : NULL;
	compose_still(&mapRes, prof ? (int) prof->stillPick : STILL_BRIGHT, px, mask);
	if (stillValid && stillW == w && stillH == h) {
		bool same = true;
		for (int i = 0; i < wh && same; ++i) same = px[i] == stillPx[i] || mask[i];
		if (same) {
			free(px);
			free(mask);
			return false;
		}
	}
	free(stillPx);
	free(mask);
	stillPx = px, stillW = w, stillH = h, stillValid = true;
	return true;
}

/* Draws the map with its generated surround; false if there's none to draw. */
static bool draw_framed_map(int s) {
	if (!(cfg->overlays & OVL_SECONDARY) || !mapframe_ready(&mf) || mapShownIdx < 0) return false;
	if (!strcmp(cfg->mapScale, "zoom")) return false; /* the map fills the height: no room for a surround */
	SDL_Rect r = screens[s].rect;
	if (mapframe_layout(&mf, r.w, r.h, cfg->mapScale) || mf.baseDirty) canvasDirty = true;
	if (canvasTexW != mf.cw || canvasTexH != mf.ch) {
		for (int i = 0; i < nViews; ++i) {
			if (canvasTex[i]) SDL_DestroyTexture(canvasTex[i]);
			canvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mf.cw, mf.ch);
			SDL_SetTextureBlendMode(canvasTex[i], SDL_BLENDMODE_NONE);
		}
		canvasTexW = mf.cw;
		canvasTexH = mf.ch;
		canvasDirty = true;
	}
	if (canvasDirty) {
		const uint32_t* px = mapframe_compose(&mf, map_px(mapShownIdx));
		for (int i = 0; i < nViews; ++i) ui_update_texture(canvasTex[i], px, mf.cw * 4);
		canvasDirty = false;
	}
	ui_image_pixels(s, canvasTex, mf.cw, mf.ch, "stretch", cfg->lcdGrid);
	return true;
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

static int game_screen(void) {
	if (nScreens < 2) return 0;
	return cfg->swap ? 1 : 0;
}

/* Frame pacing. The GBA runs at 59.73 frames a second; a 60 Hz screen then
 * shows one frame twice about every 3.7 s (a small, regular stutter).
 * "Match Display Rate" runs the game at the display's rate instead when it's
 * within 1.5% (60 Hz: 0.46% faster, sound resampled to match, a pitch change
 * nobody hears), and presents the game's screen in step with the display
 * (vsync), so each frame gets exactly one refresh. */
static double frameSec = 1.0 / GBA_FPS;
static double pacedHz;    /* the display's rate when matched, else 0 */
static double audioSpeed = 1.0; /* emulated time per real time */

static void pacing_update(void) {
	double hz = nScreens > 0 ? ui_display_hz(screens[game_screen()].view) : 0;
	pacedHz = 0;
	if (cfg->matchDisplay && hz > 0 && fabs(hz / GBA_FPS - 1) <= 0.015) pacedHz = hz;
	frameSec = 1.0 / (pacedHz > 0 ? pacedHz : GBA_FPS);
	audioSpeed = pacedHz > 0 ? pacedHz / GBA_FPS : 1.0;
	fprintf(stderr, "frame pacing: display %.3f Hz, game %.3f frames a second (%s)\n", hz, 1.0 / frameSec,
	        pacedHz > 0 ? "matched to the display" : cfg->matchDisplay ? "display too far off to match" : "the GBA's rate");
}

/* Rewind (hold L2): mGBA's rewind buffer, a state every second frame,
 * rewind_seconds long (default 30 s: 900 states, each kept as its
 * difference from the next, a few MB). */
static int rewindTick;
static bool wasRewinding;

static void rewind_setup(void) {
	emu_rewind_deinit();
	rewindTick = 0;
	if (!cfg->rewind || !emu.core) return;
	int secs = cfg->rewindSeconds < 5 ? 5 : cfg->rewindSeconds > 300 ? 300 : cfg->rewindSeconds;
	emu_rewind_init(&emu, (size_t) secs * 30);
	fprintf(stderr, "rewind: %d s (%d states)\n", secs, secs * 30);
}

/* Display choices made in the menu are remembered for next time. */
static void save_prefs(void) {
	char prefs[1100];
	snprintf(prefs, sizeof(prefs), "%s/prefs.cfg", cfg->stateDir);
	FILE* f = fopen(prefs, "w");
	if (f) {
		fprintf(f, "# written by gba-ds's pause menu; overrides gba-ds.cfg\n");
		fprintf(f, "swap = %d\nscale = %s\nmap_scale = %s\nlcd_grid = %d\nshow_fps = %d\noverlays = %s\nframe_buffer = %d\nshow_logo = %d\nmatch_display = %d\nrewind = %d\n",
		        cfg->swap ? 1 : 0, cfg->scale, cfg->mapScale, cfg->lcdGrid ? 1 : 0, cfg->showFps ? 1 : 0,
		        (const char* const[]) { "none", "primary", "secondary", "both" }[cfg->overlays & OVL_BOTH],
		        cfg->frameBuffer ? 1 : 0, cfg->showLogo ? 1 : 0, cfg->matchDisplay ? 1 : 0, cfg->rewind ? 1 : 0);
		fclose(f);
	}
}

static bool gameTexAll = true; /* upload the game's frame to every view next time */

static void swap_screens(void) {
	/* the game moves to the other view, whose copy of the frame may be old
	 * (frames go only where the game is shown; the menu uploads none) */
	if (emu.fb) {
		for (int i = 0; i < nViews; ++i) ui_update_texture(gameTex[i], emu.fb, GBA_W * 4);
	}
	if (nScreens < 2) {
		singleShowsMap = !singleShowsMap;
		return;
	}
	cfg->swap = !cfg->swap;
	save_prefs();
}

static SDL_Color borderColor;

/* User overlay PNGs in <asset_dir>/overlays/, drawn over a whole screen
 * (transparent where the picture should show). First found wins:
 *   primary (game screen):  <CODE>-primary-<scale>, <CODE>-primary,
 *                           primary-<scale>, primary
 *   secondary (map screen): <CODE>-secondary-<scale>, <CODE>-secondary,
 *                           <CODE>-<scale>, <CODE>, secondary-<scale>, secondary
 * <scale> is that screen's scaling (integer / fit / stretch). */
static void load_overlay(int which) {
	const char* scale = which ? cfg->mapScale : cfg->scale;
	if (!strcmp(overlayMode[which], scale)) return;
	snprintf(overlayMode[which], sizeof(overlayMode[which]), "%s", scale);
	ui_free_image(overlayTex[which]);
	const char* side = which ? "secondary" : "primary";
	char names[6][64];
	int n = 0;
	snprintf(names[n++], 64, "%.4s-%s-%s", emu.gameCode, side, scale);
	snprintf(names[n++], 64, "%.4s-%s", emu.gameCode, side);
	if (which) {
		snprintf(names[n++], 64, "%.4s-%s", emu.gameCode, scale); /* older names */
		snprintf(names[n++], 64, "%.4s", emu.gameCode);
	}
	snprintf(names[n++], 64, "%s-%s", side, scale);
	snprintf(names[n++], 64, "%s", side);
	char path[1200];
	for (int i = 0; i < n; ++i) {
		snprintf(path, sizeof(path), "%s/overlays/%s.png", cfg->assetDir, names[i]);
		if (ui_load_image(path, overlayTex[which], &overlayW[which], &overlayH[which], true)) {
			if (debug) fprintf(stderr, "overlay (%s): %s\n", side, path);
			return;
		}
	}
}

static void draw_overlay(int s, int which) {
	load_overlay(which);
	if (overlayTex[which][0] || overlayTex[which][1]) {
		ui_image(s, overlayTex[which], overlayW[which], overlayH[which], "stretch", NULL);
	}
}


/* Text size for the FPS counter and messages: fits the empty band (gap px tall)
 * above or below the picture, even in full-width scaling. */
static int small_text_px(int s, int gap) {
	return SDL_max(1, SDL_min(SDL_min(ui_px(s) - 1, 3), (gap - 6) / 7)); /* 7 rows tall, 3px margin each side */
}

/* Messages ("RESUMED", "STATE SAVED"...): bottom left of the touchscreen (the
 * bottom screen), in the band below the picture, at the FPS counter's size. */
static bool shotPending; /* Take Screenshot: capture the next frame drawn */

static void draw_osd(void) {
	const char* osd = ui_osd_text();
	if (!osd || menuOpen || shotPending) return;
	int s = ui_touch_screen();
	SDL_Rect r = screens[s].rect;
	bool gameHere = nScreens == 1 || s == game_screen() || !((mc || logoOnly) && cfg->map);
	SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, gameHere ? cfg->scale : cfg->mapScale);
	int gap = r.h - (img.y + img.h);
	int fpx = small_text_px(s, img.y < gap ? img.y : gap);
	int fy = gap >= 7 * fpx + 2 ? img.y + img.h + (gap - 7 * fpx) / 2 : r.h - fpx * 9;
	int fx = img.x + fpx * 2, cy = fy + fpx * 7 / 2, cap = fpx * 6;
	ui_font_text(s, TF_BOLD, fx + SDL_max(1, fpx / 2), cy + SDL_max(1, fpx / 2), cap, osd, (SDL_Color) { 0, 0, 0, 255 }, -1);
	ui_font_text(s, TF_BOLD, fx, cy, cap, osd, C_TEXT, -1);
}

/* The game's logo as a GBA screen would show it: centred on black in a
 * 240x160 picture, drawn with the second screen's scaling and the LCD grid.
 * Logos extracted from the ROM are at GBA resolution; a user's own logo
 * that's bigger stays a high-resolution image (logoCanvasTex NULL). */
static void make_logo_canvas(void) {
	ui_free_image(logoCanvasTex);
	bool pixelArt = false;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (!lg) return;
	if (lg->w > GBA_W || lg->h > GBA_H) {
		SDL_FreeSurface(lg);
		return;
	}
	static uint32_t px[GBA_W * GBA_H];
	memset(px, 0, sizeof(px));
	int ox = (GBA_W - lg->w) / 2, oy = (GBA_H - lg->h) / 2;
	SDL_LockSurface(lg);
	for (int y = 0; y < lg->h; ++y) {
		const uint8_t* row = (const uint8_t*) lg->pixels + y * lg->pitch;
		for (int x = 0; x < lg->w; ++x) {
			const uint8_t* p = row + x * 4; /* RGBA32: bytes R, G, B, A */
			unsigned a = p[3];
			unsigned r = p[0] * a / 255, gc = p[1] * a / 255, b = p[2] * a / 255;
			px[(oy + y) * GBA_W + ox + x] = 0xFF000000u | b << 16 | gc << 8 | r; /* ABGR8888 */
		}
	}
	SDL_UnlockSurface(lg);
	SDL_FreeSurface(lg);
	for (int i = 0; i < nViews; ++i) {
		logoCanvasTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		if (!logoCanvasTex[i]) continue;
		SDL_SetTextureBlendMode(logoCanvasTex[i], SDL_BLENDMODE_NONE);
		ui_update_texture(logoCanvasTex[i], px, GBA_W * 4);
	}
}

/* profile.combat's picture: the map's background colour, the logo at half
 * its size (each output pixel the average of 2x2) in the middle, dimmed. */
static void make_combat_canvas(void) {
	combatMade = true;
	uint32_t* px = malloc((size_t) mapW * mapH * 4);
	if (!px) return;
	uint32_t bg = combatProf->hud.mapClear | 0xFF000000;
	for (int i = 0; i < mapW * mapH; ++i) px[i] = bg;
	bool pixelArt = false;
	SDL_Surface* lg = ui_logo_surface(cfg, emu.gameCode, &pixelArt);
	if (lg) {
		/* half of what fills the picture's width, whole steps of 2x2 for
		 * an extracted logo */
		float sc = SDL_min((float) mapW / lg->w, (float) mapH / lg->h) * 0.5f;
		if (pixelArt) sc = lg->w > mapW / 2 ? 0.5f : 1.0f;
		int w = SDL_max(1, (int) (lg->w * sc)), h = SDL_max(1, (int) (lg->h * sc));
		int x0 = (mapW - w) / 2, y0 = (mapH - h) / 2;
		SDL_LockSurface(lg);
		for (int y = 0; y < h; ++y) {
			for (int x = 0; x < w; ++x) {
				/* the source pixels under this one, averaged (alpha-weighted) */
				int sx0 = x * lg->w / w, sx1 = SDL_max(sx0 + 1, (x + 1) * lg->w / w);
				int sy0 = y * lg->h / h, sy1 = SDL_max(sy0 + 1, (y + 1) * lg->h / h);
				unsigned acc[4] = { 0 }, n = 0;
				for (int v = sy0; v < sy1; ++v) {
					for (int u = sx0; u < sx1; ++u) {
						const uint8_t* p = (const uint8_t*) lg->pixels + v * lg->pitch + u * 4; /* R, G, B, A */
						for (int k = 0; k < 3; ++k) acc[k] += p[k] * p[3];
						acc[3] += p[3];
						++n;
					}
				}
				if (!acc[3]) continue;
				float a = (float) acc[3] / (255.0f * n) * 0.35f; /* dimmed */
				uint32_t* o = &px[(y0 + y) * mapW + x0 + x];
				uint32_t out = 0xFF000000;
				for (int k = 0; k < 3; ++k) {
					float c = (float) acc[k] / acc[3], b = (bg >> (8 * k)) & 0xFF;
					out |= (uint32_t) (b + (c - b) * a) << (8 * k);
				}
				*o = out;
			}
		}
		SDL_UnlockSurface(lg);
		SDL_FreeSurface(lg);
	}
	for (int i = 0; i < nViews; ++i) {
		if (combatTex[i]) ui_update_texture(combatTex[i], px, mapW * 4);
	}
	free(combatPx);
	combatPx = px; /* (hud.logoUnder) */
}

static void draw_game(void) {
	bool mapOn = (mc || logoOnly) && cfg->map; /* logo-only games: the logo stands in for the map */
	for (int s = 0; s < nScreens; ++s) {
		bool showGame = nScreens == 1 ? !(singleShowsMap && mapOn) : (s == game_screen() || !mapOn);
		SDL_Rect r = screens[s].rect;
		if (showGame) {
			if (nScreens == 2 && !mapOn && s != game_screen()) continue; /* no map: leave other screen black */
			ui_image_pixels(s, gameTex, GBA_W, GBA_H, cfg->scale, cfg->lcdGrid);
			if (hudProf && hudOn && hudAlt && hudProf->hud.alt.dimGame) {
				/* (the phase banner moved to the other screen) */
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				ui_fill(s, img, (SDL_Color) { 0, 0, 0, 150 });
			}
			if (cfg->overlays & OVL_PRIMARY) draw_overlay(s, 0);
			if (cfg->showFps) {
				/* sized to sit in the empty band above the picture */
				char buf[48];
				snprintf(buf, sizeof(buf), "%.1f FPS  %d%%", fps, (int) (speedPct + 0.5));
				SDL_Rect img = ui_image_rect(s, GBA_W, GBA_H, cfg->scale);
				int gap = img.y;
				int fpx = small_text_px(s, gap);
				int fy = gap >= 7 * fpx + 2 ? (gap - 7 * fpx) / 2 : fpx * 2;
				int fx = img.x + fpx * 2;
				ui_text(s, fx + 1, fy + 1, fpx, buf, (SDL_Color) { 0, 0, 0, 255 });
				ui_text(s, fx, fy, fpx, buf, C_TEXT);
			}
			continue;
		}
		/* the map screen */
		/* (and with hud.logoUnder, on the map before there's a map picture) */
		bool logoBack = combatProf && combatProf->hud.logoUnder && mapShownIdx < 0 && (combatInGame || combatProf->hud.logoAlways);
		if (overAlone && mapShownIdx >= 0) logoBack = false; /* (the phase banner, on the other armies' turn too) */
		if (((combatOn && !(overAlone && mapShownIdx >= 0)) || logoBack) && (combatTex[0] || combatTex[1])) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, combatTex, mapW, mapH, cfg->mapScale, cfg->lcdGrid ? 1 : 0);
		} else if (hudShown) {
			/* the HUD picture is at 2x: a grid cell per GBA pixel */
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, hudTex, HUD_CW, HUD_CH, cfg->mapScale, cfg->lcdGrid ? 2 : 0);
		} else if (mapShownIdx >= 0 && draw_framed_map(s)) {
			/* map + generated surround */
		} else if (mapShownIdx >= 0) {
			ui_fill(s, (SDL_Rect) { 0, 0, r.w, r.h }, borderColor);
			ui_image_pixels(s, mapTex, mapW, mapH, cfg->mapScale, cfg->lcdGrid ? 1 : 0);
		} else if (!cfg->showLogo) {
			/* nothing to show: the screen stays blank */
		} else if (logoCanvasTex[0] || logoCanvasTex[1]) {
			/* no map yet (title screen, intro...): the game's logo, as if the
			 * game drew it on a second GBA screen */
			ui_image_pixels(s, logoCanvasTex, GBA_W, GBA_H, cfg->mapScale, cfg->lcdGrid ? 1 : 0);
		} else if (logoTex[0] || logoTex[1]) {
			/* a user's high-resolution logo: fitted to where the picture goes */
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			ui_image(s, logoTex, logoW, logoH, logoPixel ? "integer" : "fit", &area);
		} else if (logoOnly) {
			/* no logo to show (none extracted for this game yet): its name */
			const MapProfile* tp = profile_find(emu.gameCode);
			SDL_Rect area = ui_image_rect(s, GBA_W, GBA_H, cfg->mapScale);
			int in = area.w / 10;
			area = (SDL_Rect) { area.x + in, area.y + in, area.w - in * 2, area.h - in * 2 };
			if (tp) ui_title_block(s, area, tp->title, (int) (ui_px(s) * 14), (SDL_Color) { 236, 236, 244, 255 });
		}
		if ((hudShown || mapShownIdx >= 0) && (cfg->overlays & OVL_SECONDARY)) { /* the map only, never the logo */
			draw_overlay(s, 1);
		}
		if (!hudShown && mapShownIdx >= 0 && mapInactive) {
			ui_dim(s, 120);
		}
		if (ctl_shown(s) && !menuOpen) ctl_draw(s); /* the pause menu covers that screen */
	}
	draw_osd();
}

static void close_ach(void) {
	if (achOpen) ra_list_close();
	achOpen = false;
}

/* the list's area: below the Back / title boxes */
static SDL_Rect ach_area(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	return (SDL_Rect) { px * 4, px * 25, r.w - px * 8, r.h - px * 27 };
}

static void open_menu(void) {
	cheatsOpen = false;
	close_ach();
	set_page(0);
	menuOpen = true;
	menuSel = 0;
	menuPressed = -1;
	menuNote[0] = 0;
	restartArmed = false;
}

/* Returns true if the session should end (*exitTo set). */
static bool activate(int item, enum GameExit* exitTo) {
	if (item != M_RESTART) restartArmed = false;
	switch (item) {
	case M_RESUME:
		menuOpen = false;
		break;
	case M_SHOT:
		menuOpen = false; /* taken from the next frame, without the menu */
		shotPending = true;
		break;
	case M_ACH:
		ra_list_open();
		achOpen = true;
		achPressed = false;
		break;
	case M_CHEATS:
		if (!cheats.n) snprintf(menuNote, sizeof(menuNote), "No cheats found (see the README)");
		else open_cheats();
		break;
	case M_SWAP:
		swap_screens();
		snprintf(menuNote, sizeof(menuNote), "Screens swapped");
		break;
	case M_SCALE: {
		/* Pixel Perfect, Full Width, Zoom, Stretch */
		const char* next = !strcmp(cfg->scale, "integer") ? "fit" : !strcmp(cfg->scale, "fit") ? "zoom" :
		                   !strcmp(cfg->scale, "zoom")    ? "stretch" : "integer";
		snprintf(cfg->scale, sizeof(cfg->scale), "%s", next);
		snprintf(cfg->mapScale, sizeof(cfg->mapScale), "%s", next);
		save_prefs();
		break;
	}
	case M_LCD:
		cfg->lcdGrid = !cfg->lcdGrid;
		save_prefs();
		break;
	case M_SAVE:
		if (state_save(gp.state)) {
			ui_osd("State saved");
			menuOpen = false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "Save failed");
		}
		break;
	case M_LOAD:
		if (!item_enabled(M_LOAD)) {
			snprintf(menuNote, sizeof(menuNote), "No saved state yet");
		} else if (state_load(gp.state)) {
			ui_osd("State loaded");
			menuOpen = false;
			return false;
		} else {
			snprintf(menuNote, sizeof(menuNote), "Load failed");
		}
		break;
	case M_OVERLAY:
		/* PRIMARY -> SECONDARY -> BOTH -> NONE -> PRIMARY */
		cfg->overlays = cfg->overlays == OVL_PRIMARY ? OVL_SECONDARY : cfg->overlays == OVL_SECONDARY ? OVL_BOTH :
		                cfg->overlays == OVL_BOTH ? 0 : OVL_PRIMARY;
		save_prefs();
		break;
	case M_FPS:
		cfg->showFps = !cfg->showFps;
		save_prefs();
		break;
	case M_FRAMEBUF:
		cfg->frameBuffer = !cfg->frameBuffer;
		snprintf(menuNote, sizeof(menuNote), "%s", cfg->frameBuffer ? "Smoother, one frame more input delay" : "Least input delay");
		save_prefs();
		break;
	case M_REWIND:
		cfg->rewind = !cfg->rewind;
		rewind_setup();
		snprintf(menuNote, sizeof(menuNote), "%s", cfg->rewind ? "Hold L2 to go back in time" : "No rewind buffer: a little less work");
		save_prefs();
		break;
	case M_MATCH:
		cfg->matchDisplay = !cfg->matchDisplay;
		pacing_update();
		if (!cfg->matchDisplay) snprintf(menuNote, sizeof(menuNote), "The GBA's own %.2f frames a second", GBA_FPS);
		else if (pacedHz > 0) snprintf(menuNote, sizeof(menuNote), "%.2f frames a second, %.1f%% faster", pacedHz, (pacedHz / GBA_FPS - 1) * 100);
		else snprintf(menuNote, sizeof(menuNote), "The display's rate isn't close enough to match");
		save_prefs();
		break;
	case M_LOGO:
		cfg->showLogo = !cfg->showLogo;
		save_prefs();
		break;
	case M_RESTART:
		if (!restartArmed) {
			restartArmed = true;
			snprintf(menuNote, sizeof(menuNote), "Unsaved progress will be lost");
			break;
		}
		emu_reset(&emu);
		aheadReady = false;
		ra_reset();
		mapShownIdx = -1; /* back to the logo until the next map */
		restartArmed = false;
		menuOpen = false;
		ui_osd("Game restarted");
		break;
	case M_LIBRARY:
		*exitTo = GAME_TO_LIBRARY;
		return true;
	case M_EXIT:
		*exitTo = GAME_QUIT_APP;
		return true;
	}
	return false;
}

/* Saves what's on the screens now (before ui_present) as
 * <screenshot_dir>/<game>-YYYYMMDD-HHMMSS.png, screens stacked top to bottom. */
static void take_screenshot(void) {
	char dir[600], path[1400];
	snprintf(dir, sizeof(dir), "%s", cfg->screenshotDir);
	mkdirs(dir);
	struct stat st;
	if (stat(dir, &st) != 0 || !S_ISDIR(st.st_mode)) {
		snprintf(dir, sizeof(dir), "%s/screenshots", cfg->stateDir);
		mkdirs(dir);
	}
	time_t now = time(NULL);
	struct tm tmv;
	localtime_r(&now, &tmv);
	char stamp[32];
	strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &tmv);
	snprintf(path, sizeof(path), "%s/%s-%s.png", dir, gp.base, stamp);
	for (int n = 2; file_exists(path) && n < 100; ++n) snprintf(path, sizeof(path), "%s/%s-%s-%d.png", dir, gp.base, stamp, n);
	ui_osd(ui_screenshot_stacked(path) ? "Screenshot saved" : "Screenshot failed");
}

/* ---------------------------------------------------------------- session */

enum GameExit game_run(Config* c, const char* rom, bool fromLibrary) {
	cfg = c;
	debug = getenv("GBADS_DEBUG") != NULL;
	game_paths(cfg, rom, &gp);
	if (!emu_open(&emu, rom, gp.sav, true)) {
		ui_osd("Couldn't load the game");
		return fromLibrary ? GAME_TO_LIBRARY : GAME_QUIT_APP;
	}
	fprintf(stderr, "game %s (%s), save %s\n", gp.base, emu.gameCode, gp.sav);
#ifdef __wasi__
	webPlaying = true;
#endif
	ra_game_start(&emu, rom);
	cheats_load(&cheats, cfg, rom, emu.gameCode, gp.cheats);
	if (cheats_on(&cheats)) cheats_apply(&cheats, &emu);
	cheatsOpen = false;
	ui_on_background(on_background);
	ui_on_foreground(on_foreground);

	mc = NULL;
	gameTexAll = true;
	mapW = GBA_W;
	mapH = GBA_H;
	memset(&mapRes, 0, sizeof(mapRes));
	mapShownIdx = -1;
	stillValid = false;
	mapInactive = false;
	frameCount = 0;
	singleShowsMap = false;
	menuOpen = false;
	logoOnly = false;
	snapProf = NULL;
	hudProf = NULL;
	hudOn = false;
	{
		const MapProfile* cp = profile_find(emu.gameCode);
		ctlProf = cp && (cp->controls.fourFace || cp->controls.snes.bottom) ? cp : NULL;
		snesHere = false;
		if (ctlProf) {
			ctl_load();
			fprintf(stderr, "controls: %s\n", ctl_name(ctlMode));
		}
	}
	if (cfg->map) {
#ifdef __ANDROID__
		android_set_pinning(cfg->pinCores);
#endif
		mc = mapcap_create(rom, emu.gameCode, cfg->mapCrop);
		const MapProfile* lp = profile_find(emu.gameCode);
		memprobe_virtual = lp ? lp->virt : NULL;
		logoOnly = lp && lp->logoOnly;
		snapProf = logoOnly && lp->snap.showMin ? lp : NULL;
		/* the HUD moves only when there's another screen to put it on */
		hudProf = lp && lp->hud.on[0].addr && nScreens > 1 ? lp : NULL;
		emu_hud_game_reg(hudProf ? hudProf->hud.gameReg.addr : 0, hudProf ? hudProf->hud.gameReg.bits : 0);
		emu_hud_band(hudProf && hudProf->hud.band, true, hudProf && hudProf->hud.band == BAND_WATCH);
		combatProf = mc && lp && lp->combat[0].addr && nScreens > 1 ? lp : NULL;
		combatOn = combatMade = false;
		overOn = false;
		for (int m = 0; m < 4; ++m) pieces[m].valid = false, pieces[m].still = 0;
		if (hudProf) {
			/* the sprites that move; tagged ones only if the tags go in */
			memcpy(hudObjs, hudProf->hud.objs, sizeof(hudObjs));
			bool patched = profile_patch_rom(hudProf, &emu);
			if (patched) fprintf(stderr, "HUD split: ROM patched\n");
			for (int i = 0; !patched && i < 8; ++i) hudObjs[i].tagged = 0;
			memcpy(hudWorldObjs, hudProf->hud.world.objs, sizeof(hudWorldObjs));
			for (int i = 0; !patched && i < 4; ++i) hudWorldObjs[i].tagged = 0;
		}
		hudOn = false;
		hudShown = false;
		mirrorHave = mirrorOn = false;
		hudScene = false;
		hudFadeEff = hudFadeIn = 0;
		memset(hudPalRef, 0, sizeof(hudPalRef));
		hudWorld = false;
		hudAlt = false;
		hudLogoDone = false;
		hudLogoBelowDone = false;
		hudBackValid = false;
		hudRegrab = 0;
		snapShow = snapRevert = 0;
		if (mc) mapcap_set_still(mc, !map_anim());
		if (logoOnly) fprintf(stderr, "logo only: %s\n", lp->title);
		if (mc) {
			fprintf(stderr, "map profile: %s\n", mapcap_title(mc));
			mapcap_size(mc, &mapW, &mapH);
			/* the full map is always up, so hide the game's own corner mini-map */
			const MapProfile* prof = profile_find(emu.gameCode);
			int nRules = 0;
			while (prof && nRules < 4 && prof->hideSprites[nRules].x1) ++nRules;
			emu_hide_sprites(&emu, prof ? prof->hideSprites : NULL, nRules);
		} else if (!logoOnly) {
			fprintf(stderr, "no map profile for %s\n", emu.gameCode);
		}
	}
	for (int i = 0; i < nViews; ++i) {
		gameTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
		mapTex[i] = SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH);
		combatTex[i] = combatProf ? SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, mapW, mapH) : NULL;
		if (combatTex[i]) SDL_SetTextureBlendMode(combatTex[i], SDL_BLENDMODE_NONE);
		hudTex[i] = hudProf ? SDL_CreateTexture(views[i].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, HUD_CW, HUD_CH) : NULL;
		if (hudTex[i]) SDL_SetTextureBlendMode(hudTex[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureBlendMode(gameTex[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureBlendMode(mapTex[i], SDL_BLENDMODE_NONE);
	}

	unsigned bc = (unsigned) strtoul(cfg->borderColor, NULL, 16);
	borderColor = (SDL_Color) { (bc >> 16) & 0xFF, (bc >> 8) & 0xFF, bc & 0xFF, 255 };
	overlayMode[0][0] = overlayMode[1][0] = 0;
	uint32_t logoGen = logo_generation();
	if (ui_load_logo(cfg, rom, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) make_logo_canvas();
	mapframe_init(&mf, mc ? profile_find(emu.gameCode) : NULL, mapW, mapH);
	canvasTexW = canvasTexH = 0;
	snprintf(framePath, sizeof(framePath), "%s/frames", cfg->stateDir);
	mkdirs(framePath);
	snprintf(framePath, sizeof(framePath), "%s/frames/%.4s.png", cfg->stateDir, emu.gameCode);
	/* (read before the request: with GBADS_SYNC the extraction is done by
	 * the time frame_request returns) */
	uint32_t frameGen = logo_generation();
	if (mf.prof && mf.prof->frame.t && !mapframe_load_atlas(&mf, framePath)) frame_request(rom, emu.gameCode, framePath);

	memset(pageLen, 0, sizeof(pageLen));
	pages[0][pageLen[0]++] = M_RESUME;
	pages[0][pageLen[0]++] = M_RESTART;
	pages[0][pageLen[0]++] = M_SAVE;
	pages[0][pageLen[0]++] = M_LOAD;
	pages[2][pageLen[2]++] = M_CHEATS;
	if (ra_enabled()) pages[2][pageLen[2]++] = M_ACH;
	pages[2][pageLen[2]++] = M_SHOT;
	if (fromLibrary) pages[0][pageLen[0]++] = M_LIBRARY;
	pages[0][pageLen[0]++] = M_EXIT;
	pages[1][pageLen[1]++] = M_SWAP;
	pages[1][pageLen[1]++] = M_SCALE;
	pages[1][pageLen[1]++] = M_LCD;
	pages[1][pageLen[1]++] = M_OVERLAY;
	pages[1][pageLen[1]++] = M_LOGO;
	pages[1][pageLen[1]++] = M_FRAMEBUF;
	pages[1][pageLen[1]++] = M_MATCH;
	pages[2][pageLen[2]++] = M_REWIND;
	pages[2][pageLen[2]++] = M_FPS;
	set_page(0);

	if (cfg->loadState[0]) {
		if (!state_load(cfg->loadState)) fprintf(stderr, "could not load %s\n", cfg->loadState);
		cfg->loadState[0] = 0;
	} else if (cfg->autoResume && file_exists(gp.autoState)) {
		if (state_load(gp.autoState)) {
			ui_osd("Resumed. Tap or HOME for the menu");
		}
	} else {
		ui_osd("Tap the screen or press HOME for the menu");
	}
	rewind_setup();

	/* The device runs at a fixed rate. The GBA's own rate follows the game's
	 * sound settings (32768 Hz at power-on, 65536 Hz once Zero Mission is
	 * running...) and can change at any moment, so samples go through a
	 * converter that is rebuilt whenever it does. Opening the device at the
	 * rate of the moment made resumed games starve and fresh boots overflow. */
	SDL_AudioSpec want = { 0 }, have;
	want.freq = 48000;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	/* GBADS_AUDIO_SAMPLES: testing only (SDL's dummy driver keeps exact time
	 * only when the buffer is a whole number of milliseconds, e.g. 480) */
	want.samples = getenv("GBADS_AUDIO_SAMPLES") ? atoi(getenv("GBADS_AUDIO_SAMPLES")) : 512;
	SDL_AudioDeviceID audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
#if !defined(__wasi__) && !defined(__ANDROID__)
	if (!audio && getenv("AUDIODEV")) {
		/* the device launch.sh asked for (the RG DS Plus's Stock Linux OS: ALSA's pulse)
		 * won't open: the driver's own default instead */
		fprintf(stderr, "no audio on %s (%s): the default device instead\n", getenv("AUDIODEV"), SDL_GetError());
		unsetenv("AUDIODEV");
		SDL_AudioQuit();
		SDL_AudioInit(NULL);
		audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
	}
#endif
#ifndef __wasi__
	if (!audio) {
		/* (a player on ROCKNIXDS heard nothing: the driver SDL picked may
		 * not reach the sound server; the others, in turn) */
		fprintf(stderr, "no audio with %s: %s\n", SDL_GetCurrentAudioDriver() ? SDL_GetCurrentAudioDriver() : "no driver", SDL_GetError());
		static const char* const tryDrivers[] = { "pipewire", "pulseaudio", "alsa" };
		const char* first = SDL_GetCurrentAudioDriver();
		for (int i = 0; i < 3 && !audio; ++i) {
			if (first && !strcmp(first, tryDrivers[i])) continue;
			SDL_AudioQuit();
			if (SDL_AudioInit(tryDrivers[i]) != 0) continue;
			audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
			if (!audio) fprintf(stderr, "no audio with %s: %s\n", tryDrivers[i], SDL_GetError());
		}
		if (!audio) SDL_AudioQuit(), SDL_AudioInit(NULL);
	}
#endif
	if (audio) {
		SDL_PauseAudioDevice(audio, 0);
		fprintf(stderr, "audio: %s%s%s, %d Hz, %d channels, %d samples\n", SDL_GetCurrentAudioDriver(), getenv("AUDIODEV") ? " on " : "",
		        getenv("AUDIODEV") ? getenv("AUDIODEV") : "", have.freq, have.channels, have.samples);
	} else {
		fprintf(stderr, "no audio: %s\n", SDL_GetError());
	}
	SDL_AudioStream* audioConv = NULL;
	int convFrom = 0;
	const int targetQueued = want.freq * cfg->audioLatency / 1000;

	static int16_t samples[4096 * 2];
	uint32_t lastRoomKey = 0;
	int sinceMapRequest = 1 << 20;
	int lastFails = 0;
	bool forceMap = true, wasDark = false;
	long refreshAt = -1;
	bool hotkeyBack = !strcmp(cfg->hotkey, "back");
	int modBtn = hotkeyBack ? B_SELECT : B_HOME;
	bool comboUsed = false;
	int comboHold = -1, comboFrames = 0; /* menu combo: the button held back, frames since */
	int comboPulse[2] = { 0, 0 };        /* SELECT, START handed to the game late */
	int selectPulse = 0;
	unsigned ffPhase = 0;
	bool noPace = getenv("GBADS_NOPACE") != NULL;
	enum GameExit exitTo = GAME_QUIT_APP;
	bool done = false;
	bool wasMenu = false;

	pacing_update();
	const double freq = (double) SDL_GetPerformanceFrequency();
	double next = SDL_GetPerformanceCounter() / freq;
	double fpsT0 = next;
	int fpsFrames = 0;

#ifdef __ANDROID__
	/* a target well under the frame time: the system clocks the CPU so each
	 * frame's work takes about this long, leaving room for heavier frames
	 * (with the full 16.7 ms it clocked down until frames barely fit; with
	 * 65% the average was 11 ms and uneven frames went to 17-21 ms) */
	android_perf_init((int64_t) (frameSec * 0.5 * 1e9));
	android_set_pinning(cfg->pinCores);
	android_pin_game_thread();
#endif
	while (!done) {
		ahead_sync(); /* the core is the loop's again */
		Uint64 workT0 = SDL_GetPerformanceCounter(); /* this frame's work, without the wait at the end */
		long preempt0 = thread_preemptions();
		double cpuMs0 = thread_cpu_ms();
		if (mf.prof && mf.prof->frame.t && !mf.atlas && logo_generation() != frameGen) {
			frameGen = logo_generation();
			if (mapframe_load_atlas(&mf, framePath)) canvasDirty = true;
		}
		if (!logoTex[0] && !logoTex[1] && logo_generation() != logoGen) {
			logoGen = logo_generation();
			if (ui_load_logo(cfg, NULL, emu.gameCode, logoTex, &logoW, &logoH, &logoPixel)) {
				make_logo_canvas();
				hudLogoBelowDone = hudLogoDone = false; /* the HUD's pictures with it */
				if (combatMade) {
					/* and the fight picture (and the map picture's logo behind) */
					make_combat_canvas();
					overDirty = true;
					if (mapShownIdx >= 0) upload_map_frame(mapShownIdx);
				}
			}
		}
		if (!ui_events()) {
			exitTo = GAME_QUIT_APP;
			break;
		}
		ui_tick();
		ui_read_buttons();
		{
			const char* ld = ui_script_load();
			if (ld && !state_load(ld)) fprintf(stderr, "could not load %s\n", ld);
		}

		/* ---- pointer: any tap opens the menu; taps in the menu press buttons */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (!menuOpen) {
				if (p.kind == 1 && ctl_tap(p.screen, p.x, p.y)) continue;
				if (p.kind == 1) {
					open_menu();
					ignorePointerUp = true;
				}
				continue;
			}
			if (p.screen != ui_touch_screen()) continue;
			if (achOpen) {
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						close_ach(); /* Back */
						ignorePointerUp = true;
						continue;
					}
					achPressed = true;
					achPressY = p.y;
					achPressScroll = ra_list_scroll_pos();
				} else if (p.kind == 3 && achPressed) {
					ra_list_set_scroll(p.screen, achPressScroll - (p.y - achPressY));
				} else if (p.kind == 2) {
					achPressed = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (cheatsOpen) {
				float px = ui_px(p.screen);
				if (p.kind == 1) {
					if (tab_at(p.screen, p.x, p.y, 2) == 0) {
						cheatsOpen = false; /* Back */
						ignorePointerUp = true;
						continue;
					}
					cheatPressRow = cheat_row_at(p.screen, p.x, p.y);
					cheatPressY = p.y;
					cheatPressScroll = cheatScroll;
					cheatDragging = false;
					ignorePointerUp = false;
				} else if (p.kind == 3 && cheatPressRow != -2) {
					if (abs(p.y - cheatPressY) > px * 4) cheatDragging = true;
					if (cheatDragging) {
						CheatGeom g = cheat_geom(p.screen);
						cheatScroll = SDL_clamp(cheatPressScroll - (p.y - cheatPressY), 0, cheat_max_scroll(&g));
					}
				} else if (p.kind == 2) {
					int row = cheat_row_at(p.screen, p.x, p.y);
					if (!ignorePointerUp && !cheatDragging && row >= 0 && row == cheatPressRow && cheatRows[row] >= 0) {
						cheatSel = row;
						toggle_cheat(row);
					}
					cheatPressRow = -2;
					cheatDragging = false;
					ignorePointerUp = false;
				}
				continue;
			}
			if (p.kind == 1) {
				int t = tab_at(p.screen, p.x, p.y, N_PAGES);
				if (t >= 0) {
					if (t != curPage) set_page(t);
					ignorePointerUp = true;
					continue;
				}
			}
			int it = item_at(p.screen, p.x, p.y);
			if (p.kind == 1) {
				ignorePointerUp = false;
				menuPressed = it;
				if (it >= 0) menuSel = it;
			} else if (p.kind == 3 && menuPressed >= 0 && it >= 0) {
				menuSel = it;
			} else if (p.kind == 2) {
				if (!ignorePointerUp && it >= 0 && it == menuPressed && activate(menuItems[it], &exitTo)) done = true;
				menuPressed = -1;
				ignorePointerUp = false;
			}
		}
		if (done) break;

		/* ---- the menu combo (menu_combo = select+start; on Android, where HOME
		 * belongs to the system): pressing both opens the menu. The first of
		 * the two is held back from the game for a few frames, so that a combo
		 * doesn't also press SELECT (a map in some games) or START (pause);
		 * if the other doesn't follow, it goes to the game after all. */
		bool comboOpened = false;
		if (!menuOpen && !strcmp(cfg->menuCombo, "select+start")) {
			if (comboHold < 0) {
				if (btn_pressed(B_SELECT) && btn_pressed(B_START)) comboHold = B_START, comboFrames = 0;
				else if (btn_pressed(B_SELECT) && !btnNow[B_START]) comboHold = B_SELECT, comboFrames = 0;
				else if (btn_pressed(B_START) && !btnNow[B_SELECT]) comboHold = B_START, comboFrames = 0;
			}
			if (comboHold >= 0) {
				int other = comboHold == B_SELECT ? B_START : B_SELECT;
				if (btnNow[comboHold] && btnNow[other]) {
					open_menu();
					comboOpened = true;
					comboUsed = true; /* not a hotkey, nor a plain SELECT for the game */
					comboHold = -1;
				} else if (!btnNow[comboHold]) {
					comboPulse[comboHold == B_START] = 4; /* a quick tap: give it to the game now */
					comboHold = -1;
				} else if (++comboFrames > 5) {
					comboHold = -1; /* just held: the game gets it from here */
				}
			}
		} else {
			comboHold = -1;
		}

		/* ---- HOME (or the hotkey modifier) alone toggles the menu; with a button it's a hotkey */
		bool mod = btnNow[modBtn];
		if (mod && !menuOpen) {
			if (btn_pressed(B_START)) exitTo = GAME_QUIT_APP, done = true, comboUsed = true;
			if (btn_pressed(B_R)) ui_osd(state_save(gp.state) ? "State saved" : "Save failed"), comboUsed = true;
			if (btn_pressed(B_L) && file_exists(gp.state)) state_load(gp.state), ui_osd("State loaded"), forceMap = true, comboUsed = true;
			if (btn_pressed(B_X)) cfg->showFps = !cfg->showFps, comboUsed = true;
			if (btn_pressed(B_Y)) swap_screens(), comboUsed = true;
		}
		if (btn_released(B_HOME) && !(modBtn == B_HOME && comboUsed)) {
			if (menuOpen) menuOpen = false;
			else open_menu();
		}
		if (btn_released(modBtn)) {
			if (comboUsed) suppress_held();
			if (hotkeyBack && !comboUsed && !menuOpen) selectPulse = 4; /* plain Select goes to the game */
			comboUsed = false;
		}
		if (done) break;

		if (menuOpen && achOpen) {
			int scr = ui_touch_screen();
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) ra_list_scroll(scr, (k ? 1 : -1) * ra_list_step(scr));
			}
			int page = ach_area(scr).h * 3 / 4;
			if (btn_pressed(B_L)) ra_list_scroll(scr, -page);
			if (btn_pressed(B_R)) ra_list_scroll(scr, page);
			if (btn_pressed(B_B)) close_ach();
			if (btn_pressed(B_START)) close_ach(), menuOpen = false;
		} else if (menuOpen && cheatsOpen) {
			/* D-pad repeats while held */
			static int held[2];
			for (int k = 0; k < 2; ++k) {
				int b = k ? B_DOWN : B_UP;
				held[k] = btnNow[b] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) cheat_move(k ? 1 : -1);
			}
			CheatGeom g = cheat_geom(ui_touch_screen());
			int page = SDL_max(1, g.listH / (g.rowH + g.gap) - 1);
			if (btn_pressed(B_L)) for (int k = 0; k < page; ++k) cheat_move(-1);
			if (btn_pressed(B_R)) for (int k = 0; k < page; ++k) cheat_move(1);
			if (btn_pressed(B_A)) toggle_cheat(cheatSel);
			if (btn_pressed(B_B)) cheatsOpen = false;
			if (btn_pressed(B_START)) cheatsOpen = false, menuOpen = false;
		} else if (menuOpen) {
			if (!comboOpened) { /* (START just opened it: not also a close) */
				if (btn_pressed(B_L) && curPage > 0) set_page(curPage - 1);
				if (btn_pressed(B_R) && curPage < N_PAGES - 1) set_page(curPage + 1);
				if (btn_pressed(B_DOWN)) menuSel = (menuSel + 1) % nMenuItems;
				if (btn_pressed(B_UP)) menuSel = (menuSel + nMenuItems - 1) % nMenuItems;
				if (btn_pressed(B_B) || btn_pressed(B_START)) menuOpen = false;
				else if (btn_pressed(B_A) && activate(menuItems[menuSel], &exitTo)) break;
			}
			if (menuOpen && !wasMenu && audio) {
				SDL_PauseAudioDevice(audio, 1);
				SDL_ClearQueuedAudio(audio);
			}
		}
		if (wasMenu && !menuOpen) {
			suppress_held();
			if (audio) SDL_PauseAudioDevice(audio, 0);
			next = SDL_GetPerformanceCounter() / freq;
			fpsT0 = next;
			fpsFrames = 0;
			forceMap = true;
		}
		wasMenu = menuOpen;

		if (menuOpen) {
			ra_idle();
			ui_clear();
			draw_game();
			draw_menu();
			ui_present();
			SDL_Delay(16);
			next = SDL_GetPerformanceCounter() / freq;
			continue;
		}

		if (btn_pressed(B_L3)) swap_screens();

		/* ---- emulate */
		uint32_t keys = 0;
		for (int b = 0; b < B_COUNT; ++b) {
			if (!btnNow[b]) suppress[b] = false; /* released: counts again */
		}
		if (!btnNow[modBtn] || hotkeyBack) {
			/* the SNES layout only where it applies (Super Mario Advance 2: in
			 * a level; on the world map R switches Mario and Luigi, and only A
			 * enters a level) */
			bool snes = ctl_four() && ctl_snes() && snesHere;
			if (snes) {
				/* by position, as the profile says (Super Mario Advance 2: B
				 * bottom jumps, Y left runs, A right and X top spin jump) */
				const typeof(ctlProf->controls.snes)* sn = &ctlProf->controls.snes;
				int right = cfg->abSwap ? B_B : B_A, bottom = cfg->abSwap ? B_A : B_B;
				if (HELD(right)) keys |= sn->right;
				if (HELD(bottom)) keys |= sn->bottom;
				if (HELD(B_X)) keys |= sn->top;
				if (HELD(B_Y)) keys |= sn->left;
			} else if (ctl_four() && !ctl_snes()) {
				/* by position: top A, bottom B, left L, right R (on the RG DS
				 * the top face button arrives as X, the left one as Y) */
				int right = cfg->abSwap ? B_B : B_A, bottom = cfg->abSwap ? B_A : B_B;
				if (HELD(B_X)) keys |= K_A;
				if (HELD(bottom)) keys |= K_B;
				if (HELD(B_Y)) keys |= K_L;
				if (HELD(right)) keys |= K_R;
			} else {
				if (HELD(B_A)) keys |= K_A;
				if (HELD(B_B)) keys |= K_B;
			}
			if (HELD(B_START) && !(hotkeyBack && btnNow[B_SELECT]) && comboHold != B_START) keys |= K_START;
			if (HELD(B_SELECT) && !hotkeyBack && comboHold != B_SELECT) keys |= K_SELECT;
			if (snes) {
				/* both shoulders are GBA L, panning the camera their own way */
				if (HELD(B_L)) keys |= K_L | KX_PAN_L;
				else if (HELD(B_R)) keys |= K_L | KX_PAN_R;
			} else {
				if (HELD(B_L)) keys |= K_L;
				if (HELD(B_R)) keys |= K_R;
				static bool told;
				if (!told && ctl_four() && ctl_snes() && (HELD(B_L) || HELD(B_R))) {
					told = true; /* (for reports: once) */
					fprintf(stderr, "controls: SNES chosen, but the GBA layout here (game mode %02x, paused %02x)\n",
					        emu_read8(&emu, ctlProf->controls.snes.when[0].addr), emu_read8(&emu, ctlProf->controls.snes.when[2].addr));
				}
			}
			if (HELD(B_UP)) keys |= K_UP;
			if (HELD(B_DOWN)) keys |= K_DOWN;
			if (HELD(B_LEFT)) keys |= K_LEFT;
			if (HELD(B_RIGHT)) keys |= K_RIGHT;
			if (hotkeyBack && btnNow[B_SELECT]) keys &= ~(K_L | K_R | K_START | KX_PAN_L | KX_PAN_R);
		}
		if (selectPulse > 0) keys |= K_SELECT, --selectPulse;
		if (comboPulse[0] > 0) keys |= K_SELECT, --comboPulse[0];
		if (comboPulse[1] > 0) keys |= K_START, --comboPulse[1];
		{
			static uint32_t lastKeys;
			if (debug && keys != lastKeys) fprintf(stderr, "frame %llu: game keys %03x\n", (unsigned long long) frameCount, keys);
			lastKeys = keys;
		}

		bool rewinding = cfg->rewind && btnNow[B_L2];
		bool ff = btnNow[B_R2] && !rewinding;
		int runs = ff ? SDL_max(1, cfg->ffSpeed) : 1;
		/* Audio keeps flowing during fast-forward: every runs-th sample is kept,
		 * so the device gets real-time worth of (sped-up) sound. Starving it
		 * makes PulseAudio/PipeWire grow their buffers after each underrun,
		 * which is what made the lag worse every time. */
		int queuedNow = audio ? (int) (SDL_GetQueuedAudioSize(audio) / 4) : 0;
		bool dropAudio = queuedNow > targetQueued * 3; /* hard cap on drift */
		/* Health report (always on, every 5 s when something went wrong): audio
		 * underruns (the queue ran dry = a crackle), slow loop iterations, and
		 * how often the background map capture was running. */
		{
			static int underruns, slow, captureBusy, n;
			static long preemptT0 = -1;
			if (preemptT0 < 0) preemptT0 = thread_preemptions();
			static Uint64 lastIter;
			Uint64 nowT = SDL_GetPerformanceCounter();
			if (lastIter && !ff && (nowT - lastIter) * 1000 / SDL_GetPerformanceFrequency() > 25) ++slow;
			lastIter = nowT;
			if (audio && frameCount > 120 && queuedNow == 0 && !ff) ++underruns;
			if (mc && mapcap_busy(mc)) ++captureBusy;
			if (++n >= 300) {
#ifdef __ANDROID__
				const bool report = true; /* performance on Android devices is still being measured */
#else
				const bool report = underruns || slow || debug;
#endif
				if (report) {
					double ms = 1000.0 / SDL_GetPerformanceFrequency() / n;
					fprintf(stderr, "frame %llu: last 5 s: %.1f fps, %d audio underrun(s), %d slow frame(s), map capture busy %d%%, "
					                "audio queue %d ms; per frame: emulating %.2f ms, drawing %.2f ms; game thread preempted %ld time(s)\n",
					        (unsigned long long) frameCount, fps, underruns, slow, captureBusy * 100 / n,
					        queuedNow * 1000 / want.freq, emuTicks * ms, drawTicks * ms, thread_preemptions() - preemptT0);
				}
				underruns = slow = captureBusy = n = 0;
				preemptT0 = thread_preemptions();
				emuTicks = drawTicks = 0;
			}
		}
		if (debug && audio && frameCount % 30 == 0) {
			fprintf(stderr, "audio queue %d ms%s\n", queuedNow * 1000 / want.freq, ff ? " (ff)" : "");
		}
		Uint64 emuT0 = SDL_GetPerformanceCounter(), raT = 0, audT = 0;
		double emuCpu0 = thread_cpu_ms();
		if (rewinding) {
			/* Rewinding: each frame goes back one state of the buffer (two
			 * frames) and runs one frame to draw it, so it plays backwards at
			 * double speed; silent, no achievements. At the buffer's start
			 * the picture stays. */
			aheadReady = false; /* (a frame emulated ahead belongs to the present) */
			if (emu_rewind_step(&emu)) {
				emu_frame(&emu, 0);
				ui_osd("Rewinding");
			} else {
				ui_osd("Can't rewind further");
			}
			while (emu_audio_read(&emu, samples, 4096) > 0) {}
			rewindTick = 0;
			wasRewinding = true;
		} else if (wasRewinding) {
			wasRewinding = false;
			forceMap = true; /* the map, as of where the rewind stopped */
		}
		for (int i = 0; i < (rewinding ? 0 : runs); ++i) {
			if (i == 0 && aheadReady) aheadReady = false; /* emulated by the worker while the last frame was shown */
			else run_frame(keys);
			Uint64 raT0 = SDL_GetPerformanceCounter();
			ra_frame();
			raT += SDL_GetPerformanceCounter() - raT0;
			Uint64 audT0 = SDL_GetPerformanceCounter();
			size_t got;
			while ((got = emu_audio_read(&emu, samples, 4096)) > 0) {
				if (!audio || dropAudio) continue;
				if (runs > 1) {
					size_t outN = 0;
					for (size_t f = 0; f < got; ++f) {
						if (ffPhase++ % runs == 0) {
							samples[outN * 2] = samples[f * 2];
							samples[outN * 2 + 1] = samples[f * 2 + 1];
							++outN;
						}
					}
					got = outN;
				}
				if (!got) continue;
				/* matched to the display, the game makes its sound audioSpeed times
				 * faster than real time: resample it from that much higher a rate */
				int rate = (int) lround(emu_audio_rate(&emu) * audioSpeed);
				if (rate != convFrom || !audioConv) {
					if (audioConv) SDL_FreeAudioStream(audioConv);
					audioConv = SDL_NewAudioStream(AUDIO_S16SYS, 2, rate, AUDIO_S16SYS, 2, want.freq);
					if (debug || convFrom) fprintf(stderr, "frame %llu: game audio rate %d Hz\n", (unsigned long long) frameCount, rate);
					convFrom = rate;
				}
				if (!audioConv) continue;
				SDL_AudioStreamPut(audioConv, samples, (int) got * 4);
				static int16_t conv[8192 * 2];
				int bytes;
				while ((bytes = SDL_AudioStreamGet(audioConv, conv, sizeof(conv))) > 0) {
					/* Paced by the display, the loop can't slow down or speed up to
					 * keep the sound queue level, so the sound gives: a sample frame
					 * dropped or repeated per chunk (at most ~0.1%, inaudible) while
					 * the queue is a quarter off its target. */
					if (pacedHz > 0 && bytes >= 8) {
						int q = (int) (SDL_GetQueuedAudioSize(audio) / 4);
						if (q > targetQueued * 5 / 4) bytes -= 4;
						else if (q < targetQueued * 3 / 4 && bytes + 4 <= (int) sizeof(conv)) {
							memcpy((char*) conv + bytes, (char*) conv + bytes - 4, 4);
							bytes += 4;
						}
					}
					int vol = ui_volume();
					if (vol < 100) {
						/* (squared: closer to how loud it sounds) */
						int gain = vol * vol * 65536 / 10000;
						for (int k = 0; k < bytes / 2; ++k) conv[k] = (int16_t) (conv[k] * gain >> 16);
					}
					SDL_QueueAudio(audio, conv, bytes);
				}
			}
			audT += SDL_GetPerformanceCounter() - audT0;
		}
		Uint64 emuT1 = SDL_GetPerformanceCounter();
		double emuCpu1 = thread_cpu_ms();
		emuTicks += emuT1 - emuT0;
		++frameCount;
		/* the rewind buffer: a state every second emulated frame */
		if (cfg->rewind && !rewinding && (rewindTick += runs) >= 2) {
			emu_rewind_append(&emu);
			rewindTick = 0;
		}

		/* ---- map refresh */
		if (mc) {
			++sinceMapRequest;
			bool dark = frame_is_dark(emu.fb);
			if (wasDark && !dark) refreshAt = (long) frameCount + 6;
			wasDark = dark;
			if (refreshAt >= 0 && (long) frameCount >= refreshAt) {
				forceMap = true;
				refreshAt = -1;
				if (debug) fprintf(stderr, "frame %llu: room transition\n", (unsigned long long) frameCount);
			}
			if (mapShownIdx >= 0 && frameCount % 30 == 0 && mapcap_in_menu(mc, &emu)) {
				/* quit to the title: the map belongs to the old session */
				mapShownIdx = -1;
				mapInactive = false;
				forceMap = true;
				if (debug) fprintf(stderr, "frame %llu: back at the menus, map cleared\n", (unsigned long long) frameCount);
			}
			uint32_t rk = mapcap_room_key(mc, &emu);
			bool roomChanged = rk && rk != lastRoomKey;
			/* Back off while captures keep failing (e.g. the game says "playing"
			 * but the map can't open: intro cutscenes, briefings). Each failed
			 * attempt emulates a few seconds in the background, which starves
			 * the game and its audio on slower devices. */
			const char* why = NULL;
			int fails = mapcap_failures(mc, &why);
			if (fails != lastFails) {
				if (debug && fails) fprintf(stderr, "frame %llu: map capture failed (%d in a row): %s\n",
				                            (unsigned long long) frameCount, fails, why ? why : "?");
				const MapProfile* dp = mapcap_profile(mc);
				if (fails && why && dp && dp->dimWhenUnavailable &&
				    (!strcmp(why, "map screen did not appear") || !strcmp(why, "map button did nothing"))) {
					mapInactive = true;
				}
				lastFails = fails;
			}
			/* quick "no map here" failures (A Link to the Past indoors) cost little
			 * and are expected, so they don't back off */
			bool cheapFail = why && !strcmp(why, "map button did nothing");
			int interval = cheapFail ? cfg->mapInterval : cfg->mapInterval << SDL_min(fails, 3);
			/* after failures, a room change still triggers a try, but no more
			 * often than the normal interval */
			bool due = sinceMapRequest >= interval ||
			           ((forceMap || roomChanged) && (!fails || sinceMapRequest >= cfg->mapInterval));
			if (due && !mapcap_busy(mc) && mapcap_live_ok(mc, &emu)) {
				if (mapcap_request(mc, &emu)) {
					sinceMapRequest = 0;
					lastRoomKey = rk;
					forceMap = false;
				}
			}
			bool fresh = mapcap_poll(mc, mapRes.serial, &mapRes);
			if (fresh && mapcap_in_menu(mc, &emu)) {
				/* a capture that finished after quitting to the title: drop it */
			} else if (fresh) {
				mapInactive = false;
				if (mapRes.hasTile) mapframe_set_tile(&mf, mapRes.tile, mapRes.tileK, mapRes.tileX, mapRes.tileY);
				mapShownIdx = -1;
				/* (pages, a few seconds each: Fire Emblem's unit status; they
				 * keep turning at their pace through new captures) */
				bool pages = mapRes.nFrames > 1 && mapRes.frameStep >= 60;
				if (!pages) mapAnimStart = frameCount;
				/* the still (each marker frozen) is only shown with map animations
				 * off, and building it is real work: only then */
				bool stillChanged = false;
				stillStale = map_anim();
				if (!stillStale) stillChanged = update_still();
				int first = pages ? (int) ((frameCount - mapAnimStart) / mapRes.frameStep) % mapRes.nFrames : 0;
				upload_map_frame(map_anim() ? first : STILL_IDX);
				if (debug) fprintf(stderr, "frame %llu: map updated (%d frame(s) %d apart%s)\n", (unsigned long long) frameCount,
				                   mapRes.nFrames, mapRes.frameStep, stillStale ? "" : stillChanged ? ", still changed" : ", still unchanged");
			} else if (map_anim() && mapRes.nFrames > 1 && mapShownIdx >= 0) {
				int idx = (int) ((frameCount - mapAnimStart) / SDL_max(1, mapRes.frameStep)) % mapRes.nFrames;
				upload_map_frame(idx);
			}
		}

		if (snapProf) update_snapshot();
		if (hudProf) update_hud();
		if (combatProf) {
			/* a fight: after a few frames of it, not on one odd frame */
			static int streak;
			combatInGame = combatProf->hud.on[0].addr && memprobes_match(combatProf->hud.on, 1, &emu);
			if ((combatInGame || combatProf->hud.logoAlways) && combatProf->hud.logoUnder && !combatMade) make_combat_canvas();
			bool c = memprobes_match(combatProf->combat, 2, &emu) ||
			         (combatProf->combatOr[0].addr && memprobes_match(combatProf->combatOr, 2, &emu));
			streak = c != combatOn ? streak + 1 : 0;
			if (streak >= 3) {
				combatOn = c;
				streak = 0;
				if (combatOn && !combatMade) make_combat_canvas();
				if (debug) fprintf(stderr, "frame %llu: fight %s\n", (unsigned long long) frameCount, combatOn ? "on" : "off");
			}
		}
		if (ctl_snes()) snesHere = memprobes_match(ctlProf->controls.snes.when, 3, &emu);
		if (ctlProf) {
			/* switch after a few frames in the new state, not on one odd frame */
			static int ctlStreak;
			ctlStreak = ctl_playing_now() != ctlPlaying ? ctlStreak + 1 : 0;
			if (ctlStreak >= 3) {
				ctlPlaying = !ctlPlaying;
				ctlStreak = 0;
				if (debug && ctlMode == CTL_AUTO) {
					fprintf(stderr, "frame %llu: auto controls: %s\n", (unsigned long long) frameCount, ctlPlaying ? "four-face" : "GBA");
				}
			}
		}

		Uint64 drawT0 = SDL_GetPerformanceCounter();
		double drawCpu0 = thread_cpu_ms();
		/* frame buffer: start the next frame now; this one is drawn from a copy */
		const uint32_t* shownFb = emu.fb;
		if (cfg->frameBuffer && !ff && !rewinding) {
			memcpy(aheadFb, emu.fb, sizeof(aheadFb));
			if (ahead_start(keys)) aheadInFlight = true, shownFb = aheadFb;
		}
		if (mirrorOn && mirrorHave) shownFb = mirrorHeld; /* (hud.mirror: the picture from before) */
		/* only the view the game is on needs it (all of them right after a swap,
		 * since the menu draws the game without uploading) */
		for (int i = 0; i < nViews; ++i) {
			if (gameTexAll || i == screens[game_screen()].view) ui_update_texture(gameTex[i], shownFb, GBA_W * 4);
		}
		gameTexAll = false;
		ui_sync_view(cfg->vsync || pacedHz > 0 ? screens[game_screen()].view : -1); /* Match Display Rate; vsync: testing (docs/ANDROID.md) */
		{
			/* the other screen's redraw waits a frame if this one is past 70% of its time */
			static double budget = -1;
			if (budget < 0) budget = getenv("GBADS_DRAW_BUDGET") ? atof(getenv("GBADS_DRAW_BUDGET")) : frameSec * 1000.0 * 0.7;
			ui_present_budget(ff ? -1 : screens[game_screen()].view, workT0, budget);
		}
		ui_clear();
		draw_game();
		if (shotPending) {
			shotPending = false;
			take_screenshot();
		}
		ra_draw(nScreens > 1 ? 1 - game_screen() : 0); /* after the screenshot: not in it */
		ui_present();
		Uint64 drawT1 = SDL_GetPerformanceCounter();
		drawTicks += drawT1 - drawT0;
		{
			double msPer = 1000.0 / (double) SDL_GetPerformanceFrequency();
			double total = (double) (drawT1 - workT0) * msPer;
			/* matched, each frame includes the wait for the display's refresh:
			 * only one that missed its refresh (about two frames long) is slow */
			if (!ff && total > frameSec * 1000.0 * (pacedHz > 0 ? 1.4 : 1.0)) {
				SlowFrame f = { total, (double) (emuT0 - workT0) * msPer, (double) (emuT1 - emuT0 - raT) * msPer, (double) raT * msPer,
					            (double) (drawT0 - emuT1) * msPer, (double) (drawT1 - drawT0) * msPer, 0, 0, 0,
					            (unsigned long long) frameCount };
				/* by screen (top first, as they're stacked), not by view */
				int sTop = 0, sBot = nScreens > 1 ? 1 : 0;
				int vTop = screens[sTop].view, vBot = screens[sBot].view;
				f.top = ui_last_present_ms(vTop);
				f.bottom = vBot != vTop ? ui_last_present_ms(vBot) : 0;
				int red = ui_last_redraws();
				f.redraws = ((red >> vTop) & 1) | (vBot != vTop ? ((red >> vBot) & 1) << 1 : 0);
				f.preempted = thread_preemptions() - preempt0;
				f.cpu = current_cpu();
				f.mhz = cpu_mhz(f.cpu);
				double cpuNow = thread_cpu_ms();
				f.cpuMs = cpuNow - cpuMs0;
				f.waitEmu = SDL_max(0.0, (double) (emuT1 - emuT0) * msPer - (emuCpu1 - emuCpu0));
				f.waitDraw = SDL_max(0.0, (double) (drawT1 - drawT0) * msPer - (cpuNow - drawCpu0));
				f.audio = (double) audT * msPer;
				slow_frame_note(f);
			}
			static Uint64 lastReport;
			if (drawT1 - lastReport > SDL_GetPerformanceFrequency()) {
				slow_frame_report(debug);
				lastReport = drawT1;
			}
		}
#ifdef __ANDROID__
		if (frameCount == 1) fprintf(stderr, "startup: first frame shown\n");
#endif

		/* ---- pacing: real-time clock nudged by audio queue depth */
		double now = SDL_GetPerformanceCounter() / freq;
		fpsFrames += runs; /* emulated frames */
		if (now - fpsT0 >= 1.0) {
			fps = fpsFrames / (now - fpsT0);
			speedPct = fps / GBA_FPS * 100.0;
			if (debug) fprintf(stderr, "speed %.1f fps (%.0f%%)\n", fps, speedPct);
			fpsFrames = 0;
			fpsT0 = now;
		}
		/* fast-forward is paced too: ff_speed frames per real frame. Matched to
		 * the display, presenting in step with it sets the pace; the clock runs
		 * a little faster, as a limit for when the display doesn't make us wait. */
		next += pacedHz > 0 ? frameSec / 1.005 : frameSec;
		if (audio) {
			int queued = (int) (SDL_GetQueuedAudioSize(audio) / 4);
			if (queued > targetQueued * 3 / 2) next += frameSec * 0.015;
			else if (queued < targetQueued / 2) next -= frameSec * 0.015;
		}
		if (next < now - 4 * frameSec) next = now;
		if (noPace) next = now; /* benchmarking: run as fast as possible */
#ifdef __ANDROID__
		android_perf_report((int64_t) ((SDL_GetPerformanceCounter() - workT0) * 1e9 / freq));
#endif
		double wait = next - now;
#ifdef __wasi__
		/* the web build: back to the page every frame, for the wait or not */
		web_sleep(wait > 0 ? (unsigned) (wait * 1000) : 0);
#else
		if (wait > 0) {
			struct timespec ts = { 0, (long) (wait * 1e9) };
			nanosleep(&ts, NULL);
		}
#endif
	}

	ahead_stop(); /* before anything else touches the core */
	ui_on_background(NULL);
	ui_on_foreground(NULL);
	ui_sync_view(-1);
	fprintf(stderr, "leaving game after %llu frames\n", (unsigned long long) frameCount);
	auto_save();
	ra_game_end();
	if (audio) SDL_CloseAudioDevice(audio);
	if (audioConv) SDL_FreeAudioStream(audioConv);
	mapcap_destroy(mc);
	mc = NULL;
	emu_rewind_deinit();
#ifdef __wasi__
	webPlaying = false;
#endif
	emu_close(&emu); /* flushes the battery save */
	for (int i = 0; i < MAP_MAX_FRAMES; ++i) {
		free(mapRes.frames[i]);
		mapRes.frames[i] = NULL;
	}
	free(stillPx);
	stillPx = NULL;
	stillValid = false;
	for (int i = 0; i < nViews; ++i) {
		SDL_DestroyTexture(gameTex[i]);
		SDL_DestroyTexture(mapTex[i]);
		if (hudTex[i]) SDL_DestroyTexture(hudTex[i]);
		gameTex[i] = mapTex[i] = hudTex[i] = NULL;
	}
	ui_free_image(logoTex);
	ui_free_image(combatTex);
	ui_free_image(logoCanvasTex);
	ui_free_image(overlayTex[0]);
	ui_free_image(overlayTex[1]);
	overlayMode[0][0] = overlayMode[1][0] = 0;
	mapframe_free(&mf);
	for (int i = 0; i < 2; ++i) {
		if (canvasTex[i]) SDL_DestroyTexture(canvasTex[i]);
		canvasTex[i] = NULL;
	}
	return exitTo;
}

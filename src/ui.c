#include "ui.h"
#include "android.h"
#include "brand.h"
#include "font.h"
#include "textfont.h"
#include "logo.h"
#include "paths.h"
#include "profiles.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>

View views[2];
int nViews;
Screen screens[2];
int nScreens;
bool btnNow[B_COUNT], btnPrev[B_COUNT];
volatile int uiQuit;

static Config* cfg;
static uint64_t ticks;
static SDL_GameController* pads[8];
static SDL_Joystick* rawJoys[8];
static bool debug;
#ifdef __ANDROID__
static int lastTouchScreen = -1; /* the screen touches came from (Android: either display may have the panel) */
#endif

/* Per-view caches for the pixel pipeline. Several entries, because the game
 * and the map can differ in size (e.g. the cropped Metroid maps); a single
 * slot made them evict each other twice per frame. */
#define PIPE_CACHE 4
typedef struct {
	SDL_Texture* tex;
	int w, h, k, strength;
	uint64_t used;
} PipeEntry;
static PipeEntry pipeTargets[2][PIPE_CACHE], pipeGrids[2][PIPE_CACHE];
static uint64_t pipeClock;

#define PQ 64
static Pointer pq[PQ];
static int pqHead, pqTail;

static char osdText[96];
static uint64_t osdUntil;

/* ------------------------------------------------------------- script */

typedef struct {
	long tick;
	uint32_t btns;
	char action[160];
} ScriptLine;
static ScriptLine* script;
static int scriptLen, scriptPos;
static uint32_t scriptBtns;
static long scriptTapUp = -1;
static float tapX, tapY;
static char pendingShot[160];

static uint32_t parse_btns(const char* s) {
	static const char* names[B_COUNT] = { "A", "B", "X", "Y", "START", "SELECT", "HOME", "L", "R", "L2", "R2", "L3",
		                                  "UP", "DOWN", "LEFT", "RIGHT" };
	uint32_t m = 0;
	char buf[128];
	snprintf(buf, sizeof(buf), "%s", s);
	for (char* t = strtok(buf, "+"); t; t = strtok(NULL, "+")) {
		for (int i = 0; i < B_COUNT; ++i) {
			if (!strcmp(t, names[i])) m |= 1u << i;
		}
	}
	return m;
}

void ui_load_script(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) {
		fprintf(stderr, "cannot open input script %s\n", path);
		return;
	}
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		long t;
		char what[160];
		if (line[0] == '#' || sscanf(line, "%ld %159s", &t, what) != 2) continue;
		script = realloc(script, sizeof(*script) * (scriptLen + 1));
		ScriptLine* s = &script[scriptLen++];
		s->tick = t;
		s->btns = 0;
		s->action[0] = 0;
		if (!strncmp(what, "TAP:", 4) || !strncmp(what, "SHOT:", 5) || !strcmp(what, "QUIT")) {
			snprintf(s->action, sizeof(s->action), "%s", what);
		} else {
			s->btns = parse_btns(what);
		}
	}
	fclose(f);
}

static void push_finger(Uint32 type, float x, float y) {
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.tfinger.touchId = 1;
	ev.tfinger.fingerId = 1;
	ev.tfinger.x = x;
	ev.tfinger.y = y;
	ev.tfinger.windowID = SDL_GetWindowID(views[0].win);
	SDL_PushEvent(&ev);
}

#ifdef __wasi__
/* State Saver's GBA DS tab: a touch on the page (x, y fractions of the window) */
__attribute__((export_name("web_touch"))) void web_touch(int type, float x, float y) {
	push_finger(type == 1 ? SDL_FINGERDOWN : type == 2 ? SDL_FINGERUP : SDL_FINGERMOTION, x, y);
}
#endif

static void run_script(void) {
	while (scriptPos < scriptLen && script[scriptPos].tick <= (long) ticks) {
		ScriptLine* s = &script[scriptPos++];
		if (!strncmp(s->action, "TAP:", 4)) {
			sscanf(s->action + 4, "%f,%f", &tapX, &tapY);
			push_finger(SDL_FINGERDOWN, tapX, tapY);
			scriptTapUp = (long) ticks + 2;
		} else if (!strncmp(s->action, "SHOT:", 5)) {
			snprintf(pendingShot, sizeof(pendingShot), "%s", s->action + 5);
		} else if (!strcmp(s->action, "QUIT")) {
			uiQuit = 1;
		} else {
			scriptBtns = s->btns;
		}
	}
	if (scriptTapUp >= 0 && (long) ticks >= scriptTapUp) {
		push_finger(SDL_FINGERUP, tapX, tapY);
		scriptTapUp = -1;
	}
}

/* ------------------------------------------------------------- setup */

static bool create_view(View* v, int display, const char* title, bool fullscreen, int ww, int wh) {
	Uint32 flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI;
	if (fullscreen) flags |= SDL_WINDOW_FULLSCREEN_DESKTOP;
	if (getenv("GBADS_WINDOW")) sscanf(getenv("GBADS_WINDOW"), "%dx%d", &ww, &wh);
	v->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED_DISPLAY(display), SDL_WINDOWPOS_CENTERED_DISPLAY(display),
	                          ww, wh, flags);
	if (!v->win) {
		fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
		return false;
	}
	v->ren = SDL_CreateRenderer(v->win, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_TARGETTEXTURE |
	                                            (cfg->vsync ? SDL_RENDERER_PRESENTVSYNC : 0));
	if (!v->ren) v->ren = SDL_CreateRenderer(v->win, -1, 0);
	if (!v->ren) {
		fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
		return false;
	}
	{
		/* which renderer, and for OpenGL which GPU driver (a software one,
		 * such as llvmpipe, would mean no GPU at all) */
		SDL_RendererInfo ri;
		const char* gl = NULL;
		if (SDL_GetRendererInfo(v->ren, &ri) == 0 && !strncmp(ri.name, "opengl", 6)) {
			typedef const unsigned char* (*GetString)(unsigned int);
			GetString gs = (GetString) SDL_GL_GetProcAddress("glGetString");
			if (gs) gl = (const char*) gs(0x1F01); /* GL_RENDERER */
		}
		fprintf(stderr, "renderer: %s%s%s\n", SDL_GetRendererInfo(v->ren, &ri) == 0 ? ri.name : "?", gl ? ", " : "", gl ? gl : "");
	}
	SDL_SetRenderDrawBlendMode(v->ren, SDL_BLENDMODE_BLEND);
	SDL_SetRenderDrawColor(v->ren, 0, 0, 0, 255);
	SDL_RenderClear(v->ren);
	SDL_RenderPresent(v->ren);
	return true;
}

static void open_pads(void) {
	for (int i = 0; i < SDL_NumJoysticks() && i < 8; ++i) {
		if (!pads[i] && SDL_IsGameController(i)) {
			pads[i] = SDL_GameControllerOpen(i);
			if (pads[i]) {
				fprintf(stderr, "controller: %s\n", SDL_GameControllerName(pads[i]));
				char* map = SDL_GameControllerMapping(pads[i]);
				if (map) fprintf(stderr, "  mapping: %s\n", map), SDL_free(map);
			}
		} else if (!pads[i] && !rawJoys[i]) {
			rawJoys[i] = SDL_JoystickOpen(i);
			if (rawJoys[i]) fprintf(stderr, "joystick without mapping: %s\n", SDL_JoystickName(rawJoys[i]));
		}
	}
}

bool ui_init(Config* c, bool wantDual) {
	cfg = c;
	debug = getenv("GBADS_DEBUG") != NULL;
	SDL_SetHint(SDL_HINT_APP_NAME, "GBA DS");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg->smooth ? "linear" : "nearest");
#ifdef __ANDROID__
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* BACK opens the menu instead of closing the app */
#endif
	setenv("SDL_VIDEO_WAYLAND_WMCLASS", "gba-ds", 0);
#ifdef __wasi__
	const Uint32 audioInit = 0; /* the web build: no audio (yet) */
#else
	const Uint32 audioInit = SDL_INIT_AUDIO;
#endif
	if (SDL_Init(SDL_INIT_VIDEO | audioInit | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	if (getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
		SDL_GameControllerAddMappingsFromFile(getenv("SDL_GAMECONTROLLERCONFIG_FILE"));
	}
	open_pads();

#ifdef __ANDROID__
	/* SDL only has the display the activity is on. The other one is a
	 * Presentation window in Java (android.c). It's view 1, drawn at
	 * ui_present: with the GPU (the game screen's renderer, made to draw into
	 * the other window), or failing that with a software renderer. */
	if (!create_view(&views[0], 0, "GBA DS", true, 1024, 768)) return false;
	nViews = 1;
	int sw, sh, w0, h0;
	SDL_RendererInfo ri;
	SDL_GetRendererOutputSize(views[0].ren, &w0, &h0);
	bool gles = SDL_GetRendererInfo(views[0].ren, &ri) == 0 && !strncmp(ri.name, "opengles", 8);
	fprintf(stderr, "game screen renderer: %s, %dx%d\n", gles ? ri.name : "not OpenGL ES", w0, h0);
	if (strcmp(cfg->layout, "single") && android_second_size(&sw, &sh) && gles && !getenv("GBADS_SOFT_SECOND") &&
	    android_second_gpu_init(w0, h0)) {
		views[1].ren = views[0].ren;
		views[1].offwin = views[1].retained = true;
		nViews = 2;
		fprintf(stderr, "second screen: GPU\n");
	} else if (strcmp(cfg->layout, "single") && android_second_size(&sw, &sh)) {
		fprintf(stderr, "second screen: software\n");
		views[1].offwin = views[1].retained = true;
		views[1].surf = SDL_CreateRGBSurfaceWithFormat(0, sw, sh, 32, SDL_PIXELFORMAT_ABGR8888);
		views[1].ren = views[1].surf ? SDL_CreateSoftwareRenderer(views[1].surf) : NULL;
		if (views[1].ren) {
			SDL_SetRenderDrawBlendMode(views[1].ren, SDL_BLENDMODE_BLEND);
			nViews = 2;
		} else {
			fprintf(stderr, "second screen renderer: %s\n", SDL_GetError());
		}
	}
	views[0].retained = true; /* drawn only when it changes, like the second screen */
	fprintf(stderr, "android: %s\n", nViews == 2 ? "two screens" : "one screen (no second display)");
	ui_layout();
	return true;
#endif
	int displays = SDL_GetNumVideoDisplays();
	const char* driver = SDL_GetCurrentVideoDriver();
	bool underSway = getenv("SWAYSOCK") != NULL;
	bool dual = !strcmp(cfg->layout, "dual") ||
	            (wantDual && !strcmp(cfg->layout, "auto") && displays >= 2 && !underSway);
	bool fullscreen = strcmp(driver, "offscreen") && strcmp(driver, "dummy") && strcmp(driver, "x11") &&
	                  strcmp(driver, "windows") && strcmp(driver, "cocoa") && !getenv("GBADS_WINDOWED");
	fprintf(stderr, "video driver %s, %d display(s)%s -> %s\n", driver, displays, underSway ? ", sway" : "",
	        dual && displays >= 2 ? "two windows" : "one window");
	for (int i = 0; i < displays && i < 4; ++i) {
		SDL_Rect b = { 0 };
		SDL_GetDisplayBounds(i, &b);
		const char* name = SDL_GetDisplayName(i);
		fprintf(stderr, "  display %d: %s, %dx%d at %d,%d\n", i, name ? name : "?", b.w, b.h, b.x, b.y);
	}
	fprintf(stderr, "touch devices: %d\n", SDL_GetNumTouchDevices());
	if (dual && displays >= 2) {
		nViews = 2;
		if (!create_view(&views[0], 0, "GBA DS", true, 720, 480) ||
		    !create_view(&views[1], 1, "GBA DS map", true, 720, 480)) {
			return false;
		}
	} else {
		nViews = 1;
		bool single = !strcmp(cfg->layout, "single");
		int ww = single ? 720 : 1440, wh = 480;
		if (getenv("SDL_BANANA_PANELS")) {
			/* BananaOS (no compositor): Banana's SDL makes the display both
			 * panels together; the window covers it, and is split like sway's */
			SDL_DisplayMode m;
			if (SDL_GetDesktopDisplayMode(0, &m) == 0 && m.w > 0 && m.h > 0) ww = m.w, wh = m.h;
			fprintf(stderr, "BananaOS: both panels as one window, %dx%d\n", ww, wh);
		}
		if (!create_view(&views[0], 0, "GBA DS", fullscreen, ww, wh)) return false;
	}
	ui_layout();
	return true;
}

void ui_shutdown(void) {
	for (int v = 0; v < 2; ++v) {
		for (int i = 0; i < PIPE_CACHE; ++i) {
			if (pipeTargets[v][i].tex) SDL_DestroyTexture(pipeTargets[v][i].tex);
			if (pipeGrids[v][i].tex) SDL_DestroyTexture(pipeGrids[v][i].tex);
		}
	}
	for (int i = 0; i < nViews; ++i) {
		if (i == 0 || views[i].ren != views[0].ren) SDL_DestroyRenderer(views[i].ren);
		if (views[i].win) SDL_DestroyWindow(views[i].win);
		if (views[i].surf) SDL_FreeSurface(views[i].surf);
		views[i] = (View) { 0 };
	}
	SDL_Quit();
}

void ui_layout(void) {
	if (nViews == 2) {
		nScreens = 2;
		for (int i = 0; i < 2; ++i) {
			int w, h;
			SDL_GetRendererOutputSize(views[i].ren, &w, &h);
#ifdef __ANDROID__
			/* screens in physical order as on ROCKNIX (0 top: the game; 1 bottom:
			 * the touchscreen). The activity, view 0, runs on display 0, which on
			 * the RG DS is the bottom (touch) screen; the top is the second display. */
			screens[1 - i] = (Screen) { i, { 0, 0, w, h } };
#else
			screens[i] = (Screen) { i, { 0, 0, w, h } };
#endif
		}
		return;
	}
	int w, h;
	SDL_GetRendererOutputSize(views[0].ren, &w, &h);
	const char* lay = cfg->layout;
	if (!strcmp(lay, "auto") || !strcmp(lay, "dual")) {
		lay = w >= h * 18 / 10 ? "span-h" : h >= w * 12 / 10 ? "span-v" : "single";
	}
	if (!strcmp(lay, "span-h")) {
		nScreens = 2;
		screens[0] = (Screen) { 0, { 0, 0, w / 2, h } };
		screens[1] = (Screen) { 0, { w / 2, 0, w - w / 2, h } };
	} else if (!strcmp(lay, "span-v")) {
		nScreens = 2;
		screens[0] = (Screen) { 0, { 0, 0, w, h / 2 } };
		screens[1] = (Screen) { 0, { 0, h / 2, w, h - h / 2 } };
	} else {
		nScreens = 1;
		screens[0] = (Screen) { 0, { 0, 0, w, h } };
	}
}

static void sway_span(void) {
	if (!cfg->sway || !getenv("SWAYSOCK") || nViews != 1 || !strcmp(cfg->layout, "single")) return;
	char cmd[256];
	snprintf(cmd, sizeof(cmd), "swaymsg '[pid=%d] fullscreen enable global' >/dev/null 2>&1", (int) getpid());
	if (system(cmd) != 0) fprintf(stderr, "note: swaymsg global fullscreen failed\n");
}

void ui_tick(void) {
	++ticks;
	if (ticks == 2 || ticks == 30 || ticks == 120) sway_span();
	run_script();
	if (cfg->quitAfter && (long) ticks >= cfg->quitAfter) uiQuit = 1;
}

uint64_t ui_ticks(void) {
	return ticks;
}

int ui_touch_screen(void) {
	if (nScreens < 2) return 0;
#ifdef __ANDROID__
	if (lastTouchScreen >= 0 && lastTouchScreen < nScreens) return lastTouchScreen;
#endif
	return !strcmp(cfg->touchScreen, "first") ? 0 : 1;
}

int ui_other_screen(void) {
	if (nScreens < 2) return 0;
	return 1 - ui_touch_screen();
}

/* ------------------------------------------------------------- input */

static int view_of_window(Uint32 id) {
#ifdef __ANDROID__
	if (id == GBADS_SECOND_WINDOW_ID) return nViews == 2 ? 1 : -1;
#endif
	for (int i = 0; i < nViews; ++i) {
		if (SDL_GetWindowID(views[i].win) == id) return i;
	}
	return -1;
}

static void queue_pointer(int kind, int view, float px, float py, bool normalized) {
	Pointer p = { kind, 0, 0, 0 };
	int ts = ui_touch_screen();
	if (normalized && !strcmp(cfg->touchMap, "screen")) {
		/* touch panel coordinates cover just the touch screen */
		p.screen = ts;
		p.x = (int) (px * screens[ts].rect.w);
		p.y = (int) (py * screens[ts].rect.h);
	} else {
		if (view < 0) view = screens[ts].view;
		int w, h;
		SDL_GetRendererOutputSize(views[view].ren, &w, &h);
		int x, y;
		if (normalized) {
			x = (int) (px * w);
			y = (int) (py * h);
		} else {
			int ww, wh;
			SDL_GetWindowSize(views[view].win, &ww, &wh);
			x = (int) (px * w / (ww ? ww : 1));
			y = (int) (py * h / (wh ? wh : 1));
		}
		p.screen = -1;
		for (int i = 0; i < nScreens; ++i) {
			SDL_Rect* r = &screens[i].rect;
			if (screens[i].view == view && x >= r->x && x < r->x + r->w && y >= r->y && y < r->y + r->h) {
				p.screen = i;
				p.x = x - r->x;
				p.y = y - r->y;
			}
		}
		if (p.screen < 0) return;
	}
	if (debug) fprintf(stderr, "pointer kind %d screen %d at %d,%d\n", p.kind, p.screen, p.x, p.y);
#ifdef __ANDROID__
	if (p.kind != 3) {
		fprintf(stderr, "touch %s on screen %d (%s display) at %d,%d\n", p.kind == 1 ? "down" : "up", p.screen,
		        screens[p.screen].view == 0 ? "game activity's" : "second", p.x, p.y);
	}
	if (p.kind == 1) lastTouchScreen = p.screen;
#endif
	int next = (pqTail + 1) % PQ;
	if (next != pqHead) {
		pq[pqTail] = p;
		pqTail = next;
	}
}

bool ui_next_pointer(Pointer* p) {
	if (pqHead == pqTail) return false;
	*p = pq[pqHead];
	pqHead = (pqHead + 1) % PQ;
	return true;
}

static bool mouseDown;
static void (*backgroundFn)(void);
#ifdef __ANDROID__
static void retained_invalidate(void);
#endif

void ui_on_background(void (*fn)(void)) {
	backgroundFn = fn;
}

static void (*foregroundFn)(void);

void ui_on_foreground(void (*fn)(void)) {
	foregroundFn = fn;
}

#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
/* touch_device: the touchscreen read straight from its input device. One
 * finger: its position and whether it's down, sent on as SDL's would be. */
static int evTouchFd = -2; /* -2: not opened yet; -1: none */
static struct { int min, max; } evAx[2];
static int evX, evY, evDown, evWasDown, evLogged;
static bool evMoved;

static void evdev_touch_poll(void) {
	if (evTouchFd == -2) {
		evTouchFd = -1;
		if (!cfg->touchDevice[0]) return;
		evTouchFd = open(cfg->touchDevice, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
		if (evTouchFd < 0) {
			fprintf(stderr, "touch device %s: can't open it\n", cfg->touchDevice);
			return;
		}
		for (int a = 0; a < 2; ++a) {
			struct input_absinfo ai = { 0 };
			if (ioctl(evTouchFd, EVIOCGABS(a ? ABS_MT_POSITION_Y : ABS_MT_POSITION_X), &ai) < 0 || ai.maximum <= ai.minimum) {
				ioctl(evTouchFd, EVIOCGABS(a ? ABS_Y : ABS_X), &ai);
			}
			evAx[a].min = ai.minimum, evAx[a].max = ai.maximum;
		}
		if (cfg->touchRotate < 0) cfg->touchRotate = evAx[0].max - evAx[0].min >= evAx[1].max - evAx[1].min ? 0 : 90;
		fprintf(stderr, "touch device %s: x %d..%d, y %d..%d, turned %d degrees\n", cfg->touchDevice, evAx[0].min, evAx[0].max,
		        evAx[1].min, evAx[1].max, cfg->touchRotate);
	}
	if (evTouchFd < 0) return;
	struct input_event ie[64];
	ssize_t n;
	while ((n = read(evTouchFd, ie, sizeof(ie))) > 0) {
		for (int i = 0; i < (int) (n / sizeof(ie[0])); ++i) {
			const struct input_event* e = &ie[i];
			if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_X || e->code == ABS_X)) evX = e->value, evMoved = true;
			else if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_Y || e->code == ABS_Y)) evY = e->value, evMoved = true;
			else if (e->type == EV_KEY && e->code == BTN_TOUCH) evDown = e->value != 0;
			else if (e->type == EV_ABS && e->code == ABS_MT_TRACKING_ID && e->value < 0) evDown = 0;
			else if (e->type == EV_SYN && e->code == SYN_REPORT) {
				float fx = (float) (evX - evAx[0].min) / SDL_max(1, evAx[0].max - evAx[0].min);
				float fy = (float) (evY - evAx[1].min) / SDL_max(1, evAx[1].max - evAx[1].min);
				float nx = fx, ny = fy;
				switch (cfg->touchRotate) {
				case 90: nx = fy, ny = 1 - fx; break;
				case 180: nx = 1 - fx, ny = 1 - fy; break;
				case 270: nx = 1 - fy, ny = fx; break;
				}
				nx = SDL_clamp(nx, 0.0f, 1.0f), ny = SDL_clamp(ny, 0.0f, 1.0f);
				int kind = evDown && !evWasDown ? 1 : !evDown && evWasDown ? 2 : evDown && evMoved ? 3 : 0;
				if (kind && kind != 3 && (debug || evLogged < 6)) {
					++evLogged;
					fprintf(stderr, "touch %s: raw %d,%d -> %.3f,%.3f\n", kind == 1 ? "down" : "up", evX, evY, nx, ny);
				}
				if (kind) queue_pointer(kind, -1, nx, ny, true);
				evWasDown = evDown;
				evMoved = false;
			}
		}
	}
}
#else
static void evdev_touch_poll(void) {}
#endif

bool ui_events(void) {
	evdev_touch_poll();
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			uiQuit = 1;
			break;
		case SDL_APP_WILLENTERBACKGROUND:
			/* now: SDL stops this thread at its next event pump (Android) */
			fprintf(stderr, "going to the background\n");
			if (backgroundFn) backgroundFn();
#ifdef __ANDROID__
			android_second_restore(); /* SDL puts its own surfaces away for the pause */
#endif
			break;
		case SDL_APP_DIDENTERFOREGROUND:
			fprintf(stderr, "back in the foreground\n");
#ifdef __ANDROID__
			retained_invalidate(); /* SDL's window surface is a new one: redraw both screens */
#endif
			if (foregroundFn) foregroundFn();
			break;
		case SDL_CONTROLLERDEVICEADDED:
		case SDL_JOYDEVICEADDED:
			open_pads();
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_SIZE_CHANGED || ev.window.event == SDL_WINDOWEVENT_RESIZED) {
				ui_layout();
			}
			break;
		case SDL_JOYBUTTONDOWN:
#ifdef __ANDROID__
			/* buttons outside the standard set (15 L2, 16 R2, 17 C, 18 Z...) */
			if (!debug && ev.jbutton.button >= 15) fprintf(stderr, "raw joystick button %d\n", ev.jbutton.button);
#endif
			{
				static int rawLogged;
				if (debug || rawLogged++ < 8) fprintf(stderr, "raw joystick %d button %d (use home_button = %d to make it HOME)\n",
				                                      (int) ev.jbutton.which, ev.jbutton.button, ev.jbutton.button);
			}
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			{
				static int padLogged;
				if (debug || padLogged++ < 8) fprintf(stderr, "controller button %s\n", SDL_GameControllerGetStringForButton(ev.cbutton.button));
			}
			break;
		case SDL_KEYDOWN:
			if (debug) fprintf(stderr, "key %s\n", SDL_GetKeyName(ev.key.keysym.sym));
			break;
		case SDL_FINGERDOWN:
		case SDL_FINGERUP:
		case SDL_FINGERMOTION:
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
			if (evTouchFd >= 0) break; /* (touch_device: read there) */
#endif
			static int touchesLogged;
			if ((debug || touchesLogged < 6) && ev.type != SDL_FINGERMOTION) {
				++touchesLogged;
				fprintf(stderr, "finger %s %.3f,%.3f window %u\n", ev.type == SDL_FINGERDOWN ? "down" : "up",
				        ev.tfinger.x, ev.tfinger.y, ev.tfinger.windowID);
			}
			queue_pointer(ev.type == SDL_FINGERDOWN ? 1 : ev.type == SDL_FINGERUP ? 2 : 3,
			              view_of_window(ev.tfinger.windowID), ev.tfinger.x, ev.tfinger.y, true);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP: {
			static int clicksLogged;
			if (clicksLogged < 6 && ev.type == SDL_MOUSEBUTTONDOWN) {
				++clicksLogged;
				fprintf(stderr, "mouse down %d,%d window %u%s\n", ev.button.x, ev.button.y, ev.button.windowID,
				        ev.button.which == SDL_TOUCH_MOUSEID ? " (from touch)" : "");
			}
		}
			if (ev.button.which == SDL_TOUCH_MOUSEID) break;
			mouseDown = ev.type == SDL_MOUSEBUTTONDOWN;
			queue_pointer(mouseDown ? 1 : 2, view_of_window(ev.button.windowID), ev.button.x, ev.button.y, false);
			break;
		case SDL_MOUSEMOTION:
			if (ev.motion.which == SDL_TOUCH_MOUSEID || !mouseDown) break;
			queue_pointer(3, view_of_window(ev.motion.windowID), ev.motion.x, ev.motion.y, false);
			break;
		}
	}
	return !uiQuit;
}

void ui_read_buttons(void) {
	memcpy(btnPrev, btnNow, sizeof(btnNow));
	memset(btnNow, 0, sizeof(btnNow));
	bool phys[SDL_CONTROLLER_BUTTON_MAX] = { 0 };
	bool l2 = false, r2 = false;
	for (int p = 0; p < 8; ++p) {
		SDL_GameController* gc = pads[p];
		if (!gc) continue;
		for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
			if (SDL_GameControllerGetButton(gc, b)) phys[b] = true;
		}
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) l2 = true;
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) r2 = true;
#ifdef __ANDROID__
		/* digital L2 / R2 keys arrive as raw joystick buttons 15 / 16, which
		 * SDL's Android controller mapping leaves out (it only maps analog
		 * trigger axes) */
		SDL_Joystick* js = SDL_GameControllerGetJoystick(gc);
		if (js && SDL_JoystickGetButton(js, 15)) l2 = true;
		if (js && SDL_JoystickGetButton(js, 16)) r2 = true;
#endif
		int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
		int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
		if (lx < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = true;
		if (lx > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = true;
		if (ly < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_UP] = true;
		if (ly > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = true;
	}
	bool rawHome = false;
	if (cfg->homeButton >= 0) {
		for (int p = 0; p < 8; ++p) {
			SDL_Joystick* j = pads[p] ? SDL_GameControllerGetJoystick(pads[p]) : rawJoys[p];
			if (j && cfg->homeButton < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, cfg->homeButton)) rawHome = true;
		}
	}
#ifdef __ANDROID__
	{
		/* GammaOS reports the face buttons by their printed (Nintendo) labels,
		 * so SDL's A is the right button and B the bottom one. Everything below
		 * works by position (as on ROCKNIX: B right, A bottom), so swap back.
		 * X (top) and Y (left) already match. */
		bool a = phys[SDL_CONTROLLER_BUTTON_A];
		phys[SDL_CONTROLLER_BUTTON_A] = phys[SDL_CONTROLLER_BUTTON_B];
		phys[SDL_CONTROLLER_BUTTON_B] = a;
	}
#endif
	/* GBA-style: A is the right face button, B the bottom one */
	btnNow[B_A] = cfg->abSwap ? phys[SDL_CONTROLLER_BUTTON_A] : phys[SDL_CONTROLLER_BUTTON_B];
	btnNow[B_B] = cfg->abSwap ? phys[SDL_CONTROLLER_BUTTON_B] : phys[SDL_CONTROLLER_BUTTON_A];
	btnNow[B_X] = phys[SDL_CONTROLLER_BUTTON_X];
	btnNow[B_Y] = phys[SDL_CONTROLLER_BUTTON_Y];
	btnNow[B_START] = phys[SDL_CONTROLLER_BUTTON_START];
	btnNow[B_SELECT] = phys[SDL_CONTROLLER_BUTTON_BACK];
	btnNow[B_HOME] = phys[SDL_CONTROLLER_BUTTON_GUIDE] || rawHome;
	btnNow[B_L] = phys[SDL_CONTROLLER_BUTTON_LEFTSHOULDER];
	btnNow[B_R] = phys[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER];
	btnNow[B_L2] = l2;
	btnNow[B_R2] = r2;
	btnNow[B_L3] = phys[SDL_CONTROLLER_BUTTON_LEFTSTICK];
	btnNow[B_UP] = phys[SDL_CONTROLLER_BUTTON_DPAD_UP];
	btnNow[B_DOWN] = phys[SDL_CONTROLLER_BUTTON_DPAD_DOWN];
	btnNow[B_LEFT] = phys[SDL_CONTROLLER_BUTTON_DPAD_LEFT];
	btnNow[B_RIGHT] = phys[SDL_CONTROLLER_BUTTON_DPAD_RIGHT];

	/* keyboard (desktop testing, and devices whose HOME key arrives as a key) */
	const Uint8* k = SDL_GetKeyboardState(NULL);
	if (k[SDL_SCANCODE_X]) btnNow[B_A] = true;
	if (k[SDL_SCANCODE_Z]) btnNow[B_B] = true;
	if (k[SDL_SCANCODE_A]) btnNow[B_L] = true;
	if (k[SDL_SCANCODE_S]) btnNow[B_R] = true;
	if (k[SDL_SCANCODE_RETURN]) btnNow[B_START] = true;
	if (k[SDL_SCANCODE_BACKSPACE] || k[SDL_SCANCODE_RSHIFT]) btnNow[B_SELECT] = true;
	if (k[SDL_SCANCODE_UP]) btnNow[B_UP] = true;
	if (k[SDL_SCANCODE_DOWN]) btnNow[B_DOWN] = true;
	if (k[SDL_SCANCODE_LEFT]) btnNow[B_LEFT] = true;
	if (k[SDL_SCANCODE_RIGHT]) btnNow[B_RIGHT] = true;
	if (k[SDL_SCANCODE_TAB]) btnNow[B_L2] = true;
	if (k[SDL_SCANCODE_SPACE]) btnNow[B_R2] = true;
	if (k[SDL_SCANCODE_Q]) btnNow[B_L3] = true;
	if (k[SDL_SCANCODE_ESCAPE] || k[SDL_SCANCODE_F1] || k[SDL_SCANCODE_HOME] || k[SDL_SCANCODE_AC_HOME] ||
	    k[SDL_SCANCODE_MENU] || k[SDL_SCANCODE_APPLICATION]) {
		btnNow[B_HOME] = true;
	}
#ifdef __ANDROID__
	if (k[SDL_SCANCODE_AC_BACK]) btnNow[B_HOME] = true; /* Android keeps HOME; BACK stands in */
#endif

	for (int i = 0; i < B_COUNT; ++i) {
		if (scriptBtns & (1u << i)) btnNow[i] = true;
	}
}

/* ------------------------------------------------------------- drawing */

/* Texture contents changed: SDL_UpdateTexture, noted so the software view
 * (below) knows its picture changed. */
#define TEXVER_N 64
static struct {
	SDL_Texture* t;
	uint32_t v;
} texVer[TEXVER_N];

static uint32_t tex_version(SDL_Texture* t) {
	for (int i = 0; i < TEXVER_N; ++i) {
		if (texVer[i].t == t) return texVer[i].v;
	}
	return 0;
}

void ui_update_texture(SDL_Texture* t, const void* px, int pitch) {
	if (!t) return;
	SDL_UpdateTexture(t, NULL, px, pitch);
	int slot = -1;
	for (int i = 0; i < TEXVER_N && slot < 0; ++i) {
		if (texVer[i].t == t) slot = i;
	}
	for (int i = 0; i < TEXVER_N && slot < 0; ++i) {
		if (!texVer[i].t) slot = i;
	}
	if (slot < 0) slot = (int) ((uintptr_t) t / 16 % TEXVER_N); /* table full: reuse a slot */
	if (texVer[slot].t != t) texVer[slot].t = t, texVer[slot].v = 0;
	++texVer[slot].v;
}

/* Retained views (Android: both screens). A view's draw calls are recorded
 * instead of drawn, and at ui_present they're replayed only if they differ
 * from that view's last frame (same calls, same texture contents), with a
 * full redraw at least once a second in case something slipped through.
 * Nothing changed: nothing is drawn or handed to the display, which keeps
 * showing the last picture. The second screen is drawn by the GPU into the
 * second display's window (android_second_begin/_end) or, failing that, by
 * a software renderer into views[1].surf. */
enum { OP_IMAGE, OP_PIXELS, OP_FILL, OP_ROUND, OP_TEXT, OP_LABEL, OP_CLIP, OP_NOCLIP };
typedef struct {
	int kind, screen;
	SDL_Texture* const* tex; /* per-view array, as passed in */
	SDL_Texture* texv;        /* this view's texture in it */
	uint32_t texVer;
	int a, b, c, d;
	SDL_Rect r;
	bool hasRect;
	SDL_Color col;
	char mode[16];
	char text[256];
} DrawOp;
#ifdef __ANDROID__
typedef struct {
	DrawOp* ops;
	int nOps, capOps;
	int state;       /* this frame: 0 not decided, 1 drawn, 2 unchanged (not drawn) */
	uint64_t lastSig;
	int sinceDraw;
	Uint64 drawTicks; /* for the log */
	int draws;
} Retained;
static Retained ret[2];
static bool replaying;

static void retained_invalidate(void) {
	for (int i = 0; i < 2; ++i) ret[i].lastSig = 0;
}
#endif

static DrawOp* rec(int screen, int kind) {
#ifndef __ANDROID__
	return NULL; /* only Android has retained views */
#else
	if (replaying || screen < 0 || screen >= nScreens || !views[screens[screen].view].retained) return NULL;
	Retained* rv = &ret[screens[screen].view];
	if (rv->nOps == rv->capOps) {
		rv->capOps = rv->capOps ? rv->capOps * 2 : 64;
		rv->ops = realloc(rv->ops, sizeof(*rv->ops) * rv->capOps);
	}
	DrawOp* o = &rv->ops[rv->nOps++];
	memset(o, 0, sizeof(*o)); /* zeroed padding: ops are hashed bytewise */
	o->kind = kind;
	o->screen = screen;
	return o;
#endif
}

static void rec_tex(DrawOp* o, SDL_Texture* const* tex) {
	o->tex = tex;
	o->texv = tex[screens[o->screen].view];
	o->texVer = tex_version(o->texv);
}

#ifdef __ANDROID__
static uint64_t ops_sig(const Retained* rv) {
	uint64_t h = 1469598103934665603ull;
	const unsigned char* p = (const unsigned char*) rv->ops;
	for (size_t i = 0; i < sizeof(*rv->ops) * rv->nOps; ++i) h = (h ^ p[i]) * 1099511628211ull;
	h = (h ^ (uint64_t) rv->nOps) * 1099511628211ull;
	h = (h ^ (uint64_t) cfg->lcdStrength) * 1099511628211ull;
	return h ^ (cfg->smooth ? 0x9e3779b97f4a7c15ull : 0);
}

/* a screenshot wants a view's pixels (only readable while it's being drawn) */
static unsigned char* swReadDst;
static SDL_Rect swReadRect;
static int swReadPitch;
static bool swReadOk;

/* Replays view v's recorded calls if they changed (or a screenshot needs
 * them). True if it now shows a new picture. Once per frame per view
 * (ui_present, or earlier for a screenshot). */
static bool sw_render(int v) {
	Retained* rv = &ret[v];
	if (rv->state) return rv->state == 1;
	uint64_t sig = ops_sig(rv);
	if (!swReadDst && sig == rv->lastSig && ++rv->sinceDraw < 60) {
		rv->state = 2;
		return false;
	}
	Uint64 t0 = SDL_GetPerformanceCounter();
	SDL_Renderer* ren = views[v].ren;
	bool second = views[v].offwin && !views[v].surf; /* the GPU second screen */
	if (second) {
		if (!android_second_begin()) {
			rv->state = 2; /* no window yet: try again next frame (lastSig unchanged) */
			return false;
		}
	} else if (!views[v].surf) {
		android_second_restore(); /* SDL's own window surface current again */
	}
	SDL_RenderSetClipRect(ren, NULL);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	replaying = true;
	for (int i = 0; i < rv->nOps; ++i) {
		DrawOp* o = &rv->ops[i];
		switch (o->kind) {
		case OP_IMAGE: ui_image(o->screen, o->tex, o->a, o->b, o->mode, o->hasRect ? &o->r : NULL); break;
		case OP_PIXELS: ui_image_pixels(o->screen, o->tex, o->a, o->b, o->mode, o->d); break;
		case OP_FILL: ui_fill(o->screen, o->r, o->col); break;
		case OP_ROUND: ui_fill_round(o->screen, o->r, o->col, o->a); break;
		case OP_TEXT: ui_text(o->screen, o->a, o->b, o->c, o->text, o->col); break;
		case OP_LABEL: ui_font_text(o->screen, o->d & 15, o->a, o->b, o->c, o->text, o->col, (o->d >> 4) - 1); break;
		case OP_CLIP: ui_clip(o->screen, &o->r); break;
		case OP_NOCLIP: ui_clip(o->screen, NULL); break;
		}
	}
	replaying = false;
	SDL_RenderSetClipRect(ren, NULL);
	if (swReadDst) swReadOk = SDL_RenderReadPixels(ren, &swReadRect, SDL_PIXELFORMAT_ABGR8888, swReadDst, swReadPitch) == 0;
	if (second) {
		SDL_RenderFlush(ren);
		android_second_end(); /* stays the current surface until the other view draws */
	} else {
		SDL_RenderPresent(ren);
	}
	rv->lastSig = sig;
	rv->sinceDraw = 0;
	rv->state = 1;
	rv->drawTicks += SDL_GetPerformanceCounter() - t0;
	++rv->draws;
	return true;
}
#endif

/* At the top of a drawing function: on a retained view, record the call
 * (the block fills in the op) and return instead of drawing. */
#define RECORD(screen, kind) DrawOp* o = rec(screen, kind); if (o)

void ui_clip(int screen, const SDL_Rect* clip) {
	RECORD(screen, clip ? OP_CLIP : OP_NOCLIP) {
		if (clip) o->r = *clip;
		return;
	}
	SDL_RenderSetClipRect(views[screens[screen].view].ren, clip);
}

void ui_clear(void) {
	for (int i = 0; i < nViews; ++i) {
		if (views[i].retained) continue; /* keeps its last picture; see sw_render */
		SDL_SetRenderDrawColor(views[i].ren, 0, 0, 0, 255);
		SDL_RenderClear(views[i].ren);
	}
#ifdef __ANDROID__
	for (int i = 0; i < 2; ++i) ret[i].nOps = 0, ret[i].state = 0;
#endif
}

static int lastRedraws;
static double lastPresentMs[2];

int ui_last_redraws(void) {
	return lastRedraws;
}

double ui_last_present_ms(int view) {
	return view >= 0 && view < 2 ? lastPresentMs[view] : 0;
}

#ifdef __ANDROID__
static int syncWant = -1, syncApplied = -2;
#endif
#ifdef __ANDROID__
static int budgetFirst = -1, deferred;
static Uint64 budgetStart;
static double budgetMs;
static int deferCount; /* for the log */
#endif
void ui_present_budget(int first, Uint64 frameStart, double ms) {
#ifdef __ANDROID__
	budgetFirst = first;
	budgetStart = frameStart;
	budgetMs = ms;
#else
	(void) first, (void) frameStart, (void) ms;
#endif
}

void ui_sync_view(int view) {
#ifdef __ANDROID__
	syncWant = view;
#else
	/* Present that view in step with its display (vsync), the others not.
	 * Applied when it changes: a call costs a renderer round trip. */
	static int applied = -2;
	if (view == applied) return;
	for (int i = 0; i < nViews; ++i) {
		if (views[i].ren) SDL_RenderSetVSync(views[i].ren, view == i ? 1 : 0);
	}
	if (applied != -2 || view >= 0) {
		fprintf(stderr, "presenting %s in step with its display\n",
		        view < 0 ? "no screen" : view == screens[0].view ? "the top screen" : "the bottom screen");
	}
	applied = view;
#endif
}

/* The refresh rate (Hz) of the display a view is on; 0 if unknown. Under
 * sway, its exact rate (outputs' current modes, in mHz; when the window
 * spans several, the slowest); otherwise SDL's, a whole number. */
double ui_display_hz(int view) {
	static double cached = -1;
	if (view < 0 || view >= nViews || !views[view].win) return 0;
	if (getenv("GBADS_DISPLAY_HZ")) return atof(getenv("GBADS_DISPLAY_HZ")); /* testing */
	double hz = 0;
#ifndef __ANDROID__
	if (cached >= 0) return cached;
#ifndef __wasi__
	if (getenv("SWAYSOCK")) {
		FILE* f = popen("swaymsg -r -t get_outputs 2>/dev/null", "r");
		if (f) {
			static char buf[65536];
			size_t n = fread(buf, 1, sizeof(buf) - 1, f);
			buf[n] = 0;
			pclose(f);
			/* each output's "current_mode": { ..., "refresh": 60000, ... } */
			for (char* p = strstr(buf, "\"current_mode\""); p; p = strstr(p + 1, "\"current_mode\"")) {
				char* r = strstr(p, "\"refresh\"");
				char* end = strchr(p, '}');
				if (!r || (end && r > end)) continue;
				r = strchr(r, ':');
				int mhz = r ? atoi(r + 1) : 0;
				fprintf(stderr, "display output: %.3f Hz\n", mhz / 1000.0);
				if (mhz > 0 && (hz == 0 || mhz / 1000.0 < hz)) hz = mhz / 1000.0;
			}
		}
	}
#endif /* __wasi__ */
#endif
	if (hz <= 0) {
		SDL_DisplayMode m;
		int d = SDL_GetWindowDisplayIndex(views[view].win);
		if (d >= 0 && SDL_GetCurrentDisplayMode(d, &m) == 0 && m.refresh_rate > 0) hz = m.refresh_rate;
	}
#ifndef __ANDROID__
	cached = hz;
#else
	(void) cached;
#endif
	return hz;
}

void ui_present(void) {
	if (pendingShot[0]) {
		ui_screenshot(pendingShot);
		pendingShot[0] = 0;
	}
#ifdef __ANDROID__
	static Uint64 handover;
	static int frames;
	lastRedraws = 0;
	if (syncWant != syncApplied) {
		/* Swap interval 1: every frame queued is shown on its own refresh, so a
		 * frame that took a bit over 16.7 ms is shown one refresh late instead
		 * of being dropped (interval 0 keeps only the latest at each refresh:
		 * a late frame meant one shown twice and the next one never). The
		 * game loop still keeps time, so this only absorbs uneven frames. */
		android_second_restore(); /* the interval applies to the current surface */
		/* Never on the second display's surface: its buffers come back too
		 * slowly on the RG DS Plus, and with interval 1 the queue filled up
		 * ("max acquired buffer count reached") and the game froze. */
		if (syncWant >= 0 && views[syncWant].offwin) syncWant = -1;
		for (int i = 0; i < nViews; ++i) {
			if (views[i].offwin) android_second_set_interval(0);
			else if (views[i].ren) SDL_RenderSetVSync(views[i].ren, syncWant == i ? 1 : 0);
		}
		fprintf(stderr, "presenting %s in step with its display\n",
		        syncWant < 0 ? "no screen" : syncWant == screens[0].view ? "the top screen" : "the bottom screen");
		syncApplied = syncWant;
	}
	int order[2] = { 0, 1 };
	if (budgetFirst == 1 && nViews == 2) order[0] = 1, order[1] = 0;
	for (int k = 0; k < nViews; ++k) {
		int i = order[k];
		Uint64 vt0 = SDL_GetPerformanceCounter();
		lastPresentMs[i] = 0;
		if (k > 0 && budgetFirst >= 0 && views[i].retained && !pendingShot[0] && deferred < 3 &&
		    (double) (vt0 - budgetStart) * 1000.0 / (double) SDL_GetPerformanceFrequency() > budgetMs &&
		    ops_sig(&ret[i]) != ret[i].lastSig) {
			/* over budget: this view's change waits a frame (lastSig unchanged, so it's redrawn next time) */
			++deferred;
			++deferCount;
			continue;
		}
		if (k > 0) deferred = 0;
		if (!views[i].retained) {
			SDL_RenderPresent(views[i].ren);
			lastRedraws |= 1 << i;
		} else if (sw_render(i) && (lastRedraws |= 1 << i, views[i].surf)) {
			Uint64 t0 = SDL_GetPerformanceCounter();
			android_second_present(views[i].surf);
			handover += SDL_GetPerformanceCounter() - t0;
		}
		lastPresentMs[i] = (double) (SDL_GetPerformanceCounter() - vt0) * 1000.0 / (double) SDL_GetPerformanceFrequency();
	}
	for (int i = 0; i < 2; ++i) ret[i].nOps = 0, ret[i].state = 0;
	if (++frames == 300) {
		double ms = 1000.0 / SDL_GetPerformanceFrequency();
		for (int i = 0; i < nViews; ++i) {
			Retained* rv = &ret[i];
			fprintf(stderr, "%s screen: redrawn %d of %d frames, %.2f ms per redraw%s\n",
			        screens[0].view == i ? "top" : "bottom", rv->draws, frames, rv->draws ? rv->drawTicks * ms / rv->draws : 0.0,
			        views[i].offwin ? (views[i].surf ? " (software)" : " (GPU, second display)") : " (GPU, game activity)");
			rv->drawTicks = 0;
			rv->draws = 0;
		}
		if (handover) fprintf(stderr, "software second screen: handing over %.2f ms per frame\n", handover * ms / frames);
		if (deferCount) fprintf(stderr, "%d redraw(s) of the other screen put off a frame (frame over budget)\n", deferCount);
		deferCount = 0;
		android_second_log_timing(frames);
		handover = 0;
		frames = 0;
	}
#else
	for (int i = 0; i < nViews; ++i) {
		Uint64 vt0 = SDL_GetPerformanceCounter();
		SDL_RenderPresent(views[i].ren);
		lastPresentMs[i] = (double) (SDL_GetPerformanceCounter() - vt0) * 1000.0 / (double) SDL_GetPerformanceFrequency();
	}
	lastRedraws = (1 << nViews) - 1;
#endif
}

SDL_Renderer* ui_ren(int screen) {
	return views[screens[screen].view].ren;
}

static void fit_rect(const SDL_Rect* area, int w, int h, const char* mode, SDL_Rect* out) {
	if (!strcmp(mode, "stretch")) {
		*out = *area;
		return;
	}
	double s = SDL_min((double) area->w / w, (double) area->h / h);
	if (!strcmp(mode, "integer") && s >= 1.0) s = (int) s;
	/* zoom: fill the height; whatever is wider than the area is cut off at
	 * the sides (ui_image_pixels clips to the screen) */
	if (!strcmp(mode, "zoom")) s = (double) area->h / h;
	out->w = (int) (w * s);
	out->h = (int) (h * s);
	out->x = area->x + (area->w - out->w) / 2;
	out->y = area->y + (area->h - out->h) / 2;
}

void ui_image(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, const SDL_Rect* areaLocal) {
	RECORD(screen, OP_IMAGE) {
		rec_tex(o, texPerView);
		o->a = w, o->b = h;
		snprintf(o->mode, sizeof(o->mode), "%s", mode);
		if (areaLocal) o->r = *areaLocal, o->hasRect = true;
		return;
	}
	Screen* s = &screens[screen];
	SDL_Rect area = areaLocal ? *areaLocal : (SDL_Rect) { 0, 0, s->rect.w, s->rect.h };
	area.x += s->rect.x;
	area.y += s->rect.y;
	SDL_Rect dst;
	fit_rect(&area, w, h, mode, &dst);
	SDL_RenderCopy(views[s->view].ren, texPerView[s->view], NULL, &dst);
}


static PipeEntry* pipe_lookup(PipeEntry* cache, int w, int h, int k, int strength, bool* hit) {
	PipeEntry* victim = &cache[0];
	for (int i = 0; i < PIPE_CACHE; ++i) {
		PipeEntry* e = &cache[i];
		if (e->tex && e->w == w && e->h == h && e->k == k && e->strength == strength) {
			e->used = ++pipeClock;
			*hit = true;
			return e;
		}
		if (!e->tex || e->used < victim->used) victim = e;
	}
	if (victim->tex) SDL_DestroyTexture(victim->tex);
	memset(victim, 0, sizeof(*victim));
	victim->w = w, victim->h = h, victim->k = k, victim->strength = strength;
	victim->used = ++pipeClock;
	*hit = false;
	return victim;
}

static SDL_Texture* pipe_target(int v, int w, int h) {
	bool hit;
	PipeEntry* e = pipe_lookup(pipeTargets[v], w, h, 0, 0, &hit);
	if (!hit) {
		e->tex = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
		if (e->tex) SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_NONE);
	}
	return e->tex;
}

/* A multiply mask: each source pixel becomes a k x k cell whose right column
 * and bottom row are darkened, like the gaps between LCD pixels. Built once. */
static SDL_Texture* pipe_grid(int v, int w, int h, int k, int strength) {
	strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
	bool hit;
	PipeEntry* e = pipe_lookup(pipeGrids[v], w, h, k, strength, &hit);
	if (hit) return e->tex;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w * k, h * k, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	Uint8 line = (Uint8) (255 * (100 - strength) / 100);
	Uint8 soft = (Uint8) (255 * (100 - strength / 3) / 100); /* top/left edge: a lighter line for depth */
	for (int y = 0; y < h * k; ++y) {
		Uint32* row = (Uint32*) ((Uint8*) s->pixels + y * s->pitch);
		int cy = y % k;
		for (int x = 0; x < w * k; ++x) {
			int cx = x % k;
			Uint8 c = 255;
			if (cx == k - 1 || cy == k - 1) c = line;
			else if (k >= 5 && (cx == 0 || cy == 0)) c = soft;
			row[x] = 0xFF000000u | (c << 16) | (c << 8) | c;
		}
	}
	e->tex = SDL_CreateTextureFromSurface(views[v].ren, s);
	SDL_FreeSurface(s);
	if (e->tex) {
		SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(e->tex, SDL_ScaleModeNearest);
	}
	return e->tex;
}

/* Total gap length in [0, t): cells of size p whose last g * p is gap. */
static double gap_upto(double t, double p, double g) {
	double n = floor(t / p), r = t - n * p, edge = (1 - g) * p;
	return n * g * p + (r > edge ? r - edge : 0);
}

/* The LCD grid for small cells (under 3 screen pixels, e.g. 2x on a 640x480
 * screen), made at the screen's own resolution: gw x gh cells over a
 * dw x dh picture, the gap between cells a quarter of a cell wide (as at 4x),
 * and each screen pixel darkened by how much of it the gap covers. Same
 * overall darkness and texture as the 4x grid, without the blur and uneven
 * lines of drawing a 3x grid and scaling it down. Built once. */
static SDL_Texture* pipe_grid_fine(int v, int dw, int dh, int gw, int gh, int strength) {
	strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
	bool hit;
	PipeEntry* e = pipe_lookup(pipeGrids[v], dw, dh, -(gw << 12 | gh), strength, &hit);
	if (hit) return e->tex;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	const double g = 0.25;
	double px = (double) dw / gw, py = (double) dh / gh;
	float* cx = malloc(sizeof(float) * (size_t) dw);
	if (!cx) {
		SDL_FreeSurface(s);
		return NULL;
	}
	for (int x = 0; x < dw; ++x) cx[x] = (float) (gap_upto(x + 1, px, g) - gap_upto(x, px, g));
	double k = strength / 100.0;
	for (int y = 0; y < dh; ++y) {
		double cy = gap_upto(y + 1, py, g) - gap_upto(y, py, g);
		Uint32* row = (Uint32*) ((Uint8*) s->pixels + y * s->pitch);
		for (int x = 0; x < dw; ++x) {
			double cover = cx[x] + cy - cx[x] * cy; /* share of the pixel in either line */
			Uint8 c = (Uint8) (255 * (1 - k * cover) + 0.5);
			row[x] = 0xFF000000u | (c << 16) | (c << 8) | c;
		}
	}
	free(cx);
	e->tex = SDL_CreateTextureFromSurface(views[v].ren, s);
	SDL_FreeSurface(s);
	if (e->tex) {
		SDL_SetTextureBlendMode(e->tex, SDL_BLENDMODE_MOD);
		SDL_SetTextureScaleMode(e->tex, SDL_ScaleModeNearest);
	}
	return e->tex;
}

static void draw_pixels(SDL_Renderer* ren, int v, SDL_Texture* src, int w, int h, const SDL_Rect* dstp, int lcd);

void ui_image_pixels(int screen, SDL_Texture* const* texPerView, int w, int h, const char* mode, int lcd) {
	RECORD(screen, OP_PIXELS) {
		rec_tex(o, texPerView);
		o->a = w, o->b = h, o->d = lcd;
		snprintf(o->mode, sizeof(o->mode), "%s", mode);
		return;
	}
	Screen* s = &screens[screen];
	int v = s->view;
	SDL_Renderer* ren = views[v].ren;
	SDL_Texture* src = texPerView[v];
	SDL_Rect dst;
	fit_rect(&s->rect, w, h, mode, &dst);
	/* zoom can be wider than the screen: keep it off the other screen */
	bool clip = dst.x < s->rect.x || dst.y < s->rect.y || dst.x + dst.w > s->rect.x + s->rect.w ||
	            dst.y + dst.h > s->rect.y + s->rect.h;
	SDL_Rect prev;
	bool hadClip = SDL_RenderIsClipEnabled(ren);
	if (hadClip) SDL_RenderGetClipRect(ren, &prev);
	if (clip) {
		SDL_Rect c = s->rect;
		if (hadClip) SDL_IntersectRect(&prev, &s->rect, &c); /* inside an existing ui_clip */
		SDL_RenderSetClipRect(ren, &c);
	}
	draw_pixels(ren, v, src, w, h, &dst, lcd);
	if (clip) SDL_RenderSetClipRect(ren, hadClip ? &prev : NULL);
}

/* ui_image_pixels' drawing, into dst on the current (screen) target. */
static void draw_pixels(SDL_Renderer* ren, int v, SDL_Texture* src, int w, int h, const SDL_Rect* dstp, int lcd) {
	SDL_Rect dst = *dstp;
	int k = SDL_max(1, SDL_min(dst.w / w, dst.h / h));
	bool exact = dst.w == w * k && dst.h == h * k;
	int c = lcd > 1 ? lcd : 1; /* source pixels per grid cell, each way */
	int gw = w / c, gh = h / c;
	if (!lcd && (exact || cfg->smooth)) {
		SDL_SetTextureScaleMode(src, cfg->smooth ? SDL_ScaleModeLinear : SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		return;
	}
	if (lcd && exact && k * c >= 3) {
		/* Pixel perfect: draw straight to the screen, grid multiplied on top.
		 * No render-target switch, so tiled GPUs don't have to flush and
		 * reload the whole framebuffer. */
		SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		SDL_Texture* grid = pipe_grid(v, gw, gh, k * c, cfg->lcdStrength);
		if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		return;
	}
	if (lcd && (dst.w < gw * 3 || dst.h < gh * 3)) {
		/* small cells (a 640x480 screen): the picture as without the grid,
		 * then a grid made at the screen's resolution on top */
		draw_pixels(ren, v, src, w, h, &dst, 0);
		SDL_Texture* grid = pipe_grid_fine(v, dst.w, dst.h, gw, gh, cfg->lcdStrength);
		if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		return;
	}
	SDL_Texture* target = SDL_RenderTargetSupported(ren) ? pipe_target(v, w * k, h * k) : NULL;
	if (!target) {
		/* no render targets: draw directly, grid on top */
		SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		if (lcd) {
			SDL_Texture* grid = pipe_grid(v, gw, gh, k * c, cfg->lcdStrength);
			if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		}
		return;
	}
	SDL_Rect clipRect;
	SDL_RenderGetClipRect(ren, &clipRect);
	bool clipped = SDL_RenderIsClipEnabled(ren);
	SDL_RenderSetClipRect(ren, NULL); /* the clip belongs to the screen, not the pipe target */
	SDL_SetRenderTarget(ren, target);
	SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
	SDL_RenderCopy(ren, src, NULL, NULL);
	if (lcd) {
		SDL_Texture* grid = pipe_grid(v, gw, gh, k * c, cfg->lcdStrength);
		if (grid) SDL_RenderCopy(ren, grid, NULL, NULL);
	}
	SDL_SetRenderTarget(ren, NULL);
	if (clipped) SDL_RenderSetClipRect(ren, &clipRect);
	SDL_SetTextureScaleMode(target, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	SDL_RenderCopy(ren, target, NULL, &dst);
}

SDL_Rect ui_image_rect(int screen, int w, int h, const char* mode) {
	SDL_Rect area = { 0, 0, screens[screen].rect.w, screens[screen].rect.h }, dst;
	fit_rect(&area, w, h, mode, &dst);
	return dst;
}

void ui_fill(int screen, SDL_Rect r, SDL_Color c) {
	RECORD(screen, OP_FILL) {
		o->r = r, o->col = c;
		return;
	}
	Screen* s = &screens[screen];
	r.x += s->rect.x;
	r.y += s->rect.y;
	SDL_Renderer* ren = views[s->view].ren;
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_RenderFillRect(ren, &r);
}

void ui_dim(int screen, Uint8 alpha) {
	ui_fill(screen, (SDL_Rect) { 0, 0, screens[screen].rect.w, screens[screen].rect.h }, (SDL_Color) { 0, 0, 0, alpha });
}

float ui_px(int screen) {
	return SDL_max(2.0f, screens[screen].rect.h / 192.0f);
}

int ui_text_width(const char* s, int px) {
	int n = (int) strlen(s);
	return n ? n * 6 * px - px : 0;
}

void ui_text(int screen, int x, int y, int px, const char* str, SDL_Color c) {
	RECORD(screen, OP_TEXT) {
		o->a = x, o->b = y, o->c = px, o->col = c;
		snprintf(o->text, sizeof(o->text), "%s", str);
		return;
	}
	Screen* s = &screens[screen];
	x += s->rect.x;
	y += s->rect.y;
	SDL_Renderer* ren = views[s->view].ren;
	SDL_SetRenderDrawColor(ren, c.r, c.g, c.b, c.a);
	SDL_Rect rects[256];
	int n = 0;
	for (; *str; ++str, x += 6 * px) {
		const uint8_t* g = font_glyph(*str);
		for (int row = 0; row < 7; ++row) {
			for (int col = 0; col < 5; ++col) {
				if ((g[row] >> (4 - col)) & 1) {
					rects[n++] = (SDL_Rect) { x + col * px, y + row * px, px, px };
					if (n == 256) {
						SDL_RenderFillRects(ren, rects, n);
						n = 0;
					}
				}
			}
		}
	}
	if (n) SDL_RenderFillRects(ren, rects, n);
}

void ui_text_center(int screen, int cx, int y, int px, const char* s, SDL_Color c) {
	ui_text(screen, cx - ui_text_width(s, px) / 2, y, px, s, c);
}

SDL_Surface* image_load(const char* path); /* image.c */

SDL_Surface* image_load_mem(const unsigned char* data, size_t size); /* image.c */

static bool textures_from(SDL_Surface* s, SDL_Texture* out[2], int* w, int* h, bool smooth) {
	out[0] = out[1] = NULL;
	if (!s) return false;
	*w = s->w;
	*h = s->h;
	for (int v = 0; v < nViews; ++v) {
		out[v] = SDL_CreateTextureFromSurface(views[v].ren, s);
		if (out[v]) SDL_SetTextureBlendMode(out[v], SDL_BLENDMODE_BLEND);
		if (out[v] && smooth) SDL_SetTextureScaleMode(out[v], SDL_ScaleModeLinear);
	}
	SDL_FreeSurface(s);
	return true;
}

bool ui_load_image(const char* path, SDL_Texture* out[2], int* w, int* h, bool smooth) {
	return textures_from(image_load(path), out, w, h, smooth);
}

bool ui_load_brand(int id, SDL_Texture* out[2], int* w, int* h) {
	return textures_from(image_load_mem(brandImages[id].png, brandImages[id].size), out, w, h, true);
}

void ui_free_image(SDL_Texture* tex[2]) {
	for (int v = 0; v < 2; ++v) {
		if (tex[v]) SDL_DestroyTexture(tex[v]);
		tex[v] = NULL;
	}
}

static bool file_there(const char* p) {
	FILE* f = fopen(p, "rb");
	if (f) fclose(f);
	return f != NULL;
}

SDL_Surface* ui_logo_surface(const Config* c, const char* code, bool* pixelArt) {
	char path[1200];
	const char* exts[] = { "png", "bmp" };
	for (int i = 0; i < 2; ++i) {
		snprintf(path, sizeof(path), "%s/logos/%.4s.%s", c->assetDir, code, exts[i]);
		if (file_there(path)) {
			*pixelArt = false;
			return image_load(path);
		}
	}
	const MapProfile* prof = profile_find(code);
	if (prof && prof->logo.rev) snprintf(path, sizeof(path), "%s/logos/%.4s-r%d.png", c->stateDir, code, prof->logo.rev);
	else snprintf(path, sizeof(path), "%s/logos/%.4s.png", c->stateDir, code);
	*pixelArt = true;
	return image_load(path);
}

bool ui_load_logo(const Config* c, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h,
                  bool* pixelArt) {
	char path[1200];
	/* a file the user supplied wins */
	const char* exts[] = { "png", "bmp" };
	for (int i = 0; i < 2; ++i) {
		snprintf(path, sizeof(path), "%s/logos/%.4s.%s", c->assetDir, code, exts[i]);
		if (file_there(path)) {
			*pixelArt = false;
			return ui_load_image(path, tex, w, h, true);
		}
	}
	/* otherwise the one extracted from the game's own title screen */
	snprintf(path, sizeof(path), "%s/logos", c->stateDir);
	mkdirs(path);
	const MapProfile* prof = profile_find(code);
	if (prof && prof->logo.rev) {
		snprintf(path, sizeof(path), "%s/logos/%.4s-r%d.png", c->stateDir, code, prof->logo.rev);
	} else {
		snprintf(path, sizeof(path), "%s/logos/%.4s.png", c->stateDir, code);
	}
	*pixelArt = true;
	if (ui_load_image(path, tex, w, h, false)) return true;
	if (rom) logo_request(rom, code, path);
	return false;
}

void ui_screenshot(const char* path) {
#ifdef __ANDROID__
	android_second_restore();
#endif
	View* v = &views[0];
	int w, h;
	SDL_GetRendererOutputSize(v->ren, &w, &h);
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
	if (SDL_RenderReadPixels(v->ren, NULL, SDL_PIXELFORMAT_ARGB8888, s->pixels, s->pitch) == 0) {
		SDL_SaveBMP(s, path);
		fprintf(stderr, "screenshot %dx%d -> %s\n", w, h, path);
	} else {
		fprintf(stderr, "screenshot failed: %s\n", SDL_GetError());
	}
	SDL_FreeSurface(s);
}

/* stb_image_write is compiled in logo.c */
int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

bool ui_screenshot_stacked(const char* pngPath) {
	int W = 0, H = 0;
	for (int i = 0; i < nScreens; ++i) {
		W = SDL_max(W, screens[i].rect.w);
		H += screens[i].rect.h;
	}
	if (!W || !H) return false;
	unsigned char* img = calloc((size_t) W * H, 4);
	if (!img) return false;
	bool ok = true;
	int y = 0;
	for (int i = 0; i < nScreens && ok; ++i) {
		SDL_Rect r = screens[i].rect;
		/* ABGR8888 is R, G, B, A in memory: what the PNG writer wants */
		unsigned char* dst = img + (size_t) y * W * 4 + (size_t) (W - r.w) / 2 * 4;
#ifdef __ANDROID__
		if (views[screens[i].view].retained) {
			/* drawn now (otherwise at ui_present), read back while it's drawn */
			swReadDst = dst, swReadRect = r, swReadPitch = W * 4, swReadOk = false;
			sw_render(screens[i].view);
			ok = swReadOk;
			swReadDst = NULL;
			y += r.h;
			continue;
		}
#endif
		ok = SDL_RenderReadPixels(views[screens[i].view].ren, &r, SDL_PIXELFORMAT_ABGR8888, dst, W * 4) == 0;
		y += r.h;
	}
	for (size_t p = 0; ok && p < (size_t) W * H; ++p) img[p * 4 + 3] = 255;
	if (ok) ok = stbi_write_png(pngPath, W, H, 4, img, W * 4) != 0;
	fprintf(stderr, "screenshot %dx%d -> %s: %s\n", W, H, pngPath, ok ? "ok" : SDL_GetError());
	free(img);
	return ok;
}

void ui_osd(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(osdText, sizeof(osdText), fmt, ap);
	va_end(ap);
	osdUntil = ticks + 120;
}

const char* ui_osd_text(void) {
	return ticks < osdUntil ? osdText : NULL;
}

/* ------------------------------------------------------------ rounded boxes, Futura labels */

#define DISC 64
static SDL_Texture* discTex[2]; /* white anti-aliased disc, per view */

static SDL_Texture* disc_texture(int view) {
	if (discTex[view]) return discTex[view];
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, DISC, DISC, 32, SDL_PIXELFORMAT_RGBA32);
	if (!s) return NULL;
	for (int y = 0; y < DISC; ++y) {
		for (int x = 0; x < DISC; ++x) {
			/* 4x4 supersampled coverage */
			int in = 0;
			for (int sy = 0; sy < 4; ++sy) {
				for (int sx = 0; sx < 4; ++sx) {
					float dx = x + (sx + 0.5f) / 4 - DISC / 2.0f, dy = y + (sy + 0.5f) / 4 - DISC / 2.0f;
					in += dx * dx + dy * dy <= (DISC / 2.0f) * (DISC / 2.0f);
				}
			}
			((Uint32*) s->pixels)[y * (s->pitch / 4) + x] = SDL_MapRGBA(s->format, 255, 255, 255, (Uint8) (in * 255 / 16));
		}
	}
	discTex[view] = SDL_CreateTextureFromSurface(views[view].ren, s);
	SDL_FreeSurface(s);
	if (discTex[view]) {
		SDL_SetTextureBlendMode(discTex[view], SDL_BLENDMODE_BLEND);
		SDL_SetTextureScaleMode(discTex[view], SDL_ScaleModeLinear);
	}
	return discTex[view];
}

void ui_fill_round(int screen, SDL_Rect r, SDL_Color c, int radius) {
	RECORD(screen, OP_ROUND) {
		o->r = r, o->col = c, o->a = radius;
		return;
	}
	radius = SDL_min(radius, SDL_min(r.w, r.h) / 2);
	if (radius <= 0) {
		ui_fill(screen, r, c);
		return;
	}
	/* the middle as three non-overlapping rects (so translucent fills stay even) */
	ui_fill(screen, (SDL_Rect) { r.x + radius, r.y, r.w - 2 * radius, r.h }, c);
	ui_fill(screen, (SDL_Rect) { r.x, r.y + radius, radius, r.h - 2 * radius }, c);
	ui_fill(screen, (SDL_Rect) { r.x + r.w - radius, r.y + radius, radius, r.h - 2 * radius }, c);
	Screen* s = &screens[screen];
	SDL_Texture* t = disc_texture(s->view);
	if (!t) return;
	SDL_SetTextureColorMod(t, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(t, c.a);
	int h = DISC / 2;
	SDL_Rect src[4] = { { 0, 0, h, h }, { h, 0, h, h }, { 0, h, h, h }, { h, h, h, h } };
	int ox = s->rect.x + r.x, oy = s->rect.y + r.y;
	SDL_Rect dst[4] = { { ox, oy, radius, radius }, { ox + r.w - radius, oy, radius, radius },
		                { ox, oy + r.h - radius, radius, radius }, { ox + r.w - radius, oy + r.h - radius, radius, radius } };
	for (int i = 0; i < 4; ++i) SDL_RenderCopy(views[s->view].ren, t, &src[i], &dst[i]);
}

/* ---------------------------------------------------------- text in Jost
 * Rasterized by textfont.c, kept as white textures (tinted when drawn), per
 * view; the least recently drawn are dropped when the cache is full. */
typedef struct {
	char* text;
	int view, font, cap;
	SDL_Texture* tex; /* NULL: nothing to draw (empty, or rendering failed) */
	int w, h, baseline, left;
	uint64_t used;
} TextEntry;
#define TEXT_CACHE 192
static TextEntry textCache[TEXT_CACHE];
static uint64_t textUse;

static TextEntry* text_entry(int view, int font, int cap, const char* text) {
	TextEntry* slot = NULL;
	for (int i = 0; i < TEXT_CACHE; ++i) {
		TextEntry* e = &textCache[i];
		if (e->text && e->view == view && e->font == font && e->cap == cap && !strcmp(e->text, text)) {
			e->used = ++textUse;
			return e;
		}
		if (!slot || !e->text || (slot->text && e->used < slot->used)) slot = e;
	}
	if (slot->text) {
		if (slot->tex) SDL_DestroyTexture(slot->tex);
		free(slot->text);
	}
	memset(slot, 0, sizeof(*slot));
	slot->text = strdup(text);
	slot->view = view, slot->font = font, slot->cap = cap;
	slot->used = ++textUse;
	TfBitmap b;
	if (slot->text && tf_render(font, cap, text, &b)) {
		SDL_Surface* surf = SDL_CreateRGBSurfaceWithFormat(0, b.w, b.h, 32, SDL_PIXELFORMAT_ARGB8888);
		if (surf) {
			for (int y = 0; y < b.h; ++y) {
				Uint32* row = (Uint32*) ((Uint8*) surf->pixels + y * surf->pitch);
				for (int x = 0; x < b.w; ++x) row[x] = ((Uint32) b.alpha[y * b.w + x] << 24) | 0xFFFFFFu;
			}
			slot->tex = SDL_CreateTextureFromSurface(views[view].ren, surf);
			SDL_FreeSurface(surf);
			if (slot->tex) SDL_SetTextureBlendMode(slot->tex, SDL_BLENDMODE_BLEND);
			slot->w = b.w, slot->h = b.h, slot->baseline = b.baseline, slot->left = b.left;
		}
		tf_free(&b);
	}
	return slot;
}

/* Text widths, remembered: measuring Jost text (kerning for every pair of
 * letters) costs real time on the handhelds' small cores, and the same
 * strings are measured every frame (centred labels, wrapped text, the
 * achievement popups fitting their lines). Direct-mapped by hash. */
#define WIDTH_CACHE 1024
static struct {
	uint64_t hash;
	char* text;
	int font, cap, w;
} widthCache[WIDTH_CACHE];

static int text_width(int font, int capPx, const char* text) {
	uint64_t h = 1469598103934665603ull ^ (uint64_t) (font * 131 + capPx);
	for (const unsigned char* p = (const unsigned char*) text; *p; ++p) h = (h ^ *p) * 1099511628211ull;
	unsigned slot = (unsigned) (h ^ (h >> 32)) % WIDTH_CACHE;
	if (widthCache[slot].text && widthCache[slot].hash == h && widthCache[slot].font == font &&
	    widthCache[slot].cap == capPx && !strcmp(widthCache[slot].text, text)) {
		return widthCache[slot].w;
	}
	int w = tf_width(font, capPx, text);
	char* copy = strdup(text);
	if (copy) {
		free(widthCache[slot].text);
		widthCache[slot].hash = h, widthCache[slot].text = copy;
		widthCache[slot].font = font, widthCache[slot].cap = capPx, widthCache[slot].w = w;
	}
	return w;
}

int ui_font_width(int font, int capPx, const char* text) {
	return text_width(font, capPx, text);
}

void ui_font_text(int screen, int font, int x, int cy, int capPx, const char* text, SDL_Color c, int align) {
	if (!text || !text[0] || capPx <= 0) return;
	RECORD(screen, OP_LABEL) {
		o->a = x, o->b = cy, o->c = capPx, o->d = font | ((align + 1) << 4), o->col = c;
		snprintf(o->text, sizeof(o->text), "%s", text);
		return;
	}
	Screen* s = &screens[screen];
	TextEntry* e = text_entry(s->view, font, capPx, text);
	if (!e->tex) return;
	int w = align < 0 ? 0 : text_width(font, capPx, text);
	int left = align < 0 ? x : align > 0 ? x - w : x - w / 2;
	/* capitals centred on cy */
	SDL_Rect dst = { s->rect.x + left - e->left, s->rect.y + cy + capPx / 2 - e->baseline, e->w, e->h };
	SDL_SetTextureColorMod(e->tex, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(e->tex, c.a);
	SDL_RenderCopy(views[s->view].ren, e->tex, NULL, &dst);
}

void ui_title_block(int screen, SDL_Rect area, const char* text, int capMax, SDL_Color c) {
	if (!text || !text[0] || area.w <= 0 || area.h <= 0) return;
	/* one line if it fits at a good size, else the break that leaves the
	 * longer line shortest ("Tactics Ogre: / The Knight of Lodis") */
	char one[256], two[2][256];
	snprintf(one, sizeof(one), "%s", text);
	int best = -1, bestW = 0;
	for (int i = 1; one[i]; ++i) {
		if (one[i] != ' ') continue;
		snprintf(two[0], sizeof(two[0]), "%.*s", i, one);
		int w = SDL_max(ui_font_width(TF_BOLD, 100, two[0]), ui_font_width(TF_BOLD, 100, one + i + 1));
		if (best < 0 || w < bestW) best = i, bestW = w;
	}
	/* a subtitle after a colon goes on its own line */
	const char* colon = strstr(one, ": ");
	if (colon) {
		best = (int) (colon - one) + 1;
		snprintf(two[0], sizeof(two[0]), "%.*s", best, one);
		bestW = SDL_max(ui_font_width(TF_BOLD, 100, two[0]), ui_font_width(TF_BOLD, 100, one + best + 1));
	}
	int oneW = ui_font_width(TF_BOLD, 100, one);
	/* capitals at most capMax, a line's width at most the area's; two lines
	 * need 3.2 capitals of height */
	int capOne = SDL_min(capMax, SDL_min(area.w * 100 / SDL_max(1, oneW), area.h * 10 / 18));
	int capTwo = best < 0 ? 0 : SDL_min(capMax, SDL_min(area.w * 100 / SDL_max(1, bestW), area.h * 10 / 32));
	int cx = area.x + area.w / 2, cy = area.y + area.h / 2;
	if (capOne >= capTwo * 9 / 10 || best < 0) {
		ui_font_text(screen, TF_BOLD, cx, cy, capOne, one, c, 0);
		return;
	}
	snprintf(two[0], sizeof(two[0]), "%.*s", best, one);
	snprintf(two[1], sizeof(two[1]), "%s", one + best + 1);
	int gap = capTwo * 8 / 10;
	ui_font_text(screen, TF_BOLD, cx, cy - (capTwo + gap) / 2, capTwo, two[0], c, 0);
	ui_font_text(screen, TF_BOLD, cx, cy + (capTwo + gap) / 2, capTwo, two[1], c, 0);
}

int ui_label_width(int screen, int capPx, const char* text) {
	(void) screen;
	return text_width(TF_BOLD, capPx, text);
}

void ui_label(int screen, int cx, int cy, int capPx, const char* text, SDL_Color c) {
	ui_font_text(screen, TF_BOLD, cx, cy, capPx, text, c, 0);
}

int ui_font_wrap(int screen, int font, int x, int y, int width, int capPx, const char* text, SDL_Color c) {
	int lineH = capPx * 2;
	char line[512];
	const char* p = text;
	while (*p) {
		/* as many words as fit */
		int n = 0, fit = 0;
		while (p[n]) {
			int e = n;
			while (p[e] && p[e] != ' ') ++e;
			if (e >= (int) sizeof(line)) break;
			memcpy(line, p, e);
			line[e] = 0;
			if (fit && text_width(font, capPx, line) > width) break;
			fit = e;
			n = e;
			while (p[n] == ' ') ++n;
		}
		if (!fit) fit = n ? n : (int) strlen(p);
		snprintf(line, sizeof(line), "%.*s", fit, p);
		ui_font_text(screen, font, x, y + capPx / 2, capPx, line, c, -1);
		y += lineH;
		p += fit;
		while (*p == ' ') ++p;
	}
	return y;
}

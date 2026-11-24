#include <limits.h>
#include "ui.h"

/* the left stick presses the D-pad (issue #57): game.c turns it off for a game
 * with left_stick_dpad = 0, and on again for its menus and the game list */
static bool stickDpad = true;
void ui_stick_dpad(bool on) {
	stickDpad = on;
}
#include "android.h"
#include "brand.h"
#include "font.h"
#include "textfont.h"
#include "fonts_data.h"
#include "logo.h"
#include "paths.h"
#include "profiles.h"
#include "emu.h"
#include "i18n.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <strings.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>

View views[2];
int nViews;
Screen screens[2];
int nScreens;
bool btnNow[B_COUNT], btnPrev[B_COUNT];
volatile int uiQuit;

static Config* cfg;
static uint64_t ticks;
static SDL_GameController* pads[8];
#ifdef __ANDROID__
/* The GPU second screen shares the game activity's renderer, so its size
 * can't come from SDL: the second display's own (it can differ from the
 * game activity's: Retroid Pocket Duo Lite, 1920x1080 and 1280x960). */
static int secondW, secondH, gameW, gameH;
#endif

/* A view's drawing size. */
static void view_size(int v, int* w, int* h) {
#ifdef __ANDROID__
	if (views[v].offwin && !views[v].surf && secondW > 0) {
		*w = secondW, *h = secondH;
		return;
	}
#endif
	SDL_GetRendererOutputSize(views[v].ren, w, h);
}
static bool padUserMapped[8]; /* its mapping came from the button mapper (controllers.txt) */
static char userGuids[16][40]; /* the controllers.txt GUIDs */
static int nUserGuids;
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
#ifdef __wasi__
/* The web player's buttons, held now: bit i is button i of ui.h's list
 * (B_A, B_B, B_X...), whatever key or controller the player mapped to it */
static uint32_t webBtns;
__attribute__((export_name("web_buttons"))) void web_buttons(uint32_t held) {
	webBtns = held;
}
#endif
static long scriptTapUp = -1;
static float tapX, tapY;
/* SWIPE:x0,y0,x1,y1 (fractions of the window): down, 4 moves a tick apart, up */
static float swipe[4];
static int swipeStep = -1;
/* DRAG:x,y puts a finger down and leaves it there, MOVE:x,y moves it, LIFT lifts it (WarioWare: Twisted!'s dial) */
static float dragX, dragY;
static bool dragDown;
static char pendingShot[160];
static char pendingLoad[160]; /* LOAD:file in an input script (regression tests) */

static void script_joy(const char* a);
static void script_virtual_pad(void);

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
		if (!strncmp(what, "TAP:", 4) || !strncmp(what, "SWIPE:", 6) || !strncmp(what, "DRAG:", 5) || !strncmp(what, "MOVE:", 5) || !strcmp(what, "LIFT") || !strncmp(what, "SHOT:", 5) || !strncmp(what, "LOAD:", 5) || !strcmp(what, "QUIT") ||
		    !strncmp(what, "JOY", 3)) {
			snprintf(s->action, sizeof(s->action), "%s", what);
		} else {
			s->btns = parse_btns(what);
		}
	}
	fclose(f);
	script_virtual_pad();
}

/* GBADS_DRAW_SHOTS_ONLY (tools/regress.py): nothing needs drawing this
 * frame, as an input script runs with no screenshot due and no touch,
 * swipe, load or joystick line within 30 ticks (the quick bar's layout and
 * hit areas are worked out as it's drawn). The game, map and HUD
 * pictures are still made every frame; only the screens aren't put
 * together. */
bool ui_scripted(void) { return scriptLen > 0; }

bool ui_script_quiet(void) {
	static int on = -1;
	if (on < 0) on = getenv("GBADS_DRAW_SHOTS_ONLY") != NULL;
	if (!on || !scriptLen || pendingShot[0] || swipeStep >= 0 || scriptTapUp >= 0) return false;
	for (int i = 0; i < scriptLen; ++i) {
		if (!script[i].action[0] || !strncmp(script[i].action, "SHOT:", 5) || !strcmp(script[i].action, "QUIT")) continue;
		long d = script[i].tick - (long) ticks;
		if (d > -30 && d < 30) return false;
	}
	return true;
}

const char* ui_script_load(void) {
	static char path[160];
	if (!pendingLoad[0]) return NULL;
	snprintf(path, sizeof(path), "%s", pendingLoad);
	pendingLoad[0] = 0;
	return path;
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
		} else if (!strncmp(s->action, "SWIPE:", 6)) {
			if (sscanf(s->action + 6, "%f,%f,%f,%f", &swipe[0], &swipe[1], &swipe[2], &swipe[3]) == 4) {
				push_finger(SDL_FINGERDOWN, swipe[0], swipe[1]);
				swipeStep = 0;
			}
		} else if (!strncmp(s->action, "DRAG:", 5)) {
			if (sscanf(s->action + 5, "%f,%f", &dragX, &dragY) == 2) push_finger(SDL_FINGERDOWN, dragX, dragY), dragDown = true;
		} else if (!strncmp(s->action, "MOVE:", 5)) {
			if (dragDown && sscanf(s->action + 5, "%f,%f", &dragX, &dragY) == 2) push_finger(SDL_FINGERMOTION, dragX, dragY);
		} else if (!strcmp(s->action, "LIFT")) {
			if (dragDown) push_finger(SDL_FINGERUP, dragX, dragY);
			dragDown = false;
		} else if (!strncmp(s->action, "SHOT:", 5)) {
			snprintf(pendingShot, sizeof(pendingShot), "%s", s->action + 5);
		} else if (!strncmp(s->action, "LOAD:", 5)) {
			snprintf(pendingLoad, sizeof(pendingLoad), "%s", s->action + 5);
		} else if (!strcmp(s->action, "QUIT")) {
			uiQuit = 1;
		} else if (!strncmp(s->action, "JOY", 3)) {
			script_joy(s->action);
		} else {
			scriptBtns = s->btns;
		}
	}
	if (swipeStep >= 0) {
		float t = ++swipeStep / 4.0f;
		if (t > 1) t = 1;
		float x = swipe[0] + (swipe[2] - swipe[0]) * t, y = swipe[1] + (swipe[3] - swipe[1]) * t;
		push_finger(swipeStep <= 4 ? SDL_FINGERMOTION : SDL_FINGERUP, x, y);
		if (swipeStep > 4) swipeStep = -1;
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

static void open_pads(void);

/* ------------------------------------------------------- raw input */

/* The button mapper's view of the controllers: raw buttons, hats and axes
 * (and keys, which it can't map), queued while capturing. */
static bool rawCapture;
static RawInput rawQ[64];
static int rawHead, rawTail;

void ui_raw_capture(bool on) {
	rawCapture = on;
	rawHead = rawTail = 0;
}

static void raw_push(int kind, SDL_JoystickID which, int index, int value) {
	if (!rawCapture || (rawTail + 1) % 64 == rawHead) return;
	rawQ[rawTail] = (RawInput) { kind, which, index, value };
	rawTail = (rawTail + 1) % 64;
}

bool ui_next_raw(RawInput* r) {
	if (rawHead == rawTail) return false;
	*r = rawQ[rawHead];
	rawHead = (rawHead + 1) % 64;
	return true;
}

void ui_mapping_file(char* out, size_t n) {
	snprintf(out, n, "%s/controllers.txt", cfg->stateDir);
}

void ui_mapped_by_user(const char* guid) {
	for (int i = 0; i < nUserGuids; ++i) {
		if (!strcmp(userGuids[i], guid)) return;
	}
	if (nUserGuids < 16) snprintf(userGuids[nUserGuids++], sizeof(userGuids[0]), "%s", guid);
}

/* The GUIDs that controllers.txt has mappings for. */
static void load_user_guids(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[1024];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#' || line[0] == '\n' || line[0] == '\r') continue;
		line[strcspn(line, ",")] = 0;
		if (line[0]) ui_mapped_by_user(line);
	}
	fclose(f);
}

static void update_user_mapped(void) {
	for (int i = 0; i < 8; ++i) {
		padUserMapped[i] = false;
		if (!pads[i]) continue;
		char guid[40];
		SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(SDL_GameControllerGetJoystick(pads[i])), guid, sizeof(guid));
		for (int k = 0; k < nUserGuids; ++k) {
			if (!strcmp(userGuids[k], guid)) padUserMapped[i] = true;
		}
	}
}

void ui_reopen_pads(void) {
	open_pads();
	update_user_mapped(); /* (an open controller's mapping changes in place) */
}

/* Input scripts can press a virtual controller's raw inputs (testing the
 * mapper): JOYBTN:n, JOYHAT:v, JOYAXIS:n,v; JOYBTN:- lets go of them all. */
static SDL_Joystick* scriptPad;
static Sint16 scriptPadRest[6];
static void script_virtual_pad(void) {
	bool wanted = false;
	for (int i = 0; i < scriptLen; ++i) wanted |= !strncmp(script[i].action, "JOY", 3);
	if (!wanted) return;
	int idx = SDL_JoystickAttachVirtual(SDL_JOYSTICK_TYPE_GAMECONTROLLER, 6, 20, 1);
	if (idx >= 0) scriptPad = SDL_JoystickOpen(idx);
	SDL_JoystickUpdate();
	for (int i = 0; scriptPad && i < 6; ++i) scriptPadRest[i] = SDL_JoystickGetAxis(scriptPad, i);
	if (!scriptPad) fprintf(stderr, "no virtual controller: %s\n", SDL_GetError());
	open_pads();
}

static void script_joy(const char* a) {
	if (!scriptPad) return;
	int n, v;
	if (!strcmp(a, "JOYBTN:-")) {
		for (int i = 0; i < 20; ++i) SDL_JoystickSetVirtualButton(scriptPad, i, 0);
		SDL_JoystickSetVirtualHat(scriptPad, 0, SDL_HAT_CENTERED);
		for (int i = 0; i < 6; ++i) SDL_JoystickSetVirtualAxis(scriptPad, i, scriptPadRest[i]);
	} else if (sscanf(a, "JOYBTN:%d", &n) == 1) {
		SDL_JoystickSetVirtualButton(scriptPad, n, 1);
	} else if (sscanf(a, "JOYHAT:%d", &v) == 1) {
		SDL_JoystickSetVirtualHat(scriptPad, 0, (Uint8) v);
	} else if (sscanf(a, "JOYAXIS:%d,%d", &n, &v) == 2) {
		SDL_JoystickSetVirtualAxis(scriptPad, n, (Sint16) v);
	}
}

/* Rumble (issue #83). The motor is the controller's own when it has one (an external pad), else the
 * device's (the RG DS Plus's vibration motor shows up as a force-feedback input device of its own,
 * which SDL lists as a haptic device). ui_rumble(level) is called every frame with how hard the
 * game wants it (0..1), times the player's strength; it only talks to the motor when the level
 * moves a step or the last request is about to run out, so it isn't an ioctl a frame. */
static SDL_Haptic* rumbleHaptic;
static bool rumbleHapticTried;
static float rumbleSent;     /* the level the motor was last asked for */
static Uint32 rumbleSentAt;  /* when (SDL_GetTicks) */
static bool rumbleOnPad;     /* the last request went to a controller */

static void rumble_haptic_open(void) {
	if (rumbleHapticTried) return;
	rumbleHapticTried = true;
#ifndef __wasi__
	if (SDL_InitSubSystem(SDL_INIT_HAPTIC) != 0) {
		fprintf(stderr, "rumble: no haptic support (%s)\n", SDL_GetError());
		return;
	}
	int n = SDL_NumHaptics();
	for (int i = 0; i < n; ++i) {
		SDL_Haptic* h = SDL_HapticOpen(i);
		if (!h) {
			fprintf(stderr, "rumble: haptic %d (%s): can't open (%s)\n", i, SDL_HapticName(i), SDL_GetError());
			continue;
		}
		bool ok = SDL_HapticRumbleSupported(h) && SDL_HapticRumbleInit(h) == 0;
		fprintf(stderr, "rumble: haptic %d (%s): %s\n", i, SDL_HapticName(i), ok ? "usable" : "no rumble");
		if (ok && !rumbleHaptic) rumbleHaptic = h;
		else SDL_HapticClose(h);
	}
	if (!rumbleHaptic) fprintf(stderr, "rumble: no haptic motor found (%d device%s)\n", n, n == 1 ? "" : "s");
#endif
}

/* One request to whichever motor there is, for ms. */
static bool rumble_send(float level, Uint32 ms) {
	bool onPad = false;
#ifndef __wasi__
	if (level > 0) {
		Uint16 v = (Uint16) (level * 65535.0f);
		for (int i = 0; i < 8; ++i)
			if (pads[i] && SDL_GameControllerRumble(pads[i], v, v, ms) == 0) onPad = true;
	} else {
		for (int i = 0; i < 8; ++i) if (pads[i] && rumbleOnPad) SDL_GameControllerRumble(pads[i], 0, 0, 0);
	}
	if (!onPad) {
		rumble_haptic_open();
		if (rumbleHaptic) {
			if (level > 0) SDL_HapticRumblePlay(rumbleHaptic, level, ms);
			else SDL_HapticRumbleStop(rumbleHaptic);
		}
	} else if (rumbleHaptic) {
		SDL_HapticRumbleStop(rumbleHaptic);
	}
#else
	(void) level, (void) ms;
#endif
	rumbleOnPad = onPad;
	return onPad;
}

void ui_rumble(float level) {
	if (level < 0) level = 0;
	if (level > 1) level = 1;
	level = floorf(level * 20.0f + 0.5f) / 20.0f; /* steps of 5 % */
	Uint32 now = SDL_GetTicks();
	if ((int) (now - rumbleSentAt) < 0) return; /* a pulse is still running */
	if (level == rumbleSent && (level == 0 || now - rumbleSentAt < 60)) return;
	rumbleSent = level;
	rumbleSentAt = now;
	if (level > 0 || rumbleOnPad || rumbleHaptic) rumble_send(level, 120); /* (each request runs out by itself if we stop asking) */
}

void ui_rumble_pulse(float level, int ms) {
	rumble_send(level, (Uint32) ms);
	rumbleSent = level;
	rumbleSentAt = SDL_GetTicks() + (Uint32) ms; /* (the next ui_rumble doesn't cut it short) */
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
		} else if (!pads[i] && rawJoys[i] && SDL_IsGameController(i)) {
			/* mapped since (the button mapper): a controller now */
			SDL_JoystickClose(rawJoys[i]);
			rawJoys[i] = NULL;
			pads[i] = SDL_GameControllerOpen(i);
			if (pads[i]) fprintf(stderr, "controller: %s (mapped)\n", SDL_GameControllerName(pads[i]));
		} else if (!pads[i] && !rawJoys[i]) {
			rawJoys[i] = SDL_JoystickOpen(i);
			if (rawJoys[i]) fprintf(stderr, "joystick without mapping: %s\n", SDL_JoystickName(rawJoys[i]));
		}
	}
	update_user_mapped();
}

bool ui_init(Config* c, bool wantDual) {
	cfg = c;
	debug = getenv("GBADS_DEBUG") != NULL;
	SDL_SetHint(SDL_HINT_APP_NAME, "GBA DS");
	SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, cfg->smooth ? "linear" : "nearest");
#ifdef __ANDROID__
	SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft");
	SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1"); /* BACK opens the menu instead of closing the app */
#else
	/* SDL drops controller events while the window isn't focused. Under sway
	 * with both panels (stock ROCKNIX on the RG DS, issue #45) our window may
	 * never get focus, and a tap doesn't give it any: keep the buttons
	 * coming regardless. The environment variable still overrides this. */
	SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
#endif
	setenv("SDL_VIDEO_WAYLAND_WMCLASS", "gba-ds", 0);
	/* (the web build's sound too: web/sdl_audio_web.c, pulled by the page) */
	const Uint32 audioInit = SDL_INIT_AUDIO;
	if (SDL_Init(SDL_INIT_VIDEO | audioInit | SDL_INIT_GAMECONTROLLER | SDL_INIT_EVENTS) != 0) {
		fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
		return false;
	}
	SDL_ShowCursor(SDL_DISABLE);
	SDL_GameControllerAddMappingsFromFile("gamecontrollerdb.txt");
	if (getenv("SDL_GAMECONTROLLERCONFIG_FILE")) {
		SDL_GameControllerAddMappingsFromFile(getenv("SDL_GAMECONTROLLERCONFIG_FILE"));
	}
	/* mappings made with the button mapper on this device (they win) */
	char mapFile[600];
	ui_mapping_file(mapFile, sizeof(mapFile));
	if (SDL_GameControllerAddMappingsFromFile(mapFile) > 0) fprintf(stderr, "button mappings: %s\n", mapFile);
	load_user_guids(mapFile);
	open_pads();

#ifdef __ANDROID__
	/* SDL only has the display the activity is on. The other one is a
	 * Presentation window in Java (android.c). It's view 1, drawn at
	 * ui_present: with the GPU (the game screen's renderer, made to draw into
	 * the other window), or failing that with a software renderer. */
	/* (GBADS_WINDOW: tools/androidtest, a window of that size, as SDL's
	 * offscreen driver makes a full-screen one 1024x768) */
	if (!create_view(&views[0], 0, "GBA DS", !getenv("GBADS_WINDOW"), 1024, 768)) return false;
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
		secondW = sw, secondH = sh, gameW = w0, gameH = h0;
		fprintf(stderr, "second screen: GPU, %dx%d (game screen %dx%d)\n", sw, sh, w0, h0);
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

static void text_cache_drop(void);
static void disc_drop(void);

/* After a wake (or a renderer reset): the textures ui.c keeps for itself
 * (the scaling pipeline's, the text images, the disc) are let go, to be
 * made again as they're next needed. */
void ui_textures_reset(void) {
	for (int v = 0; v < 2; ++v) {
		for (int i = 0; i < PIPE_CACHE; ++i) {
			if (pipeTargets[v][i].tex) SDL_DestroyTexture(pipeTargets[v][i].tex);
			if (pipeGrids[v][i].tex) SDL_DestroyTexture(pipeGrids[v][i].tex);
			memset(&pipeTargets[v][i], 0, sizeof(pipeTargets[v][i]));
			memset(&pipeGrids[v][i], 0, sizeof(pipeGrids[v][i]));
		}
	}
	text_cache_drop();
	disc_drop();
}

void ui_shutdown(void) {
	ui_rumble(0);
	if (rumbleHaptic) SDL_HapticClose(rumbleHaptic), rumbleHaptic = NULL;
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

static void layout_screens(void);

void ui_layout(void) {
	layout_screens();
	ui_layout_pictures();
}

bool ui_screens_differ(void) {
	if (nScreens < 2) return false;
	double a0 = (double) screens[0].rect.w / SDL_max(1, screens[0].rect.h);
	double a1 = (double) screens[1].rect.w / SDL_max(1, screens[1].rect.h);
	return fabs(a0 - a1) > 0.02 * SDL_max(a0, a1);
}

/* Dual-screen handhelds whose screens' sizes we know (docs/devices.csv): the
 * top and bottom screens' resolutions (either way round) and diagonals in
 * inches, for their pixels per inch, which what Android reports doesn't
 * always match. Two with the same resolutions (the Retroid Pocket Duo and
 * Duo Lite): the one nearer what the system reports, else the first. */
static const struct {
	const char* name;
	int tw, th; float tin;
	int bw, bh; float bin;
} knownDevices[] = {
	{ "AYN Thor", 1920, 1080, 6.0f, 1240, 1080, 3.92f },
	{ "Retroid Pocket Duo", 1920, 1080, 5.5f, 1280, 960, 4.5f },
	{ "Retroid Pocket Duo Lite", 1920, 1080, 5.5f, 1280, 960, 4.2f },
	{ "AYANEO Pocket DS", 1920, 1080, 7.0f, 1024, 768, 5.0f },
	{ "Anbernic RG DDS", 1200, 720, 4.7f, 960, 720, 4.0f },
	{ "Anbernic RG DS Plus", 1024, 768, 4.3f, 1024, 768, 4.3f },
	{ "Anbernic RG DS", 640, 480, 4.0f, 640, 480, 4.0f },
};

static bool same_res(int w, int h, int kw, int kh) {
	return (w == kw && h == kh) || (w == kh && h == kw);
}

/* the system's own figure: Android's DisplayMetrics, or SDL's with a window per display */
static float reported_ppi(int screen) {
	int v = screens[screen].view;
	float ppi = 0;
#ifdef __ANDROID__
	ppi = android_display_ppi(v);
#else
	if (nViews == 2) {
		float d = 0;
		int di = SDL_GetWindowDisplayIndex(views[v].win);
		if (di >= 0 && SDL_GetDisplayDPI(di, &d, NULL, NULL) == 0) ppi = d;
	}
#endif
	return ppi >= 80 && ppi <= 1000 ? ppi : 0; /* (anything else is a made-up value) */
}

float ui_screen_ppi(int screen) {
	if (screen < 0 || screen >= nScreens) return 0;
	float top = 0, bottom = 0;
	if (cfg->screenPpi[0] && sscanf(cfg->screenPpi, "%f,%f", &top, &bottom) == 2 && top > 0 && bottom > 0) return screen ? bottom : top;
	if (nScreens == 2) {
		const SDL_Rect* t = &screens[0].rect;
		const SDL_Rect* b = &screens[1].rect;
		int pick = -1;
		float best = 1e9f, rb = reported_ppi(1);
		for (int i = 0; i < (int) SDL_arraysize(knownDevices); ++i) {
			if (!same_res(t->w, t->h, knownDevices[i].tw, knownDevices[i].th) || !same_res(b->w, b->h, knownDevices[i].bw, knownDevices[i].bh)) continue;
			float bp = hypotf(knownDevices[i].bw, knownDevices[i].bh) / knownDevices[i].bin;
			float d = rb > 0 ? fabsf(bp - rb) : (pick < 0 ? 0 : 1e9f);
			if (d < best) best = d, pick = i;
		}
		if (pick >= 0) {
			static int logged = -1;
			if (logged != pick) logged = pick, fprintf(stderr, "screens: known device, %s\n", knownDevices[pick].name);
			return screen ? hypotf(knownDevices[pick].bw, knownDevices[pick].bh) / knownDevices[pick].bin
			              : hypotf(knownDevices[pick].tw, knownDevices[pick].th) / knownDevices[pick].tin;
		}
	}
	return reported_ppi(screen);
}

/* screen_match (Display: screen shapes; experimental): the wider screen's
 * pictures keep to the other screen's shape (aspect: the largest such area,
 * centred), or also its real size (size: by the screens' pixels per inch,
 * against the edge nearest the other screen: a top screen's bottom), so a
 * picture that goes on over both (Pokémon Pinball's table) carries on
 * across. Nothing when the shapes already match. */
void ui_layout_pictures(void) {
	/* screen_placement = no_gap (issue #46): with two screens, the top one's
	 * pictures against its bottom edge and the bottom one's against its
	 * top, so the two are as close together as they can be */
	bool gap = !strcmp(cfg->screenPlace, "no_gap") && nScreens == 2;
	for (int i = 0; i < nScreens; ++i) {
		screens[i].pic = (SDL_Rect) { 0, 0, screens[i].rect.w, screens[i].rect.h };
		screens[i].align = gap ? (i == 0 ? 1 : -1) : 0;
	}
	bool aspect = !strcmp(cfg->screenMatch, "aspect"), size = !strcmp(cfg->screenMatch, "size");
	static char logged[96];
	char note[64] = "";
	if ((aspect || size) && ui_screens_differ()) {
		double a0 = (double) screens[0].rect.w / screens[0].rect.h, a1 = (double) screens[1].rect.w / screens[1].rect.h;
		int wide = a0 > a1 ? 0 : 1, narrow = 1 - wide;
		const SDL_Rect* W = &screens[wide].rect;
		const SDL_Rect* N = &screens[narrow].rect;
		double a = (double) N->w / N->h;
		int w = W->h * a + 0.5, h = W->h;
		float pw = ui_screen_ppi(wide), pn = ui_screen_ppi(narrow);
		if (size && pw > 0 && pn > 0) w = N->w * pw / pn + 0.5, h = N->h * pw / pn + 0.5;
		else if (size) snprintf(note, sizeof(note), " (sizes not known: shapes only)");
		if (w > W->w) h = h * W->w / w, w = W->w;
		if (h > W->h) w = w * W->h / h, h = W->h;
		int y = size ? (wide == 0 ? W->h - h : 0) : (W->h - h) / 2;
		screens[wide].pic = (SDL_Rect) { (W->w - w) / 2, y, w, h };
		snprintf(note + strlen(note), sizeof(note) - strlen(note), " %dx%d on screen %d", w, h, wide);
	}
	char line[96];
	snprintf(line, sizeof(line), "%s%s, placement %s", cfg->screenMatch, note, gap ? "no_gap" : "center");
	if (strcmp(line, logged)) {
		snprintf(logged, sizeof(logged), "%s", line);
		fprintf(stderr, "screen shapes: %s (ppi %.0f / %.0f)\n", line, ui_screen_ppi(0), ui_screen_ppi(1));
	}
}

static void layout_screens(void) {
	if (nViews == 2) {
		nScreens = 2;
#ifdef __ANDROID__
		/* screens in physical order as on ROCKNIX (0 top: the game; 1 bottom:
		 * the menus). Android doesn't say where its displays are: with two
		 * of a size, the activity's (view 0, display 0) is the bottom one,
		 * as on the RG DS; else the larger one is on top, as on the AYN Thor
		 * (1920x1080 over 1240x1080) and other clamshells. top_screen = app
		 * / second says which instead. */
		int w0, h0, w1, h1;
		view_size(0, &w0, &h0);
		view_size(1, &w1, &h1);
		bool appOnTop = !strcmp(cfg->topScreen, "app") ? true
		                : !strcmp(cfg->topScreen, "second") ? false
		                : (long) w0 * h0 > (long) w1 * h1 * 11 / 10;
		static int logged = -1;
		if (logged != appOnTop) {
			logged = appOnTop;
			fprintf(stderr, "screens: %s on top (%dx%d over %dx%d; top_screen = %s)\n", appOnTop ? "the app's display" : "the second display",
			        appOnTop ? w0 : w1, appOnTop ? h0 : h1, appOnTop ? w1 : w0, appOnTop ? h1 : h0, cfg->topScreen);
		}
#endif
		for (int i = 0; i < 2; ++i) {
			int w, h;
			view_size(i, &w, &h);
#ifdef __ANDROID__
			screens[appOnTop ? i : 1 - i] = (Screen) { i, { 0, 0, w, h } };
#else
			screens[i] = (Screen) { i, { 0, 0, w, h } };
#endif
		}
		return;
	}
	int w, h;
	SDL_GetRendererOutputSize(views[0].ren, &w, &h);
	/* tests: two screens of other sizes stacked in the window (the top one
	 * first), as on devices whose screens differ: GBADS_SCREENS=1920x1080,1240x1080
	 * (with GBADS_WINDOW the larger width x the heights together) */
	int tw0, th0, tw1, th1;
	if (getenv("GBADS_SCREENS") && sscanf(getenv("GBADS_SCREENS"), "%dx%d,%dx%d", &tw0, &th0, &tw1, &th1) == 4) {
		nScreens = 2;
		screens[0] = (Screen) { 0, { (w - tw0) / 2, 0, tw0, th0 } };
		screens[1] = (Screen) { 0, { (w - tw1) / 2, th0, tw1, th1 } };
		return;
	}
	const char* lay = cfg->layout;
	/* a hinge across the middle of one display (Surface Duo spanned over
	 * both screens, issue #48): a screen each side of it, the hinge's
	 * pixels left out */
	int gap = 0;
#ifdef __ANDROID__
	gap = android_hinge_gap(w, h);
#endif
	if (!strcmp(lay, "auto") || !strcmp(lay, "dual")) {
		if (gap > 0) lay = h > w ? "span-v" : "span-h";
		else lay = w >= h * 18 / 10 ? "span-h" : h >= w * 12 / 10 ? "span-v" : "single";
	}
	static char logged[48];
	char line[48];
	snprintf(line, sizeof(line), "%dx%d %s gap %d", w, h, lay, gap);
	if (strcmp(line, logged)) snprintf(logged, sizeof(logged), "%s", line), fprintf(stderr, "screens: one window, %s\n", line);
	if (!strcmp(lay, "span-h")) {
		nScreens = 2;
		int s = (w - gap) / 2;
		screens[0] = (Screen) { 0, { 0, 0, s, h } };
		screens[1] = (Screen) { 0, { s + gap, 0, w - s - gap, h } };
	} else if (!strcmp(lay, "span-v")) {
		nScreens = 2;
		int s = (h - gap) / 2;
		screens[0] = (Screen) { 0, { 0, 0, w, s } };
		screens[1] = (Screen) { 0, { 0, s + gap, w, h - s - gap } };
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
		/* touch panel coordinates cover just the touch screen (a finger on
		 * its right or bottom edge comes as 1.0, or a little past 0-1 as it
		 * slides off: kept on the screen, so a lift there isn't lost) */
		p.screen = ts;
		p.x = SDL_clamp((int) (px * screens[ts].rect.w), 0, screens[ts].rect.w - 1);
		p.y = SDL_clamp((int) (py * screens[ts].rect.h), 0, screens[ts].rect.h - 1);
	} else {
		if (view < 0) view = screens[ts].view;
		int w, h;
		view_size(view, &w, &h);
		int x, y;
		if (normalized) {
			/* (kept in the view: 1.0 at the right or bottom edge, a little
			 * past 0-1 sliding off; dropped, a lift there would be lost) */
			x = SDL_clamp((int) (px * w), 0, w - 1);
			y = SDL_clamp((int) (py * h), 0, h - 1);
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
static int evVolFd = -2;   /* the volume keys' device, the same way */
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
		/* ours alone while GBA DS runs (fails if another program holds it) */
		int grab = ioctl(evTouchFd, EVIOCGRAB, 1);
		char name[64] = "?";
		ioctl(evTouchFd, EVIOCGNAME(sizeof(name)), name);
		fprintf(stderr, "touch device %s (%s): x %d..%d, y %d..%d, turned %d degrees%s\n", cfg->touchDevice, name, evAx[0].min,
		        evAx[0].max, evAx[1].min, evAx[1].max, cfg->touchRotate, grab < 0 ? ", held by another program" : "");
	}
	if (evTouchFd < 0) return;
	struct input_event ie[64];
	ssize_t n;
	while ((n = read(evTouchFd, ie, sizeof(ie))) > 0) {
		for (int i = 0; i < (int) (n / sizeof(ie[0])); ++i) {
			const struct input_event* e = &ie[i];
			static int rawEv;
			if (debug && rawEv < 24 && e->type != EV_SYN) {
				++rawEv;
				fprintf(stderr, "touch device event: type %d code %d value %d\n", e->type, e->code, e->value);
			}
			if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_X || e->code == ABS_X)) evX = e->value, evMoved = true;
			else if (e->type == EV_ABS && (e->code == ABS_MT_POSITION_Y || e->code == ABS_Y)) evY = e->value, evMoved = true;
			else if (e->type == EV_KEY && e->code == BTN_TOUCH) evDown = e->value != 0;
			else if (e->type == EV_ABS && e->code == ABS_MT_TRACKING_ID) evDown = e->value >= 0;
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

/* GBA DS's own volume, where the system leaves the volume keys to the
 * program running (launch.sh sets GBADS_VOLUME to the file that keeps it:
 * the RG DS Plus's Stock Linux OS, whose ports play straight to the sound card).
 * The keys come as key events, from their input device, or as raw joystick
 * buttons (GBADS_VOLUME_BUTTONS=up,down). */
static int softVolume = -1; /* 0-100; -1: not read yet */

static const char* volume_file(void) {
	return getenv("GBADS_VOLUME");
}

int ui_volume(void) {
	if (softVolume < 0) {
		softVolume = 100;
		FILE* f = volume_file() ? fopen(volume_file(), "r") : NULL;
		if (f) {
			int v;
			if (fscanf(f, "%d", &v) == 1) softVolume = SDL_clamp(v, 0, 100);
			fclose(f);
		}
	}
	return softVolume;
}

static void volume_key(bool up) {
	if (!volume_file()) return;
	softVolume = SDL_clamp(ui_volume() + (up ? 5 : -5), 0, 100);
	FILE* f = fopen(volume_file(), "w");
	if (f) {
		fprintf(f, "%d\n", softVolume);
		fclose(f);
	}
	static int volLogged;
	if (debug || volLogged++ < 8) fprintf(stderr, "volume %s: %d%%\n", up ? "up" : "down", softVolume);
	ui_osd("Volume %d%%", softVolume);
}

static void volume_button(int button) {
	static int vb[2] = { -2, -2 };
	if (vb[0] == -2) {
		vb[0] = vb[1] = -1;
		const char* e = getenv("GBADS_VOLUME_BUTTONS");
		if (e) sscanf(e, "%d,%d", &vb[0], &vb[1]);
	}
	if (button >= 0 && button == vb[0]) volume_key(true);
	else if (button >= 0 && button == vb[1]) volume_key(false);
}

#define EVBIT(b, n) ((b)[(n) / 8] & (1 << ((n) % 8)))
static void evdev_volume_poll(void) {
	if (evVolFd == -2) {
		evVolFd = -1;
		if (!volume_file()) return;
		for (int i = 0; i < 32 && evVolFd < 0; ++i) {
			char path[32];
			snprintf(path, sizeof(path), "/dev/input/event%d", i);
			int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (fd < 0) continue;
			unsigned char keys[KEY_MAX / 8 + 1] = { 0 };
			unsigned char abs[ABS_MAX / 8 + 1] = { 0 };
			ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys);
			ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs)), abs);
			bool other = false, anyAbs = false;
			for (int k = 0; k < KEY_MAX; ++k) {
				if (EVBIT(keys, k) && k != KEY_VOLUMEUP && k != KEY_VOLUMEDOWN && k != KEY_MUTE) other = true;
			}
			for (int a = 0; a < ABS_MAX; ++a) anyAbs |= EVBIT(abs, a) != 0;
			if (!EVBIT(keys, KEY_VOLUMEUP) || anyAbs) {
				close(fd);
				continue;
			}
			char name[64] = "?";
			ioctl(fd, EVIOCGNAME(sizeof(name)), name);
			int grab = other ? -1 : ioctl(fd, EVIOCGRAB, 1);
			fprintf(stderr, "volume keys: %s (%s)%s\n", path, name, grab == 0 ? ", ours alone" : other ? ", shared (it has other keys)" : ", shared (held by another program)");
			evVolFd = fd;
		}
		if (evVolFd < 0) fprintf(stderr, "volume keys: no input device has them\n");
	}
	if (evVolFd < 0) return;
	struct input_event ie[16];
	ssize_t n;
	while ((n = read(evVolFd, ie, sizeof(ie))) > 0) {
		for (int i = 0; i < (int) (n / sizeof(ie[0])); ++i) {
			/* presses and auto-repeats */
			if (ie[i].type == EV_KEY && ie[i].value && (ie[i].code == KEY_VOLUMEUP || ie[i].code == KEY_VOLUMEDOWN)) {
				volume_key(ie[i].code == KEY_VOLUMEUP);
			}
		}
	}
}
#else
static void evdev_touch_poll(void) {}
static void evdev_volume_poll(void) {}
static void volume_key(bool up) { (void) up; }
int ui_volume(void) { return 100; }
static void volume_button(int button) { (void) button; }
static const int evVolFd = -1;
#endif

static bool renderReset; /* the renderer lost its textures (SDL told us) */

bool ui_render_reset(void) {
	bool r = renderReset;
	renderReset = false;
	return r;
}

bool ui_events(void) {
	evdev_touch_poll();
	evdev_volume_poll();
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
		case SDL_RENDER_DEVICE_RESET:
		case SDL_RENDER_TARGETS_RESET:
			fprintf(stderr, "the renderer was reset (%s)\n", ev.type == SDL_RENDER_DEVICE_RESET ? "device" : "targets");
			renderReset = true;
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
		case SDL_JOYHATMOTION:
			if (ev.jhat.value != SDL_HAT_CENTERED) raw_push('h', ev.jhat.which, ev.jhat.hat, ev.jhat.value);
			break;
		case SDL_JOYAXISMOTION:
			raw_push('a', ev.jaxis.which, ev.jaxis.axis, ev.jaxis.value);
			break;
		case SDL_JOYBUTTONDOWN:
			raw_push('b', ev.jbutton.which, ev.jbutton.button, 1);
#ifdef __ANDROID__
			/* buttons outside the standard set (15 L2, 16 R2, 17 C, 18 Z...) */
			if (!debug && ev.jbutton.button >= 15) fprintf(stderr, "raw joystick button %d\n", ev.jbutton.button);
#endif
			{
				static int rawLogged;
				if (debug || rawLogged++ < 8) fprintf(stderr, "raw joystick %d button %d (use home_button = %d to make it HOME)\n",
				                                      (int) ev.jbutton.which, ev.jbutton.button, ev.jbutton.button);
			}
			volume_button(ev.jbutton.button);
			break;
		case SDL_CONTROLLERBUTTONDOWN:
			{
				static int padLogged;
				if (debug || padLogged++ < 8) fprintf(stderr, "controller button %s\n", SDL_GameControllerGetStringForButton(ev.cbutton.button));
			}
			break;
		case SDL_KEYDOWN: {
			if (!ev.key.repeat) raw_push('k', -1, ev.key.keysym.scancode, 1);
			static int keysLogged;
			if (debug || keysLogged++ < 6) fprintf(stderr, "key %s\n", SDL_GetKeyName(ev.key.keysym.sym));
#if defined(__linux__) && !defined(__ANDROID__) && !defined(__wasi__)
			/* the volume keys, where the system leaves them to us */
			SDL_Scancode sc = ev.key.keysym.scancode;
			if ((sc == SDL_SCANCODE_VOLUMEUP || sc == SDL_SCANCODE_VOLUMEDOWN) && evVolFd < 0) volume_key(sc == SDL_SCANCODE_VOLUMEUP);
#endif
			break;
		}
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
		bool pb[SDL_CONTROLLER_BUTTON_MAX];
		for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) pb[b] = SDL_GameControllerGetButton(gc, b);
#ifdef __ANDROID__
		if (!padUserMapped[p]) {
			/* GammaOS reports the face buttons by their printed (Nintendo)
			 * labels, so SDL's A is the right button and B the bottom one.
			 * Everything below works by position (as on ROCKNIX: B right, A
			 * bottom), so swap back. X (top) and Y (left) already match. A
			 * mapping made with the button mapper is by position already. */
			bool a = pb[SDL_CONTROLLER_BUTTON_A];
			pb[SDL_CONTROLLER_BUTTON_A] = pb[SDL_CONTROLLER_BUTTON_B];
			pb[SDL_CONTROLLER_BUTTON_B] = a;
		}
#endif
		for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
			if (pb[b]) phys[b] = true;
		}
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERLEFT) > 16000) l2 = true;
		if (SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16000) r2 = true;
#ifdef __ANDROID__
		/* digital L2 / R2 keys arrive as raw joystick buttons 15 / 16, which
		 * SDL's Android controller mapping leaves out (it only maps analog
		 * trigger axes) */
		SDL_Joystick* js = padUserMapped[p] ? NULL : SDL_GameControllerGetJoystick(gc); /* (mapped: as the mapping says) */
		if (js && SDL_JoystickGetButton(js, 15)) l2 = true;
		if (js && SDL_JoystickGetButton(js, 16)) r2 = true;
#endif
		int lx = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTX);
		int ly = SDL_GameControllerGetAxis(gc, SDL_CONTROLLER_AXIS_LEFTY);
		if (stickDpad) { /* (left_stick_dpad: off in a game, the stick does nothing there) */
			if (lx < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = true;
			if (lx > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = true;
			if (ly < -16000) phys[SDL_CONTROLLER_BUTTON_DPAD_UP] = true;
			if (ly > 16000) phys[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = true;
		}
	}
	bool rawHome = false;
	if (cfg->homeButton >= 0) {
		for (int p = 0; p < 8; ++p) {
			SDL_Joystick* j = pads[p] ? SDL_GameControllerGetJoystick(pads[p]) : rawJoys[p];
			if (j && cfg->homeButton < SDL_JoystickNumButtons(j) && SDL_JoystickGetButton(j, cfg->homeButton)) rawHome = true;
		}
	}
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
#ifdef __wasi__
		if (webBtns & (1u << i)) btnNow[i] = true;
#endif
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

/* Colour correction: mGBA's GBA Color shader (res/shaders/gba-color.shader,
 * by Pokefan531 and hunterk), as mGBA runs it by default (color_mode 1,
 * sRGB; darken_screen 0.5): each channel to the power 2.5 (target gamma 2.0
 * + 0.5), times the luminance 0.93, clamped, through the colour matrix
 * (GLSL's mat4 is filled column by column: these are its rows), to the
 * power 1 / 2.0 (display gamma). The GBA has 32768 colours: a table, by the
 * 5-bit colour each 8-bit one came from (mGBA: c << 3 | c >> 2). */
static uint32_t* ccLut;
static bool ccOn;              /* pictures go through ccLut */
static int ccWant;             /* Display > Color Correction: 0 off, 1 GBA, 2 Game Boy Color */
static int ccGain = 100;       /* brightening, in percent (the subpixel shaders) */
static int lutCc = 0;           /* what ccLut holds */
static int lutGain = -1;
static void cc_build(int mode, int gain) {
	bool cc = mode != 0;
	static const double gbaM[3][3] = {
		{ 0.80, 0.275, -0.075 },
		{ 0.135, 0.64, 0.225 },
		{ 0.195, 0.155, 0.65 },
	};
	/* A Game Boy Color's screen (GB-8): our own first guess, not copied from anywhere: its colours
	 * wash out and bleed into each other more than the GBA's (each row adds to 1.0, so white stays
	 * white), through gamma 2.2 both ways, no darkening */
	static const double gbcM[3][3] = {
		{ 0.78, 0.18, 0.04 },
		{ 0.10, 0.74, 0.16 },
		{ 0.16, 0.16, 0.68 },
	};
	const double (*m)[3] = mode == 2 ? gbcM : gbaM;
	double gam = mode == 2 ? 2.2 : 2.0, lum = mode == 2 ? 1.0 : 0.93, dark = mode == 2 ? 0.0 : 0.5;
	if (!ccLut) ccLut = malloc(32768 * sizeof(uint32_t));
	if (!ccLut) return;
	for (int c = 0; c < 32768; ++c) {
		double in[3];
		for (int k = 0; k < 3; ++k) {
			int v5 = c >> (k * 5) & 31;
			double v = ((v5 << 3) | (v5 >> 2)) / 255.0;
			if (cc) v = pow(v, gam + dark) * lum;
			in[k] = v < 0 ? 0 : v > 1 ? 1 : v;
		}
		double o[3], top = 0;
		for (int k = 0; k < 3; ++k) {
			double v = cc ? m[k][0] * in[0] + m[k][1] * in[1] + m[k][2] * in[2] : in[k];
			v = v < 0 ? 0 : v > 1 ? 1 : v;
			o[k] = cc ? pow(v, 1.0 / gam) : v;
			if (o[k] > top) top = o[k];
		}
		/* brightened (the shader's stripes darken it again), all three
		 * channels alike so the colour keeps its hue: as far as the gain,
		 * or as far as the brightest channel can go */
		double f = gain / 100.0;
		if (top > 0 && f * top > 1) f = SDL_max(1.0, 1 / top);
		uint32_t out = 0;
		for (int k = 0; k < 3; ++k) {
			int b = (int) (o[k] * f * 255.0 + 0.5);
			out |= (uint32_t) (b > 255 ? 255 : b) << (k * 8);
		}
		ccLut[c] = out;
	}
	lutCc = mode, lutGain = gain;
}
static void cc_refresh(void) {
	bool want = ccWant != 0 || ccGain != 100;
	if (want && (lutCc != ccWant || lutGain != ccGain || !ccLut)) cc_build(ccWant, ccGain);
	ccOn = want && ccLut;
}
void ui_color_correction(int mode) {
	ccWant = mode;
	cc_refresh();
}

/* Display > Shader (config.h SH_*): the pattern draw_pixels lays over the
 * pictures, and how much the colour table brightens them first. A pattern
 * can only darken (it's multiplied in), so the subpixel stripes, which take
 * away the most, get the colours brightened back (each keeping its hue,
 * so the brightest can't be brightened much, and look a little darker
 * under the stripes than without them). */
static int uiShader = SH_OFF;
static const struct {
	float other, gap, row; /* a stripe's other two colours, the gap column, the bottom row */
	int gain;
} subPresets[3] = {
	{ 0.80f, 0.80f, 0.92f, 112 }, /* light: for dim screens (about 0.9 of the picture's brightness) */
	{ 0.62f, 0.65f, 0.85f, 125 }, /* (about 0.8) */
	{ 0.45f, 0.50f, 0.75f, 140 }, /* strong: for bright screens (about 0.7) */
};
void ui_shader(int sh) {
	uiShader = sh >= 0 && sh < SH_COUNT ? sh : SH_OFF;
	ccGain = uiShader >= SH_SUB_LIGHT ? subPresets[uiShader - SH_SUB_LIGHT].gain : 100;
	cc_refresh();
}
uint32_t ui_color_correct(uint32_t p) {
	if (!ccOn) return p;
	unsigned key = (p >> 3 & 31) | (p >> 11 & 31) << 5 | (p >> 19 & 31) << 10;
	return ccLut[key] | (p & 0xFF000000u);
}

static void update_texture(SDL_Texture* t, const void* px, int pitch, bool correct);
void ui_update_texture(SDL_Texture* t, const void* px, int pitch) {
	update_texture(t, px, pitch, true);
}
/* the same for the interface's own pictures: no colour correction (it's the game's) */
void ui_update_texture_plain(SDL_Texture* t, const void* px, int pitch) {
	update_texture(t, px, pitch, false);
}
static void update_texture(SDL_Texture* t, const void* px, int pitch, bool correct) {
	if (!t) return;
	if (ccOn && correct) {
		/* (through the colour correction's table, alpha kept) */
		static uint32_t* buf;
		static size_t bufN;
		int w, h;
		if (SDL_QueryTexture(t, NULL, NULL, &w, &h) == 0 && pitch >= w * 4) {
			size_t n = (size_t) (pitch / 4) * h;
			if (n > bufN) {
				uint32_t* nb = realloc(buf, n * sizeof(uint32_t));
				if (nb) buf = nb, bufN = n;
			}
			if (n <= bufN) {
				const uint32_t* src = px;
				for (size_t i = 0; i < n; ++i) {
					uint32_t p = src[i];
					buf[i] = ccLut[(p >> 3 & 31) | (p >> 11 & 31) << 5 | (p >> 19 & 31) << 10] | (p & 0xFF000000u);
				}
				px = buf;
			}
		}
	}
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

static void font_text_raw(int screen, int font, int x, int cy, int capPx, const char* text, SDL_Color c, int align);

/* Retained views (Android: both screens). A view's draw calls are recorded
 * instead of drawn, and at ui_present they're replayed only if they differ
 * from that view's last frame (same calls, same texture contents), with a
 * full redraw at least once a second in case something slipped through.
 * Nothing changed: nothing is drawn or handed to the display, which keeps
 * showing the last picture. The second screen is drawn by the GPU into the
 * second display's window (android_second_begin/_end) or, failing that, by
 * a software renderer into views[1].surf. */
enum { OP_IMAGE, OP_PIXELS, OP_FILL, OP_ROUND, OP_TEXT, OP_LABEL, OP_CLIP, OP_NOCLIP, OP_ROUND_TOP };
static void fill_round_top(int screen, SDL_Rect r, SDL_Color c, int radius);
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
	h = (h ^ (uint64_t) cfg->shader) * 1099511628211ull;
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
	int vpY = 0; /* the second screen's viewport's top, in the game window's coordinates */
	if (second) {
		if (!android_second_begin()) {
			rv->state = 2; /* no window yet: try again next frame (lastSig unchanged) */
			return false;
		}
		/* the second display's size, at the bottom left of the surface: SDL
		 * flips viewport (and clip) y against the game window's height, so
		 * this is GL's (0, 0, secondW, secondH) there */
		int ww = gameW, wh = gameH;
		SDL_GetRendererOutputSize(ren, &ww, &wh); /* (the game window's, now) */
		vpY = wh - secondH;
		SDL_Rect vp = { 0, vpY, secondW, secondH };
		SDL_RenderSetViewport(ren, &vp);
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
		case OP_ROUND_TOP: fill_round_top(o->screen, o->r, o->col, o->a); break;
		case OP_TEXT: ui_text(o->screen, o->a, o->b, o->c, o->text, o->col); break;
		case OP_LABEL: font_text_raw(o->screen, o->d & 15, o->a, o->b, o->c, o->text, o->col, (o->d >> 4) - 1); break;
		case OP_CLIP: ui_clip(o->screen, &o->r); break;
		case OP_NOCLIP: ui_clip(o->screen, NULL); break;
		}
	}
	replaying = false;
	SDL_RenderSetClipRect(ren, NULL);
	if (swReadDst) {
		/* (SDL takes this rect in the window's coordinates, not the viewport's) */
		SDL_Rect rr = swReadRect;
		rr.y += vpY;
		swReadOk = SDL_RenderReadPixels(ren, &rr, SDL_PIXELFORMAT_ABGR8888, swReadDst, swReadPitch) == 0;
	}
	if (second) {
		SDL_RenderFlush(ren);
		android_second_end(); /* stays the current surface until the other view draws */
		SDL_RenderSetViewport(ren, NULL); /* (queued: applies on the next draw, the game screen's) */
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

static int keyCount[2];
static bool inKey;
static bool dimmed[2]; /* this frame's brightness layer is on (a present without a redraw must not add it again) */
static void key_layer(int screen, SDL_Rect r, SDL_Color c);
void ui_clear(void) {
	keyCount[0] = keyCount[1] = 0;
	dimmed[0] = dimmed[1] = false;
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

/* This app's brightness for each screen (issue #56), 20..100: a black layer over
 * everything at the end of the frame (not in screenshots), never the system's
 * own brightness. */
static int brightPct[2] = { 100, 100 };
void ui_brightness(int top, int bottom) {
	brightPct[0] = SDL_clamp(top, 20, 100);
	brightPct[1] = SDL_clamp(bottom, 20, 100);
}

void ui_present(void) {
	{
		/* testing (issue #50): GBADS_TEST_PRESENT_STALL=MS,EVERY holds every
		 * EVERY-th present up for MS ms, as one player's system did */
		static int stallMs = -1, stallEvery, presents;
		if (stallMs < 0) {
			const char* e = getenv("GBADS_TEST_PRESENT_STALL");
			stallMs = 0;
			if (e && sscanf(e, "%d,%d", &stallMs, &stallEvery) != 2) stallMs = 0;
		}
		if (stallMs > 0 && stallEvery > 0 && ++presents % stallEvery == 0) SDL_Delay((Uint32) stallMs);
	}
	if (pendingShot[0]) {
		ui_screenshot(pendingShot);
		pendingShot[0] = 0;
	}
	for (int s = 0; s < nScreens && s < 2; ++s) {
		if (brightPct[s] < 100 && !dimmed[s]) dimmed[s] = true, ui_dim(s, (Uint8) (255 * (100 - brightPct[s]) / 100));
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

/* align: where a picture shorter than the area goes (Screen.align) */
static void fit_rect_at(const SDL_Rect* area, int w, int h, const char* mode, int align, SDL_Rect* out) {
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
	out->y = area->y + (align > 0 ? area->h - out->h : align < 0 ? 0 : (area->h - out->h) / 2);
}
static void fit_rect(const SDL_Rect* area, int w, int h, const char* mode, SDL_Rect* out) {
	fit_rect_at(area, w, h, mode, 0, out);
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

/* One of the other shaders' patterns at a point in a cell (u, v in 0..1,
 * across and down), as a multiplier for red, green and blue. */
static void pattern_at(int sh, double u, double v, double k, float out[3]) {
	out[0] = out[1] = out[2] = 1;
	if (sh == SH_SCANLINES) {
		/* the bottom quarter of each row of pixels, darker than the grid's
		 * lines (there are half as many), as mGBA's Scanlines at its 0.5 */
		if (v >= 0.75) out[0] = out[1] = out[2] = (float) (1 - SDL_min(1.0, k * 1.4));
	} else if (sh == SH_SOFT) {
		/* darker toward the cell's edges, most at the corners: zfast_lcd's
		 * falloff (RetroArch's handheld/zfast-lcd) */
		double d = 2 * SDL_max(fabs(u - 0.5), fabs(v - 0.5));
		out[0] = out[1] = out[2] = (float) (1 - SDL_min(1.0, k * 1.3) * d * d * d * d);
	} else if (sh >= SH_SUB_LIGHT && sh <= SH_SUB_STRONG) {
		/* red, green and blue stripes and a gap, a quarter of the cell each
		 * (one screen pixel each at 4x), and a darker bottom row: mGBA's
		 * AGB-001 and RetroArch's lcd3x */
		int p = sh - SH_SUB_LIGHT, col = (int) (u * 4);
		if (col >= 3) {
			out[0] = out[1] = out[2] = subPresets[p].gap;
		} else {
			for (int c = 0; c < 3; ++c) out[c] = c == col ? 1.0f : subPresets[p].other;
		}
		if (v >= 0.75) for (int c = 0; c < 3; ++c) out[c] *= subPresets[p].row;
	}
}

/* A multiply mask for the shaders other than the grid: gw x gh cells over a
 * dw x dh picture (any size: whole screen pixels per cell or not), each
 * pixel the average of the pattern over 4 x 4 points in it, so the pattern
 * is as sharp as the screen allows and keeps its overall darkness. Built
 * once per size. */
static SDL_Texture* pipe_pattern(int v, int dw, int dh, int gw, int gh, int sh, int strength) {
	strength = strength < 0 ? 0 : strength > 100 ? 100 : strength;
	bool hit;
	PipeEntry* e = pipe_lookup(pipeGrids[v], dw, dh, -(gw << 12 | gh), strength + 1000 * sh, &hit);
	if (hit) return e->tex;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, dw, dh, 32, SDL_PIXELFORMAT_ARGB8888);
	if (!s) return NULL;
	enum { N = 4 };
	double px = (double) dw / gw, py = (double) dh / gh, k = strength / 100.0;
	/* three stripes don't fit in under 3 screen pixels (2x on a 640x480
	 * screen): there each pixel gets their average, in grey, so the picture
	 * has the subpixels' texture and darkness without a colour cast */
	bool grey = px < 3 || py < 3;
	for (int y = 0; y < dh; ++y) {
		Uint32* row = (Uint32*) ((Uint8*) s->pixels + y * s->pitch);
		for (int x = 0; x < dw; ++x) {
			float sum[3] = { 0, 0, 0 };
			for (int sy = 0; sy < N; ++sy) {
				double fy = (y + (sy + 0.5) / N) / py, cv = fy - floor(fy);
				for (int sx = 0; sx < N; ++sx) {
					double fx = (x + (sx + 0.5) / N) / px;
					float m[3];
					pattern_at(sh, fx - floor(fx), cv, k, m);
					for (int c = 0; c < 3; ++c) sum[c] += m[c];
				}
			}
			if (grey) sum[0] = sum[1] = sum[2] = (sum[0] + sum[1] + sum[2]) / 3;
			Uint32 r = (Uint32) (255 * sum[0] / (N * N) + 0.5f), g = (Uint32) (255 * sum[1] / (N * N) + 0.5f),
			       b = (Uint32) (255 * sum[2] / (N * N) + 0.5f);
			row[x] = 0xFF000000u | r << 16 | g << 8 | b;
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

/* The chosen shader's mask for gw x gh cells over a dw x dh picture; kc: the
 * screen pixels per cell when the picture is drawn at a whole multiple
 * (the grid then draws its lines a whole screen pixel each), else 0. */
static SDL_Texture* shader_mask(int v, int dw, int dh, int gw, int gh, int kc) {
	bool small = dw < gw * 3 || dh < gh * 3;
	if (cfg->shader == SH_GRID || cfg->shader <= SH_OFF || cfg->shader >= SH_COUNT || (cfg->shader == SH_SOFT && small)) {
		/* (soft cells have no room for their falloff in under 3 screen
		 * pixels: the fine grid there) */
		return kc > 0 && !(cfg->shader == SH_SOFT && small) ? pipe_grid(v, gw, gh, kc, cfg->lcdStrength) : pipe_grid_fine(v, dw, dh, gw, gh, cfg->lcdStrength);
	}
	return pipe_pattern(v, dw, dh, gw, gh, cfg->shader, cfg->lcdStrength);
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
	SDL_Rect dst, pic = { s->rect.x + s->pic.x, s->rect.y + s->pic.y, s->pic.w, s->pic.h };
	fit_rect_at(&pic, w, h, mode, s->align, &dst);
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
		SDL_Texture* grid = shader_mask(v, dst.w, dst.h, gw, gh, k * c);
		if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		return;
	}
	if (lcd && (dst.w < gw * 3 || dst.h < gh * 3)) {
		/* small cells (a 640x480 screen): the picture as without the grid,
		 * then a grid made at the screen's resolution on top */
		draw_pixels(ren, v, src, w, h, &dst, 0);
		SDL_Texture* grid = shader_mask(v, dst.w, dst.h, gw, gh, 0);
		if (grid) SDL_RenderCopy(ren, grid, NULL, &dst);
		return;
	}
	SDL_Texture* target = SDL_RenderTargetSupported(ren) ? pipe_target(v, w * k, h * k) : NULL;
	if (!target) {
		/* no render targets: draw directly, grid on top */
		SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
		SDL_RenderCopy(ren, src, NULL, &dst);
		if (lcd) {
			SDL_Texture* grid = shader_mask(v, w * k, h * k, gw, gh, k * c);
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
		SDL_Texture* grid = shader_mask(v, w * k, h * k, gw, gh, k * c);
		if (grid) SDL_RenderCopy(ren, grid, NULL, NULL);
	}
	SDL_SetRenderTarget(ren, NULL);
	if (clipped) SDL_RenderSetClipRect(ren, &clipRect);
	SDL_SetTextureScaleMode(target, exact ? SDL_ScaleModeNearest : SDL_ScaleModeLinear);
	SDL_RenderCopy(ren, target, NULL, &dst);
}

SDL_Rect ui_image_rect(int screen, int w, int h, const char* mode) {
	SDL_Rect area = screens[screen].pic, dst;
	fit_rect_at(&area, w, h, mode, screens[screen].align, &dst);
	return dst;
}

void ui_fill(int screen, SDL_Rect r, SDL_Color c) {
	if (!inKey) key_layer(screen, r, c);
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
const char* image_why(const char* path); /* image.c */

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

bool ui_load_surface(SDL_Surface* s, SDL_Texture* out[2], int* w, int* h, bool smooth) {
	return textures_from(s, out, w, h, smooth);
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

/* The ROM file's own CRC32: the game's logo is per file for the games where
 * romhacks share a code. Reading a whole ROM is slow (a list of fifty
 * Pokémon ROMs read them all, again at every return to the list), so it's
 * read only for a game whose logo is being shown (compute), kept for every
 * file (path, size, date) in <state_dir>/romcrc.cache, and a prefetch for
 * the others just waits until it's known. 0 if not known / not readable. */
uint32_t doCrc32(const void* buf, size_t size);
typedef struct { char path[600]; long long size, mtime; uint32_t crc; } FileCrc;
#define FILE_CRC_MAX 512
static FileCrc fileCrc[FILE_CRC_MAX];
static int nFileCrc;
static bool fileCrcLoaded;
static char fileCrcPath[1200];
static pthread_mutex_t fileCrcLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t fileCrcWake = PTHREAD_COND_INITIALIZER;
/* (fileCrcLock held for all of these) */
static void file_crc_load(const Config* c) {
	if (fileCrcLoaded) return;
	fileCrcLoaded = true;
	snprintf(fileCrcPath, sizeof(fileCrcPath), "%s/romcrc.cache", c->stateDir);
	FILE* f = fopen(fileCrcPath, "r");
	if (!f) return;
	char line[1400];
	while (nFileCrc < FILE_CRC_MAX && fgets(line, sizeof(line), f)) {
		FileCrc e;
		unsigned crc;
		int off = 0;
		if (sscanf(line, "%lld %lld %x %n", &e.size, &e.mtime, &crc, &off) < 3 || !off) continue;
		line[strcspn(line, "\r\n")] = 0;
		snprintf(e.path, sizeof(e.path), "%s", line + off);
		e.crc = crc;
		fileCrc[nFileCrc++] = e;
	}
	fclose(f);
}
static const FileCrc* file_crc_find(const char* rom, const struct stat* st) {
	for (int i = 0; i < nFileCrc; ++i)
		if (fileCrc[i].size == st->st_size && fileCrc[i].mtime == st->st_mtime && !strcmp(fileCrc[i].path, rom)) return &fileCrc[i];
	return NULL;
}
static bool fileCrcDirty;
static void file_crc_put(const char* rom, const struct stat* st, uint32_t crc) {
	FileCrc e = { .size = st->st_size, .mtime = st->st_mtime, .crc = crc };
	snprintf(e.path, sizeof(e.path), "%s", rom);
	int at = nFileCrc < FILE_CRC_MAX ? nFileCrc++ : 0; /* (full: the oldest slot) */
	for (int i = 0; i < nFileCrc; ++i)
		if (!strcmp(fileCrc[i].path, rom)) at = i; /* (the file changed: its entry again) */
	fileCrc[at] = e;
	if (crc) fileCrcDirty = true; /* (one that couldn't be read is remembered for this run only) */
}
/* The cache file, written with the lock NOT held: the list asks for checksums
 * on the main thread, and a write to a busy SD card must not hold it up. */
static pthread_mutex_t fileCrcWriteLock = PTHREAD_MUTEX_INITIALIZER;
static void file_crc_flush(void) {
	pthread_mutex_lock(&fileCrcWriteLock);
	pthread_mutex_lock(&fileCrcLock);
	char* text = NULL;
	char path[sizeof(fileCrcPath)];
	if (fileCrcDirty) {
		fileCrcDirty = false;
		snprintf(path, sizeof(path), "%s", fileCrcPath);
		size_t cap = (size_t) nFileCrc * 1400 + 16, len = 0;
		text = malloc(cap);
		for (int i = 0; text && i < nFileCrc; ++i)
			if (fileCrc[i].crc) len += (size_t) snprintf(text + len, cap - len, "%lld %lld %08X %s\n", fileCrc[i].size, fileCrc[i].mtime, fileCrc[i].crc, fileCrc[i].path);
		if (text) text[len] = 0;
	}
	pthread_mutex_unlock(&fileCrcLock);
	if (text) {
		FILE* f = fopen(path, "w");
		if (f) {
			fputs(text, f);
			fclose(f);
		}
		free(text);
	}
	pthread_mutex_unlock(&fileCrcWriteLock);
}

static uint32_t file_crc_read(const char* rom) {
	size_t n = 0;
	uint8_t* buf = emu_rom_read_all(rom, &n);
	uint32_t crc = buf ? doCrc32(buf, n) : 0;
	free(buf);
	return crc;
}

/* In the game list (ui_file_crc_async) a checksum that isn't known yet is
 * worked out on a thread of its own, the newest request first (the game under
 * the cursor), and the list reloads its logos when one is done. Reading a
 * 16 MB Pokemon ROM from the SD card on the main thread, as the first look at
 * each file used to, stopped the list for a good part of a second. */
#define FILE_CRC_QUEUE 64
static char fileCrcQueue[FILE_CRC_QUEUE][1200];
static int nFileCrcQueue;
static char fileCrcBusy[1200];
static bool fileCrcAsync, fileCrcThread;
static volatile unsigned fileCrcGen;

static void* file_crc_worker(void* arg) {
	(void) arg;
#ifndef __wasi__
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 10); /* (below the list, above the logo extraction's 19) */
#endif
	pthread_mutex_lock(&fileCrcLock);
	for (;;) {
		while (!nFileCrcQueue) pthread_cond_wait(&fileCrcWake, &fileCrcLock);
		char rom[1200];
		snprintf(rom, sizeof(rom), "%s", fileCrcQueue[--nFileCrcQueue]); /* (the newest) */
		snprintf(fileCrcBusy, sizeof(fileCrcBusy), "%s", rom);
		struct stat st;
		bool ok = stat(rom, &st) == 0 && !file_crc_find(rom, &st);
		pthread_mutex_unlock(&fileCrcLock);
		uint32_t crc = ok ? file_crc_read(rom) : 0;
		pthread_mutex_lock(&fileCrcLock);
		if (ok) file_crc_put(rom, &st, crc);
		fileCrcBusy[0] = 0;
		++fileCrcGen;
		pthread_mutex_unlock(&fileCrcLock);
		file_crc_flush();
		pthread_mutex_lock(&fileCrcLock);
	}
	return NULL;
}

void ui_file_crc_async(bool on) {
#ifndef __wasi__
	/* (not with GBADS_SYNC: the regression tests want it done there and then) */
	fileCrcAsync = on && !(getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC")));
#else
	(void) on;
#endif
}

uint32_t ui_file_crc_generation(void) {
	return __atomic_load_n(&fileCrcGen, __ATOMIC_SEQ_CST);
}

bool ui_file_crc_pending(void) {
	pthread_mutex_lock(&fileCrcLock);
	bool p = nFileCrcQueue > 0 || fileCrcBusy[0];
	pthread_mutex_unlock(&fileCrcLock);
	return p;
}

/* 0 if it isn't known (compute false), or can't be read. *pending: it's being
 * worked out (the list asked for it: ask again when ui_file_crc_generation moves). */
static uint32_t rom_file_crc_ex(const Config* c, const char* rom, bool compute, bool* pending) {
	struct stat st;
	if (pending) *pending = false;
	if (!rom || !*rom || stat(rom, &st)) return 0;
	pthread_mutex_lock(&fileCrcLock);
	file_crc_load(c);
	const FileCrc* hit = file_crc_find(rom, &st);
	if (hit) {
		uint32_t crc = hit->crc;
		pthread_mutex_unlock(&fileCrcLock);
		return crc;
	}
	if (!compute) {
		pthread_mutex_unlock(&fileCrcLock);
		return 0;
	}
	if (fileCrcAsync) {
		bool queued = !strcmp(fileCrcBusy, rom);
		for (int i = 0; i < nFileCrcQueue && !queued; ++i) queued = !strcmp(fileCrcQueue[i], rom);
		if (!queued) {
			if (nFileCrcQueue == FILE_CRC_QUEUE) { /* (full: the oldest goes) */
				memmove(fileCrcQueue[0], fileCrcQueue[1], sizeof(fileCrcQueue[0]) * (FILE_CRC_QUEUE - 1));
				--nFileCrcQueue;
			}
			snprintf(fileCrcQueue[nFileCrcQueue++], sizeof(fileCrcQueue[0]), "%s", rom);
		} else {
			/* asked for again: the one under the cursor is first */
			for (int i = 0; i < nFileCrcQueue; ++i) {
				if (strcmp(fileCrcQueue[i], rom)) continue;
				char tmp[1200];
				snprintf(tmp, sizeof(tmp), "%s", fileCrcQueue[i]);
				memmove(fileCrcQueue[i], fileCrcQueue[i + 1], sizeof(fileCrcQueue[0]) * (nFileCrcQueue - i - 1));
				snprintf(fileCrcQueue[nFileCrcQueue - 1], sizeof(fileCrcQueue[0]), "%s", tmp);
				break;
			}
		}
		if (!fileCrcThread) {
			pthread_t t;
			fileCrcThread = pthread_create(&t, NULL, file_crc_worker, NULL) == 0;
			if (fileCrcThread) pthread_detach(t);
		}
		if (fileCrcThread) {
			pthread_cond_signal(&fileCrcWake);
			pthread_mutex_unlock(&fileCrcLock);
			if (pending) *pending = true;
			return 0;
		}
		/* (no thread: now, below) */
	}
	pthread_mutex_unlock(&fileCrcLock);
	uint32_t crc = file_crc_read(rom);
	pthread_mutex_lock(&fileCrcLock);
	if (crc) file_crc_put(rom, &st, crc);
	pthread_mutex_unlock(&fileCrcLock);
	file_crc_flush();
	return crc;
}
static uint32_t rom_file_crc(const Config* c, const char* rom, bool compute) {
	return rom_file_crc_ex(c, rom, compute, NULL);
}

/* The extracted logo's file (whether it exists yet or not): per code, or
 * (profile logoPerRom) per ROM file, so romhacks that share a code each
 * get their own; a patched game is its base ROM file, so it has the base's.
 * False when the ROM file's checksum isn't known yet and isn't being asked
 * for (compute): nothing to look for, or make, yet. */
static bool extracted_path_for(const Config* c, const char* rom, const char* code, char* path, size_t n, bool compute) {
	const MapProfile* prof = profile_find(code);
	if (prof && prof->logoPerRom) {
		bool pending;
		uint32_t crc = rom_file_crc_ex(c, rom, compute, &pending);
		if (crc) {
			snprintf(path, n, "%s/logos/%.4s-%08X.png", c->stateDir, code, crc);
			return true;
		}
		if (pending) { /* (the game list: the checksum's being worked out; no path yet) */
			path[0] = 0;
			return false;
		}
		if (!compute) return false;
	}
	if (prof && prof->logo.rev) snprintf(path, n, "%s/logos/%.4s-r%d.png", c->stateDir, code, prof->logo.rev);
	else snprintf(path, n, "%s/logos/%.4s.png", c->stateDir, code);
	return true;
}
static void extracted_path(const Config* c, const char* rom, const char* code, char* path, size_t n) {
	extracted_path_for(c, rom, code, path, n, true);
}

/* The player's own logo for the game: <asset_dir>/logos/<CODE>.png|.bmp|.jpg|.webp. */
static bool own_logo(const Config* c, const char* code, char* path, size_t n) {
	const char* exts[] = { "png", "bmp", "jpg", "jpeg", "webp" };
	for (int i = 0; i < 5; ++i) {
		snprintf(path, n, "%s/logos/%.4s.%s", c->assetDir, code, exts[i]);
		if (file_there(path)) return true;
	}
	return false;
}

/* A name's title: lower case, cut at its first "(" or "[" (the type info:
 * region, revision, dump flags), spaces at its end dropped. */
static void title_key(const char* name, size_t len, char* out, size_t n) {
	size_t o = 0;
	for (size_t i = 0; i < len && name[i] && name[i] != '(' && name[i] != '[' && o + 1 < n; ++i)
		out[o++] = (char) SDL_tolower((unsigned char) name[i]);
	while (o > 0 && (out[o - 1] == ' ' || out[o - 1] == '_')) --o;
	out[o] = 0;
}

/* logo_folder's image files, listed once (and again when it changes). */
static struct {
	char dir[512];
	time_t mtime;
	char** names;
	int n;
} logoDir;

static void logo_dir_list(const char* dir) {
	struct stat st;
	if (stat(dir, &st) != 0) st.st_mtime = 0;
	if (logoDir.names && !strcmp(logoDir.dir, dir) && logoDir.mtime == st.st_mtime) return;
	for (int i = 0; i < logoDir.n; ++i) free(logoDir.names[i]);
	free(logoDir.names);
	logoDir.names = NULL, logoDir.n = 0;
	snprintf(logoDir.dir, sizeof(logoDir.dir), "%s", dir);
	logoDir.mtime = st.st_mtime;
	DIR* d = opendir(dir);
	if (!d) return;
	int cap = 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		const char* dot = strrchr(e->d_name, '.');
		if (!dot || (strcasecmp(dot, ".png") && strcasecmp(dot, ".jpg") && strcasecmp(dot, ".jpeg") && strcasecmp(dot, ".webp"))) continue;
		if (logoDir.n == cap) {
			cap = cap ? cap * 2 : 256;
			char** nn = realloc(logoDir.names, sizeof(char*) * cap);
			if (!nn) break;
			logoDir.names = nn;
		}
		logoDir.names[logoDir.n++] = strdup(e->d_name);
	}
	closedir(d);
}

/* A scraped logo (GEN-11): in logo_folder, the image whose name is the ROM
 * file's, whatever type info either has: "Game Name (USA).png",
 * "Game Name.jpg" and "Game Name-marquee.png" all go with
 * "Game Name (USA) (Rev 1).gba". Best first: the ROM's whole name, then
 * the same title, then the title with -marquee / -wheel / -logo, then with
 * any other -suffix; PNG or WebP before JPG (they can be transparent); then by name. */
static bool scraped_logo(const Config* c, const char* rom, char* path, size_t n) {
	if (!c->logoFolder[0] || !rom || !rom[0]) return false;
	const char* base = strrchr(rom, '/');
	base = base ? base + 1 : rom;
	const char* rdot = strrchr(base, '.');
	size_t rlen = rdot && rdot != base ? (size_t) (rdot - base) : strlen(base);
	char rkey[512];
	title_key(base, rlen, rkey, sizeof(rkey));
	size_t klen = strlen(rkey);
	if (!klen) return false;
	logo_dir_list(c->logoFolder);
	int best = -1, bestScore = 1 << 30;
	for (int i = 0; i < logoDir.n; ++i) {
		const char* f = logoDir.names[i];
		const char* fdot = strrchr(f, '.');
		size_t flen = (size_t) (fdot - f);
		char fkey[512];
		title_key(f, flen, fkey, sizeof(fkey));
		int rank;
		if (flen == rlen && !strncasecmp(f, base, rlen)) rank = 0;
		else if (!strcmp(fkey, rkey)) rank = 1;
		else if (!strncmp(fkey, rkey, klen) && (fkey[klen] == '-' || fkey[klen] == '_' || fkey[klen] == '.')) {
			const char* tail = fkey + klen + 1;
			rank = !strcmp(tail, "marquee") || !strcmp(tail, "wheel") || !strcmp(tail, "logo") ? 2 : 3;
		} else continue;
		int score = rank * 2 + (strcasecmp(fdot, ".png") && strcasecmp(fdot, ".webp") ? 1 : 0); /* PNG and WebP can be transparent */
		if (score < bestScore || (score == bestScore && strcmp(f, logoDir.names[best]) < 0)) best = i, bestScore = score;
	}
	if (best < 0) return false;
	snprintf(path, n, "%s/%s", c->logoFolder, logoDir.names[best]);
	return true;
}

/* A pixel unlike the background: any alpha, or, in a JPEG (no alpha to cut
 * by), a colour away from its top-left corner's (JPEG noise allowed for). */
static bool drawn_px(const uint8_t* p, const uint8_t* bg) {
	if (!bg) return p[3] != 0;
	return abs(p[0] - bg[0]) > 24 || abs(p[1] - bg[1]) > 24 || abs(p[2] - bg[2]) > 24;
}

/* What's drawn, the rest cut away: a PNG's transparent padding, or a
 * JPEG's plain border (its background inside the logo's box stays). A PNG
 * with no transparency is kept whole: its background is the logo's. */
static SDL_Surface* trim_alpha(SDL_Surface* s, bool jpeg) {
	uint8_t corner[4];
	memcpy(corner, s->pixels, 4);
	const uint8_t* bg = jpeg ? corner : NULL;
	int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
	for (int y = 0; y < s->h; ++y) {
		const uint8_t* row = (const uint8_t*) s->pixels + (size_t) y * s->pitch;
		for (int x = 0; x < s->w; ++x) {
			if (!drawn_px(row + x * 4, bg)) continue;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			y1 = y;
		}
	}
	if (x1 < 0 || (x0 == 0 && y0 == 0 && x1 == s->w - 1 && y1 == s->h - 1)) return s;
	SDL_Surface* t = SDL_CreateRGBSurfaceWithFormat(0, x1 - x0 + 1, y1 - y0 + 1, 32, SDL_PIXELFORMAT_RGBA32);
	if (!t) return s;
	for (int y = 0; y < t->h; ++y)
		memcpy((uint8_t*) t->pixels + (size_t) y * t->pitch, (const uint8_t*) s->pixels + (size_t) (y0 + y) * s->pitch + (size_t) x0 * 4, (size_t) t->w * 4);
	SDL_FreeSurface(s);
	return t;
}

/* s resampled to w x h: each output pixel the average of the source pixels
 * it covers (weighted by alpha, so transparent edges don't darken), or the
 * nearest one when it's enlarged. RGBA32 in and out. */
SDL_Surface* ui_resample(const SDL_Surface* s, int w, int h) {
	SDL_Surface* o = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (!o) return NULL;
	for (int y = 0; y < h; ++y) {
		int sy0 = y * s->h / h, sy1 = SDL_max(sy0 + 1, (y + 1) * s->h / h);
		uint8_t* out = (uint8_t*) o->pixels + (size_t) y * o->pitch;
		for (int x = 0; x < w; ++x) {
			int sx0 = x * s->w / w, sx1 = SDL_max(sx0 + 1, (x + 1) * s->w / w);
			double r = 0, g = 0, b = 0, a = 0;
			int cnt = 0;
			for (int yy = sy0; yy < sy1; ++yy) {
				const uint8_t* p = (const uint8_t*) s->pixels + (size_t) yy * s->pitch + (size_t) sx0 * 4;
				for (int xx = sx0; xx < sx1; ++xx, p += 4) {
					r += p[0] * p[3], g += p[1] * p[3], b += p[2] * p[3], a += p[3];
					++cnt;
				}
			}
			uint8_t* q = out + x * 4;
			if (a > 0) {
				q[0] = (uint8_t) lround(r / a), q[1] = (uint8_t) lround(g / a), q[2] = (uint8_t) lround(b / a);
			} else {
				q[0] = q[1] = q[2] = 0;
			}
			q[3] = (uint8_t) lround(a / cnt);
		}
	}
	return o;
}

/* What the last logo choice was, for the log (once per change) and bug
 * reports: which file, or why a custom one wasn't used. */
static char logoNote[1400];
static char logoBad[1200]; /* the last custom logo that couldn't be read */
static void logo_note(const char* fmt, ...) {
	char s[sizeof(logoNote)];
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(s, sizeof(s), fmt, ap);
	va_end(ap);
	if (!strcmp(s, logoNote)) return;
	snprintf(logoNote, sizeof(logoNote), "%s", s);
	fprintf(stderr, "logo: %s\n", s);
}

const char* ui_logo_note(void) {
	return logoNote[0] ? logoNote : "none yet";
}

/* The last scraped logo made, kept: making it means reading a big PNG. */
static struct {
	char src[1200];
	int refH, pixelate;
	SDL_Surface* s;
	int sub;
	bool provisional;
} scrapedCache;
static bool scrapedProvisional;

/* The player's own or a scraped logo sized as the game's own (GEN-11): as
 * tall as the extracted logo (refH GBA pixels; until that exists, within LOGO_BOX_W x LOGO_BOX_H),
 * never wider than the GBA screen. Pixelated: one pixel per GBA pixel (*sub
 * 1); otherwise LOGO_SUB pixels per GBA pixel, the same size on the screen. */
#define LOGO_BOX_W 224
#define LOGO_BOX_H 64
static SDL_Surface* sized_surface(const Config* c, const char* rom, const char* src, const char* code, int* sub) {
	char ref[1200];
	extracted_path(c, rom, code, ref, sizeof(ref));
	int refH = 0;
	SDL_Surface* r = image_load(ref);
	if (r) refH = r->h, SDL_FreeSurface(r);
	if (scrapedCache.s && !strcmp(scrapedCache.src, src) && scrapedCache.refH == refH && scrapedCache.pixelate == (c->customLogos == CL_PIXELATED)) {
		*sub = scrapedCache.sub;
		scrapedProvisional = scrapedCache.provisional;
		return SDL_DuplicateSurface(scrapedCache.s);
	}
	SDL_Surface* img = image_load(src);
	if (!img) {
		snprintf(logoBad, sizeof(logoBad), "%s", src);
		logo_note("%s can't be read: %s; the game's own logo instead", src, image_why(src));
		return NULL;
	}
	const char* ext = strrchr(src, '.');
	img = trim_alpha(img, ext && (!strcasecmp(ext, ".jpg") || !strcasecmp(ext, ".jpeg")));
	float gw, gh; /* the size in GBA pixels */
	if (refH > 0) {
		gh = (float) SDL_min(refH, GBA_H);
		gw = (float) img->w * gh / img->h;
		if (gw > GBA_W) gw = GBA_W, gh = (float) img->h * GBA_W / img->w;
	} else {
		float sc = SDL_min((float) LOGO_BOX_W / img->w, (float) LOGO_BOX_H / img->h);
		gw = img->w * sc, gh = img->h * sc;
	}
	int k = c->customLogos == CL_PIXELATED ? 1 : LOGO_SUB;
	int w = SDL_max(1, (int) lroundf(gw * k)), h = SDL_max(1, (int) lroundf(gh * k));
	SDL_Surface* o = ui_resample(img, w, h);
	SDL_FreeSurface(img);
	if (!o) return NULL;
	if (scrapedCache.s) SDL_FreeSurface(scrapedCache.s);
	snprintf(scrapedCache.src, sizeof(scrapedCache.src), "%s", src);
	scrapedCache.refH = refH, scrapedCache.pixelate = c->customLogos == CL_PIXELATED;
	scrapedCache.s = o, scrapedCache.sub = k, scrapedCache.provisional = refH <= 0;
	*sub = k;
	scrapedProvisional = refH <= 0;
	return SDL_DuplicateSurface(o);
}

/* A screen of two lines on every screen (a start-up that would otherwise look
 * like a hang); not in the regression tests (GBADS_SYNC). */
void ui_notice(const char* title, const char* line) {
	if (getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC"))) return;
	ui_events(); /* (keeps the system from thinking the app has hung) */
	ui_clear();
	for (int scr = 0; scr < nScreens; ++scr) {
		SDL_Rect r = screens[scr].rect;
		float px = ui_px(scr);
		ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, ui_accent_color(AC_BG));
		ui_label(scr, r.w / 2, r.h / 2 - px * 6, px * 6, title, (SDL_Color) { 236, 236, 244, 255 });
		if (line && *line) ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 8, px * 4, line, (SDL_Color) { 140, 140, 172, 255 }, 0);
	}
	ui_present();
	ui_events();
}

/* A game about to start whose logo is its own file's (profile logoPerRom) needs the
 * ROM file's checksum, which is read once and kept (romcrc.cache). Left to the first
 * frames it froze the game for seconds (the whole file read and summed from the SD
 * card), so it's done here, with a screen saying so, before the game starts. */
void ui_game_prepare(const Config* c, const char* rom) {
	uint8_t h[EMU_HEADER_MAX];
	char code[5];
	if (!rom || !emu_rom_header(rom, h, sizeof(h)) || !emu_rom_code(h, sizeof(h), code)) return;
	const MapProfile* prof = profile_find(code);
	if (!prof || !prof->logoPerRom) return;
	/* what the first start of a ROM file has to do: sum the file (above), and
	 * extract its logo (a hidden copy of the game run to its title screen: seconds
	 * of CPU on the device, which, done beside the running game, froze it) */
	bool needCrc = !rom_file_crc(c, rom, false);
	char logoFile[1200] = "";
	bool needLogo = false;
	if (!needCrc && ui_logo_source(c, rom, code, logoFile, sizeof(logoFile)) == LOGO_EXTRACTED) needLogo = !file_there(logoFile);
	if (!needCrc && !needLogo) return;
	ui_notice("Initializing game...", tr("Only the first time for each game"));
	if (needCrc) {
		fprintf(stderr, "initializing %s: reading the ROM for its checksum\n", rom);
		rom_file_crc(c, rom, true);
		if (ui_logo_source(c, rom, code, logoFile, sizeof(logoFile)) == LOGO_EXTRACTED) needLogo = !file_there(logoFile);
	}
	if (needLogo && logoFile[0]) {
		char dir[1200];
		snprintf(dir, sizeof(dir), "%s/logos", c->stateDir);
		mkdirs(dir);
		fprintf(stderr, "initializing %s: extracting its logo\n", rom);
		logo_extract(rom, code, logoFile);
	}
}

void ui_logo_queue(const Config* c, const char* rom, const char* code) {
	char path[1200];
	snprintf(path, sizeof(path), "%s/logos", c->stateDir);
	mkdirs(path);
	if (!extracted_path_for(c, rom, code, path, sizeof(path), false)) return; /* (its checksum isn't known yet) */
	if (!file_there(path)) logo_request(rom, code, path);
}

bool ui_logo_provisional(void) {
	return scrapedProvisional;
}

/* The logo's source: the player's own file or a scraped one (path set), or
 * the extracted one (path its file, which may not exist yet). */
int ui_logo_source(const Config* c, const char* rom, const char* code, char* path, size_t n) {
	if (c->customLogos != CL_OFF) {
		/* (one that couldn't be read keeps saying so: sized_surface falls back) */
		if (own_logo(c, code, path, n)) {
			if (strcmp(path, logoBad)) logo_note("own %s", path);
			return LOGO_OWN;
		}
		if (scraped_logo(c, rom, path, n)) {
			if (strcmp(path, logoBad)) logo_note("scraped %s", path);
			return LOGO_SCRAPED;
		}
	}
	if (c->customLogos == CL_OFF) logo_note("the game's own (custom_logos = off)");
	else if (c->logoFolder[0]) {
		struct stat st;
		const char* base = rom ? strrchr(rom, '/') : NULL;
		if (stat(c->logoFolder, &st) != 0 || !S_ISDIR(st.st_mode))
			logo_note("the game's own: logo_folder %s isn't a folder", c->logoFolder);
		else
			logo_note("the game's own: nothing in %s (%d images) named after %s, and no logos/%.4s.png", c->logoFolder,
			          logoDir.n, base ? base + 1 : rom ? rom : "?", code);
	} else logo_note("the game's own (no logos/%.4s.png, logo_folder not set)", code);
	extracted_path(c, rom, code, path, n);
	return LOGO_EXTRACTED;
}

bool ui_logo_custom(const Config* c, const char* rom, const char* code) {
	char path[1200];
	return own_logo(c, code, path, sizeof(path)) || scraped_logo(c, rom, path, sizeof(path));
}

SDL_Surface* ui_logo_surface(const Config* c, const char* rom, const char* code, bool* pixelArt, int* sub) {
	char path[1200];
	int one;
	if (!sub) sub = &one;
	*sub = 1;
	scrapedProvisional = false;
	if (ui_logo_source(c, rom, code, path, sizeof(path)) != LOGO_EXTRACTED) {
		/* the player's own or a scraped logo, as tall as the extracted one (GEN-11) */
		SDL_Surface* s = sized_surface(c, rom, path, code, sub);
		if (s) {
			*pixelArt = *sub == 1;
			return s;
		}
		extracted_path(c, rom, code, path, sizeof(path));
	}
	*pixelArt = true;
	return image_load(path);
}

bool ui_load_logo(const Config* c, const char* rom, const char* code, SDL_Texture* tex[2], int* w, int* h,
                  bool* pixelArt) {
	char path[1200];
	scrapedProvisional = false;
	snprintf(path, sizeof(path), "%s/logos", c->stateDir);
	mkdirs(path);
	char ext[1200];
	extracted_path(c, rom, code, ext, sizeof(ext));
	if (!ext[0]) { /* (its ROM's checksum is still being worked out: asked again when it's known) */
		*pixelArt = true;
		return false;
	}
	/* the player's own logo, else a scraped one: sized from the extracted
	 * one (made if missing) */
	if (ui_logo_source(c, rom, code, path, sizeof(path)) != LOGO_EXTRACTED) {
		int sub = 1;
		SDL_Surface* s = sized_surface(c, rom, path, code, &sub);
		if (s) {
			if (scrapedProvisional && rom) logo_request(rom, code, ext);
			*pixelArt = sub == 1;
			return textures_from(s, tex, w, h, sub > 1);
		}
	}
	/* otherwise the one extracted from the game's own title screen */
	*pixelArt = true;
	if (ui_load_image(ext, tex, w, h, false)) return true;
	if (rom) logo_request(rom, code, ext);
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
	vsnprintf(osdText, sizeof(osdText), tr(fmt), ap);
	va_end(ap);
	osdUntil = ticks + 120;
}

void ui_osd_long(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(osdText, sizeof(osdText), tr(fmt), ap);
	va_end(ap);
	osdUntil = ticks + 300;
}

const char* ui_osd_text(void) {
	return ticks < osdUntil ? osdText : NULL;
}

/* ------------------------------------------------------------ rounded boxes, Futura labels */

#define DISC 64
static SDL_Texture* discTex[2]; /* white anti-aliased disc, per view */

static void disc_drop(void) {
	for (int v = 0; v < 2; ++v) {
		if (discTex[v]) SDL_DestroyTexture(discTex[v]);
		discTex[v] = NULL;
	}
}

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
	if (!inKey) key_layer(screen, r, c);
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

/* The top r.h rows of a rounded rectangle whose corners have the full radius
 * even where r.h is less than twice it (ui_fill_round would shrink the radius
 * to fit: a short strip's corners came out nearly square). The bottom is
 * left square, for a strip that is laid over a body drawn rounded already. */
static void fill_round_top(int screen, SDL_Rect r, SDL_Color c, int radius) {
	RECORD(screen, OP_ROUND_TOP) {
		o->r = r, o->col = c, o->a = radius;
		return;
	}
	radius = SDL_min(radius, r.w / 2);
	if (radius <= 0 || r.h <= 0) {
		ui_fill(screen, r, c);
		return;
	}
	int zone = SDL_min(r.h, radius); /* the rows that have a corner in them */
	ui_fill(screen, (SDL_Rect) { r.x + radius, r.y, r.w - 2 * radius, r.h }, c);
	if (r.h > radius) {
		ui_fill(screen, (SDL_Rect) { r.x, r.y + radius, radius, r.h - radius }, c);
		ui_fill(screen, (SDL_Rect) { r.x + r.w - radius, r.y + radius, radius, r.h - radius }, c);
	}
	Screen* s = &screens[screen];
	SDL_Texture* t = disc_texture(s->view);
	if (!t) return;
	SDL_SetTextureColorMod(t, c.r, c.g, c.b);
	SDL_SetTextureAlphaMod(t, c.a);
	int h = DISC / 2, sh = SDL_max(1, h * zone / radius);
	SDL_Rect src[2] = { { 0, 0, h, sh }, { h, 0, h, sh } };
	int ox = s->rect.x + r.x, oy = s->rect.y + r.y;
	SDL_Rect dst[2] = { { ox, oy, radius, zone }, { ox + r.w - radius, oy, radius, zone } };
	for (int i = 0; i < 2; ++i) SDL_RenderCopy(views[s->view].ren, t, &src[i], &dst[i]);
}


/* ---------------------------------------------------------- keys and the theme
 * The menus' buttons look like a GBA's: a raised key with an edge under it.
 * Dark (the default): dark grey keys with a light rim and a heavy dark edge.
 * Light: the web player's pale grey keys. Text on a plain key in the light
 * theme is drawn in dark ink: ui_key notes where the keys are, and
 * ui_font_text turns light colours inside one into ink. */
static int themeLight;
static int accentIdx;
static SDL_Color accentSet[AC_COUNT];
static bool accentValid;

/* Purple is the original look; the other colours are its colours turned to
 * another hue, then darkened where needed so white text and the highlight read
 * on what's selected (and a little deeper still in the light theme, whose keys
 * are pale). The highlight is a pale tint of the hue (a light purple on purple). */
static const SDL_Color accBase[AC_COUNT] = {
	[AC_BG] = { 14, 14, 26, 255 }, [AC_HEADER] = { 30, 26, 52, 255 }, [AC_ROW] = { 22, 22, 38, 255 },
	[AC_BTN] = { 34, 30, 58, 240 }, [AC_BTN_SEL] = { 92, 52, 140, 250 }, [AC_EDGE] = { 150, 120, 210, 255 },
	[AC_HI] = { 232, 200, 120, 255 }, [AC_HI_INK] = { 122, 82, 0, 255 },
	[AC_SEL_MID] = { 102, 55, 153, 255 }, [AC_LIT_MID] = { 168, 145, 227, 255 },
	[AC_SEL_HI] = { 118, 66, 172, 255 }, [AC_SEL_LO] = { 90, 47, 138, 255 }, [AC_SEL_EDGE] = { 44, 22, 80, 255 },
	[AC_SEL_BORDER] = { 20, 0, 40, 255 }, [AC_SEL_RING] = { 169, 139, 224, 255 },
	[AC_LIT_BORDER] = { 60, 40, 110, 255 }, [AC_LIT_HI] = { 188, 166, 240, 255 }, [AC_LIT_LO] = { 154, 130, 216, 255 },
	[AC_LIT_EDGE] = { 75, 58, 130, 255 },
};
static const struct { const char* id; float hue; } accents[ACCENT_COUNT] = {
	{ "purple", 268 }, { "blue", 215 }, { "teal", 175 }, { "green", 135 }, { "red", 352 }, { "orange", 24 }, { "pink", 322 },
};

static void rgb_to_hsl(SDL_Color c, float* h, float* s, float* l) {
	float r = c.r / 255.f, g = c.g / 255.f, b = c.b / 255.f;
	float mx = SDL_max(r, SDL_max(g, b)), mn = SDL_min(r, SDL_min(g, b)), d = mx - mn;
	*l = (mx + mn) / 2;
	*s = d == 0 ? 0 : d / (1 - SDL_fabsf(2 * *l - 1));
	if (d == 0) *h = 0;
	else if (mx == r) *h = 60 * fmodf((g - b) / d + 6, 6);
	else if (mx == g) *h = 60 * ((b - r) / d + 2);
	else *h = 60 * ((r - g) / d + 4);
}
static SDL_Color hsl_to_rgb(float h, float s, float l, int a) {
	float c = (1 - SDL_fabsf(2 * l - 1)) * s, hp = fmodf(h + 720, 360) / 60, x = c * (1 - SDL_fabsf(fmodf(hp, 2) - 1)), r = 0, g = 0, b = 0;
	if (hp < 1) r = c, g = x; else if (hp < 2) r = x, g = c; else if (hp < 3) g = c, b = x;
	else if (hp < 4) g = x, b = c; else if (hp < 5) r = x, b = c; else r = c, b = x;
	float m = l - c / 2;
	return (SDL_Color) { (Uint8) SDL_clamp((int) ((r + m) * 255 + .5f), 0, 255), (Uint8) SDL_clamp((int) ((g + m) * 255 + .5f), 0, 255),
	                     (Uint8) SDL_clamp((int) ((b + m) * 255 + .5f), 0, 255), (Uint8) a };
}
static float rel_lum(SDL_Color c) {
	float v[3] = { c.r / 255.f, c.g / 255.f, c.b / 255.f };
	for (int i = 0; i < 3; ++i) v[i] = v[i] <= 0.03928f ? v[i] / 12.92f : powf((v[i] + 0.055f) / 1.055f, 2.4f);
	return 0.2126f * v[0] + 0.7152f * v[1] + 0.0722f * v[2];
}
static float contrast(SDL_Color a, SDL_Color b) {
	float la = rel_lum(a), lb = rel_lum(b);
	return (SDL_max(la, lb) + 0.05f) / (SDL_min(la, lb) + 0.05f);
}
/* a colour turned to a hue (shift degrees) with its lightness scaled */
static SDL_Color accent_turn(SDL_Color c, float shift, float lScale) {
	float h, s, l;
	rgb_to_hsl(c, &h, &s, &l);
	return hsl_to_rgb(h + shift, s, SDL_min(1.f, l * lScale), c.a);
}

static void accent_build(void) {
	float shift = accents[accentIdx].hue - accents[0].hue;
	/* the selected key's family: a lightness scale small enough for white text and the highlight to read on its body */
	float f = themeLight ? 0.92f : 1.f;
	SDL_Color hi, hiInk;
	hi = hsl_to_rgb(accents[accentIdx].hue, 0.85f, 0.80f, 255);
	hiInk = hsl_to_rgb(accents[accentIdx].hue, 0.80f, 0.24f, 255);
	SDL_Color white = { 255, 255, 255, 255 };
	for (;;) {
		SDL_Color body = accent_turn(accBase[AC_SEL_LO], shift, f);
		if ((contrast(white, body) >= 6.f && contrast(hi, body) >= 4.8f) || f < 0.4f) break;
		f *= 0.96f;
	}
	for (int i = 0; i < AC_COUNT; ++i) {
		bool sel = i == AC_BTN_SEL || i == AC_SEL_MID || i == AC_SEL_HI || i == AC_SEL_LO || i == AC_SEL_EDGE || i == AC_SEL_BORDER || i == AC_SEL_RING;
		accentSet[i] = !accentIdx && !themeLight ? accBase[i] : accent_turn(accBase[i], shift, sel ? f : 1.f);
	}
	accentSet[AC_HI] = hi;
	accentSet[AC_HI_INK] = hiInk;
	accentValid = true;
}
void ui_accent(const char* id) {
	int n = 0;
	for (int i = 0; id && i < ACCENT_COUNT; ++i) if (!strcmp(id, accents[i].id)) n = i;
	accentIdx = n;
	accentValid = false;
}
SDL_Color ui_accent_color(int role) {
	if (!accentValid) accent_build();
	return accentSet[SDL_clamp(role, 0, AC_COUNT - 1)];
}
void ui_theme(int light) {
	themeLight = light != 0;
	accentValid = false;
}
bool ui_theme_light(void) {
	return themeLight;
}
SDL_Color ui_key_ink(void) {
	return themeLight ? (SDL_Color) { 69, 71, 90, 255 } : (SDL_Color) { 238, 240, 255, 255 };
}
SDL_Color ui_key_mid(int state) {
	if (state == KEY_SEL) return ui_accent_color(AC_SEL_MID);
	if (state == KEY_LIT) return ui_accent_color(AC_LIT_MID);
	return themeLight ? (SDL_Color) { 205, 206, 212, 255 } : (SDL_Color) { 53, 54, 62, 255 };
}

/* What was drawn this frame that decides how text on it is coloured, in drawing
 * order: plain keys (ink on them in the light theme) and anything else big
 * enough to be what the text is on (a panel, a selected key): the last one under
 * the text wins, so a panel over the game list isn't "a key" for its text. */
#define KEYS_MAX 240
typedef struct { SDL_Rect r; bool plain; } KeyLayer;
static KeyLayer keyRects[2][KEYS_MAX];

static void key_add(int screen, SDL_Rect r, bool plain) {
	if (!themeLight || screen < 0 || screen > 1) return;
	if (keyCount[screen] >= KEYS_MAX) return;
	keyRects[screen][keyCount[screen]++] = (KeyLayer) { r, plain };
}
/* a big opaque fill that isn't a key: a panel, the dimming */
static void key_layer(int screen, SDL_Rect r, SDL_Color c) {
	if (c.a >= 190 && r.w * r.h > 90000) key_add(screen, r, false);
}

static SDL_Color key_text_color(int screen, int x, int y, SDL_Color c) {
	if (!themeLight || screen < 0 || screen > 1) return c;
	bool in = false;
	for (int i = keyCount[screen] - 1; i >= 0; --i) {
		SDL_Rect r = keyRects[screen][i].r;
		if (x >= r.x - 2 && x <= r.x + r.w + 2 && y >= r.y && y <= r.y + r.h) {
			in = keyRects[screen][i].plain;
			break;
		}
	}
	if (!in) return c;
	int lum = (c.r + c.g + c.b) / 3;
	SDL_Color hiC = ui_accent_color(AC_HI);
	if (c.r == hiC.r && c.g == hiC.g && c.b == hiC.b) return (SDL_Color) { ui_accent_color(AC_HI_INK).r, ui_accent_color(AC_HI_INK).g, ui_accent_color(AC_HI_INK).b, c.a }; /* the highlight */
	if (c.r > 190 && c.g > 140 && c.b < 150 && c.r - c.b > 70) return (SDL_Color) { 122, 82, 0, c.a }; /* gold */
	if (lum >= 170) return (SDL_Color) { 69, 71, 90, c.a };
	if (lum >= 90) return (SDL_Color) { 108, 110, 128, c.a }; /* (the ink itself, at 77, stays) */
	return c;
}

/* A key's colours: its border, the rim inside it, the top and bottom of the
 * body, the edge under it and (selected keys) the ring round it. */
void ui_key_palette(int state, SDL_Color* border, SDL_Color* rim, SDL_Color* hi, SDL_Color* lo, SDL_Color* edge, SDL_Color* ring) {
	*ring = ui_accent_color(AC_SEL_RING);
	if (state == KEY_SEL) {
		*border = *rim = ui_accent_color(AC_SEL_BORDER);
		*hi = ui_accent_color(AC_SEL_HI), *lo = ui_accent_color(AC_SEL_LO), *edge = ui_accent_color(AC_SEL_EDGE);
	} else if (state == KEY_LIT) {
		*border = *rim = ui_accent_color(AC_LIT_BORDER);
		*hi = ui_accent_color(AC_LIT_HI), *lo = ui_accent_color(AC_LIT_LO), *edge = ui_accent_color(AC_LIT_EDGE);
	} else if (themeLight) {
		*border = (SDL_Color) { 171, 173, 181, 255 }, *rim = *border;
		*hi = (SDL_Color) { 222, 223, 228, 255 }, *lo = (SDL_Color) { 190, 192, 199, 255 }, *edge = (SDL_Color) { 141, 143, 152, 255 };
	} else {
		*border = (SDL_Color) { 12, 12, 16, 255 }, *rim = (SDL_Color) { 94, 95, 107, 255 };
		*hi = (SDL_Color) { 66, 67, 76, 255 }, *lo = (SDL_Color) { 44, 45, 52, 255 }, *edge = (SDL_Color) { 5, 5, 8, 255 };
	}
}

static void key_draw(int screen, SDL_Rect r, int radius, int state, bool trueCorners) {
	inKey = true;
	int lift = SDL_max(2, SDL_min(r.h / 14, 5));
	SDL_Color border, rim, hi, lo, edge, ring;
	ui_key_palette(state, &border, &rim, &hi, &lo, &edge, &ring);
	if (state == KEY_SEL) {
		int e = SDL_max(2, lift);
		ui_fill_round(screen, (SDL_Rect) { r.x - e, r.y - e, r.w + e * 2, r.h + e * 2 + lift }, ring, radius + e);
	}
	/* the edge under the key, then the key: border, rim, the body in two tones, a highlight along the top */
	ui_fill_round(screen, (SDL_Rect) { r.x, r.y + lift, r.w, r.h }, edge, radius);
	ui_fill_round(screen, r, border, radius);
	ui_fill_round(screen, (SDL_Rect) { r.x + 1, r.y + 1, r.w - 2, r.h - 2 }, rim, SDL_max(1, radius - 1));
	int in = state == KEY_PLAIN && !themeLight ? 2 : 1;
	ui_fill_round(screen, (SDL_Rect) { r.x + in + 1, r.y + in + 1, r.w - (in + 1) * 2, r.h - (in + 1) * 2 }, lo, SDL_max(1, radius - in - 1));
	/* (a flat face: only 30% of the way from lo to hi at the top) */
	hi = (SDL_Color) { lo.r + (hi.r - lo.r) * 3 / 10, lo.g + (hi.g - lo.g) * 3 / 10, lo.b + (hi.b - lo.b) * 3 / 10, 255 };
	/* the body's gradient from the top: lo, then bands of colour closer to hi, shorter each time */
	int bh = r.h - (in + 1) * 2, bx = r.x + in + 1, by = r.y + in + 1, bw = r.w - (in + 1) * 2;
	for (int i = 1; i <= 6; ++i) {
		SDL_Color m = { (lo.r * (6 - i) + hi.r * i) / 6, (lo.g * (6 - i) + hi.g * i) / 6, (lo.b * (6 - i) + hi.b * i) / 6, 255 };
		SDL_Rect band = { bx, by, bw, bh * (7 - i) * 11 / 100 + 1 };
		/* (a short band keeps the body's corner radius when asked: otherwise its corners come out nearly square over the rounded body) */
		if (trueCorners) fill_round_top(screen, band, m, SDL_max(1, radius - in - 1));
		else ui_fill_round(screen, band, m, SDL_max(1, radius - in - 1));
	}
	inKey = false;
	key_add(screen, r, state == KEY_PLAIN);
}

void ui_key(int screen, SDL_Rect r, int radius, int state) { key_draw(screen, r, radius, state, false); }
void ui_key_round(int screen, SDL_Rect r, int radius, int state) { key_draw(screen, r, radius, state, true); }

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

static void text_cache_drop(void) {
	for (int i = 0; i < TEXT_CACHE; ++i) {
		if (textCache[i].tex) SDL_DestroyTexture(textCache[i].tex);
		free(textCache[i].text);
		memset(&textCache[i], 0, sizeof(textCache[i]));
	}
}
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
	return text_width(font, capPx, tr(text));
}

/* Text as it is: ui_font_text (what the interface says: translated) and
 * ui_title_block (a game's title: the player's own) both end here. */
static void font_text_raw(int screen, int font, int x, int cy, int capPx, const char* text, SDL_Color c, int align) {
	if (!text || !text[0] || capPx <= 0) return;
	c = key_text_color(screen, x, cy, c);
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

void ui_font_text(int screen, int font, int x, int cy, int capPx, const char* text, SDL_Color c, int align) {
	font_text_raw(screen, font, x, cy, capPx, tr(text), c, align);
}

/* The language changed (i18n_set): the pictures and widths of the old text are
 * dropped, and the fonts the new language's script needs are the fallbacks. */
void ui_language_changed(void) {
	text_cache_drop();
	for (int i = 0; i < WIDTH_CACHE; ++i) {
		free(widthCache[i].text);
		memset(&widthCache[i], 0, sizeof(widthCache[i]));
	}
	tf_fallback_for_script(i18n_script());
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
		int w = SDL_max(text_width(TF_BOLD, 100, two[0]), text_width(TF_BOLD, 100, one + i + 1));
		if (best < 0 || w < bestW) best = i, bestW = w;
	}
	/* a subtitle after a colon goes on its own line */
	const char* colon = strstr(one, ": ");
	if (colon) {
		best = (int) (colon - one) + 1;
		snprintf(two[0], sizeof(two[0]), "%.*s", best, one);
		bestW = SDL_max(text_width(TF_BOLD, 100, two[0]), text_width(TF_BOLD, 100, one + best + 1));
	}
	int oneW = text_width(TF_BOLD, 100, one);
	/* capitals at most capMax, a line's width at most the area's; two lines
	 * need 3.2 capitals of height */
	int capOne = SDL_min(capMax, SDL_min(area.w * 100 / SDL_max(1, oneW), area.h * 10 / 18));
	int capTwo = best < 0 ? 0 : SDL_min(capMax, SDL_min(area.w * 100 / SDL_max(1, bestW), area.h * 10 / 32));
	int cx = area.x + area.w / 2, cy = area.y + area.h / 2;
	if (capOne >= capTwo * 9 / 10 || best < 0) {
		font_text_raw(screen, TF_BOLD, cx, cy, capOne, one, c, 0);
		return;
	}
	snprintf(two[0], sizeof(two[0]), "%.*s", best, one);
	snprintf(two[1], sizeof(two[1]), "%s", one + best + 1);
	int gap = capTwo * 8 / 10;
	font_text_raw(screen, TF_BOLD, cx, cy - (capTwo + gap) / 2, capTwo, two[0], c, 0);
	font_text_raw(screen, TF_BOLD, cx, cy + (capTwo + gap) / 2, capTwo, two[1], c, 0);
}

int ui_label_width(int screen, int capPx, const char* text) {
	(void) screen;
	return text_width(TF_BOLD, capPx, tr(text));
}

void ui_label(int screen, int cx, int cy, int capPx, const char* text, SDL_Color c) {
	ui_font_text(screen, TF_BOLD, cx, cy, capPx, text, c, 0);
}

int ui_font_wrap(int screen, int font, int x, int y, int width, int capPx, const char* text, SDL_Color c) {
	return ui_font_wrap_around(screen, font, x, y, width, capPx, text, c, INT_MAX, width);
}

/* Line breaking: a Latin word is one piece, a Chinese character (with the closing punctuation that
 * may not start a line after it, and an opening mark with the character it opens) is one piece. */
static int utf8_cp(const char* p, int* len) {
	unsigned char c = (unsigned char) p[0];
	int n = c < 0x80 ? 1 : c < 0xE0 ? 2 : c < 0xF0 ? 3 : 4, cp = n == 1 ? c : c & (0xFF >> (n + 1));
	for (int i = 1; i < n; ++i) {
		if (!p[i]) { n = i; break; }
		cp = cp << 6 | (p[i] & 0x3F);
	}
	*len = n;
	return cp;
}
static bool cp_cjk(int cp) { return (cp >= 0x2E80 && cp <= 0xA4CF) || (cp >= 0xF900 && cp <= 0xFAFF) || (cp >= 0xFE30 && cp <= 0xFE4F) || (cp >= 0xFF00 && cp <= 0xFFEF); }
static bool cp_no_start(int cp) { /* closing and sentence punctuation, which stays with the character before */
	switch (cp) {
		case 0x3001: case 0x3002: case 0xFF0C: case 0xFF0E: case 0xFF1A: case 0xFF1B: case 0xFF1F: case 0xFF01: case 0xFF09:
		case 0x3009: case 0x300B: case 0x300D: case 0x300F: case 0x3011: case 0x2026: case 0x2014: case 0xFF05: case 0x30FC:
		case 0x201D: case 0x2019: case 0x00B7: case 0x203A:
		/* Japanese: the long-vowel mark, the middle dot and the small kana stay with the kana before them */
		case 0x30FB: case 0x3041: case 0x3043: case 0x3045: case 0x3047: case 0x3049: case 0x3063: case 0x3083: case 0x3085:
		case 0x3087: case 0x308E: case 0x30A1: case 0x30A3: case 0x30A5: case 0x30A7: case 0x30A9: case 0x30C3: case 0x30E3:
		case 0x30E5: case 0x30E7: case 0x30EE: case 0x301C:
			return true;
	}
	return false;
}
static bool cp_no_end(int cp) { /* opening marks, which stay with the character after */
	return cp == 0xFF08 || cp == 0x3008 || cp == 0x300A || cp == 0x300C || cp == 0x300E || cp == 0x3010 || cp == 0x201C || cp == 0x2018;
}
static int piece_end(const char* p, int n) {
	int len, cp = utf8_cp(p + n, &len);
	int e = n;
	if (cp_cjk(cp) || cp_no_end(cp)) {
		e = n + len;
		while (p[e]) { /* closing marks join; an opening mark needs the next character too */
			int l2, c2 = utf8_cp(p + e, &l2);
			if (cp_no_start(c2)) e += l2;
			else if (cp_no_end(cp) && cp_cjk(c2) && e == n + len) { e += l2; cp = 0; }
			else break;
		}
		return e;
	}
	while (p[e] && p[e] != ' ') {
		int l2, c2 = utf8_cp(p + e, &l2);
		if (e > n && cp_cjk(c2) && !cp_no_start(c2)) break; /* a Latin word ends where Chinese starts */
		e += l2;
	}
	return e;
}

static int wrap_impl(int screen, int font, int x, int y, int width, int capPx, const char* text, SDL_Color c, int narrowY, int narrowW, bool draw) {
	int lineH = capPx * 2, fullW = width;
	char line[512];
	const char* p = tr(text); /* (the lines below are drawn as they are) */
	while (*p) {
		/* a line whose box reaches the one on the right is narrower */
		width = y + lineH > narrowY ? narrowW : fullW;
		/* as many words as fit */
		int n = 0, fit = 0;
		while (p[n]) {
			int e = piece_end(p, n);
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
		if (draw) font_text_raw(screen, font, x, y + capPx / 2, capPx, line, c, -1);
		y += lineH;
		p += fit;
		while (*p == ' ') ++p;
	}
	return y;
}
int ui_font_wrap_around(int screen, int font, int x, int y, int width, int capPx, const char* text, SDL_Color c, int narrowY, int narrowW) {
	return wrap_impl(screen, font, x, y, width, capPx, text, c, narrowY, narrowW, true);
}
int ui_font_wrap_height(int font, int width, int capPx, const char* text) {
	return wrap_impl(0, font, 0, 0, width, capPx, text, (SDL_Color) { 0, 0, 0, 0 }, 0, width, false);
}

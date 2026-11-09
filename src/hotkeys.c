#include "hotkeys.h"

#include "ui.h"

#include <stdio.h>
#include <string.h>
#include <strings.h>

const char* const hotkeyBtnNames[B_COUNT] = { "a", "b", "x", "y", "start", "select", "home", "l", "r", "l2", "r2", "l3",
	                                          "up", "down", "left", "right" };
/* as players know them (the device's names, as in the wiki's Buttons table) */
static const char* const btnTitles[B_COUNT] = { "A", "B", "X", "Y", "Start", "Select", "Home", "L1", "R1", "L2", "R2", "L3",
	                                            "Up", "Down", "Left", "Right" };
static const char* const actionTitles[HK_COUNT] = { "Quit", "Quick save state", "Quick load state", "Open quick menu", "Swap screens",
                                                    "Toggle selected shader" };

static int btn_by_name(const char* s) {
	for (int b = 0; b < B_COUNT; ++b) {
		if (!strcasecmp(s, hotkeyBtnNames[b])) return b;
	}
	return -1;
}

static int mod_of(const char* h) {
	return !strcasecmp(h, "back") || !strcasecmp(h, "select") ? B_SELECT :
	       !strcasecmp(h, "start")                             ? B_START :
	       !strcasecmp(h, "l3")                                ? B_L3 : B_HOME;
}

void hotkeys_resolve(const Config* cfg, int* mod, int btn[HK_COUNT], bool log) {
	const char* h = cfg->hotkey;
	*mod = mod_of(h);
	if (log && *mod == B_HOME && strcasecmp(h, "home") && strcasecmp(h, "guide")) fprintf(stderr, "hotkey: '%s' isn't a hotkey button, HOME is\n", h);
	for (int i = 0; i < HK_COUNT; ++i) {
		bool none = !strcasecmp(cfg->hotkeyBtn[i], "none");
		int b = none ? -1 : btn_by_name(cfg->hotkeyBtn[i]);
		if (log && b < 0 && !none) fprintf(stderr, "hotkey: hotkey_%s = %s isn't a button\n", hotkeyNames[i], cfg->hotkeyBtn[i]);
		/* (Quit on the hotkey itself: START as the hotkey, Quit's default
		 * START: then it's SELECT, as on most handhelds) */
		if (b == *mod && i == HK_QUIT && *mod == B_START) b = B_SELECT;
		if (b == *mod || b == B_HOME) b = -1; /* (HOME is the menu's) */
		for (int j = 0; j < i && b >= 0; ++j) {
			if (btn[j] == b) {
				if (log) fprintf(stderr, "hotkey: %s is already %s's button\n", hotkeyBtnNames[b], hotkeyNames[j]);
				b = -1;
			}
		}
		btn[i] = b;
	}
}

/* ---------------------------------------------------------------- the screen */

static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };
static const SDL_Color C_SEL = { 92, 52, 140, 255 };
static const SDL_Color C_EDGE = { 150, 120, 210, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };

/* rows: the hotkey button, the actions, then Reset and Done side by side */
enum { R_MOD = 0, R_RESET = 1 + HK_COUNT, R_DONE, N_ROWS };

typedef struct {
	int mod, btn[HK_COUNT];
	int sel;
	int listen;          /* the row waiting for a button, or -1 */
	int secondsLeft;     /* no touchscreen: the wait's countdown, or -1 */
	bool touch;
	char note[160];
} View_;

static SDL_Rect row_rect(int scr, int row) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int m = px * 10, w = r.w - m * 2;
	if (row >= R_RESET) {
		int bw = (w - px * 8) / 2;
		return (SDL_Rect) { m + (row == R_DONE ? bw + px * 8 : 0), (int) (px * 164), bw, (int) (px * 16) };
	}
	return (SDL_Rect) { m, (int) (px * (34 + row * 16)), w, (int) (px * 13) };
}

static void draw(int scr, const View_* v) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	ui_label(scr, r.w / 2, px * 12, px * 6, "Hotkeys", C_TEXT);
	ui_font_text(scr, TF_BODY, r.w / 2, px * 24, px * 4, "In a game, hold the hotkey button and press another", C_DIM, 0);
	for (int i = 0; i < N_ROWS; ++i) {
		SDL_Rect b = row_rect(scr, i);
		bool on = i == v->sel;
		if (on) ui_fill_round(scr, (SDL_Rect) { b.x - px, b.y - px, b.w + px * 2, b.h + px * 2 }, C_EDGE, px * 4);
		ui_fill_round(scr, b, on ? C_SEL : C_ROW, px * 3);
		if (i >= R_RESET) {
			ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, i == R_DONE ? "Done" : "Reset", C_TEXT);
			continue;
		}
		const char* title = i == R_MOD ? "Hotkey button" : actionTitles[i - 1];
		ui_font_text(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2, px * 5, title, C_TEXT, -1);
		const char* val;
		SDL_Color vc = C_TEXT;
		char buf[48];
		if (v->listen == i) {
			val = "Press a button", vc = C_GOLD;
		} else if (i == R_MOD) {
			val = btnTitles[v->mod];
		} else if (v->btn[i - 1] < 0) {
			val = "None", vc = C_DIM;
		} else {
			snprintf(buf, sizeof(buf), "%s + %s", btnTitles[v->mod], btnTitles[v->btn[i - 1]]);
			val = buf;
		}
		ui_font_text(scr, TF_BOLD, b.x + b.w - px * 6, b.y + b.h / 2, px * 5, val, vc, 1);
	}
	char line[200];
	const char* foot = NULL;
	SDL_Color fc = C_DIM;
	if (v->note[0]) {
		foot = v->note, fc = C_GOLD;
	} else if (v->listen >= 0) {
		if (v->secondsLeft >= 0) snprintf(line, sizeof(line), "Kept as it was in %d s if nothing is pressed", v->secondsLeft), foot = line;
		else foot = v->listen == R_MOD ? "Home, Select, Start or L3. Tap to keep it as it is" : "Tap to set none";
	} else if (v->sel == R_MOD) {
		foot = "Select or Start still reach the game when tapped on their own, L3 still opens the quick menu";
	} else if (v->sel == R_RESET) {
		foot = "Home as the hotkey button, with Start, R1, L1, X, Y and B";
	} else if (v->sel == R_DONE) {
		foot = "Kept for every game";
	}
	if (foot) ui_font_text(scr, TF_BODY, r.w / 2, px * 154, px * 4, foot, fc, 0);
}

static bool all_released(void) {
	for (int b = 0; b < B_COUNT; ++b) {
		if (btnNow[b]) return false;
	}
	return true;
}

/* gives button b to row (the hotkey button or an action); false (and a
 * note) if it can't have it */
static bool assign(View_* v, int row, int b) {
	if (row == R_MOD) {
		if (b != B_HOME && b != B_SELECT && b != B_START && b != B_L3) {
			snprintf(v->note, sizeof(v->note), "The hotkey button can be Home, Select, Start or L3");
			return false;
		}
		v->mod = b;
		for (int i = 0; i < HK_COUNT; ++i) {
			if (v->btn[i] != b) continue;
			/* (an action on the new hotkey button: Quit on Start goes to
			 * Select, if that's free; the others have none) */
			int to = -1;
			if (i == HK_QUIT && b == B_START) {
				to = B_SELECT;
				for (int j = 0; j < HK_COUNT; ++j) {
					if (v->btn[j] == B_SELECT) to = -1;
				}
			}
			v->btn[i] = to;
			if (to >= 0) snprintf(v->note, sizeof(v->note), "%s is now Start + Select", actionTitles[i]);
			else snprintf(v->note, sizeof(v->note), "%s has no button now", actionTitles[i]);
		}
		return true;
	}
	if (b == B_HOME) {
		snprintf(v->note, sizeof(v->note), "Home opens the menu: press another button");
		return false;
	}
	if (b == v->mod) {
		snprintf(v->note, sizeof(v->note), "That's the hotkey button: press another");
		return false;
	}
	int a = row - 1;
	for (int i = 0; i < HK_COUNT; ++i) {
		if (i != a && v->btn[i] == b) {
			v->btn[i] = -1;
			snprintf(v->note, sizeof(v->note), "%s was %s's: that has no button now", btnTitles[b], actionTitles[i]);
		}
	}
	v->btn[a] = b;
	return true;
}

static void save(Config* cfg, const View_* v) {
	snprintf(cfg->hotkey, sizeof(cfg->hotkey), "%s", hotkeyBtnNames[v->mod]);
	for (int i = 0; i < HK_COUNT; ++i)
		snprintf(cfg->hotkeyBtn[i], sizeof(cfg->hotkeyBtn[i]), "%s", v->btn[i] >= 0 ? hotkeyBtnNames[v->btn[i]] : "none");
	char path[1100];
	snprintf(path, sizeof(path), "%s/hotkeys.cfg", cfg->stateDir);
	FILE* f = fopen(path, "w");
	if (!f) {
		fprintf(stderr, "hotkeys: can't write %s\n", path);
		return;
	}
	fprintf(f, "# written by Settings > Hotkeys; overrides gba-ds.cfg (delete this file to use gba-ds.cfg's again)\n");
	fprintf(f, "hotkey = %s\n", cfg->hotkey);
	for (int i = 0; i < HK_COUNT; ++i) fprintf(f, "hotkey_%s = %s\n", hotkeyNames[i], cfg->hotkeyBtn[i]);
	fclose(f);
	fprintf(stderr, "hotkeys: %s +", cfg->hotkey);
	for (int i = 0; i < HK_COUNT; ++i) fprintf(stderr, " %s %s%s", hotkeyNames[i], cfg->hotkeyBtn[i], i + 1 < HK_COUNT ? "," : "\n");
}

void hotkeys_run(Config* cfg) {
	View_ v = { 0 };
	hotkeys_resolve(cfg, &v.mod, v.btn, false);
	int startMod = v.mod, startBtn[HK_COUNT];
	memcpy(startBtn, v.btn, sizeof(startBtn));
	v.listen = -1;
	v.secondsLeft = -1;
	v.touch = SDL_GetNumTouchDevices() > 0 || cfg->touchDevice[0];
	bool waitRelease = true; /* the press that opened it, then each one taken */
	Uint32 releasedAt = 0, listenAt = 0;
	int held[2] = { 0, 0 };
	bool finished = false;

	while (!finished) {
		if (!ui_events()) break;
		ui_tick();
		ui_read_buttons();
		Uint32 now = SDL_GetTicks();
		int tapped = -2; /* -2 none; a row, or -1 outside them */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.kind != 1 || p.screen != ui_touch_screen()) continue;
			v.touch = true;
			SDL_Point pt = { p.x, p.y };
			tapped = -1;
			for (int i = 0; i < N_ROWS; ++i) {
				SDL_Rect b = row_rect(p.screen, i);
				if (SDL_PointInRect(&pt, &b)) tapped = i;
			}
		}
		if (waitRelease) {
			if (!all_released()) releasedAt = 0;
			else if (!releasedAt) releasedAt = now;
			if (releasedAt && now - releasedAt > 150) waitRelease = false, listenAt = now;
		}

		if (v.listen >= 0) {
			bool stop = false;
			if (tapped != -2) {
				/* a tap: an action has no button; the hotkey button stays */
				if (v.listen != R_MOD) v.btn[v.listen - 1] = -1, v.note[0] = 0;
				stop = true;
			} else if (!waitRelease) {
				for (int b = 0; b < B_COUNT; ++b) {
					if (!btn_pressed(b)) continue;
					v.note[0] = 0;
					stop = assign(&v, v.listen, b);
					if (!stop) waitRelease = true, releasedAt = 0; /* (and ask again) */
					break;
				}
				if (!stop && !v.touch) {
					v.secondsLeft = 10 - (int) ((now - listenAt) / 1000);
					if (v.secondsLeft <= 0) stop = true;
				}
			}
			if (stop) {
				v.listen = -1, v.secondsLeft = -1;
				waitRelease = true, releasedAt = 0;
			}
		} else {
			int act = -1;
			if (tapped >= 0) v.sel = act = tapped;
			if (!waitRelease) {
				for (int k = 0; k < 2; ++k) {
					held[k] = btnNow[k ? B_DOWN : B_UP] ? held[k] + 1 : 0;
					if (held[k] == 1 || (held[k] > 20 && held[k] % 4 == 0)) {
						if (v.sel >= R_RESET) v.sel = k ? v.sel : R_MOD + HK_COUNT;
						else v.sel = k ? v.sel + 1 : (v.sel + N_ROWS - 1) % N_ROWS;
						v.note[0] = 0;
					}
				}
				if (v.sel >= R_RESET && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) v.sel = v.sel == R_DONE ? R_RESET : R_DONE;
				if (btn_pressed(B_A)) act = v.sel;
				if (btn_pressed(B_B) || btn_pressed(B_START)) act = R_DONE;
			}
			if (act == R_DONE) {
				finished = true;
			} else if (act == R_RESET) {
				Config d;
				config_defaults(&d);
				hotkeys_resolve(&d, &v.mod, v.btn, false);
				snprintf(v.note, sizeof(v.note), "Back to Home and the buttons it started with");
			} else if (act >= 0) {
				v.listen = act;
				v.note[0] = 0;
				waitRelease = true, releasedAt = 0;
			}
		}

		ui_clear();
		for (int s = 0; s < nScreens; ++s) draw(s, &v);
		ui_present();
		SDL_Delay(15);
	}
	if (v.mod != startMod || memcmp(v.btn, startBtn, sizeof(startBtn))) save(cfg, &v);
	/* the press that closed it shouldn't do anything in the game list */
	ui_read_buttons();
	ui_read_buttons();
}

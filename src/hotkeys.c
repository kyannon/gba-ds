#include "hotkeys.h"
#include "i18n.h"

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

const char* const hotkeyBtnNames[B_COUNT] = { "a", "b", "x", "y", "start", "select", "home", "l", "r", "l2", "r2", "l3",
	                                          "up", "down", "left", "right" };
/* as players know them (the device's names, as in the wiki's Buttons table;
 * Android keeps HOME for itself, and BACK stands in for it) */
#ifdef __ANDROID__
#define HOME_TITLE "Back"
#else
#define HOME_TITLE "Home"
#endif
static const char* const btnTitles[B_COUNT] = { "A", "B", "X", "Y", "Start", "Select", HOME_TITLE, "L1", "R1", "L2", "R2", "L3",
	                                            N_("Up"), N_("Down"), N_("Left"), N_("Right")};
static const char* const actionTitles[HK_COUNT] = { N_("Close emulator"), N_("Close game"), N_("Quick save state"), N_("Quick load state"), N_("Open quick menu"), N_("Swap screens"),
                                                    N_("Toggle selected shader"), N_("Turn second screen"), N_("Rewind on/off"), N_("Fast forward on/off")};
static const char* const holdTitles[HOLD_COUNT] = { N_("Rewind (hold)"), N_("Fast forward (hold)")};

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

void hotkeys_resolve_holds(const Config* cfg, int mod, int hold[HOLD_COUNT], bool log) {
	for (int i = 0; i < HOLD_COUNT; ++i) {
		bool none = !strcasecmp(cfg->holdBtn[i], "none");
		int b = none ? -1 : btn_by_name(cfg->holdBtn[i]);
		if (log && b < 0 && !none) fprintf(stderr, "hotkey: %s = %s isn't a button\n", holdNames[i], cfg->holdBtn[i]);
		if (b == mod || b == B_HOME) {
			if (log) fprintf(stderr, "hotkey: %s = %s is the hotkey button or HOME: none\n", holdNames[i], cfg->holdBtn[i]);
			b = -1;
		}
		for (int j = 0; j < i && b >= 0; ++j) {
			if (hold[j] == b) {
				if (log) fprintf(stderr, "hotkey: %s is already %s\n", hotkeyBtnNames[b], holdNames[j]);
				b = -1;
			}
		}
		hold[i] = b;
	}
}

/* ---------------------------------------------------------------- the screen */

static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };    /* the scroll bar's track */
static const SDL_Color C_EDGE = { 150, 120, 210, 255 }; /* and its thumb */
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };

/* The list's entries, in the order shown: the hotkey button (E_MOD), each
 * action (E_ACT + HK_*) and each held button (E_HOLD + HOLD_*), rewind and
 * fast forward's held button before their on/off hotkey. It scrolls; Reset
 * and Done sit side by side under it. sel / listen are positions in it (or
 * P_RESET, P_DONE). */
enum { E_MOD = 0, E_ACT = 1, E_HOLD = 1 + HK_COUNT, N_LIST = 1 + HK_COUNT + HOLD_COUNT };
static const int listOrder[N_LIST] = { E_MOD, E_ACT + HK_QUIT, E_ACT + HK_GAME, E_ACT + HK_SAVE, E_ACT + HK_LOAD, E_ACT + HK_BAR,
	                                   E_ACT + HK_SWAP, E_ACT + HK_SHADER, E_ACT + HK_TOGGLE, E_HOLD + HOLD_REWIND,
	                                   E_ACT + HK_REWIND_TOGGLE, E_HOLD + HOLD_FF, E_ACT + HK_FF_TOGGLE };
enum { P_RESET = N_LIST, P_DONE, N_POS };
#define LIST_VIS 8 /* rows shown at once */

typedef struct {
	int mod, btn[HK_COUNT], hold[HOLD_COUNT];
	int sel, top;        /* the selected position; the first row shown */
	int listen;          /* the position waiting for a button, or -1 */
	int secondsLeft;     /* no touchscreen: the wait's countdown, or -1 */
	bool touch;
	char note[160];
} View_;

static const char* entry_title(int e) {
	return e == E_MOD ? N_("Hotkey button") : e >= E_HOLD ? holdTitles[e - E_HOLD] : actionTitles[e - E_ACT];
}

/* a list row's rect by its place on screen (0 .. LIST_VIS-1), or Reset / Done */
static SDL_Rect row_rect(int scr, int vis) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int m = px * 10, w = r.w - m * 2;
	if (vis >= P_RESET) {
		int bw = (w - px * 8) / 2;
		return (SDL_Rect) { m + (vis == P_DONE ? bw + px * 8 : 0), (int) (px * 160), bw, (int) (px * 16) };
	}
	return (SDL_Rect) { m, (int) (px * (33 + vis * 14)), w - (int) (px * 5), (int) (px * 12) };
}

static void keep_visible(View_* v) {
	if (v->sel < N_LIST) {
		if (v->sel < v->top) v->top = v->sel;
		if (v->sel >= v->top + LIST_VIS) v->top = v->sel - LIST_VIS + 1;
	}
	v->top = SDL_clamp(v->top, 0, N_LIST - LIST_VIS);
}

static void draw(int scr, const View_* v) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	ui_label(scr, r.w / 2, px * 12, px * 6, "Hotkeys", C_TEXT);
	ui_font_text(scr, TF_BODY, r.w / 2, px * 24, px * 4, "In a game, hold the hotkey button and press another", C_DIM, 0);
	for (int k = 0; k < LIST_VIS + 2; ++k) {
		int pos = k < LIST_VIS ? v->top + k : P_RESET + k - LIST_VIS;
		SDL_Rect b = row_rect(scr, k < LIST_VIS ? k : pos);
		bool on = pos == v->sel;
		ui_key(scr, b, px * 3, on ? KEY_SEL : KEY_PLAIN);
		if (pos >= P_RESET) {
			ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, pos == P_DONE ? "Done" : "Reset", C_TEXT);
			continue;
		}
		int e = listOrder[pos];
		ui_font_text(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2, px * 5, entry_title(e), C_TEXT, -1);
		const char* val;
		SDL_Color vc = C_TEXT;
		char buf[48];
		int bt = e == E_MOD ? v->mod : e >= E_HOLD ? v->hold[e - E_HOLD] : v->btn[e - E_ACT];
		if (v->listen == pos) {
			val = N_("Press a button"), vc = C_GOLD;
		} else if (bt < 0) {
			val = N_("None"), vc = C_DIM;
		} else if (e == E_MOD || e >= E_HOLD) {
			val = btnTitles[bt]; /* (a held button: on its own) */
		} else {
			snprintf(buf, sizeof(buf), "%s + %s", btnTitles[v->mod], btnTitles[bt]);
			val = buf;
		}
		ui_font_text(scr, TF_BOLD, b.x + b.w - px * 6, b.y + b.h / 2, px * 5, val, vc, 1);
	}
	/* the scroll bar: where the rows shown are in the list */
	SDL_Rect first = row_rect(scr, 0), last = row_rect(scr, LIST_VIS - 1);
	int trackY = first.y, trackH = last.y + last.h - first.y;
	SDL_Rect track = { first.x + first.w + (int) (px * 2), trackY, (int) SDL_max(2.0f, px * 1.5f), trackH };
	ui_fill(scr, track, C_ROW);
	SDL_Rect thumb = { track.x, trackY + trackH * v->top / N_LIST, track.w, trackH * LIST_VIS / N_LIST };
	ui_fill(scr, thumb, C_EDGE);

	char line[200];
	const char* foot = NULL;
	SDL_Color fc = C_DIM;
	int selE = v->sel < N_LIST ? listOrder[v->sel] : -1;
	if (v->note[0]) {
		foot = v->note, fc = C_GOLD;
	} else if (v->listen >= 0) {
		if (v->secondsLeft >= 0) snprintf(line, sizeof(line), tr("Kept as it was in %d s if nothing is pressed"), v->secondsLeft), foot = line;
		else if (listOrder[v->listen] == E_MOD) snprintf(line, sizeof(line), tr("%s, Select, Start or L3. Tap to keep it as it is"), HOME_TITLE), foot = line;
		else foot = N_("Tap to set none");
	} else if (selE == E_MOD) {
		foot = N_("Select or Start still reach the game when tapped on their own, L3 still opens the quick menu");
	} else if (selE >= E_HOLD) {
		foot = N_("Held on its own or with the hotkey button. The game doesn't get this button");
	} else if (selE == E_ACT + HK_REWIND_TOGGLE || selE == E_ACT + HK_FF_TOGGLE) {
		foot = N_("Press once to start, again to stop");
	} else if (v->sel == P_RESET) {
		snprintf(line, sizeof(line), tr("%s as the hotkey button, with Start, B, R1, L1, X, Up, Down and A; L2 and R2 held"), HOME_TITLE), foot = line;
	} else if (v->sel == P_DONE) {
		foot = N_("Kept for every game");
	}
	if (foot) ui_font_text(scr, TF_BODY, r.w / 2, px * 151, px * 4, foot, fc, 0);
}

static bool all_released(void) {
	for (int b = 0; b < B_COUNT; ++b) {
		if (btnNow[b]) return false;
	}
	return true;
}

/* gives button b to the entry at position pos (the hotkey button, an action
 * or a held button); false (and a note) if it can't have it */
static bool assign(View_* v, int pos, int b) {
	int e = listOrder[pos];
	if (e == E_MOD) {
		if (b != B_HOME && b != B_SELECT && b != B_START && b != B_L3) {
			snprintf(v->note, sizeof(v->note), tr("The hotkey button can be %s, Select, Start or L3"), HOME_TITLE);
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
			if (to >= 0) snprintf(v->note, sizeof(v->note), tr("%s is now Start + Select"), tr(actionTitles[i]));
			else snprintf(v->note, sizeof(v->note), tr("%s has no button now"), tr(actionTitles[i]));
		}
		for (int i = 0; i < HOLD_COUNT; ++i) {
			if (v->hold[i] != b) continue;
			v->hold[i] = -1;
			snprintf(v->note, sizeof(v->note), tr("%s has no button now"), tr(holdTitles[i]));
		}
		return true;
	}
	if (b == B_HOME) {
		snprintf(v->note, sizeof(v->note), tr("%s opens the menu: press another button"), HOME_TITLE);
		return false;
	}
	if (b == v->mod) {
		snprintf(v->note, sizeof(v->note), tr("That's the hotkey button: press another"));
		return false;
	}
	/* (actions and held buttons are kept apart: a button can be one action's
	 * and one held button's, the hotkey button choosing between them) */
	if (e >= E_HOLD) {
		int h = e - E_HOLD;
		for (int i = 0; i < HOLD_COUNT; ++i) {
			if (i != h && v->hold[i] == b) {
				v->hold[i] = -1;
				snprintf(v->note, sizeof(v->note), tr("%s was %s's: that has no button now"), btnTitles[b], tr(holdTitles[i]));
			}
		}
		v->hold[h] = b;
		return true;
	}
	int a = e - E_ACT;
	for (int i = 0; i < HK_COUNT; ++i) {
		if (i != a && v->btn[i] == b) {
			v->btn[i] = -1;
			snprintf(v->note, sizeof(v->note), tr("%s was %s's: that has no button now"), btnTitles[b], tr(actionTitles[i]));
		}
	}
	v->btn[a] = b;
	return true;
}

static void save(Config* cfg, const View_* v) {
	snprintf(cfg->hotkey, sizeof(cfg->hotkey), "%s", hotkeyBtnNames[v->mod]);
	for (int i = 0; i < HK_COUNT; ++i)
		snprintf(cfg->hotkeyBtn[i], sizeof(cfg->hotkeyBtn[i]), "%s", v->btn[i] >= 0 ? hotkeyBtnNames[v->btn[i]] : "none");
	for (int i = 0; i < HOLD_COUNT; ++i)
		snprintf(cfg->holdBtn[i], sizeof(cfg->holdBtn[i]), "%s", v->hold[i] >= 0 ? hotkeyBtnNames[v->hold[i]] : "none");
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
	for (int i = 0; i < HOLD_COUNT; ++i) fprintf(f, "%s = %s\n", holdNames[i], cfg->holdBtn[i]);
	fclose(f);
	fprintf(stderr, "hotkeys: %s +", cfg->hotkey);
	for (int i = 0; i < HK_COUNT; ++i) fprintf(stderr, " %s %s,", hotkeyNames[i], cfg->hotkeyBtn[i]);
	for (int i = 0; i < HOLD_COUNT; ++i) fprintf(stderr, " %s %s%s", holdNames[i], cfg->holdBtn[i], i + 1 < HOLD_COUNT ? "," : "\n");
}

static void resolve_all(const Config* cfg, View_* v) {
	hotkeys_resolve(cfg, &v->mod, v->btn, false);
	hotkeys_resolve_holds(cfg, v->mod, v->hold, false);
}

void hotkeys_run(Config* cfg) {
	View_ v = { 0 };
	resolve_all(cfg, &v);
	View_ start = v;
	v.listen = -1;
	v.secondsLeft = -1;
	v.touch = SDL_GetNumTouchDevices() > 0 || cfg->touchDevice[0];
	bool waitRelease = true; /* the press that opened it, then each one taken */
	Uint32 releasedAt = 0, listenAt = 0;
	int held[2] = { 0, 0 };
	bool finished = false;
	/* a finger on the list: where it went down, the list's top then, and
	 * whether it has moved far enough to scroll rather than tap */
	int dragY = -1, dragTop = 0;
	bool dragged = false;

	while (!finished) {
		if (!ui_events()) break;
		ui_tick();
		ui_read_buttons();
		Uint32 now = SDL_GetTicks();
		int tapped = -2; /* -2 none; a position, or -1 outside them */
		bool touchedWhileListening = false;
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.screen != ui_touch_screen()) continue;
			v.touch = true;
			float px = ui_px(p.screen);
			int pitch = (int) (px * 14);
			if (p.kind == 1) {
				if (v.listen >= 0) {
					touchedWhileListening = true;
					continue;
				}
				dragY = p.y, dragTop = v.top, dragged = false;
			} else if (p.kind == 3 && dragY >= 0 && v.listen < 0) {
				int dy = p.y - dragY;
				if (abs(dy) > pitch / 2) dragged = true;
				if (dragged) v.top = SDL_clamp(dragTop - (dy + (dy < 0 ? -pitch / 2 : pitch / 2)) / pitch, 0, N_LIST - LIST_VIS);
			} else if (p.kind == 2 && dragY >= 0) {
				if (!dragged && v.listen < 0) {
					/* a tap: the row under where the finger went down */
					SDL_Point pt = { p.x, dragY };
					tapped = -1;
					for (int k = 0; k < LIST_VIS + 2; ++k) {
						int pos = k < LIST_VIS ? v.top + k : P_RESET + k - LIST_VIS;
						SDL_Rect b = row_rect(p.screen, k < LIST_VIS ? k : pos);
						if (SDL_PointInRect(&pt, &b)) tapped = pos;
					}
				}
				dragY = -1;
			}
		}
		if (waitRelease) {
			if (!all_released()) releasedAt = 0;
			else if (!releasedAt) releasedAt = now;
			if (releasedAt && now - releasedAt > 150) waitRelease = false, listenAt = now;
		}

		if (v.listen >= 0) {
			bool stop = false;
			int e = listOrder[v.listen];
			if (touchedWhileListening) {
				/* a tap: an action or held button has no button; the hotkey button stays */
				if (e >= E_HOLD) v.hold[e - E_HOLD] = -1;
				else if (e != E_MOD) v.btn[e - E_ACT] = -1;
				v.note[0] = 0;
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
						if (v.sel >= P_RESET) v.sel = k ? v.sel : N_LIST - 1;
						else v.sel = k ? v.sel + 1 : (v.sel + N_POS - 1) % N_POS;
						v.note[0] = 0;
						keep_visible(&v);
					}
				}
				if (v.sel >= P_RESET && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) v.sel = v.sel == P_DONE ? P_RESET : P_DONE;
				if (btn_pressed(B_A)) act = v.sel;
				if (btn_pressed(B_B) || btn_pressed(B_START)) act = P_DONE;
			}
			if (act == P_DONE) {
				finished = true;
			} else if (act == P_RESET) {
				Config d;
				config_defaults(&d);
				resolve_all(&d, &v);
				snprintf(v.note, sizeof(v.note), tr("Back to %s and the buttons it started with"), HOME_TITLE);
			} else if (act >= 0) {
				v.listen = act;
				v.note[0] = 0;
				waitRelease = true, releasedAt = 0;
			}
		}

		ui_clear();
		draw(ui_touch_screen(), &v); /* (on the menu's screen only, as Settings is) */
		ui_present();
		SDL_Delay(15);
	}
	if (v.mod != start.mod || memcmp(v.btn, start.btn, sizeof(v.btn)) || memcmp(v.hold, start.hold, sizeof(v.hold))) save(cfg, &v);
	/* the press that closed it shouldn't do anything in the game list */
	ui_read_buttons();
	ui_read_buttons();
}

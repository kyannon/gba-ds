#include "folderpick.h"
#include "i18n.h"

#include "ui.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

/* (Settings > Hotkeys' look; rows and buttons are the menus' keys, ui_key) */
static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };
#define C_EDGE ui_accent_color(AC_EDGE)
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
#define C_GOLD ui_accent_color(AC_HI)

#define MAX_DIRS 2048
#define VIS 7 /* list rows on screen */
enum { BTN_CANCEL, BTN_AUTO, BTN_USE, N_BTNS };

typedef struct {
	char path[1024];
	char* names[MAX_DIRS];
	int n;        /* folders in it */
	bool up;      /* the first row goes up a folder */
	int counted;  /* files counted in it */
	int sel;      /* a list row, or -1: the buttons */
	int btn;      /* the button picked when sel is -1 */
	int top;      /* the first row shown */
	bool hasAuto; /* the Automatic button is there */
	char note[200];
} View_;

static int rows(const View_* v) {
	return v->n + (v->up ? 1 : 0);
}

static int cmp_names(const void* a, const void* b) {
	return strcasecmp(*(char* const*) a, *(char* const*) b);
}

static bool is_dir(const char* p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

static void clear(View_* v) {
	for (int i = 0; i < v->n; ++i) free(v->names[i]);
	v->n = 0;
}

static void keep_in_view(View_* v) {
	if (v->sel < 0) return;
	if (v->sel < v->top) v->top = v->sel;
	if (v->sel >= v->top + VIS) v->top = v->sel - VIS + 1;
}

/* the folders in path (and how many files have count in their name,
 * pictures aside); the row of the folder named select picked, if given */
static void load(View_* v, const char* path, const char* count, const char* select) {
	clear(v);
	snprintf(v->path, sizeof(v->path), "%s", path);
	v->up = strcmp(path, "/") != 0;
	v->counted = 0;
	v->sel = 0, v->top = 0;
	DIR* d = opendir(path);
	if (!d) {
		snprintf(v->note, sizeof(v->note), tr("This folder can't be opened"));
		return;
	}
	struct dirent* e;
	char full[2048];
	while ((e = readdir(d)) != NULL) {
		if (e->d_name[0] == '.') continue; /* (., .. and hidden folders) */
		snprintf(full, sizeof(full), "%s/%s", strcmp(path, "/") ? path : "", e->d_name);
		if (is_dir(full)) {
			char* name = v->n < MAX_DIRS ? strdup(e->d_name) : NULL;
			if (name) v->names[v->n++] = name;
		} else if (count && strstr(e->d_name, count)) {
			size_t l = strlen(e->d_name);
			if (l < 4 || strcasecmp(e->d_name + l - 4, ".png")) ++v->counted;
		}
	}
	closedir(d);
	qsort(v->names, (size_t) v->n, sizeof(v->names[0]), cmp_names);
	if (!rows(v)) v->sel = -1; /* (nothing to pick: the buttons) */
	for (int i = 0; select && i < v->n; ++i) {
		if (!strcmp(v->names[i], select)) v->sel = i + (v->up ? 1 : 0);
	}
	keep_in_view(v);
}

static void go_up(View_* v, const char* count) {
	char parent[1024], child[256] = "";
	snprintf(parent, sizeof(parent), "%s", v->path);
	char* slash = strrchr(parent, '/');
	if (!slash) return;
	snprintf(child, sizeof(child), "%s", slash + 1);
	if (slash == parent) slash[1] = 0; /* ("/") */
	else *slash = 0;
	v->note[0] = 0;
	load(v, parent, count, child);
}

static void open_row(View_* v, int row, const char* count) {
	if (v->up && row == 0) {
		go_up(v, count);
		return;
	}
	char next[1024];
	snprintf(next, sizeof(next), "%s/%s", strcmp(v->path, "/") ? v->path : "", v->names[row - (v->up ? 1 : 0)]);
	v->note[0] = 0;
	load(v, next, count, NULL);
}

/* ---------------------------------------------------------------- the screen */

static SDL_Rect row_rect(int scr, int row) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int m = px * 10;
	return (SDL_Rect) { m, (int) (px * (36 + row * 15)), r.w - m * 2 - (int) (px * 4), (int) (px * 13) };
}

static SDL_Rect btn_rect(int scr, const View_* v, int b) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int m = px * 10, w = r.w - m * 2, gap = px * 6;
	int nb = v->hasAuto ? 3 : 2, slot = b == BTN_CANCEL ? 0 : b == BTN_USE ? nb - 1 : 1;
	int bw = (w - gap * (nb - 1)) / nb;
	return (SDL_Rect) { m + slot * (bw + gap), (int) (px * 164), bw, (int) (px * 16) };
}

static void draw(int scr, const View_* v, const char* title, bool showCount, const char* autoLabel) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	ui_label(scr, r.w / 2, px * 11, px * 6, title, C_TEXT);
	/* the folder shown, its start left out (…/) when it's too long */
	int maxW = r.w - px * 20;
	const char* p = v->path;
	while (ui_font_width(TF_BODY, px * 4, p) > maxW && strchr(p + 1, '/')) p = strchr(p + 1, '/');
	char shown[1100];
	snprintf(shown, sizeof(shown), "%s%s", p != v->path ? "\xe2\x80\xa6" : "", p);
	ui_font_text(scr, TF_BODY, r.w / 2, px * 24, px * 4, shown, C_GOLD, 0);

	int nr = rows(v);
	SDL_Color ink = ui_key_ink(); /* (text on a key that isn't picked: dark on the light theme's keys) */
	for (int i = v->top; i < nr && i < v->top + VIS; ++i) {
		SDL_Rect b = row_rect(scr, i - v->top);
		bool on = i == v->sel;
		ui_key(scr, b, px * 3, on ? KEY_SEL : KEY_PLAIN); /* (the menus' keys, as Hotkeys has them) */
		SDL_Rect clip = { b.x + r.x, b.y + r.y, b.w, b.h }; /* (ui_clip: the view's coordinates) */
		ui_clip(scr, &clip);
		if (v->up && i == 0) {
			char up[300];
			const char* parent = "/";
			char tmp[1024];
			snprintf(tmp, sizeof(tmp), "%s", v->path);
			char* slash = strrchr(tmp, '/');
			if (slash && slash != tmp) {
				*slash = 0;
				parent = strrchr(tmp, '/') + 1;
			}
			snprintf(up, sizeof(up), tr("Up to %s"), parent);
			ui_font_text(scr, TF_BODY, b.x + px * 6, b.y + b.h / 2, px * 5, up, on ? C_TEXT : ink, -1);
		} else {
			ui_font_text(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2, px * 5, v->names[i - (v->up ? 1 : 0)], on ? C_TEXT : ink, -1);
		}
		ui_clip(scr, NULL);
	}
	if (!v->n) ui_font_text(scr, TF_BODY, r.w / 2, row_rect(scr, v->up ? 1 : 0).y + px * 6, px * 4, "No folders in this one", C_DIM, 0);
	if (nr > VIS) {
		/* where the list is: a bar beside it */
		SDL_Rect first = row_rect(scr, 0), last = row_rect(scr, VIS - 1);
		int h = last.y + last.h - first.y, x = first.x + first.w + px * 2;
		ui_fill_round(scr, (SDL_Rect) { x, first.y, (int) (px * 2), h }, C_ROW, px);
		int bh = h * VIS / nr, by = first.y + (h - bh) * v->top / (nr - VIS);
		ui_fill_round(scr, (SDL_Rect) { x, by, (int) (px * 2), bh }, C_EDGE, px);
	}

	char foot[220];
	SDL_Color fc = C_DIM;
	if (v->note[0]) {
		snprintf(foot, sizeof(foot), "%s", v->note), fc = C_GOLD;
	} else if (showCount) {
		if (v->counted) snprintf(foot, sizeof(foot), trn("%d RetroArch state here", "%d RetroArch states here", v->counted), v->counted);
		else snprintf(foot, sizeof(foot), "%s", tr("No RetroArch states here"));
	} else {
		foot[0] = 0;
	}
	if (foot[0]) ui_font_text(scr, TF_BODY, r.w / 2, px * 153, px * 4, foot, fc, 0);

	for (int b = 0; b < N_BTNS; ++b) {
		if (b == BTN_AUTO && !v->hasAuto) continue;
		SDL_Rect br = btn_rect(scr, v, b);
		bool on = v->sel < 0 && v->btn == b;
		ui_key(scr, br, px * 3, on ? KEY_SEL : KEY_PLAIN);
		ui_label(scr, br.x + br.w / 2, br.y + br.h / 2, px * 5, b == BTN_CANCEL ? "Cancel" : b == BTN_USE ? "Use This Folder" : autoLabel,
		         on ? C_TEXT : ink);
	}
}

static bool all_released(void) {
	for (int b = 0; b < B_COUNT; ++b) {
		if (btnNow[b]) return false;
	}
	return true;
}

FolderPick folder_pick(const char* title, const char* start, const char* count, bool showCount, const char* autoLabel,
                       char* out, size_t n) {
	View_* v = calloc(1, sizeof(View_));
	if (!v) return FOLDER_CANCEL;
	v->hasAuto = autoLabel != NULL;
	v->btn = BTN_USE;
	/* the nearest folder of start that's there, else home */
	char at[1024];
	snprintf(at, sizeof(at), "%s", start && start[0] == '/' ? start : "");
	while (at[0] && !is_dir(at)) {
		char* slash = strrchr(at, '/');
		if (!slash || slash == at) snprintf(at, sizeof(at), "/");
		else *slash = 0;
	}
	if (!at[0] || !strcmp(at, "/")) {
		const char* home = getenv("HOME");
		snprintf(at, sizeof(at), "%s", home && is_dir(home) ? home : "/");
	}
	size_t al = strlen(at);
	while (al > 1 && at[al - 1] == '/') at[--al] = 0;
	load(v, at, count, NULL);

	FolderPick result = FOLDER_CANCEL;
	bool waitRelease = true, finished = false;
	Uint32 releasedAt = 0;
	int held[2] = { 0, 0 };
	/* touch: a tap on a row opens it, a drag scrolls the list */
	int downScr = -1, downY = 0, downTop = 0;
	bool dragged = false;

	while (!finished) {
		if (!ui_events()) break;
		ui_tick();
		ui_read_buttons();
		Uint32 now = SDL_GetTicks();
		int act = -2; /* -2 nothing; a list row to open; or -10 - a button */
		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.screen != ui_touch_screen()) continue;
			float px = ui_px(p.screen);
			SDL_Point pt = { p.x, p.y };
			if (p.kind == 1) {
				downScr = p.screen, downY = p.y, downTop = v->top, dragged = false;
			} else if (p.kind == 3 && downScr >= 0) {
				int moved = (downY - p.y) / (int) (px * 15);
				if (abs(downY - p.y) > px * 6) dragged = true;
				int maxTop = rows(v) > VIS ? rows(v) - VIS : 0, t = downTop + moved;
				v->top = t < 0 ? 0 : t > maxTop ? maxTop : t;
			} else if (p.kind == 2 && downScr >= 0) {
				downScr = -1;
				if (dragged) continue;
				for (int i = 0; i < VIS && v->top + i < rows(v); ++i) {
					SDL_Rect b = row_rect(p.screen, i);
					if (SDL_PointInRect(&pt, &b)) v->sel = v->top + i, act = v->sel;
				}
				for (int b = 0; b < N_BTNS; ++b) {
					SDL_Rect br = btn_rect(p.screen, v, b);
					if ((b != BTN_AUTO || v->hasAuto) && SDL_PointInRect(&pt, &br)) v->sel = -1, v->btn = b, act = -10 - b;
				}
			}
		}
		if (waitRelease) {
			if (!all_released()) releasedAt = 0;
			else if (!releasedAt) releasedAt = now;
			if (releasedAt && now - releasedAt > 150) waitRelease = false;
		}
		if (!waitRelease) {
			int nr = rows(v);
			for (int k = 0; k < 2; ++k) {
				held[k] = btnNow[k ? B_DOWN : B_UP] ? held[k] + 1 : 0;
				if (held[k] == 1 || (held[k] > 20 && held[k] % 3 == 0)) {
					if (k) v->sel = v->sel < 0 ? v->sel : v->sel + 1 >= nr ? -1 : v->sel + 1;
					else v->sel = v->sel < 0 ? nr - 1 : v->sel > 0 ? v->sel - 1 : v->sel;
					keep_in_view(v);
				}
			}
			/* L1 / R1: a screenful at a time */
			if (nr && (btn_pressed(B_L) || btn_pressed(B_R))) {
				int s = v->sel < 0 ? nr - 1 : v->sel;
				s += btn_pressed(B_R) ? VIS : -VIS;
				v->sel = s < 0 ? 0 : s >= nr ? nr - 1 : s;
				keep_in_view(v);
			}
			if (v->sel < 0 && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) {
				int d = btn_pressed(B_RIGHT) ? 1 : N_BTNS - 1;
				do v->btn = (v->btn + d) % N_BTNS;
				while (v->btn == BTN_AUTO && !v->hasAuto);
			}
			if (btn_pressed(B_A)) act = v->sel >= 0 ? v->sel : -10 - v->btn;
			if (btn_pressed(B_B)) {
				if (v->up) go_up(v, count);
				else act = -10 - BTN_CANCEL;
			}
			if (btn_pressed(B_START)) act = -10 - BTN_USE;
		}
		if (act >= 0 && act < rows(v)) {
			open_row(v, act, count);
		} else if (act == -10 - BTN_CANCEL) {
			finished = true;
		} else if (act == -10 - BTN_AUTO) {
			result = FOLDER_AUTO, finished = true;
		} else if (act == -10 - BTN_USE) {
			if (access(v->path, W_OK) != 0) {
				snprintf(v->note, sizeof(v->note), "%s", tr("GBA DS can't save in this folder: choose another"));
			} else {
				snprintf(out, n, "%s", v->path);
				result = FOLDER_CHOSEN, finished = true;
			}
		}

		ui_clear();
		draw(ui_touch_screen(), v, title, showCount, autoLabel); /* (on the menu's screen only, as Settings is) */
		ui_present();
		SDL_Delay(15);
	}
	clear(v);
	free(v);
	/* the press that closed it shouldn't do anything in the game list */
	ui_read_buttons();
	ui_read_buttons();
	return result;
}

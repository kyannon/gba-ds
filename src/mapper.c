#include "mapper.h"

#include "ui.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };

/* What GBA DS asks for, in order. stick: an analog stick direction (only
 * an axis will do); otherwise a button, a hat direction or half an axis. */
typedef struct {
	const char* key; /* SDL's name for it in a mapping */
	const char* title;
	const char* hint;
	bool stick;
} Step;

static const Step steps[] = {
	{ "dpup", "Up", "on the D-pad", false },
	{ "dpdown", "Down", "on the D-pad", false },
	{ "dpleft", "Left", "on the D-pad", false },
	{ "dpright", "Right", "on the D-pad", false },
	{ "b", "A", "the right face button", false },
	{ "a", "B", "the bottom face button", false },
	{ "x", "X", "the top face button", false },
	{ "y", "Y", "the left face button", false },
	{ "leftshoulder", "L", "the left shoulder button", false },
	{ "rightshoulder", "R", "the right shoulder button", false },
	{ "lefttrigger", "L2", "the second left shoulder button", false },
	{ "righttrigger", "R2", "the second right shoulder button", false },
	{ "back", "Select", "", false },
	{ "start", "Start", "", false },
	{ "guide", "Home", "the button that opens GBA DS's menu (Home, Menu or Function)", false },
	{ "leftstick", "L3", "press the left stick in", false },
	{ "lefty", "Left stick: up", "push the left stick up", true },
	{ "leftx", "Left stick: left", "push the left stick left", true },
};
#define N_STEPS ((int) (sizeof(steps) / sizeof(*steps)))

#define MAX_AXES 16

static SDL_Joystick* joy_of(SDL_JoystickID id) {
	return id >= 0 ? SDL_JoystickFromInstanceID(id) : NULL;
}

/* Every open joystick's axes at rest (taken when the mapper starts). */
static struct {
	SDL_JoystickID id;
	Sint16 axes[MAX_AXES];
} rest[8];
static int nRest;

static void take_rest(void) {
	nRest = 0;
	for (int i = 0; i < SDL_NumJoysticks() && nRest < 8; ++i) {
		SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
		SDL_Joystick* j = joy_of(id);
		if (!j) continue;
		rest[nRest].id = id;
		for (int a = 0; a < MAX_AXES; ++a) rest[nRest].axes[a] = a < SDL_JoystickNumAxes(j) ? SDL_JoystickGetAxis(j, a) : 0;
		++nRest;
	}
}

static int rest_axis(SDL_JoystickID id, int axis) {
	for (int i = 0; i < nRest; ++i) {
		if (rest[i].id == id && axis < MAX_AXES) return rest[i].axes[axis];
	}
	return 0;
}

/* Nothing held: no button, hat or axis away from rest, on any controller. */
static bool all_released(void) {
	for (int i = 0; i < SDL_NumJoysticks(); ++i) {
		SDL_JoystickID id = SDL_JoystickGetDeviceInstanceID(i);
		SDL_Joystick* j = joy_of(id);
		if (!j) continue;
		for (int b = 0; b < SDL_JoystickNumButtons(j); ++b) {
			if (SDL_JoystickGetButton(j, b)) return false;
		}
		for (int h = 0; h < SDL_JoystickNumHats(j); ++h) {
			if (SDL_JoystickGetHat(j, h) != SDL_HAT_CENTERED) return false;
		}
		for (int a = 0; a < SDL_JoystickNumAxes(j) && a < MAX_AXES; ++a) {
			if (abs(SDL_JoystickGetAxis(j, a) - rest_axis(id, a)) > 12000) return false;
		}
	}
	return true;
}

/* The binding for a raw input, as a mapping writes it ("" if it isn't one
 * for this step). */
static void binding(const Step* st, const RawInput* r, char* out, size_t n) {
	out[0] = 0;
	if (r->kind == 'a') {
		int base = rest_axis(r->which, r->index);
		if (abs(r->value - base) < 20000) return;
		if (st->stick) {
			snprintf(out, n, "a%d%s", r->index, r->value < base ? "" : "~"); /* left / up is the axis's low end */
		} else if (base < -16000 || base > 16000) {
			snprintf(out, n, "a%d%s", r->index, r->value > base ? "" : "~"); /* a trigger, resting at one end */
		} else {
			snprintf(out, n, "%ca%d", r->value > 0 ? '+' : '-', r->index);
		}
	} else if (st->stick) {
		return;
	} else if (r->kind == 'b') {
		snprintf(out, n, "b%d", r->index);
	} else if (r->kind == 'h') {
		int v = r->value;
		if (v == SDL_HAT_UP || v == SDL_HAT_DOWN || v == SDL_HAT_LEFT || v == SDL_HAT_RIGHT) snprintf(out, n, "h%d.%d", r->index, v);
	}
}

static void read_line(const char* path, const char* key, char* out, size_t n) {
	out[0] = 0;
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[512];
	size_t kl = strlen(key);
	while (fgets(line, sizeof(line), f)) {
		if (!strncmp(line, key, kl)) {
			char* v = line + kl;
			v[strcspn(v, "\r\n")] = 0;
			if (*v == '"') {
				++v;
				v[strcspn(v, "\"")] = 0;
			}
			snprintf(out, n, "%s", v);
			break;
		}
	}
	fclose(f);
}

/* The device and system, for the mapping's comment. */
static void describe_device(char* out, size_t n) {
	char model[128] = "", os[128] = "";
#if defined(__linux__) && !defined(__ANDROID__)
	FILE* f = fopen("/proc/device-tree/model", "r");
	if (!f) f = fopen("/sys/devices/virtual/dmi/id/product_name", "r");
	if (f) {
		size_t k = fread(model, 1, sizeof(model) - 1, f);
		model[k] = 0;
		model[strcspn(model, "\r\n")] = 0;
		fclose(f);
	}
	read_line("/etc/os-release", "PRETTY_NAME=", os, sizeof(os));
#endif
	snprintf(out, n, "%s, %s", model[0] ? model : "unknown device", os[0] ? os : SDL_GetPlatform());
}

/* Replaces the mapping for guid in the file (and the comment above it). */
static bool save_mapping(const char* path, const char* guid, const char* comment, const char* line) {
	char* old = NULL;
	size_t len = 0;
	FILE* f = fopen(path, "r");
	if (f) {
		fseek(f, 0, SEEK_END);
		long sz = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (sz > 0 && sz < 1 << 20) {
			old = calloc(1, sz + 1);
			if (old) len = fread(old, 1, sz, f);
		}
		fclose(f);
	}
	char tmp[700];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	f = fopen(tmp, "w");
	if (!f) {
		free(old);
		return false;
	}
	char pending[512] = ""; /* a comment line, written unless the mapping under it is replaced */
	for (char* p = old; p && p < old + len && *p;) {
		char* e = strchr(p, '\n');
		size_t ll = e ? (size_t) (e - p) : strlen(p);
		char l[512];
		snprintf(l, sizeof(l), "%.*s", (int) (ll < sizeof(l) - 1 ? ll : sizeof(l) - 1), p);
		p += ll + (e ? 1 : 0);
		if (l[0] == '#') {
			if (pending[0]) fprintf(f, "%s\n", pending);
			snprintf(pending, sizeof(pending), "%s", l);
			continue;
		}
		if (!strncmp(l, guid, strlen(guid)) && l[strlen(guid)] == ',') {
			pending[0] = 0; /* this one's comment goes with it */
			continue;
		}
		if (pending[0]) fprintf(f, "%s\n", pending), pending[0] = 0;
		if (l[0]) fprintf(f, "%s\n", l);
	}
	if (pending[0]) fprintf(f, "%s\n", pending);
	fprintf(f, "# %s\n%s\n", comment, line);
	free(old);
	bool ok = fclose(f) == 0;
	return ok && rename(tmp, path) == 0;
}

/* --------------------------------------------------------------- drawing */

typedef struct {
	int step;          /* N_STEPS when done */
	const char* note;  /* gold, under the prompt */
	char noteBuf[200];
	int secondsLeft;   /* auto-skip countdown (no touchscreen), -1 none */
	bool done, saved, nothing, noPad;
	char doneText[480];
} View_;

static SDL_Rect cancel_rect(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int w = px * 50, h = px * 16;
	return (SDL_Rect) { r.w - w - px * 8, r.h - h - px * 8, w, h };
}

static void draw(int scr, const View_* v, bool touch) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	ui_label(scr, r.w / 2, px * 14, px * 6, "Map Buttons", C_TEXT);
	bool here = scr == ui_touch_screen();
	int tw = r.w - px * 24;
	if (v->noPad || v->done) {
		const char* head = v->noPad ? "No controller found" : v->saved ? "Buttons mapped" : v->nothing ? "Nothing mapped" : "Couldn't save";
		ui_label(scr, r.w / 2, r.h / 2 - px * 20, px * 10, head, C_TEXT);
		const char* body = v->noPad ? "Connect a controller and try again." : v->doneText;
		ui_font_wrap(scr, TF_BODY, px * 12, r.h / 2, tw, px * 5, body, C_DIM);
		ui_font_text(scr, TF_BODY, r.w / 2, r.h - px * 14, px * 4, touch ? "Press A or tap to close" : "Press A to close", C_DIM, 0);
		return;
	}
	char line[96];
	snprintf(line, sizeof(line), "%d of %d", v->step + 1, N_STEPS);
	ui_font_text(scr, TF_BODY, r.w / 2, px * 26, px * 4, line, C_DIM, 0);
	const Step* st = &steps[v->step];
	snprintf(line, sizeof(line), st->stick ? "%s" : "Press %s", st->title);
	ui_label(scr, r.w / 2, r.h / 2 - px * 14, px * 14, line, C_TEXT);
	if (st->hint[0]) ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 8, px * 5, st->hint, C_DIM, 0);
	if (v->note) {
		int w = ui_font_width(TF_BODY, px * 4, v->note);
		if (w <= tw) ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 24, px * 4, v->note, C_GOLD, 0);
		else ui_font_wrap(scr, TF_BODY, px * 12, r.h / 2 + px * 20, tw, px * 4, v->note, C_GOLD);
	}
	if (touch) {
		const char* foot = here ? "Tap to skip a button this device doesn't have" : "Tap the touchscreen to skip a button this device doesn't have";
		ui_font_text(scr, TF_BODY, here ? px * 10 : r.w / 2, r.h - px * 16, px * 4, foot, C_DIM, here ? -1 : 0);
		if (here) {
			SDL_Rect c = cancel_rect(scr);
			ui_fill_round(scr, c, C_ROW, px * 3);
			ui_label(scr, c.x + c.w / 2, c.y + c.h / 2, px * 5, "Cancel", C_TEXT);
		}
	}
	if (v->secondsLeft >= 0) {
		snprintf(line, sizeof(line), "Skipped in %d s if nothing is pressed", v->secondsLeft);
		ui_font_text(scr, TF_BODY, r.w / 2, r.h - px * 16, px * 4, line, C_DIM, 0);
	}
}

/* ------------------------------------------------------------------ run */

void mapper_run(const Config* cfg) {
	bool touch = SDL_GetNumTouchDevices() > 0 || cfg->touchDevice[0];
	char bind[N_STEPS][16];
	memset(bind, 0, sizeof(bind));
	SDL_JoystickID locked = -1;
	View_ v = { 0 };
	v.secondsLeft = -1;
	v.noPad = SDL_NumJoysticks() == 0;
	ui_raw_capture(true);
	take_rest();
	bool waitRelease = true; /* the press that opened the mapper, then each one taken */
	Uint32 releasedAt = 0, promptAt = SDL_GetTicks();
	bool cancelled = false, finished = false;
	fprintf(stderr, "button mapper: %d controller(s)\n", SDL_NumJoysticks());

	while (!finished) {
		if (!ui_events()) break;
		ui_tick();
		ui_read_buttons();
		Uint32 now = SDL_GetTicks();
		Pointer p;
		bool tapped = false, cancel = false;
		while (ui_next_pointer(&p)) {
			if (p.kind != 1) continue;
			touch = true; /* (a mouse, or a touchscreen SDL didn't list) */
			SDL_Rect c = cancel_rect(p.screen);
			SDL_Point pt = { p.x, p.y };
			if (p.screen == ui_touch_screen() && SDL_PointInRect(&pt, &c)) cancel = true;
			else tapped = true;
		}

		if (v.noPad || v.done) {
			RawInput r;
			while (ui_next_raw(&r)) {
			}
			if (!all_released()) releasedAt = 0;
			else if (!releasedAt) releasedAt = now;
			bool settled = releasedAt && now - releasedAt > 150;
			if (tapped || cancel || (settled && (btn_pressed(B_A) || btn_pressed(B_B) || btn_pressed(B_START)))) finished = true;
		} else if (cancel) {
			cancelled = true;
			finished = true;
		} else {
			bool skip = tapped;
			if (waitRelease) {
				RawInput r;
				while (ui_next_raw(&r)) {
				}
				if (!all_released()) releasedAt = 0;
				else if (!releasedAt) releasedAt = now;
				if (releasedAt && now - releasedAt > 150) waitRelease = false, promptAt = now;
			} else {
				RawInput r;
				const Step* st = &steps[v.step];
				while (!skip && ui_next_raw(&r)) {
					if (r.kind == 'k') {
						if (r.index == SDL_SCANCODE_ESCAPE) {
							cancelled = finished = true;
							break;
						}
						v.note = "That one comes as a keyboard key here: it can't be mapped. Tap to skip.";
						continue;
					}
					if (locked >= 0 && r.which != locked) continue;
					char b[16];
					binding(st, &r, b, sizeof(b));
					if (!b[0]) {
						if (st->stick && r.kind != 'a') v.note = "Push the stick: buttons don't count for this one.";
						continue;
					}
					int dup = -1;
					for (int i = 0; i < v.step; ++i) {
						if (!strcmp(bind[i], b)) dup = i;
					}
					if (dup >= 0) {
						snprintf(v.noteBuf, sizeof(v.noteBuf), "That one is already %s. Press another, or tap to skip.", steps[dup].title);
						v.note = v.noteBuf;
						continue;
					}
					locked = r.which;
					snprintf(bind[v.step], sizeof(bind[v.step]), "%s", b);
					fprintf(stderr, "button mapper: %s = %s\n", st->key, b);
					break;
				}
				if (bind[v.step][0] || skip) {
					if (skip) fprintf(stderr, "button mapper: %s skipped\n", st->key);
					v.note = NULL;
					++v.step;
					waitRelease = true;
					releasedAt = 0;
				}
				/* no touchscreen: a button the device doesn't have is skipped after a while */
				if (!touch && !finished && v.step < N_STEPS && !waitRelease) {
					int left = 15 - (int) ((now - promptAt) / 1000);
					v.secondsLeft = left;
					if (left <= 0) {
						fprintf(stderr, "button mapper: %s skipped (time)\n", steps[v.step].key);
						v.note = NULL;
						++v.step;
						waitRelease = true;
						releasedAt = 0;
					}
				} else {
					v.secondsLeft = -1;
				}
			}
			if (v.step >= N_STEPS && !finished) {
				/* all asked: build the mapping, use it, keep it */
				v.done = true;
				v.secondsLeft = -1;
				waitRelease = true;
				releasedAt = 0;
				int n = 0;
				for (int i = 0; i < N_STEPS; ++i) n += bind[i][0] != 0;
				SDL_Joystick* j = joy_of(locked);
				if (!n || !j) {
					v.nothing = true;
					snprintf(v.doneText, sizeof(v.doneText), "Every button was skipped, so nothing changed.");
					continue;
				}
				char guid[64], name[128];
				SDL_JoystickGetGUIDString(SDL_JoystickGetGUID(j), guid, sizeof(guid));
				snprintf(name, sizeof(name), "%s", SDL_JoystickName(j) ? SDL_JoystickName(j) : "Controller");
				for (char* c = name; *c; ++c) {
					if (*c == ',') *c = ' ';
				}
				char line[1024];
				int len = snprintf(line, sizeof(line), "%s,%s,", guid, name);
				for (int i = 0; i < N_STEPS; ++i) {
					if (bind[i][0]) len += snprintf(line + len, sizeof(line) - len, "%s:%s,", steps[i].key, bind[i]);
				}
				snprintf(line + len, sizeof(line) - len, "platform:%s,", SDL_GetPlatform());
				char device[300], comment[400], path[600];
				describe_device(device, sizeof(device));
				time_t t = time(NULL);
				char date[16];
				strftime(date, sizeof(date), "%Y-%m-%d", localtime(&t));
				snprintf(comment, sizeof(comment), "%s on %s; GBA DS %s button mapper, %s", name, device, GBADS_VERSION, date);
				ui_mapping_file(path, sizeof(path));
				if (SDL_GameControllerAddMapping(line) < 0) fprintf(stderr, "button mapper: SDL refused the mapping: %s\n", SDL_GetError());
				ui_reopen_pads();
				v.saved = save_mapping(path, guid, comment, line);
				fprintf(stderr, "button mapping (%s):\n%s\n", comment, line);
				if (v.saved) {
					snprintf(v.doneText, sizeof(v.doneText),
					         "%d of %d set for %s, kept in controllers.txt beside GBA DS's save states. "
					         "The line for it is in gba-ds.log too: send it in to have it built into GBA DS for this device.",
					         n, N_STEPS, name);
				} else {
					snprintf(v.doneText, sizeof(v.doneText), "Set for now, but %s couldn't be written.", path);
				}
			}
		}

		ui_clear();
		for (int s = 0; s < nScreens; ++s) draw(s, &v, touch);
		ui_present();
		SDL_Delay(15);
	}
	if (cancelled) fprintf(stderr, "button mapper: cancelled, nothing changed\n");
	ui_raw_capture(false);
	/* the press that closed it shouldn't do anything in the game list */
	ui_read_buttons();
	ui_read_buttons();
}

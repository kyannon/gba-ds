/* WarioWare: Twisted! (docs/games/warioware-twisted.md, WWT-1 to WWT-10): the cartridge's gyro, the
 * dial that stands in for it on the touchscreen, and the wheel the second screen turns with the
 * dial. Included by game.c (after the quick bar's canvas helpers, cv_*, and the toggle button),
 * so it is one translation unit with it and adds nothing to the build lists.
 *
 * Gyro mode (every start): the device's gyro sensor is what the game reads; the second screen is
 * the logo with a Touch! button. Touch mode: a finger dragged across the touchscreen turns a dial,
 * and what the game reads is how fast the dial's angle changes (the cartridge's gyro reads a turning
 * speed; the game adds it up), so the game's own angle follows the dial's. The second screen shows
 * the game's main menu wheel, taken once from a hidden copy of the game, turned to the dial's angle;
 * a Gyro! button goes back, with a sensitivity slider beside it. */

/* (the web build has no sensors, and no <unistd.h> here: Touch mode only) */
#ifndef __wasi__
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#define WW_SENSORS 1
#if SDL_VERSION_ATLEAST(2, 0, 9)
#define WW_SDL_SENSOR 1
#endif
#endif
#ifndef WW_SDL_SENSOR
#define WW_SDL_SENSOR 0
#endif
#ifndef WW_SENSORS
#define WW_SENSORS 0
#endif

#define WW_FPS 59.7275f
/* mGBA's gyro value is 2^21 to a count of the cartridge's 12-bit sample; RetroArch's mGBA core gives
 * 5.5e8 to a radian a second, which is 4.58 counts to a degree a second (WWT-6) */
#define WW_COUNTS_PER_DEG_S 4.577f
#define WW_MAX_COUNTS 1000
#define WW_VMAX_DEG_S 200.0f  /* the quickest the dial turns: what the cartridge's sample can carry (WWT-5) */
#define WW_BACK_DEG_S 180.0f  /* how fast it turns back to 0 when the finger lifts (WWT-5) */
#define WW_FULL_DEG 90.0f     /* the angle of a drag of half the screen's width at 100% (WWT-5) */
#define WW_MAX_DEG 180.0f
#define WW_SENS_MIN 5
#define WW_SENS_MAX 25
#define WW_SENS_DEFAULT 15
#define WW_TILT_KEEP 0.985f   /* Gyro mode's logo eases back to level, a frame's share */
#define WW_RN 176             /* the wheel's rings: radii in pixels */
#define WW_TILE 4             /* the checkered disc's squares */
#define WW_KEY_W 46
#define WW_KEY_H 17
#define WW_ROWS GBA_H
#define WW_CX 119.5f          /* the wheel's centre in the menu's picture */
#define WW_CY 111.5f

enum { WWP_NONE, WWP_BUTTON, WWP_SLIDER, WWP_DIAL, WWP_PAUSE, WWP_IGNORE };
static void open_menu(void);
static SDL_Rect bar_to_screen(int s, SDL_Rect g);
enum { WWS_NONE, WWS_TEST, WWS_IIO, WWS_SDL };

typedef struct {
	bool ok;
	uint32_t ring[WW_RN]; /* the rings at each radius (the game's own colours) */
	bool ringOn[WW_RN];   /* false: the ring layer is clear there, so the disc or the console shows */
	uint32_t chk[2];      /* the checkered disc's two colours, by (x / 4 + y / 4) & 1 */
	int ix, iy, iw, ih;   /* the console's picture and where it is in the menu's picture */
	uint32_t* icon;       /* iw * ih, alpha 0 where clear */
} WwWheel;

static struct {
	bool dial;                 /* the profile has the dial (the game is WarioWare: Twisted!) */
	bool touch;                /* Touch mode */
	volatile bool down;        /* a finger is on the dial */
	volatile float dragFrac;   /* how far it has gone, in half-screens, left negative */
	float angle;               /* the dial's angle in degrees, right (clockwise) positive */
	float tilt, shownTilt;     /* Gyro mode's logo: the sensor's turning added up, easing back to 0 */
	uint32_t* logo;            /* the logo, RGBA bytes, and its size and source pixels to a screen pixel */
	int logoW, logoH;
	float logoK;
	bool logoTried;
	uint64_t logoRetry;
	int rumOn, rumOff;         /* the motor's own pulse, in frames left on and off (WWT-11) */
	float rumLevel;
	float resid;               /* the counts not yet given to the game (they're whole counts) */
	int sens, scale, invert;   /* the slider (25-200%), the sensor's scale (%), the sensor's direction */
	char axis;                 /* the sensor's axis: z, through the screen */
	int press, pressY;         /* what a finger is on (WWP_*), and where it went down */
	bool dirty;                /* the wheel's picture is to be made again */
	/* the sensor (WWT-6) */
	int skind, fd;
	char path[300], name[80];
	float sscale;              /* IIO: radians a second to a unit of the raw reading */
	void* sdl;                 /* SDL_Sensor* */
	float testRate;            /* GBADS_GYRO_RATE: a constant rate for the tests */
	float bias;
	int still;
	bool biasKnown, logged;
	long lastLog;
	/* the wheel */
	WwWheel wheel;
	volatile bool wheelReady, wheelFailed, wheelRunning;
	volatile bool wheelCancel;
	char rom[1024];
	pthread_t thread;
	bool threadStarted;
	uint32_t px[GBA_W * GBA_H];
	float shownAngle;
	SDL_Texture* tex[2];
	SDL_Renderer* texRen[2];
	/* the buttons' canvas */
	uint32_t cv[GBA_W * WW_ROWS];
	SDL_Texture* cvTex[2];
	SDL_Renderer* cvTexRen[2];
	char cfgPath[600];
} ww = { .sens = WW_SENS_DEFAULT, .scale = 100, .axis = 'z', .fd = -1 };

static float ww_clampf(float v, float lo, float hi) {
	return v < lo ? lo : v > hi ? hi : v;
}

/* ---- the settings kept for the game: the slider, and the sensor's scale, direction and axis
 * (`states/<game>.gyro`: lines "sensitivity 100", "scale 100", "invert 0", "axis z") */
static void ww_cfg_load(void) {
	ww.sens = WW_SENS_DEFAULT, ww.scale = 100, ww.invert = 0, ww.axis = 'z';
	snprintf(ww.cfgPath, sizeof(ww.cfgPath), "%s/%s.gyro", cfg->stateDir, gp.base);
	FILE* f = fopen(ww.cfgPath, "r");
	if (!f) return;
	char k[32], v[32];
	while (fscanf(f, "%31s %31s", k, v) == 2) {
		if (!strcmp(k, "sensitivity")) ww.sens = (int) ww_clampf((float) atoi(v), WW_SENS_MIN, WW_SENS_MAX);
		else if (!strcmp(k, "scale")) ww.scale = (int) ww_clampf((float) atoi(v), 10, 1000);
		else if (!strcmp(k, "invert")) ww.invert = atoi(v) != 0;
		else if (!strcmp(k, "axis") && (v[0] == 'x' || v[0] == 'y' || v[0] == 'z')) ww.axis = v[0];
	}
	fclose(f);
}

static void ww_cfg_save(void) {
	FILE* f = fopen(ww.cfgPath, "w");
	if (!f) return;
	fprintf(f, "sensitivity %d\nscale %d\ninvert %d\naxis %c\n", ww.sens, ww.scale, ww.invert, ww.axis);
	fclose(f);
}

/* ---- the sensor (WWT-6): an IIO gyro (the RG DS's, as Linux gives it), else an SDL gyro sensor
 * (Android's); the tests give a constant rate (GBADS_GYRO_RATE, degrees a second) */
static void ww_sensor_close(void) {
#if WW_SENSORS
	if (ww.fd >= 0) close(ww.fd);
#endif
	ww.fd = -1;
#if WW_SDL_SENSOR
	if (ww.sdl) SDL_SensorClose((SDL_Sensor*) ww.sdl);
#endif
	ww.sdl = NULL;
	ww.skind = WWS_NONE;
}

#if WW_SENSORS
static double ww_read_number(const char* path, bool* ok) {
	char buf[64] = "";
	int fd = open(path, O_RDONLY);
	*ok = false;
	if (fd < 0) return 0;
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n <= 0) return 0;
	buf[n] = 0;
	*ok = true;
	return atof(buf);
}

#endif

static void ww_sensor_open(void) {
	ww_sensor_close();
	ww.bias = 0, ww.biasKnown = false, ww.still = 0, ww.logged = false;
	const char* t = getenv("GBADS_GYRO_RATE");
	if (t) {
		ww.skind = WWS_TEST, ww.testRate = (float) atof(t);
		fprintf(stderr, "gyro: test rate %.1f deg/s\n", ww.testRate);
		return;
	}
#if WW_SENSORS
	/* Linux: /sys/bus/iio/devices/iio:deviceN with in_anglvel_<axis>_raw (and its scale, radians a second) */
	char dirs[8][512];
	int nd = 0;
	const char* over = getenv("GBADS_GYRO_IIO");
	if (over) snprintf(dirs[nd++], sizeof(dirs[0]), "%s", over);
	else {
		DIR* d = opendir("/sys/bus/iio/devices");
		struct dirent* e;
		while (d && (e = readdir(d)) && nd < 8)
			if (!strncmp(e->d_name, "iio:device", 10)) snprintf(dirs[nd++], sizeof(dirs[0]), "/sys/bus/iio/devices/%s", e->d_name);
		if (d) closedir(d);
	}
	for (int i = 0; i < nd; ++i) {
		char p[600];
		bool ok;
		snprintf(p, sizeof(p), "%s/in_anglvel_%c_raw", dirs[i], ww.axis);
		if (access(p, R_OK)) continue;
		double sc;
		snprintf(p, sizeof(p), "%s/in_anglvel_%c_scale", dirs[i], ww.axis);
		sc = ww_read_number(p, &ok);
		if (!ok) {
			snprintf(p, sizeof(p), "%s/in_anglvel_scale", dirs[i]);
			sc = ww_read_number(p, &ok);
		}
		if (!ok || sc <= 0) sc = 1.0;
		snprintf(ww.path, sizeof(ww.path), "%s/in_anglvel_%c_raw", dirs[i], ww.axis);
		ww.fd = open(ww.path, O_RDONLY);
		if (ww.fd < 0) continue;
		snprintf(p, sizeof(p), "%s/name", dirs[i]);
		FILE* f = fopen(p, "r");
		ww.name[0] = 0;
		if (f) {
			if (fgets(ww.name, sizeof(ww.name), f)) ww.name[strcspn(ww.name, "\r\n")] = 0;
			fclose(f);
		}
		ww.sscale = (float) sc;
		ww.skind = WWS_IIO;
		fprintf(stderr, "gyro: IIO sensor %s (%s), axis %c, %g radians a second to a unit\n", dirs[i], ww.name[0] ? ww.name : "unnamed", ww.axis, sc);
		return;
	}
#endif
#if WW_SDL_SENSOR
	SDL_InitSubSystem(SDL_INIT_SENSOR);
	for (int i = 0; i < SDL_NumSensors(); ++i) {
		if (SDL_SensorGetDeviceType(i) != SDL_SENSOR_GYRO) continue;
		SDL_Sensor* s = SDL_SensorOpen(i);
		if (!s) continue;
		ww.sdl = s, ww.skind = WWS_SDL;
		snprintf(ww.name, sizeof(ww.name), "%s", SDL_SensorGetName(s) ? SDL_SensorGetName(s) : "");
		fprintf(stderr, "gyro: SDL sensor %s, axis %c\n", ww.name, ww.axis);
		return;
	}
#endif
	fprintf(stderr, "gyro: no sensor found (Gyro mode gives the game its resting zero; Touch mode is the way to play)\n");
}

/* the sensor's turning speed about the axis, in degrees a second, left (counter-clockwise) negative as
 * the game reads it: with its resting error taken off and a small dead zone; 0 with no sensor */
static float ww_sensor_rate(void) {
	float raw = 0;
	if (ww.skind == WWS_TEST) {
		raw = ww.testRate;
		return raw;
	}
#if WW_SENSORS
	else if (ww.skind == WWS_IIO && ww.fd >= 0) {
		char buf[40] = "";
		ssize_t n = pread(ww.fd, buf, sizeof(buf) - 1, 0);
		if (n <= 0) return 0;
		buf[n] = 0;
		raw = (float) (atof(buf) * ww.sscale * (180.0 / M_PI));
	}
#endif
#if WW_SDL_SENSOR
	else if (ww.skind == WWS_SDL && ww.sdl) {
		float v[3] = { 0, 0, 0 };
		if (SDL_SensorGetData((SDL_Sensor*) ww.sdl, v, 3) < 0) return 0;
		raw = v[ww.axis == 'x' ? 0 : ww.axis == 'y' ? 1 : 2] * (float) (180.0 / M_PI);
	}
#endif
	else return 0;
	/* a right-handed axis: turned left (counter-clockwise, seen from the front) reads positive, which the game
	 * reads as negative */
	raw = -raw;
	if (ww.invert) raw = -raw;
	if (!ww.biasKnown) ww.bias = raw, ww.biasKnown = true;
	if (fabsf(raw - ww.bias) < 2.0f) {
		if (++ww.still > 60) ww.bias += (raw - ww.bias) * 0.01f; /* (held still: the resting error is followed) */
	} else ww.still = 0;
	float r = raw - ww.bias;
	if (fabsf(r) < 0.7f) r = 0;
	return r;
}

/* ---- the dial (WWT-5) and what the game is given (WWT-6): once a frame, before the game runs it */
static void ww_step(void) {
	if (!ww.dial) return;
	float prev = ww.angle, rate;
	if (ww.touch) {
		ww.tilt = 0;
		if (ww.down) {
			float lim = 2 * WW_FULL_DEG * ww.sens / 100.0f; /* (a drag of a whole screen's width is as far as it goes) */
			float target = ww_clampf(ww.dragFrac * WW_FULL_DEG * ww.sens / 100.0f, -lim, lim);
			float d = ww_clampf((target - ww.angle) * 0.35f, -WW_VMAX_DEG_S / WW_FPS, WW_VMAX_DEG_S / WW_FPS);
			ww.angle += d;
		} else if (ww.angle != 0) {
			float step = fminf(WW_BACK_DEG_S / WW_FPS, fmaxf(0.4f, fabsf(ww.angle) * 0.25f));
			if (fabsf(ww.angle) <= step) ww.angle = 0;
			else ww.angle -= copysignf(step, ww.angle);
		}
		rate = (ww.angle - prev) * WW_FPS;
	} else {
		ww.angle = 0;
		rate = ww_sensor_rate() * ww.scale / 100.0f;
		ww.tilt = ww_clampf((ww.tilt + rate / WW_FPS) * WW_TILT_KEEP, -WW_MAX_DEG, WW_MAX_DEG);
	}
	float want = rate * WW_COUNTS_PER_DEG_S + ww.resid;
	int c = (int) lrintf(ww_clampf(want, -WW_MAX_COUNTS, WW_MAX_COUNTS));
	ww.resid = fabsf(want) > WW_MAX_COUNTS ? 0 : want - (float) c;
	emu_set_gyro(&emu, (int32_t) ((int64_t) c * (1 << 21)));
	if (c || prev != ww.angle) {
		static int logEvery = -1;
		if (logEvery < 0) logEvery = getenv("GBADS_GYRO_LOG") != NULL;
		if (logEvery) fprintf(stderr, "gyro: frame %llu %s counts %d angle %.2f\n", (unsigned long long) sec.frame, ww.touch ? "dial" : "sensor", c, ww.angle);
	}
}

/* ---- the motor (WWT-11): the game rumbles the cartridge's motor all through the microgames, and a hand that can
 * turn the device (or the dial) far faster than the original's could makes it ask for far more than a handheld's
 * motor and battery were found to take (the RG DS Plus crashed in play, with rumble on and off with it off). So this
 * game's motor is not the game's own: whenever it asks, a short fixed pulse, at a capped level, then a rest, and
 * nothing the game does makes it longer, stronger or more often. Called once a frame with the game's level;
 * the player's strength setting is applied after. */
#define WW_RUMBLE_ON 6     /* frames of a pulse (100 ms) */
#define WW_RUMBLE_OFF 9    /* frames of rest after it (150 ms) */
#define WW_RUMBLE_CAP 0.6f
static float ww_rumble(float level) {
	if (!ww.dial) return level;
	if (ww.rumOn > 0) {
		if (--ww.rumOn == 0) ww.rumOff = WW_RUMBLE_OFF;
		return ww.rumLevel;
	}
	if (ww.rumOff > 0) {
		--ww.rumOff;
		return 0;
	}
	if (level > 0.05f) {
		ww.rumLevel = WW_RUMBLE_CAP * (0.5f + 0.5f * fminf(level, 1.0f));
		ww.rumOn = WW_RUMBLE_ON - 1;
		return ww.rumLevel;
	}
	return 0;
}

/* the dial and the game's gyro back to rest (a state loaded, a mode switched) */
static void ww_rest(void) {
	ww.down = false, ww.dragFrac = 0, ww.angle = 0, ww.tilt = 0, ww.resid = 0, ww.press = WWP_NONE, ww.dirty = true;
	ww.rumOn = ww.rumOff = 0;
	if (ww.dial) emu_set_gyro(&emu, 0);
}

/* a state is loaded (WWT-2): Gyro mode, whatever was on */
static void ww_state_loaded(void) {
	if (!ww.dial) return;
	ww.touch = false;
	ww_rest();
}

static bool ww_can_touch(void) {
	return ww.dial && nScreens > 1 && cfg->map;
}

/* the hotkey and the buttons: Touch mode and back (WWT-3, WWT-9) */
static void ww_switch(void) {
	if (!ww_can_touch()) return;
	ww.touch = !ww.touch;
	ww_rest();
	fprintf(stderr, "frame %llu: gyro: %s mode\n", (unsigned long long) sec.frame, ww.touch ? "Touch" : "Gyro");
}

/* ---- the wheel (WWT-4): the main menu's rings, checkered disc and console, from a hidden copy of the game
 * run from power-on to the menu: A pressed until the title and the how-to-play screen are past, START
 * pressed there until the menu is up. Each of BG1 (the rings), BG2 (the console) and BG3 (the disc) is
 * rendered alone (what equals the backdrop is clear); the rings are the same all round, so they're a
 * table of colours by radius, and the disc a pattern, which is what lets it turn. */
static bool ww_wheel_build(const uint32_t* back, const uint32_t* l1, const uint32_t* l2, const uint32_t* l3, WwWheel* W) {
	memset(W, 0, sizeof(*W));
	/* the rings: the most common colour (or clear) at each radius, from the rows above the text bar and clear of
	 * the R prompt in the corner */
	static uint32_t vcol[WW_RN][6];
	static int vcnt[WW_RN][6];
	memset(vcol, 0, sizeof(vcol)), memset(vcnt, 0, sizeof(vcnt));
	for (int y = 0; y < 144; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			if (x >= 205 && y < 36) continue;
			int r = (int) lrintf(hypotf(x - WW_CX, y - WW_CY));
			if (r >= WW_RN) continue;
			uint32_t c = ((l1[y * GBA_W + x] ^ back[y * GBA_W + x]) & 0xFFFFFF) ? (l1[y * GBA_W + x] & 0xFFFFFF) | 0x01000000 : 0;
			int k = 0;
			while (k < 5 && vcnt[r][k] && vcol[r][k] != c) ++k;
			vcol[r][k] = c, ++vcnt[r][k];
		}
	int last = -1, changes = 0, clear = 0;
	for (int r = 0; r < WW_RN; ++r) {
		int best = -1;
		for (int k = 0; k < 6; ++k)
			if (vcnt[r][k] && (best < 0 || vcnt[r][k] > vcnt[r][best])) best = k;
		if (best < 0) {
			W->ring[r] = r ? W->ring[r - 1] : 0, W->ringOn[r] = r ? W->ringOn[r - 1] : false;
		} else {
			W->ringOn[r] = vcol[r][best] != 0;
			W->ring[r] = vcol[r][best] & 0xFFFFFF;
		}
		if (W->ringOn[r] != (last >= 0 ? W->ringOn[last] : W->ringOn[r]) || (last >= 0 && W->ringOn[r] && W->ring[r] != W->ring[last])) ++changes;
		clear += !W->ringOn[r];
		last = r;
	}
	/* the disc: two colours, squares of 4 */
	uint32_t col[2][4] = { { 0 } };
	int cnt[2][4] = { { 0 } };
	for (int y = 0; y < 144; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			int p = ((x / WW_TILE) + (y / WW_TILE)) & 1;
			uint32_t c = l3[y * GBA_W + x] & 0xFFFFFF;
			int k = 0;
			while (k < 3 && cnt[p][k] && col[p][k] != c) ++k;
			col[p][k] = c, ++cnt[p][k];
		}
	int match = 0;
	for (int p = 0; p < 2; ++p) {
		int best = 0;
		for (int k = 1; k < 4; ++k)
			if (cnt[p][k] > cnt[p][best]) best = k;
		W->chk[p] = col[p][best];
		match += cnt[p][best];
	}
	/* the console: the box round what the BG2 layer draws */
	int x0 = GBA_W, y0 = GBA_H, x1 = -1, y1 = -1;
	for (int i = 0; i < GBA_W * GBA_H; ++i)
		if ((l2[i] ^ back[i]) & 0xFFFFFF) {
			int x = i % GBA_W, y = i / GBA_W;
			if (x < x0) x0 = x;
			if (x > x1) x1 = x;
			if (y < y0) y0 = y;
			if (y > y1) y1 = y;
		}
	bool ok = changes >= 4 && clear >= 8 && match >= 144 * GBA_W * 95 / 100 && x1 >= x0 && x1 - x0 < 150 && y1 - y0 < 150 && x1 - x0 > 20 && y1 - y0 > 20;
	if (!ok) {
		fprintf(stderr, "wheel: not as expected (ring changes %d, clear radii %d, disc match %d%%, console %dx%d)\n", changes, clear, match * 100 / (144 * GBA_W), x1 - x0 + 1, y1 - y0 + 1);
		return false;
	}
	W->ix = x0, W->iy = y0, W->iw = x1 - x0 + 1, W->ih = y1 - y0 + 1;
	W->icon = calloc((size_t) W->iw * W->ih, 4);
	if (!W->icon) return false;
	for (int y = 0; y < W->ih; ++y)
		for (int x = 0; x < W->iw; ++x) {
			int i = (y0 + y) * GBA_W + x0 + x;
			if ((l2[i] ^ back[i]) & 0xFFFFFF) W->icon[y * W->iw + x] = l2[i] | 0xFF000000;
		}
	W->ok = true;
	return true;
}

static bool ww_wheel_capture(const char* rom, WwWheel* W, volatile bool* cancel) {
	Emu e;
	if (!emu_open(&e, rom, NULL, false)) return false;
	e.cancel = cancel;
	bool ok = false;
	int total = 0;
	/* power-on to the menu: some 800 frames, then it's let settle */
	for (int f = 0; f < 200 && !emu_cancelled(&e); ++f, ++total) emu_frame_blind(&e, 0);
	for (int i = 0; i < 11 && !emu_cancelled(&e); ++i) {
		for (int f = 0; f < 4; ++f, ++total) emu_frame_blind(&e, K_A);
		for (int f = 0; f < 36; ++f, ++total) emu_frame_blind(&e, 0);
	}
	for (int i = 0; i < 4 && !emu_cancelled(&e); ++i) {
		for (int f = 0; f < 4; ++f, ++total) emu_frame_blind(&e, K_START);
		for (int f = 0; f < 36; ++f, ++total) emu_frame_blind(&e, 0);
	}
	for (int f = 0; f < 119 && !emu_cancelled(&e); ++f, ++total) emu_frame_blind(&e, 0);
	if (!emu_cancelled(&e)) {
		emu_frame(&e, 0);
		size_t sz = emu_state_size(&e);
		void* st = malloc(sz);
		uint32_t* img = malloc((size_t) GBA_W * GBA_H * 4 * 4);
		if (st && img && emu_state_save(&e, st)) {
			uint32_t* back = img;
			for (int pass = 0; pass < 4; ++pass) {
				/* pass 0: the backdrop alone; 1 to 3: BG1, BG2, BG3 alone (layer switches take effect when the
				 * display registers are written: loading the state after setting them writes them) */
				emu_layers(&e, pass ? 1u << pass : 0);
				emu_state_load(&e, st);
				emu_frame(&e, 0);
				memcpy(img + (size_t) pass * GBA_W * GBA_H, e.fb, (size_t) GBA_W * GBA_H * 4);
			}
			emu_layers(&e, 0x1F);
			ok = ww_wheel_build(back, img + GBA_W * GBA_H, img + 2 * GBA_W * GBA_H, img + 3 * GBA_W * GBA_H, W);
		}
		free(st), free(img);
	}
	emu_close(&e);
	return ok;
}

static void* ww_wheel_thread(void* arg) {
	(void) arg;
	ww.wheelRunning = true;
	WwWheel W;
	bool ok = ww_wheel_capture(ww.rom, &W, &ww.wheelCancel);
	if (!ww.wheelCancel) {
		if (ok) {
			ww.wheel = W;
			ww.wheelReady = true;
		} else {
			ww.wheelFailed = true;
		}
	} else if (ok) free(W.icon);
	ww.wheelRunning = false;
	fprintf(stderr, "wheel: %s\n", ww.wheelCancel ? "called off" : ok ? "taken" : "not found");
	return NULL;
}

static void ww_wheel_stop(void) {
	if (ww.threadStarted) {
		ww.wheelCancel = true;
		pthread_join(ww.thread, NULL);
		ww.threadStarted = false;
	}
	ww.wheelCancel = false;
	free(ww.wheel.icon);
	memset(&ww.wheel, 0, sizeof(ww.wheel));
	ww.wheelReady = ww.wheelFailed = false;
}

/* the wheel turned to an angle (clockwise positive): the rings as they are (the same turned any way), the disc
 * and the console each pixel from where the turn brought it from; whatever the console doesn't cover is the disc */
static void ww_wheel_draw(const WwWheel* W, float deg, uint32_t* out) {
	float a = deg * (float) (M_PI / 180.0), ca = cosf(a), sa = sinf(a);
	for (int y = 0; y < GBA_H; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			float dx = x - WW_CX, dy = y - WW_CY;
			int r = (int) lrintf(hypotf(dx, dy));
			if (r >= WW_RN) r = WW_RN - 1;
			uint32_t c;
			if (W->ringOn[r]) c = W->ring[r];
			else {
				int sx = (int) lrintf(WW_CX + dx * ca + dy * sa), sy = (int) lrintf(WW_CY - dx * sa + dy * ca);
				int ix = sx - W->ix, iy = sy - W->iy;
				if (ix >= 0 && iy >= 0 && ix < W->iw && iy < W->ih && (W->icon[iy * W->iw + ix] >> 24)) c = W->icon[iy * W->iw + ix] & 0xFFFFFF;
				else c = W->chk[(((sx >= 0 ? sx : sx - (WW_TILE - 1)) / WW_TILE) + ((sy >= 0 ? sy : sy - (WW_TILE - 1)) / WW_TILE)) & 1];
			}
			out[y * GBA_W + x] = c | 0xFF000000;
		}
}

/* is the wheel what the second screen shows (Touch mode, once it's taken)? */
static bool ww_wheel_shown(void) {
	return ww.dial && ww.touch && ww.wheelReady && ww_can_touch();
}

/* ---- Gyro mode's second screen (WWT-2): the logo on white, tilting with the sensor's turning */
static void ww_logo_load(void) {
	/* (the logo is made in the background at the start: asked for again every half second until it's there) */
	if (ww.logo || (ww.logoTried && sec.frame < ww.logoRetry)) return;
	ww.logoTried = true;
	ww.logoRetry = sec.frame + 30;
	bool pixelArt = false;
	int sub = 1;
	SDL_Surface* lg = ui_logo_surface(cfg, romPath, emu.gameCode, emu.romCrc, &pixelArt, &sub);
	if (!lg) return;
	ww.logo = malloc((size_t) lg->w * lg->h * 4);
	if (ww.logo) {
		SDL_LockSurface(lg);
		for (int y = 0; y < lg->h; ++y) memcpy(ww.logo + (size_t) y * lg->w, (uint8_t*) lg->pixels + (size_t) y * lg->pitch, (size_t) lg->w * 4);
		SDL_UnlockSurface(lg);
		ww.logoW = lg->w, ww.logoH = lg->h;
		float fit = fminf(1.0f, fminf((float) GBA_W * sub / lg->w, (float) GBA_H * sub / lg->h));
		ww.logoK = (float) sub / fit; /* (source pixels to a screen pixel) */
	}
	SDL_FreeSurface(lg);
}

static bool ww_gyro_shown(void) {
	if (!ww.dial || ww.touch || !ww_can_touch()) return false;
	ww_logo_load();
	return ww.logo != NULL;
}

static void ww_gyro_draw(float deg, uint32_t* out) {
	float a = deg * (float) (M_PI / 180.0), ca = cosf(a), sa = sinf(a);
	for (int y = 0; y < GBA_H; ++y)
		for (int x = 0; x < GBA_W; ++x) {
			float dx = x + 0.5f - GBA_W / 2.0f, dy = y + 0.5f - GBA_H / 2.0f;
			int sx = (int) floorf((dx * ca + dy * sa) * ww.logoK + ww.logoW / 2.0f);
			int sy = (int) floorf((-dx * sa + dy * ca) * ww.logoK + ww.logoH / 2.0f);
			uint32_t c = 0xFFFFFFFF;
			if (sx >= 0 && sy >= 0 && sx < ww.logoW && sy < ww.logoH) {
				uint8_t* p = (uint8_t*) &ww.logo[sy * ww.logoW + sx]; /* RGBA bytes */
				int al = p[3], o = 0xFF;
				c = 0xFF000000 | (uint32_t) (o + (p[2] - o) * al / 255) << 16 | (uint32_t) (o + (p[1] - o) * al / 255) << 8 | (uint32_t) (o + (p[0] - o) * al / 255);
			}
			out[y * GBA_W + x] = c;
		}
}

static void ww_tex_make(bool* fresh) {
	for (int v = 0; v < nViews; ++v) {
		if (ww.tex[v] && ww.texRen[v] != views[v].ren) ww.tex[v] = NULL;
		if (!ww.tex[v]) {
			ww.tex[v] = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, GBA_H);
			ww.texRen[v] = views[v].ren;
			if (!ww.tex[v]) continue;
			SDL_SetTextureBlendMode(ww.tex[v], SDL_BLENDMODE_NONE);
			*fresh = true;
		}
	}
}

static void ww_gyro_update(void) {
	bool fresh = false;
	ww_tex_make(&fresh);
	if (!fresh && !ww.dirty && ww.shownTilt == ww.tilt) return;
	ww_gyro_draw(ww.tilt, ww.px);
	ww.shownTilt = ww.tilt, ww.shownAngle = 1000, ww.dirty = false;
	for (int v = 0; v < nViews; ++v)
		if (ww.tex[v]) ui_update_texture(ww.tex[v], ww.px, GBA_W * 4);
}

/* the wheel's picture for this frame, on every view's texture */
static void ww_wheel_update(void) {
	bool fresh = false;
	ww_tex_make(&fresh);
	if (!fresh && !ww.dirty && ww.shownAngle == ww.angle) return;
	ww_wheel_draw(&ww.wheel, ww.angle, ww.px);
	ww.shownAngle = ww.angle, ww.shownTilt = 1000, ww.dirty = false;
	for (int v = 0; v < nViews; ++v)
		if (ww.tex[v]) ui_update_texture(ww.tex[v], ww.px, GBA_W * 4);
}

/* ---- starting and ending (a game starts, a state loads, the game ends) */
static void ww_setup(const MapProfile* lp, const char* rom) {
	ww_wheel_stop();
	ww.dial = lp && lp->gyroDial;
	wwBarRow = ww.dial && nScreens > 1 && cfg->map;
	ww.touch = false, ww.down = false, ww.dragFrac = 0, ww.angle = 0, ww.tilt = 0, ww.resid = 0, ww.press = WWP_NONE, ww.dirty = true, ww.shownAngle = ww.shownTilt = 1000;
	free(ww.logo), ww.logo = NULL, ww.logoTried = false;
	if (!ww.dial) {
		ww_sensor_close();
		return;
	}
	ww_cfg_load();
	ww_sensor_open();
	snprintf(ww.rom, sizeof(ww.rom), "%s", rom);
	fprintf(stderr, "gyro: build 8: the buttons are drawn under the RetroAchievements banner, the logo is asked for until it is there\n");
	fprintf(stderr, "rumble: this game's motor is limited to short pulses (WWT-11)\n");
	fprintf(stderr, "gyro: dial on, sensitivity %d%%, sensor scale %d%%%s\n", ww.sens, ww.scale, ww.invert ? ", inverted" : "");
	if (ww_can_touch()) {
		/* the wheel is taken in the background, or inline in the tests (and where there are no threads) so that
		 * runs are exact */
#ifndef __wasi__
		if (!getenv("GBADS_SYNC")) {
			ww.threadStarted = pthread_create(&ww.thread, NULL, ww_wheel_thread, NULL) == 0;
			if (ww.threadStarted) return;
		}
#endif
		ww_wheel_thread(NULL);
	}
}

static void ww_free(void) {
	wwBarRow = false;
	free(ww.logo), ww.logo = NULL, ww.logoTried = false;
	ww_wheel_stop();
	ww_sensor_close();
	for (int v = 0; v < 2; ++v) {
		if (ww.tex[v]) SDL_DestroyTexture(ww.tex[v]);
		if (ww.cvTex[v]) SDL_DestroyTexture(ww.cvTex[v]);
		ww.tex[v] = ww.cvTex[v] = NULL;
	}
	ww.dial = false;
}

/* ---- the buttons and the slider (WWT-3, WWT-7, WWT-9), drawn on the quick bar's canvas at the second
 * screen's own pixel size, in the toggle button's look and place (GEN-14) but larger */
static int ww_ui_screen(void) {
	if (!ww_can_touch() || menuOpen || barOpen || barShown > 0) return -1;
	int s = 1 - game_screen();
	return s == ui_touch_screen() ? s : -1;
}

static SDL_Rect wwPause; /* (set by ww_ui_geom) */

/* the key (and Pause) in the canvas's pixels: along the top of the picture, so the bottom edge is free for the quick bar's
 * swipe. The key is where the contextual toggle's button goes (Settings: left, centre, right; GEN-14), Pause in the
 * opposite corner (bottom right... top right when the key is centred or at the left) */
static void ww_ui_geom(SDL_Rect* key) {
	bool right = !strcmp(cfg->toggleButton, "right"), centre = !strcmp(cfg->toggleButton, "center");
	int kx = right ? GBA_W - 6 - WW_KEY_W : centre ? (GBA_W - WW_KEY_W) / 2 : 6;
	*key = (SDL_Rect) { kx, 6, WW_KEY_W, WW_KEY_H };
	wwPause = (SDL_Rect) { right ? 6 : GBA_W - 6 - WW_KEY_W, 6, WW_KEY_W, WW_KEY_H };
}

/* a canvas rect on the screen (its own pixels), grown by pad GBA pixels */
static SDL_Rect ww_on_screen(int s, SDL_Rect r, int pad) {
	SDL_Rect pic = bar_pic_full(s);
	float u = (float) pic.w / GBA_W;
	return (SDL_Rect) { pic.x + (int) ((r.x - pad) * u), pic.y + (int) ((r.y - pad) * u), (int) ((r.w + 2 * pad) * u), (int) ((r.h + 2 * pad) * u) };
}

static bool ww_ui_hit(int s, int x, int y, int what) { /* 0 the key, 2 Pause (Touch mode only) */
	SDL_Rect key;
	ww_ui_geom(&key);
	SDL_Rect r = what == 2 && ww.touch ? wwPause : key;
	SDL_Rect sr = ww_on_screen(s, r, 3);
	SDL_Point pt = { x, y };
	return SDL_PointInRect(&pt, &sr);
}

static void draw_ww_ui(void) {
	int s = ww_ui_screen();
	if (s < 0) return;
	SDL_Rect key;
	ww_ui_geom(&key);
	cvBuf = ww.cv, cvRows = WW_ROWS;
	memset(ww.cv, 0, sizeof(ww.cv));
	cv_key(key, ww.press == WWP_BUTTON ? KEY_SEL : KEY_PLAIN);
	cv_label(key.x + key.w / 2, key.y + key.h / 2, true, ww.touch ? "Gyro!" : "Touch!", ui_key_ink(), key.w - 6);
	if (ww.touch) {
		cv_key(wwPause, ww.press == WWP_PAUSE ? KEY_SEL : KEY_PLAIN);
		cv_label(wwPause.x + wwPause.w / 2, wwPause.y + wwPause.h / 2, true, "Pause", ui_key_ink(), wwPause.w - 6);
	}
	cvBuf = barPx, cvRows = GBA_H;
	for (int v = 0; v < nViews; ++v) {
		if (ww.cvTex[v] && ww.cvTexRen[v] != views[v].ren) ww.cvTex[v] = NULL;
		if (!ww.cvTex[v]) {
			ww.cvTex[v] = SDL_CreateTexture(views[v].ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, GBA_W, WW_ROWS);
			ww.cvTexRen[v] = views[v].ren;
			if (!ww.cvTex[v]) continue;
			SDL_SetTextureBlendMode(ww.cvTex[v], SDL_BLENDMODE_BLEND);
			SDL_SetTextureScaleMode(ww.cvTex[v], SDL_ScaleModeNearest);
		}
		SDL_SetTextureAlphaMod(ww.cvTex[v], 189); /* 74%, as the toggle's button */
		ui_update_texture_plain(ww.cvTex[v], ww.cv, GBA_W * 4);
	}
	SDL_Rect pic = bar_pic_full(s), sr = screens[s].rect, clip = { sr.x, sr.y, sr.w, sr.h };
	SDL_Rect canvas = { pic.x, pic.y, pic.w, (int) ((float) pic.w / GBA_W * WW_ROWS + 0.5f) };
	ui_clip(s, &clip);
	ui_image(s, ww.cvTex, GBA_W, WW_ROWS, "stretch", &canvas);
	ui_clip(s, NULL);
}

/* ---- the sensitivity slider (WWT-7), in the quick bar's row in the place of the Controls / Sun row (game.c bar_pick_h) */
static SDL_Rect ww_bar_track_g(void) {
	SDL_Rect b = bar_rect_g();
	return (SDL_Rect) { b.x + 72, b.y + 13, 112, 3 };
}

static void ww_bar_draw(void) {
	SDL_Rect b = bar_rect_g(), t = ww_bar_track_g();
	int cy = t.y + t.h / 2;
	cv_label(b.x + 38, cy, true, "Sensitivity", G_TEXT, 62);
	cv_round((SDL_Rect) { t.x - 1, t.y - 1, t.w + 2, t.h + 2 }, 2, (SDL_Color) { 20, 16, 36, 255 });
	int kx = t.x + (ww.sens - WW_SENS_MIN) * (t.w - 1) / (WW_SENS_MAX - WW_SENS_MIN);
	cv_rect((SDL_Rect) { t.x, t.y, kx - t.x, t.h }, (SDL_Color) { 150, 150, 190, 255 });
	cv_key((SDL_Rect) { kx - 3, cy - 7, 7, 14 }, ww.press == WWP_SLIDER ? KEY_SEL : KEY_PLAIN);
	char pc[8];
	snprintf(pc, sizeof(pc), "%d%%", ww.sens);
	cv_label(t.x + t.w + 20, cy, true, pc, G_TEXT, 28);
}

static int ww_slider_value(int s, int x) {
	SDL_Rect sr = bar_to_screen(s, ww_bar_track_g());
	float f = ww_clampf((float) (x - sr.x) / (float) sr.w, 0, 1);
	return WW_SENS_MIN + (int) lrintf(f * (WW_SENS_MAX - WW_SENS_MIN));
}

/* a touch down on the bar's slider row: true if it was there (it then doesn't open the menu) */
static bool ww_bar_tap(int s, int x, int y) {
	if (!wwBarRow || !barOpen || s != ui_touch_screen()) return false;
	SDL_Point pt = { x, y };
	SDL_Rect br = bar_rect_g();
	SDL_Rect row = bar_to_screen(s, (SDL_Rect) { br.x, br.y + 3, br.w - 14, BAR_PICK_H - 2 }); /* (the pin is in the corner) */
	if (!SDL_PointInRect(&pt, &row)) return false;
	SDL_Rect tr = bar_to_screen(s, (SDL_Rect) { ww_bar_track_g().x - 4, br.y + 3, 112 + 8, BAR_PICK_H - 2 });
	if (SDL_PointInRect(&pt, &tr)) {
		ww.press = WWP_SLIDER;
		ww.sens = ww_slider_value(s, x);
	}
	return true;
}

/* ---- touches (WWT-5, WWT-8): true when the event is the dial's or one of the buttons' */
static bool ww_pointer(const Pointer* p) {
	if (ww.press == WWP_SLIDER && (p->kind == 2 || p->kind == 3)) {
		/* (the slider in the quick bar, held) */
		int s = ui_touch_screen();
		if (p->kind == 3) ww.sens = ww_slider_value(s, p->x);
		else {
			ww.press = WWP_NONE;
			ww_cfg_save();
			fprintf(stderr, "frame %llu: dial: sensitivity %d%%\n", (unsigned long long) sec.frame, ww.sens);
		}
		return true;
	}
	if (!ww_can_touch() || barOpen || barShown > 0) return false;
	int ts = ui_touch_screen();
	float u = ui_px(ts);
	if (p->kind == 1) {
		int s = ww_ui_screen();
		if (s >= 0 && p->screen == s) {
			if (ww_ui_hit(s, p->x, p->y, 0)) {
				ww.press = WWP_BUTTON;
				return true;
			}
			if (ww.touch && ww_ui_hit(s, p->x, p->y, 2)) {
				ww.press = WWP_PAUSE;
				return true;
			}
		}
		if (!ww.touch || p->screen != ts) return false; /* (Gyro mode: a tap opens the menu as ever) */
		if (p->y >= screens[ts].rect.h - u * 36) return false; /* (the bottom edge: the quick bar's swipe) */
		ww.press = WWP_DIAL, ww.down = true, ww.dragFrac = 0;
		ww.pressY = p->x; /* (where the finger went down: across) */
		fprintf(stderr, "frame %llu: dial: grabbed\n", (unsigned long long) sec.frame);
		return true;
	}
	if (ww.press == WWP_NONE) return false;
	if (p->kind == 3) {
		if (ww.press == WWP_DIAL) ww.dragFrac = (float) (p->x - ww.pressY) / (screens[ts].rect.w * 0.5f);
		return true;
	}
	if (p->kind == 2) {
		int press = ww.press;
		ww.press = WWP_NONE;
		if (press == WWP_DIAL) {
			ww.down = false;
			fprintf(stderr, "frame %llu: dial: let go at %.1f degrees\n", (unsigned long long) sec.frame, ww.angle);
		} else if (press == WWP_BUTTON) {
			int s = ww_ui_screen();
			if (s >= 0 && ww_ui_hit(s, p->x, p->y, 0)) ww_switch();
		} else if (press == WWP_PAUSE) {
			int s = ww_ui_screen();
			if (s >= 0 && ww_ui_hit(s, p->x, p->y, 2)) {
				fprintf(stderr, "frame %llu: pause button\n", (unsigned long long) sec.frame);
				open_menu();
				ignorePointerUp = true;
			}
		}
		return true;
	}
	return false;
}

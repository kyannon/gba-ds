/* mGBA's gba.h uses GBA_H as its include guard, which emu.h defines as the
 * screen height: include it first, then let emu.h take the name over. */
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gb/gb.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/gba/cheats.h>
#include <mgba/internal/gba/bios.h>
#include <mgba/internal/gba/serialize.h>
#include <mgba/internal/gba/cart/gpio.h>
#undef GBA_H
#include "emu.h"

#include <mgba/core/core.h>
#include <mgba/core/cheats.h>
#include <mgba/core/serialize.h>
#include <mgba/core/rewind.h>
#include <mgba/core/config.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include "patch.h"
#include "profiles.h"
#include <stdarg.h>
#include <stddef.h>
#include <sys/stat.h>
#include <pthread.h>
#include <ctype.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <fcntl.h>

static void quiet_log(struct mLogger* l, int cat, enum mLogLevel level, const char* fmt, va_list args) {
	(void) l;
	(void) cat;
	if (level & (mLOG_FATAL | mLOG_ERROR)) {
		vfprintf(stderr, fmt, args);
		fputc('\n', stderr);
	}
}
static struct mLogger quietLogger = { .log = quiet_log };

#ifndef __wasi__ /* (the web build has no threads) */
/* ------------------------------------------------- battery saves, async
 * When a game writes its save, mGBA flushes it from the emulation thread a
 * few frames later: futimens + msync + fsync. On Android's shared storage
 * the fsync took ~200 ms, a visible freeze at every save point. The save
 * stays a mapped file as before (so what the game writes is in the file
 * right away); only the flush to the card moves to a worker thread.
 * Linux too since v1.5.3 (issue #50): on ROCKNIX an SD card, or the merged
 * /storage/roms folder, can hold an fsync just as long. */
#include <unistd.h>
#ifdef __ANDROID__
#include "android.h"
#endif

typedef struct {
	struct VFile d;
	struct VFile* inner;
	char path[1024];
	pthread_t thread;
	pthread_mutex_t lock;
	pthread_cond_t cond;
	bool started, pending, quit;
} AsyncSave;

static void* async_save_worker(void* arg) {
	AsyncSave* a = arg;
#ifdef __ANDROID__
	android_pin_worker_thread("save writer"); /* threads start on their creator's cores: not the game's */
#endif
	pthread_mutex_lock(&a->lock);
	for (;;) {
		while (!a->pending && !a->quit) pthread_cond_wait(&a->cond, &a->lock);
		if (!a->pending) break; /* quit, nothing left to flush */
		a->pending = false;
		pthread_mutex_unlock(&a->lock);
		int fd = open(a->path, O_RDWR); /* fsync flushes the file, whichever descriptor */
		if (fd >= 0) {
			futimens(fd, NULL);
			if (fsync(fd) != 0) fprintf(stderr, "save: couldn't flush %s\n", a->path);
			else if (getenv("GBADS_DEBUG")) fprintf(stderr, "save: flushed %s\n", a->path);
			close(fd);
		}
		pthread_mutex_lock(&a->lock);
	}
	pthread_mutex_unlock(&a->lock);
	return NULL;
}

static bool as_sync(struct VFile* vf, void* buffer, size_t size) {
	(void) buffer;
	(void) size;
	AsyncSave* a = (AsyncSave*) vf;
	pthread_mutex_lock(&a->lock);
	if (!a->started) a->started = pthread_create(&a->thread, NULL, async_save_worker, a) == 0;
	bool ok = a->started;
	if (ok) {
		a->pending = true;
		pthread_cond_signal(&a->cond);
	}
	pthread_mutex_unlock(&a->lock);
	if (!ok) return a->inner->sync(a->inner, buffer, size);
	return true;
}

static bool as_close(struct VFile* vf) {
	AsyncSave* a = (AsyncSave*) vf;
	if (a->started) {
		pthread_mutex_lock(&a->lock);
		a->quit = true;
		pthread_cond_signal(&a->cond);
		pthread_mutex_unlock(&a->lock);
		pthread_join(a->thread, NULL); /* any flush asked for is done first */
	}
	bool ok = a->inner->close(a->inner);
	pthread_mutex_destroy(&a->lock);
	pthread_cond_destroy(&a->cond);
	free(a);
	return ok;
}

static off_t as_seek(struct VFile* vf, off_t o, int w) { return ((AsyncSave*) vf)->inner->seek(((AsyncSave*) vf)->inner, o, w); }
static ssize_t as_read(struct VFile* vf, void* b, size_t n) { return ((AsyncSave*) vf)->inner->read(((AsyncSave*) vf)->inner, b, n); }
static ssize_t as_readline(struct VFile* vf, char* b, size_t n) { return ((AsyncSave*) vf)->inner->readline(((AsyncSave*) vf)->inner, b, n); }
static ssize_t as_write(struct VFile* vf, const void* b, size_t n) { return ((AsyncSave*) vf)->inner->write(((AsyncSave*) vf)->inner, b, n); }
static void* as_map(struct VFile* vf, size_t n, int f) { return ((AsyncSave*) vf)->inner->map(((AsyncSave*) vf)->inner, n, f); }
static void as_unmap(struct VFile* vf, void* m, size_t n) { ((AsyncSave*) vf)->inner->unmap(((AsyncSave*) vf)->inner, m, n); }
static void as_truncate(struct VFile* vf, size_t n) { ((AsyncSave*) vf)->inner->truncate(((AsyncSave*) vf)->inner, n); }
static ssize_t as_size(struct VFile* vf) { return ((AsyncSave*) vf)->inner->size(((AsyncSave*) vf)->inner); }

static struct VFile* async_save_open(const char* path) {
	struct VFile* inner = VFileOpen(path, O_CREAT | O_RDWR);
	if (!inner) return NULL;
	AsyncSave* a = calloc(1, sizeof(*a));
	if (!a) return inner;
	a->inner = inner;
	snprintf(a->path, sizeof(a->path), "%s", path);
	pthread_mutex_init(&a->lock, NULL);
	pthread_cond_init(&a->cond, NULL);
	a->d = (struct VFile) { as_close, as_seek, as_read, as_readline, as_write, as_map, as_unmap, as_truncate, as_size, as_sync };
	return &a->d;
}
#endif

static void split_resync(Emu* e);
static void split_release(Emu* e);
static void wide_resync(Emu* e);
static void wide_release(Emu* e);

/* ---------------------------------------------- patches (GEN-9) */
/* Which patches the game applies (emu_patch_plan): every core that opens
 * that ROM gets them (the game, the hidden copy for map captures, logos,
 * twins). Any other ROM, or none planned: the patch file beside it, if any. */
#define PLAN_MAX 8
uint32_t doCrc32(const void* buf, size_t size); /* (libmgba's: its header wants zlib.h) */
static pthread_mutex_t patchedLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char rom[1024];
	char files[PLAN_MAX][1024];
	int n;
} plan;
/* The last patched ROM, so it's patched once: keyed by the ROM's and every
 * patch's paths, sizes and times. */
static struct {
	char key[PLAN_MAX * 1100 + 1100];
	int result;
	char names[256];
	char skipped[256]; /* (PATCH_PARTIAL) the ones left out */
	uint32_t fileCrc;
	uint8_t* data; /* the patched ROM (NULL unless something was applied) */
	size_t size;
} patched;

static struct { int colors; bool usePal; uint32_t rgb[4]; bool mono; bool border; } gbOpt = { 1, false, { 0 }, false, false };
void emu_gb_border_wanted(bool on) {
	gbOpt.border = on;
}
void emu_gb_colours(int colors, const uint32_t* rgb, bool mono) {
	gbOpt.colors = colors;
	gbOpt.usePal = rgb != NULL;
	if (rgb) memcpy(gbOpt.rgb, rgb, sizeof(gbOpt.rgb));
	gbOpt.mono = mono;
}

void emu_patch_plan(const char* rom, char files[][1024], int n) {
	pthread_mutex_lock(&patchedLock);
	snprintf(plan.rom, sizeof(plan.rom), "%s", rom ? rom : "");
	plan.n = 0;
	for (int i = 0; i < n && i < PLAN_MAX; ++i) snprintf(plan.files[plan.n++], sizeof(plan.files[0]), "%s", files[i]);
	pthread_mutex_unlock(&patchedLock);
}

static uint8_t* read_file(const char* path, size_t* n) {
	struct stat st;
	*n = 0;
	if (stat(path, &st) || st.st_size <= 0 || st.st_size > 64 * 1024 * 1024) return NULL;
	FILE* f = fopen(path, "rb");
	uint8_t* p = f ? malloc((size_t) st.st_size) : NULL;
	if (p && fread(p, 1, (size_t) st.st_size, f) == (size_t) st.st_size) *n = (size_t) st.st_size;
	else {
		free(p);
		p = NULL;
	}
	if (f) fclose(f);
	return p;
}

/* The patched ROM for romPath, or NULL: *result says why (PATCH_NONE: no
 * patch for it). Several patches apply in order; one that can't be applied
 * is left out (the result says so) and the rest go on. Returns a copy the
 * caller frees. patchName (256 bytes): the patches' file names, " + "
 * between. */
static uint8_t* patched_with(const char* romPath, char files[][1024], int n, size_t* size, int* result, char* patchName,
                             uint32_t* fileCrc, char* skipped);
#ifdef __wasi__
static bool openUnpatched; /* (no threads there) */
#else
static __thread bool openUnpatched;
#endif
void emu_open_unpatched(bool on) {
	openUnpatched = on;
}
static uint8_t* patched_rom(const char* romPath, size_t* size, int* result, char* patchName, uint32_t* fileCrc,
                            char* skipped) {
	if (openUnpatched) {
		*result = PATCH_NONE;
		return NULL;
	}
	char files[PLAN_MAX][1024];
	int n = 0;
	bool planned = false;
	pthread_mutex_lock(&patchedLock);
	if (plan.rom[0] && !strcmp(plan.rom, romPath)) {
		planned = true;
		for (int i = 0; i < plan.n; ++i) memcpy(files[n++], plan.files[i], sizeof(files[0]));
	}
	pthread_mutex_unlock(&patchedLock);
	if (!planned && patch_find(romPath, files[0], sizeof(files[0]))) n = 1; /* (beside the ROM) */
	return patched_with(romPath, files, n, size, result, patchName, fileCrc, skipped);
}

/* (the same with the patches given) */
static uint8_t* patched_with(const char* romPath, char files[][1024], int n, size_t* size, int* result, char* patchName,
                             uint32_t* fileCrc, char* skipped) {
	*result = PATCH_NONE;
	if (n > PLAN_MAX) n = PLAN_MAX;
	if (!n) return NULL;
	char key[sizeof(patched.key)];
	struct stat st;
	bool romOk = stat(romPath, &st) == 0;
	int k = snprintf(key, sizeof(key), "%s|%lld|%lld", romPath, romOk ? (long long) st.st_size : -1LL,
	                 romOk ? (long long) st.st_mtime : -1LL);
	for (int i = 0; i < n && k < (int) sizeof(key); ++i) {
		bool ok = stat(files[i], &st) == 0;
		k += snprintf(key + k, sizeof(key) - (size_t) k, "|%s|%lld|%lld", files[i], ok ? (long long) st.st_size : -1LL,
		              ok ? (long long) st.st_mtime : -1LL);
	}
	pthread_mutex_lock(&patchedLock);
	if (strcmp(patched.key, key)) {
		free(patched.data);
		memset(&patched, 0, sizeof(patched));
		snprintf(patched.key, sizeof(patched.key), "%s", key);
		patched.result = PATCH_UNREADABLE;
		size_t romN = 0;
		uint8_t* rom = emu_rom_read_all(romPath, &romN);
		int applied = 0, failed = PATCH_NONE;
		if (rom) {
			patched.fileCrc = doCrc32(rom, romN);
			for (int i = 0; i < n; ++i) {
				size_t pn = 0;
				uint8_t* p = read_file(files[i], &pn);
				uint8_t* out = NULL;
				size_t outN = 0;
				int r = p ? patch_apply(p, pn, rom, romN, &out, &outN) : PATCH_UNREADABLE;
				free(p);
				fprintf(stderr, "patch %s: %s\n", patch_name(files[i]),
				        r == PATCH_APPLIED     ? "applied"
				        : r == PATCH_WRONG_ROM ? "made for another ROM, not applied"
				                               : "can't be read, not applied");
				if (r == PATCH_APPLIED) {
					free(rom);
					rom = out;
					romN = outN;
					size_t l = strlen(patched.names);
					snprintf(patched.names + l, sizeof(patched.names) - l, "%s%s", l ? " + " : "", patch_name(files[i]));
					++applied;
				} else {
					if (failed == PATCH_NONE) failed = r;
					size_t l = strlen(patched.skipped);
					snprintf(patched.skipped + l, sizeof(patched.skipped) - l, "%s%s", l ? " + " : "", patch_name(files[i]));
				}
			}
			/* (none applied: why the first wasn't, under its name) */
			patched.result = failed == PATCH_NONE ? PATCH_APPLIED : applied ? PATCH_PARTIAL : failed;
			if (!applied) snprintf(patched.names, sizeof(patched.names), "%s", patched.skipped);
			if (applied) {
				patched.data = rom;
				patched.size = romN;
				rom = NULL;
			}
		}
		free(rom);
	}
	*result = patched.result;
	if (fileCrc) *fileCrc = patched.fileCrc;
	if (patchName) snprintf(patchName, 256, "%s", patched.names);
	if (skipped) snprintf(skipped, 256, "%s", patched.skipped);
	uint8_t* copy = NULL;
	if (patched.data && (copy = malloc(patched.size))) {
		memcpy(copy, patched.data, patched.size);
		*size = patched.size;
	}
	pthread_mutex_unlock(&patchedLock);
	return copy;
}

uint8_t* emu_rom_read_patched(const char* path, char files[][1024], int n, size_t* size, int* patch, char* patchName) {
	int r;
	uint8_t* p = patched_with(path, files, n, size, &r, patchName, NULL, NULL);
	if (patch) *patch = r;
	return p ? p : emu_rom_read_all(path, size);
}

uint8_t* emu_rom_read_played(const char* path, size_t* size, int* patch, char* patchName, uint32_t* fileCrc) {
	int r;
	uint8_t* p = patched_rom(path, size, &r, patchName, fileCrc, NULL);
	if (patch) *patch = r;
	if (p) return p;
	uint8_t* rom = emu_rom_read_all(path, size);
	if (rom && fileCrc) *fileCrc = doCrc32(rom, *size);
	return rom;
}

/* The game cartridge's sun sensor (Boktai): there's none on the device, so
 * the sunlight the games are given is a setting (solar_level, 0-10, as
 * mGBA's own levels). Every core reads it: the game, the hidden copies, the
 * logo's. */
int emu_solar_level = 6;
static void lux_sample(struct GBALuminanceSource* lux) {
	(void) lux;
}
static uint8_t lux_read(struct GBALuminanceSource* lux) {
	(void) lux;
	int level = emu_solar_level < 0 ? 0 : emu_solar_level > 10 ? 10 : emu_solar_level;
	int value = 0x16 + (level > 0 ? GBA_LUX_LEVELS[level - 1] : 0);
	return (uint8_t) (0xFF - value);
}
/* The GBA core's own state, or NULL when the game is a Game Boy / Game Boy Color one (issue #77):
 * everything below that reaches into mGBA's GBA structures (sprite hiding, the HUD split, I/O
 * registers, VRAM, OAM) does nothing for those, which have no profiles. */
static struct GBA* gba_of(const Emu* e) {
	return e->core && e->platform == mPLATFORM_GBA ? e->core->board : NULL;
}

/* (testing: GBADS_RTC_FIXED=<unix seconds> holds the cartridge clock still, for games
 * that look at the time of day: Boktai's areas change with it. A state carries its
 * clock's setting, so this is applied again after each load.) */
static void rtc_pin(Emu* e) {
	const char* t = getenv("GBADS_RTC_FIXED");
	if (!t) return;
	e->core->rtc.override = RTC_FIXED;
	e->core->rtc.value = atoll(t) * 1000;
}

/* Background work done on the caller's thread, with no thread of its own:
 * map captures (mapcap.c, run in mapcap_request), logo extraction (logo.c),
 * the profiles' own workers (profiles.c: Mega Man Battle Network's map,
 * Baldur's Gate's, Mario Pinball Land's counters) and the climb copy's
 * pictures, each waited for (climb.c). Exact and repeatable (the same
 * pictures at the same frames), at the cost of frames that take longer:
 * the regression tests run so, and a libretro core can choose it (run-ahead,
 * or a frontend that wants no threads). Off unless set; GBADS_SYNC=1 sets it
 * at the start (as the tests do). (The battery save's writer, emu.c, and the
 * Pinball copies' workers, workers.c, run as they always do: the copies'
 * frames are waited for each frame either way.) */
static int syncWork = -1;
void emu_set_sync_work(bool on) {
	syncWork = on;
}
bool emu_sync_work(void) {
	if (syncWork < 0) {
		const char* e = getenv("GBADS_SYNC");
		syncWork = e && atoi(e);
	}
	return syncWork;
}

/* Does the cartridge have a sun sensor (Boktai 1, 2 and 3)? mGBA sets the flag from its
 * game database (or the header's GPIO probe) when the ROM loads. */
bool emu_has_solar(Emu* e) {
	struct GBA* gba = gba_of(e);
	return gba && (gba->memory.hw.devices & HW_LIGHT_SENSOR) != 0;
}

char emu_bios_path[512];

/* The BIOS file read once (16 KB) and kept for the run: each core maps it, so it must stay. */
static uint8_t* biosData;
static char biosDataPath[512];
static pthread_mutex_t biosLock = PTHREAD_MUTEX_INITIALIZER;

static uint8_t* bios_read(const char* path, BiosFile* why) {
	*why = BIOS_FILE_MISSING;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	uint8_t* buf = malloc(GBA_SIZE_BIOS + 1);
	size_t n = buf ? fread(buf, 1, GBA_SIZE_BIOS + 1, f) : 0;
	fclose(f);
	if (n != GBA_SIZE_BIOS) {
		free(buf);
		*why = BIOS_FILE_SIZE;
		return NULL;
	}
	struct VFile* vf = VFileFromConstMemory(buf, GBA_SIZE_BIOS);
	bool ok = vf && GBAIsBIOS(vf);
	if (vf) vf->close(vf);
	if (!ok) {
		free(buf);
		*why = BIOS_FILE_NOT_BIOS;
		return NULL;
	}
	if (GBAChecksum((uint32_t*) buf, GBA_SIZE_BIOS) == GBA_DEBUG_BIOS_CHECKSUM) { /* (mGBA refuses it) */
		free(buf);
		*why = BIOS_FILE_BROKEN;
		return NULL;
	}
	*why = BIOS_FILE_OK;
	return buf;
}

BiosFile emu_bios_check(const char* path) {
	BiosFile why;
	free(bios_read(path, &why));
	return why;
}

/* Rumble (issue #83): mGBA's integrator turns the game's on/off pin writes into a 0..1 share
 * of each frame; we keep the last one for the app to hand to the motor. */
typedef struct {
	struct mRumbleIntegrator integrator;
	volatile float level;
} EmuRumble;

static void emu_rumble_set(struct mRumbleIntegrator* i, float v) {
	((EmuRumble*) i)->level = v;
}

float emu_rumble_level(const Emu* e) {
	return e->rumble ? ((EmuRumble*) e->rumble)->level : 0.0f;
}

/* Cartridge rumble for games mGBA doesn't know (issue #83). A romhack's rumble patch drives the
 * cartridge's motor pin (GPIO pin 3) as Drill Dozer does, but mGBA
 * - only listens to that pin for the game codes in its database (Pokemon Pinball's
 *   "CartRumble" patch keeps BPPE), and keeps that device list in states;
 * - drops 8-bit stores to the cartridge ("Unimplemented memory Store8"), where a real cartridge
 *   bus takes a byte as a 16-bit write of it twice; that patch writes the pins with strb.
 * - keeps a pin's last output level after the game makes it an input, where the cartridge's motor
 *   has nothing driving it and stops (Superstar Saga's rumble patch stops by writing 0 to the
 *   direction register and leaves the data register at 8, Nov 24, 2026: "never stopped rumbling").
 * So the game being played (and only it) has its stores to the three GPIO registers looked at:
 * the first one switches the motor pin on (unless the cartridge has a sensor on that port), an
 * 8-bit one is passed on as that 16-bit write, and after each the motor is off while pin 3 is not
 * an output. A game that never writes there is untouched: no device added, the ROM not copied. */
static void (*mgbaStore8)(struct ARMCore*, uint32_t, int8_t, int*);
static void (*mgbaStore16)(struct ARMCore*, uint32_t, int16_t, int*);

static bool gpio_listen(struct ARMCore* cpu, uint32_t address) {
	if ((address >> 24) != 0x08 || !IS_GPIO_REGISTER(address & 0xFFFFFE)) return false;
	struct GBACartridgeHardware* hw = &((struct GBA*) cpu->master)->memory.hw;
	if (!(hw->devices & (HW_RUMBLE | HW_LIGHT_SENSOR | HW_GYRO | HW_TILT))) GBAHardwareInitRumble(hw);
	return (hw->devices & HW_GPIO) != 0;
}

/* The motor pin as hardware has it: a pin the game has made an input drives nothing, so the motor
 * is off whatever the data register last held (mGBA keeps that bit as the output level). */
static void gpio_settle(struct ARMCore* cpu) {
	struct GBA* gba = (struct GBA*) cpu->master;
	struct GBACartridgeHardware* hw = &gba->memory.hw;
	if (!(hw->devices & HW_RUMBLE) || (hw->direction & 8) || !(hw->pinState & 8)) return;
	hw->pinState &= ~8;
	if (hw->gpioBase && hw->readWrite) ((uint16_t*) hw->gpioBase)[0] = hw->pinState; /* (the ROM's readable copy; little-endian hosts only, as mGBA's own) */
	if (gba->rumble) {
		int32_t now = mTimingCurrentTime(&gba->timing);
		gba->rumble->setRumble(gba->rumble, false, now - gba->lastRumble);
		gba->lastRumble = now;
	}
}

static void gpio_store8(struct ARMCore* cpu, uint32_t address, int8_t value, int* cycleCounter) {
	bool gpio = gpio_listen(cpu, address);
	if (gpio) {
		uint8_t b = (uint8_t) value;
		GBAHardwareGPIOWrite(&((struct GBA*) cpu->master)->memory.hw, address & 0xFFFFFE, (uint16_t) (b | b << 8));
		gpio_settle(cpu);
	}
	mgbaStore8(cpu, address, value, cycleCounter); /* (its wait states; it stores nothing there) */
}

static void gpio_store16(struct ARMCore* cpu, uint32_t address, int16_t value, int* cycleCounter) {
	bool gpio = gpio_listen(cpu, address);
	mgbaStore16(cpu, address, value, cycleCounter);
	if (gpio) gpio_settle(cpu);
}

static struct GBALuminanceSource luxSource ={ .sample = lux_sample, .readLuminance = lux_read };

/* The cartridge's gyro (WarioWare: Twisted!; mGBA knows it by game code). Every GBA core has a
 * source that reads what emu_set_gyro last gave it, 0 (the sensor's resting value) until then, so
 * the hidden copies' games sit still. The value is mGBA's own: the cartridge reads its top
 * bits (a 12-bit sample, 0x700 at rest, 2^21 to a count). Only the game being played is given
 * anything (src/ww_twisted.inc). */
typedef struct { struct mRotationSource d; int32_t value; } EmuGyro;
static int32_t gyro_read_z(struct mRotationSource* s) {
	return ((EmuGyro*) s)->value;
}
static int32_t gyro_read_tilt(struct mRotationSource* s) {
	(void) s;
	return 0;
}
void emu_set_gyro(Emu* e, int32_t value) {
	if (e->gyro) ((EmuGyro*) e->gyro)->value = value;
}

static void gb_border_snapshot(Emu* e);

bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio) {
	memset(e, 0, sizeof(*e));
	mLogSetDefaultLogger(&quietLogger);
	e->core = mCoreFind(romPath);
	if (!e->core) {
		fprintf(stderr, "mCoreFind failed for %s\n", romPath);
		return false;
	}
	if (!e->core->init(e->core)) {
		fprintf(stderr, "core init failed\n");
		return false;
	}
	e->platform = (int) e->core->platform(e->core);
	e->gbKind = 0;
	bool sgbHeader = false; /* the cartridge says it supports the Super Game Boy (0x146 = 3) */
	if (e->platform == mPLATFORM_GB) {
		uint8_t h[EMU_HEADER_MAX];
		if (emu_rom_header(romPath, h, sizeof(h))) {
			e->gbKind = h[0x143] == 0xC0 ? 2 : h[0x143] == 0x80 ? 1 : 0;
			sgbHeader = h[0x146] == 0x03 && h[0x143] != 0xC0;
			e->gbSgbHeader = sgbHeader;
		}
	}
	mCoreInitConfig(e->core, NULL);
	struct mCoreOptions opts = { .useBios = false, .volume = 0x100, .audioBuffers = 1024 };
	mCoreConfigLoadDefaults(&e->core->config, &opts);
	if (e->platform == mPLATFORM_GB) {
		/* A Game Boy or Game Boy Color game as the GBA itself played it (issue #77): on the GBA's
		 * Game Boy Color hardware (AGB), whatever the cartridge (not as a plain DMG, and not as a
		 * Super Game Boy with its border), and a DMG-only game in the colours a Game Boy Color
		 * gave it (mGBA's table of the colour boot ROM's palettes, no boot ROM needed). Settings
		 * for the other models and colours come later (they are these same mGBA options). */
		mCoreConfigSetDefaultValue(&e->core->config, "gb.model", "AGB");
		/* (GB-11) a game that supports the Super Game Boy (header 0x146 = 3) runs as on one, without its
		 * border (sgb.borders below), so it sends its own colours to it: by default (Game's own) and with
		 * the Super Game Boy palette (GB-6); a dual-mode one only while it is kept in monochrome (GB-7).
		 * Another palette (DMG, Pocket, Light, Grayscale) is the Game Boy Color hardware with those shades.
		 * Otherwise the Game Boy Color hardware (GB-1). */
		bool sgbOk = sgbHeader && (e->gbKind == 0 || (e->gbKind == 1 && gbOpt.mono));
		/* (GB-12) with the border it is a Super Game Boy whatever the palette says (the colours are its) */
		bool border = gbOpt.border && sgbOk;
		bool asSgb = sgbOk && (border || gbOpt.colors == 1 || gbOpt.colors == 3);
		e->gbSgb = asSgb;
		e->gbBorder = border;
		mCoreConfigSetDefaultValue(&e->core->config, "sgb.model", asSgb ? "SGB" : "AGB");
		mCoreConfigSetDefaultValue(&e->core->config, "cgb.model", "AGB");
		mCoreConfigSetDefaultValue(&e->core->config, "cgb.hybridModel", gbOpt.mono ? "DMG" : "AGB"); /* (GB-7) */
		mCoreConfigSetDefaultValue(&e->core->config, "cgb.sgbModel", asSgb && gbOpt.mono ? "SGB" : "AGB");
		mCoreConfigSetDefaultIntValue(&e->core->config, "gb.colors", asSgb ? 0 : gbOpt.colors); /* (GB-6: 1 is GB_COLORS_CGB; none for a game running as an SGB: its own packets colour it, a table would override them) */
		if (gbOpt.usePal) {
			for (int i = 0; i < 12; ++i) {
				char key[16];
				snprintf(key, sizeof(key), "gb.pal[%d]", i);
				mCoreConfigSetDefaultIntValue(&e->core->config, key, (int) gbOpt.rgb[i & 3]); /* background, then the two sprite palettes */
			}
		}
		mCoreConfigSetDefaultIntValue(&e->core->config, "sgb.borders", e->gbBorder ? 1 : 0);
	}
	mCoreLoadConfig(e->core);

	e->fb = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	if (!e->fb) {
		emu_close(e);
		return false;
	}
	e->picX = e->picY = 0, e->picW = GBA_W, e->picH = GBA_H;
	if (e->platform == mPLATFORM_GB) { /* (160x144: not baseVideoSize, which is the Super Game Boy border's 256x224; borders are off) */
		e->picW = 160, e->picH = 144;
		e->picX = (GBA_W - e->picW) / 2, e->picY = (GBA_H - e->picH) / 2;
	}
	if (e->gbBorder) { /* (GB-12) the core draws the border and the window in a 256x224 picture of its own */
		e->sgbFb = calloc(SGB_W * SGB_H, sizeof(uint32_t));
		if (!e->sgbFb) {
			emu_close(e);
			return false;
		}
		e->core->setVideoBuffer(e->core, (mColor*) e->sgbFb, SGB_W);
	} else {
		e->core->setVideoBuffer(e->core, (mColor*) (e->fb + e->picY * GBA_W + e->picX), GBA_W);
	}
	if (e->platform == mPLATFORM_GBA) e->core->setPeripheral(e->core, mPERIPH_GBA_LUMINANCE, &luxSource);
	if (e->platform == mPLATFORM_GBA) {
		EmuGyro* g = calloc(1, sizeof(*g));
		if (g) {
			g->d.readGyroZ = gyro_read_z;
			g->d.readTiltX = g->d.readTiltY = gyro_read_tilt;
			e->gyro = g;
			e->core->setPeripheral(e->core, mPERIPH_ROTATION, &g->d);
		}
	}
	if (audio || getenv("GBADS_HARNESS_RUMBLE")) { /* only the game being played has a motor; the hidden copies stay quiet (the harness asks for one with GBADS_HARNESS_RUMBLE: its `rumble` command) */
		EmuRumble* r = calloc(1, sizeof(*r));
		if (r) {
			mRumbleIntegratorInit(&r->integrator);
			r->integrator.setRumble = emu_rumble_set;
			e->rumble = r;
			e->core->setPeripheral(e->core, mPERIPH_RUMBLE, &r->integrator.d);
			/* (the cartridge's GPIO pins: a GBA cartridge's only. A Game Boy core's board is a struct GB,
			 * whose CPU is mGBA's 200-byte SM83Core: an ARMCore's store hooks are past its end) */
			if (e->platform == mPLATFORM_GBA) {
				struct ARMCore* cpu = ((struct GBA*) e->core->board)->cpu;
				if (!mgbaStore8) mgbaStore8 = cpu->memory.store8, mgbaStore16 = cpu->memory.store16;
				cpu->memory.store8 = gpio_store8;
				cpu->memory.store16 = gpio_store16;
			}
		}
	}
	rtc_pin(e);
	e->audio = audio;
	if (audio) {
		e->core->setAudioBufferSize(e->core, 2048);
	}

	size_t patchedSize = 0;
	uint8_t* patchedRom = patched_rom(romPath, &patchedSize, &e->patch, e->patchName, &e->fileCrc, e->patchSkipped);
	if (patchedRom) { /* a patch file beside the ROM, applied (GEN-9) */
		struct VFile* vf = VFileMemChunk(patchedRom, patchedSize);
		free(patchedRom);
		if (!vf || !e->core->loadROM(e->core, vf)) {
			if (vf) vf->close(vf);
			fprintf(stderr, "could not load the patched ROM %s\n", romPath);
			emu_close(e); /* (callers don't: a game that failed went back to the list with its core open) */
			return false;
		}
	} else if (!mCoreLoadFile(e->core, romPath)) {
		fprintf(stderr, "could not load ROM %s\n", romPath);
		emu_close(e);
		return false;
	}
	if (savePath) {
#ifndef __wasi__
		struct VFile* vf = async_save_open(savePath);
#else
		struct VFile* vf = VFileOpen(savePath, O_CREAT | O_RDWR);
#endif
		if (vf) {
			e->core->loadSave(e->core, vf);
		} else {
			fprintf(stderr, "warning: cannot open save file %s\n", savePath);
		}
	}
	if (emu_bios_path[0] && e->platform == mPLATFORM_GBA) { /* the player's own BIOS (issue #78), started past its logo as the built-in one is */
		pthread_mutex_lock(&biosLock);
		if (!biosData || strcmp(biosDataPath, emu_bios_path)) {
			BiosFile why;
			uint8_t* b = bios_read(emu_bios_path, &why);
			if (b) { /* (an earlier one stays mapped by cores still open: kept, not freed) */
				biosData = b;
				snprintf(biosDataPath, sizeof(biosDataPath), "%s", emu_bios_path);
			} else {
				fprintf(stderr, "GBA BIOS %s: not used (%d)\n", emu_bios_path, (int) why);
			}
		}
		struct VFile* bvf = biosData && !strcmp(biosDataPath, emu_bios_path) ? VFileFromConstMemory(biosData, GBA_SIZE_BIOS) : NULL;
		pthread_mutex_unlock(&biosLock);
		if (bvf) {
			e->core->opts.skipBios = true;
			e->core->loadBIOS(e->core, bvf, 0);
		}
	}
	e->core->reset(e->core);
	if (e->gbBorder) gb_border_snapshot(e);

	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	e->core->getGameInfo(e->core, &info);
	memcpy(e->gameCode, info.code, 4);
	e->gameCode[4] = 0;
	e->core->checksum(e->core, &e->romCrc, mCHECKSUM_CRC32); /* (worked out by mGBA as it loaded the ROM) */
	if (e->patch != PATCH_APPLIED && e->patch != PATCH_PARTIAL) e->fileCrc = e->romCrc;
	const char* codeless = memcmp(info.code, "\0\0\0\0", 4) ? NULL : profile_codeless(e->fileCrc);
	if (codeless) memcpy(e->gameCode, codeless, 5);
	return true;
}

static void hide_release(Emu* e);

static struct mCoreRewindContext rewindCtx;
static Emu* rewindOwner;

void emu_close(Emu* e) {
	if (e->core) {
		if (rewindOwner == e) emu_rewind_deinit();
		split_release(e);
		wide_release(e);
		hide_release(e);
		mCoreConfigDeinit(&e->core->config);
		e->core->deinit(e->core);
		e->core = NULL;
	}
	free(e->fb);
	e->fb = NULL;
	free(e->sgbFb);
	free(e->gbBorderDefault);
	e->gbBorderDefault = NULL;
	e->sgbFb = NULL;
	free(e->rumble);
	e->rumble = NULL;
	free(e->gyro);
	e->gyro = NULL;
}

bool emu_cancelled(const Emu* e) {
	return e->cancel && *e->cancel;
}

/* (GB-12) mGBA starts a Super Game Boy with a frame of its own (it says "mGBA"), until the game sends its own.
 * The border's two memories are kept as they start; the border stays black while they still are. */
static void gb_border_mem(Emu* e, uint8_t** ch, uint8_t** mp) {
	struct GB* gb = e->core->board;
	*ch = gb->video.renderer ? gb->video.renderer->sgbCharRam : NULL;
	*mp = gb->video.renderer ? gb->video.renderer->sgbMapRam : NULL;
}

static void gb_border_snapshot(Emu* e) {
	uint8_t *ch, *mp;
	gb_border_mem(e, &ch, &mp);
	if (!ch || !mp) { e->gbBorderOwn = true; return; } /* nothing to compare: show whatever the core draws */
	free(e->gbBorderDefault);
	e->gbBorderDefault = malloc(SGB_SIZE_CHAR_RAM + SGB_SIZE_MAP_RAM);
	if (!e->gbBorderDefault) { e->gbBorderOwn = true; return; } /* (as with nothing to compare) */
	memcpy(e->gbBorderDefault, ch, SGB_SIZE_CHAR_RAM);
	memcpy(e->gbBorderDefault + SGB_SIZE_CHAR_RAM, mp, SGB_SIZE_MAP_RAM);
	e->gbBorderOwn = false;
}

static void gb_border_hide_default(Emu* e) {
	if (e->gbBorderOwn) return;
	uint8_t *ch, *mp;
	gb_border_mem(e, &ch, &mp);
	if (ch && mp && memcmp(ch, e->gbBorderDefault, SGB_SIZE_CHAR_RAM) == 0 && memcmp(mp, e->gbBorderDefault + SGB_SIZE_CHAR_RAM, SGB_SIZE_MAP_RAM) == 0) {
		for (int y = 0; y < SGB_H; ++y) {
			uint32_t* row = e->sgbFb + (size_t) y * SGB_W;
			bool inY = y >= SGB_WIN_Y && y < SGB_WIN_Y + 144;
			for (int x = 0; x < SGB_W; ++x) {
				if (inY && x >= SGB_WIN_X && x < SGB_WIN_X + 160) { x = SGB_WIN_X + 159; continue; }
				row[x] = 0xFF000000u;
			}
		}
		return;
	}
	e->gbBorderOwn = true; /* the game's own: from now on as the core draws it */
}

void emu_frame(Emu* e, uint32_t keys) {
	if (emu_cancelled(e)) return;
	++e->frames;
	e->core->setKeys(e->core, keys);
	e->core->runFrame(e->core);
	if (e->sgbFb) gb_border_hide_default(e);
	if (e->sgbFb && e->fb) { /* (GB-12) the game's window into the frame, where it is without a border: everything else works on that */
		for (int y = 0; y < e->picH; ++y) {
			memcpy(e->fb + (size_t) (e->picY + y) * GBA_W + e->picX, e->sgbFb + (size_t) (SGB_WIN_Y + y) * SGB_W + SGB_WIN_X, (size_t) e->picW * sizeof(uint32_t));
		}
	}
	if (!e->audio) {
		struct mAudioBuffer* ab = e->core->getAudioBuffer(e->core);
		if (ab) {
			mAudioBufferClear(ab);
		}
	}
}

/* A frame run without drawing it (the renderer still gets every write to
 * VRAM, the palettes, OAM and the registers): for the hidden copies'
 * waits, whose pictures nobody looks at; about half a frame's work saved.
 * e->fb keeps the last picture drawn. */
void emu_frame_blind(Emu* e, uint32_t keys) {
	struct GBA* gba = gba_of(e);
	if (!gba) {
		emu_frame(e, keys);
		return;
	}
	gba->video.frameskipCounter = 1;
	emu_frame(e, keys);
	gba->video.frameskipCounter = 0;
}

void emu_reset(Emu* e) {
	e->core->reset(e->core);
	if (e->gbBorder) gb_border_snapshot(e);
	split_resync(e);
	wide_resync(e);
}

size_t emu_state_size(Emu* e) {
	return e->core->stateSize(e->core);
}

bool emu_state_patch32(Emu* e, void* state, uint32_t addr, uint32_t orig, uint32_t word) {
	(void) e;
	struct GBASerializedState* st = state;
	uint8_t* p = addr >= 0x03000000 && addr < 0x03008000 ? st->iwram + (addr & 0x7FFC) : addr >= 0x02000000 && addr < 0x02040000 ? st->wram + (addr & 0x3FFFC) : NULL;
	uint32_t was;
	if (!p) return false;
	memcpy(&was, p, 4);
	if (was != orig) return false;
	memcpy(p, &word, 4);
	return true;
}

bool emu_state_save(Emu* e, void* buf) {
	return e->core->saveState(e->core, buf);
}

bool emu_state_load(Emu* e, const void* buf) {
	bool ok = e->core->loadState(e->core, buf);
	rtc_pin(e);
	split_resync(e);
	wide_resync(e);
	return ok;
}

/* ------------------------------------------------------------ rewind */

/* mGBA's rewind: a state saved every so often, kept as the difference from
 * the next one (worked out on mGBA's own thread). One buffer, for the game
 * being played. */
void emu_rewind_init(Emu* e, size_t entries) {
	emu_rewind_deinit();
	memset(&rewindCtx, 0, sizeof(rewindCtx));
	mCoreRewindContextInit(&rewindCtx, entries, true);
	rewindOwner = e;
}

void emu_rewind_deinit(void) {
	if (!rewindOwner) return;
	mCoreRewindContextDeinit(&rewindCtx);
	rewindOwner = NULL;
}

void emu_rewind_append(Emu* e) {
	if (rewindOwner == e) mCoreRewindAppend(&rewindCtx, e->core);
}

bool emu_rewind_step(Emu* e) {
	if (rewindOwner != e || !mCoreRewindRestore(&rewindCtx, e->core, 1)) return false;
	split_resync(e);
	wide_resync(e);
	return true;
}

size_t emu_rewind_count(void) {
	return rewindOwner ? rewindCtx.size : 0;
}

/* ---------------------------------------------------------------- state files
 *
 * GBA DS's state files are mGBA's own: the state, then mGBA's extra data
 * (the battery save, the clock, when it was saved, and GBA DS's second
 * screen, EXTDATA_GBADS below).
 *
 * RetroArch's (and BananaFront's: the same files) wrap a core's state
 * (issue #39): "RASTATE" and version 1, then blocks (a 4-character tag, a
 * u32 size, the data, zeros to a multiple of 8) up to "END ": MEM the
 * core's own bytes (for its mGBA core, the same as GBA DS's states:
 * checked byte for byte), ACHV RetroAchievements progress, RPLY a replay.
 * Usually compressed (savestate_file_compression, on by default):
 * "#RZIPv" 1 "#", u32 chunk size, u64 size, then each chunk as a u32 size
 * and zlib's data; version 2 (RetroArch 1.21 on, when its Save State
 * Compression is set to zstd; issue #78) the same with a zstd frame for each
 * chunk. Loading takes any of these; saving writes the format asked for
 * (zlib's version 1, which every RetroArch reads). All numbers little-endian. */

/* (in mGBA's subsystem range, which it doesn't use for the GBA) */
#define EXTDATA_GBADS ((enum mStateExtdataTag) (EXTDATA_SUBSYSTEM_START + 0x3D))

#include <encodings/rzstd.h> /* (RZIP version 2's zstd: third_party/rzstd) */

#ifdef USE_ZLIB
/* (zlib's, linked with libmgba; its header isn't on every build's include path) */
int compress2(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen, int level);
int uncompress(unsigned char* dest, unsigned long* destLen, const unsigned char* source, unsigned long sourceLen);
unsigned long compressBound(unsigned long sourceLen);
#endif

static const uint8_t RZIP_MAGIC[8] = { '#', 'R', 'Z', 'I', 'P', 'v', 1, '#' };
#define RZIP_CHUNK (128 * 1024) /* (RetroArch's) */
#define STATE_FILE_MAX (64 * 1024 * 1024)

static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static void put_le32(uint8_t* p, uint32_t v) {
	for (int i = 0; i < 4; ++i) p[i] = (uint8_t) (v >> (8 * i));
}

/* mGBA's state for the GBA: versionMagic 0x01000000 + version */
static bool is_gba_state(const uint8_t* b, size_t n) {
	return n >= 12 && b[1] == 0 && b[2] == 0 && b[3] == 0x01;
}

/* mGBA's state for a Game Boy / Game Boy Color game (issue #77): versionMagic 0x00400000 + version */
static bool is_gb_state(const uint8_t* b, size_t n) {
	return n >= 12 && b[1] == 0 && b[2] == 0x40 && b[3] == 0;
}

static bool is_mgba_state(const uint8_t* b, size_t n) {
	return is_gba_state(b, n) || is_gb_state(b, n);
}

/* A state file read whole with RetroArch's wrapping taken off: the core's
 * bytes (malloc'd, *n) and what the file was. NULL when there's nothing
 * to load (*kind says why). */
static uint8_t* state_file_read(const char* path, size_t* n, StateFileKind* kind) {
	*n = 0;
	*kind = STATE_FILE_NONE;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	*kind = STATE_FILE_UNREADABLE;
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	fseek(f, 0, SEEK_SET);
	uint8_t* b = len > 0 && len <= STATE_FILE_MAX ? malloc((size_t) len) : NULL;
	bool ok = b && fread(b, 1, (size_t) len, f) == (size_t) len;
	fclose(f);
	if (!ok) {
		free(b);
		return NULL;
	}
	size_t bn = (size_t) len;
	bool wrapped = false;
	if (bn >= 20 && !memcmp(b, RZIP_MAGIC, 6) && b[7] == '#') {
		uint8_t* out = NULL;
		size_t got = 0;
		uint32_t chunk = le32(b + 8);
		uint64_t total = (uint64_t) le32(b + 12) | (uint64_t) le32(b + 16) << 32;
		uint8_t version = b[6];
#ifndef USE_ZLIB
		if (version == 1) total = 0; /* (no zlib in this build) */
#endif
		if ((version == 1 || version == 2) && chunk && total && total <= STATE_FILE_MAX) out = malloc((size_t) total);
		size_t at = 20;
		while (out && got < total) {
			size_t dl = (size_t) (total - got < chunk ? total - got : chunk), made = 0;
			uint32_t zn = at + 4 <= bn ? le32(b + at) : 0;
			bool ok = zn && zn <= bn - at - 4;
			if (ok && version == 2) {
				/* (one zstd frame a chunk, the chunk's size known: rzstd_decode fails rather than write past it) */
				ok = rzstd_decode(out + got, dl, b + at + 4, zn, &made) == RZSTD_PROCESS_END;
			}
#ifdef USE_ZLIB
			else if (ok) {
				unsigned long ul = (unsigned long) dl;
				ok = uncompress(out + got, &ul, b + at + 4, zn) == 0;
				made = ul;
			}
#endif
			if (!ok || !made) {
				free(out);
				out = NULL;
				break;
			}
			got += made, at += 4 + zn;
		}
		free(b);
		if (!out) return NULL; /* (damaged, or an RZIP version GBA DS doesn't know) */
		b = out;
		bn = got;
		wrapped = true;
	}
	if (bn >= 8 && !memcmp(b, "RASTATE", 7)) {
		size_t at = 8, memAt = 0, memN = 0;
		bool have = false;
		while (b[7] == 1 && at + 8 <= bn) {
			uint32_t sz = le32(b + at + 4);
			if (!memcmp(b + at, "END ", 4) || sz > bn - at - 8) break;
			if (!memcmp(b + at, "MEM ", 4)) memAt = at + 8, memN = sz, have = true;
			size_t adv = 8 + (((size_t) sz + 7) & ~(size_t) 7);
			if (adv > bn - at) break;
			at += adv;
		}
		if (!have || !memN) {
			free(b);
			return NULL;
		}
		memmove(b, b + memAt, memN);
		bn = memN;
		wrapped = true;
	}
	/* (compressed with no RASTATE inside: RetroArch's older files, the core's bytes alone) */
	bool mgba = is_mgba_state(b, bn);
	*kind = !mgba ? STATE_FILE_OTHER_CORE : wrapped ? STATE_FILE_RETROARCH : STATE_FILE_MGBA;
	*n = bn;
	return b;
}

/* written beside, then put in its place: a state is never half written */
static bool write_whole(const char* path, const void* data, size_t n) {
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE* f = fopen(tmp, "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, n, f) == n;
	ok = fclose(f) == 0 && ok;
	if (ok) ok = rename(tmp, path) == 0;
	if (!ok) remove(tmp);
	return ok;
}

/* the core's bytes in RetroArch's file: malloc'd, *outN */
static uint8_t* retroarch_wrap(const uint8_t* core, size_t n, bool compress, size_t* outN) {
	size_t pad = (n + 7) & ~(size_t) 7, pn = 8 + 8 + pad + 8;
	uint8_t* p = calloc(1, pn);
	if (!p) return NULL;
	memcpy(p, "RASTATE", 7), p[7] = 1;
	memcpy(p + 8, "MEM ", 4), put_le32(p + 12, (uint32_t) n), memcpy(p + 16, core, n);
	memcpy(p + 16 + pad, "END ", 4); /* (size 0) */
	*outN = pn;
#ifdef USE_ZLIB
	if (!compress) return p;
	size_t chunks = (pn + RZIP_CHUNK - 1) / RZIP_CHUNK, bound = compressBound(RZIP_CHUNK);
	uint8_t* z = malloc(20 + chunks * (4 + bound));
	if (!z) {
		free(p);
		return NULL;
	}
	memcpy(z, RZIP_MAGIC, 8);
	put_le32(z + 8, RZIP_CHUNK), put_le32(z + 12, (uint32_t) pn), put_le32(z + 16, 0);
	size_t at = 20;
	for (size_t off = 0; off < pn; off += RZIP_CHUNK) {
		unsigned long zn = bound;
		size_t sn = pn - off < RZIP_CHUNK ? pn - off : RZIP_CHUNK;
		if (compress2(z + at + 4, &zn, p + off, sn, 6) != 0) {
			free(p), free(z);
			return NULL;
		}
		put_le32(z + at, (uint32_t) zn);
		at += 4 + zn;
	}
	free(p);
	*outN = at;
	return z;
#else
	(void) compress; /* (no zlib here: uncompressed, which RetroArch reads too) */
	return p;
#endif
}

uint8_t* emu_state_save_mem(Emu* e, const void* extra, size_t extraN, size_t* outN) {
	*outN = 0;
	struct VFile* vf = VFileMemChunk(NULL, 0);
	if (!vf) return NULL;
	bool ok = mCoreSaveStateNamed(e->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	if (ok && extra) {
		/* GBA DS's own data, put in with mGBA's (other emulators skip it) */
		struct mStateExtdata ext;
		mStateExtdataInit(&ext);
		mCoreExtractExtdata(e->core, vf, &ext);
		void* copy = malloc(extraN ? extraN : 1);
		if (copy) {
			memcpy(copy, extra, extraN);
			struct mStateExtdataItem item = { .size = (int32_t) extraN, .data = copy, .clean = free };
			mStateExtdataPut(&ext, EXTDATA_GBADS, &item);
			size_t sz = e->core->stateSize(e->core);
			vf->truncate(vf, sz);
			vf->seek(vf, sz, SEEK_SET);
			if (!mStateExtdataSerialize(&ext, vf)) fprintf(stderr, "save state: GBA DS's own data not kept with it\n");
		}
		mStateExtdataDeinit(&ext);
	}
	size_t n = ok ? (size_t) vf->size(vf) : 0;
	uint8_t* core = n ? malloc(n) : NULL;
	ok = core && vf->seek(vf, 0, SEEK_SET) == 0 && vf->read(vf, core, n) == (ssize_t) n;
	vf->close(vf);
	if (!ok) {
		free(core);
		return NULL;
	}
	*outN = n;
	return core;
}

bool emu_state_save_file_as(Emu* e, const char* path, const void* extra, size_t extraN, StateFormat fmt) {
	size_t n;
	uint8_t* core = emu_state_save_mem(e, extra, extraN, &n);
	bool ok = core != NULL;
	uint8_t* out = core;
	size_t outN = n;
	if (ok && fmt != STATE_FORMAT_MGBA) ok = (out = retroarch_wrap(core, n, fmt == STATE_FORMAT_RETROARCH_RZIP, &outN)) != NULL;
	if (ok) ok = write_whole(path, out, outN);
	if (out != core) free(out);
	free(core);
	return ok;
}

bool emu_state_save_file(Emu* e, const char* path) {
	return emu_state_save_file_as(e, path, NULL, 0, STATE_FORMAT_MGBA);
}

bool emu_state_load_mem(Emu* e, const void* b, size_t n) {
	bool ok = false;
	struct VFile* vf = VFileFromConstMemory(b, n);
	if (vf) {
		ok = mCoreLoadStateNamed(e->core, vf, SAVESTATE_RTC);
		rtc_pin(e);
		vf->close(vf);
	}
	split_resync(e);
	wide_resync(e);
	return ok;
}

bool emu_state_load_file(Emu* e, const char* path) {
	size_t n;
	StateFileKind kind;
	uint8_t* b = state_file_read(path, &n, &kind);
	if (!b) return false;
	bool ok = (kind == STATE_FILE_MGBA || kind == STATE_FILE_RETROARCH) && emu_state_load_mem(e, b, n);
	free(b);
	return ok;
}

StateFileKind emu_state_file_kind(Emu* e, const char* path, bool* fromGbads) {
	if (fromGbads) *fromGbads = false;
	size_t n;
	StateFileKind kind;
	uint8_t* b = state_file_read(path, &n, &kind);
	if (b && fromGbads && e && (kind == STATE_FILE_MGBA || kind == STATE_FILE_RETROARCH)) {
		struct VFile* vf = VFileFromConstMemory(b, n);
		struct mStateExtdata ext;
		mStateExtdataInit(&ext);
		struct mStateExtdataItem item;
		/* (Get says yes for a tag that isn't there too: its size says) */
		if (vf && mCoreExtractExtdata(e->core, vf, &ext) && mStateExtdataGet(&ext, EXTDATA_GBADS, &item) && item.size > 0 && item.data)
			*fromGbads = true;
		mStateExtdataDeinit(&ext);
		if (vf) vf->close(vf);
	}
	free(b);
	return kind;
}

/* mGBA's own rule (GBADeserialize, src/gba/serialize.c): a different BIOS checksum is
 * only a warning, unless one side is the official BIOS and the saved program counter
 * is inside the BIOS (from 0x20: past the reset vector): then the state is refused.
 * (GBA DS's own states are made with mGBA's built-in BIOS, so those from RetroArch's
 * core with a BIOS file are the ones that meet this.) */
BiosRefusal emu_state_file_bios_refusal(Emu* e, const char* path) {
	size_t n;
	StateFileKind kind;
	uint8_t* b = state_file_read(path, &n, &kind);
	BiosRefusal r = BIOS_REFUSAL_NONE;
	const size_t pcAt = offsetof(struct GBASerializedState, cpu.gprs) + ARM_PC * sizeof(int32_t);
	if (b && (kind == STATE_FILE_MGBA || kind == STATE_FILE_RETROARCH) && n >= pcAt + 4 && e && gba_of(e)) {
		uint32_t saved = le32(b + offsetof(struct GBASerializedState, biosChecksum)), pc = le32(b + pcAt);
		uint32_t mine = gba_of(e)->biosChecksum;
		if (saved != mine && pc < GBA_SIZE_BIOS && pc >= 0x20) {
			if (saved == GBA_BIOS_CHECKSUM) r = BIOS_REFUSAL_STATE_HAS_BIOS;
			else if (mine == GBA_BIOS_CHECKSUM) r = BIOS_REFUSAL_CORE_HAS_BIOS;
		}
	}
	free(b);
	return r;
}

bool emu_state_file_rom_crc(const char* path, uint32_t* crc) {
	size_t n;
	StateFileKind kind;
	uint8_t* h = state_file_read(path, &n, &kind);
	/* GBASerializedState: versionMagic (0x01000000 + version), biosChecksum, romCrc32;
	 * GBSerializedState: versionMagic (0x00400000 + version), romCrc32 */
	bool ok = h && is_mgba_state(h, n);
	if (ok) *crc = le32(h + (is_gb_state(h, n) ? 4 : 8));
	free(h);
	return ok;
}

void* emu_state_mem_get_extra(Emu* e, const void* b, size_t bn, size_t* n) {
	*n = 0;
	struct VFile* vf = b ? VFileFromConstMemory(b, bn) : NULL;
	if (!vf) return NULL;
	struct mStateExtdata ext;
	mStateExtdataInit(&ext);
	void* out = NULL;
	struct mStateExtdataItem item;
	if (mCoreExtractExtdata(e->core, vf, &ext) && mStateExtdataGet(&ext, EXTDATA_GBADS, &item) && item.size > 0 && item.data) {
		out = malloc((size_t) item.size);
		if (out) memcpy(out, item.data, (size_t) item.size), *n = (size_t) item.size;
	}
	mStateExtdataDeinit(&ext);
	vf->close(vf);
	return out;
}

void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n) {
	*n = 0;
	size_t bn;
	StateFileKind kind;
	uint8_t* b = state_file_read(path, &bn, &kind);
	void* out = emu_state_mem_get_extra(e, b, bn, n);
	free(b);
	return out;
}

uint8_t emu_read8(Emu* e, uint32_t a) {
	return e->core->rawRead8(e->core, a, -1);
}
void emu_write8(Emu* e, uint32_t a, uint8_t v) {
	e->core->rawWrite8(e->core, a, -1, v);
}
void emu_write16(Emu* e, uint32_t a, uint16_t v) {
	e->core->rawWrite16(e->core, a, -1, v);
}
void emu_write32(Emu* e, uint32_t a, uint32_t v) {
	e->core->rawWrite32(e->core, a, -1, v);
}
uint16_t emu_read16(Emu* e, uint32_t a) {
	return e->core->rawRead16(e->core, a, -1);
}
uint32_t emu_read32(Emu* e, uint32_t a) {
	return e->core->rawRead32(e->core, a, -1);
}

static bool has_ext(const char* name, const char* const* exts) {
	const char* dot = strrchr(name, '.');
	if (!dot) return false;
	for (int i = 0; exts[i]; ++i) {
		if (!strcasecmp(dot, exts[i])) return true;
	}
	return false;
}

bool emu_is_archive_name(const char* name) {
	static const char* const exts[] = { ".zip", ".7z", NULL };
	return has_ext(name, exts);
}

bool emu_is_rom_name(const char* name) {
	static const char* const exts[] = { ".gba", ".gb", ".gbc", ".sgb", ".dmg", ".cgb", ".zip", ".7z", NULL };
	return has_ext(name, exts);
}

/* A ROM inside an archive: a GBA one, or a Game Boy / Game Boy Color one (issue #77) */
static bool any_rom(struct VFile* vf) {
	return GBAIsROM(vf) || GBIsROM(vf);
}

bool emu_rom_header(const char* path, uint8_t* out, size_t n) {
	if (n > EMU_HEADER_MAX) n = EMU_HEADER_MAX;
	struct VDir* archive = emu_is_archive_name(path) ? VDirOpenArchive(path) : NULL;
	struct VFile* vf = archive ? VDirFindFirst(archive, any_rom) : VFileOpen(path, O_RDONLY);
	bool ok = false;
	if (vf) {
		ok = vf->seek(vf, 0, SEEK_SET) == 0 && vf->read(vf, out, n) == (ssize_t) n;
		vf->close(vf);
	}
	if (archive) archive->close(archive);
	return ok;
}

bool emu_gb_sgb_header(const Emu* e) {
	return e && e->gbSgbHeader;
}
bool emu_gb_border(const Emu* e) {
	return e && e->gbBorder;
}
const uint32_t* emu_gb_border_px(const Emu* e) {
	return e ? e->sgbFb : NULL;
}
bool emu_gb_sgb(const Emu* e) {
	return e && e->gbSgb;
}
int emu_gb_kind(const Emu* e) {
	return e ? e->gbKind : 0;
}

bool emu_is_gb(const Emu* e) {
	return e && e->core && e->platform == mPLATFORM_GB;
}

/* Game Boy / Game Boy Color header (0x100..): the Nintendo logo's first bytes at 0x104 */
static bool header_is_gb(const uint8_t* h, size_t n) {
	if (n >= 0xB3 && h[0xB2] == 0x96) return false; /* (a GBA header's fixed byte) */
	return n >= 0x108 && h[0x104] == 0xCE && h[0x105] == 0xED && h[0x106] == 0x66 && h[0x107] == 0x66;
}

bool emu_rom_code(const uint8_t* h, size_t n, char code[5]) {
	if (!header_is_gb(h, n) || n < 0x150) {
		if (n < 0xB0) return false;
		memcpy(code, h + 0xAC, 4); /* the GBA's game code */
		code[4] = 0;
		return true;
	}
	/* A Game Boy game has no 4-letter code the GBA way (many have none), so it gets one of its own:
	 * "g" (plain Game Boy) or "c" (works on the Game Boy Color), then 3 base-36 digits of a hash of
	 * the header from the title to the checksums. Lower case, so it never equals a GBA game's code
	 * (those are capitals and digits) and no profile is found for it. */
	uint32_t x = 2166136261u;
	for (size_t i = 0x134; i < 0x150; ++i) x = (x ^ h[i]) * 16777619u;
	x %= 46656u;
	static const char d[] = "0123456789abcdefghijklmnopqrstuvwxyz";
	code[0] = h[0x143] & 0x80 ? 'c' : 'g';
	code[1] = d[x / 1296], code[2] = d[x / 36 % 36], code[3] = d[x % 36], code[4] = 0;
	return true;
}

bool emu_rom_file_code(const char* path, char code[5]) {
	uint8_t h[EMU_HEADER_MAX];
	if (!emu_rom_header(path, h, sizeof(h)) || !emu_rom_code(h, sizeof(h), code)) return false;
	if (memcmp(code, "\0\0\0\0", 4)) return true;
	struct VDir* archive = emu_is_archive_name(path) ? VDirOpenArchive(path) : NULL;
	struct VFile* vf = archive ? VDirFindFirst(archive, any_rom) : VFileOpen(path, O_RDONLY);
	if (vf) {
		/* (only a file the size of a known one is read whole) */
		ssize_t n = vf->size(vf);
		uint8_t* rom = n > 0 && profile_codeless_size(n) ? malloc((size_t) n) : NULL;
		if (rom && vf->seek(vf, 0, SEEK_SET) == 0 && vf->read(vf, rom, (size_t) n) == n) {
			const char* c = profile_codeless(doCrc32(rom, (size_t) n));
			if (c) memcpy(code, c, 5);
		}
		free(rom);
		vf->close(vf);
	}
	if (archive) archive->close(archive);
	return true;
}

void emu_rom_title(const uint8_t* h, size_t n, char title[17]) {
	title[0] = 0;
	size_t at = 0xA0, len = 12;
	if (header_is_gb(h, n) && n >= 0x150) at = 0x134, len = h[0x143] & 0x80 ? 11 : 15;
	if (n < at + len) return;
	memcpy(title, h + at, len);
	title[len] = 0;
	for (size_t i = 0; i < len; ++i) {
		if (title[i] && !isprint((unsigned char) title[i])) title[i] = '_';
	}
}

uint8_t* emu_rom_read_all(const char* path, size_t* size) {
	struct VDir* archive = emu_is_archive_name(path) ? VDirOpenArchive(path) : NULL;
	struct VFile* vf = archive ? VDirFindFirst(archive, any_rom) : VFileOpen(path, O_RDONLY);
	uint8_t* buf = NULL;
	if (vf) {
		ssize_t n = vf->size(vf);
		if (n > 0 && n <= 64 * 1024 * 1024 && (buf = malloc((size_t) n))) {
			if (vf->seek(vf, 0, SEEK_SET) != 0 || vf->read(vf, buf, (size_t) n) != n) {
				free(buf);
				buf = NULL;
			} else {
				*size = (size_t) n;
			}
		}
		vf->close(vf);
	}
	if (archive) archive->close(archive);
	return buf;
}

bool emu_save_memory(Emu* e, void* buf, size_t size) {
	struct VFile* vf = buf && size ? VFileFromMemory(buf, size) : NULL;
	if (!vf) return false;
	if (!e->core->loadSave(e->core, vf)) {
		vf->close(vf);
		return false;
	}
	return true;
}

size_t emu_save_memory_size(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (gba) return gba->memory.savedata.type == GBA_SAVEDATA_AUTODETECT ? GBA_SIZE_FLASH1M : GBASavedataSize(&gba->memory.savedata);
	if (e->core && e->platform == mPLATFORM_GB) return ((struct GB*) e->core->board)->sramSize;
	return 0;
}

uint8_t* emu_memory(Emu* e, int region, size_t* size) {
	size_t n = 0;
	uint8_t* p = e->core ? e->core->getMemoryBlock(e->core, (size_t) region, &n) : NULL;
	*size = p ? n : 0;
	return p;
}

size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames) {
	struct mAudioBuffer* ab = e->core->getAudioBuffer(e->core);
	if (!ab) {
		return 0;
	}
	return mAudioBufferRead(ab, out, maxFrames);
}

unsigned emu_audio_rate(Emu* e) {
	return e->core->audioSampleRate(e->core);
}

/* ------------------------------------------------------------ sprite hiding */

/* Rules per renderer: the game core hides the Metroid HUD mini-map, the map
 * capture core (another thread) hides prompts drawn over the map. */
typedef struct {
	struct GBAVideoRenderer* renderer;
	void (*orig)(struct GBAVideoRenderer* renderer, int y);
	SpriteRule rules[12];
	int n;
	bool keepOnly; /* hide every sprite that matches none of the rules instead */
	uint16_t exact[128][3]; /* and sprites exactly like these (emu_hide_exact) */
	int nExact;
	bool exactKeep; /* the exact ones are kept instead (emu_hud_keep_exact) */
	bool grouped; /* a matching sprite is hidden only as part of a group
	               * (emu_hide_grouped) */
	int nRetile; /* the HUD's sprites drawn with other tiles (emu_hud_retile) */
	int8_t retileIdx[8];
	uint16_t retileTile[8];
} HideSlot;
static HideSlot hideSlots[8];
static pthread_mutex_t hideLock = PTHREAD_MUTEX_INITIALIZER;

static HideSlot* slot_for(struct GBAVideoRenderer* r) {
	for (int i = 0; i < 8; ++i) {
		if (hideSlots[i].renderer == r) return &hideSlots[i];
	}
	return NULL;
}

static bool hidden(const HideSlot* s, const struct GBAObj* obj) {
	int x = (int) (GBAObjAttributesBGetX(obj->b) << 23) >> 23;
	int y = GBAObjAttributesAGetY(obj->a);
	if (y >= 160) y -= 256;
	int tile = GBAObjAttributesCGetTile(obj->c);
	int prio = GBAObjAttributesCGetPriority(obj->c);
	int pal = GBAObjAttributesCGetPalette(obj->c);
	for (int i = 0; i < s->nExact; ++i) {
		if (s->exact[i][0] == obj->a && s->exact[i][1] == obj->b && s->exact[i][2] == obj->c) return !s->exactKeep;
	}
	for (int i = 0; i < s->n; ++i) {
		const SpriteRule* r = &s->rules[i];
		if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1 && tile >= r->tileMin && tile <= r->tileMax &&
		    (r->prio < 0 || prio == r->prio) && (!r->palMask || (r->palMask >> pal & 1)) &&
		    (!r->tagged || (!GBAObjAttributesAIsTransformed(obj->a) && (obj->b & r->tagged))) &&
		    (!r->shapes || (r->shapes >> (GBAObjAttributesAGetShape(obj->a) * 4 + GBAObjAttributesBGetSize(obj->b)) & 1))) {
			return !s->keepOnly;
		}
	}
	return s->keepOnly;
}

/* emu_hide_grouped: of the sprites that match the rules, only those that are
 * part of the run of matching sprites at the start of the sprite table, or
 * whose run of sprites in one palette (one after another in the table)
 * matches the rules throughout, are hidden. Sprites wholly off the screen's
 * sides are passed over (they draw nothing). Lady Sia: the game puts its
 * HUD first (level 2 puts Sia among it, in her own palette), and a level's
 * thing is several sprites in one palette, some of which pass a HUD
 * piece's place with its shape (the first village's well). */
static bool groupHide[128];
static void hide_grouped(const HideSlot* s, const struct GBAVideoRendererSprite* sp, int n) {
	bool match[128], shown[128];
	if (n > 128) n = 128;
	for (int i = 0; i < n; ++i) {
		const struct GBAObj* o = &sp[i].obj;
		int x = (int) (GBAObjAttributesBGetX(o->b) << 23) >> 23;
		int w = GBAVideoObjSizes[GBAObjAttributesAGetShape(o->a) * 4 + GBAObjAttributesBGetSize(o->b)][0];
		if (GBAObjAttributesAIsTransformed(o->a)) w <<= GBAObjAttributesAGetDoubleSize(o->a);
		shown[i] = x < GBA_W && x + w > 0;
		match[i] = hidden(s, o);
		groupHide[i] = false;
	}
	/* the run at the start */
	for (int i = 0; i < n; ++i) {
		if (!shown[i]) continue;
		if (!match[i]) break;
		groupHide[i] = true;
	}
	/* runs in one palette */
	for (int i = 0; i < n;) {
		if (!shown[i]) {
			++i;
			continue;
		}
		int pal = GBAObjAttributesCGetPalette(sp[i].obj.c), j = i, end = i;
		bool all = true;
		for (; j < n; ++j) {
			if (!shown[j]) continue;
			if (GBAObjAttributesCGetPalette(sp[j].obj.c) != pal) break;
			all = all && match[j];
			end = j;
		}
		for (int k = i; k <= end; ++k) {
			if (shown[k] && all) groupHide[k] = true;
		}
		i = j;
	}
}

/* emu_hud_place: the HUD renderer's sprites, by their number in the sprite
 * table: 0 as the game has them, 1 at placeAB's place (its attributes 0 and
 * 1: place, shape and size; the picture and palette the game's), 2 left out.
 * Only that renderer's copy of the table: the game and its own picture keep
 * theirs. */
static const struct GBAVideoRenderer* placeRenderer;
static uint8_t placeHow[128];
static uint16_t placeAB[128][2];

/* Runs before the software renderer's own scanline code: when the sprite table
 * has changed, build the renderer's sprite list the way it would, minus the
 * hidden sprites, and mark it clean so the renderer uses ours. */
static void filtered_draw_scanline(struct GBAVideoRenderer* renderer, int y) {
	struct GBAVideoSoftwareRenderer* sw = (struct GBAVideoSoftwareRenderer*) renderer;
	HideSlot* s = slot_for(renderer);
	if (!s) return;
	if ((s->n || s->keepOnly || s->nExact || s->nRetile) && sw->oamDirty) {
		struct GBAObj* table = sw->d.oam->obj;
		static struct GBAObj placed[128];
		if (renderer == placeRenderer) {
			memcpy(placed, table, sizeof(placed));
			for (int i = 0; i < 128; ++i) {
				if (placeHow[i] == 1) placed[i].a = placeAB[i][0], placed[i].b = placeAB[i][1];
				else if (placeHow[i] == 2) placed[i].a = (uint16_t) ((placed[i].a & ~0x0300) | 0x0200);
			}
			table = placed;
		}
		int n = GBAVideoRendererCleanOAM(table, sw->sprites, sw->objOffsetY);
		int kept = 0;
		if (s->grouped && !s->keepOnly) {
			hide_grouped(s, sw->sprites, n);
			for (int i = 0; i < n; ++i) {
				if (!groupHide[i]) sw->sprites[kept++] = sw->sprites[i];
			}
		} else {
			for (int i = 0; i < n; ++i) {
				if (!hidden(s, &sw->sprites[i].obj)) sw->sprites[kept++] = sw->sprites[i];
			}
		}
		/* emu_hud_retile: this renderer's copy of the sprite list only (OAM itself is the game's) */
		for (int i = 0; i < kept && s->nRetile; ++i) {
			for (int k = 0; k < s->nRetile; ++k) {
				if (sw->sprites[i].index == s->retileIdx[k]) sw->sprites[i].obj.c = (sw->sprites[i].obj.c & ~0x3FF) | s->retileTile[k];
			}
		}
		sw->oamMax = kept;
		sw->oamDirty = false;
	}
	s->orig(renderer, y);
}

void emu_hide_exact(Emu* e, const uint16_t (*attrs)[3], int n) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(r);
	if (!s && (s = slot_for(NULL))) {
		s->n = 0, s->keepOnly = false;
		s->orig = r->drawScanline;
		s->renderer = r;
		r->drawScanline = filtered_draw_scanline;
	}
	if (s) {
		s->nExact = n > 128 ? 128 : n;
		s->exactKeep = false;
		if (s->nExact) memcpy(s->exact, attrs, sizeof(*attrs) * s->nExact);
	}
	pthread_mutex_unlock(&hideLock);
	((struct GBAVideoSoftwareRenderer*) r)->oamDirty = true;
}

void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(r);
	if (!s) s = slot_for(NULL);
	if (s) {
		s->n = n > 12 ? 12 : n;
		if (s->n) memcpy(s->rules, rules, sizeof(*rules) * s->n);
		if (s->renderer != r) {
			s->orig = r->drawScanline;
			s->renderer = r;
			r->drawScanline = filtered_draw_scanline;
		}
	}
	pthread_mutex_unlock(&hideLock);
	((struct GBAVideoSoftwareRenderer*) r)->oamDirty = true;
}

/* The sprites the rules match are hidden only in groups (hide_grouped).
 * Kept until the game closes. */
void emu_hide_grouped(Emu* e, bool on) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(r);
	if (s) s->grouped = on;
	pthread_mutex_unlock(&hideLock);
	((struct GBAVideoSoftwareRenderer*) r)->oamDirty = true;
}

/* The opposite of emu_hide_sprites: only the sprites that match one of the
 * rules are drawn (Lady Sia's pause screen, captured without what the
 * level left in the sprite table). */
void emu_keep_sprites(Emu* e, const SpriteRule* rules, int n) {
	emu_hide_sprites(e, rules, n);
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(r);
	if (s) s->keepOnly = n > 0;
	pthread_mutex_unlock(&hideLock);
	((struct GBAVideoSoftwareRenderer*) r)->oamDirty = true;
}

void emu_layers(Emu* e, unsigned mask) {
	for (int i = 0; i < 5; ++i) e->core->enableVideoLayer(e->core, i, (mask >> i) & 1);
}

static void hide_release(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (!gba) return;
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(gba->video.renderer);
	if (s) memset(s, 0, sizeof(*s));
	pthread_mutex_unlock(&hideLock);
}

/* ---------------------------------------------------------------- HUD split
 *
 * A second software renderer that gets every register, palette and sprite
 * write the game's own renderer gets, and draws only the HUD (some BG
 * layers and sprites of one priority) into its own buffer, on black. While
 * it's on, the game's renderer leaves those out. Each renderer keeps its
 * own per-line state (affine counters, sprite lists), so the track's
 * per-line effects are unaffected. */
/* The 'no HUD pixel' colour: nearly black (red 1, blue 1 of 31), which
 * games don't use, so a HUD layer blended with it (a half-transparent
 * panel, a banner fading in) comes out as if over black, and one blended
 * away entirely (alpha 0) comes out as this colour, nothing. */
#define HUD_CLEAR_555 0x0401

typedef struct {
	struct GBAVideoSoftwareRenderer hud;
	struct GBAVideoRenderer* main;
	bool installed, active;
	unsigned bgMask;
	SpriteRule objs[12];
	int nObjs;
	int nKeep; /* sprites the HUD also draws by their exact attributes (emu_hud_keep_exact) */
	/* one frame of other layers instead (emu_hud_grab) */
	uint32_t* grabBuf;
	unsigned grabMask;
	uint16_t grabCnt; /* instead of grabMask: the BG with this BGxCNT then */
	/* the HUD's layers by priority, taken as each frame starts (0: bgMask) */
	int prioBelow;
	unsigned keepBgs; /* never in the HUD (emu_hud_keep_bgs) */
	/* emu_hud_mem_layers: memory checked as each frame starts drawing */
	unsigned baseMask, memLayers;
	uint32_t memAddr[2], memMask[2], memVal[2];
	int memSize[2], memN;
	bool memAny; /* any one of them holds, instead of every one */
	int memGamePal; /* palette + 1: their lines with a tile of it stay on the game (0: none) */
	unsigned memLineOff; /* memLayers left out of the HUD on this line, for that */
	/* emu_hud_band: lines where a BG has one of these BGxCNT values */
	unsigned lineHud, lineGame; /* this line's HUD layers / layers left out of the game */
	int bandY0, bandY1;         /* this frame's band lines so far (y1 < y0: none) */
	struct GBA* gba;
	bool grabbing, grabbed;
	uint32_t* hudBuf;
	uint16_t (*writeVideoRegister)(struct GBAVideoRenderer*, uint32_t, uint16_t);
	void (*writeVRAM)(struct GBAVideoRenderer*, uint32_t);
	void (*writePalette)(struct GBAVideoRenderer*, uint32_t, uint16_t);
	void (*writeOAM)(struct GBAVideoRenderer*, uint32_t);
	void (*drawScanline)(struct GBAVideoRenderer*, int);
	void (*finishFrame)(struct GBAVideoRenderer*);
	void (*reset)(struct GBAVideoRenderer*);
} Split;
static Split split;
/* emu_hud_game_reg (kept across installs) */
static uint32_t gameRegAddr;
static uint16_t gameRegBits;
/* emu_hud_band (kept across installs) */
static bool bandOn, bandCopy, bandWatch;
static int lastBandY0, lastBandY1 = -1;
static uint16_t bandNormal[4]; /* each BG's BGxCNT on the lines outside the band (0: not seen yet) */

static void filtered_draw_scanline(struct GBAVideoRenderer* renderer, int y);

/* The HUD renderer's backdrop must stay exactly the 'nothing here' colour:
 * no brightening or darkening of it (BLDCNT's backdrop first target; Super
 * Mario Advance 2 darkens the bottom rows a little more each line). */
static uint16_t hud_reg(uint32_t a, uint16_t v) {
	return a == 0x50 ? v & ~0x0020 : v;
}
static uint16_t split_reg(struct GBAVideoRenderer* r, uint32_t a, uint16_t v) {
	uint16_t out;
	if (split.active && gameRegBits && a == gameRegAddr) {
		/* both renderers without those bits; the register keeps them */
		out = split.writeVideoRegister(r, a, v & ~gameRegBits) | (v & gameRegBits);
		split.hud.d.writeVideoRegister(&split.hud.d, a, hud_reg(a, v & ~gameRegBits));
	} else {
		out = split.writeVideoRegister(r, a, v);
		split.hud.d.writeVideoRegister(&split.hud.d, a, hud_reg(a, v));
	}
	return out;
}
static void split_vram(struct GBAVideoRenderer* r, uint32_t a) {
	split.writeVRAM(r, a);
	split.hud.d.writeVRAM(&split.hud.d, a);
}
static void split_palette(struct GBAVideoRenderer* r, uint32_t a, uint16_t v) {
	split.writePalette(r, a, v);
	split.hud.d.writePalette(&split.hud.d, a, a == 0 ? HUD_CLEAR_555 : v); /* backdrop: the 'nothing here' colour */
}
static void split_oam(struct GBAVideoRenderer* r, uint32_t i) {
	split.writeOAM(r, i);
	split.hud.d.writeOAM(&split.hud.d, i);
}
static void split_layers(unsigned bgMask, bool obj) {
	for (int i = 0; i < 4; ++i) split.hud.d.disableBG[i] = !((bgMask >> i) & 1);
	split.hud.d.disableOBJ = !obj;
}
/* The BG layers with this BGxCNT value / at a priority below p, as the
 * registers are now. */
static unsigned bgs_with_cnt(uint16_t cnt) {
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) m |= (unsigned) (split.gba->memory.io[(0x08 >> 1) + i] == cnt) << i;
	return m;
}
static unsigned bgs_below(int p) {
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) m |= (unsigned) ((split.gba->memory.io[(0x08 >> 1) + i] & 3) < p) << i;
	return m;
}

/* a byte, halfword or word of EWRAM, IWRAM or the I/O registers, as the
 * game has it now (0 elsewhere) */
static uint32_t split_read(uint32_t a, int size) {
	const struct GBA* g = split.gba;
	const uint8_t* p;
	switch (a >> 24) {
	case 2: p = (const uint8_t*) g->memory.wram + (a & 0x3FFFF); break;
	case 3: p = (const uint8_t*) g->memory.iwram + (a & 0x7FFF); break;
	case 4: p = (const uint8_t*) g->memory.io + (a & 0x3FF); break;
	case 6: p = (const uint8_t*) g->video.vram + ((a & 0x1FFFF) < 0x18000 ? a & 0x1FFFF : (a & 0x1FFFF) - 0x8000); break; /* (the tile maps: Mario & Luigi's boxes) */
	default: return 0;
	}
	uint32_t v = 0;
	memcpy(&v, p, size == 1 || size == 2 ? size : 4);
	return v;
}

/* Whether text BG `bg`'s map, as the registers are now, has a tile in
 * palette `pal` on screen line `y` (any column on screen; mode 0, or BG0-1
 * in mode 1) */
static bool bg_line_has_pal(int bg, int y, int pal) {
	const uint16_t* io = split.gba->memory.io;
	int mode = io[0] & 7;
	if (mode > 1 || (mode == 1 && bg > 1)) return false;
	uint16_t cnt = io[(0x08 >> 1) + bg];
	int hofs = io[(0x10 >> 1) + bg * 2] & 0x1FF, vofs = io[(0x12 >> 1) + bg * 2] & 0x1FF;
	int size = cnt >> 14, sb = (cnt >> 8) & 0x1F;
	int wide = size & 1, tall = size >> 1;
	int ty = ((y + vofs) >> 3) & (tall ? 63 : 31);
	const uint16_t* vram = split.gba->video.vram;
	for (int c = 0; c < GBA_W / 8 + 1; ++c) {
		int tx = ((hofs >> 3) + c) & (wide ? 63 : 31);
		int block = sb + (tx >> 5) + ((ty >> 5) << wide);
		uint16_t e = vram[block * 0x400 + (ty & 31) * 32 + (tx & 31)];
		if ((e & 0x3FF) && (e >> 12) == pal) return true;
	}
	return false;
}

static int copyAbove; /* emu_hud_copy_above */
static int splitRows; /* emu_hud_rows */
static bool splitCopying; /* emu_hud_copy: the HUD's layers left on the game too */

static void split_draw(struct GBAVideoRenderer* r, int y) {
	if (split.active && copyAbove) {
		bool off = y >= copyAbove;
		for (int i = 0; i < 4; ++i) {
			if ((split.bgMask >> i) & 1) r->disableBG[i] = off;
		}
	}
	if (split.active && splitRows && !splitCopying) {
		/* the HUD's layers leave the game only on its rows; below, the
		 * game draws them as it does, behind or in front of the rest */
		bool off = y < splitRows;
		for (int i = 0; i < 4; ++i) {
			if ((split.bgMask >> i) & 1) r->disableBG[i] = off;
		}
	}
	if (split.active) {
		if (split.prioBelow) {
			/* the game moves its map and menus between layers (between
			 * frames and, as a panel slides, between lines): pick them for
			 * each line as it's drawn */
			unsigned m = bgs_below(split.prioBelow) & ~split.keepBgs;
			if (m != split.bgMask) {
				split.bgMask = m;
				for (int i = 0; i < 4; ++i) r->disableBG[i] = (m >> i) & 1;
				if (!split.grabbing) split_layers(m, split.nObjs > 0 || split.nKeep > 0);
			}
		}
		if (y == 0 && split.memN) {
			/* emu_hud_mem_layers: decided as the frame starts drawing, after
			 * the game's vblank code has run (it sets the flag and draws what
			 * goes with it in the same vblank) */
			bool match = !split.memAny;
			for (int i = 0; i < split.memN && match != split.memAny; ++i) match = (split_read(split.memAddr[i], split.memSize[i]) & split.memMask[i]) == split.memVal[i];
			unsigned m = split.baseMask | (match ? split.memLayers : 0);
			if (m != split.bgMask) {
				split.bgMask = split.lineHud = split.lineGame = m;
				for (int i = 0; i < 4; ++i) r->disableBG[i] = (m >> i) & 1;
				if (!split.grabbing) split_layers(m, split.nObjs > 0 || split.nKeep > 0);
			}
		}
		if (split.memGamePal) {
			/* emu_hud_mem_layers' gamePal: on a line where one of those
			 * layers has a tile in that palette (Mario & Luigi's speech
			 * balloons, beside the boxes' text in another), the layer stays
			 * on the game there and is left out of the HUD (ML-5) */
			unsigned off = 0;
			for (int i = 0; i < 4; ++i) {
				if (((split.memLayers & split.bgMask) >> i) & 1 && bg_line_has_pal(i, y, split.memGamePal - 1)) off |= 1u << i;
			}
			if (y == 0 || off != split.memLineOff) {
				for (int i = 0; i < 4; ++i) {
					if ((split.memLayers >> i) & 1) r->disableBG[i] = ((split.bgMask & ~off) >> i) & 1;
				}
				if (!split.grabbing) split_layers(split.bgMask & ~off, split.nObjs > 0 || split.nKeep > 0);
				split.memLineOff = off;
			}
		}
		if (y == 0 && split.grabBuf) {
			if (split.grabCnt) split.grabMask = bgs_with_cnt(split.grabCnt);
			/* this frame: the grab layers into grabBuf, no sprites */
			split.grabbing = true;
			split.hud.outputBuffer = (mColor*) split.grabBuf;
			split_layers(split.grabMask, false);
		}
		if (bandOn && !split.prioBelow) {
			/* the band: lines with sprites off (the rest of the screen has
			 * them on) where layers are set up differently from elsewhere:
			 * those layers go into the HUD, and out of the game unless
			 * they're copied. Not when last frame's band was most of the
			 * screen (a full-screen scene, not a box). */
			const uint16_t* io = split.gba->memory.io;
			unsigned band = 0;
			static bool inBand;
			static int tail; /* lines of the box with the sprites back on */
			if (y == 0) split.bandY0 = GBA_H, split.bandY1 = -1, inBand = false;
			unsigned differs = 0;
			for (int i = 0; i < 4; ++i) {
				if (((io[0] >> (8 + i)) & 1) && bandNormal[i] && io[(0x08 >> 1) + i] != bandNormal[i]) differs |= 1u << i;
			}
			/* (the box's last few lines can have the sprites back on already) */
			if (!(io[0] & 0x1000)) inBand = true, tail = 0;
			else inBand = inBand && differs && ++tail <= 8;
			if (!inBand) {
				for (int i = 0; i < 4; ++i) bandNormal[i] = io[(0x08 >> 1) + i];
			} else {
				if (lastBandY1 - lastBandY0 < 100 && !bandWatch) band = differs;
				split.bandY0 = y < split.bandY0 ? y : split.bandY0, split.bandY1 = y;
			}
			unsigned hud = split.bgMask | band, game = split.bgMask | (bandCopy ? 0 : band);
			if (game != split.lineGame) {
				split.lineGame = game;
				for (int i = 0; i < 4; ++i) r->disableBG[i] = (game >> i) & 1;
			}
			if (hud != split.lineHud) {
				split.lineHud = hud;
				if (!split.grabbing) split_layers(hud, split.nObjs > 0 || split.nKeep > 0);
			}
		}
		filtered_draw_scanline(&split.hud.d, y);
	}
	split.drawScanline(r, y);
}
static void split_finish(struct GBAVideoRenderer* r) {
	if (split.active && bandOn) lastBandY0 = split.bandY0, lastBandY1 = split.bandY1;
	split.finishFrame(r);
	split.hud.d.finishFrame(&split.hud.d);
	if (split.grabbing) {
		split.grabbing = false;
		split.grabBuf = NULL;
		split.grabbed = true;
		split.hud.outputBuffer = (mColor*) split.hudBuf;
		split_layers(split.bgMask, split.nObjs > 0 || split.nKeep > 0);
	}
}
static void split_reset(struct GBAVideoRenderer* r) {
	split.reset(r);
	split.hud.d.reset(&split.hud.d);
}

/* the HUD renderer takes on the video state as it is now */
static void split_resync(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (!gba || !split.installed || gba->video.renderer != split.main) return; /* another core (logo, map capture) */
	struct GBAVideoRenderer* h = &split.hud.d;
	h->reset(h);
	for (uint32_t a = 0; a <= 0x56; a += 2) h->writeVideoRegister(h, a, hud_reg(a, gba->memory.io[a >> 1]));
	for (uint32_t a = 0; a < 0x400; a += 2) h->writePalette(h, a, a == 0 ? HUD_CLEAR_555 : gba->video.palette[a >> 1]);
	for (uint32_t i = 0; i < 512; ++i) h->writeOAM(h, i);
	split.hud.oamDirty = true;
}

static void split_release(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (!gba || !split.installed || gba->video.renderer != split.main) return; /* another core (logo, map capture) */
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(&split.hud.d);
	if (s) memset(s, 0, sizeof(*s));
	pthread_mutex_unlock(&hideLock);
	split.hud.d.deinit(&split.hud.d);
	memset(&split, 0, sizeof(split));
	/* (and what the last game set beside it: Pokemon Pinball's tall view leaves the HUD's
	 * layers on the game (emu_hud_copy, emu_hud_copy_above), and a game started after it in
	 * the same run kept its HUD on both screens) */
	splitCopying = false;
	copyAbove = 0;
}

bool emu_hud_split(Emu* e, bool on, unsigned bgMask, const SpriteRule* objs, int nObjs, uint32_t* hud) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r) return false;
	if (!split.installed) {
		if (!on) return true;
		memset(&split, 0, sizeof(split));
		splitCopying = false; /* (emu_hud_copy needs the split installed: nothing to keep) */
		GBAVideoSoftwareRendererCreate(&split.hud);
		split.hud.outputBuffer = (mColor*) hud;
		split.hudBuf = hud;
		split.hud.outputBufferStride = GBA_W;
		struct GBAVideoRenderer* h = &split.hud.d;
		h->palette = gba->video.palette;
		h->vram = gba->video.vram;
		h->oam = &gba->video.oam;
		h->cache = NULL;
		h->init(h);
		split.main = r;
		split.gba = gba;
		split.writeVideoRegister = r->writeVideoRegister, r->writeVideoRegister = split_reg;
		split.writeVRAM = r->writeVRAM, r->writeVRAM = split_vram;
		split.writePalette = r->writePalette, r->writePalette = split_palette;
		split.writeOAM = r->writeOAM, r->writeOAM = split_oam;
		split.drawScanline = r->drawScanline, r->drawScanline = split_draw;
		split.finishFrame = r->finishFrame, r->finishFrame = split_finish;
		split.reset = r->reset, r->reset = split_reset;
		/* the HUD renderer's own sprite filter: only the HUD's sprites */
		pthread_mutex_lock(&hideLock);
		HideSlot* s = slot_for(NULL);
		if (s) {
			s->renderer = h;
			s->orig = h->drawScanline;
			s->n = 0, s->nRetile = 0;
		}
		pthread_mutex_unlock(&hideLock);
		split.installed = true;
		split_resync(e);
	}
	split.bgMask = split.baseMask = bgMask;
	split.lineHud = split.lineGame = bgMask;
	if (!on) lastBandY0 = 0, lastBandY1 = -1;
	split.nObjs = nObjs < 12 ? nObjs : 12;
	if (split.nObjs) memcpy(split.objs, objs, sizeof(*objs) * split.nObjs);
	/* the HUD: those layers and sprites only */
	struct GBAVideoRenderer* h = &split.hud.d;
	if (!split.grabbing) split_layers(bgMask, split.nObjs > 0 || split.nKeep > 0);
	pthread_mutex_lock(&hideLock);
	HideSlot* hs = slot_for(h);
	if (hs) {
		hs->n = split.nObjs;
		memcpy(hs->rules, split.objs, sizeof(split.objs));
		hs->keepOnly = true;
	}
	pthread_mutex_unlock(&hideLock);
	split.hud.oamDirty = true;
	/* the game: without them (the rules can change while on: another scene) */
	for (int i = 0; i < 4; ++i) r->disableBG[i] = on && ((bgMask >> i) & 1);
	emu_hide_sprites(e, on ? split.objs : NULL, on ? split.nObjs : 0);
	split.active = on;
	if (gameRegBits) {
		/* the register as it is now, with or without those bits */
		uint16_t v = gba->memory.io[gameRegAddr >> 1];
		split.writeVideoRegister(r, gameRegAddr, on ? v & ~gameRegBits : v);
		split.hud.d.writeVideoRegister(&split.hud.d, gameRegAddr, hud_reg(gameRegAddr, on ? v & ~gameRegBits : v));
	}
	return true;
}

void emu_hud_copy(Emu* e, bool copy) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r || !split.installed || !split.active) return;
	splitCopying = copy;
	for (int i = 0; i < 4; ++i) r->disableBG[i] = !copy && ((split.bgMask >> i) & 1);
	emu_hide_sprites(e, copy ? NULL : split.objs, copy ? 0 : split.nObjs);
}

void emu_hud_keep_exact(const uint16_t (*attrs)[3], int n) {
	if (!split.installed) return;
	struct GBAVideoRenderer* h = &split.hud.d;
	n = n > 128 ? 128 : n;
	pthread_mutex_lock(&hideLock);
	HideSlot* hs = slot_for(h);
	if (hs) {
		hs->nExact = n, hs->exactKeep = true;
		if (n) memcpy(hs->exact, attrs, sizeof(*attrs) * n);
	}
	pthread_mutex_unlock(&hideLock);
	split.nKeep = hs ? n : 0;
	if (!split.grabbing) split_layers(split.bgMask, split.nObjs > 0 || split.nKeep > 0);
	split.hud.oamDirty = true;
}

void emu_hud_place(const uint8_t how[128], const uint16_t ab[128][2]) {
	if (!split.installed) return;
	pthread_mutex_lock(&hideLock);
	bool changed;
	if (how) {
		changed = placeRenderer != &split.hud.d || memcmp(placeHow, how, sizeof(placeHow)) || memcmp(placeAB, ab, sizeof(placeAB));
		memcpy(placeHow, how, sizeof(placeHow));
		memcpy(placeAB, ab, sizeof(placeAB));
		placeRenderer = &split.hud.d;
	} else {
		changed = placeRenderer != NULL;
		placeRenderer = NULL;
	}
	pthread_mutex_unlock(&hideLock);
	if (changed) split.hud.oamDirty = true;
}

void emu_hud_retile(const int* oamIndex, const uint16_t* tile, int n) {
	if (!split.installed) return;
	n = n > 8 ? 8 : n;
	pthread_mutex_lock(&hideLock);
	HideSlot* hs = slot_for(&split.hud.d);
	bool changed = false;
	if (hs) {
		changed = hs->nRetile != n;
		for (int i = 0; i < n; ++i) changed = changed || hs->retileIdx[i] != oamIndex[i] || hs->retileTile[i] != tile[i];
		hs->nRetile = n;
		for (int i = 0; i < n; ++i) hs->retileIdx[i] = (int8_t) oamIndex[i], hs->retileTile[i] = tile[i];
	}
	pthread_mutex_unlock(&hideLock);
	if (changed) split.hud.oamDirty = true;
}

void emu_hud_copy_above(int row) {
	copyAbove = row;
}

void emu_hud_rows(int rows) {
	splitRows = rows;
}

void emu_hud_band(bool on, bool copy, bool watch) {
	if (on != bandOn) memset(bandNormal, 0, sizeof(bandNormal)), lastBandY0 = 0, lastBandY1 = -1;
	bandOn = on;
	bandCopy = copy;
	bandWatch = watch;
}

bool emu_hud_band_rows(int* y0, int* y1) {
	*y0 = lastBandY0, *y1 = lastBandY1;
	/* (a band that's most of the screen isn't one) */
	return lastBandY1 >= lastBandY0 && lastBandY1 - lastBandY0 < 100;
}

void emu_hud_game_reg(uint32_t addr, uint16_t bits) {
	gameRegAddr = addr;
	gameRegBits = bits;
}

uint32_t emu_hud_clear(void) {
	/* exactly what the HUD renderer writes for its backdrop */
	return split.installed ? (uint32_t) split.hud.normalPalette[0] & 0xFFFFFF : 0xFF00FF;
}

void emu_hud_by_priority(int prioBelow) {
	split.prioBelow = prioBelow;
}

void emu_hud_keep_bgs(unsigned mask) {
	split.keepBgs = mask;
}

void emu_hud_mem_layers(const uint32_t addr[2], const uint32_t mask[2], const uint32_t value[2], const int size[2], int n, bool any, unsigned layers, int gamePal) {
	split.memN = n < 0 ? 0 : n > 2 ? 2 : n;
	split.memGamePal = split.memN ? gamePal : 0;
	split.memLineOff = 0;
	split.memAny = any;
	for (int i = 0; i < split.memN; ++i) split.memAddr[i] = addr[i], split.memMask[i] = mask[i], split.memVal[i] = value[i], split.memSize[i] = size[i];
	split.memLayers = layers;
}

uint16_t emu_io16(Emu* e, uint32_t offset) {
	struct GBA* gba = gba_of(e);
	return gba ? gba->memory.io[(offset & 0x3FE) >> 1] : 0;
}

void emu_vram_write(Emu* e, uint32_t off, const void* src, size_t n) {
	struct GBA* gba = gba_of(e);
	if (!gba || off >= 0x18000 || n > 0x18000 - off) return;
	memcpy((uint8_t*) gba->video.vram + off, src, n);
	/* (the software renderer's hook only drops its caches, whatever the address) */
	gba->video.renderer->writeVRAM(gba->video.renderer, off);
}

void emu_oam_read(Emu* e, uint16_t* out) {
	struct GBA* gba = gba_of(e);
	if (!gba) {
		memset(out, 0, 512 * sizeof(uint16_t));
		return;
	}
	memcpy(out, gba->video.oam.raw, sizeof(gba->video.oam.raw));
}
void emu_oam_write(Emu* e, const uint16_t* src) {
	struct GBA* gba = gba_of(e);
	if (!gba) return;
	for (int i = 0; i < 512; ++i) {
		if (gba->video.oam.raw[i] == src[i]) continue;
		gba->video.oam.raw[i] = src[i];
		gba->video.renderer->writeOAM(gba->video.renderer, (uint32_t) i);
	}
}

const uint16_t* emu_vram(Emu* e) {
	struct GBA* gba = gba_of(e);
	return gba ? gba->video.vram : NULL;
}

void emu_hud_grab(Emu* e, unsigned bgMask, uint32_t* buf) {
	(void) e;
	if (!split.installed || !split.active) return;
	split.grabMask = bgMask;
	split.grabCnt = 0;
	split.grabBuf = buf;
	split.grabbed = false;
}

void emu_hud_grab_cnt(Emu* e, uint16_t cnt, uint32_t* buf) {
	emu_hud_grab(e, 0, buf);
	split.grabCnt = cnt;
}

bool emu_hud_grabbed(void) {
	bool g = split.grabbed;
	split.grabbed = false;
	return g;
}

/* ---------------------------------------------------------------- cheats */

void emu_cheats_clear(Emu* e) {
	struct mCheatDevice* device = e->core->cheatDevice(e->core);
	if (!device) return;
	/* mCheatDeviceClear only frees the sets: a Code Breaker hook (the
	 * "Enable Code") would stay patched into the ROM, and the next hook
	 * would save that patch as the original instruction and loop forever.
	 * Switch each set off (restores ROM patches) and remove it (restores
	 * the hooked instruction) first. */
	while (mCheatSetsSize(&device->cheats)) {
		struct mCheatSet* set = *mCheatSetsGetPointer(&device->cheats, mCheatSetsSize(&device->cheats) - 1);
		set->enabled = false;
		mCheatRefresh(device, set);
		mCheatRemoveSet(device, set);
		mCheatSetDeinit(set);
	}
}

static bool is_hex(const char* s, size_t n) {
	for (size_t i = 0; i < n; ++i) {
		if (!isxdigit((unsigned char) s[i])) return false;
	}
	return true;
}

int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n, int type) {
	struct mCheatDevice* device = e->core->cheatDevice(e->core);
	if (!device) return 0;
	int mtype = type == EMU_CHEAT_CODEBREAKER ? GBA_CHEAT_CODEBREAKER :
	            type == EMU_CHEAT_GAMESHARK ? GBA_CHEAT_GAMESHARK :
	            type == EMU_CHEAT_ACTION_REPLAY ? GBA_CHEAT_PRO_ACTION_REPLAY : GBA_CHEAT_AUTODETECT;
	struct mCheatSet* set = device->createSet(device, name);
	int added = 0;
	for (int c = 0; c < n; ++c) {
		/* split into hex words on '+' and spaces, then pair them up:
		 * 8 + 4 digits is a Code Breaker line, 8 + 8 a GameShark / AR one */
		char words[64][20];
		int nw = 0;
		const char* p = codes[c];
		while (*p && nw < 64) {
			while (*p == '+' || isspace((unsigned char) *p)) ++p;
			const char* q = p;
			while (*q && *q != '+' && !isspace((unsigned char) *q)) ++q;
			size_t len = (size_t) (q - p);
			if (len && len < sizeof(words[0])) {
				memcpy(words[nw], p, len);
				words[nw++][len] = 0;
			}
			p = q;
		}
		for (int w = 0; w + 1 < nw; w += 2) {
			size_t l1 = strlen(words[w]), l2 = strlen(words[w + 1]);
			if (l1 != 8 || (l2 != 4 && l2 != 8) || !is_hex(words[w], l1) || !is_hex(words[w + 1], l2)) {
				fprintf(stderr, "cheat %s: skipping \"%s %s\"\n", name ? name : "", words[w], words[w + 1]);
				continue;
			}
			char line[20];
			snprintf(line, sizeof(line), "%s %s", words[w], words[w + 1]);
			int t = l2 == 4 ? GBA_CHEAT_CODEBREAKER : mtype == GBA_CHEAT_CODEBREAKER ? GBA_CHEAT_AUTODETECT : mtype;
			if (mCheatAddLine(set, line, t)) ++added;
			else fprintf(stderr, "cheat %s: mGBA rejected \"%s\"\n", name ? name : "", line);
		}
	}
	set->enabled = true;
	mCheatAddSet(device, set);
	return added;
}

/* ------------------------------------------------------------ widescreen (docs/WIDESCREEN.md)
 *
 * A GBA DS widescreen patch (docs/WIDESCREEN.md) puts a 32-byte marker in the
 * ROM: "GBADS-WIDESCREEN", a version, the aspect ratio's two numbers and the
 * picture's size (284 x 160 for 16:9, 256 x 160 for 16:10, 240 x 180 for 4:3).
 * The game's own picture stays mGBA's 240 x 160; the margins around it come
 * from two more software renderers fed the same writes as the game's, each
 * looking a margin's width to one side: text backgrounds through mGBA's own
 * layer offsets, sprites through their own copy of OAM with the positions
 * moved, affine backgrounds through their reference points moved, windows
 * through their edges moved (a window to the screen's edge reaches the
 * margin's). What the margins show is the game's own drawing past the edge;
 * the patch makes the game draw it properly (scenery loaded that far,
 * sprites not culled at 240). One axis at a time: a wider or a taller
 * picture. Only the game's own core (emu_wide_on); the hidden copies, logos
 * and captures stay 240 x 160. */

#define WIDE_MAGIC "GBADS-WIDESCREEN"

bool emu_wide_parse(const uint8_t* rom, size_t size, EmuWide* out) {
	memset(out, 0, sizeof(*out));
	if (!rom || size < 32) return false;
	for (size_t at = 0; at + 32 <= size; at += 16) {
		const uint8_t* p = rom + at;
		if (p[0] != 'G' || memcmp(p, WIDE_MAGIC, 16)) continue;
		int ver = p[16], num = p[17], den = p[18];
		int w = p[20] | p[21] << 8, h = p[22] | p[23] << 8;
		if (ver != 1 || !num || !den) continue;
		if (w < GBA_W || h < GBA_H || (w - GBA_W) % 2 || (h - GBA_H) % 2) continue;
		if (w > GBA_W + 2 * WIDE_MARGIN_MAX || h > GBA_H + 2 * WIDE_MARGIN_MAX) continue;
		if (w > GBA_W && h > GBA_H) continue; /* (one axis at a time) */
		out->found = true;
		out->num = num, out->den = den, out->w = w, out->h = h;
		out->offset = (uint32_t) at;
		return true;
	}
	return false;
}

bool emu_wide_ratio(int num, int den, EmuWide* out) {
	memset(out, 0, sizeof(*out));
	if (num <= 0 || den <= 0 || num > 255 || den > 255) return false;
	int w = GBA_W, h = GBA_H;
	if (num * GBA_H >= den * GBA_W) {
		w = (GBA_H * num * 2 + den) / (2 * den); /* (rounded) */
		w -= (w - GBA_W) % 2;
	} else {
		h = (GBA_W * den * 2 + num) / (2 * num);
		h -= (h - GBA_H) % 2;
	}
	if (w > GBA_W + 2 * WIDE_MARGIN_MAX || h > GBA_H + 2 * WIDE_MARGIN_MAX) return false;
	*out = (EmuWide) { .found = true, .num = num, .den = den, .w = w, .h = h };
	return true;
}

bool emu_wide_find(const Emu* e, EmuWide* out) {
	struct GBA* gba = gba_of(e);
	memset(out, 0, sizeof(*out));
	if (!gba || !gba->memory.rom) return false;
	return emu_wide_parse((const uint8_t*) gba->memory.rom, gba->memory.romSize, out);
}

typedef struct {
	bool installed;
	bool on; /* drawing the margins (installed stays: emu_wide_set off keeps the hooks, idle) */
	struct GBA* gba;
	struct GBAVideoRenderer* main;
	int w, h, mx, my;
	bool vert;                            /* a taller picture, instead of a wider one */
	int d[2];                             /* [0] left / top, [1] right / bottom: what the game has at a column (row) of that renderer, less the column (row) */
	struct GBAVideoSoftwareRenderer ext[2];
	union GBAOAM oam[2];
	uint32_t* buf[2];                     /* their pictures, GBA_W x GBA_H */
	uint32_t* out;                        /* the whole picture, w x h */
	int32_t ref[2][2];                    /* BG2, BG3: X, Y as the game wrote them */
	int16_t pa[2], pb[2], pc[2], pd[2];
	uint16_t (*writeVideoRegister)(struct GBAVideoRenderer*, uint32_t, uint16_t);
	void (*writeVRAM)(struct GBAVideoRenderer*, uint32_t);
	void (*writePalette)(struct GBAVideoRenderer*, uint32_t, uint16_t);
	void (*writeOAM)(struct GBAVideoRenderer*, uint32_t);
	void (*drawScanline)(struct GBAVideoRenderer*, int);
	void (*finishFrame)(struct GBAVideoRenderer*);
	void (*reset)(struct GBAVideoRenderer*);
} Wide;
static Wide wide;

/* a window's edges (start << 8 | end, as WINxH / WINxV) moved by d: an edge at
 * the screen's own edge stays there, so a window across the screen covers the
 * margins too */
static int wide_clamp(int v, int lo, int hi) {
	return v < lo ? lo : v > hi ? hi : v;
}
static uint16_t wide_win(uint16_t v, int d, int lim) {
	int s = v >> 8, e = v & 0xFF;
	if (e > lim) e = lim;
	if (s > e) return v; /* (wrapping round: left as it is) */
	int s2 = s == 0 ? 0 : wide_clamp(s - d, 0, lim);
	int e2 = e >= lim ? lim : wide_clamp(e - d, 0, lim);
	if (s2 >= e2) s2 = e2 = lim; /* (nothing of it in this renderer's view) */
	return (uint16_t) (s2 << 8 | e2);
}

static void wide_ref_write(int k, int bg, int axis) {
	struct GBAVideoRenderer* x = &wide.ext[k].d;
	int i = bg - 2, d = wide.d[k];
	int32_t v = wide.ref[i][axis];
	if (!wide.vert) v += d * (axis ? wide.pc[i] : wide.pa[i]);
	else v += d * (axis ? wide.pd[i] : wide.pb[i]);
	uint32_t a = 0x28 + (uint32_t) i * 0x10 + (uint32_t) axis * 4;
	x->writeVideoRegister(x, a, (uint16_t) (v & 0xFFFF));
	x->writeVideoRegister(x, a + 2, (uint16_t) ((v >> 16) & 0x0FFF));
}

static void wide_ext_reg(int k, uint32_t a, uint16_t v) {
	struct GBAVideoRenderer* x = &wide.ext[k].d;
	if (a >= 0x20 && a < 0x40) {
		int i = (int) (a - 0x20) / 0x10, r = (int) (a & 0xF);
		switch (r) {
		case 0x0: wide.pa[i] = (int16_t) v; break;
		case 0x2: wide.pb[i] = (int16_t) v; break;
		case 0x4: wide.pc[i] = (int16_t) v; break;
		case 0x6: wide.pd[i] = (int16_t) v; break;
		default: {
			int axis = r >= 0xC;
			uint32_t u = (uint32_t) wide.ref[i][axis];
			u = (r & 2) ? (u & 0xFFFF) | ((uint32_t) v << 16) : (u & 0xFFFF0000u) | v;
			wide.ref[i][axis] = (int32_t) (u << 4) >> 4;
			wide_ref_write(k, i + 2, axis);
			return;
		}
		}
		x->writeVideoRegister(x, a, v);
		return;
	}
	if (!wide.vert && (a == 0x40 || a == 0x42)) v = wide_win(v, wide.d[k], GBA_W);
	if (wide.vert && (a == 0x44 || a == 0x46)) v = wide_win(v, wide.d[k], GBA_H);
	x->writeVideoRegister(x, a, v);
}

static void wide_oam_copy(int k, uint32_t i) {
	uint16_t v = wide.gba->video.oam.raw[i];
	if (!wide.vert && (i & 3) == 1) v = (uint16_t) ((v & ~0x1FF) | ((v - wide.d[k]) & 0x1FF));
	if (wide.vert && (i & 3) == 0) v = (uint16_t) ((v & ~0xFF) | ((v - wide.d[k]) & 0xFF));
	wide.oam[k].raw[i] = v;
}

static uint16_t wide_reg(struct GBAVideoRenderer* r, uint32_t a, uint16_t v) {
	uint16_t out = wide.writeVideoRegister(r, a, v);
	/* (the affine copies are the game's; written once, then moved for each) */
	for (int k = 0; k < 2; ++k) {
		if (k && a >= 0x20 && a < 0x40) {
			int i = (int) (a - 0x20) / 0x10, rr = (int) (a & 0xF);
			if (rr >= 0x8) wide_ref_write(1, i + 2, rr >= 0xC);
			else wide.ext[1].d.writeVideoRegister(&wide.ext[1].d, a, v);
			continue;
		}
		wide_ext_reg(k, a, v);
	}
	return out;
}
static void wide_vram(struct GBAVideoRenderer* r, uint32_t a) {
	wide.writeVRAM(r, a);
	for (int k = 0; k < 2; ++k) wide.ext[k].d.writeVRAM(&wide.ext[k].d, a);
}
static void wide_palette(struct GBAVideoRenderer* r, uint32_t a, uint16_t v) {
	wide.writePalette(r, a, v);
	for (int k = 0; k < 2; ++k) wide.ext[k].d.writePalette(&wide.ext[k].d, a, v);
}
static void wide_oam(struct GBAVideoRenderer* r, uint32_t i) {
	wide.writeOAM(r, i);
	for (int k = 0; k < 2; ++k) {
		wide_oam_copy(k, i & 511);
		wide.ext[k].d.writeOAM(&wide.ext[k].d, i);
	}
}
static void wide_draw(struct GBAVideoRenderer* r, int y) {
	wide.drawScanline(r, y);
	for (int k = 0; wide.on && k < 2; ++k) {
		/* the top margin's renderer is needed only for its rows; each
		 * frame starts it again (finishFrame) */
		if (wide.vert && k == 0 && y >= wide.my) continue;
		struct GBAVideoRenderer* x = &wide.ext[k].d;
		for (int i = 0; i < 4; ++i) x->disableBG[i] = r->disableBG[i]; /* (a HUD moved off the game: off the margins too) */
		x->disableOBJ = r->disableOBJ;
		x->drawScanline(x, y);
	}
}
static void wide_finish(struct GBAVideoRenderer* r) {
	wide.finishFrame(r);
	for (int k = 0; k < 2; ++k) wide.ext[k].d.finishFrame(&wide.ext[k].d);
}

/* mGBA's layer offsets for text backgrounds (reset clears them) */
static void wide_offsets(void) {
	for (int k = 0; k < 2; ++k) {
		for (int i = 0; i < 4; ++i) {
			wide.ext[k].bg[i].offsetX = wide.vert ? 0 : (int16_t) -wide.d[k];
			wide.ext[k].bg[i].offsetY = wide.vert ? (int16_t) -wide.d[k] : 0;
		}
	}
}

/* the margins' renderers take on the video state as it is now */
static void wide_resync(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (!gba || !wide.installed || gba != wide.gba) return;
	for (int k = 0; k < 2; ++k) {
		struct GBAVideoRenderer* x = &wide.ext[k].d;
		x->reset(x);
	}
	wide_offsets();
	for (uint32_t a = 0; a <= 0x56; a += 2) {
		uint16_t v = gba->memory.io[a >> 1];
		for (int k = 0; k < 2; ++k) {
			if (k && a >= 0x20 && a < 0x40) {
				int i = (int) (a - 0x20) / 0x10, rr = (int) (a & 0xF);
				if (rr >= 0x8) wide_ref_write(1, i + 2, rr >= 0xC);
				else wide.ext[1].d.writeVideoRegister(&wide.ext[1].d, a, v);
				continue;
			}
			wide_ext_reg(k, a, v);
		}
	}
	for (int k = 0; k < 2; ++k) {
		struct GBAVideoRenderer* x = &wide.ext[k].d;
		for (uint32_t a = 0; a < 0x400; a += 2) x->writePalette(x, a, gba->video.palette[a >> 1]);
		for (uint32_t i = 0; i < 512; ++i) wide_oam_copy(k, i), x->writeOAM(x, i);
		wide.ext[k].oamDirty = true;
	}
}
static void wide_reset(struct GBAVideoRenderer* r) {
	wide.reset(r);
	for (int k = 0; k < 2; ++k) wide.ext[k].d.reset(&wide.ext[k].d);
	wide_offsets();
}

/* the picture's size and the margins' looks (the hooks as they are) */
static bool wide_size(const EmuWide* w) {
	uint32_t* out = calloc((size_t) w->w * (size_t) w->h, sizeof(uint32_t));
	if (!out) return false;
	free(wide.out);
	wide.out = out;
	wide.w = w->w, wide.h = w->h;
	wide.mx = (w->w - GBA_W) / 2, wide.my = (w->h - GBA_H) / 2;
	wide.vert = wide.my > 0;
	int m = wide.vert ? wide.my : wide.mx;
	wide.d[0] = -m, wide.d[1] = m;
	return true;
}

bool emu_wide_on(Emu* e, const EmuWide* w) {
	struct GBA* gba = gba_of(e);
	struct GBAVideoRenderer* r = gba ? gba->video.renderer : NULL;
	if (!r || wide.installed || !w || !w->found || e->gbBorder) return false;
	memset(&wide, 0, sizeof(wide));
	for (int k = 0; k < 2; ++k) wide.buf[k] = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	if (!wide.buf[0] || !wide.buf[1] || !wide_size(w)) {
		free(wide.out), free(wide.buf[0]), free(wide.buf[1]);
		memset(&wide, 0, sizeof(wide));
		return false;
	}
	int m = wide.vert ? wide.my : wide.mx;
	for (int k = 0; k < 2; ++k) {
		GBAVideoSoftwareRendererCreate(&wide.ext[k]);
		wide.ext[k].outputBuffer = (mColor*) wide.buf[k];
		wide.ext[k].outputBufferStride = GBA_W;
		struct GBAVideoRenderer* x = &wide.ext[k].d;
		x->palette = gba->video.palette;
		x->vram = gba->video.vram;
		x->oam = &wide.oam[k];
		x->cache = NULL;
		x->init(x);
	}
	wide.gba = gba;
	wide.main = r;
	wide.writeVideoRegister = r->writeVideoRegister, r->writeVideoRegister = wide_reg;
	wide.writeVRAM = r->writeVRAM, r->writeVRAM = wide_vram;
	wide.writePalette = r->writePalette, r->writePalette = wide_palette;
	wide.writeOAM = r->writeOAM, r->writeOAM = wide_oam;
	wide.drawScanline = r->drawScanline, r->drawScanline = wide_draw;
	wide.finishFrame = r->finishFrame, r->finishFrame = wide_finish;
	wide.reset = r->reset, r->reset = wide_reset;
	wide.installed = wide.on = true;
	wide_resync(e);
	fprintf(stderr, "widescreen: %d:%d, %d x %d (margins %d)\n", w->num, w->den, w->w, w->h, m);
	return true;
}

bool emu_wide_set(Emu* e, const EmuWide* w) {
	bool want = w && w->found;
	if (!wide.installed) return want ? emu_wide_on(e, w) : true;
	struct GBA* gba = gba_of(e);
	if (!gba || gba != wide.gba) return false;
	if (!want) {
		wide.on = false;
		fprintf(stderr, "widescreen: off\n");
		return true;
	}
	if (!wide_size(w)) return false;
	wide.on = true;
	wide_resync(e); /* (the margins' renderers looking their new width from here) */
	fprintf(stderr, "widescreen: %d:%d, %d x %d (margins %d)\n", w->num, w->den, w->w, w->h, wide.vert ? wide.my : wide.mx);
	return true;
}

bool emu_wide_active(void) {
	return wide.installed && wide.on;
}
int emu_wide_w(void) {
	return emu_wide_active() ? wide.w : GBA_W;
}
int emu_wide_h(void) {
	return emu_wide_active() ? wide.h : GBA_H;
}

const uint32_t* emu_wide_compose(const uint32_t* centre) {
	if (!emu_wide_active()) return centre;
	int W = wide.w;
	if (!wide.vert) {
		int m = wide.mx;
		for (int y = 0; y < GBA_H; ++y) {
			uint32_t* o = wide.out + (size_t) y * (size_t) W;
			memcpy(o, wide.buf[0] + y * GBA_W, (size_t) m * 4);
			memcpy(o + m, centre + y * GBA_W, GBA_W * 4);
			memcpy(o + m + GBA_W, wide.buf[1] + y * GBA_W + GBA_W - m, (size_t) m * 4);
		}
	} else {
		int m = wide.my;
		memcpy(wide.out, wide.buf[0], (size_t) m * GBA_W * 4);
		memcpy(wide.out + (size_t) m * GBA_W, centre, GBA_W * GBA_H * 4);
		memcpy(wide.out + (size_t) (m + GBA_H) * GBA_W, wide.buf[1] + (GBA_H - m) * GBA_W, (size_t) m * GBA_W * 4);
	}
	return wide.out;
}

static void wide_release(Emu* e) {
	struct GBA* gba = gba_of(e);
	if (!gba || !wide.installed || gba != wide.gba) return;
	for (int k = 0; k < 2; ++k) wide.ext[k].d.deinit(&wide.ext[k].d);
	free(wide.out), free(wide.buf[0]), free(wide.buf[1]);
	memset(&wide, 0, sizeof(wide));
}

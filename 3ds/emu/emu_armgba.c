/* GBA DS's emulator (src/emu.h) on armGBA instead of libmgba: the 3DS port's
 * core (third_party/armgba, its ARM recompiler on the console, its x86-64
 * one in the desktop test build). This file stands where src/emu.c does in
 * the other builds; the rest of GBA DS talks to it through emu.h alone.
 *
 * Each Emu is one armGBA machine (struct GBA): the game, the hidden copy the
 * map captures run on, a logo's. A state is armGBA's own (the machine as it
 * lies in memory), so one copy's state loads into another's directly.
 *
 * Not here yet (see 3ds/README.md): the HUD split (a second renderer fed the
 * same writes), rewind on the console, .7z archives. */
#include "gba.h"
#include "cheats.h"
#include "cpu/jit.h"
#include "video/render.h"

#include "emu.h"
#ifdef __3DS__
#include "host.h"
#endif
#include "patch.h"

#include <pthread.h>
#include <stdarg.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <zlib.h>

/* What Emu.core points at (the other builds' struct mCore). */
typedef struct ArmCore {
	struct GBA* gba;
	struct CheatSet cheats;
	bool cheatsOn;
	/* the battery save's file (NULL: none), and how long the game's writes
	 * have been quiet */
	char* savePath;
	int saveQuiet;
	bool savePending;
	/* sound not yet read (emu_audio_read): stereo frames in a ring */
	int16_t* ring;
	size_t ringCap, ringHead, ringCount;
	/* the ROM's buffer when another core lent it (shared_rom) */
	struct SharedRom* shared;
	unsigned layers;
	/* what its renderer leaves out (emu_layers, emu_hide_sprites) */
	struct RenderHooks hooks;
	SpriteRule rules[12];
	int nRules;
	bool keepOnly; /* hide every sprite that matches none of the rules instead */
	uint16_t exact[128][3];
	int nExact;
	bool exactKeep;
} ArmCore;

static ArmCore* AC(const Emu* e) {
	return (ArmCore*) e->core;
}

uint32_t doCrc32(const void* buf, size_t size) {
	return (uint32_t) crc32(0, buf, (uInt) size);
}

/* ---------------------------------------------- ROM files */

/* A background thread's reading of a ROM can be given up part-way (a logo
 * being made when a game is started): the flag it set for itself. */
static _Thread_local const volatile bool* readCancel;
void emu_read_cancel(const volatile bool* flag) {
	readCancel = flag;
}

static uint8_t* read_file(const char* path, size_t* n) {
	struct stat st;
	*n = 0;
	if (stat(path, &st) || st.st_size <= 0 || st.st_size > 64 * 1024 * 1024) return NULL;
	FILE* f = fopen(path, "rb");
	uint8_t* p = f ? malloc((size_t) st.st_size) : NULL;
#ifdef __3DS__
	if (!f) fprintf(stderr, "%s: can't be opened\n", path);
	else if (!p) fprintf(stderr, "%s: no memory for its %u KiB (%u KiB of the heap free)\n", path, (unsigned) (st.st_size >> 10), (unsigned) (host_heap_free() >> 10));
#endif
#ifdef __3DS__
	/* In pieces: one read of a whole ROM holds the SD card for seconds, and
	 * the game list's own reads (a logo, a thumbnail) wait behind it. */
	size_t got = 0;
	while (p && got < (size_t) st.st_size) {
		size_t want = (size_t) st.st_size - got;
		if (want > 64 * 1024) want = 64 * 1024;
		if (readCancel && *readCancel) break;
		size_t r = fread(p + got, 1, want, f);
		if (!r) {
			fprintf(stderr, "%s: read stopped at %u of %u bytes\n", path, (unsigned) got, (unsigned) st.st_size);
			break;
		}
		got += r;
		host_fs_yield();
	}
	if (p && got == (size_t) st.st_size) *n = got;
#else
	if (p && fread(p, 1, (size_t) st.st_size, f) == (size_t) st.st_size) *n = (size_t) st.st_size;
#endif
	else {
		free(p);
		p = NULL;
	}
	if (f) fclose(f);
	return p;
}

static bool has_ext(const char* name, const char* const* exts) {
	const char* dot = strrchr(name, '.');
	if (!dot) return false;
	for (int i = 0; exts[i]; ++i) {
		if (!strcasecmp(dot, exts[i])) return true;
	}
	return false;
}

/* .7z isn't read here (mGBA's reader did it in the other builds). */
bool emu_is_archive_name(const char* name) {
	static const char* const exts[] = { ".zip", NULL };
	return has_ext(name, exts);
}

bool emu_is_rom_name(const char* name) {
	static const char* const exts[] = { ".gba", ".zip", NULL };
	return has_ext(name, exts);
}

static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}
static uint16_t le16(const uint8_t* p) {
	return (uint16_t) (p[0] | p[1] << 8);
}

/* The first .gba in a .zip: up to `want` bytes of it (0: all), malloc'd. */
static uint8_t* zip_first_rom(const char* path, size_t want, size_t* size) {
	*size = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	uint8_t* out = NULL;
	uint8_t* dir = NULL;
	uint8_t tail[22 + 1024];
	fseek(f, 0, SEEK_END);
	long fileSize = ftell(f);
	long tailN = fileSize < (long) sizeof(tail) ? fileSize : (long) sizeof(tail);
	if (tailN < 22 || fseek(f, fileSize - tailN, SEEK_SET) || fread(tail, 1, (size_t) tailN, f) != (size_t) tailN) goto done;
	long eocd = -1;
	for (long i = tailN - 22; i >= 0; --i) {
		if (le32(tail + i) == 0x06054B50) {
			eocd = i;
			break;
		}
	}
	if (eocd < 0) goto done;
	int entries = le16(tail + eocd + 10);
	uint32_t dirSize = le32(tail + eocd + 12), dirOff = le32(tail + eocd + 16);
	if (dirSize > 16 * 1024 * 1024 || !(dir = malloc(dirSize)) || fseek(f, (long) dirOff, SEEK_SET) ||
	    fread(dir, 1, dirSize, f) != dirSize) {
		goto done;
	}
	uint32_t at = 0;
	for (int i = 0; i < entries && at + 46 <= dirSize && le32(dir + at) == 0x02014B50; ++i) {
		int method = le16(dir + at + 10);
		uint32_t packed = le32(dir + at + 20), full = le32(dir + at + 24), local = le32(dir + at + 42);
		int nameLen = le16(dir + at + 28), extraLen = le16(dir + at + 30), commentLen = le16(dir + at + 32);
		const char* name = (const char*) dir + at + 46;
		at += 46 + (uint32_t) (nameLen + extraLen + commentLen);
		if (at > dirSize || nameLen < 4 || strncasecmp(name + nameLen - 4, ".gba", 4)) continue;
		if ((method != 0 && method != 8) || full == 0 || full > 64 * 1024 * 1024) continue;
		uint8_t lh[30];
		if (fseek(f, (long) local, SEEK_SET) || fread(lh, 1, 30, f) != 30 || le32(lh) != 0x04034B50) break;
		if (fseek(f, le16(lh + 26) + le16(lh + 28), SEEK_CUR)) break;
		size_t n = want && want < full ? want : full;
		out = malloc(n);
		if (!out) break;
		bool ok = false;
		if (method == 0) {
			ok = fread(out, 1, n, f) == n;
		} else {
			z_stream z;
			memset(&z, 0, sizeof(z));
			uint8_t in[16384];
			if (inflateInit2(&z, -MAX_WBITS) == Z_OK) {
				z.next_out = out;
				z.avail_out = (uInt) n;
				uint32_t left = packed;
				int r = Z_OK;
				while (z.avail_out && r == Z_OK) {
					if (!z.avail_in) {
						size_t got = fread(in, 1, left < sizeof(in) ? left : sizeof(in), f);
						if (!got) break;
						left -= (uint32_t) got;
						z.next_in = in;
						z.avail_in = (uInt) got;
					}
					r = inflate(&z, Z_NO_FLUSH);
				}
				ok = !z.avail_out;
				inflateEnd(&z);
			}
		}
		if (ok) *size = n;
		else {
			free(out);
			out = NULL;
		}
		break;
	}
done:
	free(dir);
	fclose(f);
	return out;
}

bool emu_rom_header(const char* path, uint8_t* out, size_t n) {
	if (n > 0xC0) n = 0xC0;
	if (emu_is_archive_name(path)) {
		size_t got = 0;
		uint8_t* p = zip_first_rom(path, n, &got);
		bool ok = p && got == n;
		if (ok) memcpy(out, p, n);
		free(p);
		return ok;
	}
#ifdef __3DS__
	return host_read_head(path, out, n);
#else
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	bool ok = fread(out, 1, n, f) == n;
	fclose(f);
	return ok;
#endif
}

uint8_t* emu_rom_read_all(const char* path, size_t* size) {
	if (emu_is_archive_name(path)) return zip_first_rom(path, 0, size);
	return read_file(path, size);
}

/* ---------------------------------------------- patches (GEN-9)
 * As src/emu.c has them: which patches the game applies (emu_patch_plan),
 * every core that opens that ROM getting them from one cached patched copy. */
#define PLAN_MAX 8
static pthread_mutex_t patchedLock = PTHREAD_MUTEX_INITIALIZER;
static struct {
	char rom[1024];
	char files[PLAN_MAX][1024];
	int n;
} plan;
static struct {
	char key[PLAN_MAX * 1100 + 1100];
	int result;
	char names[256];
	char skipped[256];
	uint32_t fileCrc;
	uint8_t* data;
	size_t size;
} patched;

void emu_patch_plan(const char* rom, char files[][1024], int n) {
	pthread_mutex_lock(&patchedLock);
	snprintf(plan.rom, sizeof(plan.rom), "%s", rom ? rom : "");
	plan.n = 0;
	for (int i = 0; i < n && i < PLAN_MAX; ++i) snprintf(plan.files[plan.n++], sizeof(plan.files[0]), "%s", files[i]);
	pthread_mutex_unlock(&patchedLock);
}

static uint8_t* patched_with(const char* romPath, char files[][1024], int n, size_t* size, int* result, char* patchName,
                             uint32_t* fileCrc, char* skipped) {
	*result = PATCH_NONE;
	if (n > PLAN_MAX) n = PLAN_MAX;
	if (!n) return NULL;
	static char key[sizeof(patched.key)];
	struct stat st;
	pthread_mutex_lock(&patchedLock);
	int k = snprintf(key, sizeof(key), "%s|%lld|%lld", romPath, stat(romPath, &st) ? -1LL : (long long) st.st_size,
	                 stat(romPath, &st) ? -1LL : (long long) st.st_mtime);
	for (int i = 0; i < n && k < (int) sizeof(key); ++i) {
		bool ok = stat(files[i], &st) == 0;
		k += snprintf(key + k, sizeof(key) - (size_t) k, "|%s|%lld|%lld", files[i], ok ? (long long) st.st_size : -1LL,
		              ok ? (long long) st.st_mtime : -1LL);
	}
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

static uint8_t* patched_rom(const char* romPath, size_t* size, int* result, char* patchName, uint32_t* fileCrc,
                            char* skipped) {
	static char files[PLAN_MAX][1024];
	static pthread_mutex_t filesLock = PTHREAD_MUTEX_INITIALIZER;
	int n = 0;
	bool planned = false;
	pthread_mutex_lock(&filesLock);
	pthread_mutex_lock(&patchedLock);
	if (plan.rom[0] && !strcmp(plan.rom, romPath)) {
		planned = true;
		for (int i = 0; i < plan.n; ++i) memcpy(files[n++], plan.files[i], sizeof(files[0]));
	}
	pthread_mutex_unlock(&patchedLock);
	if (!planned && patch_find(romPath, files[0], sizeof(files[0]))) n = 1;
	uint8_t* p = patched_with(romPath, files, n, size, result, patchName, fileCrc, skipped);
	pthread_mutex_unlock(&filesLock);
	return p;
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

/* ---------------------------------------------- one ROM, several cores
 * The game, its hidden copy and its logo's core run the same cartridge. On
 * the console its 32 MiB can't be had twice, so the cores opened on a ROM
 * that another has open (same file, same patches) read that one's buffer;
 * the buffer goes when the last of them closes. */
typedef struct SharedRom {
	char key[1100];
	uint8_t* rom;
	uint32_t romSize, romMask;
	uint32_t crc;
	int users;
	/* what emu_open tells of the ROM, for the cores that borrow it unread
	 * (the console: no second reading of the file) */
	char path[1024];
	int patch;
	char patchName[sizeof(((Emu*) 0)->patchName)];
	uint32_t fileCrc;
} SharedRom;
#define SHARED_MAX 4
static SharedRom sharedRoms[SHARED_MAX];
static pthread_mutex_t sharedLock = PTHREAD_MUTEX_INITIALIZER;

static void shared_key(const char* romPath, const char* patchName, char* key, size_t n) {
	struct stat st;
	bool ok = stat(romPath, &st) == 0;
	snprintf(key, n, "%s|%lld|%lld|%s", romPath, ok ? (long long) st.st_size : -1LL, ok ? (long long) st.st_mtime : -1LL,
	         patchName ? patchName : "");
}

static void split_release(ArmCore* c);
static void split_frame(ArmCore* c);

/* ---------------------------------------------- battery save */

static size_t save_size(struct GBA* gba) {
	return gba->savedata.type == SAVEDATA_AUTODETECT ? 0 : savedataSize(gba);
}

static void save_read(ArmCore* c) {
	size_t n = 0;
	uint8_t* file = read_file(c->savePath, &n);
	if (!file) return;
	if (n > sizeof(c->gba->savedata.storage)) n = sizeof(c->gba->savedata.storage);
	memcpy(c->gba->savedata.storage, file, n);
	free(file);
}

/* Written to a new file, then put in the old one's place. */
static bool save_write(ArmCore* c) {
	size_t n = save_size(c->gba);
	if (!c->savePath || !n) return false;
	size_t len = strlen(c->savePath) + 8;
	char* tmp = malloc(len);
	if (!tmp) return false;
	snprintf(tmp, len, "%s.new", c->savePath);
	FILE* f = fopen(tmp, "wb");
	bool ok = f && fwrite(c->gba->savedata.storage, 1, n, f) == n;
	if (f) ok = fclose(f) == 0 && ok;
	if (ok) {
		remove(c->savePath); /* (FAT: rename doesn't replace) */
		ok = rename(tmp, c->savePath) == 0;
	}
	if (!ok) {
		fprintf(stderr, "save: couldn't write %s\n", c->savePath);
		remove(tmp);
	}
	free(tmp);
	return ok;
}

/* The game's writes to its save go to the file once they've stopped for a
 * quarter of a second (and as the game is closed). */
static void save_tick(ArmCore* c) {
	if (!c->savePath) return;
	if (c->gba->savedata.dirty) {
		c->gba->savedata.dirty = false;
		c->savePending = true;
		c->saveQuiet = 0;
	} else if (c->savePending && ++c->saveQuiet >= 15) {
		c->savePending = false;
		save_write(c);
	}
}

/* ---------------------------------------------- open, close, frames */

static void close_core(ArmCore* c) {
	if (!c) return;
	if (c->gba) {
		if (c->savePath && (c->savePending || c->gba->savedata.dirty)) save_write(c);
		if (c->cheatsOn) cheatsClear(&c->cheats, c->gba);
		if (c->shared) {
			pthread_mutex_lock(&sharedLock);
			c->gba->rom = NULL; /* gbaDestroy frees what's there */
			if (--c->shared->users == 0) {
				free(c->shared->rom);
				memset(c->shared, 0, sizeof(*c->shared));
			}
			pthread_mutex_unlock(&sharedLock);
		}
		/* (its render thread first: it draws with the hooks) */
		renderThreadStop(c->gba);
		split_release(c);
		renderSetHooks(c->gba, NULL);
		gbaDestroy(c->gba);
	}
	free(c->savePath);
	free(c->ring);
	free(c);
}

bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio) {
	memset(e, 0, sizeof(*e));
	ArmCore* c = calloc(1, sizeof(*c));
	if (!c) return false;
	e->core = (struct mCore*) c;
	c->layers = 0x1F;
#ifdef __3DS__
	/* (the game's own core is the one with sound: the large share of the
	 * recompiler's memory, and the console's other cores to draw and mix on) */
	pthread_mutex_lock(&sharedLock);
	{
		/* (the ROM is read after the machine is made: its size, so that the
		 * recompiler leaves it room; a .zip's isn't known, taken as 16 MiB) */
		size_t toLoad = 0;
		bool held = false;
		for (int i = 0; i < SHARED_MAX; ++i) held |= sharedRoms[i].users && !strcmp(sharedRoms[i].path, romPath);
		struct stat rst;
		if (!held) toLoad = emu_is_archive_name(romPath) || stat(romPath, &rst) ? 16u << 20 : (size_t) rst.st_size;
		host_jit_main(audio, toLoad);
	}
	c->gba = gbaCreate();
	pthread_mutex_unlock(&sharedLock);
#else
	c->gba = gbaCreate();
#endif
	if (!c->gba) {
		fprintf(stderr, "armGBA: no memory for a core\n");
		goto fail;
	}
	struct GBA* gba = c->gba;
	c->hooks.context = c;
	renderSetHooks(gba, &c->hooks);
	gbaLoadBios(gba, armgbaBios, SIZE_BIOS);
#ifndef __3DS__
	{
		/* GBADS_BIOS=<a GBA BIOS> (tests: the same BIOS on libmgba and armGBA, 3ds/test/dualrun.py) */
		static uint8_t file[SIZE_BIOS];
		FILE* bf = getenv("GBADS_BIOS") ? fopen(getenv("GBADS_BIOS"), "rb") : NULL;
		if (bf && fread(file, 1, sizeof(file), bf) == sizeof(file)) gbaLoadBios(gba, file, SIZE_BIOS);
		else if (getenv("GBADS_BIOS")) fprintf(stderr, "GBADS_BIOS: can't load %s\n", getenv("GBADS_BIOS"));
		if (bf) fclose(bf);
	}
#endif

	size_t size = 0;
	uint8_t* rom = NULL;
	bool ok = false;
	SharedRom* s = NULL;
	SharedRom* spare = NULL;
#ifdef __3DS__
	/* A core opened on the file another has open takes that one's cartridge
	 * as it is: neither the file read again nor 32 MiB more, which aren't
	 * there to be had. */
	pthread_mutex_lock(&sharedLock);
	for (int i = 0; i < SHARED_MAX; ++i) {
		if (sharedRoms[i].users && !strcmp(sharedRoms[i].path, romPath)) s = &sharedRoms[i];
	}
	if (s) {
		gbaOfferRom(s->rom);
		ok = gbaLoadRom(gba, s->rom, s->romSize);
		if (gbaOfferedRom()) {
			gbaOfferRom(NULL);
			ok = false;
		}
		if (ok) {
			++s->users;
			c->shared = s;
			e->patch = s->patch;
			memcpy(e->patchName, s->patchName, sizeof(e->patchName));
			e->fileCrc = s->fileCrc;
			e->romCrc = s->crc;
			memcpy(e->gameCode, s->romSize >= 0xB0 ? (const char*) s->rom + 0xAC : "\0\0\0\0", 4);
			e->gameCode[4] = 0;
		}
	}
	pthread_mutex_unlock(&sharedLock);
	if (s && !ok) {
		fprintf(stderr, "could not borrow the ROM of %s\n", romPath);
		goto fail;
	}
#endif
	if (!s) {
		rom = emu_rom_read_played(romPath, &size, &e->patch, e->patchName, &e->fileCrc);
		{
			/* (the names of the ones left out, as src/emu.c reports them) */
			int r;
			size_t n2 = 0;
			char names[256];
			uint8_t* again = e->patch == PATCH_PARTIAL ? patched_rom(romPath, &n2, &r, names, NULL, e->patchSkipped) : NULL;
			free(again);
		}
		if (!rom) {
			fprintf(stderr, "could not load ROM %s\n", romPath);
			goto fail;
		}
		e->romCrc = doCrc32(rom, size);
		if (e->patch != PATCH_APPLIED && e->patch != PATCH_PARTIAL) e->fileCrc = e->romCrc;
		memcpy(e->gameCode, size >= 0xB0 ? (const char*) rom + 0xAC : "\0\0\0\0", 4);
		e->gameCode[4] = 0;

		/* The first core on this ROM loads it; the others read its buffer. */
		char key[1100];
		shared_key(romPath, e->patchName, key, sizeof(key));
		pthread_mutex_lock(&sharedLock);
		for (int i = 0; i < SHARED_MAX; ++i) {
			if (sharedRoms[i].users && !strcmp(sharedRoms[i].key, key) && sharedRoms[i].crc == e->romCrc) s = &sharedRoms[i];
			else if (!sharedRoms[i].users && !spare) spare = &sharedRoms[i];
		}
		if (s) {
			/* (what gbaLoadRom works out from the cartridge besides copying it) */
			ok = gbaLoadRom(gba, rom, size);
			if (ok) {
				free(gba->rom);
				gba->rom = s->rom;
				gba->romSize = s->romSize;
				gba->romMask = s->romMask;
				++s->users;
				c->shared = s;
			}
		} else {
#ifdef __3DS__
			/* the buffer the file was read into becomes the cartridge */
			size_t whole = gbaRomBufferSize(size);
			uint8_t* grown = whole > size ? realloc(rom, whole) : rom;
			if (grown) {
				rom = grown;
				if (whole > size) memset(rom + size, 0, whole - size);
				gbaOfferRom(rom);
			}
#endif
			ok = gbaLoadRom(gba, rom, size);
#ifdef __3DS__
			if (gbaOfferedRom()) gbaOfferRom(NULL);
			else if (ok) rom = NULL; /* taken */
#endif
			if (ok && spare && gba->rom) {
				snprintf(spare->key, sizeof(spare->key), "%s", key);
				spare->rom = gba->rom;
				spare->romSize = gba->romSize;
				spare->romMask = gba->romMask;
				spare->crc = e->romCrc;
				spare->users = 1;
				snprintf(spare->path, sizeof(spare->path), "%s", romPath);
				spare->patch = e->patch;
				memcpy(spare->patchName, e->patchName, sizeof(spare->patchName));
				spare->fileCrc = e->fileCrc;
				c->shared = spare;
			}
		}
		pthread_mutex_unlock(&sharedLock);
		free(rom);
		if (!ok) {
			fprintf(stderr, "could not load ROM %s\n", romPath);
			goto fail;
		}
	}

	gba->savedata.backed = true;
	if (savePath) {
		c->savePath = strdup(savePath);
		if (c->savePath) save_read(c);
	}
	gbaReset(gba, true);
	gba->savedata.dirty = false;
#ifdef __3DS__
	if (audio) {
		if (renderThreadStart(gba, false, false)) renderSetHooks(gba, &c->hooks);
		audioThreadStart(gba);
	}
#else
	/* (GBADS_RENDER_THREAD=1, tests: drawn on a thread as the console's game is) */
	if (getenv("GBADS_RENDER_THREAD") && renderThreadStart(gba, false, false)) renderSetHooks(gba, &c->hooks);
#endif

	e->fb = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	e->audio = audio;
	if (audio) {
		c->ringCap = 8192;
		c->ring = calloc(c->ringCap, 2 * sizeof(int16_t));
	}
	cheatsInit(&c->cheats);
	if (!e->fb || (audio && !c->ring)) goto fail;
#ifdef __3DS__
	host_mem_note(audio ? "the game opened" : "a copy opened");
#endif
	return true;
fail:
	close_core(c);
	e->core = NULL;
	free(e->fb);
	e->fb = NULL;
	return false;
}

static Emu* rewindOwner;
void emu_close(Emu* e) {
	if (e->core) {
		if (rewindOwner == e) emu_rewind_deinit();
		close_core(AC(e));
		e->core = NULL;
	}
	free(e->fb);
	e->fb = NULL;
}

bool emu_cancelled(const Emu* e) {
	return e->cancel && *e->cancel;
}

/* armGBA's picture is the GBA's own 15-bit colours; GBA DS wants mGBA's
 * 32-bit ones (bytes R, G, B, X), widened the way mGBA widens them. */
static void take_picture(Emu* e) {
	const uint16_t* src = (const uint16_t*) AC(e)->gba->renderer.frame;
	uint32_t* dst = e->fb;
	for (int i = 0; i < GBA_W * GBA_H; ++i) {
		uint32_t v = src[i];
		uint32_t c = ((v << 3) & 0xF8) | ((v << 6) & 0xF800) | ((v << 9) & 0xF80000);
		dst[i] = c | ((c >> 5) & 0x070707) | 0xFF000000u;
	}
}

static void run_frame(Emu* e, uint32_t keys, bool draw) {
	if (emu_cancelled(e)) return;
	ArmCore* c = AC(e);
	struct GBA* gba = c->gba;
	++e->frames;
	gbaSetKeys(gba, (uint16_t) keys);
	videoDrawSkipped = !draw;
	uint32_t const frame = gba->video.frameCounter;
	do {
		gbaRunPart(gba);
	} while (gba->video.frameCounter == frame);
	videoDrawSkipped = false;
	audioThreadKick(gba);
	if (c->cheatsOn) cheatsApply(&c->cheats, gba);
	if (draw) take_picture(e);
	if (draw) split_frame(c);
	audioThreadTake(gba);
	if (c->ring) {
		const int16_t* in = (const int16_t*) gba->audio.out;
		for (uint32_t i = 0; i < gba->audio.outCount; ++i) {
			if (c->ringCount == c->ringCap) { /* nobody's reading: the oldest goes */
				c->ringHead = (c->ringHead + 1) % c->ringCap;
				--c->ringCount;
			}
			size_t at = (c->ringHead + c->ringCount) % c->ringCap;
			c->ring[at * 2] = in[i * 2];
			c->ring[at * 2 + 1] = in[i * 2 + 1];
			++c->ringCount;
		}
	}
	gba->audio.outCount = 0;
	save_tick(c);
}

void emu_frame(Emu* e, uint32_t keys) {
	run_frame(e, keys, true);
}

void emu_frame_blind(Emu* e, uint32_t keys) {
	run_frame(e, keys, false);
}

void emu_reset(Emu* e) {
	gbaReset(AC(e)->gba, true);
}

size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames) {
	ArmCore* c = AC(e);
	if (!c->ring) return 0;
	size_t n = c->ringCount < maxFrames ? c->ringCount : maxFrames;
	for (size_t i = 0; i < n; ++i) {
		size_t at = (c->ringHead + i) % c->ringCap;
		out[i * 2] = c->ring[at * 2];
		out[i * 2 + 1] = c->ring[at * 2 + 1];
	}
	c->ringHead = (c->ringHead + n) % c->ringCap;
	c->ringCount -= n;
	return n;
}

unsigned emu_audio_rate(Emu* e) {
	(void) e;
	return 32768;
}

/* ---------------------------------------------- states */

size_t emu_state_size(Emu* e) {
	(void) e;
	return gbaStateSize();
}

bool emu_state_save(Emu* e, void* buf) {
	gbaSaveState(AC(e)->gba, buf);
	return true;
}

/* The core's layer switches aren't the machine's: a state leaves them. */
static void layers_apply(ArmCore* c);

bool emu_state_load(Emu* e, const void* buf) {
	ArmCore* c = AC(e);
	/* (the borrowed cartridge is this core's own business, not the state's) */
	bool ok = gbaLoadState(c->gba, buf, gbaStateSize());
	layers_apply(c);
	return ok;
}

/* A state file: a header, armGBA's state (which holds the battery save as it
 * was), then GBA DS's own data (emu_state_file_put_extra). Not mGBA's
 * format: the other builds' states don't load here, nor these there. */
#define STATE_FILE_MAGIC 0x33534447u /* "GDS3" */
typedef struct {
	uint32_t magic;
	uint32_t romCrc;
	uint32_t stateSize;
	uint32_t extraSize;
} StateFileHeader;

bool emu_state_save_file(Emu* e, const char* path) {
	size_t n = gbaStateSize();
	void* buf = malloc(n);
	if (!buf) return false;
	gbaSaveState(AC(e)->gba, buf);
	StateFileHeader h = { STATE_FILE_MAGIC, e->romCrc, (uint32_t) n, 0 };
	FILE* f = fopen(path, "wb");
	bool ok = f && fwrite(&h, sizeof(h), 1, f) == 1 && fwrite(buf, 1, n, f) == n;
	if (f) ok = fclose(f) == 0 && ok;
	free(buf);
	return ok;
}

bool emu_state_load_file(Emu* e, const char* path) {
	ArmCore* c = AC(e);
	StateFileHeader h;
	FILE* f = fopen(path, "rb");
	if (!f) {
		fprintf(stderr, "state %s: can't be opened\n", path);
		return false;
	}
	void* buf = NULL;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == STATE_FILE_MAGIC && h.stateSize == gbaStateSize() &&
	          (buf = malloc(h.stateSize)) && fread(buf, 1, h.stateSize, f) == h.stateSize;
	fclose(f);
	if (!ok)
		fprintf(stderr, "state %s: not loaded (%s)\n", path,
		        h.magic != STATE_FILE_MAGIC ? "not a GBA DS 3DS state" : h.stateSize != gbaStateSize() ? "made by another version" : !buf ? "no memory" : "cut short");
	if (ok) {
		/* The battery save stays as it is now: a state never puts an old
		 * save back (mGBA's load left it alone too). */
		size_t keep = sizeof(c->gba->savedata.storage);
		uint8_t* save = c->savePath ? malloc(keep) : NULL;
		if (save) memcpy(save, c->gba->savedata.storage, keep);
		ok = gbaLoadState(c->gba, buf, h.stateSize);
		if (save) {
			memcpy(c->gba->savedata.storage, save, keep);
			free(save);
		}
		layers_apply(c);
		if (!ok) fprintf(stderr, "state %s: the core refused it\n", path);
	}
	free(buf);
	return ok;
}

bool emu_state_file_rom_crc(const char* path, uint32_t* crc) {
	StateFileHeader h;
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == STATE_FILE_MAGIC;
	fclose(f);
	if (ok) *crc = h.romCrc;
	return ok;
}

bool emu_state_file_put_extra(Emu* e, const char* path, const void* data, size_t n) {
	(void) e;
	StateFileHeader h;
	FILE* f = fopen(path, "r+b");
	if (!f) return false;
	bool ok = fread(&h, sizeof(h), 1, f) == 1 && h.magic == STATE_FILE_MAGIC;
	if (ok) {
		h.extraSize = (uint32_t) n;
		ok = fseek(f, 0, SEEK_SET) == 0 && fwrite(&h, sizeof(h), 1, f) == 1 &&
		     fseek(f, (long) (sizeof(h) + h.stateSize), SEEK_SET) == 0 && fwrite(data, 1, n, f) == n;
	}
	ok = fclose(f) == 0 && ok;
	return ok;
}

void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n) {
	(void) e;
	*n = 0;
	StateFileHeader h;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	void* out = NULL;
	if (fread(&h, sizeof(h), 1, f) == 1 && h.magic == STATE_FILE_MAGIC && h.extraSize && h.extraSize < 16 * 1024 * 1024 &&
	    fseek(f, (long) (sizeof(h) + h.stateSize), SEEK_SET) == 0 && (out = malloc(h.extraSize))) {
		if (fread(out, 1, h.extraSize, f) == h.extraSize) *n = h.extraSize;
		else {
			free(out);
			out = NULL;
		}
	}
	fclose(f);
	return out;
}

/* ---------------------------------------------- rewind
 * Whole states in a ring (armGBA has no rewind of its own). A state is
 * 0.8 MB, so the ring is kept short; on the console there's none
 * (EMU_REWIND_MAX 0: the memory is the game's and the recompiler's). */
#ifndef EMU_REWIND_MAX
#ifdef __3DS__
#define EMU_REWIND_MAX 0
#else
#define EMU_REWIND_MAX 64
#endif
#endif
static struct {
	uint8_t** states;
	size_t cap, head, count;
} rw;

void emu_rewind_deinit(void) {
	if (!rewindOwner) return;
	for (size_t i = 0; i < rw.cap; ++i) free(rw.states[i]);
	free(rw.states);
	memset(&rw, 0, sizeof(rw));
	rewindOwner = NULL;
}

void emu_rewind_init(Emu* e, size_t entries) {
	emu_rewind_deinit();
	if (entries > EMU_REWIND_MAX) entries = EMU_REWIND_MAX;
	if (!entries) return;
	rw.states = calloc(entries, sizeof(*rw.states));
	if (!rw.states) return;
	rw.cap = entries;
	rewindOwner = e;
}

void emu_rewind_append(Emu* e) {
	if (rewindOwner != e) return;
	size_t at = (rw.head + rw.count) % rw.cap;
	if (rw.count == rw.cap) {
		at = rw.head;
		rw.head = (rw.head + 1) % rw.cap;
		--rw.count;
	}
	if (!rw.states[at] && !(rw.states[at] = malloc(gbaStateSize()))) return;
	gbaSaveState(AC(e)->gba, rw.states[at]);
	++rw.count;
}

bool emu_rewind_step(Emu* e) {
	if (rewindOwner != e || !rw.count) return false;
	size_t at = (rw.head + rw.count - 1) % rw.cap;
	--rw.count;
	return emu_state_load(e, rw.states[at]);
}

size_t emu_rewind_count(void) {
	return rewindOwner ? rw.count : 0;
}

/* ---------------------------------------------- the game's memory */

uint8_t* emu_memory(Emu* e, int region, size_t* size) {
	struct GBA* gba = e->core ? AC(e)->gba : NULL;
	*size = 0;
	if (!gba) return NULL;
	switch (region) {
	case 2:
		*size = SIZE_EWRAM;
		return gba->ewram;
	case 3:
		*size = SIZE_IWRAM;
		return gba->iwram;
	case 0xE:
		*size = save_size(gba);
		return *size ? gba->savedata.storage : NULL;
	}
	return NULL;
}

/* Reads see memory as it is, with no side effects (the save memory's chips
 * aren't driven: its bytes are read where they lie). */
/* The write-only I/O registers (the backgrounds' offsets and affine
 * parameters, the windows, mosaic, the blend's fade, the DMA addresses),
 * which a read on the bus gives the open bus for. */
static bool io_write_only(uint32_t off) {
	return (off >= 0x010 && off <= 0x046) || off == 0x04C || off == 0x054 || (off >= 0x0A0 && off <= 0x0A7) ||
	       (off >= 0x0B0 && off <= 0x0B7) || (off >= 0x0BC && off <= 0x0C3) || (off >= 0x0C8 && off <= 0x0CF) ||
	       (off >= 0x0D4 && off <= 0x0DB);
}

uint8_t emu_read8(Emu* e, uint32_t a) {
	struct GBA* gba = AC(e)->gba;
	if (a >> 24 == 0xE) return gba->savedata.storage[a & (SIZE_FLASH1M - 1)];
	/* Profiles read registers as mGBA's debug view gives them (src/emu.c's
	 * rawRead): a write-only one as last written, not the open bus. */
	if (a >> 24 == 0x4 && io_write_only(a & 0xFFFFFF)) return (uint8_t) (gba->io[(a & 0x3FE) >> 1] >> ((a & 1) * 8));
	return (uint8_t) (memoryView32(gba, a & ~3u) >> ((a & 3) * 8));
}
uint16_t emu_read16(Emu* e, uint32_t a) {
	return (uint16_t) (emu_read8(e, a) | emu_read8(e, a + 1) << 8);
}
uint32_t emu_read32(Emu* e, uint32_t a) {
	return (uint32_t) emu_read16(e, a) | (uint32_t) emu_read16(e, a + 2) << 16;
}

/* A core that writes to the cartridge (a profile's ROM patch, for that copy
 * only) stops reading the buffer it shares and gets one of its own. */
static bool rom_own(ArmCore* c) {
	if (!c->shared) return true;
	struct GBA* gba = c->gba;
	pthread_mutex_lock(&sharedLock);
	bool ok = true;
	if (c->shared->users > 1) {
#ifdef __3DS__
		size_t n = gbaRomBufferSize(gba->romSize);
#else
		size_t n = SIZE_ROM;
#endif
		uint8_t* own = calloc(1, n);
		ok = own != NULL;
		if (ok) {
			memcpy(own, gba->rom, gba->romSize);
			gba->rom = own;
			--c->shared->users;
			c->shared = NULL;
		}
	} else {
#ifndef __3DS__
		memset(c->shared, 0, sizeof(*c->shared)); /* its only user: the buffer is this core's again */
		c->shared = NULL;
#endif
		/* (the console: still lent, patch and all - a profile's patches are
		 * made by every core on the game, and a second cartridge isn't to
		 * be had) */
	}
	pthread_mutex_unlock(&sharedLock);
	return ok;
}

/* The cartridge is written where it lies; the rest through armGBA's stores,
 * so the renderer and the recompiler hear of it. */
static bool rom_write(ArmCore* c, uint32_t a, uint32_t v, int n) {
	struct GBA* gba = c->gba;
	if (a >> 24 < 8 || a >> 24 > 0xD) return false;
	uint32_t off = a & (SIZE_ROM - 1);
	if (gba->rom && off + (uint32_t) n <= gba->romSize) {
		/* (a patch another core on this cartridge already made: nothing) */
		bool same = true;
		for (int i = 0; i < n; ++i) same = same && gba->rom[off + (uint32_t) i] == (uint8_t) (v >> (8 * i));
		if (same) return true;
		if (!rom_own(c)) return true;
		for (int i = 0; i < n; ++i) gba->rom[off + (uint32_t) i] = (uint8_t) (v >> (8 * i));
		jitInvalidate(gba, a, (uint32_t) n);
	}
	return true;
}
void emu_write8(Emu* e, uint32_t a, uint8_t v) {
	int cycles = 0;
	if (!rom_write(AC(e), a, v, 1)) memoryStore8(AC(e)->gba, a, v, &cycles);
}
void emu_write16(Emu* e, uint32_t a, uint16_t v) {
	int cycles = 0;
	if (!rom_write(AC(e), a, v, 2)) memoryStore16(AC(e)->gba, a, v, &cycles);
}
void emu_write32(Emu* e, uint32_t a, uint32_t v) {
	int cycles = 0;
	if (!rom_write(AC(e), a, v, 4)) memoryStore32(AC(e)->gba, a, v, &cycles);
}

uint16_t emu_io16(Emu* e, uint32_t offset) {
	return AC(e)->gba->io[(offset & 0x3FE) >> 1];
}

const uint16_t* emu_vram(Emu* e) {
	return (const uint16_t*) AC(e)->gba->vram;
}

/* As mGBA's (src/emu.c): the bytes as they are. Through the bus a halfword at
 * a time: a byte stored to background VRAM is written to both halves of its
 * halfword, as on the GBA, which garbled the tiles Sonic Pinball Party's
 * copies carry over (tall.live.carry). An odd byte at either end keeps its
 * neighbour. */
void emu_vram_write(Emu* e, uint32_t off, const void* src, size_t n) {
	struct GBA* gba = AC(e)->gba;
	const uint8_t* p = src;
	int cycles = 0;
	if (off >= SIZE_VRAM) return;
	if (n > SIZE_VRAM - off) n = SIZE_VRAM - off;
	for (size_t i = 0; i < n;) {
		uint32_t const a = off + (uint32_t) i;
		uint16_t half = (uint16_t) (gba->vram[a & ~1u] | gba->vram[(a & ~1u) + 1] << 8);
		if (a & 1) {
			half = (uint16_t) ((half & 0x00FF) | p[i] << 8);
			i += 1;
		} else if (i + 1 < n) {
			half = (uint16_t) (p[i] | p[i + 1] << 8);
			i += 2;
		} else {
			half = (uint16_t) ((half & 0xFF00) | p[i]);
			i += 1;
		}
		memoryStore16(gba, 0x06000000 + (a & ~1u), half, &cycles);
	}
}

void emu_oam_read(Emu* e, uint16_t* out) {
	memcpy(out, AC(e)->gba->oam, SIZE_OAM);
}

void emu_oam_write(Emu* e, const uint16_t* src) {
	int cycles = 0;
	for (uint32_t i = 0; i < SIZE_OAM / 2; ++i) memoryStore16(AC(e)->gba, 0x07000000 + i * 2, src[i], &cycles);
}

/* ---------------------------------------------- what's drawn */

/* armGBA's renderer asks this of each sprite as it lists them (its
 * RenderHooks, third_party/armgba src/video/render.h). */
static bool sprite_hidden(void* context, uint16_t a, uint16_t b, uint16_t attrC) {
	const ArmCore* c = context;
	for (int i = 0; i < c->nExact; ++i) {
		if (c->exact[i][0] == a && c->exact[i][1] == b && c->exact[i][2] == attrC) return !c->exactKeep;
	}
	if (!c->nRules && !c->keepOnly) return false;
	int x = (int) ((uint32_t) (b & 0x1FF) << 23) >> 23;
	int y = a & 0xFF;
	if (y >= 160) y -= 256;
	int tile = attrC & 0x3FF;
	int prio = (attrC >> 10) & 3;
	int pal = attrC >> 12;
	bool transformed = (a >> 8) & 1;
	int shape = (a >> 14) & 3, size = (b >> 14) & 3;
	for (int i = 0; i < c->nRules; ++i) {
		const SpriteRule* r = &c->rules[i];
		if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1 && tile >= r->tileMin && tile <= r->tileMax &&
		    (r->prio < 0 || prio == r->prio) && (!r->palMask || (r->palMask >> pal & 1)) &&
		    (!r->tagged || (!transformed && (b & r->tagged))) && (!r->shapes || (r->shapes >> (shape * 4 + size) & 1))) {
			return !c->keepOnly;
		}
	}
	return c->keepOnly;
}

static void hooks_changed(ArmCore* c) {
	c->hooks.hideSprite = c->nRules || c->keepOnly || c->nExact ? sprite_hidden : NULL;
	c->hooks.context = c;
	++c->hooks.generation;
}

static void layers_apply(ArmCore* c) {
	uint8_t hide = ~c->layers & 0x1F;
	if (hide != c->hooks.hideLayers) {
		c->hooks.hideLayers = hide;
		hooks_changed(c);
	}
}

void emu_layers(Emu* e, unsigned mask) {
	AC(e)->layers = mask;
	layers_apply(AC(e));
}

void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n) {
	ArmCore* c = AC(e);
	c->nRules = n > 12 ? 12 : n;
	if (c->nRules) memcpy(c->rules, rules, sizeof(*rules) * c->nRules);
	hooks_changed(c);
}

/* The opposite: only the sprites that match one of the rules are drawn (Lady
 * Sia's pause screen, src/emu.c). */
void emu_keep_sprites(Emu* e, const SpriteRule* rules, int n) {
	ArmCore* c = AC(e);
	c->nRules = n > 12 ? 12 : n;
	if (c->nRules) memcpy(c->rules, rules, sizeof(*rules) * c->nRules);
	c->keepOnly = n > 0;
	hooks_changed(c);
}

/* src/emu.c hides a matching sprite only where it is part of a group (its run
 * in the sprite table): armGBA's hook is asked about one sprite at a time, on
 * whichever thread draws, without its place in the table, so here every
 * matching sprite is hidden, as GBA DS did before 1.5.4 (Lady Sia's
 * scenery that shares a HUD piece's place and shape: 3ds/README.md). */
void emu_hide_grouped(Emu* e, bool on) {
	(void) e;
	(void) on;
}

void emu_hide_exact(Emu* e, const uint16_t (*attrs)[3], int n) {
	ArmCore* c = AC(e);
	c->nExact = n > 128 ? 128 : n;
	c->exactKeep = false;
	if (c->nExact) memcpy(c->exact, attrs, sizeof(*attrs) * c->nExact);
	hooks_changed(c);
}

/* ---------------------------------------------- the HUD split
 *
 * As src/emu.c's, on armGBA's second picture (RenderHooks.second: a renderer
 * of its own beside the game's, fed the same registers and video memory):
 * it draws only the HUD's layers and sprites, on a backdrop of the 'nothing
 * here' colour, while the game's picture is drawn without them. The layers
 * are decided line by line (split_line, called by whichever thread draws)
 * from the video registers as that line has them. */
#define HUD_CLEAR_555 0x0401

static struct {
	ArmCore* core; /* the core it's on (NULL: not installed) */
	bool active;
	unsigned bgMask;
	unsigned gameHide; /* BGs the game's picture is drawn without now */
	unsigned hudMask;  /* the second picture's BGs, and whether it has sprites */
	bool hudObj;
	SpriteRule objs[12];
	int nObjs;
	uint16_t keep[128][3]; /* sprites the HUD also draws by their exact attributes */
	int nKeep;
	uint32_t* hudBuf;
	uint32_t* grabBuf;
	unsigned grabMask;
	uint16_t grabCnt;
	bool grabbing, grabbed;
	int prioBelow;
	unsigned keepBgs;
	unsigned baseMask, memLayers;
	uint32_t memAddr[2], memMask[2], memVal[2];
	int memSize[2], memN;
	bool memAny;
	int memGamePal;
	unsigned memLineOff;
	unsigned lineHud, lineGame;
	int bandY0, bandY1;
	uint16_t frame[GBA_W * GBA_H];
} split;
static uint32_t gameRegAddr;
static uint16_t gameRegBits;
static bool bandOn, bandCopy, bandWatch;
static int lastBandY0, lastBandY1 = -1;
static uint16_t bandNormal[4];
static int copyAbove;
static int splitRows;
static bool splitCopying;

static uint32_t colour_8888(uint16_t v) {
	uint32_t c = ((v << 3) & 0xF8) | ((v << 6) & 0xF800) | ((v << 9) & 0xF80000);
	return c | ((c >> 5) & 0x070707) | 0xFF000000u;
}

/* the second picture's sprites: the HUD's only */
static bool second_hidden(void* context, uint16_t a, uint16_t b, uint16_t attrC) {
	(void) context;
	for (int i = 0; i < split.nKeep; ++i) {
		if (split.keep[i][0] == a && split.keep[i][1] == b && split.keep[i][2] == attrC) return false;
	}
	int x = (int) ((uint32_t) (b & 0x1FF) << 23) >> 23;
	int y = a & 0xFF;
	if (y >= 160) y -= 256;
	int tile = attrC & 0x3FF;
	int prio = (attrC >> 10) & 3;
	int pal = attrC >> 12;
	bool transformed = (a >> 8) & 1;
	int shape = (a >> 14) & 3, size = (b >> 14) & 3;
	for (int i = 0; i < split.nObjs; ++i) {
		const SpriteRule* r = &split.objs[i];
		if (x >= r->x0 && x <= r->x1 && y >= r->y0 && y <= r->y1 && tile >= r->tileMin && tile <= r->tileMax &&
		    (r->prio < 0 || prio == r->prio) && (!r->palMask || (r->palMask >> pal & 1)) &&
		    (!r->tagged || (!transformed && (b & r->tagged))) && (!r->shapes || (r->shapes >> (shape * 4 + size) & 1))) {
			return false;
		}
	}
	return true;
}

static void split_layers(unsigned bgMask, bool obj) {
	split.hudMask = bgMask & 0xF;
	split.hudObj = obj;
}
static void game_hide(unsigned bits, bool off) {
	split.gameHide = off ? split.gameHide | bits : split.gameHide & ~bits;
}

static unsigned bgs_with_cnt(const uint16_t* io, uint16_t cnt) {
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) m |= (unsigned) (io[(0x08 >> 1) + i] == cnt) << i;
	return m;
}
static unsigned bgs_below(const uint16_t* io, int p) {
	unsigned m = 0;
	for (int i = 0; i < 4; ++i) m |= (unsigned) ((io[(0x08 >> 1) + i] & 3) < p) << i;
	return m;
}

/* a byte, halfword or word of EWRAM, IWRAM, the I/O registers or VRAM, as
 * the game has it now (0 elsewhere) */
static uint32_t split_read(uint32_t a, int size) {
	const struct GBA* g = split.core->gba;
	const uint8_t* p;
	switch (a >> 24) {
	case 2: p = g->ewram + (a & 0x3FFFF); break;
	case 3: p = g->iwram + (a & 0x7FFF); break;
	case 4: p = (const uint8_t*) g->io + (a & 0x3FF); break;
	case 6: p = g->vram + ((a & 0x1FFFF) < 0x18000 ? a & 0x1FFFF : (a & 0x1FFFF) - 0x8000); break;
	default: return 0;
	}
	uint32_t v = 0;
	memcpy(&v, p, size == 1 || size == 2 ? (size_t) size : 4);
	return v;
}

/* Whether text BG `bg`'s map has a tile in palette `pal` on screen line y */
static bool bg_line_has_pal(const uint16_t* io, const uint8_t* vramBytes, int bg, int y, int pal) {
	int mode = io[0] & 7;
	if (mode > 1 || (mode == 1 && bg > 1)) return false;
	uint16_t cnt = io[(0x08 >> 1) + bg];
	int hofs = io[(0x10 >> 1) + bg * 2] & 0x1FF, vofs = io[(0x12 >> 1) + bg * 2] & 0x1FF;
	int size = cnt >> 14, sb = (cnt >> 8) & 0x1F;
	int wide = size & 1, tall = size >> 1;
	int ty = ((y + vofs) >> 3) & (tall ? 63 : 31);
	for (int c = 0; c < GBA_W / 8 + 1; ++c) {
		int tx = ((hofs >> 3) + c) & (wide ? 63 : 31);
		int block = sb + (tx >> 5) + ((ty >> 5) << wide);
		uint32_t at = ((uint32_t) (block * 0x400 + (ty & 31) * 32 + (tx & 31)) * 2) & 0x1FFFE;
		if (at >= 0x18000) continue;
		uint16_t e2;
		memcpy(&e2, vramBytes + at, 2);
		if ((e2 & 0x3FF) && (e2 >> 12) == pal) return true;
	}
	return false;
}

/* Before each line is drawn: which layers the two pictures have on it. */
static void split_line(void* context, struct RenderLine* l) {
	ArmCore* c = context;
	if (c != split.core || !split.active) return;
	const uint16_t* io = l->io;
	int y = l->y;
	bool objs = split.nObjs > 0 || split.nKeep > 0;
	if (copyAbove) game_hide(split.bgMask, y >= copyAbove);
	if (splitRows && !splitCopying) {
		/* the HUD's layers leave the game only on its rows; below, the
		 * game draws them as it does, behind or in front of the rest */
		game_hide(split.bgMask, y < splitRows);
	}
	if (split.prioBelow) {
		/* the game moves its map and menus between layers (between frames
		 * and, as a panel slides, between lines): picked for each line */
		unsigned m = bgs_below(io, split.prioBelow) & ~split.keepBgs;
		if (m != split.bgMask) {
			split.bgMask = m;
			split.gameHide = m;
			if (!split.grabbing) split_layers(m, objs);
		}
	}
	if (y == 0 && split.memN) {
		/* emu_hud_mem_layers: decided as the frame starts drawing */
		bool match = !split.memAny;
		for (int i = 0; i < split.memN && match != split.memAny; ++i) match = (split_read(split.memAddr[i], split.memSize[i]) & split.memMask[i]) == split.memVal[i];
		unsigned m = split.baseMask | (match ? split.memLayers : 0);
		if (m != split.bgMask) {
			split.bgMask = split.lineHud = split.lineGame = m;
			split.gameHide = m;
			if (!split.grabbing) split_layers(m, objs);
		}
	}
	if (split.memGamePal) {
		/* on a line where one of those layers has a tile in that palette,
		 * the layer stays on the game there and is left out of the HUD */
		unsigned off = 0;
		for (int i = 0; i < 4; ++i) {
			if (((split.memLayers & split.bgMask) >> i) & 1 && bg_line_has_pal(io, l->vram, i, y, split.memGamePal - 1)) off |= 1u << i;
		}
		if (y == 0 || off != split.memLineOff) {
			split.gameHide = (split.gameHide & ~split.memLayers) | ((split.bgMask & ~off) & split.memLayers);
			if (!split.grabbing) split_layers(split.bgMask & ~off, objs);
			split.memLineOff = off;
		}
	}
	if (y == 0 && split.grabBuf) {
		if (split.grabCnt) split.grabMask = bgs_with_cnt(io, split.grabCnt);
		/* this frame: the grab layers into grabBuf, no sprites */
		split.grabbing = true;
		split_layers(split.grabMask, false);
	}
	if (bandOn && !split.prioBelow) {
		/* the band: lines with sprites off (the rest of the screen has them
		 * on) where layers are set up differently from elsewhere: those
		 * layers go into the HUD, and out of the game unless they're
		 * copied. Not when last frame's band was most of the screen. */
		unsigned band = 0;
		static bool inBand;
		static int tail; /* lines of the box with the sprites back on */
		if (y == 0) split.bandY0 = GBA_H, split.bandY1 = -1, inBand = false;
		unsigned differs = 0;
		for (int i = 0; i < 4; ++i) {
			if (((io[0] >> (8 + i)) & 1) && bandNormal[i] && io[(0x08 >> 1) + i] != bandNormal[i]) differs |= 1u << i;
		}
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
			split.gameHide = game;
		}
		if (hud != split.lineHud) {
			split.lineHud = hud;
			if (!split.grabbing) split_layers(hud, objs);
		}
	}
	l->hideLayers |= split.gameHide & 0xF;
	l->secondHideLayers = (uint8_t) ((~split.hudMask & 0xF) | (split.hudObj ? 0 : 0x10));
}

/* After each frame of the core it's on: the second picture into the app's
 * buffer (or, once, the grab's). */
static void split_frame(ArmCore* c) {
	if (c != split.core || !split.active) return;
	if (bandOn) lastBandY0 = split.bandY0, lastBandY1 = split.bandY1;
	uint32_t* dst = split.grabbing ? split.grabBuf : split.hudBuf;
	for (int i = 0; dst && i < GBA_W * GBA_H; ++i) dst[i] = colour_8888(split.frame[i]);
	if (split.grabbing) {
		split.grabbing = false;
		split.grabBuf = NULL;
		split.grabbed = true;
		split_layers(split.bgMask, split.nObjs > 0 || split.nKeep > 0);
	}
}

static void split_release(ArmCore* c) {
	if (split.core != c) return;
	c->hooks.second = false;
	c->hooks.line = NULL;
	memset(&split, 0, sizeof(split));
}

/* a video register written again as it stands, so both pictures take it
 * with or without the masked bits */
static void rewrite_register(struct GBA* gba, uint32_t addr) {
	uint16_t v = gba->io[addr >> 1];
	gba->io[addr >> 1] = renderWriteRegister(gba, addr, v);
	if (gba->renderThread) renderThreadRegister(gba, addr, v);
}

bool emu_hud_split(Emu* e, bool on, unsigned bgMask, const SpriteRule* objs, int nObjs, uint32_t* hud) {
	ArmCore* c = AC(e);
	if (!split.core) {
		if (!on) return true;
		memset(&split, 0, sizeof(split));
		split.core = c;
		split.hudBuf = hud;
		for (int i = 0; i < GBA_W * GBA_H; ++i) split.frame[i] = HUD_CLEAR_555;
		c->hooks.secondFrame = split.frame;
		c->hooks.secondHideSprite = second_hidden;
		c->hooks.secondBackdropSet = true;
		c->hooks.secondBackdrop = HUD_CLEAR_555;
		c->hooks.line = split_line;
	}
	if (split.core != c) return false;
	split.hudBuf = hud;
	split.bgMask = split.baseMask = bgMask;
	split.lineHud = split.lineGame = bgMask;
	if (!on) lastBandY0 = 0, lastBandY1 = -1;
	split.nObjs = nObjs < 12 ? nObjs : 12;
	if (split.nObjs) memcpy(split.objs, objs, sizeof(*objs) * split.nObjs);
	/* the HUD: those layers and sprites only */
	if (!split.grabbing) split_layers(bgMask, split.nObjs > 0 || split.nKeep > 0);
	/* the game: without them (the rules can change while on: another scene) */
	split.gameHide = on ? bgMask & 0xF : 0;
	split.active = on;
	c->hooks.second = on;
	c->hooks.maskedRegister = (uint16_t) gameRegAddr;
	c->hooks.maskedBits = on ? gameRegBits : 0;
	emu_hide_sprites(e, on ? split.objs : NULL, on ? split.nObjs : 0); /* (and every line drawn again) */
	if (gameRegBits) rewrite_register(c->gba, gameRegAddr);
	return true;
}

void emu_hud_copy(Emu* e, bool copy) {
	if (!split.core || split.core != AC(e) || !split.active) return;
	splitCopying = copy;
	split.gameHide = copy ? 0 : split.bgMask & 0xF;
	emu_hide_sprites(e, copy ? NULL : split.objs, copy ? 0 : split.nObjs);
}

void emu_hud_keep_exact(const uint16_t (*attrs)[3], int n) {
	if (!split.core) return;
	n = n > 128 ? 128 : n;
	if (n) memcpy(split.keep, attrs, sizeof(*attrs) * (size_t) n);
	split.nKeep = n;
	if (!split.grabbing) split_layers(split.bgMask, split.nObjs > 0 || split.nKeep > 0);
	hooks_changed(split.core);
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
	/* exactly what the second picture's backdrop comes out as */
	return split.core ? colour_8888(HUD_CLEAR_555) & 0xFFFFFF : 0xFF00FF;
}

void emu_hud_by_priority(int prioBelow) {
	split.prioBelow = prioBelow;
}

void emu_hud_keep_bgs(unsigned mask) {
	split.keepBgs = mask;
}

void emu_hud_mem_layers(const uint32_t addr[2], const uint32_t mask[2], const uint32_t value[2], const int size[2], int n, bool any,
                        unsigned layers, int gamePal) {
	split.memN = n < 0 ? 0 : n > 2 ? 2 : n;
	split.memGamePal = split.memN ? gamePal : 0;
	split.memLineOff = 0;
	split.memAny = any;
	for (int i = 0; i < split.memN; ++i) split.memAddr[i] = addr[i], split.memMask[i] = mask[i], split.memVal[i] = value[i], split.memSize[i] = size[i];
	split.memLayers = layers;
}

void emu_hud_grab(Emu* e, unsigned bgMask, uint32_t* buf) {
	(void) e;
	if (!split.core || !split.active) return;
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

/* ---------------------------------------------- cheats */

void emu_cheats_clear(Emu* e) {
	ArmCore* c = AC(e);
	cheatsClear(&c->cheats, c->gba);
	c->cheatsOn = false;
}

/* armGBA tells the formats apart by the lines' shape (eight and four hex
 * digits: Code Breaker; eight and eight: GameShark / Action Replay). */
int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n, int type) {
	(void) name;
	(void) type;
	ArmCore* c = AC(e);
	int taken = 0;
	for (int i = 0; i < n; ++i) {
		if (cheatsAdd(&c->cheats, codes[i])) ++taken;
	}
	if (taken) c->cheatsOn = true;
	return taken;
}

/* mGBA's gba.h uses GBA_H as its include guard, which emu.h defines as the
 * screen height: include it first, then let emu.h take the name over. */
#include <mgba/flags.h>
#include <mgba/internal/gba/gba.h>
#include <mgba/internal/gba/renderers/video-software.h>
#include <mgba/internal/gba/cheats.h>
#undef GBA_H
#include "emu.h"

#include <mgba/flags.h>

#include <mgba/core/core.h>
#include <mgba/core/cheats.h>
#include <mgba/core/serialize.h>
#include <mgba/core/rewind.h>
#include <mgba/core/config.h>
#include <mgba-util/audio-buffer.h>
#include <mgba-util/vfs.h>
#include <mgba/core/log.h>
#include "patch.h"
#include <stdarg.h>
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

#if defined(__ANDROID__) || defined(GBADS_ASYNC_SAVE)
/* ------------------------------------------------- battery saves, async
 * When a game writes its save, mGBA flushes it from the emulation thread a
 * few frames later: futimens + msync + fsync. On Android's shared storage
 * the fsync took ~200 ms, a visible freeze at every save point. The save
 * stays a mapped file as before (so what the game writes is in the file
 * right away); only the flush to the card moves to a worker thread. */
#include <unistd.h>
#include <sys/stat.h>
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
static uint8_t* patched_rom(const char* romPath, size_t* size, int* result, char* patchName, uint32_t* fileCrc,
                            char* skipped) {
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
	int k = snprintf(key, sizeof(key), "%s|%lld|%lld", romPath, stat(romPath, &st) ? -1LL : (long long) st.st_size,
	                 stat(romPath, &st) ? -1LL : (long long) st.st_mtime);
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
	mCoreInitConfig(e->core, NULL);
	struct mCoreOptions opts = { .useBios = false, .volume = 0x100, .audioBuffers = 1024 };
	mCoreConfigLoadDefaults(&e->core->config, &opts);
	mCoreLoadConfig(e->core);

	e->fb = calloc(GBA_W * GBA_H, sizeof(uint32_t));
	e->core->setVideoBuffer(e->core, (mColor*) e->fb, GBA_W);
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
			return false;
		}
	} else if (!mCoreLoadFile(e->core, romPath)) {
		fprintf(stderr, "could not load ROM %s\n", romPath);
		return false;
	}
	if (savePath) {
#if defined(__ANDROID__) || defined(GBADS_ASYNC_SAVE)
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
	e->core->reset(e->core);

	struct mGameInfo info;
	memset(&info, 0, sizeof(info));
	e->core->getGameInfo(e->core, &info);
	memcpy(e->gameCode, info.code, 4);
	e->gameCode[4] = 0;
	e->core->checksum(e->core, &e->romCrc, mCHECKSUM_CRC32); /* (worked out by mGBA as it loaded the ROM) */
	if (e->patch != PATCH_APPLIED && e->patch != PATCH_PARTIAL) e->fileCrc = e->romCrc;
	return true;
}

static void hide_release(Emu* e);

static struct mCoreRewindContext rewindCtx;
static Emu* rewindOwner;

void emu_close(Emu* e) {
	if (e->core) {
		if (rewindOwner == e) emu_rewind_deinit();
		split_release(e);
		hide_release(e);
		mCoreConfigDeinit(&e->core->config);
		e->core->deinit(e->core);
		e->core = NULL;
	}
	free(e->fb);
	e->fb = NULL;
}

bool emu_cancelled(const Emu* e) {
	return e->cancel && *e->cancel;
}

void emu_frame(Emu* e, uint32_t keys) {
	if (emu_cancelled(e)) return;
	++e->frames;
	e->core->setKeys(e->core, keys);
	e->core->runFrame(e->core);
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
	struct GBA* gba = e->core->board;
	gba->video.frameskipCounter = 1;
	emu_frame(e, keys);
	gba->video.frameskipCounter = 0;
}

void emu_reset(Emu* e) {
	e->core->reset(e->core);
	split_resync(e);
}

size_t emu_state_size(Emu* e) {
	return e->core->stateSize(e->core);
}

bool emu_state_save(Emu* e, void* buf) {
	return e->core->saveState(e->core, buf);
}

bool emu_state_load(Emu* e, const void* buf) {
	bool ok = e->core->loadState(e->core, buf);
	split_resync(e);
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
	return true;
}

size_t emu_rewind_count(void) {
	return rewindOwner ? rewindCtx.size : 0;
}

bool emu_state_save_file(Emu* e, const char* path) {
	struct VFile* vf = VFileOpen(path, O_CREAT | O_TRUNC | O_RDWR);
	if (!vf) {
		return false;
	}
	bool ok = mCoreSaveStateNamed(e->core, vf, SAVESTATE_SAVEDATA | SAVESTATE_RTC | SAVESTATE_METADATA);
	vf->close(vf);
	return ok;
}

bool emu_state_load_file(Emu* e, const char* path) {
	struct VFile* vf = VFileOpen(path, O_RDONLY);
	if (!vf) {
		return false;
	}
	bool ok = mCoreLoadStateNamed(e->core, vf, SAVESTATE_RTC);
	vf->close(vf);
	split_resync(e);
	return ok;
}

/* (in mGBA's subsystem range, which it doesn't use for the GBA) */
#define EXTDATA_GBADS ((enum mStateExtdataTag) (EXTDATA_SUBSYSTEM_START + 0x3D))

bool emu_state_file_put_extra(Emu* e, const char* path, const void* data, size_t n) {
	struct VFile* vf = VFileOpen(path, O_RDWR);
	if (!vf) return false;
	struct mStateExtdata ext;
	mStateExtdataInit(&ext);
	mCoreExtractExtdata(e->core, vf, &ext);
	void* copy = malloc(n ? n : 1);
	bool ok = copy != NULL;
	if (ok) {
		memcpy(copy, data, n);
		struct mStateExtdataItem item = { .size = (int32_t) n, .data = copy, .clean = free };
		mStateExtdataPut(&ext, EXTDATA_GBADS, &item);
		size_t sz = e->core->stateSize(e->core);
		vf->truncate(vf, sz);
		vf->seek(vf, sz, SEEK_SET);
		ok = mStateExtdataSerialize(&ext, vf);
	}
	mStateExtdataDeinit(&ext);
	vf->close(vf);
	return ok;
}

void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n) {
	*n = 0;
	struct VFile* vf = VFileOpen(path, O_RDONLY);
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
	static const char* const exts[] = { ".gba", ".zip", ".7z", NULL };
	return has_ext(name, exts);
}

bool emu_rom_header(const char* path, uint8_t* out, size_t n) {
	if (n > 0xC0) n = 0xC0;
	struct VDir* archive = emu_is_archive_name(path) ? VDirOpenArchive(path) : NULL;
	struct VFile* vf = archive ? VDirFindFirst(archive, GBAIsROM) : VFileOpen(path, O_RDONLY);
	bool ok = false;
	if (vf) {
		ok = vf->seek(vf, 0, SEEK_SET) == 0 && vf->read(vf, out, n) == (ssize_t) n;
		vf->close(vf);
	}
	if (archive) archive->close(archive);
	return ok;
}

uint8_t* emu_rom_read_all(const char* path, size_t* size) {
	struct VDir* archive = emu_is_archive_name(path) ? VDirOpenArchive(path) : NULL;
	struct VFile* vf = archive ? VDirFindFirst(archive, GBAIsROM) : VFileOpen(path, O_RDONLY);
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

/* Runs before the software renderer's own scanline code: when the sprite table
 * has changed, build the renderer's sprite list the way it would, minus the
 * hidden sprites, and mark it clean so the renderer uses ours. */
static void filtered_draw_scanline(struct GBAVideoRenderer* renderer, int y) {
	struct GBAVideoSoftwareRenderer* sw = (struct GBAVideoSoftwareRenderer*) renderer;
	HideSlot* s = slot_for(renderer);
	if (!s) return;
	if ((s->n || s->keepOnly || s->nExact) && sw->oamDirty) {
		int n = GBAVideoRendererCleanOAM(sw->d.oam->obj, sw->sprites, sw->objOffsetY);
		int kept = 0;
		for (int i = 0; i < n; ++i) {
			if (!hidden(s, &sw->sprites[i].obj)) sw->sprites[kept++] = sw->sprites[i];
		}
		sw->oamMax = kept;
		sw->oamDirty = false;
	}
	s->orig(renderer, y);
}

void emu_hide_exact(Emu* e, const uint16_t (*attrs)[3], int n) {
	struct GBA* gba = e->core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
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
	struct GBA* gba = e->core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
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

void emu_layers(Emu* e, unsigned mask) {
	for (int i = 0; i < 5; ++i) e->core->enableVideoLayer(e->core, i, (mask >> i) & 1);
}

static void hide_release(Emu* e) {
	struct GBA* gba = e->core->board;
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
	struct GBA* gba = e->core->board;
	if (!split.installed || gba->video.renderer != split.main) return; /* another core (logo, map capture) */
	struct GBAVideoRenderer* h = &split.hud.d;
	h->reset(h);
	for (uint32_t a = 0; a <= 0x56; a += 2) h->writeVideoRegister(h, a, hud_reg(a, gba->memory.io[a >> 1]));
	for (uint32_t a = 0; a < 0x400; a += 2) h->writePalette(h, a, a == 0 ? HUD_CLEAR_555 : gba->video.palette[a >> 1]);
	for (uint32_t i = 0; i < 512; ++i) h->writeOAM(h, i);
	split.hud.oamDirty = true;
}

static void split_release(Emu* e) {
	struct GBA* gba = e->core->board;
	if (!split.installed || gba->video.renderer != split.main) return; /* another core (logo, map capture) */
	pthread_mutex_lock(&hideLock);
	HideSlot* s = slot_for(&split.hud.d);
	if (s) memset(s, 0, sizeof(*s));
	pthread_mutex_unlock(&hideLock);
	split.hud.d.deinit(&split.hud.d);
	memset(&split, 0, sizeof(split));
}

bool emu_hud_split(Emu* e, bool on, unsigned bgMask, const SpriteRule* objs, int nObjs, uint32_t* hud) {
	struct GBA* gba = e->core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
	if (!r) return false;
	if (!split.installed) {
		if (!on) return true;
		memset(&split, 0, sizeof(split));
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
			s->n = 0;
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
	struct GBA* gba = e->core->board;
	struct GBAVideoRenderer* r = gba->video.renderer;
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
	return ((struct GBA*) e->core->board)->memory.io[(offset & 0x3FE) >> 1];
}

void emu_vram_write(Emu* e, uint32_t off, const void* src, size_t n) {
	struct GBA* gba = e->core->board;
	if (off >= 0x18000 || n > 0x18000 - off) return;
	memcpy((uint8_t*) gba->video.vram + off, src, n);
	/* (the software renderer's hook only drops its caches, whatever the address) */
	gba->video.renderer->writeVRAM(gba->video.renderer, off);
}

void emu_oam_read(Emu* e, uint16_t* out) {
	struct GBA* gba = e->core->board;
	memcpy(out, gba->video.oam.raw, sizeof(gba->video.oam.raw));
}
void emu_oam_write(Emu* e, const uint16_t* src) {
	struct GBA* gba = e->core->board;
	for (int i = 0; i < 512; ++i) {
		if (gba->video.oam.raw[i] == src[i]) continue;
		gba->video.oam.raw[i] = src[i];
		gba->video.renderer->writeOAM(gba->video.renderer, (uint32_t) i);
	}
}

const uint16_t* emu_vram(Emu* e) {
	return ((struct GBA*) e->core->board)->video.vram;
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

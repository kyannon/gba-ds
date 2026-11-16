#include "library.h"
#include "android.h"
#include "paths.h"
#include "profiles.h"
#include "patch.h"
#include "ui.h"
#include "logo.h"
#include "brand.h"
#include "emu.h"
#include "esde.h"
#include "esinteg.h"
#include "hotkeys.h"
#include "mapper.h"
#include "ra.h"
#include "update.h"
#include "clocks.h"
#include "game.h"


#include <ctype.h>
#include <dirent.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>
#ifndef __wasi__
#include <sys/resource.h>
#include <sys/syscall.h>
#endif

typedef struct {
	char path[1024];
	char name[160];
	char code[5];
	bool supported;
	int flag; /* BRAND_FLAG_* shown on the tile, or -1 (only one region of this game) */
	long long size, mtime; /* the file's, from the scan (its ROM checksum's cache key; with a patch
	                        * file beside it, their sizes added and the later time) */
	char patch[512]; /* the names of the patches it's played with (GEN-9: beside it, then switched on; " + " between), or "" */
	PatchChoice patchOn; /* the ones switched on in its patches folder */
} GameEntry;

static GameEntry* games;
static int nGames, capGames;

static const SDL_Color C_BG = { 14, 14, 26, 255 };
static const SDL_Color C_HEADER = { 30, 26, 52, 255 };
static const SDL_Color C_ROW = { 22, 22, 38, 255 };
static const SDL_Color C_SEL = { 92, 52, 140, 255 };
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
static const SDL_Color C_GOLD = { 232, 200, 120, 255 };
static const SDL_Color C_TAG = { 52, 120, 96, 255 };

static int cmp_games(const void* a, const void* b) {
	return strcasecmp(((const GameEntry*) a)->name, ((const GameEntry*) b)->name);
}

/* Game codes of the ROMs, remembered by path, size and date: reading one
 * means opening the file (unpacking the whole ROM for a .7z), which adds up
 * in a big library, above all on Android's shared storage (issue #49).
 * Saved as <state_dir>/codes.cache, lines "size mtime code path" (before
 * v1.5.3: archives.cache, archives only, read when codes.cache isn't there). */
typedef struct {
	long long size, mtime;
	char code[5];
	char path[1024];
	bool used;
} CodeCache;
static CodeCache* codeCache;
static int nCodeCache, capCodeCache;
static bool codeCacheDirty;
static int* codeIndex; /* open addressing over codeCache by path: -1 empty */
static int nCodeIndex;  /* (a power of two, at least twice nCodeCache) */

static unsigned path_hash(const char* s) {
	unsigned h = 2166136261u;
	while (*s) h = (h ^ (unsigned char) *s++) * 16777619u;
	return h;
}

static void code_index_put(int i) {
	unsigned m = (unsigned) nCodeIndex - 1, k = path_hash(codeCache[i].path) & m;
	while (codeIndex[k] >= 0) k = (k + 1) & m;
	codeIndex[k] = i;
}

static void code_cache_add(const CodeCache* c) {
	if (nCodeCache == capCodeCache) {
		capCodeCache = capCodeCache ? capCodeCache * 2 : 256;
		codeCache = realloc(codeCache, sizeof(*codeCache) * capCodeCache);
	}
	codeCache[nCodeCache++] = *c;
	if (nCodeCache * 2 > nCodeIndex) {
		nCodeIndex = nCodeIndex ? nCodeIndex * 2 : 512;
		while (nCodeCache * 2 > nCodeIndex) nCodeIndex *= 2;
		free(codeIndex);
		codeIndex = malloc(sizeof(*codeIndex) * nCodeIndex);
		for (int i = 0; i < nCodeIndex; ++i) codeIndex[i] = -1;
		for (int i = 0; i < nCodeCache; ++i) code_index_put(i);
	} else {
		code_index_put(nCodeCache - 1);
	}
}

static CodeCache* code_cache_find(const char* path) {
	if (!nCodeIndex) return NULL;
	unsigned m = (unsigned) nCodeIndex - 1;
	for (unsigned k = path_hash(path) & m; codeIndex[k] >= 0; k = (k + 1) & m) {
		if (!strcmp(codeCache[codeIndex[k]].path, path)) return &codeCache[codeIndex[k]];
	}
	return NULL;
}

static bool code_cache_read(const char* path) {
	FILE* f = fopen(path, "r");
	if (!f) return false;
	char line[1200];
	while (fgets(line, sizeof(line), f)) {
		CodeCache c = { 0 };
		int off = 0;
		if (sscanf(line, "%lld %lld %4s %n", &c.size, &c.mtime, c.code, &off) < 3 || !off) continue;
		line[strcspn(line, "\n")] = 0;
		snprintf(c.path, sizeof(c.path), "%s", line + off);
		if (!code_cache_find(c.path)) code_cache_add(&c);
	}
	fclose(f);
	return true;
}

static void code_cache_load(const Config* cfg) {
	free(codeCache);
	free(codeIndex);
	codeCache = NULL;
	codeIndex = NULL;
	nCodeCache = capCodeCache = nCodeIndex = 0;
	codeCacheDirty = false;
	char path[1100];
	snprintf(path, sizeof(path), "%s/codes.cache", cfg->stateDir);
	if (!code_cache_read(path)) {
		snprintf(path, sizeof(path), "%s/archives.cache", cfg->stateDir);
		codeCacheDirty = code_cache_read(path); /* (written again as codes.cache) */
	}
}

static void code_cache_save(const Config* cfg) {
	bool unused = false;
	for (int i = 0; i < nCodeCache; ++i) unused |= !codeCache[i].used;
	if (!codeCacheDirty && !unused) return;
	char path[1100];
	snprintf(path, sizeof(path), "%s/codes.cache", cfg->stateDir);
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < nCodeCache; ++i) {
		if (codeCache[i].used) fprintf(f, "%lld %lld %s %s\n", codeCache[i].size, codeCache[i].mtime, codeCache[i].code, codeCache[i].path);
	}
	fclose(f);
}

/* The 4-letter game code from the ROM header (offset 0xAC). */
static bool game_code(const char* path, const struct stat* st, char code[5]) {
	CodeCache* c = code_cache_find(path);
	if (c && c->size == (long long) st->st_size && c->mtime == (long long) st->st_mtime) {
		c->used = true;
		memcpy(code, c->code, 5);
		return true;
	}
	uint8_t hdr[0xB0];
	if (!emu_rom_header(path, hdr, sizeof(hdr))) {
		if (emu_is_archive_name(path)) fprintf(stderr, "library: no GBA ROM in %s\n", path);
		return false;
	}
	memcpy(code, hdr + 0xAC, 4);
	code[4] = 0;
	for (int i = 0; i < 4; ++i) {
		if (!code[i] || !isprint((unsigned char) code[i])) code[i] = '_';
	}
	if (c) { /* (the file changed) */
		c->size = st->st_size, c->mtime = st->st_mtime, c->used = true;
		memcpy(c->code, code, 5);
	} else {
		CodeCache n = { st->st_size, st->st_mtime, "", "", true };
		memcpy(n.code, code, 5);
		snprintf(n.path, sizeof(n.path), "%s", path);
		code_cache_add(&n);
	}
	codeCacheDirty = true;
	return true;
}

/* Folders read in this scan, by identity: the same folder can be listed
 * twice under different names (Android's shared storage ignores case, so
 * ROMs/gba is ROMs/GBA; a symlink; a repeated rom_dir entry). With their
 * times, and the rom_dir folders that couldn't be read: back from a game,
 * the list is kept as it is when none of them changed (issue #49). */
#define SCANNED_MAX 256
static struct {
	dev_t dev;
	ino_t ino;
	long long mtime, mtimeNs; /* (-1: wasn't there) */
	char path[1024];
} seenDirs[SCANNED_MAX];
static int nSeenDirs;
static bool seenOverflow; /* (more folders than that: always scanned again) */

static void dir_time(const struct stat* st, long long* s, long long* ns) {
	*s = (long long) st->st_mtime;
#if defined(__linux__) || defined(__ANDROID__) || defined(__wasi__)
	*ns = (long long) st->st_mtim.tv_nsec;
#else
	*ns = 0;
#endif
}

static void dir_note(const char* d, const struct stat* st) {
	if (nSeenDirs >= SCANNED_MAX) {
		seenOverflow = true;
		return;
	}
	seenDirs[nSeenDirs].dev = st ? st->st_dev : 0;
	seenDirs[nSeenDirs].ino = st ? st->st_ino : 0;
	if (st) dir_time(st, &seenDirs[nSeenDirs].mtime, &seenDirs[nSeenDirs].mtimeNs);
	else seenDirs[nSeenDirs].mtime = seenDirs[nSeenDirs].mtimeNs = -1;
	snprintf(seenDirs[nSeenDirs].path, sizeof(seenDirs[0].path), "%s", d);
	++nSeenDirs;
}

static bool dir_seen(const char* d, const struct stat* st) {
	for (int i = 0; i < nSeenDirs; ++i) {
		if (seenDirs[i].mtime >= 0 && seenDirs[i].dev == st->st_dev && seenDirs[i].ino == st->st_ino) return true;
	}
	return false;
}

/* What the last full scan listed with (anything else: scan again) */
static bool scanned;
static char scannedRomDir[sizeof(((Config*) 0)->romDir)];
static bool scannedShowAll, scannedNameTags;

/* Has anything the list was made from changed since the last scan? */
static bool library_changed(const Config* cfg) {
	if (!scanned || seenOverflow || strcmp(scannedRomDir, cfg->romDir) || scannedShowAll != cfg->showAll ||
	    scannedNameTags != cfg->nameTags)
		return true;
	for (int i = 0; i < nSeenDirs; ++i) {
		struct stat st;
		bool there = stat(seenDirs[i].path, &st) == 0 && S_ISDIR(st.st_mode);
		if (!there || seenDirs[i].mtime < 0) {
			if (there != (seenDirs[i].mtime >= 0)) return true;
			continue;
		}
		long long t, ns;
		dir_time(&st, &t, &ns);
		if (st.st_dev != seenDirs[i].dev || st.st_ino != seenDirs[i].ino || t != seenDirs[i].mtime || ns != seenDirs[i].mtimeNs)
			return true;
	}
	return false;
}

/* A slow scan (a big library, a slow card) shows that it's under way
 * instead of a black screen (issue #49): from 0.3 s in, ten times a second. */
static Uint32 scanStart, scanDrawn;
static int scanFiles;
static void scan_progress(void);

/* Drops the tags from a file name, "(USA)", "(En,Fr,De)", "[!]" and the
 * like (name_tags = 1 keeps them): the flag tells regional copies apart.
 * A name that is nothing but tags is left as it is. */
static void strip_tags(char* name) {
	char out[sizeof(((GameEntry*) 0)->name)];
	size_t n = 0;
	int depth = 0;
	for (const char* p = name; *p && n + 1 < sizeof(out); ++p) {
		if (*p == '(' || *p == '[') ++depth;
		else if ((*p == ')' || *p == ']') && depth) --depth;
		else if (!depth) out[n++] = *p;
	}
	out[n] = 0;
	/* collapse the spaces the tags leave behind */
	char* o = out;
	for (char* q = out; *q; ++q) {
		if (*q == ' ' && (o == out || o[-1] == ' ')) continue;
		*o++ = *q;
	}
	while (o > out && (o[-1] == ' ' || o[-1] == '-')) --o;
	*o = 0;
	if (out[0]) strcpy(name, out);
}

/* Its patches (GEN-9): beside it, and the ones switched on in its folder;
 * their sizes and times (and the choice's) in its ROM checksum's key.
 * `file` is the ROM's file name. */
static void entry_patches(const Config* cfg, GameEntry* g, const char* file, long long size, long long mtime) {
	g->size = size;
	g->mtime = mtime;
	g->patch[0] = 0;
	char pp[1100];
	struct stat ps;
	if (patch_find(g->path, pp, sizeof(pp)) && stat(pp, &ps) == 0) {
		snprintf(g->patch, sizeof(g->patch), "%s", patch_name(pp));
		g->size += (long long) ps.st_size;
		if ((long long) ps.st_mtime > g->mtime) g->mtime = (long long) ps.st_mtime;
	}
	GamePaths gpx;
	char base[256];
	snprintf(base, sizeof(base), "%s", file);
	char* dot = strrchr(base, '.');
	if (dot) *dot = 0;
	snprintf(gpx.patches, sizeof(gpx.patches), "%s/%s.patches", cfg->stateDir, base);
	patch_choice_load(gpx.patches, &g->patchOn);
	if (g->patchOn.nOn) {
		char dir[1024], pf[1400];
		patch_folder(cfg, g->path, g->code, dir, sizeof(dir), false);
		bool any = false;
		for (int i = 0; i < g->patchOn.nOn; ++i) {
			snprintf(pf, sizeof(pf), "%s/%s", dir, g->patchOn.on[i]);
			if (stat(pf, &ps) != 0) continue;
			size_t l = strlen(g->patch);
			snprintf(g->patch + l, sizeof(g->patch) - l, "%s%s", l ? " + " : "", g->patchOn.on[i]);
			g->size += (long long) ps.st_size;
			if ((long long) ps.st_mtime > g->mtime) g->mtime = (long long) ps.st_mtime;
			any = true;
		}
		/* (the choice's time too: the same patches in another order) */
		if (any && stat(gpx.patches, &ps) == 0 && (long long) ps.st_mtime > g->mtime) g->mtime = (long long) ps.st_mtime;
	}
}

static void scan_dir(Config* cfg, const char* d, int depth) {
	struct stat ds;
	if (stat(d, &ds) != 0 || !S_ISDIR(ds.st_mode)) {
		if (depth == 0) {
			fprintf(stderr, "library: cannot open %s\n", d);
			dir_note(d, NULL); /* (listed if it turns up) */
		}
		return;
	}
	if (dir_seen(d, &ds)) {
		fprintf(stderr, "library: %s already listed\n", d);
		return;
	}
	DIR* dir = opendir(d);
	if (!dir) {
		if (depth == 0) fprintf(stderr, "library: cannot open %s\n", d);
		dir_note(d, NULL);
		return;
	}
	dir_note(d, &ds);
	struct dirent* de;
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.') continue;
		++scanFiles;
		scan_progress();
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", d, de->d_name);
#ifdef DT_REG
		/* (a file that isn't a ROM, a save or a picture: no need to look at it) */
		if (de->d_type == DT_REG && !emu_is_rom_name(de->d_name)) continue;
#endif
		struct stat st;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < 3 && strcasecmp(de->d_name, "images") && strcasecmp(de->d_name, "videos") &&
			    strcasecmp(de->d_name, "manuals")) {
				scan_dir(cfg, full, depth + 1);
			}
			continue;
		}
		if (!emu_is_rom_name(de->d_name)) continue;
		const char* ext = strrchr(de->d_name, '.');
		GameEntry g;
		memset(&g, 0, sizeof(g));
		snprintf(g.path, sizeof(g.path), "%s", full);
		if (!game_code(g.path, &st, g.code)) continue;
		g.supported = profile_find(g.code) != NULL;
#ifdef GBADS_SIDE_CODES
		/* a side build lists only its game's releases (the Makefile's SIDE_CODES) */
		if (!strstr(GBADS_SIDE_CODES, g.code)) continue;
#endif
		if (!g.supported && !cfg->showAll) continue; /* (before its patches are looked for: issue #49) */
		entry_patches(cfg, &g, de->d_name, (long long) st.st_size, (long long) st.st_mtime);
		int n = (int) (ext - de->d_name);
		if (n >= (int) sizeof(g.name)) n = sizeof(g.name) - 1;
		memcpy(g.name, de->d_name, n);
		for (char* c = g.name; *c; ++c) {
			if (*c == '_') *c = ' ';
		}
		const MapProfile* prof = profile_find(g.code);
		if (prof && prof->listName) snprintf(g.name, sizeof(g.name), "%s", prof->listName);
		if (!cfg->nameTags) strip_tags(g.name);
		if (nGames == capGames) {
			capGames = capGames ? capGames * 2 : 64;
			games = realloc(games, sizeof(*games) * capGames);
		}
		games[nGames++] = g;
	}
	closedir(dir);
}

/* The region letter that ends a game code: E USA, J Japan, F France,
 * D Germany; P and the other European letters Europe. */
static int region_flag(char c) {
	if (c == 'E') return BRAND_FLAG_US;
	if (c == 'J') return BRAND_FLAG_JP;
	if (c == 'F') return BRAND_FLAG_FR;
	if (c == 'D') return BRAND_FLAG_DE;
	if (strchr("PDFSIHXYZU", c) && c) return BRAND_FLAG_EU;
	return -1;
}

/* ------------------------------------------- ROM checksums (GEN-8) */
/* Is the selected game's ROM the game as released, a romhack GBA DS knows,
 * or modified? Its CRC32 against the known dumps and mods (profile_dump).
 * Reading a whole ROM takes a moment, so it's done once per file, for the
 * selected game only, on a worker thread (inline with GBADS_SYNC=1 and in
 * the web build, which has no threads), and remembered in
 * <state_dir>/roms.cache, lines "size mtime crc32 patch path" (crc32 0:
 * unreadable; the ROM as played: patched when a patch file beside it fits,
 * patch its PatchResult, GEN-9). */
typedef struct {
	long long size, mtime;
	uint32_t crc;
	int patch;
	char path[1024];
} RomCrc;
static RomCrc* romCrcs;
static int nRomCrcs;
static bool romCrcsDirty;
static pthread_mutex_t crcLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t crcWake = PTHREAD_COND_INITIALIZER;
static RomCrc crcWant; /* the next file to read (path "": none) */
static char crcWantFiles[PATCH_ON_MAX + 1][1024]; /* and the patches to apply to it, in order (GEN-9) */
static const Config* crcCfg;       /* (the settings, for the patches' folder: set by scan) */
static int crcWantN;
static bool crcThread;
static unsigned crcGen; /* bumped as a checksum is worked out (crcLock) */

/* (crcLock held) */
static RomCrc* rom_crc_find(const char* path, long long size, long long mtime) {
	for (int i = 0; i < nRomCrcs; ++i) {
		if (romCrcs[i].size == size && romCrcs[i].mtime == mtime && !strcmp(romCrcs[i].path, path)) return &romCrcs[i];
	}
	return NULL;
}

/* (crcLock held) */
static void rom_crc_put(const RomCrc* c) {
	RomCrc* old = rom_crc_find(c->path, c->size, c->mtime);
	if (old) {
		*old = *c;
	} else {
		romCrcs = realloc(romCrcs, sizeof(*romCrcs) * (nRomCrcs + 1));
		romCrcs[nRomCrcs++] = *c;
	}
	romCrcsDirty = true;
	++crcGen;
}

/* (libmgba's; its header, mgba-util/crc32.h, wants zlib.h, which isn't on
 * every build's include path) */
uint32_t doCrc32(const void* buf, size_t size);

static uint32_t rom_crc_read(const char* path, char files[][1024], int nFiles, int* patch) {
	size_t n = 0;
	uint8_t* buf = emu_rom_read_patched(path, files, nFiles, &n, patch, NULL);
	uint32_t crc = buf ? doCrc32(buf, n) : 0;
	free(buf);
	return crc;
}

static void* rom_crc_worker(void* arg) {
	(void) arg;
#ifndef __wasi__
	setpriority(PRIO_PROCESS, (id_t) syscall(SYS_gettid), 19); /* (background work: the game first) */
#endif
	pthread_mutex_lock(&crcLock);
	for (;;) {
		while (!crcWant.path[0]) pthread_cond_wait(&crcWake, &crcLock);
		RomCrc c = crcWant;
		char files[PATCH_ON_MAX + 1][1024];
		int nf = crcWantN;
		memcpy(files, crcWantFiles, sizeof(files));
		crcWant.path[0] = 0;
		pthread_mutex_unlock(&crcLock);
		c.crc = rom_crc_read(c.path, files, nf, &c.patch);
		pthread_mutex_lock(&crcLock);
		rom_crc_put(&c);
	}
	return NULL;
}

static void rom_crc_load(const Config* cfg) {
	pthread_mutex_lock(&crcLock);
	if (!romCrcs) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/roms.cache", cfg->stateDir);
		FILE* f = fopen(path, "r");
		RomCrc c;
		while (f && fscanf(f, "%lld %lld %x %d %1023[^\n]", &c.size, &c.mtime, &c.crc, &c.patch, c.path) == 5) {
			romCrcs = realloc(romCrcs, sizeof(*romCrcs) * (nRomCrcs + 1));
			romCrcs[nRomCrcs++] = c;
		}
		if (f) fclose(f);
	}
	pthread_mutex_unlock(&crcLock);
}

static void rom_crc_save(const Config* cfg) {
	pthread_mutex_lock(&crcLock);
	if (romCrcsDirty) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/roms.cache", cfg->stateDir);
		FILE* f = fopen(path, "w");
		if (f) {
			for (int i = 0; i < nRomCrcs; ++i)
				fprintf(f, "%lld %lld %08x %d %s\n", romCrcs[i].size, romCrcs[i].mtime, romCrcs[i].crc, romCrcs[i].patch,
				        romCrcs[i].path);
			fclose(f);
			romCrcsDirty = false;
		}
	}
	pthread_mutex_unlock(&crcLock);
}

/* The game's ROM checksum if it's known; if not, asks for it (only supported
 * games: the others have no list of dumps to check it against). */
static bool game_crc(const GameEntry* g, uint32_t* crc, int* patch) {
	if (!g->supported) return false;
	pthread_mutex_lock(&crcLock);
	RomCrc* c = rom_crc_find(g->path, g->size, g->mtime);
	bool have = c != NULL;
	if (have) {
		*crc = c->crc;
		*patch = c->patch;
	}
	else if (strcmp(crcWant.path, g->path)) {
		crcWant.size = g->size;
		crcWant.mtime = g->mtime;
		snprintf(crcWant.path, sizeof(crcWant.path), "%s", g->path);
		crcWantN = crcCfg ? patch_plan(crcCfg, g->path, g->code, &g->patchOn, crcWantFiles, PATCH_ON_MAX + 1) : 0;
		if (!crcThread && !(getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC")))) {
			pthread_t t;
			crcThread = pthread_create(&t, NULL, rom_crc_worker, NULL) == 0;
			if (crcThread) pthread_detach(t);
		}
		if (crcThread) {
			pthread_cond_signal(&crcWake);
		} else { /* (no threads, or GBADS_SYNC: now) */
			RomCrc w = crcWant;
			crcWant.path[0] = 0;
			pthread_mutex_unlock(&crcLock);
			w.crc = rom_crc_read(w.path, crcWantFiles, crcWantN, &w.patch);
			pthread_mutex_lock(&crcLock);
			rom_crc_put(&w);
			*crc = w.crc;
			*patch = w.patch;
			have = true;
		}
	}
	pthread_mutex_unlock(&crcLock);
	return have && *crc;
}

static unsigned rom_crc_generation(void) {
	pthread_mutex_lock(&crcLock);
	unsigned g = crcGen;
	pthread_mutex_unlock(&crcLock);
	return g;
}

static void scan_progress(void) {
	if (getenv("GBADS_SYNC") && atoi(getenv("GBADS_SYNC"))) return; /* (tests: exact frames) */
	Uint32 now = SDL_GetTicks();
	if (now - scanStart < 300 || now - scanDrawn < 100) return;
	scanDrawn = now;
	ui_events(); /* (keeps the system from thinking the app has hung) */
	ui_clear();
	for (int scr = 0; scr < nScreens; ++scr) {
		SDL_Rect r = screens[scr].rect;
		float px = ui_px(scr);
		ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
		ui_label(scr, r.w / 2, r.h / 2 - px * 6, px * 6, "Loading games...", C_TEXT);
		char line[96];
		snprintf(line, sizeof(line), "%d file%s checked, %d game%s found", scanFiles, scanFiles == 1 ? "" : "s", nGames,
		         nGames == 1 ? "" : "s");
		ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 8, px * 4, line, C_DIM, 0);
	}
	ui_present();
}

static void scan(Config* cfg) {
	free(games);
	games = NULL;
	nGames = capGames = 0;
	char dirs[1024];
	snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
	char* save;
	nSeenDirs = 0;
	seenOverflow = false;
	scanStart = SDL_GetTicks();
	scanDrawn = 0;
	scanFiles = 0;
	code_cache_load(cfg);
	rom_crc_load(cfg);
	crcCfg = cfg;
	for (char* d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) scan_dir(cfg, d, 0);
	code_cache_save(cfg);
	qsort(games, nGames, sizeof(*games), cmp_games);
	/* Region flags, only where the library holds the same game from more
	 * than one region: same first three code letters, different last one. */
	for (int i = 0; i < nGames; ++i) {
		games[i].flag = -1;
		int mine = region_flag(games[i].code[3]);
		if (mine < 0) continue;
		for (int j = 0; j < nGames; ++j) {
			if (j != i && !strncmp(games[i].code, games[j].code, 3) && region_flag(games[j].code[3]) >= 0 &&
			    region_flag(games[j].code[3]) != mine) {
				games[i].flag = mine;
				break;
			}
		}
	}
	for (int i = 0; i < nGames; ++i) ui_logo_queue(cfg, games[i].path, games[i].code);
	scanned = true;
	snprintf(scannedRomDir, sizeof(scannedRomDir), "%s", cfg->romDir);
	scannedShowAll = cfg->showAll;
	scannedNameTags = cfg->nameTags;
	fprintf(stderr, "library: %d game(s), %d file(s) in %d folder(s), %llu ms\n", nGames, scanFiles, nSeenDirs,
	        (unsigned long long) (SDL_GetTicks() - scanStart));
}

/* -------------------------------------------------------------- preview */

static SDL_Texture* thumbTex[2]; /* resume screenshot, per view */
static int thumbFor = -2;
static SDL_Texture* logoTex[2];
static int logoW, logoH;
static bool logoPixel;
static uint32_t logoGen;

/* built-in images: our logo, button glyphs, "no save state yet!" */
static SDL_Texture* brandTex[BRAND_COUNT][2];
static int brandW[BRAND_COUNT], brandH[BRAND_COUNT];

static void load_brand(void) {
	for (int i = 0; i < BRAND_COUNT; ++i) {
		if (!brandTex[i][0] && !brandTex[i][1]) ui_load_brand(i, brandTex[i], &brandW[i], &brandH[i]);
	}
}

static void free_brand(void) {
	for (int i = 0; i < BRAND_COUNT; ++i) ui_free_image(brandTex[i]);
}

/* Draws built-in image `id` at height h with its top-left at x, y; returns the width drawn. */
static int draw_brand(int scr, int id, int x, int y, int h) {
	if (!brandTex[id][0] && !brandTex[id][1]) return 0;
	int w = brandW[id] * h / brandH[id];
	SDL_Rect a = { x, y, w, h };
	ui_image(scr, brandTex[id], brandW[id], brandH[id], "stretch", &a);
	return w;
}

/* s, cut short with "..." if it's wider than maxW in that font and size. */
static void fit_text(char* out, size_t outSize, int font, int capPx, const char* s, int maxW) {
	snprintf(out, outSize, "%s", s);
	if (ui_font_width(font, capPx, out) <= maxW) return;
	size_t n = strlen(out);
	while (n > 0) {
		do --n; while (n > 0 && ((unsigned char) out[n] & 0xC0) == 0x80); /* whole UTF-8 characters */
		snprintf(out, outSize, "%.*s...", (int) n, s);
		if (ui_font_width(font, capPx, out) <= maxW) return;
	}
}

static void free_thumbs(void) {
	ui_free_image(logoTex);
	ui_free_image(thumbTex);
}

static void load_thumbs(Config* cfg, int idx) {
	if (idx == thumbFor) return;
	thumbFor = idx;
	free_thumbs();
	if (idx < 0 || idx >= nGames) return;
	GamePaths gp;
	game_paths(cfg, games[idx].path, &gp);
	if (ui_load_logo(cfg, games[idx].path, games[idx].code, logoTex, &logoW, &logoH, &logoPixel)) {
		/* drawn at a fixed height rather than a whole-number scale, so filter it */
		for (int v = 0; v < 2; ++v) {
			if (logoTex[v]) SDL_SetTextureScaleMode(logoTex[v], SDL_ScaleModeLinear);
		}
	}
	SDL_Surface* s = SDL_LoadBMP(gp.thumb);
	if (s) {
		for (int v = 0; v < nViews; ++v) thumbTex[v] = SDL_CreateTextureFromSurface(views[v].ren, s);
		SDL_FreeSurface(s);
	}
}

static void draw_preview(Config* cfg, int scr, int sel) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 0, 0, 0, 255 }); /* black, like the logos */
	load_brand();

	/* our logo top left, the game's own logo top right, both the same height */
	int m = px * 6;
	int headH = px * 21;
	int ourW = draw_brand(scr, BRAND_LOGO, m, m, headH);
	{
		/* where to find us, under our logo, as the version number is drawn
		 * (the pixel font's capitals; without https://, so it stays clear
		 * of a wide game logo on the right) */
		int vpx = SDL_max(1, px / 2);
		ui_text(scr, m + px * 2, m + headH + px * 2, vpx, "GITHUB.COM/KYANNON/GBA-DS", C_DIM);
	}

	/* button hints along the bottom */
	int ty = r.h - px * 11, d = px * 10, bx = m;
	int gy = ty + px * 7 / 2 - d / 2;
	int cap = px * 5, cy = gy + d / 2, wordW = px * 18;
	bx += draw_brand(scr, BRAND_BTN_A, bx, gy, d) + px * 2;
	ui_label(scr, bx + wordW / 2, cy, cap, "Play", C_TEXT);
	bx += wordW + px * 8;
	bx += draw_brand(scr, BRAND_BTN_B, bx, gy, d) + px * 2;
	ui_label(scr, bx + wordW / 2, cy, cap, "Quit", C_TEXT);

	{
		int vpx = SDL_max(1, px / 2);
		const char* v = "V" GBADS_VERSION;
		ui_text(scr, r.w - m - ui_text_width(v, vpx), cy - vpx * 7 / 2, vpx, v, C_DIM);
	}
	if (nGames == 0 || sel < 0) {
		ui_label(scr, r.w / 2, r.h / 2, px * 6, "No games found", C_TEXT);
		return;
	}
	GameEntry* g = &games[sel];
	load_thumbs(cfg, sel);
	if (logoTex[0] || logoTex[1]) {
		int gameH = headH * 2;
		int maxW = r.w - ourW - m * 3;
		int w = logoW * gameH / logoH, h = gameH;
		if (w > maxW) w = maxW, h = logoH * maxW / logoW;
		SDL_Rect a = { r.w - m - w, m + (gameH - h) / 2, w, h };
		ui_image(scr, logoTex, logoW, logoH, "stretch", &a);
	} else if (g->supported) {
		/* no logo for it yet (or none extracted for this game): its name in its place */
		const MapProfile* prof = profile_find(g->code);
		int gameH = headH * 2, maxW = r.w - ourW - m * 3;
		if (prof && prof->title) ui_title_block(scr, (SDL_Rect) { r.w - m - maxW, m, maxW, gameH }, prof->title, px * 9, C_TEXT);
	}
	/* (a game with no profile has no logo either: the note goes where a
	 * game's logo would, at the top right, beside ours) */
	if (!g->supported) ui_font_text(scr, TF_BODY, r.w - m, m + headH / 2, px * 5, "Single screen only: plays normally", C_DIM, 1);
	/* a romhack, or a modified ROM, under the game's logo (GEN-8), patched
	 * by a patch file beside it or not (GEN-9) */
	uint32_t crc;
	int patch = PATCH_NONE;
	if (game_crc(g, &crc, &patch)) {
		const char* name;
		const char* from = patch == PATCH_APPLIED ? " (patch)" : "";
		char line[400] = "";
		if (patch == PATCH_PARTIAL) {
			snprintf(line, sizeof(line), "Patched, but a patch was left out: see Patches in the game's menu");
		} else if (patch == PATCH_WRONG_ROM) {
			snprintf(line, sizeof(line), "Patch not applied: %s is for another ROM", g->patch);
		} else if (patch == PATCH_UNREADABLE) {
			snprintf(line, sizeof(line), "Patch not applied: %s can't be read", g->patch);
		} else {
			switch (profile_dump(g->code, crc, &name)) {
			case DUMP_MOD_TESTED: snprintf(line, sizeof(line), "Romhack: %s%s", name, from); break;
			case DUMP_MOD: snprintf(line, sizeof(line), "Romhack: %s%s, untested", name, from); break;
			case DUMP_MODIFIED:
				if (patch == PATCH_APPLIED) snprintf(line, sizeof(line), "Patched: the second screen may not work");
				else snprintf(line, sizeof(line), "Modified ROM: the second screen may not work");
				break;
			default: break;
			}
		}
		if (line[0]) ui_font_text(scr, TF_BODY, r.w - m, m + headH * 2 + px * 7, px * 4, line, C_DIM, 1);
	}

	/* the resume point (the game screen only), or a note that there isn't one,
	 * with clear space above (below the game's logo) and below (above the hints) */
	int y = m + headH * 2 + px * 16;
	SDL_Rect area = { m, y, r.w - m * 2, gy - px * 14 - y };
	if (thumbTex[0] || thumbTex[1]) {
		ui_image(scr, thumbTex, GBA_W, GBA_H, "fit", &area);
	} else {
		ui_label(scr, r.w / 2, area.y + area.h / 2, px * 7, "no save state yet!", C_TEXT);
	}
}

/* -------------------------------------------------------------- list: a grid of logos */

static const SDL_Color C_EDGE = { 150, 120, 210, 255 };

typedef struct {
	int headerH, listH, m, gap, tileW, tileH, rowH, cols, visibleRows;
} ListGeom;

/* The game list as names (default) or a grid of logos: library_view in
 * <state_dir>/library.cfg, switched by the header button or X. */
static bool listMode = true;

static ListGeom list_geom(int scr) {
	ListGeom g;
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	g.headerH = px * 20;
	g.listH = r.h - g.headerH;
	g.m = px * 4;
	if (listMode) { /* one row per game */
		g.gap = px * 2;
		g.cols = 1;
		g.tileH = px * 17;
	} else { /* two columns of logos */
		g.gap = px * 4;
		g.cols = 2;
		g.tileH = px * 40;
	}
	g.tileW = (r.w - g.m * 2 - g.gap * (g.cols - 1)) / g.cols;
	g.rowH = g.tileH + g.gap;
	g.visibleRows = SDL_max(1, (g.listH - g.m) / g.rowH);
	return g;
}

static SDL_Rect tile_rect(ListGeom* g, int i, int scroll) {
	int row = i / g->cols, col = i % g->cols;
	return (SDL_Rect) { g->m + col * (g->tileW + g->gap), g->headerH + g->m + row * g->rowH - scroll, g->tileW, g->tileH };
}

/* each game's logo, loaded on first sight; reloaded when a new one is extracted */
static SDL_Texture* tileLogo[256][2];
static int tileLogoW[256], tileLogoH[256];
static bool tileTried[256];

static void free_tiles(void) {
	for (int i = 0; i < 256; ++i) {
		ui_free_image(tileLogo[i]);
		tileTried[i] = false;
	}
}

static SDL_Rect settings_button(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int h = px * 14, w = px * 44;
	return (SDL_Rect) { r.w - w - px * 4, (px * 20 - h) / 2, w, h };
}

/* left of Settings: "Supported" (games with a second screen) or "All" (every GBA game) */
static SDL_Rect games_button(int scr) {
	SDL_Rect sb = settings_button(scr);
	float px = ui_px(scr);
	int w = px * 38;
	return (SDL_Rect) { sb.x - px * 3 - w, sb.y, w, sb.h };
}

/* square button left of that: switches between list and grid */
static SDL_Rect view_button(int scr) {
	SDL_Rect gb = games_button(scr);
	float px = ui_px(scr);
	return (SDL_Rect) { gb.x - px * 3 - gb.h, gb.y, gb.h, gb.h };
}

/* square button left of the list / grid one: the dark or light theme */
static SDL_Rect theme_button(int scr) {
	SDL_Rect vb = view_button(scr);
	float px = ui_px(scr);
	return (SDL_Rect) { vb.x - px * 3 - vb.w, vb.y, vb.w, vb.h };
}

/* a moon (the dark theme is on) or a sun (the light one) */
static void draw_theme_button(int scr) {
	SDL_Rect b = theme_button(scr);
	float px = ui_px(scr);
	ui_key(scr, b, px * 3, KEY_PLAIN);
	SDL_Color ink = ui_key_ink(), mid = ui_key_mid(KEY_PLAIN);
	int cx = b.x + b.w / 2, cy = b.y + b.h / 2, r = b.h * 13 / 56;
	if (!ui_theme_light()) {
		ui_fill_round(scr, (SDL_Rect) { cx - r, cy - r, r * 2, r * 2 }, ink, r);
		ui_fill_round(scr, (SDL_Rect) { cx - r + r * 7 / 10, cy - r - r * 3 / 10, r * 2 * 85 / 100, r * 2 * 85 / 100 }, mid, r * 85 / 100);
	} else {
		int c = r * 62 / 100;
		ui_fill_round(scr, (SDL_Rect) { cx - c, cy - c, c * 2, c * 2 }, ink, c);
		int d = SDL_max(2, r * 4 / 10), far = r * 105 / 100, near = r * 75 / 100;
		for (int i = 0; i < 8; ++i) {
			static const int dx[8] = { 1, 1, 0, -1, -1, -1, 0, 1 }, dy[8] = { 0, 1, 1, 1, 0, -1, -1, -1 };
			int k = (dx[i] && dy[i]) ? near * 7 / 10 + d / 2 : far;
			ui_fill_round(scr, (SDL_Rect) { cx + dx[i] * k - d / 2, cy + dy[i] * k - d / 2, d, d }, ink, d / 2);
		}
	}
}

static void draw_games_button(const Config* cfg, int scr) {
	SDL_Rect b = games_button(scr);
	float px = ui_px(scr);
	ui_key(scr, b, px * 3, KEY_PLAIN);
	ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, cfg->showAll ? "All" : "Supported", C_TEXT);
}

/* Four squares (in the list: switch to the grid) or three lines (in the
 * grid: switch to the list). */
static void draw_view_button(int scr) {
	SDL_Rect b = view_button(scr);
	float px = ui_px(scr);
	ui_key(scr, b, px * 3, KEY_PLAIN);
	SDL_Color ink = ui_key_ink();
	int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
	if (listMode) {
		int sq = b.h * 12 / 56, gap = b.h * 6 / 56;
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 2; ++j) {
				SDL_Rect q = { cx - sq - gap / 2 + i * (sq + gap), cy - sq - gap / 2 + j * (sq + gap), sq, sq };
				ui_fill_round(scr, q, ink, SDL_max(1, sq / 4));
			}
		}
	} else {
		int lw = b.h * 28 / 56, lh = SDL_max(2, b.h * 5 / 56), gap = b.h * 7 / 56;
		for (int j = -1; j <= 1; ++j) {
			SDL_Rect l = { cx - lw / 2, cy + j * (lh + gap) - lh / 2, lw, lh };
			ui_fill_round(scr, l, ink, lh / 2);
		}
	}
}

/* library.cfg: the game list's own settings (and, on Android, the ROM and
 * save folders and the saves' type, from Settings > Folders). */
static void save_library_cfg(const Config* cfg) {
	char file[1100];
	snprintf(file, sizeof(file), "%s/library.cfg", cfg->stateDir);
	FILE* f = fopen(file, "w");
	if (!f) return;
	fprintf(f, "# written by the game list\nlibrary_view = %s\n", listMode ? "list" : "grid");
#ifdef __ANDROID__
	fprintf(f, "rom_dir = %s\n", cfg->romDir);
	/* (Settings > Folders; empty: next to the games) */
	fprintf(f, "save_dir = %s\nsave_ext = %s\n", cfg->saveDir, cfg->saveExt[0] ? cfg->saveExt : "sav");
#endif
	fclose(f);
}

static void setting_changed(Config* cfg, const Setting* st);
/* the theme button: dark or light, kept for all games (prefs.cfg) */
static void toggle_theme(Config* cfg) {
	cfg->lightTheme = !cfg->lightTheme;
	setting_changed(cfg, config_setting("theme"));
}

static void toggle_view(Config* cfg) {
	listMode = !listMode;
	snprintf(cfg->libraryView, sizeof(cfg->libraryView), "%s", listMode ? "list" : "grid");
	save_library_cfg(cfg);
}

/* Can GBA DS write where it keeps its files (states, logos, screenshots)?
 * Checked once a run; when it can't, the game list's header says so
 * instead of everything failing quietly. */
static char storageMsg[256];
static void storage_check(const Config* cfg) {
	static bool checked;
	if (checked) return;
	checked = true;
	char path[1100];
	mkdirs(cfg->stateDir);
	snprintf(path, sizeof(path), "%s/.write-test", cfg->stateDir);
	FILE* f = fopen(path, "w");
	bool ok = f && fputs("ok\n", f) >= 0;
	if (f && fclose(f) != 0) ok = false;
	remove(path);
	/* the data folder couldn't be reached or moved to (paths.c) */
	if (ok && data_dir_note()[0]) snprintf(storageMsg, sizeof(storageMsg), "%s", data_dir_note());
	if (ok) return;
	fprintf(stderr, "storage: can't write in %s\n", cfg->stateDir);
	/* (on Android without All files access the app's own folder is used,
	 * which is always writable: this is something else, so no advice) */
	snprintf(storageMsg, sizeof(storageMsg), "Can't save files here: no states, logos or screenshots");
}

static void draw_list(Config* cfg, int scr, int sel, int scroll) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ListGeom g = list_geom(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, C_BG);
	if (nGames == 0) {
		ui_label(scr, r.w / 2, r.h / 2 - px * 14, px * 6, "No supported games in", C_TEXT);
		char dir[300];
		fit_text(dir, sizeof(dir), TF_BODY, px * 5, cfg->romDir, r.w - px * 16);
		ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2, px * 5, dir, C_GOLD, 0);
		ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 14, px * 4,
		             "Only games with a map screen are listed (show_all = 1 lists every one)", C_DIM, 0);
	}
	int done, total;
	logo_progress(&done, &total);
	bool extracting = done < total;
	for (int i = 0; i < nGames && i < 256; ++i) {
		SDL_Rect t = tile_rect(&g, i, scroll);
		if (t.y + t.h <= g.headerH || t.y >= r.h) continue;
		bool on = i == sel;
		ui_key(scr, t, px * 3, on ? KEY_SEL : KEY_PLAIN);
		if (listMode) {
			/* name on the left; region flag and "Single screen only" on the right */
			int right = t.x + t.w - px * 5;
			if (games[i].flag >= 0) {
				int fh = px * 7;
				int fw = brandH[games[i].flag] ? brandW[games[i].flag] * fh / brandH[games[i].flag] : 0;
				draw_brand(scr, games[i].flag, right - fw, t.y + (t.h - fh) / 2, fh);
				right -= fw + px * 4;
			}
			if (!games[i].supported && cfg->showAll) {
				int tagW = ui_label_width(scr, px * 4, "Single screen only") + px * 6;
				SDL_Rect tg = { right - tagW, t.y + (t.h - px * 11) / 2, tagW, px * 11 };
				ui_fill_round(scr, tg, C_TAG, px * 2);
				ui_label(scr, tg.x + tg.w / 2, tg.y + tg.h / 2, px * 4, "Single screen only", C_TEXT);
				right -= tagW + px * 4;
			}
			char line[300];
			fit_text(line, sizeof(line), TF_BOLD, px * 5, games[i].name, right - (t.x + px * 6));
			ui_font_text(scr, TF_BOLD, t.x + px * 6, t.y + t.h / 2, px * 5, line, C_TEXT, -1);
			continue;
		}
		if (!tileTried[i]) {
			tileTried[i] = true;
			bool pixelArt;
			if (ui_load_logo(cfg, games[i].path, games[i].code, tileLogo[i], &tileLogoW[i], &tileLogoH[i], &pixelArt)) {
				for (int v = 0; v < 2; ++v) {
					if (tileLogo[i][v]) SDL_SetTextureScaleMode(tileLogo[i][v], SDL_ScaleModeLinear);
				}
			}
		}
		if (tileLogo[i][0] || tileLogo[i][1]) {
			/* the logo, as large as fits with some padding, keeping its shape */
			int pad = px * 5;
			int aw = t.w - pad * 2, ah = t.h - pad * 2;
			int w = aw, h = tileLogoH[i] * aw / tileLogoW[i];
			if (h > ah) h = ah, w = tileLogoW[i] * ah / tileLogoH[i];
			SDL_Rect a = { t.x + (t.w - w) / 2, t.y + (t.h - h) / 2, w, h };
			ui_image(scr, tileLogo[i], tileLogoW[i], tileLogoH[i], "stretch", &a);
		} else if (games[i].supported && extracting) {
			ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, px * 4, "Loading thumbnail...", C_DIM);
		} else {
			/* no logo to be had (no map profile, or extraction failed): the name */
			char line[300];
			fit_text(line, sizeof(line), TF_BOLD, px * 5, games[i].name, t.w - px * 10);
			ui_label(scr, t.x + t.w / 2, t.y + t.h / 2, px * 5, line, C_TEXT);
		}
		if (games[i].flag >= 0) {
			/* region flag in the bottom-right corner */
			int fh = px * 7;
			int fw = brandH[games[i].flag] ? brandW[games[i].flag] * fh / brandH[games[i].flag] : 0;
			draw_brand(scr, games[i].flag, t.x + t.w - fw - px * 3, t.y + t.h - fh - px * 3, fh);
		}
		if (!games[i].supported && cfg->showAll) {
			int tagW = ui_label_width(scr, px * 4, "Single screen only") + px * 6;
			SDL_Rect tg = { t.x + t.w - tagW - px * 3, t.y + px * 3, tagW, px * 11 };
			ui_fill_round(scr, tg, C_TAG, px * 2);
			ui_label(scr, tg.x + tg.w / 2, tg.y + tg.h / 2, px * 4, "Single screen only", C_TEXT);
		}
	}
	/* header drawn last so tiles scroll underneath it */
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, g.headerH }, C_HEADER);
	SDL_Rect sb = settings_button(scr);
	if (storageMsg[0]) {
		char line[300];
		fit_text(line, sizeof(line), TF_BOLD, px * 4, storageMsg, theme_button(scr).x - px * 8);
		ui_font_text(scr, TF_BOLD, theme_button(scr).x / 2, g.headerH / 2, px * 4, line, C_GOLD, 0);
	} else {
		ui_label(scr, theme_button(scr).x / 2, g.headerH / 2, px * 5, "Touch or use the D-Pad", C_TEXT);
	}
	draw_theme_button(scr);
	draw_view_button(scr);
	draw_games_button(cfg, scr);
	ui_key(scr, sb, px * 3, KEY_PLAIN);
	ui_label(scr, sb.x + sb.w / 2, sb.y + sb.h / 2, px * 5, "Settings", C_TEXT);
}

/* ------------------------------------------------------------ settings */

static char settingsMsg[320];
static bool updatesOpen;   /* the Updates panel (Linux), over Settings or the game list */
static bool updatesNotice; /* opened by an automatic check */
static int updatesSel;

/* Settings: a row of buttons, each with its text below, then Back.
 * Linux: GBA DS in EmulationStation (ROCKNIX) or in BananaOS's list, where
 * there's one; Map Buttons and Hotkeys; Performance, where GBA DS can set
 * the clock governors (clocks.c); Updates. Android: Folders,
 * RetroAchievements, Map Buttons and Hotkeys, Updates. */
#define SETTINGS_MAX 8
static int nRows = 1; /* rows shown (Back is row nRows) */
/* (Hotkeys shares Map Buttons' line: the right half of it, under one text) */
enum { ROW_BANANA, ROW_ES, ROW_MAP, ROW_HOTKEYS, ROW_UPDATES, ROW_FOLDERS, ROW_RA, ROW_ESDE, ROW_PERF, ROW_RESET, ROW_DELCFG };
static int rowKind[SETTINGS_MAX];

#ifdef __ANDROID__
/* the row showing kind (0 if none) */
static int row_of(int kind) {
	for (int i = 0; i < nRows; ++i) {
		if (rowKind[i] == kind) return i;
	}
	return 0;
}
#endif

static void settings_rows(const Config* cfg) {
#ifdef __ANDROID__
	(void) cfg;
	static const int android[] = { ROW_FOLDERS, ROW_RA, ROW_UPDATES };
	nRows = 0;
	/* ES-DE's folder found: GBA DS in its emulator list (esde.c) */
	if (esde_present()) rowKind[nRows++] = ROW_ESDE;
	for (size_t i = 0; i < sizeof(android) / sizeof(android[0]); ++i) rowKind[nRows++] = android[i];
	rowKind[nRows++] = ROW_RESET;
	rowKind[nRows++] = ROW_DELCFG;
#else
	nRows = 0;
#ifndef GBADS_SIDE
	/* (not in a side build: the system's GBA games would start it) */
	if (banana_present()) rowKind[nRows++] = ROW_BANANA;
	else if (es_present(cfg)) rowKind[nRows++] = ROW_ES;
#endif
#ifdef GBADS_UPDATES
	rowKind[nRows++] = ROW_UPDATES;
#endif
	rowKind[nRows++] = ROW_RESET;
	rowKind[nRows++] = ROW_DELCFG;
#endif
}

typedef struct {
	SDL_Rect panel, row[SETTINGS_MAX], back;
	int textY[SETTINGS_MAX];
	float px;
} SettingsGeom;

static SDL_Rect set_panel(int scr);
static SDL_Rect set_row(int scr, int n, int i);
static SettingsGeom settings_geom(int scr) {
	/* the same rows as the other pages (set_row), the older panel's text now a
	 * line under them for the row chosen */
	SettingsGeom g;
	g.px = ui_px(scr);
	g.panel = set_panel(scr);
	for (int i = 0; i < nRows; ++i) g.row[i] = set_row(scr, nRows, i), g.textY[i] = 0;
	g.back = set_row(scr, nRows, nRows);
	return g;
}

/* Body text in Jost, word-wrapped; returns the y below it. */
static int draw_wrapped(int scr, int x, int y, int width, float px, const char* text, SDL_Color c) {
	return ui_font_wrap(scr, TF_BODY, x, y, width, px * 4, text, c);
}

/* The panel, its buttons (labels[i] on row i; sel == nRows is Back). */
static void draw_settings_frame(int scr, const SettingsGeom* g, int sel, const char* const* labels) {
	float px = g->px;
	ui_dim(scr, 200);
	ui_fill_round(scr, g->panel, C_HEADER, px * 4);
	ui_label(scr, g->panel.x + g->panel.w / 2, g->panel.y + px * 11, px * 6, "System", C_TEXT);
	for (int i = 0; i <= nRows; ++i) {
		SDL_Rect b = i < nRows ? g->row[i] : g->back;
		ui_key(scr, b, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		if (i == nRows) {
			ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, "Back", C_TEXT);
			continue;
		}
		ui_font_text(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2, px * 5, labels[i], C_TEXT, -1);
		ui_font_text(scr, TF_BOLD, b.x + b.w - px * 6, b.y + b.h / 2, px * 6, "\u203A", i == sel ? C_TEXT : C_DIM, 1);
	}
}

/* the line under the rows, for the row chosen (a message from it in gold) */
static void draw_system_note(int scr, const SettingsGeom* g, const char* text, SDL_Color c) {
	float px = g->px;
	if (nRows < 1 || !text) return;
	SDL_Rect last = g->row[nRows - 1];
	ui_font_wrap(scr, TF_BODY, g->panel.x + px * 8, last.y + last.h + px * 3, g->panel.w - px * 16, px * 4, text, c);
}

/* ---- Settings: a page of categories, each a page of rows (docs/MENUS.md).
 * The rows come from config.c's settings table; a change is kept in
 * states/prefs.cfg, for all games (a game's own file is for the pause menu).
 * System is the older panel: GBA DS in EmulationStation / BananaOS's list,
 * Map Buttons, Hotkeys, Updates, and on Android Folders and RetroAchievements. */
enum { SP_HOME = -1, SP_SYSTEM = 6, SP_COUNT = 7 };
typedef struct {
	const char* name;
	const char* sub;
	const char* keys[10];
} SetPage;
static const SetPage setPages[SP_SYSTEM] = {
	{ "Games", "Which games, names, saving", { "show_all", "name_tags", "theme", "save_on_quit", "auto_load", "custom_logos", "quick_bar_hint" } },
	{ "Display", "Scale, shader, colors", { "scale", "map_scale", "shader", "lcd_strength", "color_correction", "lcd_ghosting", "smooth" } },
	{ "Screens", "Placement, overlays", { "swap", "screen_match", "screen_placement", "overlays", "overlay_screens", "bezel_logo", "brightness_top", "brightness_bottom", "show_fps" } },
	{ "Controls", "Stick, buttons, hotkeys", { "left_stick_dpad", "ab_swap", "button_right", "button_bottom", "button_top", "button_left", "turbo_rate", "@map", "@hotkeys" } },
	{ "Speed and sound", "Rewind, fast forward", { "match_display", "frame_buffer", "rewind", "rewind_seconds", "fast_forward", "audio_latency", "performance" } },
	{ "Second screen", "The map and the logo", { "map", "map_interval", "map_crop", "map_animations", "show_logo", "pinball_live" } },
};
static int setPage = SP_HOME; /* SP_HOME, a category, or SP_SYSTEM (the older panel) */
static int setSel;
static bool setRescan; /* the games shown changed: list them again */

/* the rows of a page: out[i] its setting, or NULL for a row that opens a screen
 * (ids[i] is "@map" or "@hotkeys") */
static int page_settings(int page, const Setting** out, const char** ids) {
	int n = 0;
	for (int i = 0; i < 10 && setPages[page].keys[i]; ++i) {
		const char* k = setPages[page].keys[i];
		const Setting* st = k[0] == '@' ? NULL : config_setting(k);
		if (k[0] != '@' && (!st || (!strcmp(st->key, "performance") && !clocks_available()))) continue;
		ids[n] = k;
		out[n++] = st;
	}
	return n;
}

static void page_action(Config* cfg, const char* id) {
	if (!strcmp(id, "@map")) mapper_run(cfg);
	else if (!strcmp(id, "@hotkeys")) hotkeys_run(cfg);
}

/* the panel, as the older Settings has it */
static SDL_Rect set_panel(int scr) {
	SDL_Rect r = screens[scr].rect;
	int m = (int) ui_px(scr) * 6;
	return (SDL_Rect) { m, m, r.w - m * 2, r.h - m * 2 };
}

/* home: 2 x 4 tiles, the categories, System, Back */
static SDL_Rect set_tile(int scr, int i) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	int top = p.y + px * 22, gap = px * 4, rows = 4;
	int w = (p.w - px * 12 - gap) / 2, h = (p.y + p.h - px * 6 - top - gap * (rows - 1)) / rows;
	return (SDL_Rect) { p.x + px * 6 + (i % 2) * (w + gap), top + (i / 2) * (h + gap), w, h };
}

/* a page: n rows, then Back */
static SDL_Rect set_row(int scr, int n, int i) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	int top = p.y + px * 20, gap = px * 2;
	int noteH = px * 12, backH = px * 14;
	int avail = p.y + p.h - px * 6 - backH - noteH - top;
	int h = SDL_min(px * 16, avail / SDL_max(1, n) - gap);
	if (i == n) return (SDL_Rect) { p.x + (p.w - px * 60) / 2, p.y + p.h - px * 5 - backH, px * 60, backH };
	return (SDL_Rect) { p.x + px * 6, top + i * (h + gap), p.w - px * 12, h };
}

static void draw_set_home(int scr, int sel) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	ui_dim(scr, 200);
	ui_fill_round(scr, p, C_HEADER, px * 4);
	ui_label(scr, p.x + p.w / 2, p.y + px * 12, px * 6, "Settings", C_TEXT);
	for (int i = 0; i < 8; ++i) {
		SDL_Rect t = set_tile(scr, i);
		ui_key(scr, t, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		const char* name = i < SP_SYSTEM ? setPages[i].name : i == SP_SYSTEM ? "System" : "Back";
		const char* sub = i < SP_SYSTEM ? setPages[i].sub : i == SP_SYSTEM ? "Updates, integrations" : NULL;
#ifdef __ANDROID__
		if (i == SP_SYSTEM) sub = "Folders, accounts, updates";
#endif
		ui_label(scr, t.x + t.w / 2, t.y + t.h / 2 - (sub ? px * 4 : 0), px * 6, name, C_TEXT);
		if (sub) ui_font_text(scr, TF_BODY, t.x + t.w / 2, t.y + t.h / 2 + px * 7, px * 4, sub, C_DIM, 0);
	}
}

static void draw_set_page(const Config* cfg, int scr, int page, int sel) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	const Setting* rows[10];
	const char* ids[10];
	int n = page_settings(page, rows, ids);
	ui_dim(scr, 200);
	ui_fill_round(scr, p, C_HEADER, px * 4);
	ui_label(scr, p.x + p.w / 2, p.y + px * 11, px * 6, setPages[page].name, C_TEXT);
	for (int i = 0; i <= n; ++i) {
		SDL_Rect r = set_row(scr, n, i);
		ui_key(scr, r, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		if (i == n) {
			ui_label(scr, r.x + r.w / 2, r.y + r.h / 2, px * 5, "Back", C_TEXT);
			continue;
		}
		if (!rows[i]) {
			ui_font_text(scr, TF_BOLD, r.x + px * 6, r.y + r.h / 2, px * 5, !strcmp(ids[i], "@map") ? "Map buttons" : "Hotkeys", C_TEXT, -1);
			ui_font_text(scr, TF_BOLD, r.x + r.w - px * 6, r.y + r.h / 2, px * 6, "›", i == sel ? C_TEXT : C_DIM, 1);
			continue;
		}
		char val[48];
		config_setting_text(cfg, rows[i], val, sizeof(val));
		ui_font_text(scr, TF_BOLD, r.x + px * 6, r.y + r.h / 2, px * 5, rows[i]->label, C_TEXT, -1);
		int ax = r.x + r.w - px * 6;
		ui_font_text(scr, TF_BOLD, ax, r.y + r.h / 2, px * 6, "›", i == sel ? C_TEXT : C_DIM, 1);
		int vw = ui_font_width(TF_BOLD, px * 5, val);
		ui_font_text(scr, TF_BOLD, ax - px * 8, r.y + r.h / 2, px * 5, val, C_GOLD, 1);
		ui_font_text(scr, TF_BOLD, ax - px * 16 - vw, r.y + r.h / 2, px * 6, "‹", i == sel ? C_TEXT : C_DIM, 1);
	}
	const char* note = sel >= n ? NULL : rows[sel] ? rows[sel]->note
	                 : !strcmp(ids[sel], "@map") ? "Sets which of this device's buttons does what."
	                                             : "Sets the shortcuts: the hotkey button held with another.";
	if (note) {
		SDL_Rect last = set_row(scr, n, n - 1);
		ui_font_wrap(scr, TF_BODY, p.x + px * 8, last.y + last.h + px * 3, p.w - px * 16, px * 4, note, C_DIM);
	}
}

/* a setting changed in the menu: kept in prefs.cfg (for all games) and put to use */
static void setting_changed(Config* cfg, const Setting* st) {
	char lines[2][2][24];
	const char* kv[2][2];
	int n = config_setting_lines(cfg, st, lines);
	for (int i = 0; i < n; ++i) kv[i][0] = lines[i][0], kv[i][1] = lines[i][1];
	char path[1100];
	mkdirs(cfg->stateDir);
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg->stateDir);
	config_kv_update(path, "# written by gba-ds's menus; overrides gba-ds.cfg", n, kv);
	if (!strcmp(st->key, "performance")) clocks_set(cfg->perfMax);
	if (!strcmp(st->key, "theme")) ui_theme(cfg->lightTheme);
	if (!strncmp(st->key, "brightness_", 11)) ui_brightness(cfg->brightTop, cfg->brightBottom);
	if (!strcmp(st->key, "show_all") || !strcmp(st->key, "name_tags")) setRescan = true;
	if (!strcmp(st->key, "screen_match") || !strcmp(st->key, "screen_placement")) ui_layout_pictures();
	fprintf(stderr, "setting: %s = %s (all games)\n", lines[0][0], lines[0][1]);
}


/* System > Reset settings to defaults / Delete per-game settings: each asks to be
 * touched again before it does anything (confirmRow: the row asked) */
#define RESET_NOTE "Puts every setting in Settings back to GBA DS's own default: the theme, brightness, screens, controls and so on. " \
                   "Folders, accounts, hotkeys, button mapping and each game's own settings stay."
#define DELCFG_NOTE "Removes the settings every game has kept for itself (from the pause menu's This game), so all games " \
                    "use the settings for all games again. States, saves and cheats stay."
static int confirmRow = -1;

static int reset_settings(Config* cfg) {
	Config def;
	config_defaults(&def);
	const char* keys[200];
	char names[200][40];
	int n = 0;
	for (int i = 0; i < config_setting_count() && n < 200; ++i) {
		const Setting* st = config_setting_at(i);
		config_setting_copy(cfg, &def, st);
		snprintf(names[n], sizeof(names[n]), "%s", st->key);
		keys[n] = names[n];
		++n;
	}
	char path[1100];
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg->stateDir);
	config_kv_remove(path, n, keys);
	ui_theme(cfg->lightTheme);
	ui_brightness(cfg->brightTop, cfg->brightBottom);
	clocks_set(cfg->perfMax);
	ui_layout_pictures();
	setRescan = true;
	return n;
}

static int delete_game_settings(const Config* cfg) {
	static const char* const keep[] = { "prefs.cfg", "library.cfg", "hotkeys.cfg", "update.cfg" };
	DIR* d = opendir(cfg->stateDir);
	if (!d) return 0;
	int n = 0;
	struct dirent* e;
	while ((e = readdir(d))) {
		size_t len = strlen(e->d_name);
		if (len < 5 || strcmp(e->d_name + len - 4, ".cfg")) continue;
		bool kept = false;
		for (size_t i = 0; i < sizeof(keep) / sizeof(*keep); ++i) kept |= !strcmp(e->d_name, keep[i]);
		if (kept) continue;
		char path[1400];
		snprintf(path, sizeof(path), "%s/%s", cfg->stateDir, e->d_name);
		if (remove(path) == 0) ++n;
	}
	closedir(d);
	return n;
}

static void system_action(Config* cfg, int row) {
	if (confirmRow != row) {
		confirmRow = row;
		snprintf(settingsMsg, sizeof(settingsMsg), "Are you sure? Touch it again to %s. Anything else cancels.",
		         rowKind[row] == ROW_RESET ? "put every setting back to its default" : "delete every game's own settings");
		return;
	}
	confirmRow = -1;
	if (rowKind[row] == ROW_RESET) {
		int n = reset_settings(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), "Settings are back to their defaults.");
		fprintf(stderr, "settings: reset to defaults (%d settings)\n", n);
	} else {
		int n = delete_game_settings(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), n == 0 ? "No game has settings of its own."
		         : n == 1 ? "Deleted the settings of 1 game." : "Deleted the settings of %d games.", n);
		fprintf(stderr, "settings: deleted %d per-game settings files\n", n);
	}
}

/* one frame of input for the pages; false when Settings should close */
static bool settings_pages_input(Config* cfg, int ls) {
	const Setting* rows[10];
	const char* ids[10];
	int n = setPage >= 0 && setPage < SP_SYSTEM ? page_settings(setPage, rows, ids) : 0;
	bool back = btn_pressed(B_B) || btn_pressed(B_SELECT) || btn_released(B_HOME);
	if (setPage == SP_HOME) {
		if (btn_pressed(B_DOWN)) setSel = (setSel + 2) % 8;
		if (btn_pressed(B_UP)) setSel = (setSel + 6) % 8;
		if (btn_pressed(B_RIGHT) || btn_pressed(B_LEFT)) setSel ^= 1;
		if (btn_pressed(B_A) || btn_pressed(B_START)) {
			if (setSel == 7) return false;
			setPage = setSel;
			setSel = 0;
			if (setPage == SP_SYSTEM) settings_rows(cfg), confirmRow = -1;
		}
		if (back) return false;
	} else {
		int dir = 0;
		if (btn_pressed(B_DOWN)) setSel = (setSel + 1) % (n + 1);
		if (btn_pressed(B_UP)) setSel = (setSel + n) % (n + 1);
		if (btn_pressed(B_LEFT)) dir = -1;
		if (btn_pressed(B_RIGHT) || btn_pressed(B_A) || btn_pressed(B_START)) dir = 1;
		if (setSel == n) {
			if (dir && !btn_pressed(B_LEFT) && !btn_pressed(B_RIGHT)) setPage = SP_HOME, setSel = 0;
		} else if (!rows[setSel]) {
			if (dir > 0) page_action(cfg, ids[setSel]);
		} else if (dir && config_setting_step(cfg, rows[setSel], dir)) {
			setting_changed(cfg, rows[setSel]);
		}
		if (back) setSel = setPage, setPage = SP_HOME;
	}
	Pointer pt;
	while (ui_next_pointer(&pt)) {
		if (pt.screen != ls || pt.kind != 1) continue;
		SDL_Point q = { pt.x, pt.y };
		SDL_Rect panel = set_panel(ls);
		if (setPage == SP_HOME) {
			int hit = -1;
			for (int i = 0; i < 8; ++i) {
				SDL_Rect t = set_tile(ls, i);
				if (SDL_PointInRect(&q, &t)) hit = i;
			}
			if (hit == 7 || (hit < 0 && !SDL_PointInRect(&q, &panel))) return false;
			if (hit >= 0) {
				setPage = hit;
				setSel = 0;
				if (setPage == SP_SYSTEM) settings_rows(cfg), confirmRow = -1;
			}
			continue;
		}
		SDL_Rect b = set_row(ls, n, n);
		if (SDL_PointInRect(&q, &b)) {
			setSel = setPage, setPage = SP_HOME;
			continue;
		}
		if (!SDL_PointInRect(&q, &panel)) {
			setSel = setPage, setPage = SP_HOME;
			continue;
		}
		for (int i = 0; i < n; ++i) {
			SDL_Rect r = set_row(ls, n, i);
			if (!SDL_PointInRect(&q, &r)) continue;
			setSel = i;
			if (!rows[i]) {
				page_action(cfg, ids[i]);
				continue;
			}
			/* the left part of the value steps back, the rest forward */
			int d = q.x > r.x + r.w * 62 / 100 && q.x < r.x + r.w * 75 / 100 ? -1 : 1;
			if (config_setting_step(cfg, rows[i], d)) setting_changed(cfg, rows[i]);
		}
	}
	return true;
}

#ifdef __ANDROID__
/* Android: the ROM and save folders (Folders, a panel of its own), chosen
 * with Android's folder picker (android.c), kept in <state_dir>/library.cfg;
 * the RetroAchievements account, signed in with a dialog (android.c,
 * GbaDsActivity) and kept as a token by ra.c. */
static bool pickPending, raDialogPending;
static int pickFor; /* PICK_ROMS, PICK_SAVES or PICK_DATA: what the picker's folder is for */
enum { PICK_ROMS, PICK_SAVES, PICK_DATA };
static int settingsMsgRow; /* the row settingsMsg is about: shown in place of its text */
static bool foldersOpen;
static int foldersSel;

static bool srm_saves(const Config* cfg) {
	return !strcmp(cfg->saveExt, "srm");
}

/* "Games are read from ... Saves are kept in ..." (the Folders panel's) */
static void folders_text(const Config* cfg, char* line, size_t n) {
	char dirs[sizeof(cfg->romDir)];
	snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
	for (char* p = dirs; *p; ++p) {
		if (*p == ':') *p = ' ';
	}
	const char* type = srm_saves(cfg) ? ".srm" : ".sav";
	if (cfg->saveDir[0]) snprintf(line, n, "Games are read from %s (subfolders too). Saves (%s) are kept in %s.", dirs, type, cfg->saveDir);
	else snprintf(line, n, "Games are read from %s (subfolders too). Saves (%s) are kept next to the games.", dirs, type);
}

static void draw_settings(const Config* cfg, int scr, int sel) {
	if (sel != confirmRow) confirmRow = -1; /* moved off the row: no longer asked */
	SettingsGeom g = settings_geom(scr);
	char user[128], err[256];
	int ra = ra_account(user, sizeof(user), err, sizeof(err));
	const char* raLabel = ra == RA_ACCOUNT_OK     ? "RetroAchievements: Sign Out" :
	                      ra == RA_ACCOUNT_BUSY   ? "RetroAchievements: Signing In..." :
	                      ra == RA_ACCOUNT_OFF    ? "RetroAchievements: Off" :
	                                                "RetroAchievements: Sign In";
	const char* labels[SETTINGS_MAX];
	for (int i = 0; i < nRows; ++i) {
		int k = rowKind[i];
		labels[i] = k == ROW_ESDE    ? (esde_integration_on() ? "GBA DS in ES-DE: On" : "GBA DS in ES-DE: Off")
		          : k == ROW_FOLDERS ? "Folders"
		          : k == ROW_RA      ? raLabel
		          : k == ROW_RESET   ? (confirmRow == i ? "Are you sure? Reset settings" : "Reset settings to defaults")
		          : k == ROW_DELCFG  ? (confirmRow == i ? "Are you sure? Delete them" : "Delete per-game settings")
		                             : "Updates";
	}
	draw_settings_frame(scr, &g, sel, labels);
	if (sel >= nRows) return;
	char line[1400];
	const char* text = NULL;
	SDL_Color col = C_DIM;
	switch (rowKind[sel]) {
	case ROW_ESDE:
		text = "Makes GBA DS the emulator for Game Boy Advance games in ES-DE. For a game it doesn't support, "
		       "choose another in ES-DE (the game's metadata > Alternative emulator).";
		break;
	case ROW_RESET:
		text = RESET_NOTE;
		break;
	case ROW_DELCFG:
		text = DELCFG_NOTE;
		break;
	case ROW_FOLDERS:
		text = cfg->saveDir[0] ? "Where games are read from, and the folder their saves are kept in."
		                       : "Where games are read from. Their saves are kept next to them.";
		break;
	case ROW_RA:
		if (raDialogPending) {
			snprintf(line, sizeof(line), "Enter your account in the sign-in box...");
		} else if (ra == RA_ACCOUNT_OK) {
			snprintf(line, sizeof(line), "Signed in as %s. Achievements are earned in casual mode.", user);
		} else if (ra == RA_ACCOUNT_BUSY) {
			snprintf(line, sizeof(line), "Signing in as %s...", user);
		} else if (ra == RA_ACCOUNT_FAILED) {
			size_t n = strlen(err);
			while (n && (err[n - 1] == '.' || err[n - 1] == ' ')) err[--n] = 0; /* the server's own full stop */
			snprintf(line, sizeof(line), "Couldn't sign in as %s%s%s. Sign in to try again.", user, err[0] ? ": " : "", err);
			col = C_GOLD;
		} else if (ra == RA_ACCOUNT_OFF) {
			snprintf(line, sizeof(line), "Switched off in gba-ds.cfg (retroachievements = 0).");
		} else {
			snprintf(line, sizeof(line),
			         "Earn achievements in casual mode with your retroachievements.org account. GBA DS keeps a sign-in token, "
			         "not your password.");
		}
		text = line;
		break;
	default:
		text = "This is v" GBADS_VERSION ". Look for a new version, and download and install it.";
		break;
	}
	if (settingsMsg[0] && settingsMsgRow == sel) text = settingsMsg, col = C_GOLD;
	draw_system_note(scr, &g, text, col);
}

static void settings_activate(Config* cfg, int row) {
	settingsMsg[0] = 0;
	settingsMsgRow = row;
	if (rowKind[row] == ROW_RESET || rowKind[row] == ROW_DELCFG) {
		system_action(cfg, row);
		return;
	}
	confirmRow = -1;
	switch (rowKind[row]) {
	case ROW_ESDE:
		esde_integration_set(!esde_integration_on(), settingsMsg, sizeof(settingsMsg));
		return;
	case ROW_FOLDERS:
		foldersOpen = true, foldersSel = 0;
		return;
	case ROW_UPDATES:
		updatesOpen = true, updatesNotice = false, updatesSel = 0;
		return;
	default:
		break;
	}
	char user[128], err[256];
	int ra = ra_account(user, sizeof(user), err, sizeof(err));
	if (ra == RA_ACCOUNT_OK) {
		ra_sign_out();
		snprintf(settingsMsg, sizeof(settingsMsg), "Signed out of RetroAchievements.");
	} else if (ra == RA_ACCOUNT_NONE || ra == RA_ACCOUNT_FAILED) {
		if (android_ra_dialog(user)) raDialogPending = true;
		else snprintf(settingsMsg, sizeof(settingsMsg), "Couldn't open the sign-in box.");
	}
}

/* GBA DS can write in dir (made if it isn't there): All files access */
static bool can_write_in(const char* dir) {
	mkdirs(dir);
	char test[1100];
	snprintf(test, sizeof(test), "%s/.gba-ds-write-test", dir);
	FILE* f = fopen(test, "w");
	if (!f) return false;
	fclose(f);
	remove(test);
	return true;
}

/* GBA DS's own folder (issue #34): where it's moved to at the next start
 * (android_data_setup), data_dir in data-folder.cfg in the app's storage */
static bool data_folder_wanted(char* out, size_t n) {
	char record[1100];
	return android_data_record(record, sizeof(record)) && kv_get(record, "data_dir", out, n) && out[0] == '/';
}

/* a folder with anything in it but hidden files */
static bool dir_in_use(const char* dir) {
	DIR* d = opendir(dir);
	if (!d) return false;
	bool used = false;
	struct dirent* e;
	while (!used && (e = readdir(d))) used = e->d_name[0] != '.';
	closedir(d);
	return used;
}

static void data_folder_picked(const char* path) {
	const char* home = android_home();
	char to[1100], states[1200], record[1100];
	struct stat st;
	snprintf(to, sizeof(to), "%s", path);
	snprintf(states, sizeof(states), "%s/states", path);
	/* a folder with other things in it (Emulators, the storage's own top):
	 * GBA DS's in a "GBA DS" folder there, not spread among them */
	if (home && strcmp(path, home) && dir_in_use(path) && stat(states, &st) != 0) snprintf(to, sizeof(to), "%s/GBA DS", path);
	size_t hn = home ? strlen(home) : 0;
	if (!android_data_record(record, sizeof(record))) {
		snprintf(settingsMsg, sizeof(settingsMsg), "Couldn't keep that choice.");
	} else if (home && !strcmp(to, home)) {
		/* (chosen back before the move happened: nothing to move) */
		kv_set(record, "data_dir", to, "# GBA DS's data folder");
		snprintf(settingsMsg, sizeof(settingsMsg), "GBA DS's files are kept there already.");
	} else if (home && !strncmp(to, home, hn) && to[hn] == '/') {
		snprintf(settingsMsg, sizeof(settingsMsg), "That's inside GBA DS's own folder: choose one outside it.");
	} else if (!can_write_in(path)) {
		snprintf(settingsMsg, sizeof(settingsMsg),
		         "GBA DS can't save files there: allow it All files access in Android's settings, or choose another folder.");
	} else if (!kv_set(record, "data_dir", to, "# GBA DS's data folder")) {
		snprintf(settingsMsg, sizeof(settingsMsg), "Couldn't keep that choice.");
	} else {
		snprintf(settingsMsg, sizeof(settingsMsg),
		         "GBA DS's files go to %s the next time it starts: close GBA DS and open it again.", to);
		fprintf(stderr, "library: data folder to %s at the next start\n", to);
	}
}

/* Answers from the folder picker and the sign-in box, once they come.
 * True if the ROM folder changed. */
static bool settings_poll(Config* cfg) {
	if (raDialogPending) {
		settingsMsgRow = row_of(ROW_RA);
		char user[128], pass[256];
		int st = android_take_ra_credentials(user, sizeof(user), pass, sizeof(pass));
		if (st) raDialogPending = false;
		if (st > 0 && user[0] && pass[0]) ra_sign_in(user, pass);
		else if (st > 0) snprintf(settingsMsg, sizeof(settingsMsg), "Enter both your username and your password.");
		memset(pass, 0, sizeof(pass));
	}
	if (!pickPending) return false;
	char path[1024];
	int st = android_take_picked_folder(path, sizeof(path));
	if (!st) return false;
	pickPending = false;
	settingsMsgRow = row_of(ROW_FOLDERS);
	if (st < 0) {
		snprintf(settingsMsg, sizeof(settingsMsg), "No folder chosen.");
		return false;
	}
	if (pickFor == PICK_DATA) {
		data_folder_picked(path);
		return false;
	}
	if (pickFor == PICK_SAVES) {
		if (!can_write_in(path)) {
			snprintf(settingsMsg, sizeof(settingsMsg),
			         "GBA DS can't save files there: allow it All files access in Android's settings, or choose another folder.");
			return false;
		}
		snprintf(cfg->saveDir, sizeof(cfg->saveDir), "%s", path);
		save_library_cfg(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), "Saves will be kept there.");
		fprintf(stderr, "library: saves in %s\n", path);
		return false;
	}
	snprintf(cfg->romDir, sizeof(cfg->romDir), "%s", path);
	save_library_cfg(cfg);
	return true;
}

/* ------------------------------------------------------------ Settings > Folders (Android) */

enum { FB_ROMS, FB_SAVES, FB_BESIDE, FB_TYPE, FB_DATA, FB_BACK };

static int folders_buttons(const Config* cfg, int* ids) {
	int n = 0;
	ids[n++] = FB_ROMS, ids[n++] = FB_SAVES;
	if (cfg->saveDir[0]) ids[n++] = FB_BESIDE;
	ids[n++] = FB_TYPE, ids[n++] = FB_DATA, ids[n++] = FB_BACK;
	return n;
}

typedef struct {
	SDL_Rect panel, text, btn[6];
	float px;
} FoldersGeom;

static FoldersGeom folders_geom(int scr, int nb) {
	FoldersGeom g;
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	g.px = px;
	int m = px * 6, rowH = nb > 5 ? px * 12 : px * 14, gap = px * 3;
	g.panel = (SDL_Rect) { m, m, r.w - m * 2, r.h - m * 2 };
	int y = g.panel.y + g.panel.h - px * 6;
	for (int i = nb - 1; i >= 0; --i) {
		y -= rowH;
		g.btn[i] = (SDL_Rect) { g.panel.x + px * 6, y, g.panel.w - px * 12, rowH };
		y -= gap;
	}
	int top = g.panel.y + px * 22;
	g.text = (SDL_Rect) { g.panel.x + px * 8, top, g.panel.w - px * 16, y - top - px * 2 };
	return g;
}

static void draw_folders(const Config* cfg, int scr, int sel) {
	int ids[6];
	int nb = folders_buttons(cfg, ids);
	FoldersGeom g = folders_geom(scr, nb);
	float px = g.px;
	ui_dim(scr, 200);
	ui_fill_round(scr, g.panel, C_HEADER, px * 4);
	ui_label(scr, g.panel.x + g.panel.w / 2, g.panel.y + px * 12, px * 6, "Folders", C_TEXT);
	for (int i = 0; i < nb; ++i) {
		SDL_Rect b = g.btn[i];
		const char* label = ids[i] == FB_ROMS   ? "Choose ROM folder"
		                  : ids[i] == FB_SAVES  ? "Choose save folder"
		                  : ids[i] == FB_BESIDE ? "Keep saves next to the games"
		                  : ids[i] == FB_TYPE   ? (srm_saves(cfg) ? "Save files: .srm (RetroArch's)" : "Save files: .sav")
		                  : ids[i] == FB_DATA   ? "Choose GBA DS folder"
		                                        : "Back";
		ui_key(scr, b, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, label, C_TEXT);
	}
	int tx = g.text.x, tw = g.text.w, y = g.text.y;
	SDL_Rect clip = { screens[scr].rect.x + g.text.x, screens[scr].rect.y + g.text.y, g.text.w, g.text.h };
	ui_clip(scr, &clip);
	char line[1600];
	if (pickPending) snprintf(line, sizeof(line), "Choose a folder in Android's file picker...");
	else folders_text(cfg, line, sizeof(line));
	y = draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
	/* GBA DS's own folder, and where it's going */
	char want[1024];
	const char* home = android_home();
	if (home) {
		if (data_folder_wanted(want, sizeof(want)) && strcmp(want, home))
			snprintf(line, sizeof(line), "GBA DS's own files (states, settings, cheats, overlays) are in %s, moving to %s at the next start.", home, want);
		else
			snprintf(line, sizeof(line), "GBA DS's own files (states, settings, cheats, overlays) are in %s.", home);
		y = draw_wrapped(scr, tx, y + px * 3, tw, px, line, C_TEXT);
	}
	if (settingsMsg[0]) y = draw_wrapped(scr, tx, y + px * 3, tw, px, settingsMsg, C_GOLD);
	draw_wrapped(scr, tx, y + px * 3, tw, px,
	             "A game's save already next to it, or in the other type, is copied over the first time you play it. "
	             ".srm is the type RetroArch uses: choose its saves folder too to share each game's save with it.",
	             C_DIM);
	ui_clip(scr, NULL);
}

/* One step of the Folders panel; false once it's closed. */
static bool folders_input(Config* cfg, int ls) {
	int ids[6];
	int nb = folders_buttons(cfg, ids);
	if (foldersSel >= nb) foldersSel = nb - 1;
	int act = -1;
	if (btn_pressed(B_DOWN)) foldersSel = (foldersSel + 1) % nb;
	if (btn_pressed(B_UP)) foldersSel = (foldersSel + nb - 1) % nb;
	if (btn_pressed(B_A) || btn_pressed(B_START)) act = ids[foldersSel];
	if (btn_pressed(B_B) || btn_released(B_HOME)) act = FB_BACK;
	Pointer p;
	while (ui_next_pointer(&p)) {
		if (p.screen != ls || p.kind != 1) continue;
		FoldersGeom g = folders_geom(ls, nb);
		SDL_Point pt = { p.x, p.y };
		for (int i = 0; i < nb; ++i) {
			if (SDL_PointInRect(&pt, &g.btn[i])) foldersSel = i, act = ids[i];
		}
		if (act < 0 && !SDL_PointInRect(&pt, &g.panel)) act = FB_BACK;
	}
	if (act >= 0 && act != FB_BACK) settingsMsg[0] = 0;
	switch (act) {
	case FB_ROMS:
	case FB_SAVES:
	case FB_DATA:
		if (pickPending) break;
		pickFor = act == FB_ROMS ? PICK_ROMS : act == FB_SAVES ? PICK_SAVES : PICK_DATA;
		if (android_pick_folder()) pickPending = true;
		else snprintf(settingsMsg, sizeof(settingsMsg), "Couldn't open the file picker.");
		break;
	case FB_BESIDE:
		cfg->saveDir[0] = 0;
		save_library_cfg(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), "Saves will be kept next to the games.");
		fprintf(stderr, "library: saves next to the games\n");
		foldersSel = 1;
		break;
	case FB_TYPE:
		snprintf(cfg->saveExt, sizeof(cfg->saveExt), "%s", srm_saves(cfg) ? "sav" : "srm");
		save_library_cfg(cfg);
		fprintf(stderr, "library: saves as .%s\n", cfg->saveExt);
		break;
	case FB_BACK:
		settingsMsg[0] = 0;
		return false;
	default:
		break;
	}
	return true;
}
#else
static int settingsMsgRow; /* the row settingsMsg is about: shown in place of its text */

static void draw_settings(const Config* cfg, int scr, int sel) {
	if (sel != confirmRow) confirmRow = -1; /* moved off the row: no longer asked */
	SettingsGeom g = settings_geom(scr);
	float px = g.px;
	const char* labels[SETTINGS_MAX];
	for (int i = 0; i < nRows; ++i) {
		labels[i] = rowKind[i] == ROW_BANANA ? (banana_integration_on() ? "GBA DS in BananaOS's GBA list: On" : "GBA DS in BananaOS's GBA list: Off")
		          : rowKind[i] == ROW_ES     ? (es_integration_on(cfg) ? "GBA DS in EmulationStation: On" : "GBA DS in EmulationStation: Off")
		          : rowKind[i] == ROW_RESET  ? (confirmRow == i ? "Are you sure? Reset settings" : "Reset settings to defaults")
		          : rowKind[i] == ROW_DELCFG ? (confirmRow == i ? "Are you sure? Delete them" : "Delete per-game settings")
		          : rowKind[i] == ROW_PERF   ? (cfg->perfMax ? "Performance: Set to Max" : "Performance: Let the System Decide")
		                                     : "Updates";
	}
	draw_settings_frame(scr, &g, sel, labels);
	if (sel >= nRows) return;
	int i = sel;
	const char* text = rowKind[i] == ROW_BANANA
	                       ? "Games picked in BananaOS's own Game Boy Advance list start in GBA DS when it has "
	                         "second-screen support for them. Restart BananaOS after switching this."
	                   : rowKind[i] == ROW_ES
	                       ? "Adds GBA DS to the emulators for Game Boy Advance games in EmulationStation. Pick it for one game "
	                         "(its Advanced Game Options > Emulator) or for all of them (the system's advanced settings). "
	                         "The other emulators keep working as before."
	                   : rowKind[i] == ROW_RESET ? RESET_NOTE
	                   : rowKind[i] == ROW_DELCFG ? DELCFG_NOTE
	                   : rowKind[i] == ROW_PERF
	                       ? (cfg->perfMax ? "The processor, memory and graphics run at full speed while GBA DS is open: "
	                                         "the smoothest play, but the battery runs down faster and the device gets warmer."
	                                       : "The system sets the speed, as for its other emulators. If games stutter or "
	                                         "the sound breaks up, try Set to Max.")
	                       : "This is v" GBADS_VERSION ". Look for a new version, and download and install it.";
	/* a message from switching a row (settings_activate): in place of its text */
	bool msg = settingsMsg[0] && settingsMsgRow == i;
	if (msg) text = settingsMsg;
	draw_system_note(scr, &g, text, msg ? C_GOLD : C_DIM);
	/* the trade-off, set apart and in bold so it isn't missed */
	if (rowKind[i] == ROW_BANANA && !msg) {
		SDL_Rect last = g.row[nRows - 1];
		ui_font_wrap(scr, TF_BOLD, g.panel.x + px * 8, last.y + last.h + px * 28, g.panel.w - px * 16, px * 4,
		             "NOTE: Unsupported games will play in RetroArch, but without BananaOS's own "
		             "second screen (its game panel and companions).",
		             C_TEXT);
	}
}

static void settings_activate(Config* cfg, int row) {
	settingsMsg[0] = 0;
	settingsMsgRow = row;
	if (rowKind[row] == ROW_RESET || rowKind[row] == ROW_DELCFG) {
		system_action(cfg, row);
		return;
	}
	confirmRow = -1;
	if (rowKind[row] == ROW_UPDATES) updatesOpen = true, updatesNotice = false, updatesSel = 0;
	else if (rowKind[row] == ROW_BANANA) banana_integration_set(!banana_integration_on(), settingsMsg, sizeof(settingsMsg));
	else if (rowKind[row] == ROW_ES) es_integration_set(cfg, !es_integration_on(cfg), settingsMsg, sizeof(settingsMsg));
	else if (rowKind[row] == ROW_PERF) {
		cfg->perfMax = !cfg->perfMax;
		clocks_set(cfg->perfMax);
		prefs_save(cfg);
	}
}

static bool settings_poll(Config* cfg) {
	(void) cfg;
	return false;
}
#endif

#ifdef GBADS_UPDATES
/* ------------------------------------------------------------ updates */

enum { UB_CHECK, UB_AUTO, UB_BACK, UB_UPDATE, UB_LATER, UB_SKIP, UB_CANCEL };

/* The buttons for the state, bottom to top order reversed: top first. */
static int update_buttons(const UpdateInfo* u, int* ids) {
	int n = 0;
	switch (u->state) {
	case UPD_AVAILABLE:
		ids[n++] = UB_UPDATE, ids[n++] = UB_LATER, ids[n++] = UB_SKIP;
		break;
	case UPD_DOWNLOADING:
		ids[n++] = UB_CANCEL;
		break;
	case UPD_INSTALLING:
		break;
	case UPD_DONE:
#ifdef __ANDROID__
		ids[n++] = UB_BACK; /* (Android's installer is up; it closes GBA DS once it installs) */
#endif
		break;
	case UPD_CHECKING:
		ids[n++] = UB_BACK;
		break;
	default:
		ids[n++] = UB_CHECK, ids[n++] = UB_AUTO, ids[n++] = UB_BACK;
	}
	return n;
}

typedef struct {
	SDL_Rect panel, text, bar, btn[4];
	float px;
} UpdatesGeom;

static UpdatesGeom updates_geom(int scr, int nb) {
	UpdatesGeom g;
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	g.px = px;
	int m = px * 6, rowH = px * 15, gap = px * 4;
	g.panel = (SDL_Rect) { m, m, r.w - m * 2, r.h - m * 2 };
	int y = g.panel.y + g.panel.h - px * 6;
	for (int i = nb - 1; i >= 0; --i) {
		y -= rowH;
		g.btn[i] = (SDL_Rect) { g.panel.x + px * 6, y, g.panel.w - px * 12, rowH };
		y -= gap;
	}
	int top = g.panel.y + px * 22;
	g.text = (SDL_Rect) { g.panel.x + px * 8, top, g.panel.w - px * 16, y - top - px * 2 };
	g.bar = (SDL_Rect) { g.panel.x + px * 12, g.panel.y + g.panel.h / 2 - px * 2, g.panel.w - px * 24, px * 4 };
	return g;
}

static void draw_updates(int scr, int sel) {
	UpdateInfo u;
	update_info(&u);
	int ids[4];
	int nb = update_buttons(&u, ids);
	UpdatesGeom g = updates_geom(scr, nb);
	float px = g.px;
	ui_dim(scr, 200);
	ui_fill_round(scr, g.panel, C_HEADER, px * 4);
	ui_label(scr, g.panel.x + g.panel.w / 2, g.panel.y + px * 12, px * 6,
	         u.state == UPD_AVAILABLE ? "A new version of GBA DS" : "Updates", C_TEXT);
	for (int i = 0; i < nb; ++i) {
		SDL_Rect b = g.btn[i];
		const char* label = ids[i] == UB_CHECK  ? "Check now"
		                  : ids[i] == UB_AUTO   ? (u.autoOn ? "Automatic checks: On" : "Automatic checks: Off")
		                  : ids[i] == UB_BACK   ? "Back"
		                  : ids[i] == UB_UPDATE ? "Update now"
		                  : ids[i] == UB_LATER  ? "Later"
		                  : ids[i] == UB_SKIP   ? "Skip this version"
		                                        : "Cancel";
		ui_key(scr, b, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, label, C_TEXT);
	}
	int tx = g.text.x, tw = g.text.w, y = g.text.y;
	char line[1600];
	SDL_Rect clip = { screens[scr].rect.x + g.text.x, screens[scr].rect.y + g.text.y, g.text.w, g.text.h };
	ui_clip(scr, &clip);
	switch (u.state) {
	case UPD_AVAILABLE:
#ifdef __ANDROID__
		snprintf(line, sizeof(line), "GBA DS v%s is out (this is v%s). Update now to download it (%.1f MB) and install "
		         "it: Android asks you to confirm. Your games, saves, states and settings are kept.", u.version,
		         GBADS_VERSION, u.size / 1048576.0);
#else
		snprintf(line, sizeof(line), "GBA DS v%s is out (this is v%s). Update now to download it (%.1f MB) and restart "
		         "into it; your games, saves, states and settings are kept.", u.version, GBADS_VERSION, u.size / 1048576.0);
#endif
		y = draw_wrapped(scr, tx, y, tw, px, line, C_TEXT) + px * 2;
		/* the release notes: a heading or a bullet point a line */
		for (const char* p = u.notes; *p;) {
			size_t len = strcspn(p, "\n");
			snprintf(line, sizeof(line), "%.*s", (int) len, p);
			p += len + (p[len] == '\n');
			bool bullet = !strncmp(line, "\xe2\x80\xa2", 3);
			bool heading = len && line[len - 1] == ':' && !bullet;
			y = draw_wrapped(scr, tx + (bullet ? px * 3 : 0), y + (heading ? px * 3 : px), tw - (bullet ? px * 3 : 0), px, line,
			                 heading ? C_TEXT : C_DIM);
		}
		break;
	case UPD_DOWNLOADING:
	case UPD_INSTALLING:
	case UPD_DONE: {
#ifdef __ANDROID__
		if (u.state == UPD_DONE) {
			/* Android's installer is asking; it closes GBA DS while it installs */
			snprintf(line, sizeof(line),
			         "Confirm the update to v%s in Android's installer. Android closes GBA DS while it installs; open it "
			         "again afterwards. If Android asks first, allow GBA DS to install apps.", u.version);
			draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
			break;
		}
		const char* what = u.state == UPD_DOWNLOADING ? "Downloading" : "Installing";
#else
		const char* what = u.state == UPD_DOWNLOADING ? "Downloading" : u.state == UPD_INSTALLING ? "Installing" : "Installed";
#endif
		snprintf(line, sizeof(line), u.state == UPD_DONE ? "%s v%s. Restarting..." : "%s v%s...", what, u.version);
		ui_label(scr, g.panel.x + g.panel.w / 2, g.bar.y - px * 10, px * 5, line, C_TEXT);
		int pc = u.state == UPD_DOWNLOADING ? u.percent : 100;
		ui_fill(scr, g.bar, C_ROW);
		ui_fill(scr, (SDL_Rect) { g.bar.x, g.bar.y, g.bar.w * pc / 100, g.bar.h }, C_SEL);
		if (u.state == UPD_DOWNLOADING) {
			snprintf(line, sizeof(line), "%d%%", pc);
			ui_font_text(scr, TF_BODY, g.panel.x + g.panel.w / 2, g.bar.y + px * 12, px * 4, line, C_DIM, 0);
		}
		break;
	}
	default: {
		snprintf(line, sizeof(line), "This is GBA DS v%s.", GBADS_VERSION);
		if (u.state == UPD_CHECKING) snprintf(line + strlen(line), sizeof(line) - strlen(line), " Checking for a new version...");
		else if (u.state == UPD_LATEST) snprintf(line + strlen(line), sizeof(line) - strlen(line), " It's the newest version.");
		y = draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
		if (u.state == UPD_ERROR) y = draw_wrapped(scr, tx, y + px * 3, tw, px, u.error, C_GOLD);
		if (u.lastCheck > 0) {
			time_t t = (time_t) u.lastCheck;
			char when[64];
			strftime(when, sizeof(when), "%e %B %Y, %H:%M", localtime(&t));
			snprintf(line, sizeof(line), "Last checked %s.", when[0] == ' ' ? when + 1 : when);
			y = draw_wrapped(scr, tx, y + px * 3, tw, px, line, C_DIM);
		}
		draw_wrapped(scr, tx, y + px * 3, tw, px,
		             "With automatic checks on, GBA DS looks for a new version once a day when the game list opens, and asks "
		             "before downloading anything. Updates come from the releases on github.com/kyannon/gba-ds.",
		             C_DIM);
	}
	}
	ui_clip(scr, NULL);
}

/* One step of the Updates panel; false once it's closed. */
static bool updates_input(int ls) {
	UpdateInfo u;
	update_info(&u);
	int ids[4];
	int nb = update_buttons(&u, ids);
	if (updatesSel >= nb) updatesSel = nb ? nb - 1 : 0;
	int act = -1;
	if (nb) {
		if (btn_pressed(B_DOWN)) updatesSel = (updatesSel + 1) % nb;
		if (btn_pressed(B_UP)) updatesSel = (updatesSel + nb - 1) % nb;
		if (btn_pressed(B_A) || btn_pressed(B_START)) act = ids[updatesSel];
	}
	if (btn_pressed(B_B) || btn_released(B_HOME)) {
		act = u.state == UPD_DOWNLOADING ? UB_CANCEL : u.state == UPD_AVAILABLE ? UB_LATER
		    : u.state == UPD_INSTALLING ? -1
#ifndef __ANDROID__
		    : u.state == UPD_DONE ? -1
#endif
		    : UB_BACK;
	}
	Pointer p;
	while (ui_next_pointer(&p)) {
		if (p.screen != ls || p.kind != 1) continue;
		UpdatesGeom g = updates_geom(ls, nb);
		SDL_Point pt = { p.x, p.y };
		for (int i = 0; i < nb; ++i) {
			if (SDL_PointInRect(&pt, &g.btn[i])) updatesSel = i, act = ids[i];
		}
	}
	switch (act) {
	case UB_CHECK: update_check_now(); updatesSel = 0; break;
	case UB_AUTO: update_set_auto(!u.autoOn); break;
	case UB_UPDATE: update_install(); updatesSel = 0; break;
	case UB_SKIP: update_skip(); return false;
	case UB_CANCEL: update_cancel(); updatesSel = 0; break;
	case UB_LATER:
	case UB_BACK: return false;
	default: break;
	}
	return true;
}
#endif

/* "Preparing games" over a dimmed screen, with a progress bar. */
/* Starting a game whose logo (or map border) isn't ready yet: only its
 * own are made first (and the one already under way finished), the rest
 * wait until the game list is back. */
static void draw_wait(int scr, const char* name) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_dim(scr, 235);
	int cy = r.h / 2;
	ui_label(scr, r.w / 2, cy - px * 14, px * 6, "Preparing the game...", C_TEXT);
	char line[200];
	fit_text(line, sizeof(line), TF_BODY, px * 4, name, r.w - px * 16);
	ui_font_text(scr, TF_BODY, r.w / 2, cy + px * 2, px * 4, line, C_DIM, 0);
	ui_font_text(scr, TF_BODY, r.w / 2, cy + px * 14, px * 4, "First run only. B: back", C_DIM, 0);
}

/* The logos (and map borders) still to make, in the order they'll be seen:
 * the selected game's first, then the others on screen, top to bottom */
static void prioritise(int scr, int sel, int scroll) {
	ListGeom g = list_geom(scr);
	SDL_Rect r = screens[scr].rect;
	for (int i = nGames - 1; i >= 0; --i) {
		SDL_Rect t = tile_rect(&g, i, scroll);
		if (i != sel && t.y + t.h > g.headerH && t.y < r.h) logo_front(games[i].code, false);
	}
	if (sel >= 0 && sel < nGames) logo_front(games[sel].code, false);
}

static int clamp_scroll(int scr, int scroll) {
	ListGeom g = list_geom(scr);
	int rows = (nGames + g.cols - 1) / g.cols;
	int max = g.m * 2 + rows * g.rowH - g.gap - g.listH;
	if (max < 0) max = 0;
	return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

static int keep_visible(int scr, int sel, int scroll) {
	ListGeom g = list_geom(scr);
	int top = (sel / g.cols) * g.rowH;
	if (top < scroll) scroll = top;
	if (top + g.tileH + g.m * 2 > scroll + g.listH) scroll = top + g.tileH + g.m * 2 - g.listH;
	return clamp_scroll(scr, scroll);
}

bool library_run(Config* cfg, char* outPath, size_t outSize) {
	storage_check(cfg);

	/* the last game played (reselected) */
	char lastFile[1100], last[1024] = "";
	snprintf(lastFile, sizeof(lastFile), "%s/last_game.txt", cfg->stateDir);
	FILE* f = fopen(lastFile, "r");
	if (f) {
		if (fgets(last, sizeof(last), f)) last[strcspn(last, "\n")] = 0;
		fclose(f);
	}

	/* The whole library is read at start, and again when a ROM folder has
	 * changed; back from a game with the folders as they were, the list is
	 * kept and only the game just played is looked at again (its patches
	 * can be switched in the game: GEN-9). Issue #49: a big library took a
	 * while each time. */
	if (library_changed(cfg)) {
		scan(cfg);
	} else {
		for (int i = 0; i < nGames; ++i) {
			struct stat st;
			if (strcmp(games[i].path, last) || stat(games[i].path, &st) != 0) continue;
			const char* file = strrchr(games[i].path, '/');
			entry_patches(cfg, &games[i], file ? file + 1 : games[i].path, (long long) st.st_size, (long long) st.st_mtime);
		}
		fprintf(stderr, "library: %d game(s), folders unchanged\n", nGames);
	}
	thumbFor = -2;

	int sel = nGames ? 0 : -1;
	for (int i = 0; i < nGames; ++i) {
		if (!strcmp(games[i].path, last)) sel = i;
	}
	listMode = strcmp(cfg->libraryView, "grid") != 0;
	int ls = ui_touch_screen();
	int scroll = sel > 0 ? keep_visible(ls, sel, 0) : 0;
	bool dragging = false, pressed = false;
	int pressY = 0, pressScroll = 0;
	int chosen = -1;
	int waiting = -1; /* chosen, but its logo or map border isn't made yet */
	int prioSel = -2, prioScroll = -1; /* the queue last ordered for these */
	logo_pause(false); /* (back from a game: the rest of the logos) */
	bool quit = false;
	int repeat = 0;
	bool settingsOpen = false;
	int settingsSel = 0;
	settingsMsg[0] = 0;
	updatesOpen = false;
	update_auto_check(); /* once a day, in the background */
#ifdef GBADS_UPDATES
	int restartIn = -1; /* frames until the restart into an installed update */
#endif

#ifdef __wasi__
	int listFrames = 0;
#endif
	while (!quit && chosen < 0) {
#ifdef __wasi__
		if (listFrames++) logo_step(); /* (the web build: a logo a frame, from the list's second) */
#endif
		{
			static unsigned crcSeen;
			unsigned cg = rom_crc_generation();
			if (cg != crcSeen) { /* a ROM's checksum worked out (GEN-8) */
				crcSeen = cg;
				rom_crc_save(cfg);
			}
		}
		if (logo_generation() != logoGen) { /* a logo finished extracting */
			logoGen = logo_generation();
			thumbFor = -2;
			free_tiles();
		}
		if (!ui_events()) {
			quit = true;
			break;
		}
		ui_tick();
		ui_read_buttons();
		ra_idle(); /* sign-in answers (at start-up, or from Settings) */
		ls = ui_touch_screen();
		ListGeom g = list_geom(ls);

		/* The first time games are listed, their logos and map borders are
		 * pulled out of the ROMs in the background, the ones on screen
		 * first. A game started before its own are ready waits for them
		 * (and for the one under way, so the game doesn't compete with it
		 * for the CPU: that made the audio crackle); the rest wait for the
		 * game list (logo_pause). */
		if (sel != prioSel || scroll != prioScroll) {
			prioritise(ls, sel, scroll);
			prioSel = sel, prioScroll = scroll;
		}
		if (waiting >= 0) {
			if (!logo_busy(games[waiting].code) && !logo_busy(NULL)) {
				chosen = waiting;
				break;
			}
			Pointer p;
			while (ui_next_pointer(&p)) {
			}
			if (btn_pressed(B_B) || btn_released(B_HOME)) waiting = -1, logo_pause(false);
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			for (int s = 0; s < nScreens && waiting >= 0; ++s) draw_wait(s, games[waiting].name);
			ui_present();
			SDL_Delay(15);
			continue;
		}

#ifdef GBADS_UPDATES
		/* an automatic check found a new version: ask (on the game list) */
		if (!updatesOpen && !settingsOpen && update_notice_pending()) {
			update_notice_seen();
			updatesOpen = true, updatesNotice = true, updatesSel = 0;
		}
		if (updatesOpen) {
			if (!updates_input(ls)) updatesOpen = false;
			UpdateInfo u;
			update_info(&u);
#ifndef __ANDROID__ /* (Android: its installer replaces the app) */
			if (u.state == UPD_DONE && restartIn < 0) restartIn = 90;
#endif
			if (restartIn > 0 && --restartIn == 0) {
				quit = true; /* main() starts the new version */
				break;
			}
			pressed = false;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (settingsOpen && updatesOpen) draw_settings(cfg, ls, settingsSel);
			if (updatesOpen) draw_updates(ls, updatesSel);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			ui_present();
			SDL_Delay(15);
			continue;
		}
#endif
#ifdef __ANDROID__
		if (foldersOpen) {
			if (settings_poll(cfg)) {
				/* a new ROM folder: list it */
				free_thumbs();
				free_tiles();
				scan(cfg);
				thumbFor = -2;
				sel = nGames ? 0 : -1;
				scroll = 0;
				snprintf(settingsMsg, sizeof(settingsMsg), "%d game%s found.", nGames, nGames == 1 ? "" : "s");
			}
			if (!pickPending && !folders_input(cfg, ls)) foldersOpen = false;
			pressed = false;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			draw_settings(cfg, ls, settingsSel);
			if (foldersOpen) draw_folders(cfg, ls, foldersSel);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			ui_present();
			SDL_Delay(15);
			continue;
		}
#endif
		if (settingsOpen && setPage != SP_SYSTEM) {
			if (!settings_pages_input(cfg, ls)) {
				settingsOpen = false;
				if (setRescan) {
					setRescan = false;
					free_thumbs();
					free_tiles();
					scan(cfg);
					thumbFor = -2;
					sel = nGames ? 0 : -1;
					scroll = 0;
				}
			}
			pressed = false;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (settingsOpen && setPage == SP_HOME) draw_set_home(ls, setSel);
			else if (settingsOpen && setPage < SP_SYSTEM) draw_set_page(cfg, ls, setPage, setSel);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			ui_present();
			SDL_Delay(15);
			continue;
		}
		if (settingsOpen) {
			if (settings_poll(cfg)) {
				/* a new ROM folder: list it */
				free_thumbs();
				free_tiles();
				scan(cfg);
				thumbFor = -2;
				sel = nGames ? 0 : -1;
				scroll = 0;
				snprintf(settingsMsg, sizeof(settingsMsg), "%d game%s found.", nGames, nGames == 1 ? "" : "s");
			}
			if (btn_pressed(B_DOWN)) settingsSel = (settingsSel + 1) % (nRows + 1);
			if (btn_pressed(B_UP)) settingsSel = (settingsSel + nRows) % (nRows + 1);
			if (btn_pressed(B_A) || btn_pressed(B_START)) {
				if (settingsSel < nRows) settings_activate(cfg, settingsSel);
				else setPage = SP_HOME, setSel = SP_SYSTEM;
			}
			if (btn_pressed(B_B) || btn_pressed(B_SELECT) || btn_released(B_HOME)) setPage = SP_HOME, setSel = SP_SYSTEM;
			Pointer p;
			while (ui_next_pointer(&p)) {
				if (p.screen != ls || p.kind != 1) continue;
				SettingsGeom sg = settings_geom(ls);
				SDL_Point pt = { p.x, p.y };
				int hit = -1;
				for (int i = 0; i < nRows; ++i) {
					if (SDL_PointInRect(&pt, &sg.row[i])) hit = i;
				}
				if (hit >= 0) settingsSel = hit, settings_activate(cfg, hit);
				else if (SDL_PointInRect(&pt, &sg.back) || !SDL_PointInRect(&pt, &sg.panel)) settingsSel = nRows, setPage = SP_HOME, setSel = SP_SYSTEM;
			}
			pressed = false;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (settingsOpen) draw_settings(cfg, ls, settingsSel);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			ui_present();
			SDL_Delay(15);
			continue;
		}
		if (btn_pressed(B_X)) {
			toggle_view(cfg);
			scroll = sel >= 0 ? keep_visible(ls, sel, 0) : 0;
			continue;
		}
		if (btn_pressed(B_SELECT)) {
			settingsOpen = true;
			setPage = SP_HOME, setSel = 0;
			settingsSel = 0;
			settingsMsg[0] = 0;
			continue;
		}

		if (nGames) {
			/* up/down: a row, left/right: a column, L/R: a screenful */
			int dir = 0;
			if (btn_pressed(B_DOWN)) dir = g.cols, repeat = 0;
			if (btn_pressed(B_UP)) dir = -g.cols, repeat = 0;
			if (btn_pressed(B_RIGHT)) dir = 1, repeat = 0;
			if (btn_pressed(B_LEFT)) dir = -1, repeat = 0;
			bool held = btnNow[B_DOWN] || btnNow[B_UP] || btnNow[B_LEFT] || btnNow[B_RIGHT];
			if (held && ++repeat > 18 && repeat % 6 == 0) {
				dir = btnNow[B_DOWN] ? g.cols : btnNow[B_UP] ? -g.cols : btnNow[B_RIGHT] ? 1 : -1;
			}
			if (btn_pressed(B_R)) dir = g.cols * g.visibleRows;
			if (btn_pressed(B_L)) dir = -g.cols * g.visibleRows;
			if (dir) {
				sel += dir;
				if (sel < 0) sel = 0;
				if (sel >= nGames) sel = nGames - 1;
				scroll = keep_visible(ls, sel, scroll);
			}
			if (btn_pressed(B_A) || btn_pressed(B_START)) chosen = sel;
		}
		if (btn_pressed(B_B) || btn_released(B_HOME)) quit = true;

		Pointer p;
		while (ui_next_pointer(&p)) {
			if (p.screen != ls) continue;
			if (p.kind == 1) {
				SDL_Rect sb = settings_button(ls), vb = view_button(ls), tb = theme_button(ls), gb = games_button(ls);
				SDL_Point pt = { p.x, p.y };
				if (SDL_PointInRect(&pt, &gb)) {
					/* Supported / All: kept for all games (show_all), the list made again */
					cfg->showAll = !cfg->showAll;
					setting_changed(cfg, config_setting("show_all"));
					setRescan = false;
					free_thumbs();
					free_tiles();
					scan(cfg);
					thumbFor = -2;
					sel = nGames ? 0 : -1;
					scroll = 0;
					pressed = false;
					break;
				}
				if (SDL_PointInRect(&pt, &tb)) {
					toggle_theme(cfg);
					pressed = false;
					break;
				}
				if (SDL_PointInRect(&pt, &vb)) {
					toggle_view(cfg);
					scroll = sel >= 0 ? keep_visible(ls, sel, 0) : 0;
					pressed = false;
					break;
				}
				if (SDL_PointInRect(&pt, &sb)) {
					settingsOpen = true;
					setPage = SP_HOME, setSel = 0;
					settingsSel = 0;
					settingsMsg[0] = 0;
					pressed = false;
					break;
				}
				pressed = true;
				dragging = false;
				pressY = p.y;
				pressScroll = scroll;
			} else if (p.kind == 3 && pressed) {
				if (abs(p.y - pressY) > g.tileH / 6) dragging = true;
				if (dragging) scroll = clamp_scroll(ls, pressScroll - (p.y - pressY));
			} else if (p.kind == 2 && pressed) {
				pressed = false;
				if (dragging) continue;
				if (p.y >= g.headerH) {
					for (int i = 0; i < nGames; ++i) {
						SDL_Rect t = tile_rect(&g, i, scroll);
						if (p.x >= t.x && p.x < t.x + t.w && p.y >= t.y && p.y < t.y + t.h) {
							sel = i;
							chosen = i;
						}
					}
				}
			}
		}

		if (chosen >= 0) {
			/* (its own jobs first, and nothing else started meanwhile) */
			logo_pause(true);
			logo_front(games[chosen].code, true);
			if (logo_busy(games[chosen].code) || logo_busy(NULL)) waiting = chosen, chosen = -1;
		}

		ui_clear();
		draw_list(cfg, ls, sel, scroll);
		if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
		ui_present();
		SDL_Delay(15);
	}
	free_thumbs();
	free_brand();
	free_tiles();
	if (chosen < 0) return false;
	snprintf(outPath, outSize, "%s", games[chosen].path);
	f = fopen(lastFile, "w");
	if (f) {
		fprintf(f, "%s\n", outPath);
		fclose(f);
	}
	return true;
}

#include "library.h"
#include "android.h"
#include "paths.h"
#include "profiles.h"
#include "patch.h"
#include "i18n.h"
#include "ui.h"
#include "logo.h"
#include "brand.h"
#include "emu.h"
#include "esde.h"
#include "esinteg.h"
#include "hotkeys.h"
#include "folderpick.h"
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
#include <math.h>
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
	bool fav;            /* a favorite: listed first, whatever the sort */
	long long played;    /* when it was last started (seconds), 0: never */
} GameEntry;

static GameEntry* games;
static int nGames, capGames;

/* An array grown to hold n + 1 elements (doubling from `first`): the array,
 * or NULL (the old one, still there, left as it was) when there's no memory. */
static void* grow(void* p, int* cap, int n, size_t elem, int first) {
	if (n < *cap) return p;
	int c = *cap ? *cap * 2 : first;
	void* q = realloc(p, elem * (size_t) c);
	if (q) *cap = c;
	return q;
}

/* (the accent's colours: Settings > Display > Color) */
#define C_BG ui_accent_color(AC_BG)
#define C_HEADER ui_accent_color(AC_HEADER)
#define C_ROW ui_accent_color(AC_ROW)
#define C_SEL ui_accent_color(AC_BTN_SEL)
static const SDL_Color C_TEXT = { 236, 236, 244, 255 };
static const SDL_Color C_DIM = { 140, 140, 172, 255 };
#define C_GOLD ui_accent_color(AC_HI)
static const SDL_Color C_TAG = { 52, 120, 96, 255 };

/* Favorites and when each game was last played, kept by ROM path in
 * <state_dir>/favorites.txt (one path a line) and played.txt ("seconds path"
 * a line). They stay when a game isn't listed for a while (Supported / All,
 * a folder that's away). The list's order: favorites first, then A to Z or
 * the most recently played first (the ones never played after them, A to Z),
 * by library_sort. */
typedef struct {
	char path[1024];
	bool fav;
	long long played;
} Mark;
static Mark* marks;
static int nMarks, capMarks;
static bool marksLoaded;
static int sortMode; /* 0: A to Z, 1: recently played */

static Mark* mark_find(const char* path, bool create) {
	for (int i = 0; i < nMarks; ++i)
		if (!strcmp(marks[i].path, path)) return &marks[i];
	if (!create) return NULL;
	Mark* grown = grow(marks, &capMarks, nMarks, sizeof(*marks), 64);
	if (!grown) {
		static Mark spare; /* (no memory: the mark isn't kept) */
		memset(&spare, 0, sizeof(spare));
		return &spare;
	}
	marks = grown;
	Mark* m = &marks[nMarks++];
	memset(m, 0, sizeof(*m));
	snprintf(m->path, sizeof(m->path), "%s", path);
	return m;
}

static void marks_load(const Config* cfg) {
	if (marksLoaded) return;
	marksLoaded = true;
	char file[1100], line[1100];
	snprintf(file, sizeof(file), "%s/favorites.txt", cfg->stateDir);
	FILE* f = fopen(file, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			if (line[0]) mark_find(line, true)->fav = true;
		}
		fclose(f);
	}
	snprintf(file, sizeof(file), "%s/played.txt", cfg->stateDir);
	f = fopen(file, "r");
	if (f) {
		while (fgets(line, sizeof(line), f)) {
			line[strcspn(line, "\r\n")] = 0;
			char* sp = strchr(line, ' ');
			if (!sp || !sp[1]) continue;
			*sp = 0;
			mark_find(sp + 1, true)->played = atoll(line);
		}
		fclose(f);
	}
}

static void marks_save(const Config* cfg) {
	char file[1100];
	snprintf(file, sizeof(file), "%s/favorites.txt", cfg->stateDir);
	FILE* f = fopen(file, "w");
	if (f) {
		for (int i = 0; i < nMarks; ++i)
			if (marks[i].fav) fprintf(f, "%s\n", marks[i].path);
		fclose(f);
	}
	snprintf(file, sizeof(file), "%s/played.txt", cfg->stateDir);
	f = fopen(file, "w");
	if (f) {
		for (int i = 0; i < nMarks; ++i)
			if (marks[i].played > 0) fprintf(f, "%lld %s\n", marks[i].played, marks[i].path);
		fclose(f);
	}
}

static int cmp_games(const void* a, const void* b) {
	const GameEntry *x = a, *y = b;
	if (x->fav != y->fav) return x->fav ? -1 : 1;
	if (sortMode == 1 && x->played != y->played) return x->played > y->played ? -1 : 1;
	int c = strcasecmp(x->name, y->name);
	return c ? c : strcmp(x->path, y->path);
}

static void sort_games(const Config* cfg) {
	marks_load(cfg);
	for (int i = 0; i < nGames; ++i) {
		const Mark* m = mark_find(games[i].path, false);
		games[i].fav = m && m->fav;
		games[i].played = m ? m->played : 0;
	}
	qsort(games, nGames, sizeof(*games), cmp_games);
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

/* (no memory for it: not remembered, read again next time) */
static void code_cache_add(const CodeCache* c) {
	CodeCache* grown = grow(codeCache, &capCodeCache, nCodeCache, sizeof(*codeCache), 256);
	if (!grown) return;
	codeCache = grown;
	if ((nCodeCache + 1) * 2 > nCodeIndex) {
		/* (the index made again, twice as big, before anything is let go) */
		int n = nCodeIndex ? nCodeIndex * 2 : 512;
		while ((nCodeCache + 1) * 2 > n) n *= 2;
		int* index = malloc(sizeof(*index) * (size_t) n);
		if (!index) return;
		free(codeIndex);
		codeIndex = index, nCodeIndex = n;
		codeCache[nCodeCache++] = *c;
		for (int i = 0; i < nCodeIndex; ++i) codeIndex[i] = -1;
		for (int i = 0; i < nCodeCache; ++i) code_index_put(i);
	} else {
		codeCache[nCodeCache++] = *c;
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
	/* ("____", no code in its header, is looked at again if it's the size of a dump known by its
	 * CRC32 (profile_codeless_size): a newer build may know it. Other sizes are kept.) */
	if (c && c->size == (long long) st->st_size && c->mtime == (long long) st->st_mtime &&
	    (strcmp(c->code, "____") || !profile_codeless_size(st->st_size))) {
		c->used = true;
		memcpy(code, c->code, 5);
		return true;
	}
	if (!emu_rom_file_code(path, code)) {
		if (emu_is_archive_name(path)) fprintf(stderr, "library: no GBA or Game Boy ROM in %s\n", path);
		return false;
	}
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
		GameEntry* grown = grow(games, &capGames, nGames, sizeof(*games), 64);
		if (!grown) continue; /* (no memory: not listed) */
		games = grown;
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
	if (c && strchr("PSIHXYZU", c)) return BRAND_FLAG_EU; /* (D and F: above) */
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
static int nRomCrcs, capRomCrcs;
static bool romCrcsDirty;
static pthread_mutex_t crcLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t crcWake = PTHREAD_COND_INITIALIZER;
static RomCrc crcWant; /* the next file to read (path "": none) */
static char crcBusy[1024]; /* the file the worker is reading (crcLock) */
static PatchChoice crcWantChoice; /* and the patches switched on for it (GEN-9): the worker finds their files, not the list's thread */
static char crcWantCode[16];
static const Config* crcCfg;       /* (the settings, for the patches' folder: set by scan) */
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
		RomCrc* grown = grow(romCrcs, &capRomCrcs, nRomCrcs, sizeof(*romCrcs), 64);
		if (!grown) return; /* (no memory: worked out again next time) */
		romCrcs = grown;
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
		snprintf(crcBusy, sizeof(crcBusy), "%s", c.path);
		PatchChoice choice = crcWantChoice;
		char code[16];
		snprintf(code, sizeof(code), "%s", crcWantCode);
		crcWant.path[0] = 0;
		pthread_mutex_unlock(&crcLock);
		/* (the patch files beside the ROM and in its folder: the list's own thread waited
		 * on these stats behind the other reads of a busy SD card) */
		char files[PATCH_ON_MAX + 1][1024];
		int nf = crcCfg ? patch_plan(crcCfg, c.path, code, &choice, files, PATCH_ON_MAX + 1) : 0;
		c.crc = rom_crc_read(c.path, files, nf, &c.patch);
		pthread_mutex_lock(&crcLock);
		crcBusy[0] = 0;
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
			RomCrc* grown = grow(romCrcs, &capRomCrcs, nRomCrcs, sizeof(*romCrcs), 64);
			if (!grown) break;
			romCrcs = grown;
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
 * games and Game Boy / Game Boy Color games, GB-14: the others have no list of
 * dumps to check it against). */
static bool game_crc(const GameEntry* g, uint32_t* crc, int* patch) {
	if (!g->supported && !profile_code_is_gb(g->code)) return false;
	pthread_mutex_lock(&crcLock);
	RomCrc* c = rom_crc_find(g->path, g->size, g->mtime);
	bool have = c != NULL;
	if (have) {
		*crc = c->crc;
		*patch = c->patch;
	}
	else if (strcmp(crcWant.path, g->path) && strcmp(crcBusy, g->path)) { /* (not asked for, and not being read right now) */
		crcWant.size = g->size;
		crcWant.mtime = g->mtime;
		snprintf(crcWant.path, sizeof(crcWant.path), "%s", g->path);
		crcWantChoice = g->patchOn;
		snprintf(crcWantCode, sizeof(crcWantCode), "%s", g->code);
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
			char wf[PATCH_ON_MAX + 1][1024];
			int wn = crcCfg ? patch_plan(crcCfg, w.path, crcWantCode, &crcWantChoice, wf, PATCH_ON_MAX + 1) : 0;
			w.crc = rom_crc_read(w.path, wf, wn, &w.patch);
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

/* The game's ROM checksum if it's already known, without asking for it (0: not yet) */
static uint32_t known_crc(const GameEntry* g) {
	pthread_mutex_lock(&crcLock);
	RomCrc* c = rom_crc_find(g->path, g->size, g->mtime);
	uint32_t crc = c ? c->crc : 0;
	pthread_mutex_unlock(&crcLock);
	return crc;
}

/* The known mod the game is, or 0: its logo is its own (GEN-20) */
static uint32_t logo_mod_of(const GameEntry* g) {
	uint32_t crc = known_crc(g);
	return profile_known_mod(g->code, crc) ? crc : 0;
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
	{
		char files[80], games[80]; /* (two counts, each with its own plural; the language's own joiner puts them together) */
		snprintf(files, sizeof(files), trn("%d file checked", "%d files checked", scanFiles), scanFiles);
		snprintf(games, sizeof(games), trn("%d game found", "%d games found", nGames), nGames);
		snprintf(line, sizeof(line), tr("%s, %s"), files, games);
	}
		ui_font_text(scr, TF_BODY, r.w / 2, r.h / 2 + px * 8, px * 4, line, C_DIM, 0);
	}
	ui_present();
}

/* A library folder named gba has its Game Boy and Game Boy Color folders beside it (gb, gbc: the
 * way EmulationStation, Banana's list and Android's ROM folders lay them out), read the same way
 * (issue #77), in the folder's own letter case (GBA / Gba / gba). Not there: noted as missing, so
 * the list is made again if one turns up. */
static void scan_siblings(Config* cfg, const char* d) {
	char base[1024];
	snprintf(base, sizeof(base), "%s", d);
	size_t n = strlen(base);
	while (n > 1 && base[n - 1] == '/') base[--n] = 0;
	char* slash = strrchr(base, '/');
	char* name = slash ? slash + 1 : base;
	if (strcasecmp(name, "gba")) return;
	bool upper = isupper((unsigned char) name[0]) && isupper((unsigned char) name[1]) && isupper((unsigned char) name[2]);
	bool first = isupper((unsigned char) name[0]);
	static const char* const sib[] = { "gb", "gbc" };
	for (int i = 0; i < 2; ++i) {
		char path[1024], leaf[8];
		snprintf(leaf, sizeof(leaf), "%s", sib[i]);
		for (char* c = leaf; *c; ++c) *c = (char) (upper || (first && c == leaf) ? toupper((unsigned char) *c) : *c);
		snprintf(path, sizeof(path), "%.*s%s", (int) (name - base), base, leaf);
		struct stat st;
		if (stat(path, &st) == 0 && S_ISDIR(st.st_mode)) scan_dir(cfg, path, 0);
		else dir_note(path, NULL);
	}
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
	for (char* d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) {
		scan_dir(cfg, d, 0);
		scan_siblings(cfg, d);
	}
	code_cache_save(cfg);
	sort_games(cfg);
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
	for (int i = 0; i < nGames; ++i) ui_logo_queue(cfg, games[i].path, games[i].code, known_crc(&games[i]));
	scanned = true;
	snprintf(scannedRomDir, sizeof(scannedRomDir), "%s", cfg->romDir);
	scannedShowAll = cfg->showAll;
	scannedNameTags = cfg->nameTags;
	fprintf(stderr, "library: %d game(s), %d file(s) in %d folder(s), %llu ms\n", nGames, scanFiles, nSeenDirs,
	        (unsigned long long) (SDL_GetTicks() - scanStart));
}

/* -------------------------------------------------------------- preview */

static SDL_Texture* thumbTex[2]; /* resume screenshot, per view */
static int thumbW = GBA_W, thumbH = GBA_H; /* its size (a Game Boy game's is its own, GB-13) */
static int thumbFor = -2;
static uint32_t thumbMod; /* the known mod its logo was loaded for (GEN-20) */
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
	thumbMod = logo_mod_of(&games[idx]);
	if (ui_load_logo(cfg, games[idx].path, games[idx].code, known_crc(&games[idx]), logoTex, &logoW, &logoH,
	                 &logoPixel)) {
		/* drawn at a fixed height rather than a whole-number scale, so filter it */
		for (int v = 0; v < 2; ++v) {
			if (logoTex[v]) SDL_SetTextureScaleMode(logoTex[v], SDL_ScaleModeLinear);
		}
	}
	SDL_Surface* s = SDL_LoadBMP(gp.thumb);
	if (s) {
		thumbW = s->w, thumbH = s->h;
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
	{ /* (a word wider than its slot, as in German, makes the slot wider) */
		int ww = SDL_max(wordW, ui_label_width(scr, cap, "Play"));
		ui_label(scr, bx + ww / 2, cy, cap, "Play", C_TEXT);
		bx += ww + px * 8;
	}
	bx += draw_brand(scr, BRAND_BTN_B, bx, gy, d) + px * 2;
	ui_label(scr, bx + SDL_max(wordW, ui_label_width(scr, cap, "Quit")) / 2, cy, cap, "Quit", C_TEXT);

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
		const char* from = patch == PATCH_APPLIED ? N_(" (patch)"): "";
		char line[400] = "";
		if (patch == PATCH_PARTIAL) {
			snprintf(line, sizeof(line), tr("Patched, but a patch was left out: see Patches in the game's menu"));
		} else if (patch == PATCH_WRONG_ROM) {
			snprintf(line, sizeof(line), tr("Patch not applied: %s is for another ROM"), g->patch);
		} else if (patch == PATCH_UNREADABLE) {
			snprintf(line, sizeof(line), tr("Patch not applied: %s can't be read"), g->patch);
		} else if (profile_code_is_gb(g->code)) {
			/* (a Game Boy game: no second screen to lose, GB-14) */
			if (!profile_gb_dump_known(crc)) snprintf(line, sizeof(line), "%s", patch == PATCH_APPLIED ? tr("Patched ROM: not a known dump") : tr("Modified ROM: not a known dump"));
		} else {
			switch (profile_dump(g->code, crc, &name)) {
			case DUMP_MOD_TESTED: snprintf(line, sizeof(line), tr("Romhack: %s%s"), name, from); break;
			case DUMP_MOD: snprintf(line, sizeof(line), tr("Romhack: %s%s, untested"), name, from); break;
			case DUMP_MODIFIED:
				if (patch == PATCH_APPLIED) snprintf(line, sizeof(line), tr("Patched: the second screen may not work"));
				else snprintf(line, sizeof(line), tr("Modified ROM: the second screen may not work"));
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
		ui_image(scr, thumbTex, thumbW, thumbH, "fit", &area);
	} else {
		ui_label(scr, r.w / 2, area.y + area.h / 2, px * 7, "no save state yet!", C_TEXT);
	}
}

/* -------------------------------------------------------------- list: a grid of logos */

#define C_EDGE ui_accent_color(AC_EDGE)

typedef struct {
	int headerH, listH, m, gap, tileW, tileH, rowH, cols, visibleRows;
	int barH; /* the sort bar above the first row: part of the list, it scrolls away (0 with no games) */
} ListGeom;

/* The game list as names (default) or a grid of logos, two or three columns
 * wide: library_view ("list", "grid", "grid3") in <state_dir>/library.cfg,
 * switched by the header button or X (list, two columns, three, list...). */
static int viewCols = 1; /* 1: the list; 2 or 3: that many columns of logos */
#define listMode (viewCols == 1)

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
	} else if (viewCols == 2) { /* two columns of logos */
		g.gap = px * 4;
		g.cols = 2;
		g.tileH = px * 40;
	} else { /* three columns: the same tile's shape, narrower */
		g.gap = px * 4;
		g.cols = 3;
		g.tileH = px * 28;
	}
	g.tileW = (r.w - g.m * 2 - g.gap * (g.cols - 1)) / g.cols;
	g.rowH = g.tileH + g.gap;
	g.barH = nGames ? px * 18 : 0;
	g.visibleRows = SDL_max(1, (g.listH - g.m) / g.rowH);
	return g;
}

static SDL_Rect tile_rect(ListGeom* g, int i, int scroll) {
	int row = i / g->cols, col = i % g->cols;
	return (SDL_Rect) { g->m + col * (g->tileW + g->gap), g->headerH + g->m + g->barH + row * g->rowH - scroll, g->tileW, g->tileH };
}

/* each game's logo, loaded on first sight; reloaded when a new one is extracted */
static SDL_Texture* tileLogo[256][2];
static int tileLogoW[256], tileLogoH[256];
static bool tileTried[256];
static uint32_t tileMod[256]; /* the known mod each was loaded for (GEN-20) */

static void free_tiles(void) {
	for (int i = 0; i < 256; ++i) {
		ui_free_image(tileLogo[i]);
		tileTried[i] = false;
	}
}

/* (a ROM file's checksum was worked out: only what has no logo yet needs asking again) */
static void retry_tiles_without_logo(void) {
	for (int i = 0; i < 256; ++i)
		if (!tileLogo[i][0] && !tileLogo[i][1]) tileTried[i] = false;
	if (!logoTex[0] && !logoTex[1]) thumbFor = -2;
}

/* (a ROM's checksum was worked out, GEN-8: a game now known to be a known
 * mod has a logo of its own, GEN-20) */
static void retry_logos_of_mods(void) {
	for (int i = 0; i < nGames && i < 256; ++i) {
		if (!tileTried[i] || tileMod[i] == logo_mod_of(&games[i])) continue;
		ui_free_image(tileLogo[i]);
		tileTried[i] = false;
	}
	if (thumbFor >= 0 && thumbFor < nGames && thumbMod != logo_mod_of(&games[thumbFor])) thumbFor = -2;
}

/* The sort bar: the top row of the list itself, not of the header, so it
 * scrolls away with the games. Two buttons (A to Z, Recently played) and a
 * hint for the favorites' star. */
static const char* const sortNames[2] = { N_("A to Z"), N_("Recently played") };
static bool barFocus; /* the D-pad is on the bar (Up from the first game) */
static int barSel;    /* and on this button */

static SDL_Rect sort_button(int scr, const ListGeom* g, int scroll, int which) {
	float px = ui_px(scr);
	int h = px * 13, x = g->m, w0 = px * 34, w1 = px * 62, gap = px * 3;
	int y = g->headerH + g->m + (int) (px * 2) - scroll; /* (a little below the header, so no key's top edge is lost) */
	return which == 0 ? (SDL_Rect) { x, y, w0, h } : (SDL_Rect) { x + w0 + gap, y, w1, h };
}

/* A filled five-pointed star, by scanlines (no polygon call to lean on). */
static void draw_star(int scr, int cx, int cy, int r, SDL_Color c) {
	float vx[10], vy[10];
	for (int k = 0; k < 10; ++k) {
		float a = -3.14159265f / 2 + k * 3.14159265f / 5, rr = (k & 1) ? r * 0.42f : (float) r;
		vx[k] = cx + cosf(a) * rr;
		vy[k] = cy + sinf(a) * rr + r * 0.06f;
	}
	for (int y = cy - r; y <= cy + r; ++y) {
		float yy = y + 0.5f, xs[10];
		int n = 0;
		for (int k = 0; k < 10; ++k) {
			int j = (k + 1) % 10;
			float y0 = vy[k], y1 = vy[j];
			if ((y0 <= yy && y1 > yy) || (y1 <= yy && y0 > yy)) xs[n++] = vx[k] + (yy - y0) / (y1 - y0) * (vx[j] - vx[k]);
		}
		for (int a = 1; a < n; ++a)
			for (int b = a; b > 0 && xs[b] < xs[b - 1]; --b) {
				float t = xs[b];
				xs[b] = xs[b - 1], xs[b - 1] = t;
			}
		for (int a = 0; a + 1 < n; a += 2) ui_fill(scr, (SDL_Rect) { (int) (xs[a] + 0.5f), y, SDL_max(1, (int) (xs[a + 1] - xs[a] + 0.5f)), 1 }, c);
	}
}

/* where a tile's star is touched: its right end in the list, its corner in the grids */
static SDL_Rect star_rect(int scr, const SDL_Rect* t) {
	float px = ui_px(scr);
	if (viewCols == 1) return (SDL_Rect) { t->x + t->w - (int) (px * 14), t->y, (int) (px * 14), t->h };
	return (SDL_Rect) { t->x, t->y, (int) (px * 14), (int) (px * 14) };
}

#define STAR_GOLD ((SDL_Color) { 255, 200, 64, 255 })
static void draw_star_mark(int scr, const SDL_Rect* t, bool fav) {
	SDL_Rect s = star_rect(scr, t);
	float px = ui_px(scr);
	SDL_Color c = fav ? STAR_GOLD : C_DIM;
	draw_star(scr, s.x + s.w / 2, s.y + s.h / 2, (int) (px * (viewCols == 3 ? 3.0f : 3.6f)), c);
}

static void draw_sort_bar(int scr, const ListGeom* g, int scroll) {
	if (!g->barH) return;
	float px = ui_px(scr);
	for (int w = 0; w < 2; ++w) {
		SDL_Rect b = sort_button(scr, g, scroll, w);
		bool active = sortMode == w, focus = barFocus && barSel == w;
		ui_key(scr, b, px * 3, focus ? KEY_SEL : active ? KEY_LIT : KEY_PLAIN);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 4.5f, tr(sortNames[w]), C_TEXT);
	}
	SDL_Rect last = sort_button(scr, g, scroll, 1);
	int right = screens[scr].rect.w - g->m - px * 2, cy = last.y + last.h / 2;
	const char* hint = tr("Y: favorite");
	int hw = ui_label_width(scr, px * 4, hint);
	if (right - hw - px * 10 > last.x + last.w + px * 4) {
		ui_label(scr, right - hw / 2, cy, px * 4, hint, C_DIM);
		draw_star(scr, right - hw - px * 5, cy, (int) (px * 3.2f), STAR_GOLD);
	}
}

static SDL_Rect settings_button(int scr) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	int h = px * 14, w = px * 44;
	return (SDL_Rect) { r.w - w - px * 4, (px * 20 - h) / 2, w, h };
}

/* A centred label that fits maxW x maxH: shrunk first; when that would be much smaller than cap and the text has a space, on two lines (as large as fits). Longer translations (French's "Touch or use the D-Pad", "Supported") need it. */
static void label_fit(int scr, int cx, int cy, int cap, int maxW, int maxH, const char* text) {
	int w = ui_label_width(scr, cap, text), one = cap;
	if (w > maxW && w > 0) one = (int) ((long long) cap * maxW / w);
	const char* sp = NULL;
	if (one < cap * 75 / 100) {
		size_t n = strlen(text), best = n;
		for (size_t i = 1; i + 1 < n; ++i)
			if (text[i] == ' ' && (best == n || labs((long) i - (long) n / 2) < labs((long) best - (long) n / 2))) best = i;
		if (best < n) sp = text + best;
	}
	if (!sp) {
		ui_label(scr, cx, cy, SDL_max(one, cap * 45 / 100), text, C_TEXT);
		return;
	}
	char a[160], b[160];
	size_t la = (size_t) (sp - text);
	snprintf(a, sizeof a, "%.*s", (int) SDL_min(la, sizeof a - 1), text);
	snprintf(b, sizeof b, "%s", sp + 1);
	int wa = ui_label_width(scr, cap, a), wb = ui_label_width(scr, cap, b), wm = SDL_max(wa, wb), two = cap;
	if (wm > maxW && wm > 0) two = (int) ((long long) cap * maxW / wm);
	two = SDL_min(two, maxH * 24 / 100); /* two lines, each a cap high, with a gap, in the button */
	two = SDL_max(two, cap * 45 / 100);
	if (two < one) two = one;
	ui_label(scr, cx, cy - two * 9 / 10, two, a, C_TEXT);
	ui_label(scr, cx, cy + two * 9 / 10, two, b, C_TEXT);
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
	label_fit(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, b.w - px * 2, b.h, cfg->showAll ? tr("All") : tr("Supported"));
}

/* What the button does next: four squares (in the list: switch to two
 * columns), nine (in two columns: switch to three) or three lines (in three
 * columns: switch to the list). */
static void draw_view_button(int scr) {
	SDL_Rect b = view_button(scr);
	float px = ui_px(scr);
	ui_key(scr, b, px * 3, KEY_PLAIN);
	SDL_Color ink = ui_key_ink();
	int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
	if (viewCols == 1) {
		int sq = b.h * 12 / 56, gap = b.h * 6 / 56;
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 2; ++j) {
				SDL_Rect q = { cx - sq - gap / 2 + i * (sq + gap), cy - sq - gap / 2 + j * (sq + gap), sq, sq };
				ui_fill_round(scr, q, ink, SDL_max(1, sq / 4));
			}
		}
	} else if (viewCols == 2) {
		int sq = b.h * 8 / 56, gap = b.h * 4 / 56;
		for (int i = -1; i <= 1; ++i) {
			for (int j = -1; j <= 1; ++j) {
				SDL_Rect q = { cx + i * (sq + gap) - sq / 2, cy + j * (sq + gap) - sq / 2, sq, sq };
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

static const char* view_name(void) {
	return viewCols == 3 ? "grid3" : viewCols == 2 ? "grid" : "list";
}

/* library.cfg: the game list's own settings (and, on Android, the ROM and
 * save folders and the saves' type, from Settings > Folders). */
static void save_library_cfg(const Config* cfg) {
	char file[1100];
	snprintf(file, sizeof(file), "%s/library.cfg", cfg->stateDir);
	FILE* f = fopen(file, "w");
	if (!f) return;
	fprintf(f, "# written by the game list\nlibrary_view = %s\nlibrary_sort = %s\n", view_name(), sortMode == 1 ? "recent" : "alpha");
	/* (Settings > Save States: shared with RetroArch or not, issue #39) */
	fprintf(f, "state_folder = %s\n", strcmp(cfg->stateFolder, "retroarch") ? "gbads" : "retroarch");
	/* (Settings > RetroArch's Folder: one chosen by hand, or empty: found from RetroArch's settings) */
	fprintf(f, "retroarch_state_dir = %s\n", cfg->raStateDir);
	/* (Settings > GBA BIOS: the player's own, off unless switched on; issue #78) */
	fprintf(f, "gba_bios = %d\n", cfg->gbaBios ? 1 : 0);
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
	viewCols = viewCols % 3 + 1;
	snprintf(cfg->libraryView, sizeof(cfg->libraryView), "%s", view_name());
	save_library_cfg(cfg);
}

/* The list made again in its new order, the selection staying on its game. */
static int resort(const Config* cfg, int sel) {
	char keep[1024] = "";
	if (sel >= 0 && sel < nGames) snprintf(keep, sizeof(keep), "%s", games[sel].path);
	sort_games(cfg);
	free_tiles();
	thumbFor = -2;
	for (int i = 0; i < nGames; ++i)
		if (!strcmp(games[i].path, keep)) return i;
	return nGames ? 0 : -1;
}

static int set_sort(Config* cfg, int mode, int sel) {
	sortMode = mode;
	snprintf(cfg->librarySort, sizeof(cfg->librarySort), "%s", mode == 1 ? "recent" : "alpha");
	save_library_cfg(cfg);
	return resort(cfg, sel);
}

static int toggle_fav(const Config* cfg, int sel) {
	Mark* m = mark_find(games[sel].path, true);
	m->fav = !m->fav;
	marks_save(cfg);
	return resort(cfg, sel);
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
	snprintf(storageMsg, sizeof(storageMsg), tr("Can't save files here: no states, logos or screenshots"));
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
	bool extracting = done < total || ui_file_crc_pending(); /* (or a ROM's checksum, for its logo's name) */
	for (int i = 0; i < nGames && i < 256; ++i) {
		SDL_Rect t = tile_rect(&g, i, scroll);
		if (t.y + t.h <= g.headerH || t.y >= r.h) continue;
		bool on = i == sel && !barFocus;
		ui_key(scr, t, px * 3, on ? KEY_SEL : KEY_PLAIN);
		draw_star_mark(scr, &t, games[i].fav);
		if (listMode) {
			/* name on the left; region flag and "Single screen only" on the right */
			int right = t.x + t.w - px * 14;
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
			tileMod[i] = logo_mod_of(&games[i]);
			bool pixelArt;
			if (ui_load_logo(cfg, games[i].path, games[i].code, known_crc(&games[i]), tileLogo[i], &tileLogoW[i],
			                 &tileLogoH[i], &pixelArt)) {
				for (int v = 0; v < 2; ++v) {
					if (tileLogo[i][v]) SDL_SetTextureScaleMode(tileLogo[i][v], SDL_ScaleModeLinear);
				}
			}
		}
		if (tileLogo[i][0] || tileLogo[i][1]) {
			/* the logo, as large as fits with some padding, keeping its shape */
			int pad = viewCols == 3 ? px * 4 : px * 5;
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
	draw_sort_bar(scr, &g, scroll);
	/* header drawn last so tiles scroll underneath it */
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, g.headerH }, C_HEADER);
	SDL_Rect sb = settings_button(scr);
	if (storageMsg[0]) {
		char line[300];
		fit_text(line, sizeof(line), TF_BOLD, px * 4, storageMsg, theme_button(scr).x - px * 8);
		ui_font_text(scr, TF_BOLD, theme_button(scr).x / 2, g.headerH / 2, px * 4, line, C_GOLD, 0);
	} else {
		label_fit(scr, theme_button(scr).x / 2, g.headerH / 2, px * 5, theme_button(scr).x - px * 8, g.headerH, tr("Touch or use the D-Pad"));
	}
	draw_theme_button(scr);
	draw_view_button(scr);
	draw_games_button(cfg, scr);
	ui_key(scr, sb, px * 3, KEY_PLAIN);
	label_fit(scr, sb.x + sb.w / 2, sb.y + sb.h / 2, px * 5, sb.w - px * 4, sb.h, tr("Settings"));
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
#define SETTINGS_MAX 9
static int nRows = 1; /* rows shown (Back is row nRows) */
/* (Hotkeys shares Map Buttons' line: the right half of it, under one text) */
enum { ROW_BANANA, ROW_ES, ROW_MAP, ROW_HOTKEYS, ROW_UPDATES, ROW_FOLDERS, ROW_RA, ROW_ESDE, ROW_PERF, ROW_STATES, ROW_STATES_DIR, ROW_RESET, ROW_DELCFG, ROW_ROTATION, ROW_LANGUAGE, ROW_BIOS };
static int rowKind[SETTINGS_MAX];

/* System's top row: the interface's language, stepped on each press (the setting is the one in config.c's table) */
static const Setting* lang_setting(void) { return config_setting("language"); }
static const char* lang_label(const Config* cfg) {
	static char label[200];
	char value[120];
	config_setting_text(cfg, lang_setting(), value, sizeof(value));
	snprintf(label, sizeof(label), "%s: %s", tr("Language"), value);
	return label;
}

/* System's GBA BIOS row (issue #78): the player's own BIOS, for save states made with one (RetroArch's
 * mGBA core with a BIOS file set). Off unless switched on, and never found by itself: the file is
 * <data folder>/bios/gba_bios.bin, which the player puts there. Off, games run with mGBA's built-in BIOS
 * exactly as before. emu_bios_path is what emu_open gives every core. */
static void bios_file(const Config* cfg, char* out, size_t n) {
	snprintf(out, n, "%s/bios/gba_bios.bin", cfg->assetDir);
}
void library_bios_apply(const Config* cfg) { /* (start-up, and when the row changes) */
	char file[1100];
	bios_file(cfg, file, sizeof(file));
	snprintf(emu_bios_path, sizeof(emu_bios_path), "%s", cfg->gbaBios ? file : "");
}
static void bios_switch(Config* cfg) {
	char file[1100];
	bios_file(cfg, file, sizeof(file));
	if (cfg->gbaBios) {
		cfg->gbaBios = false;
		snprintf(settingsMsg, sizeof(settingsMsg), "%s", tr("Off: games start with GBA DS's built-in BIOS again."));
	} else {
		char dir[1100];
		snprintf(dir, sizeof(dir), "%s/bios", cfg->assetDir);
		mkdirs(dir);
		BiosFile st = emu_bios_check(file);
		if (st == BIOS_FILE_OK) {
			cfg->gbaBios = true;
			snprintf(settingsMsg, sizeof(settingsMsg), "%s", tr("On: games start with your BIOS from now on. A state saved with the other BIOS may not load."));
		} else {
			snprintf(settingsMsg, sizeof(settingsMsg), "%s",
			         st == BIOS_FILE_MISSING ? tr("Still off: put your GBA BIOS in the bios folder of GBA DS's data folder, named gba_bios.bin.")
			         : st == BIOS_FILE_SIZE  ? tr("Still off: gba_bios.bin isn't a GBA BIOS (it should be 16 KB).")
			         : st == BIOS_FILE_BROKEN ? tr("Still off: that is a known broken BIOS dump.")
			                                  : tr("Still off: gba_bios.bin isn't a GBA BIOS."));
		}
	}
	library_bios_apply(cfg);
	save_library_cfg(cfg);
	fprintf(stderr, "library: GBA BIOS %s\n", cfg->gbaBios ? "on" : "off");
}
static const char* bios_label(const Config* cfg) { return cfg->gbaBios ? N_("GBA BIOS: Your Own") : N_("GBA BIOS: Built-in"); }
static const char* bios_note(const Config* cfg, SDL_Color* col) {
	if (cfg->gbaBios) {
		char file[1100];
		bios_file(cfg, file, sizeof(file));
		if (emu_bios_check(file) != BIOS_FILE_OK) {
			*col = C_GOLD;
			return tr("gba_bios.bin can't be used now, so games run with the built-in BIOS. Put it back in the bios folder.");
		}
		return tr("Games start with the GBA BIOS in gba_bios.bin, in the bios folder of GBA DS's data folder.");
	}
	return tr("Use your own GBA BIOS (gba_bios.bin, in the bios folder of GBA DS's data folder) instead of the built-in one. Only needed for save states made with one.");
}

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
	static const int android[] = { ROW_FOLDERS, ROW_RA };
	nRows = 0;
	rowKind[nRows++] = ROW_LANGUAGE;
	rowKind[nRows++] = ROW_UPDATES;
	/* ES-DE's folder found: GBA DS in its emulator list (esde.c) */
	if (esde_present()) rowKind[nRows++] = ROW_ESDE;
	for (size_t i = 0; i < sizeof(android) / sizeof(android[0]); ++i) {
		rowKind[nRows++] = android[i];
		/* one display (a phone, a Surface Duo): hold the window portrait or landscape (issue #60) */
		if (android[i] == ROW_RA && android_one_display()) rowKind[nRows++] = ROW_ROTATION;
	}
	rowKind[nRows++] = ROW_BIOS; /* (issue #78) */
	rowKind[nRows++] = ROW_RESET;
	rowKind[nRows++] = ROW_DELCFG;
#else
	nRows = 0;
	rowKind[nRows++] = ROW_LANGUAGE;
#ifdef GBADS_UPDATES
	rowKind[nRows++] = ROW_UPDATES;
#endif
	rowKind[nRows++] = ROW_STATES; /* (save states shared with RetroArch: issue #39) */
	rowKind[nRows++] = ROW_STATES_DIR;
#ifndef GBADS_SIDE
	/* (not in a side build: the system's GBA games would start it) */
	if (banana_present()) rowKind[nRows++] = ROW_BANANA;
	else if (es_present(cfg)) rowKind[nRows++] = ROW_ES;
#endif
	rowKind[nRows++] = ROW_BIOS; /* (issue #78) */
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
static SDL_Rect set_row_gap(int scr, int n, int i, int gapAt);
static SDL_Rect set_row_gaps(int scr, int n, int i, int gaps);
/* the Updates row's text: this version, and what the row does */
static const char* update_blurb(void) {
	static char blurb[160];
	snprintf(blurb, sizeof(blurb), tr("This is v%s. Look for a new version, and download and install it."), GBADS_VERSION);
	return blurb;
}

static void draw_set_heading(int scr, SDL_Rect p, const char* title, int icon);
static void set_notes(int scr, SDL_Rect panel, SDL_Rect back, const char* t1, SDL_Color c1, int f1, const char* t2, SDL_Color c2, int f2);
/* System's layout: Updates, a little room, the rows (Save States | RetroArch's State Folder side by
 * side, then the OS's integration), a little room, Reset | Delete side by side. A row that shares the
 * one before's slot is its second half. */
static bool row_second(int i) {
	return i > 0 && ((rowKind[i] == ROW_STATES_DIR && rowKind[i - 1] == ROW_STATES) || (rowKind[i] == ROW_DELCFG && rowKind[i - 1] == ROW_RESET));
}
static int row_slot(int i) { /* i == nRows: Back's */
	int slot = -1;
	for (int k = 0; k <= i && k < nRows; ++k) slot += row_second(k) ? 0 : 1;
	return i >= nRows ? slot + 1 : slot;
}
static SettingsGeom settings_geom(int scr) {
	SettingsGeom g;
	g.px = ui_px(scr);
	g.panel = set_panel(scr);
	int slots = row_slot(nRows) , gaps = 0;
	/* room before the first row after the top ones (Language, Updates), and before Reset */
	int top = 0;
	while (top < nRows && (rowKind[top] == ROW_LANGUAGE || rowKind[top] == ROW_UPDATES)) ++top;
	for (int i = 0; i < nRows; ++i) {
		if (row_second(i)) continue;
		int sl = row_slot(i);
		if ((i == top && top > 0 && top < nRows) || rowKind[i] == ROW_RESET) gaps |= 1 << sl;
	}
	for (int i = 0; i < nRows; ++i) {
		g.row[i] = set_row_gaps(scr, slots, row_slot(i), gaps), g.textY[i] = 0;
		if ((i + 1 < nRows && row_second(i + 1)) || row_second(i)) {
			int half = (g.row[i].w - g.px * 3) / 2;
			g.row[i].w = half;
			if (row_second(i)) g.row[i].x += half + g.px * 3;
		}
	}
	g.back = set_row_gaps(scr, slots, slots, gaps);
	/* BananaOS's note is long (and its NOTE under it): Back sits a little higher so the text, centred on it, stays in the panel */
	for (int i = 0; i < nRows; ++i)
		if (rowKind[i] == ROW_BANANA) g.back.y -= g.px * 12;
	return g;
}

/* The size for text to fit maxW (never below 45% of cap: past that, a longer text is better cut off than unreadable). */
static int fit_cap(int face, int cap, int maxW, const char* text) {
	int w = ui_font_width(face, cap, text), c = cap;
	if (w > maxW && w > 0) c = (int) ((long long) cap * maxW / w);
	return SDL_max(c, cap * 45 / 100);
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
	draw_set_heading(scr, g->panel, "System", GAME_ICON_SYSTEM);
	for (int i = 0; i <= nRows; ++i) {
		SDL_Rect b = i < nRows ? g->row[i] : g->back;
		ui_key(scr, b, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		if (i == nRows) {
			ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, "Back", C_TEXT);
			continue;
		}
		/* (a half-width row's longer label is set a little smaller to fit) */
		int cap = (int) (px * 5), room = b.w - (int) (px * 16), lw = ui_font_width(TF_BOLD, cap, labels[i]);
		if (lw > room && cap * room / lw < cap * 75 / 100) {
			/* much too long even smaller (a German label): two lines, at a size that fits the button */
			int c2 = cap * 85 / 100, h2 = ui_font_wrap_height(TF_BOLD, room, c2, labels[i]);
			while (h2 > b.h - (int) px * 3 && c2 > cap / 2) c2 = c2 * 9 / 10, h2 = ui_font_wrap_height(TF_BOLD, room, c2, labels[i]);
			ui_font_wrap(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2 - h2 / 2 + (int) px * 2, room, c2, labels[i], C_TEXT);
		} else {
			if (lw > room) cap = cap * room / lw;
			ui_font_text(scr, TF_BOLD, b.x + px * 6, b.y + b.h / 2, cap, labels[i], C_TEXT, -1);
		}
		ui_font_text(scr, TF_BOLD, b.x + b.w - px * 6, b.y + b.h / 2, px * 6, "\u203A", i == sel ? C_TEXT : C_DIM, 1);
	}
}

/* a page's helper text: beside Back (to its left, wrapped), the block centred on Back's height;
 * a second part (bold) goes under the first */
static void set_notes(int scr, SDL_Rect panel, SDL_Rect back, const char* t1, SDL_Color c1, int f1, const char* t2, SDL_Color c2, int f2) {
	int px = (int) ui_px(scr), cap = px * 4;
	int x = panel.x + px * 8, w = back.x - px * 4 - x;
	int gap = t1 && t2 ? px * 3 : 0;
	/* (the block is centred on Back's height and may use the room down to the panel's edge: a longer text,
	 * as in German, is set smaller until it fits, then wrapped) */
	int room = 2 * (panel.y + panel.h - px * 4 - (back.y + back.h / 2));
	int h1 = 0, h2 = 0;
	for (int c = cap; ; c = c * 9 / 10) {
		cap = c;
		h1 = t1 ? ui_font_wrap_height(f1, w, cap, t1) : 0;
		h2 = t2 ? ui_font_wrap_height(f2, w, cap, t2) : 0;
		if (h1 + gap + h2 <= room || c * 100 < px * 4 * 55) break;
	}
	int y = back.y + back.h / 2 - (h1 + gap + h2) / 2;
	if (t1) y = ui_font_wrap(scr, f1, x, y, w, cap, t1, c1) + gap;
	if (t2) ui_font_wrap(scr, f2, x, y, w, cap, t2, c2);
}

static void draw_system_note(int scr, const SettingsGeom* g, const char* text, SDL_Color c) {
	if (nRows < 1 || !text) return;
	set_notes(scr, g->panel, g->back, text, c, TF_BODY, NULL, c, TF_BODY);
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
	const char* keys[20];
} SetPage;
static const SetPage setPages[SP_SYSTEM] = {
	{ N_("Games"), N_("Which games, names, saving"), { "show_all", "name_tags", "save_on_quit", "auto_load", "custom_logos", "quick_bar_hint" } },
	{ N_("Display"), N_("Scale, shader, theme, color"), { "scale", "map_scale", "gb_size", "gb_scale", "gb_palette", "gb_dual", "gb_border", "gb_correction", "gb_ghosting", "gb_shader", "shader", "lcd_strength", "color_correction", "lcd_ghosting", "smooth", "theme", "accent" } },
	{ "Screens", N_("Placement, overlays"), { "swap", "single_layout", "single_size", "screen_match", "screen_placement", "overlays", "overlay_screens", "bezel_logo", "brightness_top", "brightness_bottom" } },
	{ "Controls", N_("Stick, buttons, hotkeys"), { "@map", "@hotkeys", "left_stick_dpad", "ab_swap", "button_right", "button_bottom", "button_top", "button_left", "button_l", "button_r", "button_dpad_up", "button_dpad_down", "button_dpad_left", "button_dpad_right", "turbo_rate", "rumble_strength" } },
	{ N_("Speed and sound"), N_("Rewind, fast forward"), { "show_fps", "match_display", "frame_buffer", "rewind", "rewind_seconds", "fast_forward", "audio_latency", "performance" } },
	{ "Second screen", N_("The map and the logo"), { "map", "map_interval", "toggle", "auto_rotate", "toggle_button", "pinball_live" } },
};
static int setPage = SP_HOME; /* SP_HOME, a category, or SP_SYSTEM (the older panel) */
static int setSel;
#define SET_VIS 8 /* most rows shown at once: more scroll */
static int setTop;     /* the first row shown */
static int setDragY;   /* where a finger last moved the list from */
static int setNVis(int n) { return SDL_min(n, SET_VIS); }
/* the row chosen kept on screen (Back, after the last row, needs none) */
static void set_scroll(int n) {
	if (setSel < n && setSel < setTop) setTop = setSel;
	if (setSel < n && setSel >= setTop + SET_VIS) setTop = setSel - SET_VIS + 1;
	setTop = SDL_clamp(setTop, 0, SDL_max(0, n - SET_VIS));
}
static bool setRescan; /* the games shown changed: list them again */

/* the rows of a page: out[i] its setting, or NULL for a row that opens a screen
 * (ids[i] is "@map" or "@hotkeys") */
static int page_settings(int page, const Setting** out, const char** ids) {
	int n = 0;
	for (int i = 0; i < 20 && setPages[page].keys[i]; ++i) {
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

/* a page: n rows, then Back, at the right of the strip under the rows (the note
 * flows round it). gapAt: a row with a little more room above it (System: before
 * Reset settings), or -1 */
static SDL_Rect set_row_gaps(int scr, int n, int i, int gaps) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	int top = p.y + px * 22, gap = px * 2, extra = px * 6;
	int nGaps = __builtin_popcount(gaps & ((1 << n) - 1)); /* (a bit per slot with room before it) */
	int backW = px * 40, backH = px * 14, strip = px * 28;
	int avail = p.y + p.h - px * 5 - strip - top - extra * nGaps;
	int h = SDL_min(px * 18, avail / SDL_max(1, n) - gap);
	if (i == n) return (SDL_Rect) { p.x + p.w - px * 6 - backW, p.y + p.h - px * 5 - backH, backW, backH };
	int before = __builtin_popcount(gaps & ((2 << i) - 1) & ((1 << n) - 1));
	return (SDL_Rect) { p.x + px * 6, top + i * (h + gap) + before * extra, p.w - px * 12, h };
}
static SDL_Rect set_row_gap(int scr, int n, int i, int gapAt) {
	return set_row_gaps(scr, n, i, gapAt > 0 && gapAt < n ? 1 << gapAt : 0);
}
static SDL_Rect set_row(int scr, int n, int i) {
	return set_row_gap(scr, n, i, -1);
}

/* the heading of every Settings page: large, filling the strip above the rows */
static void draw_set_heading(int scr, SDL_Rect p, const char* title, int icon) {
	int px = (int) ui_px(scr), cap = px * 10;
	int w = ui_label_width(scr, cap, title), maxW = p.w - px * 12;
	if (w > maxW) cap = cap * maxW / w;
	int tx = p.x + p.w / 2;
	if (icon >= 0) {
		/* the pause menu's picture for the page, before the title */
		int is = cap * 2, gap = cap / 2, tw = ui_label_width(scr, cap, title);
		SDL_Rect ib = { tx - (tw + gap + is) / 2, p.y + px * 11 - is / 2, is, is };
		game_page_icon(scr, icon, ib, C_TEXT, C_HEADER);
		tx = ib.x + is + gap + tw / 2;
	}
	ui_label(scr, tx, p.y + px * 11, cap, title, C_TEXT);
}

static void draw_set_home(int scr, int sel) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	ui_dim(scr, 200);
	ui_fill_round(scr, p, C_HEADER, px * 4);
	draw_set_heading(scr, p, "GBA DS Settings", -1);
	for (int i = 0; i < 8; ++i) {
		SDL_Rect t = set_tile(scr, i);
		ui_key(scr, t, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		const char* name = i < SP_SYSTEM ? setPages[i].name : i == SP_SYSTEM ? "System" : "Back";
		const char* sub = i < SP_SYSTEM ? setPages[i].sub : i == SP_SYSTEM ? N_("Updates, integrations"): NULL;
#ifdef __ANDROID__
		if (i == SP_SYSTEM) sub = N_("Folders, accounts, updates");
#endif
		/* the pause menu's picture (not on Back), then the text, all left-aligned in the button */
		int iw = t.h * 6 / 10, gap = px * 3, x0 = t.x + px * 5, tx = x0 + iw + gap;
		if (i <= SP_SYSTEM) {
			SDL_Rect ib = { x0, t.y, iw, t.h };
			game_page_icon(scr, i == SP_SYSTEM ? GAME_ICON_SYSTEM : i, ib, i == sel || !ui_theme_light() ? C_TEXT : ui_key_ink(), ui_key_mid(i == sel ? KEY_SEL : KEY_PLAIN));
		}
		int tw = t.x + t.w - px * 5 - tx; /* (the room for the text; a longer one is set smaller) */
		ui_font_text(scr, TF_BOLD, tx, t.y + t.h / 2 - (sub ? px * 4 : 0), fit_cap(TF_BOLD, px * 6, tw, name), name, C_TEXT, -1);
		if (sub) ui_font_text(scr, TF_BODY, tx, t.y + t.h / 2 + px * 7, fit_cap(TF_BODY, px * 4, tw, sub), sub, C_DIM, -1);
	}
}

static void draw_set_page(const Config* cfg, int scr, int page, int sel) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	const Setting* rows[20];
	const char* ids[20];
	int n = page_settings(page, rows, ids), nv = setNVis(n);
	setTop = SDL_clamp(setTop, 0, SDL_max(0, n - SET_VIS));
	ui_dim(scr, 200);
	ui_fill_round(scr, p, C_HEADER, px * 4);
	draw_set_heading(scr, p, setPages[page].name, page);
	for (int s = 0; s <= nv; ++s) {
		int i = s == nv ? n : setTop + s;
		SDL_Rect r = set_row(scr, nv, s);
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
		int ax = r.x + r.w - px * 6;
		/* (the value keeps at most 55% of the row, the label the rest: either is set smaller to fit) */
		int vcap = fit_cap(TF_BOLD, px * 5, r.w * 55 / 100, val), vw = ui_font_width(TF_BOLD, vcap, val);
		int lroom = (ax - px * 16 - vw) - (r.x + px * 6) - px * 8;
		ui_font_text(scr, TF_BOLD, r.x + px * 6, r.y + r.h / 2, fit_cap(TF_BOLD, px * 5, lroom, rows[i]->label), rows[i]->label, C_TEXT, -1);
		ui_font_text(scr, TF_BOLD, ax, r.y + r.h / 2, px * 6, "›", i == sel ? C_TEXT : C_DIM, 1);
		ui_font_text(scr, TF_BOLD, ax - px * 8, r.y + r.h / 2, vcap, val, C_GOLD, 1);
		ui_font_text(scr, TF_BOLD, ax - px * 16 - vw, r.y + r.h / 2, px * 6, "‹", i == sel ? C_TEXT : C_DIM, 1);
	}
	if (n > SET_VIS) {
		/* more rows than fit: a bar down the panel's right edge */
		SDL_Rect a = set_row(scr, nv, 0), z = set_row(scr, nv, nv - 1);
		int bw = px * 2, bx = p.x + p.w - px * 4, th = z.y + z.h - a.y;
		ui_fill_round(scr, (SDL_Rect) { bx, a.y, bw, th }, (SDL_Color) { 60, 56, 84, 255 }, px);
		int bh = SDL_max(bw * 3, th * SET_VIS / n), by = a.y + (th - bh) * setTop / SDL_max(1, n - SET_VIS);
		ui_fill_round(scr, (SDL_Rect) { bx, by, bw, bh }, C_GOLD, px);
	}
	const char* note = sel >= n ? NULL : rows[sel] ? rows[sel]->note
	                 : !strcmp(ids[sel], "@map") ? N_("Sets which of this device's buttons does what."): N_("Sets the shortcuts: the hotkey button held with another.");
	if (note) {
		set_notes(scr, p, set_row(scr, nv, nv), note, C_DIM, TF_BODY, NULL, C_DIM, TF_BODY);
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
	if (!strcmp(st->key, "language")) i18n_set(cfg->language), ui_language_changed();
	if (!strcmp(st->key, "theme")) ui_theme(cfg->lightTheme);
	if (!strcmp(st->key, "accent")) ui_accent(cfg->accent), ui_theme(cfg->lightTheme);
	if (!strncmp(st->key, "brightness_", 11)) ui_brightness(cfg->brightTop, cfg->brightBottom);
	if (!strcmp(st->key, "rumble_strength")) ui_rumble_pulse(cfg->rumbleStrength / 100.0f, 250); /* a buzz to feel it by */
	if (!strcmp(st->key, "show_all") || !strcmp(st->key, "name_tags")) setRescan = true;
	if (!strcmp(st->key, "screen_match") || !strcmp(st->key, "screen_placement")) ui_layout_pictures();
	if (!strcmp(st->key, "single_layout") || !strcmp(st->key, "single_size")) ui_layout();
	fprintf(stderr, "setting: %s = %s (all games)\n", lines[0][0], lines[0][1]);
}


/* System > Reset settings to defaults / Delete per-game settings: each asks to be
 * touched again before it does anything (confirmRow: the row asked) */
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
	ui_accent(cfg->accent);
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
		snprintf(settingsMsg, sizeof(settingsMsg), "%s",
		         rowKind[row] == ROW_RESET ? tr("Are you sure? Touch it again to put every setting back to its default. Anything else cancels.")
		                                   : tr("Are you sure? Touch it again to delete every game's own settings. Anything else cancels."));
		return;
	}
	confirmRow = -1;
	if (rowKind[row] == ROW_RESET) {
		int n = reset_settings(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Settings are back to their defaults."));
		fprintf(stderr, "settings: reset to defaults (%d settings)\n", n);
	} else {
		int n = delete_game_settings(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), n == 0 ? tr("No game has settings of its own."): n == 1 ? tr("Deleted the settings of 1 game."): tr("Deleted the settings of %d games."), n);
		fprintf(stderr, "settings: deleted %d per-game settings files\n", n);
	}
}

/* one frame of input for the pages; false when Settings should close */
static bool settings_pages_input(Config* cfg, int ls) {
	const Setting* rows[20];
	const char* ids[20];
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
			setTop = 0;
			if (setPage == SP_SYSTEM) settings_rows(cfg), confirmRow = -1;
		}
		if (back) return false;
	} else {
		int dir = 0;
		if (btn_pressed(B_DOWN)) setSel = (setSel + 1) % (n + 1);
		if (btn_pressed(B_UP)) setSel = (setSel + n) % (n + 1);
		set_scroll(n);
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
		if (pt.screen == ls && pt.kind == 3 && setPage >= 0 && setPage < SP_SYSTEM && n > SET_VIS) {
			/* a finger dragged up or down moves the list a row at a time */
			int step = SDL_max(1, set_row(ls, setNVis(n), 1).y - set_row(ls, setNVis(n), 0).y);
			if (abs(pt.y - setDragY) >= step) setTop = SDL_clamp(setTop + (pt.y < setDragY ? 1 : -1), 0, n - SET_VIS), setDragY = pt.y;
			continue;
		}
		if (pt.screen != ls || pt.kind != 1) continue;
		setDragY = pt.y;
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
				setTop = 0;
				if (setPage == SP_SYSTEM) settings_rows(cfg), confirmRow = -1;
			}
			continue;
		}
		int nv = setNVis(n);
		SDL_Rect b = set_row(ls, nv, nv);
		if (SDL_PointInRect(&q, &b)) {
			setSel = setPage, setPage = SP_HOME;
			continue;
		}
		if (!SDL_PointInRect(&q, &panel)) {
			setSel = setPage, setPage = SP_HOME;
			continue;
		}
		for (int i = setTop; i < setTop + nv; ++i) {
			SDL_Rect r = set_row(ls, nv, i - setTop);
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
	if (cfg->saveDir[0]) snprintf(line, n, tr("Games are read from %s (subfolders too). Saves (%s) are kept in %s."), dirs, type, cfg->saveDir);
	else snprintf(line, n, tr("Games are read from %s (subfolders too). Saves (%s) are kept next to the games."), dirs, type);
}

static void draw_settings(const Config* cfg, int scr, int sel) {
	if (sel != confirmRow) confirmRow = -1; /* moved off the row: no longer asked */
	SettingsGeom g = settings_geom(scr);
	char user[128], err[256];
	int ra = ra_account(user, sizeof(user), err, sizeof(err));
	const char* raLabel = ra == RA_ACCOUNT_OK     ? N_("RetroAchievements: Sign Out"):
	                      ra == RA_ACCOUNT_BUSY   ? N_("RetroAchievements: Signing In..."):
	                      ra == RA_ACCOUNT_OFF    ? N_("RetroAchievements: Off"):
	                                                N_("RetroAchievements: Sign In");
	const char* labels[SETTINGS_MAX];
	for (int i = 0; i < nRows; ++i) {
		int k = rowKind[i];
		labels[i] = k == ROW_LANGUAGE ? lang_label(cfg)
		          : k == ROW_BIOS    ? bios_label(cfg)
		          : k == ROW_ESDE    ? (esde_integration_on() ? N_("GBA DS in ES-DE: On"): N_("GBA DS in ES-DE: Off"))
		          : k == ROW_FOLDERS ? "Folders"
		          : k == ROW_RA      ? raLabel
		          : k == ROW_ROTATION ? (android_rotation_get() == 1 ? N_("Rotation: Portrait"): android_rotation_get() == 2 ? N_("Rotation: Landscape"): N_("Rotation: Auto"))
		          : k == ROW_RESET   ? (confirmRow == i ? N_("Are you sure? Reset settings"): N_("Reset settings to defaults"))
		          : k == ROW_DELCFG  ? (confirmRow == i ? N_("Are you sure? Delete them"): N_("Delete per-game settings"))
		                             : N_("Updates");
	}
	draw_settings_frame(scr, &g, sel, labels);
	if (sel >= nRows) return;
	char line[1400];
	const char* text = NULL;
	SDL_Color col = C_DIM;
	switch (rowKind[sel]) {
	case ROW_LANGUAGE:
		text = N_("Automatic follows the system. Text that isn't translated yet stays in English.");
		break;
	case ROW_BIOS:
		text = bios_note(cfg, &col);
		break;
	case ROW_ESDE:
		text = N_("Makes GBA DS the emulator for Game Boy Advance games in ES-DE. For a game it doesn't support, "
		       "choose another in ES-DE (the game's metadata > Alternative emulator).");
		break;
	case ROW_RESET:
	case ROW_DELCFG:
		text = NULL; /* (the label says it; asks again before doing it) */
		break;
	case ROW_ROTATION:
		text = android_rotation_get() == 1 ? N_("Held upright: the two screens one above the other. Either way up."): android_rotation_get() == 2 ? N_("Held on its side: the two screens side by side. Either way round."): N_("Follows how you hold the phone, and Android's own rotation lock.");
		break;
	case ROW_FOLDERS:
		text = cfg->saveDir[0] ? N_("Where games are read from, and the folder their saves are kept in."): N_("Where games are read from. Their saves are kept next to them.");
		break;
	case ROW_RA:
		if (raDialogPending) {
			snprintf(line, sizeof(line), tr("Enter your account in the sign-in box..."));
		} else if (ra == RA_ACCOUNT_OK) {
			snprintf(line, sizeof(line), tr("Signed in as %s. Achievements are earned in casual mode."), user);
		} else if (ra == RA_ACCOUNT_BUSY) {
			snprintf(line, sizeof(line), tr("Signing in as %s..."), user);
		} else if (ra == RA_ACCOUNT_FAILED) {
			size_t n = strlen(err);
			while (n && (err[n - 1] == '.' || err[n - 1] == ' ')) err[--n] = 0; /* the server's own full stop */
			snprintf(line, sizeof(line), tr("Couldn't sign in as %s%s%s. Sign in to try again."), user, err[0] ? ": " : "", err);
			col = C_GOLD;
		} else if (ra == RA_ACCOUNT_OFF) {
			snprintf(line, sizeof(line), tr("Switched off in gba-ds.cfg (retroachievements = 0)."));
		} else {
			snprintf(line, sizeof(line),
			         tr("Earn achievements in casual mode with your retroachievements.org account. GBA DS keeps a sign-in token, "
			         "not your password."));
		}
		text = line;
		break;
	default:
		text = update_blurb();
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
	case ROW_LANGUAGE:
		if (config_setting_step(cfg, lang_setting(), 1)) setting_changed(cfg, lang_setting());
		return;
	case ROW_BIOS:
		bios_switch(cfg);
		return;
	case ROW_ESDE:
		esde_integration_set(!esde_integration_on(), settingsMsg, sizeof(settingsMsg));
		return;
	case ROW_ROTATION:
		android_rotation_set((android_rotation_get() + 1) % 3);
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
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Signed out of RetroAchievements."));
	} else if (ra == RA_ACCOUNT_NONE || ra == RA_ACCOUNT_FAILED) {
		if (android_ra_dialog(user)) raDialogPending = true;
		else snprintf(settingsMsg, sizeof(settingsMsg), tr("Couldn't open the sign-in box."));
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
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Couldn't keep that choice."));
	} else if (home && !strcmp(to, home)) {
		/* (chosen back before the move happened: nothing to move) */
		kv_set(record, "data_dir", to, "# GBA DS's data folder");
		snprintf(settingsMsg, sizeof(settingsMsg), tr("GBA DS's files are kept there already."));
	} else if (home && !strncmp(to, home, hn) && to[hn] == '/') {
		snprintf(settingsMsg, sizeof(settingsMsg), tr("That's inside GBA DS's own folder: choose one outside it."));
	} else if (!can_write_in(path)) {
		snprintf(settingsMsg, sizeof(settingsMsg),
		         tr("GBA DS can't save files there: allow it All files access in Android's settings, or choose another folder."));
	} else if (!kv_set(record, "data_dir", to, "# GBA DS's data folder")) {
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Couldn't keep that choice."));
	} else {
		snprintf(settingsMsg, sizeof(settingsMsg),
		         tr("GBA DS's files go to %s the next time it starts: close GBA DS and open it again."), to);
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
		else if (st > 0) snprintf(settingsMsg, sizeof(settingsMsg), tr("Enter both your username and your password."));
		memset(pass, 0, sizeof(pass));
	}
	if (!pickPending) return false;
	char path[1024];
	int st = android_take_picked_folder(path, sizeof(path));
	if (!st) return false;
	pickPending = false;
	settingsMsgRow = row_of(ROW_FOLDERS);
	if (st < 0) {
		snprintf(settingsMsg, sizeof(settingsMsg), tr("No folder chosen."));
		return false;
	}
	if (pickFor == PICK_DATA) {
		data_folder_picked(path);
		return false;
	}
	if (pickFor == PICK_SAVES) {
		if (!can_write_in(path)) {
			snprintf(settingsMsg, sizeof(settingsMsg),
			         tr("GBA DS can't save files there: allow it All files access in Android's settings, or choose another folder."));
			return false;
		}
		snprintf(cfg->saveDir, sizeof(cfg->saveDir), "%s", path);
		save_library_cfg(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Saves will be kept there."));
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
		const char* label = ids[i] == FB_ROMS   ? N_("Choose ROM folder"): ids[i] == FB_SAVES  ? N_("Choose save folder"): ids[i] == FB_BESIDE ? N_("Keep saves next to the games"): ids[i] == FB_TYPE   ? (srm_saves(cfg) ? N_("Save files: .srm (RetroArch's)"): N_("Save files: .sav"))
		                  : ids[i] == FB_DATA   ? N_("Choose GBA DS folder"): "Back";
		ui_key(scr, b, px * 3, i == sel ? KEY_SEL : KEY_PLAIN);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, label, C_TEXT);
	}
	int tx = g.text.x, tw = g.text.w, y = g.text.y;
	SDL_Rect clip = { screens[scr].rect.x + g.text.x, screens[scr].rect.y + g.text.y, g.text.w, g.text.h };
	ui_clip(scr, &clip);
	char line[1600];
	if (pickPending) snprintf(line, sizeof(line), tr("Choose a folder in Android's file picker..."));
	else folders_text(cfg, line, sizeof(line));
	y = draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
	/* GBA DS's own folder, and where it's going */
	char want[1024];
	const char* home = android_home();
	if (home) {
		if (data_folder_wanted(want, sizeof(want)) && strcmp(want, home))
			snprintf(line, sizeof(line), tr("GBA DS's own files (states, settings, cheats, overlays) are in %s, moving to %s at the next start."), home, want);
		else
			snprintf(line, sizeof(line), tr("GBA DS's own files (states, settings, cheats, overlays) are in %s."), home);
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
		else snprintf(settingsMsg, sizeof(settingsMsg), tr("Couldn't open the file picker."));
		break;
	case FB_BESIDE:
		cfg->saveDir[0] = 0;
		save_library_cfg(cfg);
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Saves will be kept next to the games."));
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

/* Settings > Save States (issue #39): slots 0-9 and the automatic state in
 * GBA DS's own folder, or shared with RetroArch (its own files, in its
 * folder: a game goes on in either). The folder shown is the selected
 * game's (RetroArch's can depend on the game's). */
/* a long path's last parts (…/savestates/gba; whole up to 40 characters), for a line of text */
static const char* path_tail(const char* path) {
	const char* p = path;
	int parts = 0;
	for (const char* c = path + strlen(path); strlen(path) > 40 && c > path; --c) {
		if (*c == '/' && ++parts == 3) {
			p = c;
			break;
		}
	}
	static char out[1100];
	snprintf(out, sizeof(out), "%s%s", p != path ? "\xe2\x80\xa6" : "", p);
	return out;
}
static const char* states_rom(void) {
	return thumbFor >= 0 && thumbFor < nGames ? games[thumbFor].path : NULL; /* (the game shown in the list) */
}
static void states_text(const Config* cfg, char* out, size_t n) {
	char dir[1024];
	bool compress;
	bool found = retroarch_state_dir(cfg, states_rom(), dir, sizeof(dir), &compress);
	if (!found)
		snprintf(out, n, tr("Slots 0-9 and the resume point are GBA DS's own. To share them, choose RetroArch's state folder."));
	else if (strcmp(cfg->stateFolder, "retroarch"))
		snprintf(out, n, tr("Slots 0-9 and the resume point are GBA DS's own. Shared, they're RetroArch's: %s."), path_tail(dir));
	else
		snprintf(out, n, tr("Slots 0-9 and the resume point are RetroArch's (its GBA core set to mGBA): %s."), path_tail(dir));
}
/* Settings > RetroArch's Folder: its state folder chosen by hand (kept as
 * retroarch_state_dir in library.cfg), or found from its settings again
 * (Automatic). Choosing one shares the states with RetroArch. */
static void states_dir_pick(Config* cfg) {
	char dir[1024] = "", chosen[1024];
	bool compress;
	bool found = retroarch_state_dir(cfg, states_rom(), dir, sizeof(dir), &compress);
	bool byHand = cfg->raStateDir[0] != 0;
	/* (opens at the folder now, else where ROCKNIX or BananaOS keep RetroArch's states; else home) */
	const char* start = found ? dir : file_exists("/storage/roms") ? "/storage/roms/savestates" : "/data/states";
	FolderPick r = folder_pick("RetroArch's State Folder", start, ".state", true, byHand ? "Automatic" : NULL, chosen,
	                           sizeof(chosen));
	if (r == FOLDER_CANCEL) return;
	if (r == FOLDER_CHOSEN) {
		if (strlen(chosen) >= sizeof(cfg->raStateDir)) { /* (a cut-off path would be another folder) */
			snprintf(settingsMsg, sizeof(settingsMsg), "%s", tr("That folder's path is too long."));
			return;
		}
		snprintf(cfg->raStateDir, sizeof(cfg->raStateDir), "%s", chosen);
		snprintf(cfg->stateFolder, sizeof(cfg->stateFolder), "retroarch");
		snprintf(settingsMsg, sizeof(settingsMsg), tr("Slots 0-9 and the automatic state are now shared with RetroArch, in %s."), path_tail(chosen));
	} else {
		cfg->raStateDir[0] = 0;
		if (retroarch_state_dir(cfg, states_rom(), dir, sizeof(dir), &compress)) {
			snprintf(settingsMsg, sizeof(settingsMsg), tr("RetroArch's state folder is found from its settings again: %s."), path_tail(dir));
		} else {
			snprintf(cfg->stateFolder, sizeof(cfg->stateFolder), "gbads");
			snprintf(settingsMsg, sizeof(settingsMsg), tr("RetroArch's settings weren't found, so slots 0-9 and the automatic state are GBA DS's own again."));
		}
	}
	save_library_cfg(cfg);
	fprintf(stderr, "library: RetroArch's state folder %s%s, states %s\n", cfg->raStateDir[0] ? "chosen: " : "from its settings",
	        cfg->raStateDir, strcmp(cfg->stateFolder, "retroarch") ? "GBA DS's own" : "shared");
}
/* RetroArch's State Folder's note: the folder, and where it came from */
static void states_dir_text(const Config* cfg, char* out, size_t n) {
	char dir[1024];
	bool compress;
	if (!retroarch_state_dir(cfg, states_rom(), dir, sizeof(dir), &compress))
		snprintf(out, n, tr("RetroArch's settings weren't found here: choose the folder where it keeps its save states."));
	else
		snprintf(out, n, tr("%s %s. Choose another if RetroArch keeps its states somewhere else."),
		         cfg->raStateDir[0] ? tr("Chosen here:"): tr("From RetroArch's settings:"), path_tail(dir));
}
static void states_switch(Config* cfg) {
	char dir[1024];
	bool compress;
	bool shared = !strcmp(cfg->stateFolder, "retroarch");
	if (!shared && !retroarch_state_dir(cfg, states_rom(), dir, sizeof(dir), &compress)) {
		/* (RetroArch's settings not found: where are its states?) */
		states_dir_pick(cfg);
		return;
	}
	snprintf(cfg->stateFolder, sizeof(cfg->stateFolder), "%s", shared ? "gbads" : "retroarch");
	save_library_cfg(cfg);
	fprintf(stderr, "library: save states %s\n", shared ? "in GBA DS's own folder" : "shared with RetroArch");
	snprintf(settingsMsg, sizeof(settingsMsg), "%s",
	         shared ? tr("Slots 0-9 and the automatic state are GBA DS's own again. The states saved in RetroArch's folder stay there."): tr("Slots 0-9 and the automatic state are now RetroArch's. GBA DS's own states stay in its folder, for when this is switched back."));
}

static void draw_settings(const Config* cfg, int scr, int sel) {
	if (sel != confirmRow) confirmRow = -1; /* moved off the row: no longer asked */
	SettingsGeom g = settings_geom(scr);
	const char* labels[SETTINGS_MAX];
	bool shared = !strcmp(cfg->stateFolder, "retroarch");
	char statesText[1400], statesDirText[1400];
	states_text(cfg, statesText, sizeof(statesText));
	states_dir_text(cfg, statesDirText, sizeof(statesDirText));
	for (int i = 0; i < nRows; ++i) {
		labels[i] = rowKind[i] == ROW_LANGUAGE ? lang_label(cfg)
		          : rowKind[i] == ROW_BIOS   ? bios_label(cfg)
		          : rowKind[i] == ROW_BANANA ? (banana_integration_on() ? N_("GBA DS in BananaOS's GBA list: On"): N_("GBA DS in BananaOS's GBA list: Off"))
		          : rowKind[i] == ROW_ES     ? (es_integration_on(cfg) ? N_("GBA DS in EmulationStation: On"): N_("GBA DS in EmulationStation: Off"))
		          : rowKind[i] == ROW_RESET  ? (confirmRow == i ? N_("Are you sure? Reset settings"): N_("Reset settings to defaults"))
		          : rowKind[i] == ROW_DELCFG ? (confirmRow == i ? N_("Are you sure? Delete them"): N_("Delete per-game settings"))
		          : rowKind[i] == ROW_STATES ? (shared ? N_("Save States: Shared with RetroArch"): N_("Save States: GBA DS's Own"))
		          : rowKind[i] == ROW_STATES_DIR ? "RetroArch's State Folder"
		          : rowKind[i] == ROW_PERF   ? (cfg->perfMax ? N_("Performance: Set to Max"): N_("Performance: Let the System Decide"))
		                                     : N_("Updates");
	}
	draw_settings_frame(scr, &g, sel, labels);
	if (sel >= nRows) return;
	int i = sel;
	const char* text = rowKind[i] == ROW_LANGUAGE ? N_("Automatic follows the system. Text that isn't translated yet stays in English.")
	                   : rowKind[i] == ROW_BANANA
	                       ? N_("Games picked in BananaOS's own Game Boy Advance list start in GBA DS when it has "
	                         "second-screen support for them. Restart BananaOS after switching this."): rowKind[i] == ROW_ES
	                       ? N_("Adds GBA DS to the emulators for Game Boy Advance games in EmulationStation. Pick it for one game "
	                         "(its Advanced Game Options > Emulator) or for all of them (the system's advanced settings). "
	                         "The other emulators keep working as before."): rowKind[i] == ROW_RESET || rowKind[i] == ROW_DELCFG ? NULL
	                   : rowKind[i] == ROW_STATES ? statesText
	                   : rowKind[i] == ROW_STATES_DIR ? statesDirText
	                   : rowKind[i] == ROW_PERF
	                       ? (cfg->perfMax ? N_("The processor, memory and graphics run at full speed while GBA DS is open: "
	                                         "the smoothest play, but the battery runs down faster and the device gets warmer."): N_("The system sets the speed, as for its other emulators. If games stutter or "
	                                         "the sound breaks up, try Set to Max."))
	                       : update_blurb();
	SDL_Color biosCol = C_DIM;
	if (rowKind[i] == ROW_BIOS) text = bios_note(cfg, &biosCol);
	/* a message from switching a row (settings_activate): in place of its text */
	bool msg = settingsMsg[0] && settingsMsgRow == i;
	if (msg) text = settingsMsg;
	/* (BananaOS: the trade-off set apart and in bold so it isn't missed) */
	if (rowKind[i] == ROW_BANANA && !msg)
		set_notes(scr, g.panel, g.back, text, C_DIM, TF_BODY,
		          "NOTE: Unsupported games will play in RetroArch, but without BananaOS's own "
		          "second screen (its game panel and companions).", C_TEXT, TF_BOLD);
	else
		draw_system_note(scr, &g, text, msg ? C_GOLD : biosCol);
}

static void settings_activate(Config* cfg, int row) {
	settingsMsg[0] = 0;
	settingsMsgRow = row;
	if (rowKind[row] == ROW_RESET || rowKind[row] == ROW_DELCFG) {
		system_action(cfg, row);
		return;
	}
	confirmRow = -1;
	if (rowKind[row] == ROW_LANGUAGE) {
		if (config_setting_step(cfg, lang_setting(), 1)) setting_changed(cfg, lang_setting());
	} else if (rowKind[row] == ROW_UPDATES) updatesOpen = true, updatesNotice = false, updatesSel = 0;
	else if (rowKind[row] == ROW_BANANA) banana_integration_set(!banana_integration_on(), settingsMsg, sizeof(settingsMsg));
	else if (rowKind[row] == ROW_ES) es_integration_set(cfg, !es_integration_on(cfg), settingsMsg, sizeof(settingsMsg));
	else if (rowKind[row] == ROW_BIOS) bios_switch(cfg);
	else if (rowKind[row] == ROW_STATES) states_switch(cfg);
	else if (rowKind[row] == ROW_STATES_DIR) states_dir_pick(cfg);
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
		const char* label = ids[i] == UB_CHECK  ? N_("Check now"): ids[i] == UB_AUTO   ? (u.autoOn ? N_("Automatic checks: On"): N_("Automatic checks: Off"))
		                  : ids[i] == UB_BACK   ? "Back"
		                  : ids[i] == UB_UPDATE ? N_("Update now"): ids[i] == UB_LATER  ? N_("Later"): ids[i] == UB_SKIP   ? N_("Skip this version"): "Cancel";
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
		snprintf(line, sizeof(line), tr("GBA DS v%s is out (this is v%s). Update now to download it (%.1f MB) and install "
		         "it: Android asks you to confirm. Your games, saves, states and settings are kept."), u.version,
		         GBADS_VERSION, u.size / 1048576.0);
#else
		snprintf(line, sizeof(line), tr("GBA DS v%s is out (this is v%s). Update now to download it (%.1f MB) and restart "
		         "into it; your games, saves, states and settings are kept."), u.version, GBADS_VERSION, u.size / 1048576.0);
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
			         tr("Confirm the update to v%s in Android's installer. Android closes GBA DS while it installs; open it "
			         "again afterwards. If Android asks first, allow GBA DS to install apps."), u.version);
			draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
			break;
		}
		const char* what = u.state == UPD_DOWNLOADING ? N_("Downloading"): N_("Installing");
#else
		const char* what = u.state == UPD_DOWNLOADING ? N_("Downloading"): u.state == UPD_INSTALLING ? N_("Installing"): N_("Installed");
#endif
		snprintf(line, sizeof(line), u.state == UPD_DONE ? tr("%s v%s. Restarting..."): "%s v%s...", what, u.version);
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
		snprintf(line, sizeof(line), tr("This is GBA DS v%s."), GBADS_VERSION);
		if (u.state == UPD_CHECKING) snprintf(line + strlen(line), sizeof(line) - strlen(line), tr(" Checking for a new version..."));
		else if (u.state == UPD_LATEST) snprintf(line + strlen(line), sizeof(line) - strlen(line), tr(" It's the newest version."));
		y = draw_wrapped(scr, tx, y, tw, px, line, C_TEXT);
		if (u.state == UPD_ERROR) y = draw_wrapped(scr, tx, y + px * 3, tw, px, u.error, C_GOLD);
		if (u.lastCheck > 0) {
			time_t t = (time_t) u.lastCheck;
			char when[64];
			strftime(when, sizeof(when), "%e %B %Y, %H:%M", localtime(&t));
			snprintf(line, sizeof(line), tr("Last checked %s."), when[0] == ' ' ? when + 1 : when);
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
/* Starting a game whose logo isn't ready yet: only its own is made first
 * (another game's under way is called off, logo_yield), the rest wait until
 * the game list is back. */
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
	int max = g.m * 2 + g.barH + rows * g.rowH - g.gap - g.listH;
	if (max < 0) max = 0;
	return scroll < 0 ? 0 : scroll > max ? max : scroll;
}

static int keep_visible(int scr, int sel, int scroll) {
	ListGeom g = list_geom(scr);
	int tileTop = g.barH + (sel / g.cols) * g.rowH;
	int top = sel < g.cols ? 0 : tileTop; /* (the first row brings the sort bar back into view) */
	if (top < scroll) scroll = top;
	if (tileTop + g.tileH + g.m * 2 > scroll + g.listH) scroll = tileTop + g.tileH + g.m * 2 - g.listH;
	return clamp_scroll(scr, scroll);
}

/* ---- the first-run language question (issue #92) ----
 * A first install asks which language the menus are in before the game list
 * (or, launched straight into a game, before the first pause menu): a
 * system can report a language its player doesn't read (the RG DS Plus's
 * stock OS says Chinese), and then the Language row is in that language too.
 * The top screen says "Language" in every language; the touch screen lists
 * Automatic (with the system's language), English and every catalogue by its
 * own name. A picks (both screens redraw in it at once); A on the language
 * already picked, START or Continue keeps it; B puts back the one it opened
 * with. language_asked = 1 in prefs.cfg once kept.
 * Which builds ask (the owner's rule, Nov 25, 2026): releases (a version
 * without -dev, the APK included) and builds made with GBADS_LANG_ASK=1 (the
 * main session's final dev build); never other test builds, nor the web
 * build (the web player and State Saver keep no settings for a visitor who
 * doesn't allow storage, so they'd ask every time). GBADS_LANG_ASK=1 in the
 * environment turns it on for a test (regress.py's cases). */
bool library_language_ask_built(void) {
#if defined(__wasi__)
	return false;
#else
	const char* env = getenv("GBADS_LANG_ASK");
	if (env && env[0]) return env[0] == '1';
#ifdef GBADS_LANG_ASK
	return GBADS_LANG_ASK != 0;
#else
	return !strstr(GBADS_VERSION, "-dev");
#endif
#endif
}

#define LA_MAX 24
typedef struct {
	int n;                    /* items: 0 Automatic, then English and the catalogues; n is Continue */
	const char* code[LA_MAX];
	int lang[LA_MAX];         /* index into i18nLangs (-1: Automatic, English) */
	int chosen, sel;
} LangAsk;

static SDL_Rect la_rect(int scr, const LangAsk* a, int i) {
	int px = (int) ui_px(scr);
	SDL_Rect p = set_panel(scr);
	int top = p.y + px * 22, gap = px * 2;
	int backW = px * 40, backH = px * 14;
	if (i >= a->n) return (SDL_Rect) { p.x + p.w - px * 6 - backW, p.y + p.h - px * 5 - backH, backW, backH };
	int rows = 1 + (a->n - 1 + 1) / 2;
	int avail = p.y + p.h - px * 5 - backH - px * 4 - top;
	int h = SDL_min((int) (ui_px(scr) * 19), avail / rows - gap); /* (the unit unrounded: the RG DS's 640x480 screens have 2.67) */
	int w = p.w - px * 12, half = (w - px * 3) / 2;
	if (i == 0) return (SDL_Rect) { p.x + px * 6, top, w, h };
	int row = 1 + (i - 1) / 2, col = (i - 1) % 2;
	return (SDL_Rect) { p.x + px * 6 + col * (half + px * 3), top + row * (h + gap), half, h };
}

/* the language a choice puts in use, as a code ("en" for Automatic on a system none of them matches) */
static const char* la_code_in_use(const LangAsk* a, int i) {
	if (i != 0) return a->code[i];
	int s = i18n_system_index();
	return s >= 0 ? i18nLangs[s].code : "en";
}

static void la_apply(const LangAsk* a) {
	i18n_set(a->code[a->chosen]);
	ui_language_changed();
}

/* "Language" in every language, a line at a time across the screen; the one in use brighter */
static int la_words(int scr, int y, int cap, int maxW, bool draw) {
	const char* words[LA_MAX];
	int nw = 0;
	words[nw++] = "Language";
	for (int i = 0; i < i18nLangCount && nw < LA_MAX; ++i) {
		if (i18nLangs[i].status == I18N_TEST) continue;
		const char* w = i18n_tr_in(i18nLangs[i].code, "Language");
		bool dup = false;
		for (int k = 0; k < nw; ++k) dup |= !strcmp(words[k], w);
		if (!dup) words[nw++] = w;
	}
	SDL_Rect r = screens[scr].rect;
	int px = (int) ui_px(scr), gap = px * 8, lineH = cap * 2;
	const char* here = tr("Language");
	int k = 0;
	while (k < nw) {
		int end = k, lw = 0;
		while (end < nw) {
			int w = ui_font_width_as_is(TF_BOLD, cap, words[end]) + (end > k ? gap : 0);
			if (end > k && lw + w > maxW) break;
			lw += w, ++end;
		}
		if (draw) {
			int x = r.w / 2 - lw / 2;
			for (int j = k; j < end; ++j) {
				bool on = !strcmp(words[j], here);
				ui_font_text_as_is(scr, TF_BOLD, x, y + lineH / 2, cap, words[j], on ? C_TEXT : (SDL_Color) { 93, 90, 114, 255 }, -1);
				x += ui_font_width_as_is(TF_BOLD, cap, words[j]) + gap;
			}
		}
		y += lineH, k = end;
	}
	return y;
}

/* a line set smaller until it fits maxW, centred */
static void la_line(int scr, int font, int cx, int cy, int cap, int maxW, const char* text, SDL_Color c, bool asIs) {
	int w = asIs ? ui_font_width_as_is(font, cap, text) : ui_font_width(font, cap, text);
	if (w > maxW && w > 0) cap = cap * maxW / w;
	if (asIs) ui_font_text_as_is(scr, font, cx, cy, cap, text, c, 0);
	else ui_font_text(scr, font, cx, cy, cap, text, c, 0);
}

static void la_draw_top(int scr) {
	SDL_Rect r = screens[scr].rect;
	int px = (int) ui_px(scr), m = px * 6;
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 0, 0, 0, 255 });
	load_brand();
	draw_brand(scr, BRAND_LOGO, m, m, px * 21);
	int maxW = r.w - px * 32;
	/* the words and the lines, centred between the logo and the buttons */
	bool en = !strcmp(i18n_code(), "en");
	int blockH = la_words(scr, 0, px * 9, maxW, false) + px * 8 + px * (en ? 14 : 36);
	int y0 = px * 30 + (r.h - px * 16 - px * 30 - blockH) / 2;
	int y = la_words(scr, y0, px * 9, maxW, true) + px * 8;
	la_line(scr, TF_BOLD, r.w / 2, y, px * 6, r.w - px * 16, N_("Choose the language for GBA DS's menus."), C_TEXT, false);
	la_line(scr, TF_BODY, r.w / 2, y + px * 11, px * 4, r.w - px * 16, N_("You can change it later in Settings > System."), C_DIM, false);
	if (!en) {
		/* and in English, which most players read some of */
		la_line(scr, TF_BODY, r.w / 2, y + px * 25, px * 4, r.w - px * 16, "Choose the language for GBA DS's menus.", (SDL_Color) { 120, 117, 142, 255 }, true);
		la_line(scr, TF_BODY, r.w / 2, y + px * 33, px * 4, r.w - px * 16, "You can change it later in Settings > System.", (SDL_Color) { 96, 94, 116, 255 }, true);
	}
	/* the buttons: A picks, START keeps it */
	int ty = r.h - px * 11, d = px * 10, bx = m;
	int gy = ty + px * 7 / 2 - d / 2, cap = px * 5, cy = gy + d / 2, wordW = px * 18;
	bx += draw_brand(scr, BRAND_BTN_A, bx, gy, d) + px * 2;
	int ww = SDL_max(wordW, ui_label_width(scr, cap, "Choose"));
	ui_label(scr, bx + ww / 2, cy, cap, "Choose", C_TEXT);
	bx += ww + px * 8;
	int pw = ui_font_width_as_is(TF_BOLD, px * 3, "START") + px * 5;
	ui_fill_round(scr, (SDL_Rect) { bx, cy - px * 4, pw, px * 8 }, C_TEXT, px * 4);
	ui_font_text_as_is(scr, TF_BOLD, bx + pw / 2, cy, px * 3, "START", (SDL_Color) { 16, 16, 26, 255 }, 0);
	bx += pw + px * 2;
	ww = SDL_max(wordW, ui_label_width(scr, cap, "Continue"));
	ui_label(scr, bx + ww / 2, cy, cap, "Continue", C_TEXT);
	int vpx = SDL_max(1, px / 2);
	const char* v = "V" GBADS_VERSION;
	ui_text(scr, r.w - m - ui_text_width(v, vpx), cy - vpx * 7 / 2, vpx, v, C_DIM);
}

static void la_draw_list(int scr, const LangAsk* a, bool oneScreen) {
	int px = (int) ui_px(scr);
	SDL_Rect r = screens[scr].rect;
	SDL_Rect p = set_panel(scr);
	ui_fill(scr, (SDL_Rect) { 0, 0, r.w, r.h }, (SDL_Color) { 7, 6, 12, 255 });
	ui_fill_round(scr, p, C_HEADER, px * 4);
	if (oneScreen) {
		/* (no other screen for "Language" in every language: it goes in the heading's strip, smaller) */
		la_words(scr, p.y + px * 2, px * 4, p.w - px * 12, true);
	} else {
		draw_set_heading(scr, p, "Language", -1);
	}
	for (int i = 0; i <= a->n; ++i) {
		SDL_Rect b = la_rect(scr, a, i);
		bool sel = i == a->sel, lit = i == a->chosen;
		ui_key(scr, b, px * 3, sel ? KEY_SEL : lit ? KEY_LIT : KEY_PLAIN);
		int cy = b.y + b.h / 2;
		if (i == a->n) {
			ui_label(scr, b.x + b.w / 2, cy, px * 5, "Continue", C_TEXT);
		} else if (i == 0) {
			int s = i18n_system_index();
			char sys[120];
			snprintf(sys, sizeof(sys), tr("System: %s"), s >= 0 ? i18nLangs[s].autonym : "English");
			ui_font_text(scr, TF_BOLD, b.x + px * 6, cy, px * 5, "Automatic", C_TEXT, -1);
			ui_font_text_as_is(scr, TF_BODY, b.x + b.w - px * 6, cy, px * 4, sys, sel || lit ? C_TEXT : C_DIM, 1);
		} else {
			const I18nLang* L = a->lang[i] >= 0 ? &i18nLangs[a->lang[i]] : NULL;
			const char* name = L ? L->autonym : "English";
			int room = b.w - px * 12, cap = px * 5;
			if (L && L->status == I18N_MACHINE) {
				const char* note = i18n_tr_in(L->code, "machine translated");
				ui_font_text_as_is(scr, TF_BOLD, b.x + px * 6, b.y + b.h * 35 / 100, cap, name, C_TEXT, -1);
				int nc = px * 7 / 2, nw = ui_font_width_as_is(TF_BOLD, nc, note);
				if (nw > room) nc = nc * room / nw;
				ui_font_text_as_is(scr, TF_BOLD, b.x + px * 6, b.y + b.h * 72 / 100, nc, note, sel || lit ? C_TEXT : C_DIM, -1);
			} else {
				ui_font_text_as_is(scr, TF_BOLD, b.x + px * 6, cy, cap, name, C_TEXT, -1);
			}
		}
	}
	set_notes(scr, p, la_rect(scr, a, a->n), "Automatic follows the system. Text that isn't translated yet stays in English.", C_DIM, TF_BODY, NULL, C_DIM, TF_BODY);
}

static void la_move(LangAsk* a, int dx, int dy) {
	int i = a->sel, n = a->n;
	if (dy) {
		if (i == n) i = dy < 0 ? n - 1 : 0;
		else if (i == 0) i = dy > 0 ? 1 : n;
		else {
			int j = i + dy * 2;
			if (j < 1) j = 0;
			else if (j >= n) j = dy > 0 ? n : n - 1;
			i = j;
		}
	} else if (dx && i >= 1 && i < n) {
		int j = (i - 1) % 2 == 0 ? i + 1 : i - 1;
		if (j >= 1 && j < n) i = j;
	} else if (dx && i == n && dx < 0) {
		i = n - 1;
	}
	a->sel = i;
}

/* Asks, until the player keeps a language (prefs.cfg: language, language_asked = 1). */
void library_language_ask(Config* cfg) {
	LangAsk a;
	memset(&a, 0, sizeof(a));
	a.code[a.n] = "auto", a.lang[a.n++] = -1;
	a.code[a.n] = "en", a.lang[a.n++] = -1;
	for (int i = 0; i < i18nLangCount && a.n < LA_MAX - 1; ++i) {
		if (i18nLangs[i].status == I18N_TEST) continue;
		a.code[a.n] = i18nLangs[i].code, a.lang[a.n++] = i;
	}
	int start = 0;
	for (int i = 0; i < a.n; ++i)
		if (!strcmp(a.code[i], cfg->language)) start = i;
	a.chosen = a.sel = start;
	fprintf(stderr, "language: asking (first run), now %s (%s)\n", cfg->language, i18n_code());

	bool done = false, waitRelease = true;
	Uint32 releasedAt = 0;
	enum UiFace faceWas = ui_face_now();
	ui_face(UI_FACE_MENU); /* (GEN-19: a menu, over the whole of one display) */
	while (!done) {
		if (!ui_events()) break;
		ui_tick();
		ui_read_buttons();
		Uint32 now = SDL_GetTicks();
		int scr = ui_touch_screen(), top = ui_other_screen();
		bool oneScreen = top == scr;
		int act = -1; /* an item pressed (n: Continue) */
		Pointer pt;
		while (ui_next_pointer(&pt)) {
			if (pt.screen != scr || pt.kind != 1) continue;
			SDL_Point q = { pt.x, pt.y };
			for (int i = 0; i <= a.n; ++i) {
				SDL_Rect b = la_rect(scr, &a, i);
				if (SDL_PointInRect(&q, &b)) a.sel = i, act = i;
			}
		}
		if (waitRelease) {
			bool any = false;
			for (int b = 0; b < B_COUNT; ++b) any |= btnNow[b];
			if (any) releasedAt = 0;
			else if (!releasedAt) releasedAt = now;
			if (releasedAt && now - releasedAt > 150) waitRelease = false;
		} else {
			if (btn_pressed(B_UP)) la_move(&a, 0, -1);
			if (btn_pressed(B_DOWN)) la_move(&a, 0, 1);
			if (btn_pressed(B_LEFT)) la_move(&a, -1, 0);
			if (btn_pressed(B_RIGHT)) la_move(&a, 1, 0);
			if (btn_pressed(B_A)) act = a.sel;
			if (btn_pressed(B_START)) act = a.n;
			if (btn_pressed(B_B) && a.chosen != start) a.chosen = a.sel = start, la_apply(&a);
		}
		if (act >= 0) {
			if (act == a.n || act == a.chosen) {
				done = true;
			} else {
				a.chosen = act;
				la_apply(&a);
			}
		}
		ui_clear();
		if (!oneScreen) la_draw_top(top);
		la_draw_list(scr, &a, oneScreen);
		ui_present();
		SDL_Delay(15);
	}
	ui_face(faceWas);
	if (!done) { /* (the app closed: asked again next time) */
		i18n_set(cfg->language);
		ui_language_changed();
		return;
	}
	snprintf(cfg->language, sizeof(cfg->language), "%s", a.code[a.chosen]);
	cfg->languageAsked = true;
	char path[1100];
	mkdirs(cfg->stateDir);
	snprintf(path, sizeof(path), "%s/prefs.cfg", cfg->stateDir);
	const char* kv[2][2] = { { "language", cfg->language }, { "language_asked", "1" } };
	config_kv_update(path, "# written by gba-ds's menus; overrides gba-ds.cfg", 2, kv);
	fprintf(stderr, "language: kept %s (%s)\n", cfg->language, la_code_in_use(&a, a.chosen));
	/* the press that kept it shouldn't do anything after */
	ui_read_buttons();
	ui_read_buttons();
}

bool library_run(Config* cfg, char* outPath, size_t outSize) {
	if (!cfg->languageAsked) library_language_ask(cfg); /* (a first install, issue #92) */
	storage_check(cfg);
	sortMode = !strcmp(cfg->librarySort, "recent") ? 1 : 0;
	barFocus = false;
	barSel = sortMode;

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
		sort_games(cfg); /* (a game just played comes first in Recently played) */
		free_tiles();
	}
	thumbFor = -2;

	int sel = nGames ? 0 : -1;
	for (int i = 0; i < nGames; ++i) {
		if (!strcmp(games[i].path, last)) sel = i;
	}
	viewCols = !strcmp(cfg->libraryView, "grid3") ? 3 : !strcmp(cfg->libraryView, "grid") ? 2 : 1;
	ui_face(UI_FACE_LIST); /* (GEN-19: back from a game, as when the app started) */
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
	ui_file_crc_async(true); /* (no whole-ROM reads while the list draws) */
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
				retry_logos_of_mods();
			}
		}
		static unsigned fileCrcSeen;
		if (logo_generation() != logoGen) { /* a logo finished extracting */
			logoGen = logo_generation();
			fileCrcSeen = ui_file_crc_generation();
			thumbFor = -2;
			free_tiles();
		} else if (ui_file_crc_generation() != fileCrcSeen) { /* a ROM file's checksum was worked out (its logo's name) */
			fileCrcSeen = ui_file_crc_generation();
			retry_tiles_without_logo();
		}
		if (!ui_events()) {
			quit = true;
			break;
		}
		ui_tick();
		ui_read_buttons();
		ra_idle(); /* sign-in answers (at start-up, or from Settings) */
		{
			/* GEN-19: on one display, Settings and its screens (and the update
			 * notice) take the whole of it; the list and the preview come back
			 * when they close */
			bool menu = settingsOpen || updatesOpen;
#ifdef __ANDROID__
			menu |= foldersOpen;
#endif
			int was = nScreens;
			SDL_Rect wasR = screens[ui_touch_screen()].rect;
			ui_face(menu ? UI_FACE_MENU : UI_FACE_LIST);
			SDL_Rect nowR = screens[ui_touch_screen()].rect;
			if (nScreens != was || !SDL_RectEquals(&wasR, &nowR)) {
				thumbFor = -2; /* (the preview's size) */
				scroll = sel >= 0 ? keep_visible(ui_touch_screen(), sel, 0) : 0;
			}
		}
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
			if (!logo_busy(games[waiting].code)) {
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
				snprintf(settingsMsg, sizeof(settingsMsg), trn("%d game found.", "%d games found.", nGames), nGames);
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
			if (settingsOpen && setPage == SP_SYSTEM) settingsSel = 0; /* (just opened) */
			pressed = false;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (settingsOpen && setPage == SP_HOME) draw_set_home(ls, setSel);
			else if (settingsOpen && setPage < SP_SYSTEM) draw_set_page(cfg, ls, setPage, setSel);
			else if (settingsOpen) draw_settings(cfg, ls, settingsSel); /* System, just opened: its panel from the first frame */
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
				snprintf(settingsMsg, sizeof(settingsMsg), trn("%d game found.", "%d games found.", nGames), nGames);
			}
			/* rows sharing a slot (Save States | RetroArch's State Folder, Reset | Delete): Left / Right
			 * between them, Up / Down to the slot above or below (Back's is the last) */
			{
				int sl = row_slot(settingsSel), nSl = row_slot(nRows) + 1;
				if (settingsSel < nRows && (btn_pressed(B_LEFT) || btn_pressed(B_RIGHT))) {
					int other = row_second(settingsSel) ? settingsSel - 1 : (settingsSel + 1 < nRows && row_second(settingsSel + 1) ? settingsSel + 1 : settingsSel);
					settingsSel = other;
				}
				int dy = btn_pressed(B_DOWN) ? 1 : btn_pressed(B_UP) ? -1 : 0;
				if (dy) {
					int want = (sl + dy + nSl) % nSl, pick = nRows;
					for (int i = 0; i < nRows; ++i)
						if (row_slot(i) == want && !row_second(i)) { pick = i; break; }
					settingsSel = pick;
				}
			}
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

		if (barFocus) {
			/* on the sort bar: Left / Right pick a button, A applies it, Down goes back to the games */
			if (btn_pressed(B_LEFT)) barSel = 0;
			if (btn_pressed(B_RIGHT)) barSel = 1;
			if (btn_pressed(B_DOWN)) barFocus = false;
			if (btn_pressed(B_A) || btn_pressed(B_START)) sel = set_sort(cfg, barSel, sel);
		} else if (nGames) {
			if (btn_pressed(B_Y) && sel >= 0) {
				/* Y: the selected game a favorite, or not any more */
				sel = toggle_fav(cfg, sel);
				scroll = keep_visible(ls, sel, scroll);
			}
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
			if (btn_pressed(B_UP) && sel < g.cols) {
				/* Up from the first row: the sort bar above it */
				barFocus = true, barSel = sortMode, dir = 0;
				scroll = 0;
			}
			if (dir) {
				sel += dir;
				if (sel < 0) sel = 0;
				if (sel >= nGames) sel = nGames - 1;
				scroll = keep_visible(ls, sel, scroll);
			}
			if (!barFocus && (btn_pressed(B_A) || btn_pressed(B_START))) chosen = sel;
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
				barFocus = false; /* (touching takes the D-pad's place off the bar) */
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
					SDL_Point pt = { p.x, p.y };
					bool used = false;
					for (int w = 0; w < 2 && nGames && !used; ++w) {
						SDL_Rect b = sort_button(ls, &g, scroll, w);
						if (SDL_PointInRect(&pt, &b)) {
							used = true;
							barFocus = false;
							sel = set_sort(cfg, w, sel);
							scroll = keep_visible(ls, sel, scroll);
						}
					}
					for (int i = 0; i < nGames && !used; ++i) {
						SDL_Rect t = tile_rect(&g, i, scroll);
						if (p.x >= t.x && p.x < t.x + t.w && p.y >= t.y && p.y < t.y + t.h) {
							barFocus = false;
							SDL_Rect st = star_rect(ls, &t);
							if (SDL_PointInRect(&pt, &st)) {
								/* the star: a favorite, or not (the list is made again) */
								sel = toggle_fav(cfg, i);
								scroll = keep_visible(ls, sel, scroll);
							} else {
								sel = i;
								chosen = i;
							}
							used = true;
						}
					}
				}
			}
		}

		if (chosen >= 0) {
			/* (its own jobs first, and nothing else started meanwhile) */
			logo_pause(true);
			logo_front(games[chosen].code, true);
			logo_yield(games[chosen].code); /* (another game's logo under way: made later) */
			if (logo_busy(games[chosen].code)) waiting = chosen, chosen = -1;
		}

		ui_clear();
		draw_list(cfg, ls, sel, scroll);
		if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
		ui_present();
		SDL_Delay(15);
	}
	ui_file_crc_async(false);
	free_thumbs();
	free_brand();
	free_tiles();
	if (chosen < 0) return false;
	snprintf(outPath, outSize, "%s", games[chosen].path);
	/* (Recently played) */
	mark_find(outPath, true)->played = (long long) time(NULL);
	marks_save(cfg);
	f = fopen(lastFile, "w");
	if (f) {
		fprintf(f, "%s\n", outPath);
		fclose(f);
	}
	return true;
}

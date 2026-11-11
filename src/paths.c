#include "paths.h"

#include "emu.h"
#include "patch.h"
#include "profiles.h"

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utime.h>

bool file_exists(const char* path) {
	struct stat st;
	return stat(path, &st) == 0;
}

void mkdirs(const char* path) {
	char tmp[1024];
	snprintf(tmp, sizeof(tmp), "%s", path);
	for (char* p = tmp + 1; *p; ++p) {
		if (*p == '/') {
			*p = 0;
			mkdir(tmp, 0755);
			*p = '/';
		}
	}
	mkdir(tmp, 0755);
}

bool copy_file(const char* from, const char* to) {
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	char buf[8192];
	size_t n;
	while ((n = fread(buf, 1, sizeof(buf), in)) > 0) fwrite(buf, 1, n, out);
	fclose(in);
	fclose(out);
	return true;
}

/* two files with the same bytes */
static bool same_file_data(const char* a, const char* b) {
	FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
	bool same = fa && fb;
	char ba[8192], bb[8192];
	while (same) {
		size_t na = fread(ba, 1, sizeof(ba), fa), nb = fread(bb, 1, sizeof(bb), fb);
		if (na != nb || memcmp(ba, bb, na)) same = false;
		if (!na) break;
	}
	if (fa) fclose(fa);
	if (fb) fclose(fb);
	return same;
}

void game_paths(const Config* cfg, const char* rom, GamePaths* o) {
	char a[1024], b[1024];
	snprintf(a, sizeof(a), "%s", rom);
	snprintf(b, sizeof(b), "%s", rom);
	const char* romDir = dirname(a);
	snprintf(o->base, sizeof(o->base), "%s", basename(b));
	char* dot = strrchr(o->base, '.');
	if (dot) *dot = 0;

	const char* saveDir = cfg->saveDir[0] ? cfg->saveDir : romDir;
	mkdirs(saveDir);
	mkdirs(cfg->stateDir);
	const char* ext = cfg->saveExt[0] ? cfg->saveExt : "sav";
	snprintf(o->sav, sizeof(o->sav), "%s/%s.%s", saveDir, o->base, ext);
	snprintf(o->quick, sizeof(o->quick), "%s/%s.quick.state", cfg->stateDir, o->base);
	for (int i = 0; i < 10; ++i) snprintf(o->slot[i], sizeof(o->slot[i]), "%s/%s.slot%d.state", cfg->stateDir, o->base, i);
	snprintf(o->autoState, sizeof(o->autoState), "%s/%s.auto.state", cfg->stateDir, o->base);
	snprintf(o->thumb, sizeof(o->thumb), "%s/%s.game.bmp", cfg->stateDir, o->base);
	snprintf(o->mapThumb, sizeof(o->mapThumb), "%s/%s.map.bmp", cfg->stateDir, o->base);
	snprintf(o->cheats, sizeof(o->cheats), "%s/%s.cheats", cfg->stateDir, o->base);
	snprintf(o->patches, sizeof(o->patches), "%s/%s.patches", cfg->stateDir, o->base);

	/* GBA DS's own save from where it kept them before (save_from: on
	 * BananaOS, /data/saves/<game>.sav, now the GBA list's own
	 * /data/saves/gba/<game>.srm, shared with its emulators): carried over
	 * if there's no save yet, or if it's newer than the one there (that one
	 * kept as .bak, as BananaOS does). Left where it was. */
	if (cfg->saveFrom[0]) {
		char old[1100], bak[1110];
		snprintf(old, sizeof(old), "%s/%s.sav", cfg->saveFrom, o->base);
		struct stat so, sn;
		if (strcmp(old, o->sav) && stat(old, &so) == 0 && so.st_size > 0) {
			if (stat(o->sav, &sn) != 0) {
				if (copy_file(old, o->sav)) fprintf(stderr, "save carried over: %s -> %s\n", old, o->sav);
			} else if (so.st_mtime > sn.st_mtime && !same_file_data(old, o->sav)) {
				snprintf(bak, sizeof(bak), "%s.bak", o->sav);
				if (copy_file(o->sav, bak) && copy_file(old, o->sav))
					fprintf(stderr, "save carried over (newer): %s -> %s, the one there kept as %s\n", old, o->sav, bak);
			}
		}
	}
	/* First run: carry over a battery save in the other form beside it
	 * (RetroArch's .srm for a .sav, or the other way round) */
	if (!file_exists(o->sav)) {
		char other[1100];
		snprintf(other, sizeof(other), "%s/%s.%s", saveDir, o->base, strcmp(ext, "srm") ? "srm" : "sav");
		if (copy_file(other, o->sav)) fprintf(stderr, "imported %s -> %s\n", other, o->sav);
	}
	/* Saves kept in a folder of their own (save_dir; Android's Settings >
	 * Folders), and none there yet: the one next to the ROM, where they
	 * were kept before, in either form, is copied over (and left there) */
	if (!file_exists(o->sav) && cfg->saveDir[0] && strcmp(saveDir, romDir)) {
		char beside[1100];
		snprintf(beside, sizeof(beside), "%s/%s.%s", romDir, o->base, ext);
		if (!file_exists(beside)) snprintf(beside, sizeof(beside), "%s/%s.%s", romDir, o->base, strcmp(ext, "srm") ? "srm" : "sav");
		if (copy_file(beside, o->sav)) fprintf(stderr, "save carried over from beside the ROM: %s -> %s\n", beside, o->sav);
	}
	/* Before v1.4.5 there was one save state, <game>.state, used by the
	 * quick bar and the pause menu alike: it becomes the quick state, and
	 * slot 0 gets a copy, so it's where a player looks either way (with
	 * its RetroAchievements progress, .ra, beside each) */
	if (!file_exists(o->quick)) {
		char old[1100], from[1110], to[1110];
		snprintf(old, sizeof(old), "%s/%s.state", cfg->stateDir, o->base);
		if (file_exists(old)) {
			snprintf(from, sizeof(from), "%s.ra", old);
			if (!file_exists(o->slot[0])) {
				/* (with the original's time: the menu shows when a slot was saved) */
				struct stat st;
				if (copy_file(old, o->slot[0]) && stat(old, &st) == 0) {
					struct utimbuf ut = { st.st_atime, st.st_mtime };
					utime(o->slot[0], &ut);
				}
				snprintf(to, sizeof(to), "%s.ra", o->slot[0]);
				if (file_exists(from)) copy_file(from, to);
			}
			if (rename(old, o->quick) != 0 && copy_file(old, o->quick)) remove(old);
			snprintf(to, sizeof(to), "%s.ra", o->quick);
			if (file_exists(from) && rename(from, to) != 0 && copy_file(from, to)) remove(from);
			fprintf(stderr, "save state %s -> %s (and slot 0)\n", old, o->quick);
		} else {
			/* older builds (when the app was called gbadual) kept it next to the ROM */
			snprintf(old, sizeof(old), "%s/%s.gbadual.ss0", saveDir, o->base);
			if (file_exists(old) && rename(old, o->quick) != 0) copy_file(old, o->quick);
		}
	}
}

/* ------------------------------------------------ patches (GEN-9) */

void patch_choice_load(const char* path, PatchChoice* c) {
	memset(c, 0, sizeof(*c));
	c->sepSave = c->sepStates = true;
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[512];
	while (fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "on ", 3)) {
			/* (one line per patch, in the order they're applied) */
			if (c->nOn < PATCH_ON_MAX && line[3] && patch_choice_find(c, line + 3) < 0)
				snprintf(c->on[c->nOn++], sizeof(c->on[0]), "%s", line + 3);
		} else if (!strncmp(line, "separate_save ", 14)) c->sepSave = line[14] == '1';
		else if (!strncmp(line, "separate_states ", 16)) c->sepStates = line[16] == '1';
	}
	fclose(f);
}

void patch_choice_save(const char* path, const PatchChoice* c) {
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < c->nOn; ++i) fprintf(f, "on %s\n", c->on[i]);
	fprintf(f, "separate_save %d\nseparate_states %d\n", c->sepSave, c->sepStates);
	fclose(f);
}

int patch_choice_find(const PatchChoice* c, const char* file) {
	for (int i = 0; i < c->nOn; ++i)
		if (!strcmp(c->on[i], file)) return i;
	return -1;
}

bool patch_choice_same(const PatchChoice* a, const PatchChoice* b) {
	if (a->nOn != b->nOn) return false;
	for (int i = 0; i < a->nOn; ++i)
		if (strcmp(a->on[i], b->on[i])) return false;
	return true;
}

/* a folder name from a header's title: SD cards' file systems take no : / \ * ? " < > | */
static void safe_name(char* s) {
	for (char* p = s; *p; ++p) {
		if (strchr(":/\\*?\"<>|", *p) || (unsigned char) *p < 32) *p = '_';
	}
}

void patch_folder(const Config* cfg, const char* rom, const char* code, char* out, size_t size, bool make) {
	char name[300];
	if (!profile_release_name(code, name, sizeof(name))) {
		uint8_t h[0xB0];
		char title[13] = "";
		if (emu_rom_header(rom, h, sizeof(h))) {
			memcpy(title, h + 0xA0, 12);
			title[12] = 0;
		}
		for (int i = (int) strlen(title) - 1; i >= 0 && (title[i] == ' ' || !title[i]); --i) title[i] = 0;
		snprintf(name, sizeof(name), "%s (%.4s)", title[0] ? title : "GBA", code);
	}
	safe_name(name);
	snprintf(out, size, "%s/%s", cfg->patchDir, name);
	if (make) mkdirs(out);
}

void patch_title(const char* file, char* out, size_t size) {
	const char* slash = strrchr(file, '/');
	snprintf(out, size, "%s", slash ? slash + 1 : file);
	char* dot = strrchr(out, '.');
	if (dot && dot != out) *dot = 0;
}

int patch_plan(const Config* cfg, const char* rom, const char* code, const PatchChoice* c, char files[][1024], int max) {
	int n = 0;
	if (n < max && patch_find(rom, files[n], 1024)) ++n; /* beside the ROM: always on, first */
	char dir[1024] = "";
	if (c && c->nOn) patch_folder(cfg, rom, code, dir, sizeof(dir), false);
	for (int i = 0; c && i < c->nOn && n < max; ++i) {
		snprintf(files[n], 1024, "%s/%s", dir, c->on[i]);
		if (file_exists(files[n])) ++n;
	}
	return n;
}

static int title_cmp(const void* a, const void* b) {
	return strcasecmp((const char*) a, (const char*) b);
}

void game_paths_patched(const Config* cfg, GamePaths* o, const PatchChoice* c) {
	if (!c->nOn || (!c->sepSave && !c->sepStates)) return;
	char t[PATCH_ON_MAX][256], base[600];
	for (int i = 0; i < c->nOn; ++i) patch_title(c->on[i], t[i], sizeof(t[0]));
	qsort(t, (size_t) c->nOn, sizeof(t[0]), title_cmp);
	int l = snprintf(base, sizeof(base), "%s", o->base);
	for (int i = 0; i < c->nOn && l < (int) sizeof(base); ++i) l += snprintf(base + l, sizeof(base) - (size_t) l, " + %s", t[i]);
	if (c->sepSave) {
		char dir[1024];
		snprintf(dir, sizeof(dir), "%s", o->sav);
		char* slash = strrchr(dir, '/');
		const char* ext = strrchr(o->sav, '.');
		if (slash && ext) {
			*slash = 0;
			char ex[16];
			snprintf(ex, sizeof(ex), "%s", ext);
			snprintf(o->sav, sizeof(o->sav), "%s/%s%s", dir, base, ex);
		}
	}
	if (c->sepStates) {
		snprintf(o->quick, sizeof(o->quick), "%s/%s.quick.state", cfg->stateDir, base);
		for (int i = 0; i < 10; ++i) snprintf(o->slot[i], sizeof(o->slot[i]), "%s/%s.slot%d.state", cfg->stateDir, base, i);
		snprintf(o->autoState, sizeof(o->autoState), "%s/%s.auto.state", cfg->stateDir, base);
	}
}

/* ------------------------------------------------------------ key = value files */

/* the value in a "key = value" line, or NULL */
static const char* kv_match(const char* line, const char* key) {
	while (isspace((unsigned char) *line)) ++line;
	size_t k = strlen(key);
	if (strncmp(line, key, k)) return NULL;
	line += k;
	while (*line == ' ' || *line == '\t') ++line;
	if (*line != '=') return NULL;
	++line;
	while (*line == ' ' || *line == '\t') ++line;
	return line;
}

bool kv_get(const char* file, const char* key, char* out, size_t n) {
	FILE* f = fopen(file, "r");
	if (!f) return false;
	char line[1200];
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		const char* v = kv_match(line, key);
		if (!v) continue;
		snprintf(out, n, "%s", v);
		size_t len = strlen(out);
		while (len && isspace((unsigned char) out[len - 1])) out[--len] = 0;
		found = true;
	}
	fclose(f);
	return found;
}

bool kv_set(const char* file, const char* key, const char* val, const char* header) {
	char text[16384] = "";
	size_t len = 0;
	bool done = false;
	FILE* f = fopen(file, "r");
	if (f) {
		char line[1200];
		while (fgets(line, sizeof(line), f)) {
			if (kv_match(line, key)) {
				if (done) continue;
				done = true;
				len += snprintf(text + len, sizeof(text) - len, "%s = %s\n", key, val);
			} else {
				len += snprintf(text + len, sizeof(text) - len, "%s", line);
				if (len && text[len - 1] != '\n' && len < sizeof(text) - 1) text[len++] = '\n', text[len] = 0;
			}
			if (len >= sizeof(text)) return false;
		}
		fclose(f);
	} else if (header) {
		len += snprintf(text, sizeof(text), "%s\n", header);
	}
	if (!done) len += snprintf(text + len, sizeof(text) - len, "%s = %s\n", key, val);
	if (len >= sizeof(text)) return false;
	char tmp[1100];
	snprintf(tmp, sizeof(tmp), "%s.tmp", file);
	f = fopen(tmp, "w");
	if (!f) return false;
	bool ok = fputs(text, f) >= 0;
	if (fclose(f) != 0) ok = false;
	if (ok && rename(tmp, file) == 0) return true;
	remove(tmp);
	return false;
}

/* ------------------------------------------------------------ the data folder */

static char dataNote[256];

const char* data_dir_note(void) {
	return dataNote;
}

static bool is_dir(const char* p) {
	struct stat st;
	return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

/* the same folder (both there), or the same path */
static bool same_dir(const char* a, const char* b) {
	struct stat sa, sb;
	if (stat(a, &sa) == 0 && stat(b, &sb) == 0) return sa.st_dev == sb.st_dev && sa.st_ino == sb.st_ino;
	return !strcmp(a, b);
}

/* path is inside dir (not dir itself), by their real paths where they exist */
static bool inside(const char* path, const char* dir) {
	char rp[PATH_MAX], rd[PATH_MAX];
	if (!realpath(dir, rd)) snprintf(rd, sizeof(rd), "%s", dir);
	if (!realpath(path, rp)) snprintf(rp, sizeof(rp), "%s", path);
	size_t n = strlen(rd);
	if (n == 1) return rp[1] != 0; /* (dir is /) */
	return !strncmp(rp, rd, n) && rp[n] == '/';
}

static bool can_write(const char* dir) {
	char test[1100];
	snprintf(test, sizeof(test), "%s/.gba-ds-write-test", dir);
	FILE* f = fopen(test, "w");
	if (!f) return false;
	bool ok = fclose(f) == 0;
	remove(test);
	return ok;
}

static void remove_tree(const char* p) {
	struct stat st;
	if (lstat(p, &st) != 0) return;
	if (S_ISDIR(st.st_mode)) {
		DIR* d = opendir(p);
		struct dirent* e;
		while (d && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char c[1100];
			snprintf(c, sizeof(c), "%s/%s", p, e->d_name);
			remove_tree(c);
		}
		if (d) closedir(d);
		rmdir(p);
	} else {
		remove(p);
	}
}

/* a copy of a file or folder (its files' times kept: the pause menu shows
 * when a state was saved); false if any of it failed */
static bool copy_tree(const char* from, const char* to) {
	struct stat st;
	if (lstat(from, &st) != 0) return false;
	if (S_ISDIR(st.st_mode)) {
		if (mkdir(to, 0755) != 0 && !is_dir(to)) return false;
		DIR* d = opendir(from);
		if (!d) return false;
		bool ok = true;
		struct dirent* e;
		while (ok && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char a[1100], b[1100];
			snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
			snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
			ok = copy_tree(a, b);
		}
		closedir(d);
		return ok;
	}
	if (!S_ISREG(st.st_mode)) return true; /* (links and the like: left out) */
	FILE* in = fopen(from, "rb");
	if (!in) return false;
	FILE* out = fopen(to, "wb");
	if (!out) {
		fclose(in);
		return false;
	}
	char buf[1 << 16];
	size_t n;
	bool ok = true;
	while (ok && (n = fread(buf, 1, sizeof(buf), in)) > 0) ok = fwrite(buf, 1, n, out) == n;
	if (ferror(in)) ok = false;
	fclose(in);
	if (fclose(out) != 0) ok = false;
	struct utimbuf ut = { st.st_atime, st.st_mtime };
	utime(to, &ut);
	return ok;
}

/* from moved to to: renamed, or copied and then deleted (another card or
 * storage). Where to is there already, folders are merged; a file there
 * already is kept and the one from `from` left where it is (except the
 * README.txt files GBA DS puts in its folders). Returns how many things
 * couldn't move. */
static int move_tree(const char* from, const char* to) {
	struct stat sf, st;
	if (lstat(from, &sf) != 0) return 0;
	if (lstat(to, &st) != 0) {
		if (rename(from, to) == 0) return 0;
		if (errno != EXDEV) {
			fprintf(stderr, "data folder: can't move %s to %s: %s\n", from, to, strerror(errno));
			return 1;
		}
		if (!copy_tree(from, to)) {
			fprintf(stderr, "data folder: can't copy %s to %s\n", from, to);
			remove_tree(to);
			return 1;
		}
		remove_tree(from);
		return 0;
	}
	if (S_ISDIR(sf.st_mode) && S_ISDIR(st.st_mode)) {
		int fails = 0;
		DIR* d = opendir(from);
		struct dirent* e;
		while (d && (e = readdir(d))) {
			if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
			char a[1100], b[1100];
			snprintf(a, sizeof(a), "%s/%s", from, e->d_name);
			snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
			fails += move_tree(a, b);
		}
		if (d) closedir(d);
		rmdir(from); /* (once empty) */
		return fails;
	}
	const char* name = strrchr(from, '/');
	if (!strcmp(name ? name + 1 : from, "README.txt") || same_file_data(from, to)) {
		remove(from);
		return 0;
	}
	fprintf(stderr, "data folder: %s is there already: %s left where it is\n", to, from);
	return 1;
}

/* GBA DS's own folders on Linux (the app folder's other files are the program) */
static const char* const dataEntries[] = { "states", "cheats", "overlays", "logos", "patches", "bug-reports", "screenshots" };
/* the ones the zip has (with a README.txt): an update puts them back in the app folder */
static const char* const shippedEntries[] = { "cheats", "overlays", "logos", "patches" };

void data_dir_setup(const char* defDir, const char* record, const char* want, bool all, char* out, size_t n) {
	dataNote[0] = 0;
	char now[1024], to[1024];
	if (!record || !kv_get(record, "data_dir_now", now, sizeof(now)) || now[0] != '/') snprintf(now, sizeof(now), "%s", defDir);
	snprintf(to, sizeof(to), "%s", want && want[0] ? want : defDir);
	for (size_t k = strlen(to); k > 1 && to[k - 1] == '/'; --k) to[k - 1] = 0;
	snprintf(out, n, "%s", now);

	if (to[0] != '/') {
		snprintf(dataNote, sizeof(dataNote), "data_dir must be a full path (/...): not used");
		fprintf(stderr, "data folder: data_dir = %s isn't a full path: ignored\n", to);
		snprintf(to, sizeof(to), "%s", now);
	}
	if (same_dir(to, now)) {
		snprintf(out, n, "%s", now);
		if (same_dir(now, defDir)) {
			mkdirs(now);
		} else {
			/* a folder of the player's: made again if it's gone (where its
			 * parent is there: not on a card that isn't in) */
			char parent[1024];
			snprintf(parent, sizeof(parent), "%s", now);
			char* s = strrchr(parent, '/');
			if (s && s != parent) *s = 0;
			if (!is_dir(now) && is_dir(parent)) mkdirs(now);
			if (!is_dir(now) || !can_write(now)) {
				fprintf(stderr, "data folder: can't reach %s: using %s for now\n", now, defDir);
				snprintf(dataNote, sizeof(dataNote), "Can't reach GBA DS's folder, using %s for now: %s", defDir, now);
				snprintf(out, n, "%s", defDir);
				mkdirs(defDir);
				return;
			}
		}
	} else {
		/* moving */
		char parent[1024];
		snprintf(parent, sizeof(parent), "%s", to);
		char* s = strrchr(parent, '/');
		if (s && s != parent) *s = 0;
		bool nested = false;
		if (all) {
			nested = inside(to, now);
		} else {
			for (size_t i = 0; i < sizeof(dataEntries) / sizeof(dataEntries[0]); ++i) {
				char e[1100];
				snprintf(e, sizeof(e), "%s/%s", now, dataEntries[i]);
				nested |= same_dir(to, e) || inside(to, e);
			}
		}
		if (!is_dir(parent)) {
			fprintf(stderr, "data folder: %s isn't there (for %s): staying in %s\n", parent, to, now);
			snprintf(dataNote, sizeof(dataNote), "Chosen folder not found, files not moved: %s", to);
		} else if (nested) {
			fprintf(stderr, "data folder: %s is inside %s's own folders: staying there\n", to, now);
			snprintf(dataNote, sizeof(dataNote), "Chosen folder is inside GBA DS's own, files not moved: %s", to);
		} else if ((mkdirs(to), !is_dir(to)) || !can_write(to)) {
			fprintf(stderr, "data folder: can't write in %s: staying in %s\n", to, now);
			snprintf(dataNote, sizeof(dataNote), "Can't save in the chosen folder, files not moved: %s", to);
		} else {
			char states[1100];
			snprintf(states, sizeof(states), "%s/states", to);
			if (is_dir(states)) {
				/* GBA DS's files from before (moved back, or another install's) */
				fprintf(stderr, "data folder: %s has GBA DS's files already: using them (%s left as it is)\n", to, now);
			} else if (is_dir(now)) {
				fprintf(stderr, "data folder: moving %s to %s\n", now, to);
				char a[1100], b[1100];
				snprintf(a, sizeof(a), "%s/states", now);
				int fails = move_tree(a, states);
				if (fails) {
					snprintf(dataNote, sizeof(dataNote), "Couldn't move GBA DS's files, they stay in %s", now);
					rmdir(to);
					if (!is_dir(now) || !can_write(now)) snprintf(out, n, "%s", defDir), mkdirs(defDir);
					return;
				}
				if (all) {
					DIR* d = opendir(now);
					struct dirent* e;
					while (d && (e = readdir(d))) {
						if (!strcmp(e->d_name, ".") || !strcmp(e->d_name, "..")) continue;
						snprintf(a, sizeof(a), "%s/%s", now, e->d_name);
						snprintf(b, sizeof(b), "%s/%s", to, e->d_name);
						fails += move_tree(a, b);
					}
					if (d) closedir(d);
					/* the old folder, once empty (a line Java's launch.log wrote
					 * there meanwhile goes over too) */
					if (rmdir(now) != 0) {
						snprintf(a, sizeof(a), "%s/launch.log", now);
						snprintf(b, sizeof(b), "%s/launch.log.old", to);
						rename(a, b);
						rmdir(now);
					}
				} else {
					for (size_t i = 1; i < sizeof(dataEntries) / sizeof(dataEntries[0]); ++i) {
						snprintf(a, sizeof(a), "%s/%s", now, dataEntries[i]);
						snprintf(b, sizeof(b), "%s/%s", to, dataEntries[i]);
						fails += move_tree(a, b);
					}
					/* (a folder of the player's, once empty) */
					if (!same_dir(now, defDir)) rmdir(now);
				}
				if (fails) snprintf(dataNote, sizeof(dataNote), "Some files couldn't be moved, they're still in %s", now);
				fprintf(stderr, "data folder: moved%s\n", fails ? ", some files left (above)" : "");
			}
			snprintf(out, n, "%s", to);
			snprintf(now, sizeof(now), "%s", to);
		}
	}
	/* where it is now, for the next start */
	if (record) {
		char was[1024] = "";
		bool have = kv_get(record, "data_dir_now", was, sizeof(was));
		if ((have || !same_dir(out, defDir) || (want && want[0])) && strcmp(was, out))
			kv_set(record, "data_dir_now", out, "# GBA DS's data folder");
	}
	/* Linux: the folders an update (or unzipping a new version over the old)
	 * put back in the app folder: anything a player put in them goes over
	 * too, the README files go */
	if (!all && !same_dir(out, defDir)) {
		for (size_t i = 0; i < sizeof(shippedEntries) / sizeof(shippedEntries[0]); ++i) {
			char a[1100], b[1100];
			snprintf(a, sizeof(a), "%s/%s", defDir, shippedEntries[i]);
			snprintf(b, sizeof(b), "%s/%s", out, shippedEntries[i]);
			if (is_dir(a)) move_tree(a, b);
		}
	}
}

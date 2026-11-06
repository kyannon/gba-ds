#include "paths.h"

#include <libgen.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
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

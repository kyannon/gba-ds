#include "cheats.h"
#include "profiles.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

/* ---------------------------------------------------------------- names */

#define TAG_LEN 48 /* a "(...)" tag of a file name, lower case */

/* "Metroid Fusion (World) (Code Breaker).cht" -> title "metroid fusion",
 * tags "world" and "code breaker". Lower case, letters and digits only,
 * single spaces; '_' counts as a space (ROMs named Metroid_Fusion_USA). */
static void title_of(const char* file, char* title, size_t tsize, char tags[][TAG_LEN], int* ntags, int maxTags) {
	const char* base = strrchr(file, '/');
	base = base ? base + 1 : file;
	char tmp[512];
	snprintf(tmp, sizeof(tmp), "%s", base);
	char* dot = strrchr(tmp, '.');
	if (dot) *dot = 0;
	size_t o = 0;
	int depth = 0;
	bool space = true;
	char tag[TAG_LEN];
	size_t tl = 0;
	if (ntags) *ntags = 0;
	for (const char* p = tmp; *p; ++p) {
		char ch = *p;
		if (ch == '(' || ch == '[') {
			++depth;
			tl = 0;
			continue;
		}
		if (ch == ')' || ch == ']') {
			if (depth > 0) --depth;
			if (ntags && *ntags < maxTags && tl) {
				tag[tl] = 0;
				snprintf(tags[(*ntags)++], TAG_LEN, "%s", tag);
			}
			continue;
		}
		if (depth) {
			if (tl + 1 < sizeof(tag)) tag[tl++] = (char) tolower((unsigned char) ch);
			continue;
		}
		if (isalnum((unsigned char) ch)) {
			if (o + 2 >= tsize) break;
			title[o++] = (char) tolower((unsigned char) ch);
			space = false;
		} else if (!space && o + 2 < tsize) {
			title[o++] = ' ';
			space = true;
		}
	}
	while (o && title[o - 1] == ' ') --o;
	title[o] = 0;
}

enum { REGION_ANY, REGION_USA, REGION_JAPAN, REGION_EUROPE, REGION_COUNT };

/* The region a game code's last letter stands for: Europe takes P, the
 * language releases (D F S I H), X Y Z and U (Australia). */
static int region_of(char codeLetter) {
	switch (codeLetter) {
	case 'E': return REGION_USA;
	case 'J': return REGION_JAPAN;
	case 'P': case 'D': case 'F': case 'S': case 'I': case 'H': case 'X': case 'Y': case 'Z': case 'U': return REGION_EUROPE;
	default: return REGION_ANY;
	}
}

/* Does a tag like "usa", "usa, europe", "world" fit region r (REGION_ANY: any)?
 * 1: yes; -1: it names regions, but not ours; 0: it names no region. */
static int tag_region_match(const char* tag, int r) {
	static const char* const names[REGION_COUNT][3] = {
		{ 0 }, { "usa", "us", 0 }, { "japan", "jp", 0 }, { "europe", "eu", 0 }
	};
	bool isRegion = false;
	char t[TAG_LEN];
	snprintf(t, sizeof(t), "%s", tag);
	for (char* w = strtok(t, ", "); w; w = strtok(NULL, ", ")) {
		if (!strcmp(w, "world")) return 1;
		for (int k = REGION_USA; k < REGION_COUNT; ++k) {
			for (int j = 0; names[k][j]; ++j) {
				if (!strcmp(w, names[k][j])) {
					isRegion = true;
					if (r == REGION_ANY || r == k) return 1;
				}
			}
		}
	}
	return isRegion ? -1 : 0;
}

static int type_of(char tags[][TAG_LEN], int n, char* label, size_t lsize) {
	snprintf(label, lsize, "Cheats");
	for (int i = 0; i < n; ++i) {
		if (strstr(tags[i], "code breaker") || strstr(tags[i], "codebreaker")) {
			snprintf(label, lsize, "Code Breaker");
			return EMU_CHEAT_CODEBREAKER;
		}
		if (strstr(tags[i], "gameshark") || strstr(tags[i], "game shark")) {
			snprintf(label, lsize, "GameShark");
			return EMU_CHEAT_GAMESHARK;
		}
		if (strstr(tags[i], "action replay")) {
			snprintf(label, lsize, "Action Replay");
			return EMU_CHEAT_ACTION_REPLAY;
		}
	}
	return EMU_CHEAT_DETECT;
}

/* ---------------------------------------------------------------- search */

typedef struct {
	char romTitle[256];
	int region;
	bool strict; /* RAM moved in this release: only files naming its region */
	size_t bestLen;
	Cheats* out;
} Search;

static void consider(Search* s, const char* path) {
	char title[256], tags[8][TAG_LEN], label[64];
	int ntags = 0;
	title_of(path, title, sizeof(title), tags, &ntags, 8);
	size_t tl = strlen(title);
	/* the file's title must start the ROM's, at a word boundary */
	if (!tl || strncmp(s->romTitle, title, tl) || (s->romTitle[tl] && s->romTitle[tl] != ' ')) return;
	bool regionTag = false, regionOk = false;
	for (int i = 0; i < ntags; ++i) {
		int m = tag_region_match(tags[i], s->region);
		if (s->strict && m > 0 && strstr(tags[i], "world")) m = -1;
		if (m) regionTag = true;
		if (m > 0) regionOk = true;
	}
	if (regionTag && !regionOk) return;
	/* the longest matching title wins ("Castlevania" vs "Castlevania - Aria of Sorrow") */
	if (tl < s->bestLen) return;
	if (tl > s->bestLen) {
		s->bestLen = tl;
		s->out->nFiles = 0;
	}
	if (s->out->nFiles >= CHEAT_MAX_FILES) return;
	CheatFile* f = &s->out->files[s->out->nFiles++];
	snprintf(f->path, sizeof(f->path), "%s", path);
	f->type = type_of(tags, ntags, label, sizeof(label));
	snprintf(f->label, sizeof(f->label), "%s", label);
}

/* Looks at every .cht file in dir and in the folders up to two levels below
 * it (RetroArch keeps them in one folder per system). */
static void scan(Search* s, const char* dir, int depth) {
	DIR* d = opendir(dir);
	if (!d) return;
	struct dirent* de;
	while ((de = readdir(d))) {
		if (de->d_name[0] == '.') continue;
		const char* ext = strrchr(de->d_name, '.');
		bool cht = ext && !strcasecmp(ext, ".cht");
		if (de->d_type == DT_REG && !cht) continue; /* (a library has thousands of files: no stat for these) */
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", dir, de->d_name);
		bool isDir = de->d_type == DT_DIR;
		if (!isDir && de->d_type != DT_REG) { /* a link, or a file system that doesn't say */
			struct stat st;
			if (stat(full, &st) != 0) continue;
			isDir = S_ISDIR(st.st_mode);
		}
		if (isDir) {
			if (depth < 2) scan(s, full, depth + 1);
		} else if (cht) {
			consider(s, full);
		}
	}
	closedir(d);
}

static int cmp_files(const void* a, const void* b) {
	return strcasecmp(((const CheatFile*) a)->path, ((const CheatFile*) b)->path);
}

/* ---------------------------------------------------------------- parsing */

static void unquote(const char* v, char* out, size_t size) {
	while (isspace((unsigned char) *v)) ++v;
	size_t o = 0;
	if (*v == '"') {
		++v;
		while (*v && *v != '"' && o + 1 < size) {
			if (*v == '\\' && v[1]) ++v;
			out[o++] = *v++;
		}
	} else {
		while (*v && *v != '\n' && *v != '\r' && o + 1 < size) out[o++] = *v++;
		while (o && isspace((unsigned char) out[o - 1])) --o;
	}
	out[o] = 0;
}

/* Reads one .cht file: lines "cheatN_desc", "cheatN_code" and
 * "cheatN_enable" (N below CHEATS_PER_FILE_MAX), the value quoted or not.
 * A cheat with no code is dropped. */
static void parse_file(Cheats* c, int fileIdx) {
	FILE* f = fopen(c->files[fileIdx].path, "r");
	if (!f) return;
	struct { char desc[128]; char* code; bool on; } tmp[CHEATS_PER_FILE_MAX];
	memset(tmp, 0, sizeof(tmp));
	int maxIdx = -1;
	char line[4096];
	while (fgets(line, sizeof(line), f)) {
		char* p = line;
		while (isspace((unsigned char) *p)) ++p;
		if (strncmp(p, "cheat", 5) || !isdigit((unsigned char) p[5])) continue;
		char* end;
		long i = strtol(p + 5, &end, 10);
		if (i < 0 || i >= CHEATS_PER_FILE_MAX || *end != '_') continue;
		char* eq = strchr(end, '=');
		if (!eq) continue;
		char key[32];
		size_t kl = (size_t) (eq - end - 1);
		while (kl && isspace((unsigned char) end[1 + kl - 1])) --kl;
		if (kl >= sizeof(key)) continue;
		memcpy(key, end + 1, kl);
		key[kl] = 0;
		char val[4096];
		unquote(eq + 1, val, sizeof(val));
		if (!strcmp(key, "desc")) snprintf(tmp[i].desc, sizeof(tmp[i].desc), "%s", val);
		else if (!strcmp(key, "code")) {
			free(tmp[i].code);
			tmp[i].code = strdup(val);
		} else if (!strcmp(key, "enable")) tmp[i].on = !strcasecmp(val, "true") || !strcmp(val, "1");
		else continue;
		if (i > maxIdx) maxIdx = (int) i;
	}
	fclose(f);
	/* room for all of them at once (at most maxIdx + 1 are kept) */
	Cheat* grown = maxIdx >= 0 ? realloc(c->items, sizeof(*c->items) * (c->n + maxIdx + 1)) : NULL;
	if (grown) c->items = grown; /* (when realloc fails, the old block stays and these cheats are dropped) */
	for (int i = 0; i <= maxIdx; ++i) {
		if (!grown || !tmp[i].code || !tmp[i].code[0]) {
			free(tmp[i].code);
			continue;
		}
		Cheat* ch = &c->items[c->n++];
		memset(ch, 0, sizeof(*ch));
		snprintf(ch->desc, sizeof(ch->desc), "%s", tmp[i].desc[0] ? tmp[i].desc : "(no name)");
		ch->code = tmp[i].code;
		ch->on = tmp[i].on;
		ch->file = fileIdx;
	}
}

/* ---------------------------------------------------------------- prefs */

static const char* file_name(const char* path) {
	const char* b = strrchr(path, '/');
	return b ? b + 1 : path;
}

/* <state_dir>/<game>.cheats: a header line, then "<cht file name>\t<desc>"
 * for each cheat that is on. Cheats of one file with the same description are
 * told apart by "\t#k" after the description (k counting from 0 among them);
 * a line without it (older versions', or a cheat whose description is its
 * file's only one) means every cheat with that description. */
static int same_before(const Cheats* c, int i) {
	int k = 0;
	for (int j = 0; j < i; ++j) {
		if (c->items[j].file == c->items[i].file && !strcmp(c->items[j].desc, c->items[i].desc)) ++k;
	}
	return k;
}
static bool has_same_after(const Cheats* c, int i) {
	for (int j = 0; j < c->n; ++j) {
		if (j != i && c->items[j].file == c->items[i].file && !strcmp(c->items[j].desc, c->items[i].desc)) return true;
	}
	return false;
}

void cheats_save(const Cheats* c) {
	if (!c->prefs[0] || !c->n) return;
	FILE* f = fopen(c->prefs, "w");
	if (!f) return;
	fprintf(f, "# gba-ds cheats that are on: cht file<TAB>description[<TAB>#n for one of several with that description]\n");
	for (int i = 0; i < c->n; ++i) {
		if (!c->items[i].on) continue;
		fprintf(f, "%s\t%s", file_name(c->files[c->items[i].file].path), c->items[i].desc);
		if (has_same_after(c, i)) fprintf(f, "\t#%d", same_before(c, i));
		fputc('\n', f);
	}
	fclose(f);
}

static void load_prefs(Cheats* c) {
	FILE* f = fopen(c->prefs, "r");
	if (!f) return;
	for (int i = 0; i < c->n; ++i) c->items[i].on = false;
	char line[1400];
	while (fgets(line, sizeof(line), f)) {
		if (line[0] == '#') continue;
		line[strcspn(line, "\r\n")] = 0;
		char* tab = strchr(line, '\t');
		if (!tab) continue;
		*tab = 0;
		char* desc = tab + 1;
		int which = -1; /* every cheat with this description */
		char* mark = strrchr(desc, '\t');
		if (mark && mark[1] == '#' && mark[2] >= '0' && mark[2] <= '9' && strspn(mark + 2, "0123456789") == strlen(mark + 2)) {
			which = atoi(mark + 2);
			*mark = 0;
		}
		for (int i = 0; i < c->n; ++i) {
			if (!strcmp(c->items[i].desc, desc) && !strcmp(file_name(c->files[c->items[i].file].path), line) &&
			    (which < 0 || same_before(c, i) == which)) {
				c->items[i].on = true;
			}
		}
	}
	fclose(f);
}

/* ---------------------------------------------------------------- public */

void cheats_load(Cheats* c, const Config* cfg, const char* romPath, const char* gameCode, const char* prefsPath) {
	memset(c, 0, sizeof(*c));
	snprintf(c->prefs, sizeof(c->prefs), "%s", prefsPath ? prefsPath : "");
	Search s = { .out = c };
	title_of(romPath, s.romTitle, sizeof(s.romTitle), NULL, NULL, 0);
	s.region = gameCode ? region_of(gameCode[3]) : 0;
	s.strict = gameCode && profile_ram_moved(gameCode);
	char dirs[2200];
#ifdef __ANDROID__
	snprintf(dirs, sizeof(dirs), "%s", cfg->cheatDir);
#else
	/* GBA DS's own cheats folder (gba-ds/cheats), then cheat_dir's folders */
	snprintf(dirs, sizeof(dirs), "%s/cheats:%s", cfg->assetDir, cfg->cheatDir);
#endif
	char* save;
	for (char* d = strtok_r(dirs, ":", &save); d; d = strtok_r(NULL, ":", &save)) scan(&s, d, 0);
	qsort(c->files, c->nFiles, sizeof(*c->files), cmp_files);
	for (int i = 0; i < c->nFiles; ++i) parse_file(c, i);
	load_prefs(c);
	fprintf(stderr, "cheats: %d in %d file(s) for \"%s\"\n", c->n, c->nFiles, s.romTitle);
}

void cheats_apply(const Cheats* c, Emu* e) {
	emu_cheats_clear(e);
	const char* codes[CHEATS_PER_FILE_MAX];
	for (int f = 0; f < c->nFiles; ++f) {
		int n = 0;
		for (int i = 0; i < c->n && n < CHEATS_PER_FILE_MAX; ++i) {
			if (c->items[i].file == f && c->items[i].on) codes[n++] = c->items[i].code;
		}
		if (n) {
			int lines = emu_cheats_add(e, c->files[f].label, codes, n, c->files[f].type);
			fprintf(stderr, "cheats: %d on from %s (%d code lines)\n", n, file_name(c->files[f].path), lines);
		}
	}
}

int cheats_on(const Cheats* c) {
	int n = 0;
	for (int i = 0; i < c->n; ++i) n += c->items[i].on;
	return n;
}

void cheats_free(Cheats* c) {
	for (int i = 0; i < c->n; ++i) free(c->items[i].code);
	free(c->items);
	c->items = NULL;
	c->n = 0;
}

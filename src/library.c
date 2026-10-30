#include "library.h"
#include "android.h"
#include "paths.h"
#include "profiles.h"
#include "ui.h"
#include "logo.h"
#include "brand.h"
#include "emu.h"
#include "esinteg.h"
#include "mapper.h"
#include "ra.h"
#include "update.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

typedef struct {
	char path[1024];
	char name[160];
	char code[5];
	bool supported;
	int flag; /* BRAND_FLAG_* shown on the tile, or -1 (only one region of this game) */
} GameEntry;

static GameEntry* games;
static int nGames;

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

/* Game codes of archived ROMs, remembered by path, size and date: reading
 * one means unpacking the whole ROM, which is slow for a .7z. Saved as
 * <state_dir>/archives.cache, lines "size mtime code path". */
typedef struct {
	long long size, mtime;
	char code[5];
	char path[1024];
	bool used;
} CodeCache;
static CodeCache* codeCache;
static int nCodeCache;
static bool codeCacheDirty;

static void code_cache_load(const Config* cfg) {
	free(codeCache);
	codeCache = NULL;
	nCodeCache = 0;
	codeCacheDirty = false;
	char path[1100];
	snprintf(path, sizeof(path), "%s/archives.cache", cfg->stateDir);
	FILE* f = fopen(path, "r");
	if (!f) return;
	char line[1200];
	while (fgets(line, sizeof(line), f)) {
		CodeCache c = { 0 };
		int off = 0;
		if (sscanf(line, "%lld %lld %4s %n", &c.size, &c.mtime, c.code, &off) < 3 || !off) continue;
		line[strcspn(line, "\n")] = 0;
		snprintf(c.path, sizeof(c.path), "%s", line + off);
		codeCache = realloc(codeCache, sizeof(*codeCache) * (nCodeCache + 1));
		codeCache[nCodeCache++] = c;
	}
	fclose(f);
}

static void code_cache_save(const Config* cfg) {
	bool unused = false;
	for (int i = 0; i < nCodeCache; ++i) unused |= !codeCache[i].used;
	if (!codeCacheDirty && !unused) return;
	char path[1100];
	snprintf(path, sizeof(path), "%s/archives.cache", cfg->stateDir);
	FILE* f = fopen(path, "w");
	if (!f) return;
	for (int i = 0; i < nCodeCache; ++i) {
		if (codeCache[i].used) fprintf(f, "%lld %lld %s %s\n", codeCache[i].size, codeCache[i].mtime, codeCache[i].code, codeCache[i].path);
	}
	fclose(f);
}

/* The 4-letter game code from the ROM header (offset 0xAC). */
static bool game_code(const char* path, const struct stat* st, char code[5]) {
	bool archive = emu_is_archive_name(path);
	if (archive) {
		for (int i = 0; i < nCodeCache; ++i) {
			CodeCache* c = &codeCache[i];
			if (c->size == (long long) st->st_size && c->mtime == (long long) st->st_mtime && !strcmp(c->path, path)) {
				c->used = true;
				memcpy(code, c->code, 5);
				return true;
			}
		}
	}
	uint8_t hdr[0xB0];
	if (!emu_rom_header(path, hdr, sizeof(hdr))) {
		if (archive) fprintf(stderr, "library: no GBA ROM in %s\n", path);
		return false;
	}
	memcpy(code, hdr + 0xAC, 4);
	code[4] = 0;
	for (int i = 0; i < 4; ++i) {
		if (!code[i] || !isprint((unsigned char) code[i])) code[i] = '_';
	}
	if (archive) {
		CodeCache c = { st->st_size, st->st_mtime, "", "", true };
		memcpy(c.code, code, 5);
		snprintf(c.path, sizeof(c.path), "%s", path);
		codeCache = realloc(codeCache, sizeof(*codeCache) * (nCodeCache + 1));
		codeCache[nCodeCache++] = c;
		codeCacheDirty = true;
	}
	return true;
}

/* Folders read in this scan, by identity: the same folder can be listed
 * twice under different names (Android's shared storage ignores case, so
 * ROMs/gba is ROMs/GBA; a symlink; a repeated rom_dir entry). */
static struct {
	dev_t dev;
	ino_t ino;
} seenDirs[256];
static int nSeenDirs;

static bool dir_seen(const char* d) {
	struct stat st;
	if (stat(d, &st) != 0) return false;
	for (int i = 0; i < nSeenDirs; ++i) {
		if (seenDirs[i].dev == st.st_dev && seenDirs[i].ino == st.st_ino) return true;
	}
	if (nSeenDirs < 256) seenDirs[nSeenDirs].dev = st.st_dev, seenDirs[nSeenDirs].ino = st.st_ino, ++nSeenDirs;
	return false;
}

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

static void scan_dir(Config* cfg, const char* d, int depth) {
	if (dir_seen(d)) {
		fprintf(stderr, "library: %s already listed\n", d);
		return;
	}
	DIR* dir = opendir(d);
	if (!dir) {
		if (depth == 0) fprintf(stderr, "library: cannot open %s\n", d);
		return;
	}
	struct dirent* de;
	while ((de = readdir(dir))) {
		if (de->d_name[0] == '.') continue;
		char full[1024];
		snprintf(full, sizeof(full), "%s/%s", d, de->d_name);
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
		if (!g.supported && !cfg->showAll) continue;
		int n = (int) (ext - de->d_name);
		if (n >= (int) sizeof(g.name)) n = sizeof(g.name) - 1;
		memcpy(g.name, de->d_name, n);
		for (char* c = g.name; *c; ++c) {
			if (*c == '_') *c = ' ';
		}
		const MapProfile* prof = profile_find(g.code);
		if (prof && prof->listName) snprintf(g.name, sizeof(g.name), "%s", prof->listName);
		if (!cfg->nameTags) strip_tags(g.name);
		games = realloc(games, sizeof(*games) * (nGames + 1));
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

static void scan(Config* cfg) {
	free(games);
	games = NULL;
	nGames = 0;
	char dirs[1024];
	snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
	char* save;
	nSeenDirs = 0;
	code_cache_load(cfg);
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
	for (int i = 0; i < nGames; ++i) {
		SDL_Texture* t[2];
		int w, h;
		bool px;
		if (ui_load_logo(cfg, games[i].path, games[i].code, t, &w, &h, &px)) ui_free_image(t);
		char fp[1200];
		snprintf(fp, sizeof(fp), "%s/frames", cfg->stateDir);
		mkdirs(fp);
		snprintf(fp, sizeof(fp), "%s/frames/%.4s.png", cfg->stateDir, games[i].code);
		frame_request(games[i].path, games[i].code, fp);
	}
	fprintf(stderr, "library: %d game(s)\n", nGames);
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

/* square button left of Settings: switches between list and grid */
static SDL_Rect view_button(int scr) {
	SDL_Rect sb = settings_button(scr);
	float px = ui_px(scr);
	return (SDL_Rect) { sb.x - px * 3 - sb.h, sb.y, sb.h, sb.h };
}

/* Four squares (in the list: switch to the grid) or three lines (in the
 * grid: switch to the list). */
static void draw_view_button(int scr) {
	SDL_Rect b = view_button(scr);
	float px = ui_px(scr);
	ui_fill_round(scr, b, C_ROW, px * 3);
	int cx = b.x + b.w / 2, cy = b.y + b.h / 2;
	if (listMode) {
		int sq = b.h * 12 / 56, gap = b.h * 6 / 56;
		for (int i = 0; i < 2; ++i) {
			for (int j = 0; j < 2; ++j) {
				SDL_Rect q = { cx - sq - gap / 2 + i * (sq + gap), cy - sq - gap / 2 + j * (sq + gap), sq, sq };
				ui_fill_round(scr, q, C_TEXT, SDL_max(1, sq / 4));
			}
		}
	} else {
		int lw = b.h * 28 / 56, lh = SDL_max(2, b.h * 5 / 56), gap = b.h * 7 / 56;
		for (int j = -1; j <= 1; ++j) {
			SDL_Rect l = { cx - lw / 2, cy + j * (lh + gap) - lh / 2, lw, lh };
			ui_fill_round(scr, l, C_TEXT, lh / 2);
		}
	}
}

/* library.cfg: the game list's own settings (and, on Android, the ROM
 * folder picked in Settings). */
static void save_library_cfg(const Config* cfg) {
	char file[1100];
	snprintf(file, sizeof(file), "%s/library.cfg", cfg->stateDir);
	FILE* f = fopen(file, "w");
	if (!f) return;
	fprintf(f, "# written by the game list\nlibrary_view = %s\n", listMode ? "list" : "grid");
#ifdef __ANDROID__
	fprintf(f, "rom_dir = %s\n", cfg->romDir);
#endif
	fclose(f);
}

static void toggle_view(Config* cfg) {
	listMode = !listMode;
	snprintf(cfg->libraryView, sizeof(cfg->libraryView), "%s", listMode ? "list" : "grid");
	save_library_cfg(cfg);
}

/* Can GBA DS write where it keeps its files (states, logos, screenshots)?
 * Checked once a run; when it can't, the game list's header says so
 * instead of everything failing quietly. */
static char storageMsg[128];
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
		if (on) ui_fill_round(scr, (SDL_Rect) { t.x - px, t.y - px, t.w + px * 2, t.h + px * 2 }, C_EDGE, px * 4);
		ui_fill_round(scr, t, on ? C_SEL : C_ROW, px * 3);
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
		fit_text(line, sizeof(line), TF_BOLD, px * 4, storageMsg, view_button(scr).x - px * 8);
		ui_font_text(scr, TF_BOLD, view_button(scr).x / 2, g.headerH / 2, px * 4, line, C_GOLD, 0);
	} else {
		ui_label(scr, view_button(scr).x / 2, g.headerH / 2, px * 5, "Touch or use the D-Pad to select a game", C_TEXT);
	}
	draw_view_button(scr);
	ui_fill_round(scr, sb, C_ROW, px * 3);
	ui_label(scr, sb.x + sb.w / 2, sb.y + sb.h / 2, px * 5, "Settings", C_TEXT);
}

/* ------------------------------------------------------------ settings */

static char settingsMsg[128];
static bool updatesOpen;   /* the Updates panel (Linux), over Settings or the game list */
static bool updatesNotice; /* opened by an automatic check */
static int updatesSel;

/* Settings: a row of buttons, each with its text below, then Back.
 * Linux: GBA DS in EmulationStation (ROCKNIX) or in BananaOS's list, where
 * there's one; Map Buttons; Updates. Android: ROM Folder, RetroAchievements, Map Buttons. */
#define SETTINGS_MAX 4
static int nRows = 1; /* rows shown (Back is row nRows) */
#ifndef __ANDROID__
enum { ROW_BANANA, ROW_ES, ROW_MAP, ROW_UPDATES };
static int rowKind[SETTINGS_MAX];
#endif

static void settings_rows(const Config* cfg) {
#ifdef __ANDROID__
	(void) cfg;
	nRows = 4; /* ROM Folder, RetroAchievements, Map Buttons, Updates */
#else
	nRows = 0;
	if (banana_present()) rowKind[nRows++] = ROW_BANANA;
	else if (es_present(cfg)) rowKind[nRows++] = ROW_ES;
	rowKind[nRows++] = ROW_MAP;
#ifdef GBADS_UPDATES
	rowKind[nRows++] = ROW_UPDATES;
#endif
#endif
}

typedef struct {
	SDL_Rect panel, row[SETTINGS_MAX], back;
	int textY[SETTINGS_MAX];
	float px;
} SettingsGeom;

static SettingsGeom settings_geom(int scr) {
	SettingsGeom g;
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	g.px = px;
	/* (four rows: Android's, each with a line or two of text) */
	int m = px * 6, rowH = nRows > 3 ? px * 12 : nRows > 2 ? px * 14 : px * 18;
	g.panel = (SDL_Rect) { m, m, r.w - m * 2, r.h - m * 2 };
	int gap = nRows > 3 ? px * 24 : nRows > 2 ? px * 28 : px * 44; /* room for each row's text under it */
	int y = g.panel.y + (nRows > 3 ? px * 22 : px * 26);
	for (int i = 0; i < nRows; ++i) {
		g.row[i] = (SDL_Rect) { g.panel.x + px * 6, y, g.panel.w - px * 12, rowH };
		g.textY[i] = g.row[i].y + rowH + px * 8;
#ifndef __ANDROID__
		/* Linux, three rows: room for each one's text (lines of about 8 units) */
		if (nRows > 2) gap = px * (rowKind[i] == ROW_BANANA || rowKind[i] == ROW_ES ? 38 : rowKind[i] == ROW_MAP ? 30 : 22);
#endif
		y += rowH + gap;
	}
	g.back = (SDL_Rect) { g.panel.x + (g.panel.w - px * 60) / 2, g.panel.y + g.panel.h - rowH - px * 6, px * 60, rowH };
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
	ui_label(scr, g->panel.x + g->panel.w / 2, g->panel.y + px * 12, px * 6, "Settings", C_TEXT);
	for (int i = 0; i <= nRows; ++i) {
		SDL_Rect b = i < nRows ? g->row[i] : g->back;
		if (i == sel) ui_fill_round(scr, (SDL_Rect) { b.x - px, b.y - px, b.w + px * 2, b.h + px * 2 }, C_EDGE, px * 4);
		ui_fill_round(scr, b, i == sel ? C_SEL : C_ROW, px * 3);
		ui_label(scr, b.x + b.w / 2, b.y + b.h / 2, px * 5, i < nRows ? labels[i] : "Back", C_TEXT);
	}
}

#ifdef __ANDROID__
/* Android: the ROM folder, chosen with Android's folder picker (android.c),
 * kept in <state_dir>/library.cfg; the RetroAchievements account, signed in
 * with a dialog (android.c, GbaDsActivity) and kept as a token by ra.c. */
static bool pickPending, raDialogPending;
static int settingsMsgRow; /* the row settingsMsg is about: shown in place of its text */

static void draw_settings(const Config* cfg, int scr, int sel) {
	SettingsGeom g = settings_geom(scr);
	float px = g.px;
	char user[128], err[256];
	int ra = ra_account(user, sizeof(user), err, sizeof(err));
	const char* raLabel = ra == RA_ACCOUNT_OK     ? "RetroAchievements: Sign Out" :
	                      ra == RA_ACCOUNT_BUSY   ? "RetroAchievements: Signing In..." :
	                      ra == RA_ACCOUNT_OFF    ? "RetroAchievements: Off" :
	                                                "RetroAchievements: Sign In";
	const char* labels[SETTINGS_MAX] = { "ROM Folder", raLabel, "Map Buttons", "Updates" };
	draw_settings_frame(scr, &g, sel, labels);
	int tx = g.row[0].x + px * 2, tw = g.row[0].w - px * 4;
	char line[1400];
	if (pickPending) {
		snprintf(line, sizeof(line), "Choose a folder in Android's file picker...");
	} else {
		char dirs[sizeof(cfg->romDir)];
		snprintf(dirs, sizeof(dirs), "%s", cfg->romDir);
		for (char* p = dirs; *p; ++p) {
			if (*p == ':') *p = ' ';
		}
		snprintf(line, sizeof(line), "Games are read from %s (subfolders too).", dirs);
	}
	if (settingsMsg[0] && settingsMsgRow == 0) draw_wrapped(scr, tx, g.textY[0], tw, px, settingsMsg, C_GOLD);
	else draw_wrapped(scr, tx, g.textY[0], tw, px, line, C_DIM);
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
	} else if (ra == RA_ACCOUNT_OFF) {
		snprintf(line, sizeof(line), "Switched off in gba-ds.cfg (retroachievements = 0).");
	} else {
		snprintf(line, sizeof(line),
		         "Earn achievements in casual mode with your retroachievements.org account. GBA DS keeps a sign-in token, "
		         "not your password.");
	}
	if (settingsMsg[0] && settingsMsgRow == 1) draw_wrapped(scr, tx, g.textY[1], tw, px, settingsMsg, C_GOLD);
	else draw_wrapped(scr, tx, g.textY[1], tw, px, line, ra == RA_ACCOUNT_FAILED ? C_GOLD : C_DIM);
	draw_wrapped(scr, tx, g.textY[2], tw, px,
	             "Sets which of this device's buttons does what: GBA DS asks for each one in turn. "
	             "Tap to skip any the device doesn't have.",
	             C_DIM);
	draw_wrapped(scr, tx, g.textY[3], tw, px, "This is v" GBADS_VERSION ". Look for a new version, and download and install it.",
	             C_DIM);
}

static void settings_activate(Config* cfg, int row) {
	(void) cfg;
	settingsMsg[0] = 0;
	settingsMsgRow = row;
	if (row == 0) {
		if (android_pick_folder()) pickPending = true;
		else snprintf(settingsMsg, sizeof(settingsMsg), "Couldn't open the file picker.");
		return;
	}
	if (row == 2) {
		mapper_run(cfg);
		return;
	}
	if (row == 3) {
		updatesOpen = true, updatesNotice = false, updatesSel = 0;
		return;
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

/* Answers from the folder picker and the sign-in box, once they come.
 * True if the ROM folder changed. */
static bool settings_poll(Config* cfg) {
	if (raDialogPending) {
		settingsMsgRow = 1;
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
	settingsMsgRow = 0;
	if (st < 0) {
		snprintf(settingsMsg, sizeof(settingsMsg), "No folder chosen.");
		return false;
	}
	snprintf(cfg->romDir, sizeof(cfg->romDir), "%s", path);
	save_library_cfg(cfg);
	return true;
}
#else
static void draw_settings(const Config* cfg, int scr, int sel) {
	SettingsGeom g = settings_geom(scr);
	float px = g.px;
	const char* labels[SETTINGS_MAX];
	for (int i = 0; i < nRows; ++i) {
		labels[i] = rowKind[i] == ROW_BANANA ? (banana_integration_on() ? "GBA DS in BananaOS's GBA list: On" : "GBA DS in BananaOS's GBA list: Off")
		          : rowKind[i] == ROW_ES     ? (es_integration_on(cfg) ? "GBA DS in EmulationStation: On" : "GBA DS in EmulationStation: Off")
		          : rowKind[i] == ROW_MAP    ? "Map Buttons"
		                                     : "Updates";
	}
	draw_settings_frame(scr, &g, sel, labels);
	int tx = g.row[0].x + px * 2, tw = g.row[0].w - px * 4;
	int y = 0;
	for (int i = 0; i < nRows; ++i) {
		const char* text = rowKind[i] == ROW_BANANA
		                       ? "Games picked in BananaOS's own Game Boy Advance list start in GBA DS when it has "
		                         "second-screen support for them; every other game plays in RetroArch as before. "
		                         "BananaOS reads its list when it starts, so restart it after switching this."
		                   : rowKind[i] == ROW_ES
		                       ? "Adds GBA DS to the emulators for Game Boy Advance games in EmulationStation. Pick it for one game "
		                         "(its Advanced Game Options > Emulator) or for all of them (the system's advanced settings). "
		                         "The other emulators keep working as before."
		                   : rowKind[i] == ROW_MAP
		                       ? "Sets which of this device's buttons does what: GBA DS asks for each one in turn. "
		                         "Tap to skip any the device doesn't have."
		                       : "This is v" GBADS_VERSION ". Look for a new version, and download and install it.";
		y = draw_wrapped(scr, tx, g.textY[i], tw, px, text, C_DIM);
	}
	if (settingsMsg[0]) draw_wrapped(scr, tx, y + px * 6, tw, px, settingsMsg, C_GOLD);
}

static void settings_activate(Config* cfg, int row) {
	settingsMsg[0] = 0;
	if (rowKind[row] == ROW_UPDATES) updatesOpen = true, updatesNotice = false, updatesSel = 0;
	else if (rowKind[row] == ROW_BANANA) banana_integration_set(!banana_integration_on(), settingsMsg, sizeof(settingsMsg));
	else if (rowKind[row] == ROW_ES) es_integration_set(cfg, !es_integration_on(cfg), settingsMsg, sizeof(settingsMsg));
	else mapper_run(cfg);
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
		if (i == sel) ui_fill_round(scr, (SDL_Rect) { b.x - px, b.y - px, b.w + px * 2, b.h + px * 2 }, C_EDGE, px * 4);
		ui_fill_round(scr, b, i == sel ? C_SEL : C_ROW, px * 3);
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
static void draw_wait(int scr, int done, int total) {
	SDL_Rect r = screens[scr].rect;
	float px = ui_px(scr);
	ui_dim(scr, 235);
	int cy = r.h / 2;
	ui_label(scr, r.w / 2, cy - px * 14, px * 6, "Preparing games...", C_TEXT);
	int bw = r.w / 2, bh = px * 4;
	SDL_Rect bar = { (r.w - bw) / 2, cy - bh / 2, bw, bh };
	ui_fill(scr, bar, C_ROW);
	if (total > 0) ui_fill(scr, (SDL_Rect) { bar.x, bar.y, bw * done / total, bh }, C_SEL);
	char line[64];
	snprintf(line, sizeof(line), "%d of %d", done, total);
	ui_font_text(scr, TF_BODY, r.w / 2, cy + px * 11, px * 4, line, C_DIM, 0);
	ui_font_text(scr, TF_BODY, r.w / 2, cy + px * 22, px * 4, "First run only. B: back", C_DIM, 0);
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
	scan(cfg);
	thumbFor = -2;

	/* reselect the last game played */
	char lastFile[1100], last[1024] = "";
	snprintf(lastFile, sizeof(lastFile), "%s/last_game.txt", cfg->stateDir);
	FILE* f = fopen(lastFile, "r");
	if (f) {
		if (fgets(last, sizeof(last), f)) last[strcspn(last, "\n")] = 0;
		fclose(f);
	}
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
	int waiting = -1; /* chosen, but the background extraction isn't done yet */
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

	while (!quit && chosen < 0) {
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
		 * pulled out of the ROMs in the background. Wait for that before
		 * starting a game so the game doesn't compete with it for the CPU
		 * (which made the audio crackle on the first launch). */
		if (waiting >= 0) {
			int done, total;
			logo_progress(&done, &total);
			if (done >= total) {
				chosen = waiting;
				break;
			}
			Pointer p;
			while (ui_next_pointer(&p)) {
			}
			if (btn_pressed(B_B) || btn_released(B_HOME)) waiting = -1;
			ui_clear();
			draw_list(cfg, ls, sel, scroll);
			if (nScreens > 1) draw_preview(cfg, ui_other_screen(), sel);
			for (int s = 0; s < nScreens; ++s) draw_wait(s, done, total);
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
				else settingsOpen = false;
			}
			if (btn_pressed(B_B) || btn_pressed(B_SELECT) || btn_released(B_HOME)) settingsOpen = false;
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
				else if (SDL_PointInRect(&pt, &sg.back) || !SDL_PointInRect(&pt, &sg.panel)) settingsSel = nRows, settingsOpen = false;
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
			settings_rows(cfg);
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
				SDL_Rect sb = settings_button(ls), vb = view_button(ls);
				SDL_Point pt = { p.x, p.y };
				if (SDL_PointInRect(&pt, &vb)) {
					toggle_view(cfg);
					scroll = sel >= 0 ? keep_visible(ls, sel, 0) : 0;
					pressed = false;
					break;
				}
				if (SDL_PointInRect(&pt, &sb)) {
					settingsOpen = true;
					settings_rows(cfg);
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
			int done, total;
			logo_progress(&done, &total);
			if (done < total) waiting = chosen, chosen = -1;
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

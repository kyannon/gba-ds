#include "update.h"

#ifdef GBADS_UPDATES

#include "http.h"
#include "paths.h"
#include "sha256.h"

#include <mgba-util/vfs.h>

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_URL "https://api.github.com/repos/kyannon/gba-ds/releases/latest"
#define CHECK_EVERY (24 * 60 * 60)

static struct {
	SDL_mutex* lock;
	UpdateInfo info;
	char url[256];
	char stateFile[1100], workDir[1100];
	char installDir[1024], exePath[1024];
	char skip[32];
	int autoCfg; /* update.cfg's auto (-1: not set; gba-ds.cfg's update_check decides) */
	bool autoDefault;
	bool checkedThisRun, notice, noticeSeen, busy;
	/* the release found */
	char assetUrl[512], assetName[128], digest[80];
	long assetSize;
	volatile bool cancel;
	char** argv;
} U;

/* ------------------------------------------------------------ update.cfg */

static void save_state_file(void) {
	FILE* f = fopen(U.stateFile, "w");
	if (!f) return;
	fprintf(f, "# GBA DS updates (the game list's Settings > Updates)\n");
	fprintf(f, "last_check = %ld\n", U.info.lastCheck);
	if (U.skip[0]) fprintf(f, "skip = %s\n", U.skip);
	if (U.autoCfg >= 0) fprintf(f, "auto = %d\n", U.autoCfg);
	fclose(f);
}

static void load_state_file(void) {
	FILE* f = fopen(U.stateFile, "r");
	if (!f) return;
	char line[256];
	while (fgets(line, sizeof(line), f)) {
		char key[32], val[64];
		if (sscanf(line, " %31[a-z_] = %63s", key, val) != 2) continue;
		if (!strcmp(key, "last_check")) U.info.lastCheck = atol(val);
		else if (!strcmp(key, "skip")) snprintf(U.skip, sizeof(U.skip), "%s", val);
		else if (!strcmp(key, "auto")) U.autoCfg = atoi(val) != 0;
	}
	fclose(f);
}

/* ------------------------------------------------------------ versions */

/* "v1.3.1", "1.3", "1.3.2-dev" -> parts; a -suffix sorts before the release */
static void parse_version(const char* s, int v[4]) {
	if (*s == 'v' || *s == 'V') ++s;
	v[0] = v[1] = v[2] = 0;
	v[3] = 1;
	sscanf(s, "%d.%d.%d", &v[0], &v[1], &v[2]);
	if (strchr(s, '-')) v[3] = 0;
}

static int compare_versions(const char* a, const char* b) {
	int x[4], y[4];
	parse_version(a, x);
	parse_version(b, y);
	for (int i = 0; i < 4; ++i) {
		if (x[i] != y[i]) return x[i] < y[i] ? -1 : 1;
	}
	return 0;
}

/* ------------------------------------------------------------ JSON, just enough */

static const char* skip_ws(const char* p) {
	while (*p == ' ' || *p == '\n' || *p == '\r' || *p == '\t') ++p;
	return p;
}

/* past a JSON value starting at p */
static const char* skip_value(const char* p) {
	p = skip_ws(p);
	if (*p == '"') {
		for (++p; *p && *p != '"'; ++p) {
			if (*p == '\\' && p[1]) ++p;
		}
		return *p ? p + 1 : p;
	}
	if (*p == '{' || *p == '[') {
		int depth = 0;
		for (; *p; ++p) {
			if (*p == '"') {
				p = skip_value(p) - 1;
			} else if (*p == '{' || *p == '[') {
				++depth;
			} else if (*p == '}' || *p == ']') {
				if (--depth == 0) return p + 1;
			}
		}
		return p;
	}
	while (*p && *p != ',' && *p != '}' && *p != ']') ++p;
	return p;
}

/* The value of key in the object at obj (its own keys only), or NULL. */
static const char* json_find(const char* obj, const char* key) {
	const char* p = skip_ws(obj);
	if (*p != '{') return NULL;
	++p;
	size_t kl = strlen(key);
	for (;;) {
		p = skip_ws(p);
		if (*p != '"') return NULL;
		const char* k = p + 1;
		p = skip_value(p);
		bool match = (size_t) (p - 1 - k) == kl && !strncmp(k, key, kl);
		p = skip_ws(p);
		if (*p != ':') return NULL;
		p = skip_ws(p + 1);
		if (match) return p;
		p = skip_ws(skip_value(p));
		if (*p != ',') return NULL;
		++p;
	}
}

/* A string value, unescaped (UTF-8 kept; \uXXXX as UTF-8). */
static bool json_string(const char* v, char* out, size_t size) {
	if (!v || *v != '"' || !size) return false;
	size_t n = 0;
	for (++v; *v && *v != '"'; ++v) {
		unsigned c = (unsigned char) *v;
		if (c == '\\') {
			++v;
			c = *v == 'n' ? '\n' : *v == 't' ? '\t' : *v == 'r' ? '\r' : *v == 'b' || *v == 'f' ? ' ' : (unsigned char) *v;
			if (*v == 'u') {
				unsigned u = 0;
				sscanf(v + 1, "%4x", &u);
				v += 4;
				char tmp[4];
				int k = 0;
				if (u < 0x80) tmp[k++] = (char) u;
				else if (u < 0x800) tmp[k++] = (char) (0xC0 | u >> 6), tmp[k++] = (char) (0x80 | (u & 63));
				else tmp[k++] = (char) (0xE0 | u >> 12), tmp[k++] = (char) (0x80 | ((u >> 6) & 63)), tmp[k++] = (char) (0x80 | (u & 63));
				for (int i = 0; i < k && n + 1 < size; ++i) out[n++] = tmp[i];
				continue;
			}
		}
		if (n + 1 < size) out[n++] = (char) c;
	}
	out[n] = 0;
	return true;
}

/* The release notes' first part (up to Install / Checksums), markdown
 * markers taken out. */
static void notes_text(const char* md, char* out, size_t size) {
	size_t n = 0;
	const char* p = md;
	while (*p && n + 1 < size) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t) (eol - p) : strlen(p);
		char line[600];
		snprintf(line, sizeof(line), "%.*s", (int) (len < sizeof(line) - 1 ? len : sizeof(line) - 1), p);
		line[strcspn(line, "\r")] = 0;
		p += len + (eol != NULL);
		if (!strncmp(line, "## Install", 10) || !strncmp(line, "## Checksums", 12) || !strncmp(line, "## Known", 8)) break;
		char* s = line;
		while (*s == ' ') ++s;
		bool heading = false;
		if (*s == '#') {
			while (*s == '#') ++s;
			while (*s == ' ') ++s;
			heading = true;
		}
		bool bullet = (*s == '-' || *s == '*') && s[1] == ' ';
		if (bullet) s += 2;
		if (!*s) continue;
		if (n && n + 1 < size) out[n++] = '\n';
		if (bullet && n + 4 < size) memcpy(out + n, "\xe2\x80\xa2 ", 4), n += 4; /* a bullet and a space */
		for (; *s && n + 1 < size; ++s) {
			if (*s == '*' || *s == '`') continue;
			if (*s == '[') continue; /* [text](link) -> text */
			if (*s == ']' && s[1] == '(') {
				char* e = strchr(s, ')');
				if (e) s = e;
				continue;
			}
			out[n++] = *s;
		}
		if (heading && n + 1 < size) out[n++] = ':';
	}
	out[n < size ? n : size - 1] = 0;
}

/* ------------------------------------------------------------ the worker */

static void set_error(const char* fmt, const char* detail) {
	SDL_LockMutex(U.lock);
	U.info.state = UPD_ERROR;
	snprintf(U.info.error, sizeof(U.info.error), fmt, detail ? detail : "");
	U.busy = false;
	SDL_UnlockMutex(U.lock);
	fprintf(stderr, "update: %s\n", U.info.error);
}

static int check_thread(void* ud) {
	bool automatic = ud != NULL;
	char ua[64], err[200];
	snprintf(ua, sizeof(ua), "GBA-DS/%s", GBADS_VERSION);
	char* body = NULL;
	size_t len = 0;
	int code = http_get(U.url, ua, 20, NULL, &body, &len, err, sizeof(err), NULL);
	if (code != 200 || !body) {
		if (code > 0 && !err[0]) snprintf(err, sizeof(err), "the server answered %d", code);
		free(body);
		set_error("Couldn't check for updates: %s.", err);
		if (automatic) {
			SDL_LockMutex(U.lock);
			U.info.state = UPD_IDLE; /* not worth a word unless asked */
			SDL_UnlockMutex(U.lock);
		}
		return 0;
	}
	char tag[64] = "", name[128];
	json_string(json_find(body, "tag_name"), tag, sizeof(tag));
	const char* draft = json_find(body, "draft");
	const char* pre = json_find(body, "prerelease");
	const char* assets = json_find(body, "assets");
	char asset[512] = "", assetName[128] = "", digest[80] = "";
	long size = 0;
	if (assets && *assets == '[') {
		const char* p = skip_ws(assets + 1);
		while (*p == '{') {
			if (json_string(json_find(p, "name"), name, sizeof(name)) && !strncmp(name, "gba-ds-v", 8) &&
			    strstr(name, "-linux-aarch64.zip")) {
				snprintf(assetName, sizeof(assetName), "%s", name);
				json_string(json_find(p, "browser_download_url"), asset, sizeof(asset));
				json_string(json_find(p, "digest"), digest, sizeof(digest));
				const char* sz = json_find(p, "size");
				if (sz) size = atol(sz);
			}
			p = skip_ws(skip_value(p));
			if (*p != ',') break;
			p = skip_ws(p + 1);
		}
	}
	char* notes = malloc(8192);
	notes[0] = 0;
	json_string(json_find(body, "body"), notes, 8192);
	free(body);
	if (!tag[0]) {
		free(notes);
		set_error("Couldn't check for updates: %s.", "the answer had no release in it");
		return 0;
	}
	SDL_LockMutex(U.lock);
	U.info.lastCheck = (long) time(NULL);
	U.checkedThisRun = true;
	const char* ver = tag[0] == 'v' ? tag + 1 : tag;
	bool newer = compare_versions(ver, GBADS_VERSION) > 0 && asset[0] && !(draft && *draft == 't') && !(pre && *pre == 't');
	if (newer) {
		U.info.state = UPD_AVAILABLE;
		snprintf(U.info.version, sizeof(U.info.version), "%s", ver);
		notes_text(notes, U.info.notes, sizeof(U.info.notes));
		snprintf(U.assetUrl, sizeof(U.assetUrl), "%s", asset);
		snprintf(U.assetName, sizeof(U.assetName), "%s", assetName);
		snprintf(U.digest, sizeof(U.digest), "%s", digest);
		U.assetSize = size;
		U.info.size = size;
		/* the notice: only for automatic checks, and not for a version skipped */
		if (automatic && strcmp(U.skip, ver) != 0) U.notice = true, U.noticeSeen = false;
	} else {
		U.info.state = automatic ? UPD_IDLE : UPD_LATEST;
	}
	U.busy = false;
	save_state_file();
	SDL_UnlockMutex(U.lock);
	fprintf(stderr, "update: latest release %s (this is %s)%s\n", tag, GBADS_VERSION, newer ? ": newer" : "");
	free(notes);
	return 0;
}

static void start_check(bool automatic) {
	SDL_LockMutex(U.lock);
	if (U.busy) {
		SDL_UnlockMutex(U.lock);
		return;
	}
	U.busy = true;
	U.checkedThisRun = true;
	if (!automatic || U.info.state == UPD_IDLE) U.info.state = automatic ? UPD_IDLE : UPD_CHECKING;
	SDL_UnlockMutex(U.lock);
	SDL_Thread* t = SDL_CreateThread(check_thread, "update-check", automatic ? (void*) 1 : NULL);
	if (t) SDL_DetachThread(t);
	else set_error("Couldn't check for updates%s.", "");
}

/* ------------------------------------------------------------ installing */

static bool mkdir_for(const char* path) {
	char d[1100];
	snprintf(d, sizeof(d), "%s", path);
	char* slash = strrchr(d, '/');
	if (!slash) return true;
	*slash = 0;
	mkdirs(d);
	return true;
}

static bool path_exists(const char* p) {
	struct stat st;
	return stat(p, &st) == 0;
}

static bool ends_with(const char* s, const char* end) {
	size_t a = strlen(s), b = strlen(end);
	return a >= b && !strcmp(s + a - b, end);
}

/* Write vf's contents to dest (0755 if exec). */
static bool write_file(struct VFile* vf, const char* dest, bool exec) {
	mkdir_for(dest);
	int fd = open(dest, O_WRONLY | O_CREAT | O_TRUNC, exec ? 0755 : 0644);
	if (fd < 0) return false;
	char buf[65536];
	ssize_t n;
	bool ok = true;
	vf->seek(vf, 0, SEEK_SET);
	while ((n = vf->read(vf, buf, sizeof(buf))) > 0) {
		if (write(fd, buf, (size_t) n) != n) {
			ok = false;
			break;
		}
	}
	if (n < 0) ok = false;
	if (fsync(fd) != 0 && errno != EINVAL) { /* (a filesystem without fsync is fine) */ }
	if (close(fd) != 0) ok = false;
	if (exec) chmod(dest, 0755);
	return ok;
}

typedef struct { char from[1100], to[1100]; } Move;

/* Unpack the release zip: gba-ds/... into the app's folder, the other
 * top-level files (GBA DS.sh, images/) beside it if they're there already.
 * Everything is written as name.new first, then renamed over the old files
 * (a running program or script keeps its old copy). The player's
 * gba-ds.cfg is kept. */
static bool install_zip(const char* zip, char* err, size_t errSize) {
	struct VDir* d = VDirOpenArchive(zip);
	if (!d) {
		snprintf(err, errSize, "the download isn't a zip");
		return false;
	}
	char tmp[1024], parent[1024];
	snprintf(tmp, sizeof(tmp), "%s", U.installDir);
	snprintf(parent, sizeof(parent), "%s", dirname(tmp));
	Move* moves = NULL;
	int nMoves = 0, cap = 0;
	bool ok = true, sawBinary = false;
	char newBinary[1100] = "";
	d->rewind(d);
	struct VDirEntry* e;
	while (ok && (e = d->listNext(d))) {
		const char* name = e->name(e);
		if (!name || !name[0] || ends_with(name, "/") || e->type(e) == VFS_DIRECTORY) continue;
		if (strstr(name, "..") || name[0] == '/') continue;
		char dest[1100];
		bool app = !strncmp(name, "gba-ds/", 7);
		if (app) {
			const char* rel = name + 7;
			if (!strcmp(rel, "gba-ds.cfg") && path_exists(U.installDir)) {
				char p[1100];
				snprintf(p, sizeof(p), "%s/gba-ds.cfg", U.installDir);
				if (path_exists(p)) continue; /* the player's settings */
			}
			snprintf(dest, sizeof(dest), "%s/%s", U.installDir, rel);
		} else {
			char top[256];
			snprintf(top, sizeof(top), "%s", name);
			char* slash = strchr(top, '/');
			if (slash) *slash = 0;
			char p[1100];
			snprintf(p, sizeof(p), "%s/%s", parent, top);
			if (!path_exists(p)) continue; /* not part of this install */
			snprintf(dest, sizeof(dest), "%s/%s", parent, name);
		}
		bool exec = !strcmp(name, "gba-ds/gba-ds") || ends_with(name, ".sh");
		if (!strcmp(name, "gba-ds/gba-ds")) sawBinary = true;
		struct VFile* vf = d->openFile(d, name, O_RDONLY);
		if (!vf) {
			snprintf(err, errSize, "couldn't read %s in the download", name);
			ok = false;
			break;
		}
		if (nMoves == cap) {
			cap = cap ? cap * 2 : 64;
			moves = realloc(moves, sizeof(Move) * (size_t) cap);
		}
		Move* m = &moves[nMoves++];
		snprintf(m->to, sizeof(m->to), "%s", dest);
		snprintf(m->from, sizeof(m->from), "%.1090s.new", dest);
		if (!write_file(vf, m->from, exec)) {
			snprintf(err, errSize, "couldn't write %s: %s", dest, strerror(errno));
			ok = false;
		}
		/* a copy run from elsewhere (launch.sh's, when the card won't run
		 * programs): the new program goes there too */
		if (ok && !strcmp(name, "gba-ds/gba-ds") && U.exePath[0] && strncmp(U.exePath, U.installDir, strlen(U.installDir)) != 0) {
			snprintf(newBinary, sizeof(newBinary), "%.1090s.new", U.exePath);
			if (!write_file(vf, newBinary, true)) newBinary[0] = 0;
		}
		vf->close(vf);
	}
	d->close(d);
	if (ok && !sawBinary) {
		snprintf(err, errSize, "the download has no gba-ds program in it");
		ok = false;
	}
	for (int i = 0; i < nMoves; ++i) {
		if (!ok) {
			unlink(moves[i].from);
		} else if (rename(moves[i].from, moves[i].to) != 0) {
			snprintf(err, errSize, "couldn't replace %s: %s", moves[i].to, strerror(errno));
			ok = false;
			unlink(moves[i].from);
		}
	}
	if (newBinary[0]) {
		if (ok && rename(newBinary, U.exePath) != 0) unlink(newBinary);
		else if (!ok) unlink(newBinary);
	}
	fprintf(stderr, "update: %s %d files into %s\n", ok ? "installed" : "failed after", nMoves, U.installDir);
	free(moves);
	return ok;
}

static int install_thread(void* ud) {
	(void) ud;
	char zip[1200], err[200] = "", ua[64];
	snprintf(ua, sizeof(ua), "GBA-DS/%s", GBADS_VERSION);
	mkdirs(U.workDir);
	snprintf(zip, sizeof(zip), "%s/%s", U.workDir, U.assetName);
	int code = http_get(U.assetUrl, ua, 600, zip, NULL, NULL, err, sizeof(err), &U.cancel);
	if (U.cancel) {
		fprintf(stderr, "update: download cancelled\n");
		unlink(zip);
		SDL_LockMutex(U.lock);
		U.info.state = UPD_AVAILABLE;
		U.busy = false;
		SDL_UnlockMutex(U.lock);
		return 0;
	}
	if (code / 100 != 2) {
		set_error("Couldn't download the update: %s.", err);
		return 0;
	}
	struct stat st;
	if (U.assetSize > 0 && (stat(zip, &st) != 0 || st.st_size != U.assetSize)) {
		unlink(zip);
		set_error("The download was incomplete%s. Try again.", "");
		return 0;
	}
	if (!strncmp(U.digest, "sha256:", 7)) {
		char hex[65];
		if (!sha256_file_hex(zip, hex) || strcasecmp(hex, U.digest + 7) != 0) {
			unlink(zip);
			set_error("The download didn't match its checksum%s. Try again.", "");
			return 0;
		}
	}
	SDL_LockMutex(U.lock);
	U.info.state = UPD_INSTALLING;
	SDL_UnlockMutex(U.lock);
	bool ok = install_zip(zip, err, sizeof(err));
	unlink(zip);
	if (!ok) {
		set_error("Couldn't install the update: %s.", err);
		return 0;
	}
	SDL_LockMutex(U.lock);
	U.info.state = UPD_DONE;
	U.busy = false;
	if (U.skip[0]) U.skip[0] = 0, save_state_file();
	SDL_UnlockMutex(U.lock);
	return 0;
}

static void progress(void) {
	if (U.info.state != UPD_DOWNLOADING || U.assetSize <= 0) return;
	char part[1300];
	snprintf(part, sizeof(part), "%s/%s.part", U.workDir, U.assetName);
	struct stat st;
	int pc = stat(part, &st) == 0 ? (int) (st.st_size * 100 / U.assetSize) : 0;
	U.info.percent = pc > 100 ? 100 : pc;
}

/* ------------------------------------------------------------ API */

void update_init(const Config* cfg, const char* exeDir, int argc, char** argv) {
	(void) argc;
	U.lock = SDL_CreateMutex();
	U.argv = argv;
	U.autoCfg = -1;
	U.autoDefault = cfg->updateCheck;
	snprintf(U.url, sizeof(U.url), "%s", cfg->updateUrl[0] ? cfg->updateUrl : DEFAULT_URL);
	snprintf(U.stateFile, sizeof(U.stateFile), "%s/update.cfg", cfg->stateDir);
	snprintf(U.workDir, sizeof(U.workDir), "%s/update", cfg->stateDir);
	snprintf(U.installDir, sizeof(U.installDir), "%s", exeDir);
	ssize_t n = readlink("/proc/self/exe", U.exePath, sizeof(U.exePath) - 1);
	U.exePath[n > 0 ? n : 0] = 0;
	load_state_file();
	U.info.autoOn = U.autoCfg >= 0 ? U.autoCfg : U.autoDefault;
	/* leftovers of a download cut short */
	struct VDir* d = VDirOpen(U.workDir);
	if (d) {
		struct VDirEntry* e;
		while ((e = d->listNext(d))) {
			const char* nm = e->name(e);
			if (ends_with(nm, ".part") || ends_with(nm, ".zip")) {
				char p[1400];
				snprintf(p, sizeof(p), "%s/%s", U.workDir, nm);
				unlink(p);
			}
		}
		d->close(d);
	}
}

void update_auto_check(void) {
	if (!U.lock || !U.info.autoOn || U.checkedThisRun) return;
	long now = (long) time(NULL);
	if (U.info.lastCheck > 0 && now - U.info.lastCheck < CHECK_EVERY && now >= U.info.lastCheck) return;
	start_check(true);
}

void update_check_now(void) {
	if (U.lock) start_check(false);
}

void update_install(void) {
	SDL_LockMutex(U.lock);
	if (U.busy || U.info.state != UPD_AVAILABLE || !U.assetUrl[0]) {
		SDL_UnlockMutex(U.lock);
		return;
	}
	U.busy = true;
	U.cancel = false;
	U.notice = false;
	U.info.state = UPD_DOWNLOADING;
	U.info.percent = 0;
	SDL_UnlockMutex(U.lock);
	SDL_Thread* t = SDL_CreateThread(install_thread, "update-install", NULL);
	if (t) SDL_DetachThread(t);
	else set_error("Couldn't start the download%s.", "");
}

void update_cancel(void) {
	fprintf(stderr, "update: cancelling the download\n");
	U.cancel = true;
}

void update_skip(void) {
	SDL_LockMutex(U.lock);
	snprintf(U.skip, sizeof(U.skip), "%s", U.info.version);
	U.notice = false;
	save_state_file();
	SDL_UnlockMutex(U.lock);
}

void update_set_auto(bool on) {
	SDL_LockMutex(U.lock);
	U.autoCfg = on;
	U.info.autoOn = on;
	save_state_file();
	SDL_UnlockMutex(U.lock);
}

void update_info(UpdateInfo* out) {
	if (!U.lock) {
		memset(out, 0, sizeof(*out));
		return;
	}
	SDL_LockMutex(U.lock);
	progress();
	*out = U.info;
	SDL_UnlockMutex(U.lock);
}

bool update_notice_pending(void) {
	if (!U.lock) return false;
	SDL_LockMutex(U.lock);
	bool p = U.notice && !U.noticeSeen && U.info.state == UPD_AVAILABLE;
	SDL_UnlockMutex(U.lock);
	return p;
}

void update_notice_seen(void) {
	if (!U.lock) return;
	SDL_LockMutex(U.lock);
	U.noticeSeen = true;
	SDL_UnlockMutex(U.lock);
}

void update_restart_if_installed(void) {
	if (!U.lock || U.info.state != UPD_DONE || !U.exePath[0] || !U.argv) return;
	char exe[1024];
	snprintf(exe, sizeof(exe), "%s", U.exePath);
	char* del = strstr(exe, " (deleted)");
	if (del) *del = 0;
	fprintf(stderr, "update: restarting %s\n", exe);
	fflush(stderr);
	execv(exe, U.argv);
	fprintf(stderr, "update: couldn't restart: %s\n", strerror(errno));
}

#endif

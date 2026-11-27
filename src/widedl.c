/* GBA DS's widescreen patches, offered for download: widedl.h. */
#include "widedl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#ifndef GBADS_WIDE_REF
#define GBADS_WIDE_REF "main"
#endif
#define WIDE_REPO "https://raw.githubusercontent.com/kyannon/gba-ds/" GBADS_WIDE_REF "/widescreen/"
#define WIDE_MAX 64

const char* widedl_base(void) {
	const char* u = getenv("GBADS_WIDE_URL");
	return u && u[0] ? u : WIDE_REPO;
}

const WideEntry* widedl_nearest(const WideEntry* e, int n, int w, int h) {
	const WideEntry* best = NULL;
	double bestD = 0.02;
	if (w <= 0 || h <= 0) return NULL;
	double screen = (double) w / h;
	for (int i = 0; i < n; ++i) {
		double r = (double) e[i].num / e[i].den, d = r > screen ? r / screen - 1 : screen / r - 1;
		if (d <= bestD) best = &e[i], bestD = d;
	}
	return best;
}

#ifdef __wasi__
void widedl_refresh(const char* stateDir) {
	(void) stateDir;
}
int widedl_generation(void) {
	return 0;
}
int widedl_for(const char* code, uint32_t fileCrc, WideEntry* out, int max) {
	(void) code, (void) fileCrc, (void) out, (void) max;
	return 0;
}
bool widedl_fetch(const WideEntry* e, const char* dir, char* err, size_t errSize) {
	(void) e, (void) dir;
	snprintf(err, errSize, "No downloads in the web player");
	return false;
}
#else
#include <pthread.h>
#include <unistd.h>
#include "http.h"
#include "sha256.h"

static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static WideEntry list[WIDE_MAX];
static int nList, generation;
static bool started;
static char cachePath[1100];

static const char* user_agent(void) {
#ifdef GBADS_VERSION
	return "GBADS/" GBADS_VERSION;
#else
	return "GBADS";
#endif
}

/* a path under widescreen/ as a URL: spaces and the like escaped */
static void url_of(const char* path, char* out, size_t size) {
	size_t n = (size_t) snprintf(out, size, "%s", widedl_base());
	for (const char* p = path; *p && n + 4 < size; ++p) {
		unsigned char c = (unsigned char) *p;
		if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || strchr("-_.~/()", c)) out[n++] = (char) c;
		else n += (size_t) snprintf(out + n, size - n, "%%%02X", c);
	}
	out[n < size ? n : size - 1] = 0;
}

/* index.txt: tab-separated: code, ratio, path, SHA-256, CRC32s (comma-separated, or -) */
static int parse(const char* text, WideEntry* out, int max) {
	int n = 0;
	const char* p = text;
	while (*p && n < max) {
		const char* eol = strchr(p, '\n');
		size_t len = eol ? (size_t) (eol - p) : strlen(p);
		char line[1024];
		snprintf(line, sizeof(line), "%.*s", (int) (len < sizeof(line) - 1 ? len : sizeof(line) - 1), p);
		p = eol ? eol + 1 : p + len;
		if (line[0] == '#' || !line[0]) continue;
		char* f[5];
		int k = 0;
		char* s = line;
		for (; k < 5 && s; ++k) {
			f[k] = s;
			s = strchr(s, '\t');
			if (s) *s++ = 0;
		}
		if (k < 5) continue;
		WideEntry e;
		memset(&e, 0, sizeof(e));
		char* cr = strchr(f[4], '\r');
		if (cr) *cr = 0;
		if (strlen(f[0]) != 4 || sscanf(f[1], "%d:%d", &e.num, &e.den) != 2 || e.num <= 0 || e.den <= 0 || strlen(f[3]) != 64) continue;
		if (strstr(f[2], "..") || f[2][0] == '/') continue;
		memcpy(e.code, f[0], 4);
		snprintf(e.path, sizeof(e.path), "%s", f[2]);
		const char* base = strrchr(f[2], '/');
		snprintf(e.file, sizeof(e.file), "%s", base ? base + 1 : f[2]);
		snprintf(e.sha256, sizeof(e.sha256), "%s", f[3]);
		for (char* c = f[4]; *c && e.nCrcs < 8;) {
			char* end = NULL;
			unsigned long v = strtoul(c, &end, 16);
			if (end == c) break;
			e.crcs[e.nCrcs++] = (uint32_t) v;
			c = *end == ',' ? end + 1 : end;
		}
		out[n++] = e;
	}
	return n;
}

static bool load(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) return false;
	char* text = calloc(1, 256 * 1024);
	size_t got = text ? fread(text, 1, 256 * 1024 - 1, f) : 0;
	fclose(f);
	if (!text) return false;
	text[got] = 0;
	WideEntry* tmp = calloc(WIDE_MAX, sizeof(WideEntry));
	int n = tmp ? parse(text, tmp, WIDE_MAX) : 0;
	bool ok = tmp && strstr(text, "# GBA DS widescreen patches") == text;
	if (ok) {
		pthread_mutex_lock(&lock);
		memcpy(list, tmp, sizeof(WideEntry) * (size_t) n);
		nList = n;
		++generation;
		pthread_mutex_unlock(&lock);
	}
	free(tmp);
	free(text);
	return ok;
}

static void* fetch_list(void* arg) {
	(void) arg;
	char url[1400], tmp[1200], err[200] = "";
	url_of("index.txt", url, sizeof(url));
	snprintf(tmp, sizeof(tmp), "%s.dl", cachePath);
	int code = http_get(url, user_agent(), 15, tmp, NULL, NULL, err, sizeof(err), NULL);
	if (code == 200 && load(tmp)) {
		rename(tmp, cachePath);
		fprintf(stderr, "widescreen: list fetched (%s)\n", url);
	} else {
		unlink(tmp);
		fprintf(stderr, "widescreen: list not fetched from %s: %d %s\n", url, code, err);
	}
	return NULL;
}

void widedl_refresh(const char* stateDir) {
	if (started) return;
	started = true;
	snprintf(cachePath, sizeof(cachePath), "%s/widescreen-index.txt", stateDir);
	load(cachePath);
	pthread_t t;
	if (!pthread_create(&t, NULL, fetch_list, NULL)) pthread_detach(t);
}

int widedl_generation(void) {
	pthread_mutex_lock(&lock);
	int g = generation;
	pthread_mutex_unlock(&lock);
	return g;
}

int widedl_for(const char* code, uint32_t fileCrc, WideEntry* out, int max) {
	int n = 0;
	pthread_mutex_lock(&lock);
	for (int i = 0; i < nList && n < max; ++i) {
		if (strncmp(list[i].code, code, 4)) continue;
		bool fits = list[i].nCrcs == 0;
		for (int k = 0; k < list[i].nCrcs && !fits; ++k) fits = list[i].crcs[k] == fileCrc;
		if (fits) out[n++] = list[i];
	}
	pthread_mutex_unlock(&lock);
	return n;
}

bool widedl_fetch(const WideEntry* e, const char* dir, char* err, size_t errSize) {
	char url[1400], dest[1400], tmp[1500], why[200] = "";
	url_of(e->path, url, sizeof(url));
	snprintf(dest, sizeof(dest), "%s/%s", dir, e->file);
	snprintf(tmp, sizeof(tmp), "%s.dl", dest);
	int code = http_get(url, user_agent(), 30, tmp, NULL, NULL, why, sizeof(why), NULL);
	if (code != 200) {
		unlink(tmp);
		if (code < 0) snprintf(err, errSize, "Couldn't download it: %s", why);
		else snprintf(err, errSize, "Couldn't download it (the server said %d)", code);
		fprintf(stderr, "widescreen: %s: %d %s\n", url, code, why);
		return false;
	}
	char hex[65] = "";
	if (!sha256_file_hex(tmp, hex) || strcasecmp(hex, e->sha256)) {
		unlink(tmp);
		snprintf(err, errSize, "The download didn't match GBA DS's list: try again later");
		fprintf(stderr, "widescreen: %s: SHA-256 %s, the list says %s\n", url, hex, e->sha256);
		return false;
	}
	if (rename(tmp, dest)) {
		unlink(tmp);
		snprintf(err, errSize, "Couldn't save it in the patches folder");
		return false;
	}
	fprintf(stderr, "widescreen: downloaded %s to %s\n", url, dest);
	return true;
}
#endif

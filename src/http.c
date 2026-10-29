#include "http.h"

#if !defined(__ANDROID__) && !defined(__wasi__)

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char** environ;

static bool have_curl_program(void) {
	static int found = -1;
	if (found >= 0) return found;
	found = 0;
	const char* path = getenv("PATH");
	char dir[1024];
	while (path && *path && !found) {
		size_t n = strcspn(path, ":");
		snprintf(dir, sizeof(dir), "%.*s/curl", (int) n, path);
		if (access(dir, X_OK) == 0) found = 1;
		path += n + (path[n] == ':');
	}
	return found;
}

/* ------------------------------------------------------------ the curl program */

static int curl_program(const char* url, const char* ua, int timeout, const char* part, char** body, size_t* len, char* err,
                        size_t errSize, volatile bool* cancel) {
	char tmo[16];
	snprintf(tmo, sizeof(tmo), "%d", timeout);
	char* argv[24];
	int n = 0;
	argv[n++] = "curl";
	argv[n++] = "-sS";
	argv[n++] = "-L";
	argv[n++] = "--max-time";
	argv[n++] = tmo;
	argv[n++] = "-A";
	argv[n++] = (char*) ua;
	argv[n++] = "-H";
	argv[n++] = "Accept: application/vnd.github+json";
	argv[n++] = "-w";
	argv[n++] = "%{http_code}";
	if (part) {
		argv[n++] = "-o";
		argv[n++] = (char*) part;
	}
	argv[n++] = (char*) url;
	argv[n] = NULL;
	int po[2], pe[2];
	if (pipe(po)) return -1;
	if (pipe(pe)) {
		close(po[0]), close(po[1]);
		return -1;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	posix_spawn_file_actions_adddup2(&fa, po[1], 1);
	posix_spawn_file_actions_adddup2(&fa, pe[1], 2);
	posix_spawn_file_actions_addclose(&fa, po[0]);
	posix_spawn_file_actions_addclose(&fa, po[1]);
	posix_spawn_file_actions_addclose(&fa, pe[0]);
	posix_spawn_file_actions_addclose(&fa, pe[1]);
	/* SDL's threads block SIGTERM and others, and a child inherits that:
	 * give curl a clean mask so a cancel can stop it */
	posix_spawnattr_t at;
	posix_spawnattr_init(&at);
	sigset_t none, all;
	sigemptyset(&none);
	sigfillset(&all);
	posix_spawnattr_setsigmask(&at, &none);
	posix_spawnattr_setsigdefault(&at, &all);
	posix_spawnattr_setflags(&at, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF);
	pid_t pid;
	int e = posix_spawnp(&pid, "curl", &fa, &at, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	posix_spawnattr_destroy(&at);
	close(po[1]), close(pe[1]);
	if (e) {
		close(po[0]), close(pe[0]);
		snprintf(err, errSize, "can't run curl: %s", strerror(e));
		return -1;
	}
	/* stdout: the body (no outPath) then the status code; stderr: why */
	size_t cap = 65536, got = 0;
	char* buf = malloc(cap + 1);
	char ebuf[512];
	size_t elen = 0;
	/* read both pipes until both close, checking *cancel every 200 ms
	 * (a download to a file says nothing on stdout until it ends) */
	bool outOpen = true, errOpen = true, killed = false;
	while (outOpen || errOpen) {
		if (cancel && *cancel && !killed) kill(pid, SIGTERM), killed = true;
		struct pollfd pf[2] = { { outOpen ? po[0] : -1, POLLIN, 0 }, { errOpen ? pe[0] : -1, POLLIN, 0 } };
		int pr = poll(pf, 2, 200);
		if (pr < 0 && errno == EINTR) continue;
		if (pr < 0) break;
		if (pf[0].revents) {
			if (got == cap) {
				char* nb = realloc(buf, cap * 2 + 1);
				if (!nb) break;
				buf = nb, cap *= 2;
			}
			ssize_t r = read(po[0], buf + got, cap - got);
			if (r > 0) got += (size_t) r;
			else if (r == 0 || errno != EINTR) outOpen = false;
		}
		if (pf[1].revents) {
			char tmp[256];
			ssize_t q = read(pe[0], tmp, sizeof(tmp));
			if (q > 0) {
				size_t k = SDL_min((size_t) q, sizeof(ebuf) - 1 - elen);
				memcpy(ebuf + elen, tmp, k);
				elen += k;
			} else if (q == 0 || errno != EINTR) {
				errOpen = false;
			}
		}
	}
	ebuf[elen] = 0;
	close(po[0]), close(pe[0]);
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	buf[got] = 0;
	/* the last 3 characters are the status code */
	int code = -1;
	if (got >= 3) {
		code = atoi(buf + got - 3);
		got -= 3;
		buf[got] = 0;
	}
	int rc = WIFEXITED(st) ? WEXITSTATUS(st) : -1;
	if (rc != 0 || code <= 0) {
		ebuf[strcspn(ebuf, "\n")] = 0;
		snprintf(err, errSize, "%s", cancel && *cancel ? "cancelled" : ebuf[0] ? ebuf : "no answer");
		free(buf);
		return -1;
	}
	if (body) {
		*body = buf;
		if (len) *len = got;
	} else {
		free(buf);
	}
	return code;
}

/* ------------------------------------------------------------ libcurl */

typedef void CURL;
struct curl_slist;
static struct {
	bool tried, ok;
	CURL* (*easy_init)(void);
	int (*easy_setopt)(CURL*, int, ...);
	int (*easy_perform)(CURL*);
	int (*easy_getinfo)(CURL*, int, ...);
	void (*easy_cleanup)(CURL*);
	struct curl_slist* (*slist_append)(struct curl_slist*, const char*);
	void (*slist_free_all)(struct curl_slist*);
	const char* (*easy_strerror)(int);
} lc;
enum {
	LC_WRITEDATA = 10001, LC_URL = 10002, LC_TIMEOUT = 13, LC_USERAGENT = 10018, LC_HTTPHEADER = 10023,
	LC_FOLLOWLOCATION = 52, LC_NOSIGNAL = 99, LC_CAINFO = 10065, LC_WRITEFUNCTION = 20011, LC_INFO_RESPONSE_CODE = 0x200002
};
static SDL_mutex* lcLock;

static bool libcurl_load(void) {
	if (lc.tried) return lc.ok;
	lc.tried = true;
	void* h = SDL_LoadObject("libcurl.so.4");
	if (!h) h = SDL_LoadObject("libcurl.so");
	if (!h) return false;
	*(void**) &lc.easy_init = SDL_LoadFunction(h, "curl_easy_init");
	*(void**) &lc.easy_setopt = SDL_LoadFunction(h, "curl_easy_setopt");
	*(void**) &lc.easy_perform = SDL_LoadFunction(h, "curl_easy_perform");
	*(void**) &lc.easy_getinfo = SDL_LoadFunction(h, "curl_easy_getinfo");
	*(void**) &lc.easy_cleanup = SDL_LoadFunction(h, "curl_easy_cleanup");
	*(void**) &lc.slist_append = SDL_LoadFunction(h, "curl_slist_append");
	*(void**) &lc.slist_free_all = SDL_LoadFunction(h, "curl_slist_free_all");
	*(void**) &lc.easy_strerror = SDL_LoadFunction(h, "curl_easy_strerror");
	void (*global_init)(long) = (void (*)(long)) SDL_LoadFunction(h, "curl_global_init");
	lc.ok = lc.easy_init && lc.easy_setopt && lc.easy_perform && lc.easy_getinfo && lc.easy_cleanup && lc.slist_append &&
	        lc.slist_free_all && lc.easy_strerror && global_init;
	if (lc.ok) global_init(3);
	return lc.ok;
}

typedef struct { char* buf; size_t len, cap; FILE* f; volatile bool* cancel; } Sink;

static size_t sink_write(char* p, size_t sz, size_t n, void* ud) {
	Sink* s = ud;
	size_t k = sz * n;
	if (s->cancel && *s->cancel) return 0;
	if (s->f) return fwrite(p, 1, k, s->f);
	if (s->len + k + 1 > s->cap) {
		size_t cap = (s->len + k + 1) * 2;
		char* nb = realloc(s->buf, cap);
		if (!nb) return 0;
		s->buf = nb, s->cap = cap;
	}
	memcpy(s->buf + s->len, p, k);
	s->len += k;
	s->buf[s->len] = 0;
	return k;
}

static int libcurl_get(const char* url, const char* ua, int timeout, const char* part, char** body, size_t* len, char* err,
                       size_t errSize, volatile bool* cancel) {
	if (!lcLock) lcLock = SDL_CreateMutex();
	SDL_LockMutex(lcLock);
	bool ok = libcurl_load();
	SDL_UnlockMutex(lcLock);
	if (!ok) {
		snprintf(err, errSize, "no curl program and no libcurl");
		return -1;
	}
	CURL* c = lc.easy_init();
	if (!c) {
		snprintf(err, errSize, "libcurl didn't start");
		return -1;
	}
	Sink sink = { .cancel = cancel };
	if (part && !(sink.f = fopen(part, "wb"))) {
		lc.easy_cleanup(c);
		snprintf(err, errSize, "can't write %s", part);
		return -1;
	}
	lc.easy_setopt(c, LC_URL, url);
	lc.easy_setopt(c, LC_FOLLOWLOCATION, 1L);
	lc.easy_setopt(c, LC_TIMEOUT, (long) timeout);
	lc.easy_setopt(c, LC_NOSIGNAL, 1L);
	lc.easy_setopt(c, LC_USERAGENT, ua);
	if (access("/etc/ssl/certs/ca-certificates.crt", R_OK) == 0) lc.easy_setopt(c, LC_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
	struct curl_slist* hdr = lc.slist_append(NULL, "Accept: application/vnd.github+json");
	lc.easy_setopt(c, LC_HTTPHEADER, hdr);
	lc.easy_setopt(c, LC_WRITEFUNCTION, sink_write);
	lc.easy_setopt(c, LC_WRITEDATA, &sink);
	int rc = lc.easy_perform(c);
	long code = 0;
	lc.easy_getinfo(c, LC_INFO_RESPONSE_CODE, &code);
	if (rc) snprintf(err, errSize, "%s", cancel && *cancel ? "cancelled" : lc.easy_strerror(rc));
	lc.slist_free_all(hdr);
	lc.easy_cleanup(c);
	if (sink.f) fclose(sink.f);
	if (rc || code <= 0) {
		free(sink.buf);
		return -1;
	}
	if (body) {
		*body = sink.buf ? sink.buf : strdup("");
		if (len) *len = sink.len;
	} else {
		free(sink.buf);
	}
	return (int) code;
}

int http_get(const char* url, const char* userAgent, int timeoutSec, const char* outPath, char** body, size_t* len,
             char* err, size_t errSize, volatile bool* cancel) {
	char dummy[8];
	if (!err) err = dummy, errSize = sizeof(dummy);
	err[0] = 0;
	if (body) *body = NULL;
	char part[1200] = "";
	if (outPath) snprintf(part, sizeof(part), "%s.part", outPath);
	int code = have_curl_program()
	               ? curl_program(url, userAgent, timeoutSec, outPath ? part : NULL, body, len, err, errSize, cancel)
	               : libcurl_get(url, userAgent, timeoutSec, outPath ? part : NULL, body, len, err, errSize, cancel);
	if (outPath) {
		if (code / 100 == 2 && !(cancel && *cancel)) {
			if (rename(part, outPath) != 0) {
				snprintf(err, errSize, "can't save the download: %s", strerror(errno));
				code = -1;
			}
		} else {
			unlink(part);
		}
	}
	if (code > 0 && code / 100 != 2 && !err[0]) snprintf(err, errSize, "the server answered %d", code);
	return code;
}

#endif

#ifdef __ANDROID__
/* Android: no curl; Java's HttpURLConnection (Http.java, through android.c),
 * which uses the system's certificates. A download is cancelled with
 * android_http_cancel(); timeoutSec is Java's own (15 s to connect, 30 s
 * without data). */
#include "android.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

int http_get(const char* url, const char* userAgent, int timeoutSec, const char* outPath, char** body, size_t* len,
             char* err, size_t errSize, volatile bool* cancel) {
	(void) timeoutSec;
	char dummy[8];
	if (!err) err = dummy, errSize = sizeof(dummy);
	err[0] = 0;
	if (outPath) android_http_cancel(false);
	int code = android_http(url, NULL, NULL, userAgent, outPath, body, len);
	if (outPath && (code / 100 != 2 || (cancel && *cancel))) {
		char part[1200];
		snprintf(part, sizeof(part), "%s.part", outPath);
		unlink(part);
		unlink(outPath);
	}
	if (cancel && *cancel) snprintf(err, errSize, "cancelled");
	else if (code < 0) snprintf(err, errSize, "no answer from the server (is the device online?)");
	else if (code / 100 != 2) snprintf(err, errSize, "the server answered %d", code);
	return code;
}
#endif

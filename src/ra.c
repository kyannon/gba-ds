/* RetroAchievements (see ra.h). */
#ifdef GBADS_RA

#include "ra.h"
#include "i18n.h"
#include "paths.h"
#include "textfont.h"
#include "ui.h"

#include "rc_api_runtime.h"
#include "rc_client.h"
#include "rc_util.h"
#include "rhash/md5.h"

#ifdef __ANDROID__
#include "android.h"
#endif

#include <mgba/core/version.h>

#include <SDL.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#ifndef __ANDROID__
#include <spawn.h>
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

/* The server adds "Warning: ..." achievements (unknown emulator, outdated
 * client...) with ids from here on: not real ones, so never listed. */
#define WARNING_ID 101000001u

#ifndef __ANDROID__
extern char** environ;
#endif

static rc_client_t* client;
static const Config* cfg;
static char userAgent[160];
static char cacheDir[1100]; /* <state_dir>/ra: login token, badges */

/* ------------------------------------------------------------ HTTP (curl; Android: Java's HttpURLConnection) */

/* One request for the worker thread: a server call for rc_client (its
 * response goes back to the main thread), or a file to download. */
typedef struct Job {
	struct Job* next;
	char* url;
	char* post;  /* NULL: GET */
	char* ctype;
	rc_client_server_callback_t cb;
	void* cbData;
	char* outPath; /* download: the file to write */
	char* body;
	size_t len;
	int status;
	char qkey[96]; /* an unlock or leaderboard entry kept on disk until the server has it (see Pending) */
} Job;

static pthread_mutex_t qLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t qCond = PTHREAD_COND_INITIALIZER;
static Job *calls, *downloads, *done; /* calls go before downloads */
static bool workerUp, workerQuit, workerIdle = true;
static pthread_t worker;

static char* dupstr(const char* s) {
	return s ? strdup(s) : NULL;
}

static void job_free(Job* j) {
	free(j->url);
	free(j->post);
	free(j->ctype);
	free(j->outPath);
	free(j->body);
	free(j);
}

static void push(Job** list, Job* j, bool front) {
	j->next = NULL;
	if (front || !*list) {
		j->next = *list;
		*list = j;
		return;
	}
	Job* t = *list;
	while (t->next) t = t->next;
	t->next = j;
}

#ifdef __ANDROID__
/* Android has no curl: the request goes through Java (android.c, Http.java),
 * from this worker thread. */
static void run_job(Job* j) {
	if (j->outPath) {
		if (android_http(j->url, NULL, NULL, userAgent, j->outPath, NULL, NULL) / 100 != 2) {
			fprintf(stderr, "ra: download failed: %s\n", j->url);
		}
		return;
	}
	char* body = NULL;
	size_t len = 0;
	const char* ctype = j->post ? (j->ctype ? j->ctype : "application/x-www-form-urlencoded") : NULL;
	int code = android_http(j->url, ctype, j->post, userAgent, NULL, &body, &len);
	j->status = code > 0 ? code : RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
	j->body = body;
	j->len = body ? len : 0;
}
#else
/* Runs curl with argv; its output goes to *out (malloc'd), stdin gets
 * `in` (may be NULL). Returns curl's exit status (-1: couldn't run it). */
static int run_curl(char* const* argv, const char* in, char** out, size_t* outLen) {
	int po[2], pi[2] = { -1, -1 };
	if (pipe(po)) return -1;
	if (in && pipe(pi)) {
		close(po[0]), close(po[1]);
		return -1;
	}
	posix_spawn_file_actions_t fa;
	posix_spawn_file_actions_init(&fa);
	posix_spawn_file_actions_adddup2(&fa, po[1], 1);
	posix_spawn_file_actions_addclose(&fa, po[0]);
	posix_spawn_file_actions_addclose(&fa, po[1]);
	if (in) {
		posix_spawn_file_actions_adddup2(&fa, pi[0], 0);
		posix_spawn_file_actions_addclose(&fa, pi[0]);
		posix_spawn_file_actions_addclose(&fa, pi[1]);
	} else {
		posix_spawn_file_actions_addopen(&fa, 0, "/dev/null", O_RDONLY, 0);
	}
	pid_t pid;
	int err = posix_spawnp(&pid, "curl", &fa, NULL, argv, environ);
	posix_spawn_file_actions_destroy(&fa);
	close(po[1]);
	if (in) close(pi[0]);
	if (err) {
		close(po[0]);
		if (in) close(pi[1]);
		fprintf(stderr, "ra: can't run curl: %s\n", strerror(err));
		return -1;
	}
	if (in) {
		/* requests are small (well under a pipe's buffer) */
		size_t n = strlen(in), off = 0;
		while (off < n) {
			ssize_t w = write(pi[1], in + off, n - off);
			if (w <= 0) break;
			off += (size_t) w;
		}
		close(pi[1]);
	}
	size_t cap = 4096, len = 0;
	char* buf = malloc(cap + 1);
	for (;;) {
		if (len == cap) {
			char* nb = realloc(buf, cap * 2 + 1);
			if (!nb) break;
			buf = nb, cap *= 2;
		}
		ssize_t r = read(po[0], buf + len, cap - len);
		if (r < 0 && errno == EINTR) continue;
		if (r <= 0) break;
		len += (size_t) r;
	}
	close(po[0]);
	buf[len] = 0;
	int st = 0;
	while (waitpid(pid, &st, 0) < 0 && errno == EINTR) {}
	if (out) {
		*out = buf;
		*outLen = len;
	} else {
		free(buf);
	}
	return WIFEXITED(st) ? WEXITSTATUS(st) : -1;
}

/* No curl program (BananaOS ships libcurl without it): the library itself,
 * loaded at run time (SDL_LoadObject), so nothing new to link against. The
 * option and info numbers are libcurl's stable ABI. */
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
	LC_WRITEDATA = 10001, LC_URL = 10002, LC_TIMEOUT = 13, LC_POSTFIELDS = 10015, LC_USERAGENT = 10018,
	LC_HTTPHEADER = 10023, LC_FAILONERROR = 45, LC_FOLLOWLOCATION = 52, LC_POSTFIELDSIZE = 60, LC_NOSIGNAL = 99,
	LC_CAINFO = 10065, LC_WRITEFUNCTION = 20011, LC_INFO_RESPONSE_CODE = 0x200002
};

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

static bool libcurl_load(void) {
	if (lc.tried) return lc.ok;
	lc.tried = true;
	void* h = SDL_LoadObject("libcurl.so.4");
	if (!h) h = SDL_LoadObject("libcurl.so");
	if (!h) {
		fprintf(stderr, "ra: no curl program and no libcurl: can't reach the server\n");
		return false;
	}
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
	if (lc.ok) global_init(3); /* CURL_GLOBAL_DEFAULT */
	fprintf(stderr, "ra: no curl program: %s\n", lc.ok ? "using libcurl" : "libcurl incomplete, can't reach the server");
	return lc.ok;
}

typedef struct { char* buf; size_t len, cap; FILE* f; } LcSink;

static size_t lc_write(char* p, size_t sz, size_t n, void* ud) {
	LcSink* s = ud;
	size_t k = sz * n;
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

static void run_job_libcurl(Job* j) {
	j->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
	if (!libcurl_load()) return;
	CURL* c = lc.easy_init();
	if (!c) return;
	LcSink sink = { 0 };
	char tmp[1200] = "";
	if (j->outPath) {
		snprintf(tmp, sizeof(tmp), "%s.part", j->outPath);
		sink.f = fopen(tmp, "wb");
		if (!sink.f) {
			lc.easy_cleanup(c);
			return;
		}
		lc.easy_setopt(c, LC_FAILONERROR, 1L);
	}
	lc.easy_setopt(c, LC_URL, j->url);
	lc.easy_setopt(c, LC_FOLLOWLOCATION, 1L);
	lc.easy_setopt(c, LC_TIMEOUT, 30L);
	lc.easy_setopt(c, LC_NOSIGNAL, 1L);
	lc.easy_setopt(c, LC_USERAGENT, userAgent);
	if (access("/etc/ssl/certs/ca-certificates.crt", R_OK) == 0) lc.easy_setopt(c, LC_CAINFO, "/etc/ssl/certs/ca-certificates.crt");
	lc.easy_setopt(c, LC_WRITEFUNCTION, lc_write);
	lc.easy_setopt(c, LC_WRITEDATA, &sink);
	struct curl_slist* hdr = NULL;
	char ctype[300];
	if (j->post && !j->outPath) {
		snprintf(ctype, sizeof(ctype), "Content-Type: %s", j->ctype ? j->ctype : "application/x-www-form-urlencoded");
		hdr = lc.slist_append(NULL, ctype);
		lc.easy_setopt(c, LC_HTTPHEADER, hdr);
		lc.easy_setopt(c, LC_POSTFIELDSIZE, (long) strlen(j->post));
		lc.easy_setopt(c, LC_POSTFIELDS, j->post);
	}
	int rc = lc.easy_perform(c);
	long code = 0;
	lc.easy_getinfo(c, LC_INFO_RESPONSE_CODE, &code);
	if (rc) fprintf(stderr, "ra: %s: %s\n", j->outPath ? "download failed" : "request failed", lc.easy_strerror(rc));
	if (hdr) lc.slist_free_all(hdr);
	lc.easy_cleanup(c);
	if (j->outPath) {
		fclose(sink.f);
		if (rc == 0) rename(tmp, j->outPath);
		else unlink(tmp);
		free(sink.buf);
		return;
	}
	if (rc == 0 && code > 0) j->status = (int) code;
	j->body = sink.buf;
	j->len = rc == 0 ? sink.len : 0;
}

static void run_job(Job* j) {
	if (!have_curl_program()) {
		run_job_libcurl(j);
		return;
	}
	char* argv[24];
	int n = 0;
	argv[n++] = "curl";
	argv[n++] = "-sS";
	argv[n++] = "-L";
	argv[n++] = "--max-time";
	argv[n++] = "30";
	argv[n++] = "-A";
	argv[n++] = userAgent;
	if (j->outPath) {
		char tmp[1200];
		snprintf(tmp, sizeof(tmp), "%s.part", j->outPath);
		argv[n++] = "-f";
		argv[n++] = "-o";
		argv[n++] = tmp;
		argv[n++] = j->url;
		argv[n] = NULL;
		if (run_curl(argv, NULL, NULL, NULL) == 0) {
			rename(tmp, j->outPath);
		} else {
			unlink(tmp);
			fprintf(stderr, "ra: download failed: %s\n", j->url);
		}
		return;
	}
	char ctype[300];
	argv[n++] = "-w";
	argv[n++] = "\n%{http_code}";
	if (j->post) {
		snprintf(ctype, sizeof(ctype), "Content-Type: %s", j->ctype ? j->ctype : "application/x-www-form-urlencoded");
		argv[n++] = "-H";
		argv[n++] = ctype;
		argv[n++] = "--data-binary";
		argv[n++] = "@-";
	}
	argv[n++] = j->url;
	argv[n] = NULL;
	char* out = NULL;
	size_t len = 0;
	int rc = run_curl(argv, j->post, &out, &len);
	j->status = RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
	if (rc == 0 && out) {
		/* the body, then "\n" and the HTTP status */
		char* nl = strrchr(out, '\n');
		if (nl) {
			int code = atoi(nl + 1);
			*nl = 0;
			len = (size_t) (nl - out);
			if (code > 0) j->status = code;
		}
	} else if (out) {
		len = 0;
		out[0] = 0;
	}
	j->body = out;
	j->len = len;
}
#endif

static void* worker_main(void* arg) {
	(void) arg;
#ifdef __ANDROID__
	android_pin_worker_thread("RetroAchievements");
#endif
	pthread_mutex_lock(&qLock);
	for (;;) {
		while (!calls && !downloads && !workerQuit) {
			workerIdle = true;
			pthread_cond_broadcast(&qCond);
			pthread_cond_wait(&qCond, &qLock);
		}
		if (!calls && (!downloads || workerQuit)) break; /* quitting: finish the calls, skip the downloads */
		Job* j = calls ? calls : downloads;
		if (j == calls) calls = j->next;
		else downloads = j->next;
		workerIdle = false;
		pthread_mutex_unlock(&qLock);
		run_job(j);
		pthread_mutex_lock(&qLock);
		if (j->outPath) job_free(j);
		else push(&done, j, false);
	}
	workerIdle = true;
	pthread_cond_broadcast(&qCond);
	pthread_mutex_unlock(&qLock);
	return NULL;
}

static void submit(Job* j, bool download, bool front) {
	pthread_mutex_lock(&qLock);
	if (!workerUp) {
		workerUp = pthread_create(&worker, NULL, worker_main, NULL) == 0;
	}
	push(download ? &downloads : &calls, j, front);
	pthread_cond_broadcast(&qCond);
	pthread_mutex_unlock(&qLock);
}

/* ------------------------------------------------------------ unlocks the server hasn't got yet */

/* An achievement unlock or a leaderboard entry is kept in <state_dir>/ra/pending.txt
 * from the moment rc_client sends it until the server has answered, so one earned
 * while the connection is down (rc_client retries those, but only while the app
 * runs) is still sent after the app is closed: at the next sign-in, before the
 * game's achievements load, so it isn't earned twice. The token isn't kept (the
 * request is built again with the current one, and the time since it was earned
 * brought up to date). The file is the player's own (mode 0600), one line each:
 *   A|L queued-time user id hardcore|score game-hash seconds-since */
typedef struct Pending {
	struct Pending* next;
	char key[96];
	bool leaderboard, sending;
	char user[64], hash[33];
	uint32_t id, hardcore, since;
	int32_t score;
	time_t queued;
} Pending;

#define PENDING_MAX 300
#define PENDING_DAYS 60

static Pending* pendingList;
static bool pendingRead;
static int replayBusy; /* kept unlocks on their way (the game's achievements load after them) */

static void pending_path(char* out, size_t n) {
	snprintf(out, n, "%s/pending.txt", cacheDir);
}

static int pending_count(void) {
	int n = 0;
	for (Pending* p = pendingList; p; p = p->next) ++n;
	return n;
}

static void pending_save(void) {
	char path[1200], tmp[1210];
	pending_path(path, sizeof(path));
	if (!pendingList) {
		unlink(path);
		return;
	}
	snprintf(tmp, sizeof(tmp), "%s.part", path);
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	FILE* f = fd < 0 ? NULL : fdopen(fd, "w");
	if (!f) {
		if (fd >= 0) close(fd);
		return;
	}
	for (Pending* p = pendingList; p; p = p->next) {
		fprintf(f, "%c %lld %s %u %d %s %u\n", p->leaderboard ? 'L' : 'A', (long long) p->queued, p->user, p->id,
		        p->leaderboard ? (int) p->score : (int) p->hardcore, p->hash[0] ? p->hash : "-", p->since);
	}
	fclose(f);
	rename(tmp, path);
}

static void pending_key(Pending* p) {
	snprintf(p->key, sizeof(p->key), "%c%u:%d:", p->leaderboard ? 'L' : 'A', p->id, p->leaderboard ? (int) p->score : (int) p->hardcore);
	size_t n = strlen(p->key);
	for (const char* u = p->user; *u && n + 1 < sizeof(p->key); ++u) p->key[n++] = (char) tolower((unsigned char) *u);
	p->key[n] = 0;
}

static Pending* pending_find(const char* key) {
	for (Pending* p = pendingList; p; p = p->next) {
		if (!strcmp(p->key, key)) return p;
	}
	return NULL;
}

/* the file, once (entries older than PENDING_DAYS are let go) */
static void pending_read(void) {
	if (pendingRead) return;
	pendingRead = true;
	char path[1200];
	pending_path(path, sizeof(path));
	FILE* f = fopen(path, "r");
	if (!f) return;
	time_t now = time(NULL);
	Pending** tail = &pendingList;
	char line[256];
	bool dropped = false;
	while (fgets(line, sizeof(line), f)) {
		Pending* p = calloc(1, sizeof(*p));
		char kind = 0, hash[40] = "";
		long long queued = 0;
		int v = 0;
		if (!p || sscanf(line, "%c %lld %63s %u %d %39s %u", &kind, &queued, p->user, &p->id, &v, hash, &p->since) != 7 ||
		    (kind != 'A' && kind != 'L') || now - (time_t) queued > PENDING_DAYS * 86400 || pending_count() >= PENDING_MAX) {
			free(p);
			dropped = true;
			continue;
		}
		p->leaderboard = kind == 'L';
		p->queued = (time_t) queued;
		if (p->leaderboard) p->score = v;
		else p->hardcore = (uint32_t) v;
		snprintf(p->hash, sizeof(p->hash), "%.32s", strcmp(hash, "-") ? hash : "");
		pending_key(p);
		*tail = p;
		tail = &p->next;
	}
	fclose(f);
	if (dropped) pending_save();
}

/* a parameter of a urlencoded request ("r=awardachievement&u=...") */
static bool post_param(const char* post, const char* name, char* out, size_t n) {
	size_t nl = strlen(name);
	for (const char* q = post; q && *q;) {
		if (!strncmp(q, name, nl) && q[nl] == '=') {
			size_t len = strcspn(q + nl + 1, "&");
			if (len >= n) return false;
			memcpy(out, q + nl + 1, len);
			out[len] = 0;
			return true;
		}
		q = strchr(q, '&');
		if (q) ++q;
	}
	return false;
}

/* rc_client is sending this request: if it's an unlock or a leaderboard entry,
 * keep it until the server has answered (rc_client's own retries of it come
 * through here again: they find it kept already). */
static void pending_track(Job* j, const char* post) {
	char r[32], u[64], a[24], h[8], s[24], m[40], o[24];
	if (!post || !post_param(post, "r", r, sizeof(r)) || !post_param(post, "u", u, sizeof(u)) || strchr(u, '%') || strchr(u, '+')) return;
	bool lb = !strcmp(r, "submitlbentry");
	if (!lb && strcmp(r, "awardachievement")) return;
	if (!post_param(post, lb ? "i" : "a", a, sizeof(a))) return;
	Pending probe = { 0 };
	probe.leaderboard = lb;
	probe.id = (uint32_t) strtoul(a, NULL, 10);
	if (lb) probe.score = post_param(post, "s", s, sizeof(s)) ? (int32_t) strtol(s, NULL, 10) : 0;
	else probe.hardcore = post_param(post, "h", h, sizeof(h)) ? (uint32_t) strtoul(h, NULL, 10) : 0;
	snprintf(probe.user, sizeof(probe.user), "%s", u);
	pending_key(&probe);
	pending_read();
	snprintf(j->qkey, sizeof(j->qkey), "%s", probe.key);
	if (pending_find(probe.key) || pending_count() >= PENDING_MAX) return;
	Pending* p = calloc(1, sizeof(*p));
	if (!p) return;
	*p = probe;
	if (post_param(post, "m", m, sizeof(m))) snprintf(p->hash, sizeof(p->hash), "%.32s", m);
	if (post_param(post, "o", o, sizeof(o))) p->since = (uint32_t) strtoul(o, NULL, 10);
	p->queued = time(NULL);
	Pending** t = &pendingList;
	while (*t) t = &(*t)->next;
	*t = p;
	pending_save();
}

/* The server's answer: it has it (or won't ever take it), or it should be tried again. */
static void pending_answer(const char* key, int status) {
	Pending* p = pending_find(key);
	if (!p) return;
	bool retry = status <= 0 || status >= 500 || status == 401 || status == 403 || status == 408 || status == 429;
	if (retry) {
		p->sending = false;
		return;
	}
	for (Pending** t = &pendingList; *t; t = &(*t)->next) {
		if (*t == p) {
			*t = p->next;
			break;
		}
	}
	free(p);
	pending_save();
}

static void begin_load(void);
static bool gameWanted, gameLoaded, loadFailed;
static bool resetWanted; /* RC_CLIENT_EVENT_RESET: hardcore switched on (ra_reset_wanted) */
static int loginState;
enum { LOGIN_OFF, LOGIN_BUSY, LOGIN_OK, LOGIN_FAILED };

static void replay_done(const rc_api_server_response_t* resp, void* ud) {
	(void) resp, (void) ud;
	if (replayBusy > 0 && --replayBusy == 0) {
		fprintf(stderr, "ra: the unlocks kept from earlier are done\n");
		if (gameWanted && !gameLoaded && !loadFailed && loginState == LOGIN_OK) begin_load();
	}
}

/* Signed in: send what's kept for this account (the request built again, with
 * this session's token and the time it has been waiting added). */
static void pending_replay(const char* user, const char* token) {
	pending_read();
	rc_api_host_t host = { cfg->raHost[0] ? cfg->raHost : NULL, NULL };
	time_t now = time(NULL);
	for (Pending* p = pendingList; p; p = p->next) {
		if (p->sending || strcasecmp(p->user, user)) continue;
		uint32_t since = p->since + (now > p->queued ? (uint32_t) (now - p->queued) : 0);
		rc_api_request_t rq;
		int rc;
		if (p->leaderboard) {
			rc_api_submit_lboard_entry_request_t a = { user, token, p->id, p->score, p->hash[0] ? p->hash : NULL, since };
			rc = rc_api_init_submit_lboard_entry_request_hosted(&rq, &a, &host);
		} else {
			rc_api_award_achievement_request_t a = { user, token, p->id, p->hardcore, p->hash[0] ? p->hash : NULL, since };
			rc = rc_api_init_award_achievement_request_hosted(&rq, &a, &host);
		}
		if (rc != RC_OK) continue;
		Job* j = calloc(1, sizeof(*j));
		j->url = dupstr(rq.url);
		j->post = dupstr(rq.post_data);
		j->ctype = dupstr(rq.content_type);
		j->cb = replay_done;
		snprintf(j->qkey, sizeof(j->qkey), "%s", p->key);
		rc_api_destroy_request(&rq);
		p->sending = true;
		++replayBusy;
		fprintf(stderr, "ra: sending %s kept from earlier (%u)\n", p->leaderboard ? "a leaderboard entry" : "an unlock", p->id);
		submit(j, false, false);
	}
}

/* rc_client's server calls */
static void server_call(const rc_api_request_t* req, rc_client_server_callback_t cb, void* cbData, rc_client_t* c) {
	(void) c;
	Job* j = calloc(1, sizeof(*j));
	j->url = dupstr(req->url);
	j->post = dupstr(req->post_data);
	j->ctype = dupstr(req->content_type);
	j->cb = cb;
	j->cbData = cbData;
	pending_track(j, req->post_data);
	submit(j, false, false);
}

/* Hands finished server calls back to rc_client (main thread). */
static void pump(void) {
	pthread_mutex_lock(&qLock);
	Job* list = done;
	done = NULL;
	pthread_mutex_unlock(&qLock);
	while (list) {
		Job* j = list;
		list = j->next;
		if (j->qkey[0]) pending_answer(j->qkey, j->status);
		rc_api_server_response_t resp = { j->body ? j->body : "", j->body ? j->len : 0, j->status };
		j->cb(&resp, j->cbData);
		job_free(j);
	}
}

/* Badges: <state_dir>/ra/badges/<file name in the URL> */
static void badge_path(const char* url, char* out, size_t n) {
	const char* base = url ? strrchr(url, '/') : NULL;
	snprintf(out, n, "%s/badges/%s", cacheDir, base ? base + 1 : "none.png");
}

static void fetch_badge(const char* url, bool soon) {
	if (!url || !*url) return;
	char path[1200];
	badge_path(url, path, sizeof(path));
	if (file_exists(path)) return;
	Job* j = calloc(1, sizeof(*j));
	j->url = strdup(url);
	j->outPath = strdup(path);
	submit(j, true, soon);
}

/* ------------------------------------------------------------ popups */

enum { POP_UNLOCK, POP_GAME, POP_DONE, POP_NOTE }; /* NOTE: a status message, no badge */

typedef struct {
	int kind;
	char kicker[64], title[160], desc[300];
	int points;
	char badge[1200];
	SDL_Texture* tex[2];
	int texW, texH;
	bool texTried;
	Uint32 shownMs, lastMs;
} Popup;

#define MAX_POPUPS 8
static Popup popups[MAX_POPUPS];
static int nPopups;

static void popup_add(int kind, const char* kicker, const char* title, const char* desc, int points, const char* badgeUrl) {
	if (nPopups == MAX_POPUPS) return;
	Popup* p = &popups[nPopups++];
	memset(p, 0, sizeof(*p));
	p->kind = kind;
	snprintf(p->kicker, sizeof(p->kicker), "%s", kicker);
	snprintf(p->title, sizeof(p->title), "%s", title ? title : "");
	snprintf(p->desc, sizeof(p->desc), "%s", desc ? desc : "");
	p->points = points;
	if (badgeUrl && *badgeUrl) {
		badge_path(badgeUrl, p->badge, sizeof(p->badge));
		fetch_badge(badgeUrl, true);
	}
}

static void popup_pop(void) {
	ui_free_image(popups[0].tex);
	memmove(popups, popups + 1, sizeof(Popup) * (size_t) (nPopups - 1));
	--nPopups;
}

/* why the achievement list is empty, while no game is loaded */
static char listStatus[300];

static void note(const char* title, const char* desc) {
	title = tr(title), desc = tr(desc); /* (the app's own messages; the game's and the server's words stay as they come) */
	popup_add(POP_NOTE, "RETROACHIEVEMENTS", title, desc, 0, NULL);
	snprintf(listStatus, sizeof(listStatus), "%s. %s", title, desc);
}

/* text cut to fit width, with "..." */
static void fit(char* out, size_t n, int font, int cap, const char* s, int maxW) {
	snprintf(out, n, "%s", s);
	if (ui_font_width(font, cap, out) <= maxW) return;
	size_t len = strlen(out);
	while (len > 0) {
		do --len;
		while (len > 0 && ((unsigned char) out[len] & 0xC0) == 0x80); /* whole UTF-8 characters */
		while (len > 0 && out[len - 1] == ' ') --len;
		if (len + 4 > n) continue;
		memcpy(out + len, "...", 4);
		if (ui_font_width(font, cap, out) <= maxW) return;
	}
}

static const SDL_Color P_BG = { 30, 26, 52, 245 }, P_EDGE = { 150, 120, 210, 255 }, P_GOLD = { 232, 200, 120, 255 },
                       P_TEXT = { 236, 236, 244, 255 }, P_DIM = { 176, 176, 204, 255 }, P_NAVY = { 16, 32, 96, 255 },
                       P_SHADOW = { 0, 0, 0, 90 }, P_BLANK = { 52, 46, 84, 255 };

#define POP_IN_MS 250

static void draw_popup(int s) {
	if (!nPopups) return;
	Popup* p = &popups[0];
	Uint32 now = SDL_GetTicks();
	/* the clock only runs while it's on screen (not under the pause menu) */
	if (p->lastMs) p->shownMs += SDL_min(now - p->lastMs, 100u);
	p->lastMs = now;
	Uint32 life = p->kind == POP_UNLOCK ? 5000 : 4500;
	if (p->shownMs >= life) {
		popup_pop();
		return;
	}
	if (!p->texTried && p->badge[0] && file_exists(p->badge)) {
		p->texTried = true;
		ui_load_image(p->badge, p->tex, &p->texW, &p->texH, true);
	}

	/* the approved mockup is 1024x768: everything scales from there */
	SDL_Rect r = screens[s].rect;
	float f = SDL_min(r.w / 1024.0f, r.h / 768.0f);
#define F(v) ((int) ((v) * f + 0.5f))
	int cw = F(820), ch = F(116);
	int x = (r.w - cw) / 2, y = F(14);
	/* slide down from above the screen, and back up at the end */
	Uint32 t = p->shownMs, left = life - p->shownMs;
	Uint32 k = SDL_min(t, left);
	if (k < POP_IN_MS) {
		float e = (float) k / POP_IN_MS;
		e = 1 - (1 - e) * (1 - e);
		y = (int) (-ch - F(10) + (y + ch + F(10)) * e);
	}
	ui_fill_round(s, (SDL_Rect) { x, y + F(6), cw, ch }, P_SHADOW, F(22));
	ui_fill_round(s, (SDL_Rect) { x, y, cw, ch }, P_EDGE, F(22));
	ui_fill_round(s, (SDL_Rect) { x + F(3), y + F(3), cw - F(6), ch - F(6) }, P_BG, F(19));

	int tx = x + F(28);
	if (p->kind != POP_NOTE) {
		int b = F(88), bx = x + F(14), by = y + (ch - b) / 2;
		ui_fill_round(s, (SDL_Rect) { bx - F(3), by - F(3), b + F(6), b + F(6) }, P_GOLD, F(9)); /* square badges: corners stay inside */
		if (p->tex[0] || p->tex[1]) {
			SDL_Rect a = { bx, by, b, b };
			ui_image(s, p->tex, p->texW, p->texH, "stretch", &a);
		} else {
			ui_fill_round(s, (SDL_Rect) { bx, by, b, b }, P_BLANK, F(6));
		}
		tx = bx + b + F(20);
	}
	int right = x + cw - F(24);
	if (p->kind == POP_UNLOCK) {
		int pw = F(80), px0 = x + cw - pw - F(18), py0 = y + (ch - pw) / 2;
		ui_fill_round(s, (SDL_Rect) { px0, py0, pw, pw }, P_GOLD, F(18));
		char num[16];
		snprintf(num, sizeof(num), "%d", p->points);
		ui_font_text(s, TF_BOLD, px0 + pw / 2, py0 + F(30), F(30), num, P_NAVY, 0);
		ui_font_text(s, TF_BODY, px0 + pw / 2, py0 + F(61), F(12), p->points == 1 ? "point" : "points", P_NAVY, 0);
		right = px0 - F(16);
	}
	char line[320];
	fit(line, sizeof(line), TF_BOLD, F(13), p->kicker, right - tx);
	ui_font_text(s, TF_BOLD, tx, y + F(27), F(13), line, P_GOLD, -1);
	fit(line, sizeof(line), TF_BOLD, F(24), p->title, right - tx);
	ui_font_text(s, TF_BOLD, tx, y + F(59), F(24), line, P_TEXT, -1);
	/* the description: smaller to fit on one line, then cut */
	int cap = F(15);
	while (cap > F(11) && ui_font_width(TF_BODY, cap, p->desc) > right - tx) --cap;
	fit(line, sizeof(line), TF_BODY, cap, p->desc, right - tx);
	ui_font_text(s, TF_BODY, tx, y + F(94), cap, line, P_DIM, -1);
#undef F
}

/* ------------------------------------------------------------ leaderboards, challenges, progress */

/* What stays on the screen while it's true, in the corners of the screen the
 * popups use: a leaderboard's running value (bottom right), the badge of an
 * achievement whose challenge is under way (bottom left) and, for a few
 * seconds, how far a measured achievement has got (above it). */
#define MAX_TRACKERS 4
#define MAX_INDICATORS 6
#define PROGRESS_MS 4000

typedef struct {
	uint32_t id;
	char badge[1200];
	SDL_Texture* tex[2];
	int w, h;
	bool tried;
} BadgeIcon;

static struct {
	uint32_t id;
	char text[RC_CLIENT_LEADERBOARD_DISPLAY_SIZE];
} trackers[MAX_TRACKERS];
static int nTrackers;
static BadgeIcon challenges[MAX_INDICATORS];
static int nChallenges;
static BadgeIcon progressIcon;
static char progressText[32];
static bool progressOn;
static Uint32 progressUntil;

static void icon_set(BadgeIcon* ic, const rc_client_achievement_t* a) {
	ui_free_image(ic->tex);
	memset(ic, 0, sizeof(*ic));
	ic->id = a->id;
	if (a->badge_url && *a->badge_url) {
		badge_path(a->badge_url, ic->badge, sizeof(ic->badge));
		fetch_badge(a->badge_url, true);
	}
}

static void overlays_clear(void) {
	nTrackers = 0;
	for (int i = 0; i < nChallenges; ++i) ui_free_image(challenges[i].tex);
	nChallenges = 0;
	ui_free_image(progressIcon.tex);
	memset(&progressIcon, 0, sizeof(progressIcon));
	progressOn = false;
}

static void tracker_set(uint32_t id, const char* text) {
	for (int i = 0; i < nTrackers; ++i) {
		if (trackers[i].id == id) {
			snprintf(trackers[i].text, sizeof(trackers[i].text), "%s", text);
			return;
		}
	}
	if (nTrackers == MAX_TRACKERS) return;
	trackers[nTrackers].id = id;
	snprintf(trackers[nTrackers].text, sizeof(trackers[nTrackers].text), "%s", text);
	++nTrackers;
}

static void tracker_hide(uint32_t id) {
	for (int i = 0; i < nTrackers; ++i) {
		if (trackers[i].id == id) {
			memmove(trackers + i, trackers + i + 1, sizeof(trackers[0]) * (size_t) (nTrackers - i - 1));
			--nTrackers;
			return;
		}
	}
}

static void challenge_show(const rc_client_achievement_t* a) {
	for (int i = 0; i < nChallenges; ++i) {
		if (challenges[i].id == a->id) return;
	}
	if (nChallenges == MAX_INDICATORS) return;
	icon_set(&challenges[nChallenges++], a);
}

static void challenge_hide(uint32_t id) {
	for (int i = 0; i < nChallenges; ++i) {
		if (challenges[i].id == id) {
			ui_free_image(challenges[i].tex);
			memmove(challenges + i, challenges + i + 1, sizeof(challenges[0]) * (size_t) (nChallenges - i - 1));
			--nChallenges;
			return;
		}
	}
}

static void progress_show(const rc_client_achievement_t* a) {
	if (progressIcon.id != a->id) icon_set(&progressIcon, a);
	snprintf(progressText, sizeof(progressText), "%s", a->measured_progress);
	progressOn = true;
	progressUntil = SDL_GetTicks() + PROGRESS_MS;
}

static void icon_draw(int s, BadgeIcon* ic, SDL_Rect box, int frame, int radius) {
	if (!ic->tried && ic->badge[0] && file_exists(ic->badge)) {
		ic->tried = true;
		ui_load_image(ic->badge, ic->tex, &ic->w, &ic->h, true);
	}
	ui_fill_round(s, (SDL_Rect) { box.x - frame, box.y - frame, box.w + 2 * frame, box.h + 2 * frame }, P_GOLD, radius + frame);
	if (ic->tex[0] || ic->tex[1]) ui_image(s, ic->tex, ic->w, ic->h, "stretch", &box);
	else ui_fill_round(s, box, P_BLANK, radius);
}

static void draw_overlays(int s) {
	if (progressOn && SDL_GetTicks() >= progressUntil) progressOn = false;
	if (!nTrackers && !nChallenges && !progressOn) return;
	SDL_Rect r = screens[s].rect;
	float f = SDL_min(r.w / 1024.0f, r.h / 768.0f);
#define F(v) ((int) ((v) * f + 0.5f))
	int margin = F(22), bs = F(56), gap = F(10);
	/* challenge badges, left to right along the bottom */
	int y = r.h - margin - bs;
	for (int i = 0; i < nChallenges; ++i) {
		icon_draw(s, &challenges[i], (SDL_Rect) { margin + F(3) + i * (bs + gap + F(6)), y, bs, bs }, F(3), F(8));
	}
	if (progressOn) {
		int ph = F(64), cap = F(26);
		int pw = F(120) + ui_font_width(TF_BOLD, cap, progressText);
		int py = r.h - margin - ph - (nChallenges ? bs + gap + F(8) : 0);
		ui_fill_round(s, (SDL_Rect) { margin, py + F(5), pw, ph }, P_SHADOW, F(18));
		ui_fill_round(s, (SDL_Rect) { margin, py, pw, ph }, P_EDGE, F(18));
		ui_fill_round(s, (SDL_Rect) { margin + F(3), py + F(3), pw - F(6), ph - F(6) }, P_BG, F(15));
		int b = ph - F(16);
		icon_draw(s, &progressIcon, (SDL_Rect) { margin + F(12), py + F(8), b, b }, F(2), F(6));
		ui_font_text(s, TF_BOLD, margin + F(12) + b + F(16), py + ph / 2 + F(1), cap, progressText, P_TEXT, -1);
	}
	/* a leaderboard's running value, right-aligned along the bottom, newest on top */
	int cap = F(26), th = F(52), ty = r.h - margin - th;
	for (int i = 0; i < nTrackers; ++i) {
		int tw = F(40) + ui_font_width(TF_BOLD, cap, trackers[i].text);
		int tx = r.w - margin - tw;
		ui_fill_round(s, (SDL_Rect) { tx, ty + F(5), tw, th }, P_SHADOW, F(16));
		ui_fill_round(s, (SDL_Rect) { tx, ty, tw, th }, P_EDGE, F(16));
		ui_fill_round(s, (SDL_Rect) { tx + F(3), ty + F(3), tw - F(6), th - F(6) }, P_BG, F(13));
		ui_font_text(s, TF_BOLD, tx + tw / 2, ty + th / 2 + F(1), cap, trackers[i].text, P_GOLD, 0);
		ty -= th + F(10);
	}
#undef F
}

void ra_draw(int s) {
	draw_popup(s);
	draw_overlays(s);
}

/* ------------------------------------------------------------ the client */

static Emu* emu;
static char gameHash[33];
/* gameWanted: a game is running and should be loaded once signed in; loadFailed: this
 * game's achievements won't load (ra_loading is over) */
static char pendingProgress[1100]; /* a state loaded before the game was */

static bool loginRejected; /* the server said no to every credential (don't retry) */
static int loginStep;
static bool loginInteractive; /* signing in from the Settings screen: its status shows there, no popups */
static bool raOff;            /* retroachievements = 0 */
static char raUser[128], raPass[256], sysToken[256], savedUser[128], savedToken[256], lastError[256];

static uint32_t read_memory(uint32_t addr, uint8_t* buf, uint32_t n, rc_client_t* c) {
	(void) c;
	if (!emu) return 0;
	uint32_t got = 0;
	while (got < n) {
		uint32_t a = addr + got;
		int region;
		uint32_t off, end;
		if (a < 0x8000) region = 3, off = a, end = 0x8000;
		else if (a < 0x48000) region = 2, off = a - 0x8000, end = 0x48000;
		else if (a < 0x58000) region = 0xE, off = a - 0x48000, end = 0x58000;
		else break;
		size_t size;
		uint8_t* mem = emu_memory(emu, region, &size);
		uint32_t chunk = SDL_min(n - got, end - a);
		if (!mem || off >= size) {
			memset(buf + got, 0, chunk); /* no save RAM (yet): reads as 0 */
		} else {
			uint32_t avail = (uint32_t) SDL_min((size_t) chunk, size - off);
			memcpy(buf + got, mem + off, avail);
			if (avail < chunk) memset(buf + got + avail, 0, chunk - avail);
		}
		got += chunk;
	}
	return got;
}

static void log_message(const char* msg, const rc_client_t* c) {
	(void) c;
	/* (rcheevos ends some lines with its reason, which can be empty: "Login
	 * failed: " when no answer came; said so rather than left blank) */
	size_t n = strlen(msg);
	if (n >= 2 && !strcmp(msg + n - 2, ": ")) fprintf(stderr, "ra: %.*s (no reason given)\n", (int) (n - 2), msg);
	else fprintf(stderr, "ra: %s\n", msg);
}

static void save_token(const char* user, const char* token) {
	if (!token || !*token || !strcmp(token, savedToken)) return;
	char path[1200];
	snprintf(path, sizeof(path), "%s/login.cfg", cacheDir);
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if (fd < 0) return;
	FILE* f = fdopen(fd, "w");
	if (!f) {
		close(fd);
		return;
	}
	fprintf(f, "# RetroAchievements sign-in, kept so the password isn't sent every time\nuser = %s\ntoken = %s\n", user, token);
	fclose(f);
	snprintf(savedUser, sizeof(savedUser), "%s", user);
	snprintf(savedToken, sizeof(savedToken), "%s", token);
}

static void next_login(void);

static void login_done(int result, const char* error, rc_client_t* c, void* ud) {
	(void) c, (void) ud;
	if (result == RC_OK) {
		const rc_client_user_t* u = rc_client_get_user_info(client);
		loginState = LOGIN_OK;
		loginInteractive = false;
		lastError[0] = 0;
		fprintf(stderr, "ra: signed in as %s\n", u && u->display_name ? u->display_name : raUser);
		if (u) save_token(u->username ? u->username : raUser, u->token);
		memset(raPass, 0, sizeof(raPass)); /* the token does from here on */
		if (u && u->username && u->token) pending_replay(u->username, u->token); /* (the game loads after them) */
		if (gameWanted && !replayBusy) begin_load();
		return;
	}
	snprintf(lastError, sizeof(lastError), "%s", error && *error ? error : rc_error_str(result));
	fprintf(stderr, "ra: sign-in failed (%s)\n", lastError);
	if (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN || result == RC_ACCESS_DENIED) {
		loginRejected = true;
		next_login(); /* the next credential, if any */
		return;
	}
	loginRejected = false; /* offline or the server is down: try again with the next game */
	loginState = LOGIN_FAILED;
	if (gameWanted) loadFailed = true;
	if (gameWanted) note("Couldn't connect", "Playing without achievements this time");
}

/* Saved token, then ROCKNIX's token, then the password. */
static void next_login(void) {
	loginState = LOGIN_BUSY;
	while (loginStep < 3) {
		int step = loginStep++;
		if (step == 0 && savedToken[0]) {
			rc_client_begin_login_with_token(client, raUser, savedToken, login_done, NULL);
			return;
		}
		if (step == 1 && sysToken[0] && strcmp(sysToken, savedToken)) {
			rc_client_begin_login_with_token(client, raUser, sysToken, login_done, NULL);
			return;
		}
		if (step == 2 && raPass[0]) {
			rc_client_begin_login_with_password(client, raUser, raPass, login_done, NULL);
			return;
		}
	}
	loginState = LOGIN_FAILED;
	memset(raPass, 0, sizeof(raPass));
	if (loginInteractive) {
		loginInteractive = false; /* the Settings screen shows lastError */
		return;
	}
#ifdef __ANDROID__
	note("Sign-in failed", cfg->raUser[0] ? "Check ra_username and ra_password in gba-ds.cfg"
	                                     : "Sign in again in the game list's Settings");
#else
	note("Sign-in failed", cfg->raUser[0] ? "Check ra_username and ra_password in gba-ds.cfg"
	                                     : "Check the account in EmulationStation's RetroAchievements settings");
#endif
}

static void start_login(void) {
	loginStep = 0;
	loginRejected = false;
	next_login();
}

static void apply_progress(const char* statePath) {
	char path[1200];
	snprintf(path, sizeof(path), "%s.ra", statePath);
	FILE* f = fopen(path, "rb");
	uint8_t* buf = NULL;
	long n = 0;
	if (f) {
		fseek(f, 0, SEEK_END);
		n = ftell(f);
		fseek(f, 0, SEEK_SET);
		if (n > 0 && (buf = malloc((size_t) n)) && fread(buf, 1, (size_t) n, f) != (size_t) n) {
			free(buf);
			buf = NULL;
		}
		fclose(f);
	}
	/* no progress file: start the achievements over (as if just loaded) */
	int rc = rc_client_deserialize_progress_sized(client, buf, buf ? (size_t) n : 0);
	if (rc != RC_OK) fprintf(stderr, "ra: progress in %s not used (%s)\n", path, rc_error_str(rc));
	free(buf);
}

/* The banner at the top of the second screen: the game, its progress and the mode
 * (as a game starts, and again when the player switches hardcore on or off). */
static void game_banner(void) {
	fprintf(stderr, "ra: banner: %s\n", ra_hardcore() ? "hardcore" : "casual");
	const rc_client_game_t* g = rc_client_get_game_info(client);
	rc_client_user_game_summary_t sum;
	rc_client_get_user_game_summary(client, &sum);
	char desc[200];
	if (!sum.num_core_achievements) {
		snprintf(desc, sizeof(desc), tr("This game has no achievements yet"));
	} else if (sum.num_unlocked_achievements == sum.num_core_achievements) {
		snprintf(desc, sizeof(desc), tr("All %u achievements unlocked"), sum.num_core_achievements);
	} else {
		snprintf(desc, sizeof(desc), tr("%u of %u achievements unlocked"), sum.num_unlocked_achievements, sum.num_core_achievements);
	}
	popup_add(POP_GAME, ra_hardcore() ? tr("RETROACHIEVEMENTS \xC2\xB7 HARDCORE") : tr("RETROACHIEVEMENTS \xC2\xB7 CASUAL"), g ? g->title : "", desc, 0, g ? g->badge_url : NULL);
}

static void load_done(int result, const char* error, rc_client_t* c, void* ud) {
	(void) c, (void) ud;
	if (!gameWanted) return; /* ended meanwhile */
	if (result != RC_OK) {
		loadFailed = true;
		fprintf(stderr, "ra: game not loaded (%s)\n", error && *error ? error : rc_error_str(result));
		if (result == RC_NO_GAME_LOADED) {
			char desc[120];
			snprintf(desc, sizeof(desc), tr("No achievements for this ROM (MD5 %s)"), gameHash);
			note("Unknown version of this game", desc);
		} else {
			note("Couldn't load the achievements", error && *error ? error : rc_error_str(result));
		}
		return;
	}
	gameLoaded = true;
	if (pendingProgress[0]) {
		apply_progress(pendingProgress);
		pendingProgress[0] = 0;
	}
	const rc_client_game_t* g = rc_client_get_game_info(client);
	rc_client_user_game_summary_t sum;
	rc_client_get_user_game_summary(client, &sum);
	fprintf(stderr, "ra: %s (game %u): %u of %u achievements, %u of %u points\n", g ? g->title : "?", g ? g->id : 0,
	        sum.num_unlocked_achievements, sum.num_core_achievements, sum.points_unlocked, sum.points_core);
	game_banner();
	/* the badges, so popups have them right away */
	rc_client_achievement_list_t* list =
	    rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_LOCK_STATE);
	if (list) {
		for (uint32_t i = 0; i < list->num_buckets; ++i) {
			for (uint32_t j = 0; j < list->buckets[i].num_achievements; ++j) {
				const rc_client_achievement_t* a = list->buckets[i].achievements[j];
				if (a->id >= WARNING_ID) continue;
				/* the list shows each in its current state; popups need the unlocked one */
				if (!a->unlocked) fetch_badge(a->badge_locked_url, false);
				fetch_badge(a->badge_url, false);
			}
		}
		rc_client_destroy_achievement_list(list);
	}
}

static void begin_load(void) {
	if (!gameHash[0]) return;
	rc_client_begin_load_game(client, gameHash, load_done, NULL);
}

static void on_event(const rc_client_event_t* ev, rc_client_t* c) {
	(void) c;
	switch (ev->type) {
	case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
		const rc_client_achievement_t* a = ev->achievement;
		if (a->id >= WARNING_ID) {
			fprintf(stderr, "ra: server warning: %s (%s)\n", a->title, a->description ? a->description : "");
			break;
		}
		fprintf(stderr, "ra: unlocked \"%s\" (%u points)\n", a->title, a->points);
		popup_add(POP_UNLOCK, tr("ACHIEVEMENT UNLOCKED"), a->title, a->description, (int) a->points, a->badge_url);
		break;
	}
	case RC_CLIENT_EVENT_GAME_COMPLETED: {
		const rc_client_game_t* g = rc_client_get_game_info(client);
		rc_client_user_game_summary_t sum;
		rc_client_get_user_game_summary(client, &sum);
		char desc[120];
		snprintf(desc, sizeof(desc), tr("All %u achievements unlocked"), sum.num_core_achievements);
		popup_add(POP_DONE, "GAME COMPLETED", g ? g->title : "", desc, 0, g ? g->badge_url : NULL);
		break;
	}
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_SHOW:
		if (ev->achievement->id < WARNING_ID) challenge_show(ev->achievement);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_CHALLENGE_INDICATOR_HIDE:
		challenge_hide(ev->achievement->id);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_SHOW:
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_UPDATE:
		if (ev->achievement->id < WARNING_ID) progress_show(ev->achievement);
		break;
	case RC_CLIENT_EVENT_ACHIEVEMENT_PROGRESS_INDICATOR_HIDE:
		progressOn = false;
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_STARTED:
		fprintf(stderr, "ra: leaderboard attempt started: %s\n", ev->leaderboard->title);
		popup_add(POP_NOTE, tr("LEADERBOARD ATTEMPT STARTED"), ev->leaderboard->title, ev->leaderboard->description, 0, NULL);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_FAILED:
		popup_add(POP_NOTE, tr("LEADERBOARD ATTEMPT FAILED"), ev->leaderboard->title, ev->leaderboard->description, 0, NULL);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_SUBMITTED: {
		fprintf(stderr, "ra: leaderboard entry submitted: %s (%s)\n", ev->leaderboard->title, ev->leaderboard->tracker_value ? ev->leaderboard->tracker_value : "");
		char desc[120];
		snprintf(desc, sizeof(desc), tr("Score: %s"), ev->leaderboard->tracker_value ? ev->leaderboard->tracker_value : "");
		popup_add(POP_NOTE, tr("LEADERBOARD SUBMITTED"), ev->leaderboard->title, desc, 0, NULL);
		break;
	}
	case RC_CLIENT_EVENT_LEADERBOARD_SCOREBOARD: {
		const rc_client_leaderboard_scoreboard_t* sb = ev->leaderboard_scoreboard;
		const rc_client_leaderboard_t* lb = rc_client_get_leaderboard_info(client, sb->leaderboard_id);
		char desc[160];
		snprintf(desc, sizeof(desc), tr("Rank %u of %u \xC2\xB7 your score %s \xC2\xB7 best %s"), sb->new_rank, sb->num_entries,
		         sb->submitted_score, sb->best_score);
		popup_add(POP_NOTE, tr("LEADERBOARD RANK"), lb ? lb->title : "", desc, 0, NULL);
		break;
	}
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_SHOW:
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_UPDATE:
		tracker_set(ev->leaderboard_tracker->id, ev->leaderboard_tracker->display);
		break;
	case RC_CLIENT_EVENT_LEADERBOARD_TRACKER_HIDE:
		tracker_hide(ev->leaderboard_tracker->id);
		break;
	case RC_CLIENT_EVENT_RESET:
		/* (hardcore switched on with a game loaded: it starts over) */
		resetWanted = true;
		break;
	case RC_CLIENT_EVENT_SERVER_ERROR:
		fprintf(stderr, "ra: server error (%s): %s\n", ev->server_error->api ? ev->server_error->api : "?",
		        ev->server_error->error_message ? ev->server_error->error_message : "?");
		break;
	case RC_CLIENT_EVENT_DISCONNECTED:
		note("Offline", "Unlocks will be sent when the connection is back");
		break;
	case RC_CLIENT_EVENT_RECONNECTED:
		note("Back online", "Unlocks sent");
		break;
	default:
		break;
	}
}

#ifndef __ANDROID__
/* key=value from ROCKNIX's system.cfg */
static bool sys_setting(const char* key, char* out, size_t n) {
	FILE* f = fopen(cfg->raSystemCfg, "r");
	if (!f) return false;
	char line[1024];
	size_t kl = strlen(key);
	bool found = false;
	while (fgets(line, sizeof(line), f)) {
		if (strncmp(line, key, kl) || line[kl] != '=') continue;
		char* v = line + kl + 1;
		v[strcspn(v, "\r\n")] = 0;
		size_t vl = strlen(v);
		if (vl >= 2 && (v[0] == '"' || v[0] == '\'') && v[vl - 1] == v[0]) v[vl - 1] = 0, ++v;
		snprintf(out, n, "%s", v);
		found = true; /* the last one wins */
	}
	fclose(f);
	return found;
}
#endif

/* <state_dir>/ra/login.cfg: the account last signed in, and its token */
static void read_saved_login(void) {
	char path[1200], line[512];
	savedUser[0] = savedToken[0] = 0;
	snprintf(path, sizeof(path), "%s/login.cfg", cacheDir);
	FILE* f = fopen(path, "r");
	while (f && fgets(line, sizeof(line), f)) {
		line[strcspn(line, "\r\n")] = 0;
		if (!strncmp(line, "user = ", 7)) snprintf(savedUser, sizeof(savedUser), "%s", line + 7);
		if (!strncmp(line, "token = ", 8)) snprintf(savedToken, sizeof(savedToken), "%s", line + 8);
	}
	if (f) fclose(f);
}

/* the leading numbers of a version ("1.5.8-dev+x" -> "1.5.8", "0.11.0-gitabc" -> "0.11.0") */
static void numeric_version(const char* v, char* out, size_t n) {
	size_t k = 0;
	while (v && (isdigit((unsigned char) *v) || (*v == '.' && k && isdigit((unsigned char) v[1]))) && k + 1 < n) out[k++] = *v++;
	if (!k) out[k++] = '0';
	out[k] = 0;
}

static bool client_setup(void) {
	if (client) return true;
	char badges[1200];
	snprintf(badges, sizeof(badges), "%s/badges", cacheDir);
	mkdirs(badges);
	client = rc_client_create(read_memory, server_call);
	if (!client) return false;
	rc_client_enable_logging(client, getenv("GBADS_DEBUG") ? RC_CLIENT_LOG_LEVEL_VERBOSE : RC_CLIENT_LOG_LEVEL_WARN, log_message);
	rc_client_set_event_handler(client, on_event);
	rc_client_set_hardcore_enabled(client, 0); /* on from ra_start_mode, before a game starts */
	if (cfg->raHost[0]) rc_client_set_host(client, cfg->raHost);
	/* RetroAchievements' client identity (hardcore compliance, C): name and version
	 * (numbers only: "1.5.8-dev" sends 1.5.8), the system, the core and its version,
	 * then rc_client's own clause */
	char clause[64] = "", ver[32], core[32];
	rc_client_get_user_agent_clause(client, clause, sizeof(clause));
	numeric_version(GBADS_VERSION, ver, sizeof(ver));
	numeric_version(projectVersion, core, sizeof(core));
#ifdef __ANDROID__
	const char* system = "Android";
#else
	const char* system = "Linux";
#endif
	snprintf(userAgent, sizeof(userAgent), "GBADS/%s (%s) mgba/%s %s", ver, system, core, clause);
	fprintf(stderr, "ra: user agent %s\n", userAgent);
	return true;
}

void ra_init(const Config* c) {
	cfg = c;
	snprintf(cacheDir, sizeof(cacheDir), "%s/ra", cfg->stateDir);
	if (!strcmp(cfg->raMode, "0") || !strcasecmp(cfg->raMode, "off")) {
		raOff = true;
		return;
	}
	read_saved_login();
	if (cfg->raUser[0]) {
		snprintf(raUser, sizeof(raUser), "%s", cfg->raUser);
		snprintf(raPass, sizeof(raPass), "%s", cfg->raPassword);
	} else {
#ifdef __ANDROID__
		/* the account signed in in the game list's Settings (ra_sign_in) */
		snprintf(raUser, sizeof(raUser), "%s", savedUser);
#else
		char enabled[16] = "";
		sys_setting("global.retroachievements", enabled, sizeof(enabled));
		sys_setting("global.retroachievements.username", raUser, sizeof(raUser));
		sys_setting("global.retroachievements.password", raPass, sizeof(raPass));
		sys_setting("global.retroachievements.token", sysToken, sizeof(sysToken));
		char hc[16] = "";
		sys_setting("global.retroachievements.hardcore", hc, sizeof(hc));
		if (!strcmp(hc, "1") && !cfg->raHardcore) fprintf(stderr, "ra: EmulationStation's hardcore setting isn't used (hardcore is a GBA DS setting: ra_hardcore)\n");
		if (strcmp(enabled, "1") && strcmp(cfg->raMode, "1")) {
			if (raUser[0]) fprintf(stderr, "ra: off (RetroAchievements is switched off in EmulationStation)\n");
			raUser[0] = 0;
			return;
		}
#endif
	}
	if (!raUser[0]) {
		if (!strcmp(cfg->raMode, "1")) fprintf(stderr, "ra: no account (set ra_username / ra_password)\n");
		return;
	}
	if (strcasecmp(savedUser, raUser)) savedToken[0] = 0; /* another account's */
	if (!client_setup()) return;
	fprintf(stderr, "ra: signing in as %s\n", raUser);
	start_login();
}

int ra_account(char* user, size_t userSize, char* msg, size_t msgSize) {
	snprintf(user, userSize, "%s", raUser);
	snprintf(msg, msgSize, "%s", lastError);
	if (raOff) return RA_ACCOUNT_OFF;
	switch (loginState) {
	case LOGIN_BUSY: return RA_ACCOUNT_BUSY;
	case LOGIN_OK: return RA_ACCOUNT_OK;
	case LOGIN_FAILED: return RA_ACCOUNT_FAILED;
	default: return RA_ACCOUNT_NONE;
	}
}

void ra_sign_in(const char* user, const char* password) {
	if (raOff || !cfg || loginState == LOGIN_BUSY) return;
	lastError[0] = 0;
	if (!client_setup()) {
		snprintf(lastError, sizeof(lastError), tr("RetroAchievements couldn't start"));
		loginState = LOGIN_FAILED;
		return;
	}
	if (loginState == LOGIN_OK) rc_client_logout(client);
	snprintf(raUser, sizeof(raUser), "%s", user);
	snprintf(raPass, sizeof(raPass), "%s", password);
	sysToken[0] = savedToken[0] = 0;
	loginInteractive = true;
	fprintf(stderr, "ra: signing in as %s (from Settings)\n", raUser);
	loginStep = 2; /* the password */
	loginRejected = false;
	next_login();
}

void ra_sign_out(void) {
	if (!cfg || loginState == LOGIN_BUSY) return;
	if (client && loginState == LOGIN_OK) rc_client_logout(client);
	fprintf(stderr, "ra: signed out\n");
	loginState = LOGIN_OFF;
	raUser[0] = savedUser[0] = savedToken[0] = lastError[0] = 0;
	memset(raPass, 0, sizeof(raPass));
	char path[1200];
	snprintf(path, sizeof(path), "%s/login.cfg", cacheDir);
	unlink(path);
}

void ra_shutdown(void) {
	if (!client) return;
	pump();
	rc_client_destroy(client);
	client = NULL;
	if (workerUp) {
		/* let unlocks that are still on their way finish (a few seconds at most) */
		pthread_mutex_lock(&qLock);
		workerQuit = true;
		pthread_cond_broadcast(&qCond);
		struct timespec until;
		clock_gettime(CLOCK_REALTIME, &until);
		until.tv_sec += 5;
		while ((calls || !workerIdle) && pthread_cond_timedwait(&qCond, &qLock, &until) == 0) {}
		pthread_mutex_unlock(&qLock);
		pthread_detach(worker);
	}
}

/* ------------------------------------------------------------ hardcore
 *
 * Hardcore (RetroAchievements' compliance page, B): while it's on, game.c
 * refuses state loads, rewind and cheats. It applies while a game's
 * achievements are loaded or loading: an unrecognised game, a patched one
 * or one started offline has none, and plays as it always did. */
bool ra_hardcore(void) {
	return client && gameWanted && !loadFailed && rc_client_get_hardcore_enabled(client);
}

/* Before ra_game_start: the mode this game starts in. casualOnly: it starts
 * from a saved state (resuming, a patch change, --load-state), which has to
 * be casual; the player's choice stays for the next game. */
void ra_start_mode(bool hardcore, bool casualOnly) {
	if (!client) return;
	rc_client_set_hardcore_enabled(client, hardcore && !casualOnly);
	resetWanted = false; /* (nothing loaded yet: no reset to ask for) */
	if (hardcore && casualOnly) fprintf(stderr, "ra: starting in casual mode (the game starts from a saved state or an input script)\n");
}

/* The menu's Hardcore switch. Switching on with a game loaded starts it over
 * (rc_client asks for the reset: ra_reset_wanted); off carries on as it is. */
void ra_set_hardcore(bool on) {
	if (!client) return;
	fprintf(stderr, "ra: %s mode\n", on ? "hardcore" : "casual");
	rc_client_set_hardcore_enabled(client, on);
}

void ra_show_banner(void) {
	if (client && gameLoaded) game_banner();
}

bool ra_reset_wanted(void) {
	bool r = resetWanted;
	resetWanted = false;
	return r;
}

void ra_game_start(Emu* e, const char* romPath) {
	if (!client) return;
	emu = e;
	gameLoaded = false;
	loadFailed = false;
	gameWanted = true;
	pendingProgress[0] = 0;
	nPopups = 0;
	overlays_clear();
	snprintf(listStatus, sizeof(listStatus), tr("Loading the achievements..."));
	/* the RetroAchievements hash of a GBA game: MD5 of the whole ROM */
	size_t size = 0;
	uint8_t* rom = emu_rom_read_all(romPath, &size);
	gameHash[0] = 0;
	if (rom) {
		md5_state_t st;
		md5_byte_t digest[16];
		md5_init(&st);
		for (size_t off = 0; off < size; off += 1 << 20) {
			md5_append(&st, rom + off, (int) SDL_min((size_t) 1 << 20, size - off));
		}
		md5_finish(&st, digest);
		rc_format_md5(gameHash, digest);
		free(rom);
		fprintf(stderr, "ra: game hash %s\n", gameHash);
	}
	if (loginState == LOGIN_OK) {
		if (!replayBusy) begin_load();
	} else if (loginState == LOGIN_FAILED && !loginRejected) {
		start_login();
	}
}

/* The game's achievements are still being fetched and set up (the server's
 * answers are parsed on the main thread: seconds of CPU on the device, which
 * froze a game a second into play; game.c waits for it behind a screen). */
bool ra_loading(void) {
	return client && gameWanted && !gameLoaded && !loadFailed && (loginState == LOGIN_OK || loginState == LOGIN_BUSY);
}

void ra_game_patched(void) {
	if (!client) return;
	emu = NULL;
	gameLoaded = gameWanted = false;
	nPopups = 0;
	overlays_clear();
	gameHash[0] = 0;
	note("Off while a patch is on", "RetroAchievements doesn't allow patched games");
	fprintf(stderr, "ra: off, the game is patched\n");
}

void ra_list_close(void);

void ra_game_end(void) {
	if (!client) return;
	ra_list_close();
	pump();
	if (gameLoaded || rc_client_is_game_loaded(client)) rc_client_unload_game(client);
	gameWanted = gameLoaded = false;
	emu = NULL;
	for (int i = 0; i < nPopups; ++i) ui_free_image(popups[i].tex);
	nPopups = 0;
	overlays_clear();
}

void ra_frame(void) {
	if (!client) return;
	pump();
	rc_client_do_frame(client);
}

void ra_idle(void) {
	if (!client) return;
	pump();
	rc_client_idle(client);
}

void ra_reset(void) {
	if (client && gameLoaded) rc_client_reset(client);
}

void ra_state_saved(const char* statePath) {
	if (!client) return;
	if (!gameLoaded) {
		/* nothing to save yet: don't leave an older state's progress beside it */
		char path[1200];
		snprintf(path, sizeof(path), "%s.ra", statePath);
		unlink(path);
		return;
	}
	size_t n = rc_client_progress_size(client);
	uint8_t* buf = n ? malloc(n) : NULL;
	if (!buf) return;
	if (rc_client_serialize_progress_sized(client, buf, n) == RC_OK) {
		char path[1200];
		snprintf(path, sizeof(path), "%s.ra", statePath);
		FILE* f = fopen(path, "wb");
		if (f) {
			fwrite(buf, 1, n, f);
			fclose(f);
		}
	}
	free(buf);
}

void ra_state_loaded(const char* statePath) {
	if (!client) return;
	if (gameLoaded) apply_progress(statePath);
	else if (gameWanted) snprintf(pendingProgress, sizeof(pendingProgress), "%s", statePath);
}

/* ------------------------------------------------------------ achievement list */

/* The pause menu's Achievements page: rc_client's list grouped by progress
 * (recently unlocked, almost there, locked, unlocked...), each with its
 * badge, description and points. Scrolls by D-pad, L/R or dragging. */
typedef struct {
	const char* heading;                /* a bucket label, or NULL for an achievement */
	const rc_client_achievement_t* ach;
	SDL_Texture* tex[2];
	int texW, texH;
	bool texTried;
	char path[1200];
} ListRow;

static rc_client_achievement_list_t* achList;
static ListRow* rows;
static int nRows;
static int listScroll, listViewH = 1;

bool ra_enabled(void) {
	return client != NULL;
}

void ra_list_close(void) {
	for (int i = 0; i < nRows; ++i) ui_free_image(rows[i].tex);
	free(rows);
	rows = NULL;
	nRows = 0;
	if (achList) rc_client_destroy_achievement_list(achList);
	achList = NULL;
}

void ra_list_open(void) {
	ra_list_close();
	listScroll = 0;
	if (!client || !gameLoaded) return;
	achList = rc_client_create_achievement_list(client, RC_CLIENT_ACHIEVEMENT_CATEGORY_CORE, RC_CLIENT_ACHIEVEMENT_LIST_GROUPING_PROGRESS);
	if (!achList) return;
	int n = 0;
	for (uint32_t b = 0; b < achList->num_buckets; ++b) n += 1 + (int) achList->buckets[b].num_achievements;
	rows = calloc((size_t) n, sizeof(*rows));
	if (!rows) return;
	for (uint32_t b = 0; b < achList->num_buckets; ++b) {
		const rc_client_achievement_bucket_t* bk = &achList->buckets[b];
		if (!bk->num_achievements) continue;
		int head = nRows;
		rows[nRows++].heading = bk->label ? bk->label : "";
		for (uint32_t j = 0; j < bk->num_achievements; ++j) {
			if (bk->achievements[j]->id >= WARNING_ID) continue;
			ListRow* r = &rows[nRows++];
			r->ach = bk->achievements[j];
			const char* url = r->ach->unlocked ? r->ach->badge_url : r->ach->badge_locked_url;
			if (url && *url) {
				badge_path(url, r->path, sizeof(r->path));
				fetch_badge(url, false);
			}
		}
		if (nRows == head + 1) { /* only warnings: no heading either */
			nRows = head;
			memset(&rows[head], 0, sizeof(*rows));
		}
	}
}

static int row_h(float px, const ListRow* r) {
	return r->heading ? px * 13 : px * 23;
}

static int list_total(float px) {
	int t = 0;
	for (int i = 0; i < nRows; ++i) t += row_h(px, &rows[i]);
	return t;
}

void ra_list_scroll(int scr, int dy) {
	int maxS = SDL_max(0, list_total(ui_px(scr)) - listViewH);
	listScroll = SDL_clamp(listScroll + dy, 0, maxS);
}

int ra_list_step(int scr) {
	return ui_px(scr) * 23;
}

int ra_list_scroll_pos(void) {
	return listScroll;
}

void ra_list_set_scroll(int scr, int y) {
	listScroll = 0;
	ra_list_scroll(scr, y);
}

void ra_list_draw(int s, SDL_Rect a) {
	float px = ui_px(s);
	SDL_Rect sr = screens[s].rect;
	if (!client || !gameLoaded || !achList) {
		const char* msg = !client ? N_("RetroAchievements is off"): listStatus[0] ? listStatus : N_("Loading the achievements...");
		ui_font_wrap(s, TF_BODY, a.x + px * 6, a.y + a.h / 3, a.w - px * 12, px * 5, msg, P_DIM);
		return;
	}
	/* summary */
	rc_client_user_game_summary_t sum;
	rc_client_get_user_game_summary(client, &sum);
	char line[400];
	if (!sum.num_core_achievements) {
		snprintf(line, sizeof(line), tr("This game has no achievements yet"));
	} else {
		snprintf(line, sizeof(line), "%u of %u unlocked \xC2\xB7 %u of %u points \xC2\xB7 %s", sum.num_unlocked_achievements,
		         sum.num_core_achievements, sum.points_unlocked, sum.points_core, ra_hardcore() ? "hardcore" : "casual");
	}
	ui_font_text(s, TF_BODY, a.x + a.w / 2, a.y + px * 5, px * 4, line, P_GOLD, 0);
	int top = a.y + px * 12;
	/* Rich Presence: what the server shows on the player's profile, from the game's own script */
	char rp[160] = "";
	if (rc_client_has_rich_presence(client) && rc_client_get_rich_presence_message(client, rp, sizeof(rp)) > 0) {
		char fitted[200];
		int cap = px * 3 + px / 2;
		fit(fitted, sizeof(fitted), TF_BODY, cap, rp, a.w - px * 8);
		ui_font_text(s, TF_BODY, a.x + a.w / 2, a.y + px * 10, cap, fitted, P_DIM, 0);
		top = a.y + px * 14;
	}
	listViewH = a.y + a.h - top;
	ra_list_scroll(s, 0); /* keep in range */

	SDL_Rect clip = { sr.x + a.x, sr.y + top, a.w, listViewH };
	ui_clip(s, &clip);
	int y = top - listScroll;
	int rightW = px * 26; /* points / state column */
	for (int i = 0; i < nRows; ++i) {
		ListRow* r = &rows[i];
		int h = row_h(px, r);
		if (y + h <= top || y >= top + listViewH) {
			y += h;
			continue;
		}
		if (r->heading) {
			fit(line, sizeof(line), TF_BOLD, px * 4, r->heading, a.w - px * 4);
			ui_font_text(s, TF_BOLD, a.x + px * 2, y + px * 8, px * 4, line, P_GOLD, -1);
			y += h;
			continue;
		}
		const rc_client_achievement_t* ach = r->ach;
		SDL_Rect box = { a.x, y + px, a.w, h - px * 2 };
		ui_fill_round(s, box, (SDL_Color) { 34, 30, 58, 240 }, px * 3);
		/* badge */
		int b = px * 17, bx = box.x + px * 2, by = box.y + (box.h - b) / 2;
		if (ach->unlocked) ui_fill_round(s, (SDL_Rect) { bx - px / 2 - 1, by - px / 2 - 1, b + px + 2, b + px + 2 }, P_GOLD, px * 2);
		if (!r->texTried && r->path[0] && file_exists(r->path)) {
			r->texTried = true;
			ui_load_image(r->path, r->tex, &r->texW, &r->texH, true);
		}
		if (r->tex[0] || r->tex[1]) {
			SDL_Rect ba = { bx, by, b, b };
			ui_image(s, r->tex, r->texW, r->texH, "stretch", &ba);
		} else {
			ui_fill_round(s, (SDL_Rect) { bx, by, b, b }, P_BLANK, px * 2);
		}
		/* title and description */
		int tx = bx + b + px * 5, tw = box.x + box.w - rightW - tx;
		fit(line, sizeof(line), TF_BOLD, px * 5, ach->title, tw);
		ui_font_text(s, TF_BOLD, tx, box.y + px * 13 / 2, px * 5, line, ach->unlocked ? P_TEXT : P_DIM, -1);
		int cap = px * 4;
		while (cap > px * 3 && ui_font_width(TF_BODY, cap, ach->description) > tw) --cap;
		fit(line, sizeof(line), TF_BODY, cap, ach->description, tw);
		ui_font_text(s, TF_BODY, tx, box.y + px * 14, cap, line, P_DIM, -1);
		/* progress bar for measured achievements */
		if (!ach->unlocked && ach->measured_progress[0] && ach->measured_percent > 0) {
			SDL_Rect bar = { tx, box.y + px * 18, tw - px * 4, SDL_max(2, px * 3 / 4) };
			ui_fill_round(s, bar, P_BLANK, bar.h / 2);
			bar.w = (int) (bar.w * SDL_min(ach->measured_percent, 100.0f) / 100.0f);
			if (bar.w > 0) ui_fill_round(s, bar, P_EDGE, bar.h / 2);
		}
		/* points, then unlocked / progress */
		int rx = box.x + box.w - px * 5;
		snprintf(line, sizeof(line), "%u", ach->points);
		ui_font_text(s, TF_BOLD, rx, box.y + px * 13 / 2, px * 6, line, ach->unlocked ? P_GOLD : P_DIM, 1);
		const char* sub = ach->unlocked ? N_("Unlocked"): ach->measured_progress[0] ? ach->measured_progress : ach->points == 1 ? N_("point"): N_("points");
		ui_font_text(s, TF_BODY, rx, box.y + px * 14, px * 3, sub,
		             ach->unlocked ? (SDL_Color) { 120, 210, 150, 255 } : P_DIM, 1);
		y += h;
	}
	ui_clip(s, NULL);
	/* scroll bar */
	int total = list_total(px), maxS = total - listViewH;
	if (maxS > 0) {
		int bh = SDL_max(px * 6, listViewH * listViewH / total);
		int byy = top + (listViewH - bh) * listScroll / maxS;
		ui_fill_round(s, (SDL_Rect) { sr.w - px * 3, byy, px * 2, bh }, P_EDGE, px);
	}
}

#endif

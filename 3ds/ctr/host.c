/* host.h, the memory split, and the program's start: GBA DS's own main
 * (src/main.c, built as gbads_main) run with its folder on the SD card. */
#include "host.h"

#include <3ds.h>

#include <errno.h>
#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#define APP_DIR "/3ds/gba-ds"
#define ROM_DIR "/roms/gba"

/* The recompiler's helpers and the app's menus keep long paths on the stack. */
u32 __stacksize__ = 512 * 1024;

/* libctru splits the app's memory evenly between the heap and linear memory
 * (what the GPU and the DSP read), up to 32 MiB of linear. Here the heap
 * holds the game (up to 32 MiB), the recompiler's code for the game and its
 * hidden copy, and the copies themselves; the textures and the sound need
 * linear memory: it gets 12 MiB, and the heap the rest, as far as the
 * heap's address range goes. */
#define LINEAR_WANTED (12u << 20) /* (6.5 MiB of it in use in a game, 2026-11-12) */
extern char* fake_heap_start;
extern char* fake_heap_end;
u32 __ctru_heap;
u32 __ctru_heap_size;
u32 __ctru_linear_heap;
u32 __ctru_linear_heap_size;
void mappableInit(u32 addrMin, u32 addrMax);

void __system_allocateHeaps(void) {
	Handle limit = 0;
	s64 most = 0, now = 0;
	ResourceLimitType commit = RESLIMIT_COMMIT;
	if (R_FAILED(svcGetResourceLimit(&limit, CUR_PROCESS_HANDLE))) svcBreak(USERBREAK_PANIC);
	svcGetResourceLimitLimitValues(&most, limit, &commit, 1);
	svcGetResourceLimitCurrentValues(&now, limit, &commit, 1);
	svcCloseHandle(limit);
	u32 remaining = (u32) (most - now) & ~0xFFFu;
	u32 area = OS_HEAP_AREA_END - OS_HEAP_AREA_BEGIN;
	u32 heap = remaining > 3 * LINEAR_WANTED ? remaining - LINEAR_WANTED : remaining / 2;
	if (heap > area) heap = area;
	u32 linear = remaining - heap;
	/* An emulator gives the 124 MiB a New 3DS's own software gets; the console
	 * gives homebrew 64 MiB (heap 41568 KiB, 2026-11-12). The same here, so
	 * that what is tested is what the console has. */
	if (isEmulator() && heap > (41568u << 10)) heap = (41568u << 10) + (16u << 20) - LINEAR_WANTED, linear = LINEAR_WANTED;
	__ctru_heap_size = heap;
	__ctru_linear_heap_size = linear;
	if (R_FAILED(svcControlMemory(&__ctru_heap, OS_HEAP_AREA_BEGIN, 0, __ctru_heap_size, MEMOP_ALLOC, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	if (R_FAILED(svcControlMemory(&__ctru_linear_heap, 0, 0, __ctru_linear_heap_size, MEMOP_ALLOC_LINEAR, MEMPERM_READ | MEMPERM_WRITE)))
		svcBreak(USERBREAK_PANIC);
	mappableInit(OS_MAP_AREA_BEGIN, OS_MAP_AREA_END);
	fake_heap_start = (char*) __ctru_heap;
	fake_heap_end = fake_heap_start + __ctru_heap_size;
}

void logLine(const char* format, ...) {
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

bool isEmulator(void) {
	static int known = -1;
	if (known < 0) {
		s64 value = 0;
		known = R_SUCCEEDED(svcGetSystemInfo(&value, 0x20000, 0)) && value != 0;
	}
	return known;
}

bool isNew3ds(void) {
	static int known = -1;
	if (known < 0) {
		bool isNew = false;
		APT_CheckNew3DS(&isNew);
		known = isNew;
	}
	return known;
}

size_t host_heap_free(void) {
	struct mallinfo m = mallinfo();
	return __ctru_heap_size > (u32) m.uordblks ? __ctru_heap_size - (u32) m.uordblks : 0;
}

/* Between a background thread's reads of a large file: a moment for the
 * screen's own thread to get its reads in, longer while the buttons are in use. */
static volatile u64 uiBusyUntil;
void host_ui_busy(void) {
	uiBusyUntil = osGetTime() + 1500;
}
void host_fs_yield(void) {
	/* (held off altogether while the buttons are in use: up to 1.5 s after the last press) */
	do svcSleepThread(osGetTime() < uiBusyUntil ? 20000000LL : 500000LL);
	while (osGetTime() < uiBusyUntil);
}

void host_mem_note(const char* what) {
	logLine("memory: %u KiB of the heap free, %u KiB of linear (%s)", (unsigned) (host_heap_free() >> 10), (unsigned) (linearSpaceFree() >> 10), what);
}

int host_side_core(void) {
	return isNew3ds() ? 3 : -2;
}

/* pthread_create for the app's threads (the build names it in place of the
 * C library's): on the side core, a step below the game, with room on the
 * stack for an emulator. libctru's pthread_t is its Thread, so pthread_join
 * works on these. */
struct start {
	void* (*fn)(void*);
	void* arg;
};

static void start_main(void* p) {
	struct start s = *(struct start*) p;
	free(p);
	s.fn(s.arg);
}

int host_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*fn)(void*), void* arg) {
	(void) attr;
	struct start* s = malloc(sizeof(*s));
	if (!s) return ENOMEM;
	s->fn = fn;
	s->arg = arg;
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	Thread t = threadCreate(start_main, s, 256 * 1024, priority + 1, host_side_core(), false);
	if (!t && host_side_core() != -2) t = threadCreate(start_main, s, 256 * 1024, priority + 1, -2, false);
	logLine("thread %p: %s", (void*) fn, t ? "made" : "not made");
	host_mem_note("a thread");
	if (!t) {
		free(s);
		return EAGAIN;
	}
	*thread = (pthread_t) t;
	return 0;
}

/* The SD card, opened once for the calls made on it here. */
static bool sd_archive(FS_Archive* archive) {
	static FS_Archive sd;
	static bool open;
	if (!open) open = R_SUCCEEDED(FSUSER_OpenArchive(&sd, ARCHIVE_SDMC, fsMakePath(PATH_EMPTY, "")));
	*archive = sd;
	return open;
}

bool host_read_head(const char* path, void* out, size_t n) {
	if (!strncmp(path, "sdmc:", 5)) path += 5;
	FS_Archive archive;
	if (!sd_archive(&archive)) return false;
	Handle file;
	if (R_FAILED(FSUSER_OpenFile(&file, archive, fsMakePath(PATH_ASCII, path), FS_OPEN_READ, 0))) return false;
	u32 got = 0;
	Result rc = FSFILE_Read(file, &got, 0, out, (u32) n);
	FSFILE_Close(file);
	return R_SUCCEEDED(rc) && got == n;
}

int host_read_dir(const char* path, HostEntry** out) {
	*out = NULL;
	if (!strncmp(path, "sdmc:", 5)) path += 5;
	FS_Archive archive;
	if (!sd_archive(&archive)) return -1;
	Handle dir;
	if (R_FAILED(FSUSER_OpenDirectory(&dir, archive, fsMakePath(PATH_ASCII, path)))) return -1;
	enum { BATCH = 64 };
	FS_DirectoryEntry* batch = malloc(BATCH * sizeof(*batch));
	HostEntry* list = NULL;
	int count = 0;
	u32 got = 0;
	while (batch && R_SUCCEEDED(FSDIR_Read(dir, &got, BATCH, batch)) && got) {
		HostEntry* grown = realloc(list, (size_t) (count + (int) got) * sizeof(*list));
		if (!grown) break;
		list = grown;
		for (u32 i = 0; i < got; ++i) {
			HostEntry* e = &list[count++];
			ssize_t n = utf16_to_utf8((uint8_t*) e->name, batch[i].name, sizeof(e->name) - 1);
			e->name[n < 0 ? 0 : n >= (ssize_t) sizeof(e->name) ? (ssize_t) sizeof(e->name) - 1 : n] = 0;
			e->dir = (batch[i].attributes & FS_ATTRIBUTE_DIRECTORY) != 0;
			e->size = (long long) batch[i].fileSize;
		}
	}
	free(batch);
	FSDIR_Close(dir);
	*out = list;
	return count;
}

/* stat as the C library has it leaves a file's time at 0: the SD card's own
 * record of it (the build links stat through here: -Wl,--wrap=stat). */
int __real_stat(const char* path, struct stat* st);
int __wrap_stat(const char* path, struct stat* st) {
	int rc = __real_stat(path, st);
	/* (not a game's: the game list reads a whole folder of them, and each
	 * question is another trip to the SD card) */
	const char* dot = strrchr(path, '.');
	bool game = dot && (!strcasecmp(dot, ".gba") || !strcasecmp(dot, ".zip"));
	if (rc == 0 && !game && !st->st_mtime && !S_ISDIR(st->st_mode)) {
		u64 mtime = 0;
		if (R_SUCCEEDED(archive_getmtime(path, &mtime))) st->st_mtime = (time_t) mtime;
	}
	return rc;
}

#ifdef GBADS_LOG_URL
/* A diagnosis build only (make LOG_URL=http://<host>:<port>/log, never a
 * release), as armGBA-3DS's and PokeDaisy's: a log POSTed to a machine on
 * the local network (3ds/tools/logserver.py), for finding out what happened
 * on a console without taking its SD card out. */
static void log_upload(const char* path, const char* name) {
	FILE* f = fopen(path, "rb");
	if (!f) return;
	fseek(f, 0, SEEK_END);
	long length = ftell(f);
	/* the end, if it's long: what happened last is what's wanted */
	long most = 768 * 1024;
	long from = length > most ? length - most : 0;
	size_t size = (size_t) (length - from);
	char* data = size ? malloc(size) : NULL;
	fseek(f, from, SEEK_SET);
	bool ok = data && fread(data, 1, size, f) == size;
	fclose(f);
	if (!ok) logLine("log upload: %s: nothing read of %s (%ld bytes)", name, path, length);
	/* (just started, the console may not be on the network yet: up to 5 s) */
	if (ok && R_SUCCEEDED(acInit())) {
		u32 wifi = 0;
		for (int i = 0; i < 50 && (R_FAILED(ACU_GetWifiStatus(&wifi)) || !wifi); ++i) svcSleepThread(100000000LL);
		if (!wifi) logLine("log upload: not on the network");
		acExit();
	}
	/* the http service holds a POST's body in memory shared with it */
	Result init = ok ? httpcInit(1 << 20) : 0;
	if (ok && R_FAILED(init)) logLine("log upload: %s: no http service (%08lX)", name, (unsigned long) init);
	if (ok && R_SUCCEEDED(init)) {
		char url[256];
		snprintf(url, sizeof(url), "%s?name=%s", GBADS_LOG_URL, name);
		httpcContext context;
		u32 status = 0;
		Result rc = httpcOpenContext(&context, HTTPC_METHOD_POST, url, 0);
		if (R_SUCCEEDED(rc)) {
			httpcAddRequestHeaderField(&context, "Content-Type", "text/plain");
			httpcAddPostDataRaw(&context, (const u32*) data, size);
			rc = httpcBeginRequest(&context);
			if (R_SUCCEEDED(rc)) rc = httpcGetResponseStatusCodeTimeout(&context, &status, 3000000000ull);
			httpcCloseContext(&context);
		}
		logLine("log upload: %s: %08lX, HTTP %lu", name, (unsigned long) rc, (unsigned long) status);
		httpcExit();
	}
	free(data);
}
#endif

/* The log so far, sent now: the game list calls this each time it comes up
 * (at start, and back from a game that ran or didn't), so what happened is
 * here without the app having to end well. */
void host_log_checkpoint(void) {
#ifdef GBADS_LOG_URL
	fflush(stderr);
	log_upload(APP_DIR "/log.txt", "gba-ds-now");
#endif
}

int gbads_main(int argc, char** argv);

int main(void) {
	osSetSpeedupEnable(true);
	mkdir("/3ds", 0777);
	mkdir(APP_DIR, 0777);
	mkdir("/roms", 0777);
	mkdir(ROM_DIR, 0777);
	/* the last run's log kept: what to attach to a bug report */
	remove(APP_DIR "/log-prev.txt");
	rename(APP_DIR "/log.txt", APP_DIR "/log-prev.txt");
	if (freopen(APP_DIR "/log.txt", "w", stderr)) setvbuf(stderr, NULL, _IOLBF, 1024);
	/* the 3DS's own defaults, in a file to change them in: where the games
	 * are; each picture as large as its screen takes; no rewind (the memory
	 * is the game's and the recompiler's) */
	FILE* f = fopen(APP_DIR "/gba-ds.cfg", "r");
	if (f) {
		fclose(f);
	} else if ((f = fopen(APP_DIR "/gba-ds.cfg", "w"))) {
		fputs("# GBA DS on the 3DS\nrom_dir = " ROM_DIR "\nscale = fit\nmap_scale = fit\nrewind = 0\n", f);
		fclose(f);
	}
	setenv("GBADS_HOME", APP_DIR, 1);
	logLine("GBA DS for the 3DS %s (GBA DS %s)%s", GBADS_VERSION, GBADS_UPSTREAM,
#ifdef GBADS_LOG_URL
	        ", diagnosis build: logs to " GBADS_LOG_URL
#else
	        ""
#endif
	);
	logLine("%s 3DS%s, heap %lu KiB, linear %lu KiB", isNew3ds() ? "New" : "Old", isEmulator() ? " (emulator)" : "",
	        (unsigned long) (__ctru_heap_size >> 10), (unsigned long) (__ctru_linear_heap_size >> 10));
	char name[] = "gba-ds";
	char* args[] = { name, NULL };
#ifdef GBADS_LOG_URL
	log_upload(APP_DIR "/log-prev.txt", "gba-ds-prev"); /* the last run's: all of it if that one crashed */
#endif
	int rc = gbads_main(1, args);
	jitHostRestoreMemory();
	fflush(stderr);
#ifdef GBADS_LOG_URL
	log_upload(APP_DIR "/log.txt", "gba-ds");
	fflush(stderr);
#endif
	return rc;
}

/* (a diagnosis, linked in by the test build's --wrap: who takes the heap) */
void* __real_malloc(size_t n);
void* __real_calloc(size_t a, size_t b);
void* __wrap_malloc(size_t n) {
	if (n >= 48 * 1024) logLine("malloc %u KiB from %p", (unsigned) (n >> 10), __builtin_return_address(0));
	return __real_malloc(n);
}
void* __wrap_calloc(size_t a, size_t b) {
	if (a * b >= 48 * 1024) logLine("calloc %u KiB from %p", (unsigned) ((a * b) >> 10), __builtin_return_address(0));
	return __real_calloc(a, b);
}

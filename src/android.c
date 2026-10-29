/* Android glue: see android.h. The Java side is in android/app/src/main/java
 * (GbaDsActivity, SecondScreen). */
#ifdef __ANDROID__
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* sched_setaffinity, CPU_SET */
#endif

#include "android.h"
#include "emu.h"
#include "paths.h"
#include "profiles.h"

#include <EGL/egl.h>
#include <android/log.h>
#include <dlfcn.h>
#include <errno.h>
#include <sys/resource.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>
#include <jni.h>
#include <pthread.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define TAG "GBADS"

/* ------------------------------------------------------------- log */

static FILE* logFile;

static void* log_thread(void* arg) {
	FILE* in = fdopen((int) (intptr_t) arg, "r");
	char line[1024];
	while (in && fgets(line, sizeof(line), in)) {
		size_t n = strlen(line);
		if (logFile) {
			fputs(line, logFile);
			fflush(logFile);
		}
		if (n && line[n - 1] == '\n') line[n - 1] = 0;
		__android_log_write(ANDROID_LOG_INFO, TAG, line);
	}
	return NULL;
}

void android_init_log(void) {
	static bool done;
	if (done) return;
	done = true;
	const char* home = android_home();
	if (home) {
		char path[1100];
		snprintf(path, sizeof(path), "%s/gba-ds.log", home);
		logFile = fopen(path, "w");
	}
	int fds[2];
	if (pipe(fds) != 0) return;
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	close(fds[1]);
	pthread_t t;
	if (pthread_create(&t, NULL, log_thread, (void*) (intptr_t) fds[0]) == 0) pthread_detach(t);
}

const char* android_home(void) {
	const char* h = getenv("GBADS_HOME");
	return h && *h ? h : NULL;
}

/* ------------------------------------------------------------- defaults */

void android_config_defaults(Config* c) {
	/* shared storage ignores case: this is also a folder called GBA */
	snprintf(c->romDir, sizeof(c->romDir), "/storage/emulated/0/ROMs/gba");
	/* GBADS_HOME is "GBA DS" on the internal storage (or, without All files
	 * access, the app's folder), laid out like the ROCKNIX install folder:
	 * states/ (the default state_dir), logos/ and overlays/ (asset_dir),
	 * cheats/, screenshots/, gba-ds.log */
	const char* home = android_home();
	if (home) {
		snprintf(c->screenshotDir, sizeof(c->screenshotDir), "%s/screenshots", home);
		snprintf(c->cheatDir, sizeof(c->cheatDir), "%s/cheats", home);
		static const struct {
			const char* dir;
			const char* text;
		} folders[] = {
			{ "cheats", "Put RetroArch cheat files (.cht) here, subfolders are fine.\n"
			            "A file goes with the ROM of the same name: Metroid Fusion (World) (Code Breaker).cht\n"
			            "with Metroid Fusion (USA).gba. Switch cheats on in the pause menu: Game > Cheats.\n" },
			{ "overlays", "Overlay PNGs (1024x768, transparent where the picture shows): primary.png (game screen) /\n"
			              "secondary.png (map screen) for every game, <CODE>-primary.png / <CODE>-secondary.png for\n"
			              "one game, optionally with -integer / -fit / -stretch. See the README.\n" },
			{ "logos", "Optional: your own logo images (<CODE>.png or .bmp) override the ones extracted from the games.\n" },
			{ "screenshots", NULL },
		};
		for (size_t i = 0; i < sizeof(folders) / sizeof(folders[0]); ++i) {
			char path[1100];
			snprintf(path, sizeof(path), "%s/%s", home, folders[i].dir);
			mkdirs(path);
			if (!folders[i].text) continue;
			snprintf(path, sizeof(path), "%s/%s/README.txt", home, folders[i].dir);
			FILE* f = fopen(path, "wx"); /* only if it isn't there yet */
			if (f) {
				fputs(folders[i].text, f);
				fclose(f);
			}
		}
		/* the extracted logos and map borders in states/ aren't photos */
		char nomedia[1100];
		snprintf(nomedia, sizeof(nomedia), "%s/states", home);
		mkdirs(nomedia);
		snprintf(nomedia, sizeof(nomedia), "%s/states/.nomedia", home);
		FILE* f = fopen(nomedia, "a");
		if (f) fclose(f);
	}
	c->esConfigDir[0] = 0;
	c->sway = false;
	/* Android keeps HOME for itself */
	snprintf(c->menuCombo, sizeof(c->menuCombo), "select+start");
}

/* ------------------------------------------------------------- performance hint
 * Android lowers the CPU clock when the load looks moderate; at ~9 ms of
 * work per 16.7 ms frame that made busy scenes miss frames. A performance
 * hint session (Android 13+, looked up at run time) tells the scheduler
 * the frame deadline and each frame's actual work, so it raises the clock
 * before frames run late. */
typedef struct APerformanceHintManager APerformanceHintManager;
typedef struct APerformanceHintSession APerformanceHintSession;
static APerformanceHintSession* hintSession;
static int (*hintReport)(APerformanceHintSession*, int64_t);

/* as high as the system lets an app go, short of audio's: other threads of
 * ours and ordinary system work wait for the game */
int android_raise_thread(void) {
	static const int nices[] = { -10, -8, -4 };
	for (int i = 0; i < 3; ++i) {
		if (setpriority(PRIO_PROCESS, (id_t) gettid(), nices[i]) == 0) return nices[i];
	}
	return 0;
}

void android_perf_init(int64_t targetNs) {
	static bool tried;
	if (tried) return;
	tried = true;
	void* lib = dlopen("libandroid.so", RTLD_NOW);
	APerformanceHintManager* (*getManager)(void) = lib ? dlsym(lib, "APerformanceHint_getManager") : NULL;
	APerformanceHintSession* (*create)(APerformanceHintManager*, const int32_t*, size_t, int64_t) =
		lib ? dlsym(lib, "APerformanceHint_createSession") : NULL;
	hintReport = lib ? dlsym(lib, "APerformanceHint_reportActualWorkDuration") : NULL;
	if (!getManager || !create || !hintReport) {
		fprintf(stderr, "performance hint: not available on this Android version\n");
		return;
	}
	/* the game thread ahead of the app's other threads (as for display
	 * threads); refused on some systems, which is fine */
	int32_t tid = gettid();
	int got = android_raise_thread();
	if (got) fprintf(stderr, "game thread priority raised (nice %d)\n", got);
	else fprintf(stderr, "game thread priority: %s\n", strerror(errno));
	APerformanceHintManager* m = getManager();
	hintSession = m ? create(m, &tid, 1, targetNs) : NULL;
	fprintf(stderr, "performance hint: %s\n", hintSession ? "on (game thread)" : "refused by the system");
}

/* ------------------------------------------------------------- cores */

static bool pinOn; /* pin_cores (off by default); set before the game thread pins */
static int gameCore = -1;
static cpu_set_t workerCores;
static int nWorkerCores;
static pthread_once_t coresOnce = PTHREAD_ONCE_INIT;

static long core_max_khz(int cpu) {
	char path[96];
	snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/cpufreq/cpuinfo_max_freq", cpu);
	FILE* f = fopen(path, "r");
	long k = 0;
	if (f) {
		if (fscanf(f, "%ld", &k) != 1) k = 0;
		fclose(f);
	}
	return k;
}

static void pick_cores(void) {
	cpu_set_t allowed;
	CPU_ZERO(&allowed);
	if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0) {
		fprintf(stderr, "cores: can't read which ones the app may use (%s)\n", strerror(errno));
		return;
	}
	char list[256] = "";
	size_t len = 0;
	long best = -1;
	int n = 0;
	for (int c = 0; c < CPU_SETSIZE && c < 64; ++c) {
		if (!CPU_ISSET(c, &allowed)) continue;
		++n;
		long k = core_max_khz(c);
		len += (size_t) snprintf(list + len, len < sizeof(list) ? sizeof(list) - len : 0, "%s%d (%ld MHz)", len ? ", " : "", c, k / 1000);
		if (len >= sizeof(list)) len = sizeof(list) - 1;
		if (k >= best) best = k, gameCore = c; /* ties: the higher number */
	}
	CPU_ZERO(&workerCores);
	nWorkerCores = 0;
	for (int c = 0; c < CPU_SETSIZE && c < 64; ++c) {
		if (CPU_ISSET(c, &allowed) && c != gameCore) {
			CPU_SET(c, &workerCores);
			++nWorkerCores;
		}
	}
	if (n < 2) gameCore = -1; /* nothing to separate */
	fprintf(stderr, "cores the app may use: %s\n", list);
}

void android_set_pinning(bool on) {
	pinOn = on;
	static int logged = -1;
	if (on != logged) fprintf(stderr, "cores: %s\n", on ? "pinning threads (pin_cores = 1)" : "not pinning threads");
	logged = on;
}

void android_pin_game_thread(void) {
	if (!pinOn) return;
	pthread_once(&coresOnce, pick_cores);
	if (gameCore < 0) return;
	cpu_set_t one;
	CPU_ZERO(&one);
	CPU_SET(gameCore, &one);
	if (sched_setaffinity(0, sizeof(one), &one) == 0) fprintf(stderr, "cores: game thread on cpu %d\n", gameCore);
	else fprintf(stderr, "cores: couldn't pin the game thread (%s)\n", strerror(errno));
}

void android_pin_worker_thread(const char* what) {
	if (!pinOn) return;
	pthread_once(&coresOnce, pick_cores);
	if (gameCore < 0 || !nWorkerCores) return;
	if (sched_setaffinity(0, sizeof(workerCores), &workerCores) == 0) fprintf(stderr, "cores: %s kept off cpu %d\n", what, gameCore);
	else fprintf(stderr, "cores: couldn't move the %s thread (%s)\n", what, strerror(errno));
}

void android_perf_report(int64_t workNs) {
	if (hintSession && workNs > 0) hintReport(hintSession, workNs);
}

/* ------------------------------------------------------------- HTTP (RetroAchievements) */

static jclass httpClass;
static jmethodID httpRequest;

/* The app's classes are only visible to FindClass from a thread Java
 * started, so Http is looked up here, while Java loads this library. */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
	JNIEnv* env;
	if ((*vm)->GetEnv(vm, (void**) &env, JNI_VERSION_1_6) != JNI_OK) return JNI_VERSION_1_6;
	jclass c = (*env)->FindClass(env, "io/github/kyannon/gbads/Http");
	if (c) {
		httpClass = (*env)->NewGlobalRef(env, c);
		httpRequest = (*env)->GetStaticMethodID(env, c, "request",
		                                        "(Ljava/lang/String;Ljava/lang/String;[BLjava/lang/String;Ljava/lang/String;[I)[B");
		(*env)->DeleteLocalRef(env, c);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		httpRequest = NULL;
	}
	if (!httpRequest) __android_log_write(ANDROID_LOG_ERROR, TAG, "Http.request not found: no RetroAchievements");
	return JNI_VERSION_1_6;
}

int android_http(const char* url, const char* contentType, const char* post, const char* userAgent, const char* outPath,
                 char** body, size_t* len) {
	if (body) *body = NULL;
	if (len) *len = 0;
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv(); /* attaches this thread if needed */
	if (!env || !httpClass || !httpRequest) return -1;
	if ((*env)->PushLocalFrame(env, 16) < 0) {
		(*env)->ExceptionClear(env);
		return -1;
	}
	jstring jurl = (*env)->NewStringUTF(env, url);
	jstring jtype = contentType ? (*env)->NewStringUTF(env, contentType) : NULL;
	jstring jua = userAgent ? (*env)->NewStringUTF(env, userAgent) : NULL;
	jstring jout = outPath ? (*env)->NewStringUTF(env, outPath) : NULL;
	jbyteArray jpost = NULL;
	if (post) {
		jsize n = (jsize) strlen(post);
		jpost = (*env)->NewByteArray(env, n);
		if (jpost) (*env)->SetByteArrayRegion(env, jpost, 0, n, (const jbyte*) post);
	}
	jintArray jstatus = (*env)->NewIntArray(env, 1);
	int code = -1;
	if (jurl && jstatus) {
		jbyteArray res = (jbyteArray) (*env)->CallStaticObjectMethod(env, httpClass, httpRequest, jurl, jtype, jpost, jua, jout, jstatus);
		if ((*env)->ExceptionCheck(env)) {
			(*env)->ExceptionClear(env);
		} else {
			jint st = -1;
			(*env)->GetIntArrayRegion(env, jstatus, 0, 1, &st);
			code = st;
			if (res && body) {
				jsize n = (*env)->GetArrayLength(env, res);
				*body = malloc((size_t) n + 1);
				if (*body) {
					(*env)->GetByteArrayRegion(env, res, 0, n, (jbyte*) *body);
					(*body)[n] = 0;
					if (len) *len = (size_t) n;
				}
			}
		}
	}
	(*env)->PopLocalFrame(env, NULL);
	return code;
}

/* ------------------------------------------------------------- RetroAchievements sign-in dialog */

static pthread_mutex_t raLock = PTHREAD_MUTEX_INITIALIZER;
static char raUser[128], raPass[256];
static int raState; /* 0 nothing, 1 entered, -1 cancelled */

/* Calls a method of the activity, (String) -> void, from the game thread. */
static bool call_activity(const char* name, const char* arg) {
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jobject activity = (jobject) SDL_AndroidGetActivity();
	if (!env || !activity) return false;
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID m = (*env)->GetMethodID(env, cls, name, "(Ljava/lang/String;)V");
	bool ok = m != NULL;
	if (ok) {
		jstring s = (*env)->NewStringUTF(env, arg ? arg : "");
		(*env)->CallVoidMethod(env, activity, m, s);
		(*env)->DeleteLocalRef(env, s);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		ok = false;
	}
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
	return ok;
}

bool android_ra_dialog(const char* user) {
	pthread_mutex_lock(&raLock);
	raState = 0;
	pthread_mutex_unlock(&raLock);
	return call_activity("raSignInDialog", user);
}

int android_take_ra_credentials(char* user, size_t userSize, char* pass, size_t passSize) {
	pthread_mutex_lock(&raLock);
	int st = raState;
	if (st == 1) {
		snprintf(user, userSize, "%s", raUser);
		snprintf(pass, passSize, "%s", raPass);
	}
	memset(raPass, 0, sizeof(raPass));
	raState = 0;
	pthread_mutex_unlock(&raLock);
	return st;
}

JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_GbaDsActivity_nativeRaCredentials(JNIEnv* env, jclass cls, jstring user,
                                                                                     jstring pass) {
	const char* u = user ? (*env)->GetStringUTFChars(env, user, NULL) : NULL;
	const char* p = pass ? (*env)->GetStringUTFChars(env, pass, NULL) : NULL;
	pthread_mutex_lock(&raLock);
	if (u && p) {
		snprintf(raUser, sizeof(raUser), "%s", u);
		snprintf(raPass, sizeof(raPass), "%s", p);
		raState = 1;
	} else {
		raState = -1;
	}
	pthread_mutex_unlock(&raLock);
	if (u) (*env)->ReleaseStringUTFChars(env, user, u);
	if (p) (*env)->ReleaseStringUTFChars(env, pass, p);
}

/* ------------------------------------------------------------- ROM folder picker */

static pthread_mutex_t pickLock = PTHREAD_MUTEX_INITIALIZER;
static char picked[1024];
static int pickState; /* 0 nothing, 1 a folder picked, -1 cancelled */

bool android_pick_folder(void) {
	JNIEnv* env = (JNIEnv*) SDL_AndroidGetJNIEnv();
	jobject activity = (jobject) SDL_AndroidGetActivity();
	if (!env || !activity) return false;
	/* by the activity's class: FindClass on this thread wouldn't see the app's classes */
	jclass cls = (*env)->GetObjectClass(env, activity);
	jmethodID m = (*env)->GetMethodID(env, cls, "pickRomFolder", "()V");
	bool ok = m != NULL;
	if (ok) {
		pthread_mutex_lock(&pickLock);
		pickState = 0;
		pthread_mutex_unlock(&pickLock);
		(*env)->CallVoidMethod(env, activity, m);
	}
	if ((*env)->ExceptionCheck(env)) {
		(*env)->ExceptionClear(env);
		ok = false;
	}
	(*env)->DeleteLocalRef(env, cls);
	(*env)->DeleteLocalRef(env, activity);
	return ok;
}

int android_take_picked_folder(char* out, size_t size) {
	pthread_mutex_lock(&pickLock);
	int st = pickState;
	if (st == 1) snprintf(out, size, "%s", picked);
	pickState = 0;
	pthread_mutex_unlock(&pickLock);
	return st;
}

JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_GbaDsActivity_nativeFolderPicked(JNIEnv* env, jclass cls, jstring path) {
	const char* p = path ? (*env)->GetStringUTFChars(env, path, NULL) : NULL;
	pthread_mutex_lock(&pickLock);
	if (p && *p) {
		snprintf(picked, sizeof(picked), "%s", p);
		pickState = 1;
	} else {
		pickState = -1;
	}
	pthread_mutex_unlock(&pickLock);
	if (p) (*env)->ReleaseStringUTFChars(env, path, p);
}

/* ------------------------------------------------------------- second screen */

/* Frames go to the second display from a thread of their own: handing a
 * buffer to a window (ANativeWindow_lock) waits for that display's refresh,
 * and doing it on the game loop on top of the game screen's own wait capped
 * the game at 30 fps. The loop copies the finished frame into "back" and
 * moves on; the thread takes the newest frame and posts it. */
static pthread_mutex_t winLock = PTHREAD_MUTEX_INITIALIZER;
static ANativeWindow* secondWin;
static int dispW, dispH;
static bool geometrySet;

static pthread_mutex_t bufLock = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t bufCond = PTHREAD_COND_INITIALIZER;
static uint8_t *back, *front;
static int bufW, bufH;
static bool fresh;

bool android_second_size(int* w, int* h) {
	pthread_mutex_lock(&winLock);
	*w = dispW;
	*h = dispH;
	pthread_mutex_unlock(&winLock);
	return *w > 0 && *h > 0;
}

static void post_frame(const uint8_t* px, int fw, int fh) {
	pthread_mutex_lock(&winLock);
	if (secondWin) {
		if (!geometrySet) {
			/* the compositor scales our buffer to the surface if they differ */
			ANativeWindow_setBuffersGeometry(secondWin, fw, fh, WINDOW_FORMAT_RGBX_8888);
			geometrySet = true;
		}
		ANativeWindow_Buffer b;
		if (ANativeWindow_lock(secondWin, &b, NULL) == 0) {
			int w = SDL_min(b.width, fw), h = SDL_min(b.height, fh);
			for (int y = 0; y < h; ++y) {
				memcpy((uint8_t*) b.bits + (size_t) y * b.stride * 4, px + (size_t) y * fw * 4, (size_t) w * 4);
			}
			ANativeWindow_unlockAndPost(secondWin);
		}
	}
	pthread_mutex_unlock(&winLock);
}

static void* present_thread(void* arg) {
	android_pin_worker_thread("software second screen");
	for (;;) {
		pthread_mutex_lock(&bufLock);
		while (!fresh) pthread_cond_wait(&bufCond, &bufLock);
		uint8_t* t = front;
		front = back;
		back = t;
		fresh = false;
		int w = bufW, h = bufH;
		pthread_mutex_unlock(&bufLock);
		post_frame(front, w, h);
	}
	return NULL;
}

void android_second_present(SDL_Surface* s) {
	static bool started;
	pthread_mutex_lock(&bufLock);
	if (!back || bufW != s->w || bufH != s->h) {
		free(back);
		free(front);
		back = malloc((size_t) s->w * s->h * 4);
		front = malloc((size_t) s->w * s->h * 4);
		bufW = s->w;
		bufH = s->h;
	}
	if (back && front) {
		for (int y = 0; y < s->h; ++y) {
			memcpy(back + (size_t) y * s->w * 4, (const uint8_t*) s->pixels + (size_t) y * s->pitch, (size_t) s->w * 4);
		}
		fresh = true;
		pthread_cond_signal(&bufCond);
	}
	pthread_mutex_unlock(&bufLock);
	if (!started) {
		started = true;
		pthread_t t;
		if (pthread_create(&t, NULL, present_thread, NULL) == 0) pthread_detach(t);
	}
}

/* --- GPU path: SDL's OpenGL ES context draws into an EGL window surface on
 * the second display's window, between android_second_begin / _end on the
 * game thread. The software path above is the fallback. */
static EGLConfig gpuConfig;
static bool gpuOn;
static int gpuW, gpuH;
static EGLDisplay surf2Dpy = EGL_NO_DISPLAY;
static EGLSurface surf2 = EGL_NO_SURFACE;
static bool surf2Interval;
static int surf2Want; /* swap interval wanted (android_second_set_interval) */

bool android_second_gpu_init(int w, int h) {
	EGLDisplay d = eglGetCurrentDisplay();
	EGLContext c = eglGetCurrentContext();
	if (d == EGL_NO_DISPLAY || c == EGL_NO_CONTEXT) return false;
	EGLint id = 0, n = 0;
	if (!eglQueryContext(d, c, EGL_CONFIG_ID, &id)) return false;
	EGLint attrs[] = { EGL_CONFIG_ID, id, EGL_NONE };
	if (!eglChooseConfig(d, attrs, &gpuConfig, 1, &n) || n != 1) return false;
	gpuW = w;
	gpuH = h;
	gpuOn = true;
	return true;
}

/* The second display's surface stays current after a redraw (onSurf2)
 * until the game activity's screen draws again: while only one screen
 * changes, frames don't pay for switching surfaces twice. */
static bool onSurf2;
static EGLDisplay sdlDpy;
static EGLSurface sdlDraw, sdlRead;
static EGLContext sdlCtx;
static Uint64 tBegin, tSwap, tRestore; /* for the log */
static int nBegin, nRestore;

bool android_second_begin(void) {
	Uint64 t0 = SDL_GetPerformanceCounter();
	pthread_mutex_lock(&winLock);
	EGLDisplay d = eglGetCurrentDisplay();
	EGLContext c = eglGetCurrentContext();
	if (!gpuOn || !secondWin || d == EGL_NO_DISPLAY || c == EGL_NO_CONTEXT) goto fail;
	if (!onSurf2) {
		/* what SDL had current: its window's surface */
		sdlDpy = d, sdlCtx = c;
		sdlDraw = eglGetCurrentSurface(EGL_DRAW);
		sdlRead = eglGetCurrentSurface(EGL_READ);
	}
	bool fresh = false;
	if (surf2 == EGL_NO_SURFACE) {
		EGLint fmt = 0;
		eglGetConfigAttrib(d, gpuConfig, EGL_NATIVE_VISUAL_ID, &fmt);
		/* the second display's own size (ui.c draws it through a viewport of
		 * that size); before Oct 2026 the game screen's, which a display of
		 * another size cut off (Retroid Pocket Duo Lite: 1920x1080 and
		 * 1280x960, issue #8) */
		int bw = dispW > 0 ? dispW : gpuW, bh = dispH > 0 ? dispH : gpuH;
		ANativeWindow_setBuffersGeometry(secondWin, bw, bh, fmt);
		surf2 = eglCreateWindowSurface(d, gpuConfig, secondWin, NULL);
		if (surf2 == EGL_NO_SURFACE) {
			static int logged;
			if (logged++ < 3) fprintf(stderr, "second screen: eglCreateWindowSurface failed (0x%x)\n", eglGetError());
			goto fail;
		}
		surf2Dpy = d;
		surf2Interval = false;
		fresh = true;
		fprintf(stderr, "second screen: drawing with the GPU (%dx%d)\n", bw, bh);
	}
	if (!onSurf2 || fresh) {
		if (!eglMakeCurrent(d, surf2, surf2, c)) {
			static int logged;
			if (logged++ < 3) fprintf(stderr, "second screen: eglMakeCurrent failed (0x%x)\n", eglGetError());
			goto fail;
		}
		onSurf2 = true;
	}
	if (!surf2Interval) {
		eglSwapInterval(d, surf2Want);
		surf2Interval = true;
	}
	tBegin += SDL_GetPerformanceCounter() - t0;
	++nBegin;
	return true; /* winLock stays held until android_second_end */
fail:
	pthread_mutex_unlock(&winLock);
	return false;
}

void android_second_end(void) {
	Uint64 t0 = SDL_GetPerformanceCounter();
	eglSwapBuffers(surf2Dpy, surf2);
	tSwap += SDL_GetPerformanceCounter() - t0;
	pthread_mutex_unlock(&winLock);
}

void android_second_restore(void) {
	if (!onSurf2) return;
	Uint64 t0 = SDL_GetPerformanceCounter();
	eglMakeCurrent(sdlDpy, sdlDraw, sdlRead, sdlCtx);
	onSurf2 = false;
	tRestore += SDL_GetPerformanceCounter() - t0;
	++nRestore;
}

void android_second_set_interval(int interval) {
	pthread_mutex_lock(&winLock);
	if (interval != surf2Want) {
		surf2Want = interval;
		surf2Interval = false;
	}
	pthread_mutex_unlock(&winLock);
}

void android_second_log_timing(int frames) {
	if (!nBegin && !nRestore) return;
	double ms = 1000.0 / SDL_GetPerformanceFrequency();
	fprintf(stderr, "second display GL: %d redraws, switching to it %.2f ms, swap %.2f ms; %d switches back, %.2f ms each\n", nBegin,
	        nBegin ? tBegin * ms / nBegin : 0.0, nBegin ? tSwap * ms / nBegin : 0.0, nRestore, nRestore ? tRestore * ms / nRestore : 0.0);
	tBegin = tSwap = tRestore = 0;
	nBegin = nRestore = 0;
}

JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeSetDisplay(JNIEnv* env, jclass cls, jint w, jint h) {
	pthread_mutex_lock(&winLock);
	dispW = w;
	dispH = h;
	pthread_mutex_unlock(&winLock);
}

/* Called on the UI thread; holding the lock means the native side is done
 * with the old surface before Java lets it go. */
JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeSetSurface(JNIEnv* env, jclass cls, jobject surface) {
	pthread_mutex_lock(&winLock);
	/* may still be current on the game thread (until it next switches): EGL
	 * then frees it once it isn't */
	if (surf2 != EGL_NO_SURFACE) eglDestroySurface(surf2Dpy, surf2);
	surf2 = EGL_NO_SURFACE;
	if (secondWin) ANativeWindow_release(secondWin);
	secondWin = surface ? ANativeWindow_fromSurface(env, surface) : NULL;
	geometrySet = false;
	pthread_mutex_unlock(&winLock);
	__android_log_print(ANDROID_LOG_INFO, TAG, "second screen surface %s", surface ? "ready" : "gone");
}

/* kind: 1 down, 2 up, 3 motion; x, y: 0..1 across the second display.
 * Goes through SDL's event queue (thread-safe) to ui.c, which treats the
 * window id as the second view. */
JNIEXPORT void JNICALL Java_io_github_kyannon_gbads_SecondScreen_nativeTouch(JNIEnv* env, jclass cls, jint kind, jfloat x,
                                                                            jfloat y) {
	if (!SDL_WasInit(SDL_INIT_EVENTS)) return;
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = kind == 1 ? SDL_FINGERDOWN : kind == 2 ? SDL_FINGERUP : SDL_FINGERMOTION;
	ev.tfinger.touchId = 2;
	ev.tfinger.fingerId = 0;
	ev.tfinger.x = x;
	ev.tfinger.y = y;
	ev.tfinger.pressure = 1.0f;
	ev.tfinger.windowID = GBADS_SECOND_WINDOW_ID;
	SDL_PushEvent(&ev);
}

#endif

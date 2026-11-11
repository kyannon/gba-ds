/* desktop stand-ins for android.c's GPU path: a pbuffer instead of the
 * second display's window; each finished frame is saved as a BMP */
#include "android.h"
#include <SDL_egl.h>
void glReadPixels(int, int, int, int, unsigned, unsigned, void*);
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
void android_init_log(void) {}
const char* android_home(void) { return getenv("GBADS_HOME"); }
void android_config_defaults(Config* c) { (void) c; }
static int pickAsked;
bool android_pick_folder(void) { pickAsked = 1; return true; }
int android_take_picked_folder(char* o, size_t n) {
	if (!pickAsked) return 0;
	pickAsked = 0;
	snprintf(o, n, "%s", getenv("PICKED"));
	return 1;
}
bool android_second_size(int* w, int* h) {
	*w = 1024, *h = 768;
	if (getenv("STUB_SECOND")) sscanf(getenv("STUB_SECOND"), "%dx%d", w, h);
	return true;
}
void android_second_present(SDL_Surface* s) { (void) s; }
static EGLConfig conf; static int W, H; static EGLSurface pb = EGL_NO_SURFACE;
static EGLDisplay sd; static EGLSurface sdraw, sread; static EGLContext sctx; static int n;
bool android_second_gpu_init(int w, int h) {
	EGLDisplay d = eglGetCurrentDisplay(); EGLContext c = eglGetCurrentContext();
	if (d == EGL_NO_DISPLAY || c == EGL_NO_CONTEXT) return false;
	EGLint id, k; eglQueryContext(d, c, EGL_CONFIG_ID, &id);
	EGLint a[] = { EGL_CONFIG_ID, id, EGL_NONE };
	if (!eglChooseConfig(d, a, &conf, 1, &k) || k != 1) return false;
	W = w, H = h; android_second_size(&W, &H); /* as android.c: the second display's own size */
	return true;
}
static int onPb, switches, restores;
bool android_second_begin(void) {
	EGLDisplay d = eglGetCurrentDisplay(); EGLContext c = eglGetCurrentContext();
	if (pb == EGL_NO_SURFACE) {
		EGLint a[] = { EGL_WIDTH, W, EGL_HEIGHT, H, EGL_NONE };
		pb = eglCreatePbufferSurface(d, conf, a);
		if (pb == EGL_NO_SURFACE) { fprintf(stderr, "pbuffer failed 0x%x\n", eglGetError()); return false; }
	}
	if (!onPb) { sd = d, sctx = c, sdraw = eglGetCurrentSurface(EGL_DRAW), sread = eglGetCurrentSurface(EGL_READ); eglMakeCurrent(d, pb, pb, c); onPb = 1; ++switches; }
	return true;
}
void android_second_end(void) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
	glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, s->pixels);
	SDL_Surface* f = SDL_CreateRGBSurfaceWithFormat(0, W, H, 32, SDL_PIXELFORMAT_ABGR8888);
	for (int y = 0; y < H; ++y) memcpy((char*) f->pixels + y * f->pitch, (char*) s->pixels + (H - 1 - y) * s->pitch, W * 4);
	char p[256]; snprintf(p, sizeof p, "%s/second-%04d.bmp", getenv("SHOTDIR"), n++);
	SDL_SaveBMP(f, p); SDL_FreeSurface(s); SDL_FreeSurface(f);
	eglSwapBuffers(sd, pb);
}
void android_second_restore(void) { if (onPb) { eglMakeCurrent(sd, sdraw, sread, sctx); onPb = 0; ++restores; } }
void android_second_log_timing(int frames) { fprintf(stderr, "stub: %d switches to the second surface, %d back\n", switches, restores); switches = restores = 0; }
void android_perf_init(int64_t t) { (void) t; }
void android_perf_report(int64_t w) { (void) w; }

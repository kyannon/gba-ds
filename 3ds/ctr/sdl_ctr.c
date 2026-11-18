/* sdl/SDL.h for the Nintendo 3DS: the part of SDL2 that GBA DS uses, drawn
 * with the GPU (citro2d), the buttons and the touch screen read through
 * hid, sound through ndsp. The top screen is display 0 (400x240), the
 * bottom one display 1 (320x240); each window fills its display.
 *
 * One GPU frame holds both screens: it begins at the first drawing call
 * after the last one ended, and ends when every window's renderer has been
 * presented. */
#include "sdl/SDL.h"

#include "host.h"

#include <3ds.h>
#include <stdatomic.h>
#include <citro2d.h>

#include <malloc.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

/* ------------------------------------------------------------ the basics */

static char sdlError[160] = "";
static u64 startMs;

static void set_error(const char* fmt, ...) {
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(sdlError, sizeof(sdlError), fmt, ap);
	va_end(ap);
}

const char* SDL_GetError(void) {
	return sdlError;
}

const char* SDL_GetPlatform(void) {
	return "Nintendo 3DS";
}

SDL_bool SDL_SetHint(const char* name, const char* value) {
	(void) name;
	(void) value;
	return SDL_TRUE;
}

int SDL_ShowCursor(int toggle) {
	(void) toggle;
	return 0;
}

Uint32 SDL_GetTicks(void) {
	return (Uint32) (osGetTime() - startMs);
}

Uint64 SDL_GetPerformanceCounter(void) {
	return svcGetSystemTick();
}

Uint64 SDL_GetPerformanceFrequency(void) {
	return SYSCLOCK_ARM11;
}

void SDL_Delay(Uint32 ms) {
	svcSleepThread((s64) ms * 1000000LL);
}

SDL_PowerState SDL_GetPowerInfo(int* seconds, int* percent) {
	if (seconds) *seconds = -1;
	u8 level = 0, charging = 0;
	if (R_FAILED(PTMU_GetBatteryLevel(&level))) {
		if (percent) *percent = -1;
		return SDL_POWERSTATE_UNKNOWN;
	}
	PTMU_GetBatteryChargeState(&charging);
	/* (the system gives a level from 0 to 5) */
	if (percent) *percent = level >= 5 ? 100 : level * 20;
	return charging ? SDL_POWERSTATE_CHARGING : SDL_POWERSTATE_ON_BATTERY;
}

SDL_bool SDL_IntersectRect(const SDL_Rect* a, const SDL_Rect* b, SDL_Rect* out) {
	int x0 = SDL_max(a->x, b->x), y0 = SDL_max(a->y, b->y);
	int x1 = SDL_min(a->x + a->w, b->x + b->w), y1 = SDL_min(a->y + a->h, b->y + b->h);
	out->x = x0, out->y = y0;
	out->w = x1 > x0 ? x1 - x0 : 0;
	out->h = y1 > y0 ? y1 - y0 : 0;
	return out->w && out->h ? SDL_TRUE : SDL_FALSE;
}

/* ------------------------------------------------------------ threads */

struct SDL_Thread {
	Thread t;
	SDL_ThreadFunction fn;
	void* data;
};
struct SDL_mutex {
	RecursiveLock lock;
};

static void thread_main(void* arg) {
	SDL_Thread* t = arg;
	t->fn(t->data);
}

/* The app's own side threads (a save written, a file read): on the system's
 * core, below the game. */
SDL_Thread* SDL_CreateThread(SDL_ThreadFunction fn, const char* name, void* data) {
	(void) name;
	SDL_Thread* t = calloc(1, sizeof(*t));
	if (!t) return NULL;
	t->fn = fn;
	t->data = data;
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	t->t = threadCreate(thread_main, t, 64 * 1024, priority + 1, host_side_core(), true);
	if (!t->t) {
		set_error("no thread");
		free(t);
		return NULL;
	}
	return t;
}

void SDL_DetachThread(SDL_Thread* thread) {
	(void) thread; /* (made detached: it frees itself; the small SDL_Thread is left) */
}

SDL_mutex* SDL_CreateMutex(void) {
	SDL_mutex* m = calloc(1, sizeof(*m));
	if (m) RecursiveLock_Init(&m->lock);
	return m;
}

int SDL_LockMutex(SDL_mutex* m) {
	if (m) RecursiveLock_Lock(&m->lock);
	return 0;
}

int SDL_UnlockMutex(SDL_mutex* m) {
	if (m) RecursiveLock_Unlock(&m->lock);
	return 0;
}

/* ------------------------------------------------------------ surfaces */

SDL_Surface* SDL_CreateRGBSurfaceWithFormatFrom(void* pixels, int w, int h, int depth, int pitch, Uint32 format) {
	(void) depth;
	SDL_Surface* s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	s->w = w, s->h = h, s->pitch = pitch;
	s->pixels = pixels;
	s->format = &s->formatStore;
	s->formatStore.format = format;
	s->formatStore.BitsPerPixel = 32;
	s->formatStore.BytesPerPixel = 4;
	return s;
}

SDL_Surface* SDL_CreateRGBSurfaceWithFormat(Uint32 flags, int w, int h, int depth, Uint32 format) {
	(void) flags;
	void* px = calloc((size_t) SDL_max(w, 1) * SDL_max(h, 1), 4);
	if (!px) {
		set_error("no memory for a %dx%d picture", w, h);
		return NULL;
	}
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(px, w, h, depth, w * 4, format);
	if (!s) {
		free(px);
		return NULL;
	}
	s->ownsPixels = true;
	return s;
}

void SDL_FreeSurface(SDL_Surface* s) {
	if (!s) return;
	if (s->ownsPixels) free(s->pixels);
	free(s);
}

SDL_Surface* SDL_DuplicateSurface(SDL_Surface* s) {
	if (!s) return NULL;
	SDL_Surface* d = SDL_CreateRGBSurfaceWithFormat(0, s->w, s->h, 32, s->format->format);
	if (!d) return NULL;
	for (int y = 0; y < s->h; ++y) memcpy((Uint8*) d->pixels + y * d->pitch, (const Uint8*) s->pixels + y * s->pitch, (size_t) s->w * 4);
	return d;
}

Uint32 SDL_MapRGBA(const SDL_PixelFormat* f, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
	if (f->format == SDL_PIXELFORMAT_ARGB8888) return (Uint32) a << 24 | (Uint32) r << 16 | (Uint32) g << 8 | b;
	return (Uint32) a << 24 | (Uint32) b << 16 | (Uint32) g << 8 | r;
}

static void put16(FILE* f, unsigned v) {
	fputc(v & 0xFF, f), fputc(v >> 8 & 0xFF, f);
}
static void put32(FILE* f, unsigned v) {
	put16(f, v & 0xFFFF), put16(f, v >> 16);
}

/* 24-bit BMPs: the thumbnails a save state keeps. */
int SDL_SaveBMP(SDL_Surface* s, const char* path) {
	FILE* f = fopen(path, "wb");
	if (!f) {
		set_error("can't write %s", path);
		return -1;
	}
	int row = (s->w * 3 + 3) & ~3;
	fputc('B', f), fputc('M', f);
	put32(f, 54 + row * s->h), put32(f, 0), put32(f, 54);
	put32(f, 40), put32(f, s->w), put32(f, s->h), put16(f, 1), put16(f, 24);
	put32(f, 0), put32(f, row * s->h), put32(f, 2835), put32(f, 2835), put32(f, 0), put32(f, 0);
	Uint8* line = calloc(1, row);
	bool argb = s->format->format == SDL_PIXELFORMAT_ARGB8888;
	for (int y = s->h - 1; line && y >= 0; --y) {
		const Uint32* src = (const Uint32*) ((const Uint8*) s->pixels + y * s->pitch);
		for (int x = 0; x < s->w; ++x) {
			Uint32 p = src[x];
			Uint8 r = argb ? p >> 16 : p, g = p >> 8, b = argb ? p : p >> 16;
			line[x * 3] = b, line[x * 3 + 1] = g, line[x * 3 + 2] = r;
		}
		fwrite(line, 1, row, f);
	}
	free(line);
	return fclose(f) == 0 ? 0 : -1;
}

SDL_Surface* SDL_LoadBMP(const char* path) {
	FILE* f = fopen(path, "rb");
	if (!f) {
		set_error("no %s", path);
		return NULL;
	}
	Uint8 h[54];
	SDL_Surface* s = NULL;
	if (fread(h, 1, 54, f) == 54 && h[0] == 'B' && h[1] == 'M') {
		int off = h[10] | h[11] << 8 | h[12] << 16 | h[13] << 24;
		int w = h[18] | h[19] << 8 | h[20] << 16 | h[21] << 24;
		int ht = (int) (h[22] | h[23] << 8 | h[24] << 16 | (unsigned) h[25] << 24);
		int bpp = h[28] | h[29] << 8;
		bool down = ht < 0;
		if (down) ht = -ht;
		if (w > 0 && ht > 0 && w <= 4096 && ht <= 4096 && (bpp == 24 || bpp == 32)) {
			s = SDL_CreateRGBSurfaceWithFormat(0, w, ht, 32, SDL_PIXELFORMAT_ABGR8888);
			int row = (w * (bpp / 8) + 3) & ~3;
			Uint8* line = malloc(row);
			fseek(f, off, SEEK_SET);
			for (int y = 0; s && line && y < ht; ++y) {
				if (fread(line, 1, row, f) != (size_t) row) break;
				Uint32* dst = (Uint32*) s->pixels + (down ? y : ht - 1 - y) * w;
				for (int x = 0; x < w; ++x) {
					const Uint8* p = line + x * (bpp / 8);
					dst[x] = 0xFF000000u | (Uint32) p[0] << 16 | (Uint32) p[1] << 8 | p[2];
				}
			}
			free(line);
		}
	}
	fclose(f);
	if (!s) set_error("%s isn't a BMP this reads", path);
	return s;
}

/* ------------------------------------------------------------ video */

struct SDL_Window {
	int display;
	int w, h;
	SDL_Renderer* ren;
};

struct SDL_Texture {
	C3D_Tex tex;
	C3D_RenderTarget* target; /* SDL_TEXTUREACCESS_TARGET */
	int w, h; /* as the app knows it */
	int tw, th; /* the texels used: smaller when w or h is past the GPU's 1024 */
	Uint32 format;
	SDL_BlendMode blend;
	SDL_ScaleMode scale;
	bool linear; /* the filter the GPU texture is set to */
	Uint8 r, g, b, a;
};

struct SDL_Renderer {
	SDL_Window* win;
	C3D_RenderTarget* screen;
	SDL_Texture* target; /* SDL_SetRenderTarget */
	int w, h;
	Uint8 r, g, b, a;
	SDL_BlendMode blend;
	bool clip;
	SDL_Rect clipRect;
	bool vsync;
	bool presented;
};

static bool videoUp;
static SDL_Window windows[2];
static SDL_Renderer renderers[2];
static bool inFrame;
static C3D_RenderTarget* bound; /* the target of the scene under way */
static SDL_BlendMode boundBlend = SDL_BLENDMODE_BLEND;
static bool scissorOn;

int SDL_GetNumVideoDisplays(void) {
	return 2;
}

const char* SDL_GetCurrentVideoDriver(void) {
	return "n3ds";
}

int SDL_GetDisplayBounds(int display, SDL_Rect* r) {
	r->x = 0, r->y = display ? 240 : 0;
	r->w = display ? 320 : 400, r->h = 240;
	return 0;
}

const char* SDL_GetDisplayName(int display) {
	return display ? "bottom screen" : "top screen";
}

int SDL_GetDisplayDPI(int display, float* d, float* h, float* v) {
	(void) display;
	(void) d;
	(void) h;
	(void) v;
	return -1;
}

int SDL_GetDesktopDisplayMode(int display, SDL_DisplayMode* m) {
	memset(m, 0, sizeof(*m));
	m->w = display ? 320 : 400, m->h = 240;
	m->refresh_rate = 60;
	return 0;
}

int SDL_GetCurrentDisplayMode(int display, SDL_DisplayMode* m) {
	return SDL_GetDesktopDisplayMode(display, m);
}

int SDL_GetNumTouchDevices(void) {
	return 1;
}

void* SDL_GL_GetProcAddress(const char* name) {
	(void) name;
	return NULL;
}

SDL_Window* SDL_CreateWindow(const char* title, int x, int y, int w, int h, Uint32 flags) {
	(void) title;
	(void) y;
	(void) w;
	(void) h;
	(void) flags;
	int display = ((Uint32) x & 0xFFFF0000u) == 0x2FFF0000u ? x & 0xFFFF : 0;
	if (display < 0 || display > 1) display = 0;
	SDL_Window* win = &windows[display];
	win->display = display;
	win->w = display ? 320 : 400;
	win->h = 240;
	return win;
}

void SDL_DestroyWindow(SDL_Window* w) {
	(void) w;
}

Uint32 SDL_GetWindowID(SDL_Window* w) {
	return w ? (Uint32) w->display + 1 : 0;
}

int SDL_GetWindowDisplayIndex(SDL_Window* w) {
	return w ? w->display : -1;
}

void SDL_GetWindowSize(SDL_Window* win, int* w, int* h) {
	if (w) *w = win->w;
	if (h) *h = win->h;
}

SDL_Renderer* SDL_CreateRenderer(SDL_Window* w, int index, Uint32 flags) {
	(void) index;
	SDL_Renderer* r = &renderers[w->display];
	memset(r, 0, sizeof(*r));
	r->win = w;
	w->ren = r;
	r->w = w->w, r->h = w->h;
	r->screen = C2D_CreateScreenTarget(w->display ? GFX_BOTTOM : GFX_TOP, GFX_LEFT);
	if (!r->screen) {
		set_error("no render target for the %s screen", w->display ? "bottom" : "top");
		return NULL;
	}
	r->a = 255;
	r->vsync = (flags & SDL_RENDERER_PRESENTVSYNC) != 0;
	return r;
}

SDL_Renderer* SDL_CreateSoftwareRenderer(SDL_Surface* s) {
	(void) s;
	set_error("no software renderer");
	return NULL;
}

void SDL_DestroyRenderer(SDL_Renderer* r) {
	(void) r;
}

int SDL_GetRendererInfo(SDL_Renderer* r, SDL_RendererInfo* info) {
	(void) r;
	memset(info, 0, sizeof(*info));
	info->name = "citro2d";
	info->max_texture_width = info->max_texture_height = 1024;
	return 0;
}

int SDL_GetRendererOutputSize(SDL_Renderer* r, int* w, int* h) {
	if (w) *w = r->target ? r->target->w : r->w;
	if (h) *h = r->target ? r->target->h : r->h;
	return 0;
}

int SDL_RenderSetVSync(SDL_Renderer* r, int vsync) {
	r->vsync = vsync != 0;
	return 0;
}

static void doomed_free(void);
static void doomed_targets_free(void);

static void frame_begin(void) {
	if (inFrame) return;
	bool sync = false;
	for (int i = 0; i < 2; ++i) {
		if (windows[i].ren && windows[i].ren->vsync) sync = true;
		if (windows[i].ren) windows[i].ren->presented = false;
	}
	doomed_targets_free(); /* (citro3d stops the app if a render target goes inside a frame) */
	C3D_FrameBegin(sync ? C3D_FRAME_SYNCDRAW : 0);
	doomed_free();
	inFrame = true;
	bound = NULL;
	boundBlend = SDL_BLENDMODE_BLEND;
	scissorOn = false;
}

static void set_blend(SDL_BlendMode mode) {
	if (mode == boundBlend) return;
	C2D_Flush();
	boundBlend = mode;
	switch (mode) {
	case SDL_BLENDMODE_NONE:
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_ONE, GPU_ZERO, GPU_ONE, GPU_ZERO);
		break;
	case SDL_BLENDMODE_MOD:
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_DST_COLOR, GPU_ZERO, GPU_ZERO, GPU_ONE);
		break;
	case SDL_BLENDMODE_ADD:
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE, GPU_ZERO, GPU_ONE);
		break;
	default:
		C3D_AlphaBlend(GPU_BLEND_ADD, GPU_BLEND_ADD, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA, GPU_SRC_ALPHA, GPU_ONE_MINUS_SRC_ALPHA);
		break;
	}
}

/* The clip in the GPU's own terms: a screen's picture is stored on its
 * side (240 wide, the screen's width tall). */
static void apply_scissor(SDL_Renderer* r) {
	C2D_Flush();
	if (!r->clip) {
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		scissorOn = false;
		return;
	}
	int tw = r->target ? r->target->w : r->w, th = r->target ? r->target->h : r->h;
	int x0 = SDL_clamp(r->clipRect.x, 0, tw), y0 = SDL_clamp(r->clipRect.y, 0, th);
	int x1 = SDL_clamp(r->clipRect.x + r->clipRect.w, x0, tw), y1 = SDL_clamp(r->clipRect.y + r->clipRect.h, y0, th);
	if (r->target) C3D_SetScissor(GPU_SCISSOR_NORMAL, x0, y0, x1, y1);
	else C3D_SetScissor(GPU_SCISSOR_NORMAL, th - y1, tw - x1, th - y0, tw - x0);
	scissorOn = true;
}

/* Makes r's target the one drawn to. */
static void scene(SDL_Renderer* r) {
	frame_begin();
	C3D_RenderTarget* want = r->target ? r->target->target : r->screen;
	if (bound != want) {
		C2D_SceneBegin(want);
		bound = want;
		if (r->clip || scissorOn) apply_scissor(r);
	}
}

int SDL_SetRenderDrawBlendMode(SDL_Renderer* r, SDL_BlendMode mode) {
	r->blend = mode;
	return 0;
}

int SDL_SetRenderDrawColor(SDL_Renderer* ren, Uint8 r, Uint8 g, Uint8 b, Uint8 a) {
	ren->r = r, ren->g = g, ren->b = b, ren->a = a;
	return 0;
}

int SDL_RenderClear(SDL_Renderer* r) {
	frame_begin();
	C3D_RenderTarget* t = r->target ? r->target->target : r->screen;
	if (bound == t) C2D_Flush();
	C2D_TargetClear(t, C2D_Color32(r->r, r->g, r->b, r->a));
	return 0;
}

void SDL_RenderPresent(SDL_Renderer* r) {
	if (!inFrame) return;
	r->presented = true;
	for (int i = 0; i < 2; ++i) {
		if (windows[i].ren && !windows[i].ren->presented) return;
	}
	C2D_Flush();
	if (boundBlend != SDL_BLENDMODE_BLEND) set_blend(SDL_BLENDMODE_BLEND);
	if (scissorOn) {
		C3D_SetScissor(GPU_SCISSOR_DISABLE, 0, 0, 0, 0);
		scissorOn = false;
	}
	C3D_FrameEnd(0);
	inFrame = false;
	bound = NULL;
	static int frames;
	if (frames < 3) logLine("start: frame %d drawn", ++frames);
}

int SDL_RenderFlush(SDL_Renderer* r) {
	(void) r;
	if (inFrame) C2D_Flush();
	return 0;
}

int SDL_RenderSetClipRect(SDL_Renderer* r, const SDL_Rect* rect) {
	if (!rect && !r->clip) return 0;
	if (rect && r->clip && !memcmp(rect, &r->clipRect, sizeof(*rect))) return 0;
	r->clip = rect != NULL;
	if (rect) r->clipRect = *rect;
	C3D_RenderTarget* t = r->target ? r->target->target : r->screen;
	if (inFrame && bound == t) apply_scissor(r);
	return 0;
}

void SDL_RenderGetClipRect(SDL_Renderer* r, SDL_Rect* rect) {
	if (r->clip) *rect = r->clipRect;
	else memset(rect, 0, sizeof(*rect));
}

SDL_bool SDL_RenderIsClipEnabled(SDL_Renderer* r) {
	return r->clip ? SDL_TRUE : SDL_FALSE;
}

int SDL_RenderSetViewport(SDL_Renderer* r, const SDL_Rect* rect) {
	(void) r;
	(void) rect;
	return 0;
}

int SDL_RenderFillRects(SDL_Renderer* r, const SDL_Rect* rects, int n) {
	scene(r);
	set_blend(r->blend);
	u32 c = C2D_Color32(r->r, r->g, r->b, r->a);
	for (int i = 0; i < n; ++i) C2D_DrawRectSolid(rects[i].x, rects[i].y, 0.5f, rects[i].w, rects[i].h, c);
	return 0;
}

int SDL_RenderFillRect(SDL_Renderer* r, const SDL_Rect* rect) {
	SDL_Rect all = { 0, 0, 0, 0 };
	if (!rect) {
		SDL_GetRendererOutputSize(r, &all.w, &all.h);
		rect = &all;
	}
	return SDL_RenderFillRects(r, rect, 1);
}

int SDL_RenderCopy(SDL_Renderer* r, SDL_Texture* t, const SDL_Rect* src, const SDL_Rect* dst) {
	if (!t) return -1;
	scene(r);
	set_blend(t->blend);
	bool linear = t->scale != SDL_ScaleModeNearest;
	if (linear != t->linear) {
		C2D_Flush();
		C3D_TexSetFilter(&t->tex, linear ? GPU_LINEAR : GPU_NEAREST, linear ? GPU_LINEAR : GPU_NEAREST);
		t->linear = linear;
	}
	SDL_Rect whole = { 0, 0, t->w, t->h }, out = { 0, 0, 0, 0 };
	if (!src) src = &whole;
	if (!dst) {
		SDL_GetRendererOutputSize(r, &out.w, &out.h);
		dst = &out;
	}
	if (src->w <= 0 || src->h <= 0 || dst->w <= 0 || dst->h <= 0) return 0;
	/* the part of the texture, its top row at v = 1 */
	float kx = (float) t->tw / t->w / t->tex.width, ky = (float) t->th / t->h / t->tex.height;
	Tex3DS_SubTexture sub;
	sub.width = (u16) src->w;
	sub.height = (u16) src->h;
	sub.left = src->x * kx;
	sub.right = (src->x + src->w) * kx;
	sub.top = 1.0f - src->y * ky;
	sub.bottom = 1.0f - (src->y + src->h) * ky;
	C2D_Image img = { &t->tex, &sub };
	C2D_ImageTint tint;
	bool tinted = t->a != 255 || t->r != 255 || t->g != 255 || t->b != 255;
	if (tinted) {
		/* (a colour mod stands for the texture's own colour: white shapes
		 * take it as SDL would give it) */
		bool coloured = t->r != 255 || t->g != 255 || t->b != 255;
		C2D_PlainImageTint(&tint, C2D_Color32(t->r, t->g, t->b, t->a), coloured ? 1.0f : 0.0f);
	}
	C2D_DrawImageAt(img, dst->x, dst->y, 0.5f, tinted ? &tint : NULL, (float) dst->w / src->w, (float) dst->h / src->h);
	return 0;
}

int SDL_RenderReadPixels(SDL_Renderer* r, const SDL_Rect* rect, Uint32 format, void* pixels, int pitch) {
	(void) r;
	(void) rect;
	(void) format;
	(void) pixels;
	(void) pitch;
	set_error("the screens can't be read back");
	return -1;
}

SDL_bool SDL_RenderTargetSupported(SDL_Renderer* r) {
	(void) r;
	return SDL_TRUE;
}

int SDL_SetRenderTarget(SDL_Renderer* r, SDL_Texture* t) {
	if (t && !t->target) return -1;
	if (inFrame) C2D_Flush();
	r->target = t;
	return 0;
}

static int pow2(int v) {
	int p = 8;
	while (p < v) p <<= 1;
	return p;
}

/* Whole pictures on their way to the GPU's copy engine: two buffers, used in
 * turn. C3D_SyncDisplayTransfer can return with its copy still under way: it
 * waits for the GPU's PPF event, which the frame's own copies to the screens
 * have left set. With one buffer, the next picture (the game's, uploaded
 * right after the second screen's) was written over it while the copy engine
 * still read the last one, whose lower rows came out as the game's (on the
 * console; Azahar's copies finish at once). A buffer is filled again two
 * uploads later, after the upload between has waited for the GPU's queue
 * (C3D_SyncDisplayTransfer does that first), its copy with it. */
static u32* stages[2];
static size_t stageSizes[2];
static int stageNext;

/* Texel (x, y) of a w-wide texture in the GPU's layout: 8x8 tiles, row by
 * row, Morton order inside a tile; the first row is the picture's top. */
static inline u32 tiled(u32 x, u32 y, u32 w) {
	return ((((y >> 3) * (w >> 3)) + (x >> 3)) << 6) |
	       ((x & 1) | ((y & 1) << 1) | ((x & 2) << 1) | ((y & 2) << 2) | ((x & 4) << 2) | ((y & 4) << 3));
}

SDL_Texture* SDL_CreateTexture(SDL_Renderer* r, Uint32 format, int access, int w, int h) {
	(void) r;
	if (w <= 0 || h <= 0) {
		set_error("a %dx%d texture", w, h);
		return NULL;
	}
	SDL_Texture* t = calloc(1, sizeof(*t));
	if (!t) return NULL;
	t->w = w, t->h = h;
	t->tw = w, t->th = h;
	while (t->tw > 1024 || t->th > 1024) t->tw = (t->tw + 1) / 2, t->th = (t->th + 1) / 2;
	t->format = format;
	t->blend = SDL_BLENDMODE_NONE;
	t->r = t->g = t->b = t->a = 255;
	bool ok;
	if (access == SDL_TEXTUREACCESS_TARGET) {
		ok = C3D_TexInitVRAM(&t->tex, (u16) pow2(t->tw), (u16) pow2(t->th), GPU_RGBA8);
		if (ok) {
			t->target = C3D_RenderTargetCreateFromTex(&t->tex, GPU_TEXFACE_2D, 0, -1);
			if (!t->target) {
				C3D_TexDelete(&t->tex);
				ok = false;
			}
		}
	} else {
		ok = C3D_TexInit(&t->tex, (u16) pow2(t->tw), (u16) pow2(t->th), GPU_RGBA8);
		if (ok) {
			memset(t->tex.data, 0, t->tex.size);
			GSPGPU_FlushDataCache(t->tex.data, t->tex.size);
		}
	}
	if (!ok) {
		set_error("no memory for a %dx%d texture", w, h);
		free(t);
		return NULL;
	}
	C3D_TexSetFilter(&t->tex, GPU_NEAREST, GPU_NEAREST);
	C3D_TexSetWrap(&t->tex, GPU_CLAMP_TO_EDGE, GPU_CLAMP_TO_EDGE);
	return t;
}

/* A texture the app lets go may be in the frame the GPU is still drawing:
 * it's freed once the next frame begins, which waits for that one. */
static SDL_Texture* doomed[256];
static int nDoomed;

static void texture_free(SDL_Texture* t) {
	if (t->target) C3D_RenderTargetDelete(t->target);
	C3D_TexDelete(&t->tex);
	free(t);
}

/* The render targets of the textures let go: between frames. */
static void doomed_targets_free(void) {
	for (int i = 0; i < nDoomed; ++i) {
		if (!doomed[i]->target) continue;
		C3D_RenderTargetDelete(doomed[i]->target);
		doomed[i]->target = NULL;
	}
}

static void doomed_free(void) {
	for (int i = 0; i < nDoomed; ++i) texture_free(doomed[i]);
	nDoomed = 0;
}

void SDL_DestroyTexture(SDL_Texture* t) {
	if (!t) return;
	for (int i = 0; i < 2; ++i) {
		if (renderers[i].target == t) renderers[i].target = NULL;
	}
	if (t->target && bound == t->target) {
		C2D_Flush();
		bound = NULL;
	}
	if (nDoomed == (int) SDL_arraysize(doomed)) {
		/* (more than a frame ever lets go: finish what's drawn, then free) */
		if (inFrame) {
			C2D_Flush();
			C3D_FrameEnd(0);
			inFrame = false;
			bound = NULL;
		}
		C3D_FrameBegin(0);
		C3D_FrameEnd(0);
		doomed_free();
	}
	doomed[nDoomed++] = t;
}

int SDL_QueryTexture(SDL_Texture* t, Uint32* format, int* access, int* w, int* h) {
	if (!t) return -1;
	if (format) *format = t->format;
	if (access) *access = t->target ? SDL_TEXTUREACCESS_TARGET : SDL_TEXTUREACCESS_STREAMING;
	if (w) *w = t->w;
	if (h) *h = t->h;
	return 0;
}

int SDL_UpdateTexture(SDL_Texture* t, const SDL_Rect* rect, const void* pixels, int pitch) {
	if (!t || t->target || !pixels) return -1;
	SDL_Rect whole = { 0, 0, t->w, t->h };
	if (!rect) rect = &whole;
	u32* out = t->tex.data;
	u32 texW = t->tex.width;
	int step = t->w / t->tw + (t->w % t->tw ? 1 : 0); /* 1 unless the picture was too large */
	if (step < 1) step = 1;
	bool argb = t->format == SDL_PIXELFORMAT_ARGB8888;
	bool opaque = t->format == SDL_PIXELFORMAT_XBGR8888;
	if (step == 1 && rect->x == 0 && rect->w == t->tw && rect->y >= 0 && !(rect->y & 7) && rect->h > 0 && rect->y + rect->h <= t->th &&
	    (!(rect->h & 7) || rect->y + rect->h == t->th) &&
	    texW >= 64 && !inFrame) {
		/* whole rows (whole bands of 8, or the picture's last): written
		 * straight into a plain buffer in the GPU's colour order, and the
		 * GPU's copy engine (the display transfer) lays them out in tiles,
		 * which took the CPU 35 ns a pixel. Not inside a frame being drawn
		 * (the transfer would land in its command list) */
		u32 h8 = (u32) (rect->h + 7) & ~7u;
		size_t need = (size_t) texW * h8 * 4;
		int k = stageNext;
		if (need > stageSizes[k]) {
			if (stages[k]) linearFree(stages[k]);
			stages[k] = linearAlloc(need);
			stageSizes[k] = stages[k] ? need : 0;
		}
		u32* stage = stages[k];
		if (stage) stageNext ^= 1;
		if (stage) {
			for (int y = 0; y < rect->h; ++y) {
				const Uint32* src = (const Uint32*) ((const Uint8*) pixels + (size_t) y * pitch);
				u32* o = stage + (size_t) y * texW;
				int x = 0;
				if (argb) for (; x < t->tw; ++x) o[x] = src[x] << 8 | src[x] >> 24;
				else if (opaque) for (; x < t->tw; ++x) o[x] = __builtin_bswap32(src[x] | 0xFF000000u);
				else for (; x < t->tw; ++x) o[x] = __builtin_bswap32(src[x]);
			}
			GSPGPU_FlushDataCache(stage, (u32) texW * (u32) rect->h * 4);
			C3D_SyncDisplayTransfer(stage, GX_BUFFER_DIM(texW, h8), out + (size_t) rect->y * texW, GX_BUFFER_DIM(texW, h8),
			                        GX_TRANSFER_FLIP_VERT(0) | GX_TRANSFER_OUT_TILED(1) | GX_TRANSFER_RAW_COPY(0) |
			                        GX_TRANSFER_IN_FORMAT(GX_TRANSFER_FMT_RGBA8) | GX_TRANSFER_OUT_FORMAT(GX_TRANSFER_FMT_RGBA8) |
			                        GX_TRANSFER_SCALING(GX_TRANSFER_SCALE_NO));
			return 0;
		}
	}
	if (step == 1) {
		/* (the usual case: no division (the ARM11 has none: two a pixel
		 * were most of a 480x320 picture's 20 ms), each row's tiles worked
		 * out once) */
		static const u8 xbits[8] = { 0, 1, 4, 5, 16, 17, 20, 21 };
		int w = SDL_min(rect->w, t->tw - rect->x), h = SDL_min(rect->h, t->th - rect->y);
		for (int y = 0; y < h; ++y) {
			const Uint32* src = (const Uint32*) ((const Uint8*) pixels + (size_t) y * pitch);
			u32 ty = (u32) (rect->y + y);
			u32* row = out + ((((ty >> 3) * (texW >> 3)) << 6) | ((ty & 1) << 1) | ((ty & 2) << 2) | ((ty & 4) << 3));
			for (int x = 0; x < w; ++x) {
				u32 tx = (u32) (rect->x + x);
				Uint32 p = src[x];
				if (argb) p = p << 8 | p >> 24;
				else p = __builtin_bswap32(opaque ? p | 0xFF000000u : p);
				row[((tx >> 3) << 6) | xbits[tx & 7]] = p;
			}
		}
		GSPGPU_FlushDataCache(t->tex.data, t->tex.size);
		return 0;
	}
	for (int y = 0; y < rect->h; y += step) {
		const Uint32* src = (const Uint32*) ((const Uint8*) pixels + (size_t) y * pitch);
		u32 ty = (u32) ((rect->y + y) / step);
		if ((int) ty >= t->th) break;
		for (int x = 0; x < rect->w; x += step) {
			u32 tx = (u32) ((rect->x + x) / step);
			if ((int) tx >= t->tw) break;
			Uint32 p = src[x];
			/* the GPU's RGBA8 is R in the top byte */
			if (argb) p = p << 8 | p >> 24;
			else p = __builtin_bswap32(opaque ? p | 0xFF000000u : p);
			out[tiled(tx, ty, texW)] = p;
		}
	}
	GSPGPU_FlushDataCache(t->tex.data, t->tex.size);
	return 0;
}

SDL_Texture* SDL_CreateTextureFromSurface(SDL_Renderer* r, SDL_Surface* s) {
	if (!s) return NULL;
	SDL_Texture* t = SDL_CreateTexture(r, s->format->format, SDL_TEXTUREACCESS_STATIC, s->w, s->h);
	if (!t) return NULL;
	t->blend = SDL_BLENDMODE_BLEND; /* (as SDL does for a surface with alpha) */
	SDL_UpdateTexture(t, NULL, s->pixels, s->pitch);
	return t;
}

int SDL_SetTextureBlendMode(SDL_Texture* t, SDL_BlendMode mode) {
	if (t) t->blend = mode;
	return 0;
}

int SDL_SetTextureScaleMode(SDL_Texture* t, SDL_ScaleMode mode) {
	if (t) t->scale = mode;
	return 0;
}

int SDL_SetTextureColorMod(SDL_Texture* t, Uint8 r, Uint8 g, Uint8 b) {
	if (t) t->r = r, t->g = g, t->b = b;
	return 0;
}

int SDL_SetTextureAlphaMod(SDL_Texture* t, Uint8 a) {
	if (t) t->a = a;
	return 0;
}

/* ------------------------------------------------------------ sound
 *
 * A queue, as SDL_QueueAudio's: the app puts in what the game made, and the
 * DSP plays it from a ring of wave buffers filled as they come free. */

#define WAVE_BUFFERS 6
#define WAVE_FRAMES 512
#define FIFO_FRAMES 16384

static bool dspUp, audioOpen, audioPaused;
static ndspWaveBuf waveBuf[WAVE_BUFFERS];
static s16* waveMem;
static s16 fifo[FIFO_FRAMES * 2];
static volatile u32 fifoHead, fifoCount; /* frames */
static LightLock audioLock;
static int deviceRate = 48000; /* what the app asked for */
static int sourceRate = 32768; /* what the game makes (SDL_NewAudioStream) */

/* Moves queued sound into wave buffers the DSP has finished. */
static void audio_feed(void) {
	for (int i = 0; i < WAVE_BUFFERS; ++i) {
		ndspWaveBuf* b = &waveBuf[i];
		if (b->status != NDSP_WBUF_FREE && b->status != NDSP_WBUF_DONE) continue;
		u32 n = fifoCount < WAVE_FRAMES ? fifoCount : WAVE_FRAMES;
		/* whole buffers while there's enough, so a short one is the last */
		if (n < WAVE_FRAMES / 2) break;
		s16* dst = b->data_pcm16;
		for (u32 k = 0; k < n; ++k) {
			u32 at = (fifoHead + k) % FIFO_FRAMES;
			dst[k * 2] = fifo[at * 2];
			dst[k * 2 + 1] = fifo[at * 2 + 1];
		}
		fifoHead = (fifoHead + n) % FIFO_FRAMES;
		fifoCount -= n;
		b->nsamples = n;
		DSP_FlushDataCache(dst, n * 4);
		ndspChnWaveBufAdd(0, b);
	}
}

static void audio_callback(void* arg) {
	(void) arg;
	if (!audioOpen || audioPaused) return;
	LightLock_Lock(&audioLock);
	audio_feed();
	LightLock_Unlock(&audioLock);
}

int SDL_AudioInit(const char* driver) {
	(void) driver;
	if (dspUp) return 0;
	/* ndsp needs the DSP firmware dump (sdmc:/3ds/dspfirm.cdc); without it
	 * the game runs silent. An emulator has no firmware to give, but ndsp
	 * won't start without one: there, and never on a console, a blank one. */
	Result rc = ndspInit();
	if (R_FAILED(rc) && isEmulator()) {
		static u8 blankFirmware[4096];
		ndspUseComponent(blankFirmware, sizeof(blankFirmware), 0xFF, 0xFF);
		rc = ndspInit();
	}
	if (R_FAILED(rc)) {
		set_error("ndspInit %08lX (no sdmc:/3ds/dspfirm.cdc?)", (unsigned long) rc);
		return -1;
	}
	LightLock_Init(&audioLock);
	dspUp = true;
	return 0;
}

void SDL_AudioQuit(void) {
	if (!dspUp) return;
	ndspExit();
	dspUp = false;
}

const char* SDL_GetCurrentAudioDriver(void) {
	return dspUp ? "ndsp" : NULL;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char* device, int capture, const SDL_AudioSpec* want, SDL_AudioSpec* have, int changes) {
	(void) device;
	(void) capture;
	(void) changes;
	if (!dspUp && SDL_AudioInit(NULL) != 0) return 0;
	if (!waveMem) waveMem = linearAlloc(WAVE_BUFFERS * WAVE_FRAMES * 4);
	if (!waveMem) {
		set_error("no memory for sound");
		return 0;
	}
	deviceRate = want->freq > 0 ? want->freq : 48000;
	if (have) {
		*have = *want;
		have->freq = deviceRate;
		have->samples = WAVE_FRAMES;
	}
	ndspSetOutputMode(NDSP_OUTPUT_STEREO);
	ndspSetOutputCount(1);
	ndspChnReset(0);
	ndspChnSetFormat(0, NDSP_FORMAT_STEREO_PCM16);
	ndspChnSetInterp(0, NDSP_INTERP_LINEAR);
	ndspChnSetRate(0, (float) sourceRate);
	float mix[12] = { 1.0f, 1.0f };
	ndspChnSetMix(0, mix);
	ndspChnWaveBufClear(0);
	memset(waveBuf, 0, sizeof(waveBuf));
	for (int i = 0; i < WAVE_BUFFERS; ++i) waveBuf[i].data_vaddr = waveMem + i * WAVE_FRAMES * 2;
	fifoHead = fifoCount = 0;
	audioPaused = true;
	audioOpen = true;
	ndspSetCallback(audio_callback, NULL);
	return 1;
}

void SDL_CloseAudioDevice(SDL_AudioDeviceID dev) {
	(void) dev;
	if (!audioOpen) return;
	audioOpen = false;
	ndspSetCallback(NULL, NULL);
	ndspChnWaveBufClear(0);
}

void SDL_PauseAudioDevice(SDL_AudioDeviceID dev, int pause) {
	(void) dev;
	if (!audioOpen) return;
	audioPaused = pause != 0;
	ndspChnSetPaused(0, audioPaused);
}

int SDL_QueueAudio(SDL_AudioDeviceID dev, const void* data, Uint32 len) {
	(void) dev;
	if (!audioOpen) return -1;
	const s16* src = data;
	u32 frames = len / 4;
	LightLock_Lock(&audioLock);
	if (frames > FIFO_FRAMES - fifoCount) frames = FIFO_FRAMES - fifoCount;
	u32 tail = (fifoHead + fifoCount) % FIFO_FRAMES;
	for (u32 k = 0; k < frames; ++k) {
		u32 at = (tail + k) % FIFO_FRAMES;
		fifo[at * 2] = src[k * 2];
		fifo[at * 2 + 1] = src[k * 2 + 1];
	}
	fifoCount += frames;
	if (!audioPaused) audio_feed();
	LightLock_Unlock(&audioLock);
	return 0;
}

/* What hasn't been heard yet, in the bytes the app queued: its own rate's. */
Uint32 SDL_GetQueuedAudioSize(SDL_AudioDeviceID dev) {
	(void) dev;
	if (!audioOpen) return 0;
	LightLock_Lock(&audioLock);
	u32 frames = fifoCount;
	for (int i = 0; i < WAVE_BUFFERS; ++i) {
		if (waveBuf[i].status == NDSP_WBUF_QUEUED) frames += waveBuf[i].nsamples;
		else if (waveBuf[i].status == NDSP_WBUF_PLAYING) {
			u32 pos = ndspChnGetSamplePos(0);
			frames += pos < waveBuf[i].nsamples ? waveBuf[i].nsamples - pos : 0;
		}
	}
	LightLock_Unlock(&audioLock);
	return (Uint32) ((u64) frames * deviceRate / sourceRate) * 4;
}

void SDL_ClearQueuedAudio(SDL_AudioDeviceID dev) {
	(void) dev;
	if (!audioOpen) return;
	LightLock_Lock(&audioLock);
	fifoHead = fifoCount = 0;
	ndspChnWaveBufClear(0);
	for (int i = 0; i < WAVE_BUFFERS; ++i) waveBuf[i].status = NDSP_WBUF_FREE;
	LightLock_Unlock(&audioLock);
}

struct SDL_AudioStream {
	s16 data[8192 * 2];
	int frames;
};

SDL_AudioStream* SDL_NewAudioStream(SDL_AudioFormat sf, Uint8 sc, int srate, SDL_AudioFormat df, Uint8 dc, int drate) {
	(void) sf;
	(void) sc;
	(void) df;
	(void) dc;
	(void) drate;
	SDL_AudioStream* s = calloc(1, sizeof(*s));
	if (!s) return NULL;
	if (srate > 0 && srate != sourceRate) {
		sourceRate = srate;
		if (audioOpen) ndspChnSetRate(0, (float) sourceRate);
	}
	return s;
}

int SDL_AudioStreamPut(SDL_AudioStream* s, const void* buf, int len) {
	int frames = len / 4;
	if (frames > 8192 - s->frames) frames = 8192 - s->frames;
	memcpy(s->data + s->frames * 2, buf, (size_t) frames * 4);
	s->frames += frames;
	return 0;
}

int SDL_AudioStreamGet(SDL_AudioStream* s, void* buf, int len) {
	int frames = len / 4;
	if (frames > s->frames) frames = s->frames;
	if (!frames) return 0;
	memcpy(buf, s->data, (size_t) frames * 4);
	s->frames -= frames;
	if (s->frames) memmove(s->data, s->data + frames * 2, (size_t) s->frames * 4);
	return frames * 4;
}

void SDL_FreeAudioStream(SDL_AudioStream* s) {
	free(s);
}

/* ------------------------------------------------------------ input
 *
 * One controller: the console's own buttons. A and B by position as SDL
 * names them (its A is the bottom face button: the 3DS's B), which is how
 * the app reads them; X and Y by their labels, which is what its menus and
 * hotkeys show. ZL is the controller's
 * GUIDE (GBA DS's menu button), ZR its right trigger; the Circle Pad is the
 * left stick. */

struct SDL_Joystick {
	int unused;
};
struct SDL_GameController {
	int unused;
};
static SDL_Joystick theJoystick;
static SDL_GameController thePad;
static u32 padHeld;
/* The buttons are read every few milliseconds on a thread of their own: the
 * app looks once a frame, and a frame that waits on the SD card (the game
 * list, while logos are made) is longer than a tap on the D-Pad. */
static atomic_uint padLatched, padNow;
static Thread padThread;
static volatile bool padStop;

static void pad_main(void* arg) {
	(void) arg;
	while (!padStop) {
		hidScanInput();
		atomic_fetch_or(&padLatched, hidKeysDown());
		atomic_store(&padNow, hidKeysHeld());
		svcSleepThread(4000000LL);
	}
}

static void pad_start(void) {
	s32 priority = 0x30;
	svcGetThreadPriority(&priority, CUR_THREAD_HANDLE);
	padStop = false;
	padThread = threadCreate(pad_main, NULL, 8 * 1024, priority - 1, host_side_core(), false);
}

static void pad_end(void) {
	if (!padThread) return;
	padStop = true;
	threadJoin(padThread, U64_MAX);
	threadFree(padThread);
	padThread = NULL;
}
static circlePosition circle;
static const u32 padKeys[SDL_CONTROLLER_BUTTON_MAX] = {
	[SDL_CONTROLLER_BUTTON_A] = KEY_B,
	[SDL_CONTROLLER_BUTTON_B] = KEY_A,
	[SDL_CONTROLLER_BUTTON_X] = KEY_X,
	[SDL_CONTROLLER_BUTTON_Y] = KEY_Y,
	[SDL_CONTROLLER_BUTTON_BACK] = KEY_SELECT,
	[SDL_CONTROLLER_BUTTON_GUIDE] = KEY_ZL,
	[SDL_CONTROLLER_BUTTON_START] = KEY_START,
	[SDL_CONTROLLER_BUTTON_LEFTSHOULDER] = KEY_L,
	[SDL_CONTROLLER_BUTTON_RIGHTSHOULDER] = KEY_R,
	[SDL_CONTROLLER_BUTTON_DPAD_UP] = KEY_DUP,
	[SDL_CONTROLLER_BUTTON_DPAD_DOWN] = KEY_DDOWN,
	[SDL_CONTROLLER_BUTTON_DPAD_LEFT] = KEY_DLEFT,
	[SDL_CONTROLLER_BUTTON_DPAD_RIGHT] = KEY_DRIGHT,
};
static const char* const padNames[SDL_CONTROLLER_BUTTON_MAX] = {
	"a", "b", "x", "y", "back", "guide", "start", "leftstick", "rightstick", "leftshoulder", "rightshoulder",
	"dpup", "dpdown", "dpleft", "dpright", "misc1", "paddle1", "paddle2", "paddle3", "paddle4", "touchpad",
};

int SDL_NumJoysticks(void) {
	return 1;
}

SDL_bool SDL_IsGameController(int index) {
	return index == 0 ? SDL_TRUE : SDL_FALSE;
}

SDL_GameController* SDL_GameControllerOpen(int index) {
	return index == 0 ? &thePad : NULL;
}

const char* SDL_GameControllerName(SDL_GameController* c) {
	(void) c;
	return "Nintendo 3DS";
}

char* SDL_GameControllerMapping(SDL_GameController* c) {
	(void) c;
	return strdup("000000004e696e74656e646f20334453,Nintendo 3DS,a:b0,b:b1,x:b2,y:b3,back:b4,guide:b5,start:b6,leftshoulder:b9,"
	              "rightshoulder:b10,dpup:b11,dpdown:b12,dpleft:b13,dpright:b14,leftx:a0,lefty:a1,righttrigger:a5,");
}

SDL_Joystick* SDL_GameControllerGetJoystick(SDL_GameController* c) {
	return c ? &theJoystick : NULL;
}

Uint8 SDL_GameControllerGetButton(SDL_GameController* c, SDL_GameControllerButton b) {
	(void) c;
	return b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX && padKeys[b] && (padHeld & padKeys[b]) ? 1 : 0;
}

Sint16 SDL_GameControllerGetAxis(SDL_GameController* c, SDL_GameControllerAxis a) {
	(void) c;
	switch (a) {
	case SDL_CONTROLLER_AXIS_LEFTX: return (Sint16) SDL_clamp(circle.dx * 210, -32767, 32767);
	case SDL_CONTROLLER_AXIS_LEFTY: return (Sint16) SDL_clamp(-circle.dy * 210, -32767, 32767);
	case SDL_CONTROLLER_AXIS_TRIGGERRIGHT: return padHeld & KEY_ZR ? 32767 : 0;
	default: return 0;
	}
}

const char* SDL_GameControllerGetStringForButton(SDL_GameControllerButton b) {
	return b >= 0 && b < SDL_CONTROLLER_BUTTON_MAX ? padNames[b] : "?";
}

int SDL_GameControllerAddMapping(const char* mapping) {
	(void) mapping;
	return 0;
}

int SDL_GameControllerAddMappingsFromFile(const char* path) {
	(void) path;
	return 0;
}

SDL_Joystick* SDL_JoystickOpen(int index) {
	return index == 0 ? &theJoystick : NULL;
}

void SDL_JoystickClose(SDL_Joystick* j) {
	(void) j;
}

SDL_Joystick* SDL_JoystickFromInstanceID(SDL_JoystickID id) {
	return id == 0 ? &theJoystick : NULL;
}

const char* SDL_JoystickName(SDL_Joystick* j) {
	(void) j;
	return "Nintendo 3DS";
}

SDL_JoystickGUID SDL_JoystickGetGUID(SDL_Joystick* j) {
	(void) j;
	SDL_JoystickGUID g = { { 0, 0, 0, 0, 'N', 'i', 'n', 't', 'e', 'n', 'd', 'o', ' ', '3', 'D', 'S' } };
	return g;
}

void SDL_JoystickGetGUIDString(SDL_JoystickGUID guid, char* out, int n) {
	int at = 0;
	for (int i = 0; i < 16 && at + 2 < n; ++i) at += snprintf(out + at, (size_t) (n - at), "%02x", guid.data[i]);
	if (n > 0) out[at < n ? at : n - 1] = 0;
}

SDL_JoystickID SDL_JoystickGetDeviceInstanceID(int index) {
	return index == 0 ? 0 : -1;
}

int SDL_JoystickNumButtons(SDL_Joystick* j) {
	(void) j;
	return 15;
}

int SDL_JoystickNumAxes(SDL_Joystick* j) {
	(void) j;
	return 6;
}

int SDL_JoystickNumHats(SDL_Joystick* j) {
	(void) j;
	return 0;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick* j, int b) {
	(void) j;
	return SDL_GameControllerGetButton(&thePad, (SDL_GameControllerButton) b);
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick* j, int a) {
	(void) j;
	return SDL_GameControllerGetAxis(&thePad, (SDL_GameControllerAxis) a);
}

Uint8 SDL_JoystickGetHat(SDL_Joystick* j, int h) {
	(void) j;
	(void) h;
	return SDL_HAT_CENTERED;
}

void SDL_JoystickUpdate(void) {
}

int SDL_JoystickAttachVirtual(SDL_JoystickType type, int axes, int buttons, int hats) {
	(void) type;
	(void) axes;
	(void) buttons;
	(void) hats;
	set_error("no virtual controllers");
	return -1;
}

int SDL_JoystickSetVirtualAxis(SDL_Joystick* j, int axis, Sint16 v) {
	(void) j;
	(void) axis;
	(void) v;
	return -1;
}

int SDL_JoystickSetVirtualButton(SDL_Joystick* j, int button, Uint8 v) {
	(void) j;
	(void) button;
	(void) v;
	return -1;
}

int SDL_JoystickSetVirtualHat(SDL_Joystick* j, int hat, Uint8 v) {
	(void) j;
	(void) hat;
	(void) v;
	return -1;
}

const Uint8* SDL_GetKeyboardState(int* n) {
	static Uint8 none[SDL_NUM_SCANCODES];
	if (n) *n = SDL_NUM_SCANCODES;
	return none;
}

const char* SDL_GetKeyName(SDL_Keycode key) {
	(void) key;
	return "";
}

/* ------------------------------------------------------------ events */

#define EVENTS 64
static SDL_Event queue[EVENTS];
static int queueHead, queueCount;
static bool pumped;
static bool touching;
static touchPosition lastTouch;
static bool quitSent;

int SDL_PushEvent(SDL_Event* ev) {
	if (queueCount == EVENTS) return 0;
	queue[(queueHead + queueCount++) % EVENTS] = *ev;
	return 1;
}

static void push_finger(Uint32 type, const touchPosition* t) {
	SDL_Event ev;
	memset(&ev, 0, sizeof(ev));
	ev.type = type;
	ev.tfinger.x = (float) t->px / 320.0f;
	ev.tfinger.y = (float) t->py / 240.0f;
	ev.tfinger.windowID = 2; /* the bottom screen's window */
	SDL_PushEvent(&ev);
}

/* Once for each time the app empties the queue: the system's own events,
 * the buttons, the touch screen. */
static void pump(void) {
	if (!aptMainLoop()) {
		if (!quitSent) {
			SDL_Event ev;
			memset(&ev, 0, sizeof(ev));
			ev.type = SDL_QUIT;
			SDL_PushEvent(&ev);
			quitSent = true;
		}
		return;
	}
	{
		/* (a diagnosis: the frames that keep the buttons waiting) */
		static u64 last;
		static int told;
		u64 now = osGetTime();
		if (last && now - last > 150 && told < 200) ++told, logLine("slow frame: %u ms", (unsigned) (now - last));
		last = now;
		static unsigned pumps;
		if (++pumps % 900 == 0) host_mem_note("now");
	}
	u32 down;
	if (padThread) {
		/* (a press shorter than the frame still counts as held for this one) */
		down = atomic_exchange(&padLatched, 0);
		if (down) host_ui_busy();
		padHeld = atomic_load(&padNow) | down;
	} else {
		hidScanInput();
		down = hidKeysDown();
		padHeld = hidKeysHeld();
	}
	hidCircleRead(&circle);
	for (int b = 0; b < SDL_CONTROLLER_BUTTON_MAX; ++b) {
		if (!padKeys[b] || !(down & padKeys[b])) continue;
		SDL_Event ev;
		memset(&ev, 0, sizeof(ev));
		ev.type = SDL_JOYBUTTONDOWN;
		ev.jbutton.button = (Uint8) b;
		ev.jbutton.state = 1;
		SDL_PushEvent(&ev);
		ev.type = SDL_CONTROLLERBUTTONDOWN;
		SDL_PushEvent(&ev);
	}
	if (padHeld & KEY_TOUCH) {
		touchPosition t;
		hidTouchRead(&t);
		if (!touching) push_finger(SDL_FINGERDOWN, &t);
		else if (t.px != lastTouch.px || t.py != lastTouch.py) push_finger(SDL_FINGERMOTION, &t);
		lastTouch = t;
		touching = true;
	} else if (touching) {
		push_finger(SDL_FINGERUP, &lastTouch);
		touching = false;
	}
}

int SDL_PollEvent(SDL_Event* ev) {
	if (!queueCount && !pumped) {
		pump();
		pumped = true;
	}
	if (!queueCount) {
		pumped = false;
		return 0;
	}
	if (ev) *ev = queue[queueHead];
	queueHead = (queueHead + 1) % EVENTS;
	--queueCount;
	return 1;
}

/* ------------------------------------------------------------ start, end */

int SDL_Init(Uint32 flags) {
	if (!startMs) startMs = osGetTime();
	if ((flags & SDL_INIT_VIDEO) && !videoUp) {
		logLine("start: screens");
		gfxInitDefault();
		if (!C3D_Init(C3D_DEFAULT_CMDBUF_SIZE) || !C2D_Init(C2D_DEFAULT_MAX_OBJECTS)) {
			set_error("the GPU didn't start");
			return -1;
		}
		C2D_Prepare();
		ptmuInit();
		videoUp = true;
		logLine("start: the GPU is up");
		if (!padThread) pad_start();
	}
	if (flags & SDL_INIT_AUDIO) {
		int rc = SDL_AudioInit(NULL); /* (silent without the DSP's firmware) */
		logLine("start: sound %s", rc == 0 ? "is up" : SDL_GetError());
	}
	return 0;
}

void SDL_Quit(void) {
	pad_end();
	SDL_CloseAudioDevice(1);
	SDL_AudioQuit();
	if (videoUp) {
		if (inFrame) C3D_FrameEnd(0);
		inFrame = false;
		ptmuExit();
		C2D_Fini();
		C3D_Fini();
		gfxExit();
		videoUp = false;
	}
}

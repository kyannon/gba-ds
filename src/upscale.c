/* Upscaling filters on the GPU (issue #89); see upscale.h.
 *
 * SDL2's renderer has no way to run a shader of our own, so this draws with
 * OpenGL ES 2 directly, on the renderer's own context, between its draws:
 *   1. SDL_RenderFlush, so everything SDL queued is drawn first;
 *   2. SDL_GL_BindTexture for the GL names of SDL's textures (the game's
 *      picture, and our own render-target textures, each pass's result);
 *   3. the filter's program draws a quad into our own framebuffer object,
 *      attached to the pass's target texture;
 *   4. every piece of state SDL's GLES2 renderer caches is put back: the
 *      program, the framebuffer, the viewport, blending and the scissor test
 *      (as they were), the texture (SDL_GL_UnbindTexture, which tells SDL it
 *      must bind its next one again) and the array buffer (SDL binds its own
 *      at every queue run, and sets every attribute's pointer at every draw).
 * The result is an ordinary SDL texture that SDL then draws to the screen.
 *
 * The filters:
 *   - Scale2x / Scale4x, written here: AdvanceMAME's algorithm (Andrea
 *     Mazzoleni): each pixel E becomes four, each taking a neighbour's colour
 *     where two neighbours meeting at that corner match and the edge isn't
 *     straight. The pixels are read at their centres, nearest, so equal colours
 *     compare exactly. Scale4x is two Scale2x passes.
 *   - hq2x, MMPX, xBR-lv2, xBR-lv3, OmniScale: mGBA's own shader packs, unmodified
 *     (third_party/mgba-shaders, embedded by tools/embed_shaders.py), run the way
 *     mGBA's GLES2 renderer runs them: GLSL ES 1.00 with its plain vertex shader
 *     where the pack has none, attribute `position`, uniforms `tex`, `texSize`
 *     (the source's size), `outputSize` (the target's) and the manifest's
 *     uniforms at their defaults. (libmgba itself is built without that
 *     renderer: scripts/mgba-options.sh.)
 * Each program is built the first time its filter is used; one that doesn't
 * build leaves its filter drawing as Sharp (logged once). MMPX, xBR-lv2,
 * xBR-lv3 and OmniScale are heavy: on a GPU in tooWeak they aren't run at all
 * (greyed in the menus, drawn as Sharp), on one not tried they run with a
 * warning. */
#include "upscale.h"
#include "upscale_shaders.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* per filter: the program, its scale each pass and how many passes */
enum { PROG_SCALE2X, PROG_PACK0, PROG_COUNT = PROG_PACK0 + UP_PACK_COUNT };
static const struct {
	int prog, scale, passes;
	bool heavy; /* too much for some GPUs (tooWeak below) */
} filters[FILTER_COUNT] = {
	[FILTER_SCALE2X] = { PROG_SCALE2X, 2, 1 },
	[FILTER_SCALE4X] = { PROG_SCALE2X, 2, 2 },
	[FILTER_HQ2X] = { PROG_PACK0 + UP_PACK_HQ2X, 2, 1 },
	[FILTER_MMPX] = { PROG_PACK0 + UP_PACK_MMPX, 2, 1, true },
	[FILTER_XBR_LV2] = { PROG_PACK0 + UP_PACK_XBR_LV2, 4, 1, true },
	[FILTER_XBR_LV3] = { PROG_PACK0 + UP_PACK_XBR_LV3, 4, 1, true },
	[FILTER_OMNISCALE] = { PROG_PACK0 + UP_PACK_OMNISCALE, 4, 1, true },
};
#define MAX_PASSES 2

/* GPUs (a part of GL_RENDERER) the heavy filters are known to be too much for:
 * they're shown greyed and draw as Sharp there (the owner's rule, Nov 26, 2026:
 * the heavy ones are for stronger devices, greyed out where they can't hold
 * full speed). On a GPU in neither list they're allowed, with a warning. */
static const char* const tooWeak[] = {
	"Mali-G52", /* RG DS Plus (BananaOS, Panfrost), Nov 26, 2026: MMPX and xBR-lv3
	             * about 200 ms a frame, OmniScale about 60; xBR-lv2 about 40 fps
	             * even in a light game (Ninja Five-O) */
};
/* GPUs they were tried on at full speed (none yet) */
static const char* const triedFine[] = { NULL };

static int gpu_class(const char* gpu) {
	if (!gpu || !*gpu) return 0;
	for (size_t i = 0; i < sizeof(tooWeak) / sizeof(tooWeak[0]); ++i) {
		if (tooWeak[i] && strstr(gpu, tooWeak[i])) return -1;
	}
	for (size_t i = 0; i < sizeof(triedFine) / sizeof(triedFine[0]); ++i) {
		if (triedFine[i] && strstr(gpu, triedFine[i])) return 1;
	}
	return 0;
}

int upscale_factor(int filter) {
	if (filter < 0 || filter >= FILTER_COUNT || !filters[filter].scale) return 0;
	int f = 1;
	for (int i = 0; i < filters[filter].passes; ++i) f *= filters[filter].scale;
	return f;
}

/* --- the shaders */

/* What goes before every shader. First GLSL ES 1.00, as mGBA's GLES2 renderer
 * does (with high precision where the GPU has it: the upscalers find their
 * place inside a source pixel from texCoord, which medium precision can't hold
 * at 4x a 240-pixel row). hq2x, OmniScale and xBR were written for desktop
 * GLSL (bit operations, mat4x3), which ES 1.00 refuses: those are built again
 * as GLSL ES 3.00 with mGBA's own GL 3 defines (_gl32VHeader / _gl32FHeader),
 * which an OpenGL ES 3 context (the RG DS Plus's Mali, Mesa) takes even when
 * SDL asked for ES 2. */
static const char* const vsHeader[2] = {
	"#version 100\n",
	"#version 300 es\n"
	"#define attribute in\n"
	"#define varying out\n",
};
static const char* const fsHeader[2] = {
	"#version 100\n"
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n",
	"#version 300 es\n"
	"precision highp float;\n"
	"#define varying in\n"
	"#define texture2D texture\n"
	"out vec4 compat_FragColor;\n"
	"#define gl_FragColor compat_FragColor\n",
};
static const char* const glslName[2] = { "GLSL ES 1.00", "GLSL ES 3.00" };

/* mGBA's plain vertex shader for a pass (its _nullVertexShader): texCoord's
 * t = 0 is the first row, as SDL uploads it and as our framebuffer writes it */
static const char* const plainVs =
	"attribute vec4 position;\n"
	"varying vec2 texCoord;\n"
	"void main() {\n"
	"	gl_Position = position;\n"
	"	texCoord = (position.st + vec2(1.0, 1.0)) * vec2(0.5, 0.5);\n"
	"}\n";

/* One Scale2x step: the target is twice the source each way; gl_FragCoord's
 * row y comes from the source's row y / 2. */
static const char* const scale2xFs =
	"uniform sampler2D tex;\n"
	"uniform vec2 texSize;\n"
	"void main() {\n"
	"	vec2 o = floor(gl_FragCoord.xy);\n"
	"	vec2 s = floor(o * 0.5);\n"
	"	vec2 q = o - 2.0 * s;\n"
	"	vec2 d = 1.0 / texSize;\n"
	"	vec2 c = (s + 0.5) * d;\n"
	"	vec4 E = texture2D(tex, c);\n"
	"	vec4 B = texture2D(tex, c - vec2(0.0, d.y));\n"
	"	vec4 H = texture2D(tex, c + vec2(0.0, d.y));\n"
	"	vec4 D = texture2D(tex, c - vec2(d.x, 0.0));\n"
	"	vec4 F = texture2D(tex, c + vec2(d.x, 0.0));\n"
	"	vec4 r = E;\n"
	"	if (B != H && D != F) {\n"
	"		if (q.y < 0.5) r = q.x < 0.5 ? (D == B ? D : E) : (B == F ? F : E);\n"
	"		else r = q.x < 0.5 ? (D == H ? D : E) : (H == F ? F : E);\n"
	"	}\n"
	"	gl_FragColor = r;\n"
	"}\n";

static const char* prog_name(int p) {
	return p >= PROG_PACK0 && p < PROG_COUNT ? upscalePacks[p - PROG_PACK0].name : "Scale2x";
}

#ifdef __wasi__

/* The web build: SDL's renderer there is the software one, so each pass runs
 * in the web player's own WebGL2 context instead (web/player/worker.js,
 * web_gl_pass), with the same shaders, headers, uniforms and quad as on a
 * device: the picture's pixels go in, the pass's come back, and the result is
 * an ordinary streaming texture. Slower than on a device (two copies a pass),
 * but it shows the filters as they look. The State Saver page has no such
 * import: web_gl_ok answers 0 there and the filters stay greyed. */
extern int web_gl_ok(void) __attribute__((import_module("env"), import_name("web_gl_ok")));
extern int web_gl_pass(int key, const char* vs0, const char* fs0, const char* vs1, const char* fs1, const char* uniforms,
                       const void* in, int inW, int inH, void* out, int outW, int outH)
    __attribute__((import_module("env"), import_name("web_gl_pass")));

static int webGl = -1;              /* web_gl_ok, asked once */
static char* webSrc[PROG_COUNT][5]; /* per program: vs0, fs0, vs1, fs1, uniforms */
static uint32_t* webBuf[MAX_PASSES + 1];
static size_t webBufN[MAX_PASSES + 1];
static SDL_Texture* webTex;
static SDL_Renderer* webTexRen;
static SDL_Texture* webRead; /* the source copied into a target, to read it back */
static int webTexW, webTexH, webReadW, webReadH;
static const uint32_t* webLast;
static int webLastW, webLastH;

static bool web_gl(void) {
	if (webGl < 0) {
		webGl = web_gl_ok() ? 1 : 0;
		fprintf(stderr, "upscale: %s\n", webGl ? "WebGL2 in the web player (each pass read back)" : "not available (no WebGL2 here)");
		(void) gpu_class;
	}
	return webGl > 0;
}

static char* cat2(const char* a, const char* b) {
	size_t la = strlen(a), lb = strlen(b);
	char* r = malloc(la + lb + 1);
	if (r) memcpy(r, a, la), memcpy(r + la, b, lb + 1);
	return r;
}

static bool web_prog(int p) {
	if (webSrc[p][0]) return true;
	const UpscalePack* pk = p >= PROG_PACK0 && p < PROG_COUNT ? &upscalePacks[p - PROG_PACK0] : NULL;
	const char* vsSrc = pk && pk->vs ? pk->vs : plainVs;
	const char* fsSrc = pk ? pk->fs : scale2xFs;
	char uni[512] = "";
	for (int i = 0; pk && i < pk->nUniforms; ++i) {
		size_t l = strlen(uni);
		snprintf(uni + l, sizeof(uni) - l, "%s=%g\n", pk->uniforms[i].name, (double) pk->uniforms[i].value);
	}
	webSrc[p][0] = cat2(vsHeader[0], vsSrc), webSrc[p][1] = cat2(fsHeader[0], fsSrc);
	/* (a shader with an output of its own, as xBR's in GLSL 3.00: WebGL2 refuses
	 * two outputs without locations, which a device's driver lets by, so the
	 * header's isn't added) */
	static const char* const fsHeaderOwnOut =
	    "#version 300 es\n"
	    "precision highp float;\n"
	    "#define varying in\n"
	    "#define texture2D texture\n";
	webSrc[p][2] = cat2(vsHeader[1], vsSrc), webSrc[p][3] = cat2(strstr(fsSrc, "\nout vec4 ") ? fsHeaderOwnOut : fsHeader[1], fsSrc);
	webSrc[p][4] = cat2(uni, "");
	for (int i = 0; i < 5; ++i) {
		if (!webSrc[p][i]) return false;
	}
	fprintf(stderr, "upscale: %s sent to WebGL2\n", prog_name(p));
	return true;
}

static uint32_t* web_buf(int i, int w, int h) {
	size_t n = (size_t) w * h;
	if (webBufN[i] < n) {
		free(webBuf[i]);
		webBuf[i] = malloc(n * 4);
		webBufN[i] = webBuf[i] ? n : 0;
	}
	return webBuf[i];
}

bool upscale_ready(SDL_Renderer* ren) {
	(void) ren;
	return web_gl();
}
int upscale_fit(SDL_Renderer* ren, int filter) {
	(void) ren;
	if (!upscale_factor(filter)) return UPSCALE_FITS;
	if (!web_gl()) return UPSCALE_NO_GL;
	return filters[filter].heavy ? UPSCALE_UNTRIED : UPSCALE_FITS;
}
bool upscale_filter_ready(SDL_Renderer* ren, int filter) {
	(void) ren;
	return upscale_factor(filter) && web_gl() && web_prog(filters[filter].prog);
}
SDL_Texture* upscale_run(SDL_Renderer* ren, SDL_Texture* src, int w, int h, int filter) {
	webLast = NULL;
	if (!upscale_filter_ready(ren, filter)) return NULL;
	int p = filters[filter].prog, scale = filters[filter].scale, passes = filters[filter].passes;
	/* src's pixels: copied into a target of its size and read back */
	if (!webRead || webReadW != w || webReadH != h) {
		if (webRead) SDL_DestroyTexture(webRead);
		webRead = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
		webReadW = w, webReadH = h;
	}
	uint32_t* in = web_buf(0, w, h);
	if (!webRead || !in) return NULL;
	SDL_Texture* was = SDL_GetRenderTarget(ren);
	SDL_Rect clip;
	bool clipped = SDL_RenderIsClipEnabled(ren);
	SDL_RenderGetClipRect(ren, &clip);
	SDL_RenderSetClipRect(ren, NULL);
	SDL_SetRenderTarget(ren, webRead);
	SDL_SetRenderDrawColor(ren, 0, 0, 0, 255);
	SDL_RenderClear(ren);
	SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
	SDL_RenderCopy(ren, src, NULL, NULL);
	int rd = SDL_RenderReadPixels(ren, NULL, SDL_PIXELFORMAT_ABGR8888, in, w * 4);
	SDL_SetRenderTarget(ren, was);
	if (clipped) SDL_RenderSetClipRect(ren, &clip);
	if (rd != 0) return NULL;
	int sw = w, sh = h;
	const uint32_t* cur = in;
	for (int i = 0; i < passes; ++i) {
		uint32_t* out = web_buf(i + 1, sw * scale, sh * scale);
		if (!out || !web_gl_pass(p, webSrc[p][0], webSrc[p][1], webSrc[p][2], webSrc[p][3], webSrc[p][4], cur, sw, sh, out, sw * scale, sh * scale)) {
			static bool said;
			if (!said) fprintf(stderr, "upscale: %s didn't run in WebGL2, drawing without the filter\n", prog_name(p));
			said = true;
			return NULL;
		}
		cur = out, sw *= scale, sh *= scale;
	}
	if (!webTex || webTexRen != ren || webTexW != sw || webTexH != sh) {
		if (webTex && webTexRen == ren) SDL_DestroyTexture(webTex);
		webTex = SDL_CreateTexture(ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STREAMING, sw, sh);
		webTexRen = ren, webTexW = sw, webTexH = sh;
		if (!webTex) return NULL;
		SDL_SetTextureBlendMode(webTex, SDL_BLENDMODE_NONE);
	}
	SDL_UpdateTexture(webTex, NULL, cur, sw * 4);
	webLast = cur, webLastW = sw, webLastH = sh;
	return webTex;
}
const uint32_t* upscale_pixels(int* w, int* h) {
	if (webLast) *w = webLastW, *h = webLastH;
	return webLast;
}
void upscale_forget(SDL_Renderer* ren) {
	if (webTexRen == ren) webTex = NULL, webTexRen = NULL;
	if (webRead) webRead = NULL; /* (destroyed with its renderer) */
}

#else

/* --- the OpenGL ES 2 entry points, from SDL_GL_GetProcAddress (no GL
 * headers or library at build time: SDL has loaded the driver) */
typedef unsigned int GLenum;
typedef unsigned int GLuint;
typedef int GLint;
typedef int GLsizei;
typedef unsigned char GLboolean;
typedef char GLchar;
typedef float GLfloat;
typedef ptrdiff_t GLsizeiptr;

#define GL_TRIANGLE_STRIP 0x0005
#define GL_RENDERER 0x1F01
#define GL_BLEND 0x0BE2
#define GL_SCISSOR_TEST 0x0C11
#define GL_VIEWPORT 0x0BA2
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_BINDING_2D 0x8069
#define GL_FLOAT 0x1406
#define GL_ARRAY_BUFFER 0x8892
#define GL_ARRAY_BUFFER_BINDING 0x8894
#define GL_STATIC_DRAW 0x88E4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_CURRENT_PROGRAM 0x8B8D
#define GL_FRAMEBUFFER 0x8D40
#define GL_FRAMEBUFFER_BINDING 0x8CA6
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5

#define GLFUNCS(X) \
	X(const unsigned char*, glGetString, (GLenum)) \
	X(void, glGetIntegerv, (GLenum, GLint*)) \
	X(GLboolean, glIsEnabled, (GLenum)) \
	X(void, glEnable, (GLenum)) \
	X(void, glDisable, (GLenum)) \
	X(void, glViewport, (GLint, GLint, GLsizei, GLsizei)) \
	X(GLuint, glCreateShader, (GLenum)) \
	X(void, glShaderSource, (GLuint, GLsizei, const GLchar* const*, const GLint*)) \
	X(void, glCompileShader, (GLuint)) \
	X(void, glGetShaderiv, (GLuint, GLenum, GLint*)) \
	X(void, glGetShaderInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
	X(void, glDeleteShader, (GLuint)) \
	X(GLuint, glCreateProgram, (void)) \
	X(void, glAttachShader, (GLuint, GLuint)) \
	X(void, glBindAttribLocation, (GLuint, GLuint, const GLchar*)) \
	X(void, glLinkProgram, (GLuint)) \
	X(void, glGetProgramiv, (GLuint, GLenum, GLint*)) \
	X(void, glGetProgramInfoLog, (GLuint, GLsizei, GLsizei*, GLchar*)) \
	X(void, glUseProgram, (GLuint)) \
	X(GLint, glGetUniformLocation, (GLuint, const GLchar*)) \
	X(void, glUniform1i, (GLint, GLint)) \
	X(void, glUniform1f, (GLint, GLfloat)) \
	X(void, glUniform2f, (GLint, GLfloat, GLfloat)) \
	X(void, glGenBuffers, (GLsizei, GLuint*)) \
	X(void, glBindBuffer, (GLenum, GLuint)) \
	X(void, glBufferData, (GLenum, GLsizeiptr, const void*, GLenum)) \
	X(void, glVertexAttribPointer, (GLuint, GLint, GLenum, GLboolean, GLsizei, const void*)) \
	X(void, glEnableVertexAttribArray, (GLuint)) \
	X(void, glGenFramebuffers, (GLsizei, GLuint*)) \
	X(void, glBindFramebuffer, (GLenum, GLuint)) \
	X(void, glFramebufferTexture2D, (GLenum, GLenum, GLenum, GLuint, GLint)) \
	X(GLenum, glCheckFramebufferStatus, (GLenum)) \
	X(void, glDrawArrays, (GLenum, GLint, GLsizei))

#define DECL(ret, name, args) ret(SDLCALL* name) args;
static struct {
	GLFUNCS(DECL)
} gl;
#undef DECL

static bool load_gl(void) {
#define LOAD(ret, name, args) \
	if (!(*(void**) &gl.name = SDL_GL_GetProcAddress(#name))) { \
		fprintf(stderr, "upscale: no %s\n", #name); \
		return false; \
	}
	GLFUNCS(LOAD)
#undef LOAD
	return true;
}

/* --- what's made per renderer (the game's renderer, and on BananaOS's two
 * windows the other's) */

typedef struct {
	bool tried, ok;
	GLuint prog;
	GLint locTexSize, locOutputSize;
} Prog;

typedef struct {
	SDL_Renderer* ren;
	bool tried, ok; /* the context: GL loaded, buffer and framebuffer made */
	char gpu[96];   /* GL_RENDERER */
	int gpuClass;   /* gpu_class */
	GLuint vbo, fbo;
	Prog progs[PROG_COUNT];
	SDL_Texture* step[MAX_PASSES];
	int stepW[MAX_PASSES], stepH[MAX_PASSES];
} Ctx;

static Ctx ctxs[4];
static bool glLoaded, glFailed;

static Ctx* ctx_for(SDL_Renderer* ren) {
	for (int i = 0; i < 4; ++i) {
		if (ctxs[i].ren == ren) return &ctxs[i];
	}
	for (int i = 0; i < 4; ++i) {
		if (!ctxs[i].ren) {
			memset(&ctxs[i], 0, sizeof(ctxs[i]));
			ctxs[i].ren = ren;
			return &ctxs[i];
		}
	}
	return NULL;
}

/* (quiet: the log is kept in *err, for when no version builds) */
static GLuint compile(GLenum type, const char* header, const char* src, char* err, size_t errSize) {
	GLuint sh = gl.glCreateShader(type);
	const char* parts[2] = { header, src };
	gl.glShaderSource(sh, 2, parts, NULL);
	gl.glCompileShader(sh);
	GLint ok = 0;
	gl.glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[768] = "";
		gl.glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		snprintf(err, errSize, "%s shader: %s", type == GL_VERTEX_SHADER ? "vertex" : "fragment", log);
		gl.glDeleteShader(sh);
		return 0;
	}
	return sh;
}

/* Saved and put back around our drawing: what SDL's GLES2 renderer assumes
 * is still as it left it. */
typedef struct {
	GLint prog, fbo, buf, viewport[4];
	GLboolean blend, scissor;
} GlState;

static void save_state(GlState* s) {
	gl.glGetIntegerv(GL_CURRENT_PROGRAM, &s->prog);
	gl.glGetIntegerv(GL_FRAMEBUFFER_BINDING, &s->fbo);
	gl.glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &s->buf);
	gl.glGetIntegerv(GL_VIEWPORT, s->viewport);
	s->blend = gl.glIsEnabled(GL_BLEND);
	s->scissor = gl.glIsEnabled(GL_SCISSOR_TEST);
}

static void restore_state(const GlState* s) {
	gl.glUseProgram((GLuint) s->prog);
	gl.glBindFramebuffer(GL_FRAMEBUFFER, (GLuint) s->fbo);
	gl.glBindBuffer(GL_ARRAY_BUFFER, (GLuint) s->buf);
	gl.glViewport(s->viewport[0], s->viewport[1], s->viewport[2], s->viewport[3]);
	if (s->blend) gl.glEnable(GL_BLEND);
	else gl.glDisable(GL_BLEND);
	if (s->scissor) gl.glEnable(GL_SCISSOR_TEST);
	else gl.glDisable(GL_SCISSOR_TEST);
}

/* the GL name of an SDL texture; leaves it bound (SDL knows) */
static GLuint gl_name(SDL_Texture* t) {
	if (SDL_GL_BindTexture(t, NULL, NULL) != 0) return 0;
	GLint id = 0;
	gl.glGetIntegerv(GL_TEXTURE_BINDING_2D, &id);
	return (GLuint) id;
}

/* Make ren's context current the way SDL does (through a texture of its own),
 * run fn, put SDL's state back. */
static bool with_context(Ctx* c, bool (*fn)(Ctx*, void*), void* arg) {
	SDL_RenderFlush(c->ren);
	SDL_Texture* probe = SDL_CreateTexture(c->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (!probe || SDL_GL_BindTexture(probe, NULL, NULL) != 0) {
		fprintf(stderr, "upscale: no GL context (%s)\n", SDL_GetError());
		if (probe) SDL_DestroyTexture(probe);
		return false;
	}
	GlState st;
	save_state(&st);
	bool ok = fn(c, arg);
	restore_state(&st);
	SDL_GL_UnbindTexture(probe);
	SDL_DestroyTexture(probe);
	return ok;
}

static bool make_context(Ctx* c, void* arg) {
	(void) arg;
	static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
	gl.glGenBuffers(1, &c->vbo);
	gl.glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
	gl.glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
	gl.glGenFramebuffers(1, &c->fbo);
	const unsigned char* g = gl.glGetString(GL_RENDERER);
	const char* as = getenv("GBADS_GPU"); /* (tests: as if the GPU were this one) */
	snprintf(c->gpu, sizeof(c->gpu), "%s", as && *as ? as : g ? (const char*) g : "");
	c->gpuClass = gpu_class(c->gpu);
	return c->vbo && c->fbo;
}

/* the program from one GLSL version's headers, or 0 (err says why) */
static GLuint build_prog(int v, const char* vsSrc, const char* fsSrc, char* err, size_t errSize) {
	GLuint vs = compile(GL_VERTEX_SHADER, vsHeader[v], vsSrc, err, errSize);
	GLuint fs = vs ? compile(GL_FRAGMENT_SHADER, fsHeader[v], fsSrc, err, errSize) : 0;
	GLuint prog = 0;
	if (vs && fs) {
		prog = gl.glCreateProgram();
		gl.glAttachShader(prog, vs);
		gl.glAttachShader(prog, fs);
		gl.glBindAttribLocation(prog, 0, "position"); /* SDL's position attribute, always enabled */
		gl.glLinkProgram(prog);
		GLint linked = 0;
		gl.glGetProgramiv(prog, GL_LINK_STATUS, &linked);
		if (!linked) {
			char log[768] = "";
			gl.glGetProgramInfoLog(prog, sizeof(log), NULL, log);
			snprintf(err, errSize, "link: %s", log);
			prog = 0;
		}
	}
	if (vs) gl.glDeleteShader(vs);
	if (fs) gl.glDeleteShader(fs);
	return prog;
}

static bool make_prog(Ctx* c, void* arg) {
	int p = *(int*) arg;
	Prog* pr = &c->progs[p];
	const UpscalePack* pk = p >= PROG_PACK0 && p < PROG_COUNT ? &upscalePacks[p - PROG_PACK0] : NULL;
	const char* vsSrc = pk && pk->vs ? pk->vs : plainVs;
	const char* fsSrc = pk ? pk->fs : scale2xFs;
	char err[2][800] = { "", "" };
	int v = 0;
	for (; v < 2 && !pr->prog; ++v) pr->prog = build_prog(v, vsSrc, fsSrc, err[v], sizeof(err[v]));
	if (!pr->prog) {
		fprintf(stderr, "upscale: %s not available (didn't build)\n  %s: %s\n  %s: %s\n", prog_name(p), glslName[0], err[0], glslName[1], err[1]);
		return false;
	}
	gl.glUseProgram(pr->prog);
	gl.glUniform1i(gl.glGetUniformLocation(pr->prog, "tex"), 0);
	pr->locTexSize = gl.glGetUniformLocation(pr->prog, "texSize");
	pr->locOutputSize = gl.glGetUniformLocation(pr->prog, "outputSize");
	for (int i = 0; pk && i < pk->nUniforms; ++i) {
		GLint loc = gl.glGetUniformLocation(pr->prog, pk->uniforms[i].name);
		if (loc >= 0) gl.glUniform1f(loc, pk->uniforms[i].value);
	}
	fprintf(stderr, "upscale: %s ready (%s)\n", prog_name(p), glslName[v - 1]);
	return true;
}

/* ren's context, made the first time (an OpenGL ES 2 renderer only) */
static Ctx* ready_ctx(SDL_Renderer* ren) {
	Ctx* c = ctx_for(ren);
	if (!c) return NULL;
	if (!c->tried) {
		c->tried = true;
		SDL_RendererInfo ri;
		if (SDL_GetRendererInfo(ren, &ri) != 0 || strcmp(ri.name, "opengles2")) {
			fprintf(stderr, "upscale: not available (renderer %s)\n", SDL_GetRendererInfo(ren, &ri) == 0 ? ri.name : "?");
		} else {
			if (!glLoaded && !glFailed) {
				glLoaded = load_gl();
				glFailed = !glLoaded;
			}
			c->ok = glLoaded && with_context(c, make_context, NULL);
			if (c->ok) {
				char heavy[96] = "";
				for (int f = 0; f < FILTER_COUNT; ++f) {
					if (filters[f].heavy) {
						size_t l = strlen(heavy);
						snprintf(heavy + l, sizeof(heavy) - l, "%s%s", l ? ", " : "", upscalePacks[filters[f].prog - PROG_PACK0].name);
					}
				}
				fprintf(stderr, "upscale: OpenGL ES 2, GPU %s: %s %s\n", c->gpu[0] ? c->gpu : "?", heavy,
				        c->gpuClass < 0 ? "too heavy here (greyed, drawn as Sharp)" : c->gpuClass > 0 ? "tried at full speed" : "allowed, not tried on this GPU");
			}
		}
	}
	return c->ok ? c : NULL;
}

bool upscale_ready(SDL_Renderer* ren) {
	return ready_ctx(ren) != NULL;
}

int upscale_fit(SDL_Renderer* ren, int filter) {
	if (!upscale_factor(filter)) return UPSCALE_FITS;
	Ctx* c = ren ? ready_ctx(ren) : NULL;
	if (!c) return UPSCALE_NO_GL;
	if (!filters[filter].heavy || c->gpuClass > 0) return UPSCALE_FITS;
	return c->gpuClass < 0 ? UPSCALE_TOO_HEAVY : UPSCALE_UNTRIED;
}

bool upscale_filter_ready(SDL_Renderer* ren, int filter) {
	if (!upscale_factor(filter)) return false;
	Ctx* c = ready_ctx(ren);
	if (!c || upscale_fit(ren, filter) == UPSCALE_TOO_HEAVY) return false;
	int p = filters[filter].prog;
	Prog* pr = &c->progs[p];
	if (!pr->tried) {
		pr->tried = true;
		pr->ok = with_context(c, make_prog, &p);
	}
	return pr->ok;
}

/* the i-th pass's target, made when its size changes */
static SDL_Texture* step_tex(Ctx* c, int i, int w, int h) {
	if (c->step[i] && c->stepW[i] == w && c->stepH[i] == h) return c->step[i];
	if (c->step[i]) SDL_DestroyTexture(c->step[i]);
	c->step[i] = SDL_CreateTexture(c->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_TARGET, w, h);
	c->stepW[i] = w, c->stepH[i] = h;
	if (c->step[i]) {
		SDL_SetTextureBlendMode(c->step[i], SDL_BLENDMODE_NONE);
		SDL_SetTextureScaleMode(c->step[i], SDL_ScaleModeNearest);
	}
	return c->step[i];
}

SDL_Texture* upscale_run(SDL_Renderer* ren, SDL_Texture* src, int w, int h, int filter) {
	if (!upscale_filter_ready(ren, filter)) return NULL;
	Ctx* c = ctx_for(ren);
	const Prog* pr = &c->progs[filters[filter].prog];
	int scale = filters[filter].scale, passes = filters[filter].passes;
	int tw, th;
	if (SDL_QueryTexture(src, NULL, NULL, &tw, &th) != 0 || tw != w || th != h) return NULL;
	SDL_Texture* out[MAX_PASSES] = { NULL };
	GLuint outName[MAX_PASSES] = { 0 };
	int ow = w, oh = h;
	for (int i = 0; i < passes; ++i) {
		ow *= scale, oh *= scale;
		if (!(out[i] = step_tex(c, i, ow, oh))) return NULL;
	}
	/* the targets' GL names (making the renderer's context current) */
	for (int i = 0; i < passes; ++i) {
		if (!(outName[i] = gl_name(out[i]))) return NULL;
	}
	SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
	SDL_RenderFlush(ren);
	if (!gl_name(src)) {
		SDL_GL_UnbindTexture(src);
		return NULL;
	}
	GlState st;
	save_state(&st);
	gl.glUseProgram(pr->prog);
	gl.glDisable(GL_BLEND);
	gl.glDisable(GL_SCISSOR_TEST);
	gl.glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
	gl.glEnableVertexAttribArray(0);
	gl.glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, NULL);
	gl.glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
	bool ok = true;
	int sw = w, sh = h;
	for (int i = 0; i < passes; ++i) {
		gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outName[i], 0);
		if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			ok = false;
			break;
		}
		/* (the source: the game's picture, then the last pass's result; bound
		 * on unit 0, which SDL leaves active) */
		SDL_GL_BindTexture(i == 0 ? src : out[i - 1], NULL, NULL);
		gl.glViewport(0, 0, sw * scale, sh * scale);
		if (pr->locTexSize >= 0) gl.glUniform2f(pr->locTexSize, (GLfloat) sw, (GLfloat) sh);
		if (pr->locOutputSize >= 0) gl.glUniform2f(pr->locOutputSize, (GLfloat) (sw * scale), (GLfloat) (sh * scale));
		gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		sw *= scale, sh *= scale;
	}
	restore_state(&st);
	SDL_GL_UnbindTexture(src); /* SDL binds its next texture again */
	if (!ok) {
		static bool said;
		if (!said) fprintf(stderr, "upscale: framebuffer incomplete, drawing without the filter\n");
		said = true;
		return NULL;
	}
	return out[passes - 1];
}

const uint32_t* upscale_pixels(int* w, int* h) {
	(void) w, (void) h;
	return NULL;
}

void upscale_forget(SDL_Renderer* ren) {
	for (int i = 0; i < 4; ++i) {
		if (ctxs[i].ren == ren) {
			/* (the textures and GL objects go with the renderer) */
			memset(&ctxs[i], 0, sizeof(ctxs[i]));
		}
	}
}

#endif

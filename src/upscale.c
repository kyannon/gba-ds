/* Upscaling filters on the GPU (issue #89); see upscale.h.
 *
 * SDL2's renderer has no way to run a shader of our own, so this draws with
 * OpenGL ES 2 directly, on the renderer's own context, between its draws:
 *   1. SDL_RenderFlush, so everything SDL queued is drawn first;
 *   2. SDL_GL_BindTexture for the GL names of SDL's textures (the game's
 *      picture, and our own render-target textures of 2x / 4x its size);
 *   3. our program draws a quad into our own framebuffer object, attached to
 *      the target texture (one pass a 2x step: Scale4x is two);
 *   4. every piece of state SDL's GLES2 renderer caches is put back: the
 *      program, the framebuffer, the viewport, blending and the scissor test
 *      (as they were), the texture (SDL_GL_UnbindTexture, which tells SDL it
 *      must bind its next one again) and the array buffer (SDL binds its own
 *      at every queue run, and sets every attribute's pointer at every draw).
 * The result is an ordinary SDL texture that SDL then draws to the screen.
 *
 * The scalers are written here (not mGBA's shader packs, which libmgba isn't
 * built with: scripts/mgba-options.sh). Scale2x is AdvanceMAME's algorithm
 * (Andrea Mazzoleni): each pixel E becomes four, each taking a neighbour's
 * colour where two neighbours meeting at that corner match and the edge
 * isn't straight. The pixels are read at their centres, nearest, so equal
 * colours compare exactly. */
#include "upscale.h"

#include <stdio.h>
#include <string.h>

int upscale_factor(int filter) {
	return filter == FILTER_SCALE2X ? 2 : filter == FILTER_SCALE4X ? 4 : 0;
}

#ifdef __wasi__

bool upscale_ready(SDL_Renderer* ren) {
	(void) ren;
	return false;
}
SDL_Texture* upscale_run(SDL_Renderer* ren, SDL_Texture* src, int w, int h, int filter) {
	(void) ren, (void) src, (void) w, (void) h, (void) filter;
	return NULL;
}
void upscale_forget(SDL_Renderer* ren) {
	(void) ren;
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

/* --- the shaders */

static const char* const vertexSrc =
	"attribute vec2 pos;\n"
	"void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n";

/* One Scale2x step: the target is twice the source each way. Rows are
 * counted the same way in both (row 0 at t = 0, as SDL uploads them), so
 * gl_FragCoord's row y comes from the source's row y / 2. */
static const char* const scale2xSrc =
	"#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
	"precision highp float;\n"
	"#else\n"
	"precision mediump float;\n"
	"#endif\n"
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

/* --- what's made per renderer (the game's renderer, and on BananaOS's two
 * windows the other's) */

typedef struct {
	SDL_Renderer* ren;
	bool tried, ok;
	GLuint prog, vbo, fbo;
	GLint locTexSize;
	SDL_Texture* step[2]; /* 2x, 4x */
	int stepW[2], stepH[2];
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

static GLuint compile(GLenum type, const char* src) {
	GLuint sh = gl.glCreateShader(type);
	gl.glShaderSource(sh, 1, &src, NULL);
	gl.glCompileShader(sh);
	GLint ok = 0;
	gl.glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[512] = "";
		gl.glGetShaderInfoLog(sh, sizeof(log), NULL, log);
		fprintf(stderr, "upscale: shader: %s\n", log);
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

static bool build(Ctx* c) {
	SDL_RendererInfo ri;
	if (SDL_GetRendererInfo(c->ren, &ri) != 0 || strcmp(ri.name, "opengles2")) {
		fprintf(stderr, "upscale: not available (renderer %s)\n", SDL_GetRendererInfo(c->ren, &ri) == 0 ? ri.name : "?");
		return false;
	}
	if (!glLoaded && !glFailed) {
		glLoaded = load_gl();
		glFailed = !glLoaded;
	}
	if (!glLoaded) return false;
	SDL_RenderFlush(c->ren);
	/* make the renderer's context current the way SDL does: through a texture */
	SDL_Texture* probe = SDL_CreateTexture(c->ren, SDL_PIXELFORMAT_ABGR8888, SDL_TEXTUREACCESS_STATIC, 1, 1);
	if (!probe || SDL_GL_BindTexture(probe, NULL, NULL) != 0) {
		fprintf(stderr, "upscale: no GL context (%s)\n", SDL_GetError());
		if (probe) SDL_DestroyTexture(probe);
		return false;
	}
	GlState st;
	save_state(&st);
	GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc), fs = compile(GL_FRAGMENT_SHADER, scale2xSrc);
	bool ok = false;
	if (vs && fs) {
		c->prog = gl.glCreateProgram();
		gl.glAttachShader(c->prog, vs);
		gl.glAttachShader(c->prog, fs);
		gl.glBindAttribLocation(c->prog, 0, "pos"); /* SDL's position attribute, always enabled */
		gl.glLinkProgram(c->prog);
		GLint linked = 0;
		gl.glGetProgramiv(c->prog, GL_LINK_STATUS, &linked);
		if (!linked) {
			char log[512] = "";
			gl.glGetProgramInfoLog(c->prog, sizeof(log), NULL, log);
			fprintf(stderr, "upscale: link: %s\n", log);
		} else {
			gl.glUseProgram(c->prog);
			gl.glUniform1i(gl.glGetUniformLocation(c->prog, "tex"), 0);
			c->locTexSize = gl.glGetUniformLocation(c->prog, "texSize");
			static const GLfloat quad[] = { -1, -1, 1, -1, -1, 1, 1, 1 };
			gl.glGenBuffers(1, &c->vbo);
			gl.glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
			gl.glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
			gl.glGenFramebuffers(1, &c->fbo);
			ok = true;
		}
	}
	if (vs) gl.glDeleteShader(vs);
	if (fs) gl.glDeleteShader(fs);
	restore_state(&st);
	SDL_GL_UnbindTexture(probe);
	SDL_DestroyTexture(probe);
	fprintf(stderr, "upscale: %s\n", ok ? "ready (OpenGL ES 2)" : "not available (shaders didn't build)");
	return ok;
}

bool upscale_ready(SDL_Renderer* ren) {
	Ctx* c = ctx_for(ren);
	if (!c) return false;
	if (!c->tried) {
		c->tried = true;
		c->ok = build(c);
	}
	return c->ok;
}

/* the i-th step's target (2x, 4x of w x h), made when its size changes */
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
	int factor = upscale_factor(filter);
	if (!factor || !upscale_ready(ren)) return NULL;
	Ctx* c = ctx_for(ren);
	int tw, th;
	if (SDL_QueryTexture(src, NULL, NULL, &tw, &th) != 0 || tw != w || th != h) return NULL;
	int steps = factor == 4 ? 2 : 1;
	SDL_Texture* out[2] = { step_tex(c, 0, w * 2, h * 2), steps > 1 ? step_tex(c, 1, w * 4, h * 4) : NULL };
	if (!out[0] || (steps > 1 && !out[1])) return NULL;
	/* the step textures' GL names (making the renderer's context current) */
	GLuint outName[2] = { gl_name(out[0]), steps > 1 ? gl_name(out[1]) : 0 };
	SDL_SetTextureScaleMode(src, SDL_ScaleModeNearest);
	SDL_RenderFlush(ren);
	GLuint srcName = gl_name(src);
	if (!srcName || !outName[0] || (steps > 1 && !outName[1])) {
		SDL_GL_UnbindTexture(src);
		return NULL;
	}
	GlState st;
	save_state(&st);
	gl.glUseProgram(c->prog);
	gl.glDisable(GL_BLEND);
	gl.glDisable(GL_SCISSOR_TEST);
	gl.glBindBuffer(GL_ARRAY_BUFFER, c->vbo);
	gl.glEnableVertexAttribArray(0);
	gl.glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, NULL);
	gl.glBindFramebuffer(GL_FRAMEBUFFER, c->fbo);
	bool ok = true;
	int sw = w, sh = h;
	for (int i = 0; i < steps && ok; ++i) {
		gl.glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outName[i], 0);
		if (gl.glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
			ok = false;
			break;
		}
		/* (the source: the game's picture, then the 2x step; bound on unit 0,
		 * which SDL leaves active) */
		SDL_GL_BindTexture(i == 0 ? src : out[0], NULL, NULL);
		gl.glViewport(0, 0, sw * 2, sh * 2);
		gl.glUniform2f(c->locTexSize, (GLfloat) sw, (GLfloat) sh);
		gl.glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		sw *= 2, sh *= 2;
	}
	restore_state(&st);
	SDL_GL_UnbindTexture(src); /* SDL binds its next texture again */
	if (!ok) {
		static bool said;
		if (!said) fprintf(stderr, "upscale: framebuffer incomplete, drawing without the filter\n");
		said = true;
		return NULL;
	}
	return out[steps - 1];
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

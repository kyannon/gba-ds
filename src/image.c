/* PNG/BMP/JPEG loading via stb_image (public domain); WebP via libwebp's
 * decoder (BSD, third_party/libwebp), told by its contents, not its name. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wall"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-but-set-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_BMP
#define STBI_ONLY_JPEG /* scraped logos (GEN-11) */
#define STBI_NO_STDIO_WARNINGS
#include "stb_image.h"
#pragma GCC diagnostic pop

#include <SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "src/webp/decode.h"

static bool is_webp(const unsigned char* h, size_t n) {
	return n >= 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WEBP", 4);
}

/* RGBA32 pixels (w x h, tightly packed) into a new surface. */
static SDL_Surface* surface_from(const unsigned char* px, int w, int h) {
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		for (int y = 0; y < h; ++y) memcpy((Uint8*) s->pixels + y * s->pitch, px + (size_t) y * w * 4, (size_t) w * 4);
	}
	return s;
}

/* A WebP image (a still one: an animation needs libwebp's demuxer). */
static SDL_Surface* webp_load(const unsigned char* data, size_t size) {
	int w, h;
	uint8_t* px = WebPDecodeRGBA(data, size, &w, &h);
	if (!px) return NULL;
	SDL_Surface* s = surface_from(px, w, h);
	WebPFree(px);
	return s;
}

/* The whole file, or NULL (*size set). */
static unsigned char* read_all(const char* path, size_t* size) {
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	unsigned char* b = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n > 0 && n < (64L << 20) && fseek(f, 0, SEEK_SET) == 0 && (b = malloc((size_t) n)) && fread(b, 1, (size_t) n, f) != (size_t) n) {
			free(b);
			b = NULL;
		}
		if (b) *size = (size_t) n;
	}
	fclose(f);
	return b;
}

/* Returns an RGBA32 surface (caller frees) or NULL. */
SDL_Surface* image_load(const char* path) {
	unsigned char head[12] = { 0 };
	size_t got = 0;
	FILE* f = fopen(path, "rb");
	if (!f) return NULL;
	got = fread(head, 1, sizeof(head), f);
	fclose(f);
	if (is_webp(head, got)) {
		size_t size = 0;
		unsigned char* b = read_all(path, &size);
		if (!b) return NULL;
		SDL_Surface* s = webp_load(b, size);
		free(b);
		return s;
	}
	int w, h, n;
	unsigned char* px = stbi_load(path, &w, &h, &n, 4);
	if (!px) return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		for (int y = 0; y < h; ++y) memcpy((Uint8*) s->pixels + y * s->pitch, px + y * w * 4, w * 4);
	}
	stbi_image_free(px);
	return s;
}

/* Why image_load(path) gave nothing, in a few words for the log: a format
 * it doesn't read (WebP, GIF...) whatever the file's name says, or stb's
 * reason. */
const char* image_why(const char* path) {
	unsigned char h[12] = { 0 };
	FILE* f = fopen(path, "rb");
	if (!f) return "can't be opened";
	size_t n = fread(h, 1, sizeof(h), f);
	fclose(f);
	if (is_webp(h, n)) return "a WebP image that couldn't be decoded (an animated one, or damaged)";
	if (n >= 6 && !memcmp(h, "GIF8", 4)) return "it's a GIF image (PNG, JPG, WebP or BMP only)";
	if (n >= 12 && !memcmp(h + 4, "ftyp", 4)) return "it's an AVIF / HEIF image (PNG, JPG, WebP or BMP only)";
	const char* why = stbi_failure_reason();
	return why ? why : "unreadable";
}

/* Same, from an image held in memory. */
SDL_Surface* image_load_mem(const unsigned char* data, size_t size) {
	if (is_webp(data, size)) return webp_load(data, size);
	int w, h, n;
	unsigned char* px = stbi_load_from_memory(data, (int) size, &w, &h, &n, 4);
	if (!px) return NULL;
	SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
	if (s) {
		for (int y = 0; y < h; ++y) memcpy((Uint8*) s->pixels + y * s->pitch, px + y * w * 4, w * 4);
	}
	stbi_image_free(px);
	return s;
}

/* PNG/BMP/JPEG loading via stb_image (public domain). */
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
#include <stdio.h>
#include <string.h>

/* Returns an RGBA32 surface (caller frees) or NULL. */
SDL_Surface* image_load(const char* path) {
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
	if (n >= 12 && !memcmp(h, "RIFF", 4) && !memcmp(h + 8, "WEBP", 4)) return "it's a WebP image (PNG, JPG or BMP only)";
	if (n >= 6 && !memcmp(h, "GIF8", 4)) return "it's a GIF image (PNG, JPG or BMP only)";
	if (n >= 12 && !memcmp(h + 4, "ftyp", 4)) return "it's an AVIF / HEIF image (PNG, JPG or BMP only)";
	const char* why = stbi_failure_reason();
	return why ? why : "unreadable";
}

/* Same, from a PNG held in memory. */
SDL_Surface* image_load_mem(const unsigned char* data, size_t size) {
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

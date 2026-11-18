/* write_png for the desktop test build, with stb_image_write (src/logo.c holds its code) instead of libpng. */
#include "emu.h"

#include <stdlib.h>

int stbi_write_png(char const* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

bool write_png(const char* path, const uint32_t* px, int w, int h, int stride) {
	uint8_t* rgb = malloc((size_t) w * (size_t) h * 3);
	if (!rgb) return false;
	for (int y = 0; y < h; ++y) {
		for (int x = 0; x < w; ++x) {
			uint32_t c = px[y * stride + x];
			uint8_t* o = rgb + ((size_t) y * (size_t) w + (size_t) x) * 3;
			o[0] = c & 0xFF;
			o[1] = (c >> 8) & 0xFF;
			o[2] = (c >> 16) & 0xFF;
		}
	}
	bool ok = stbi_write_png(path, w, h, 3, rgb, w * 3) != 0;
	free(rgb);
	return ok;
}

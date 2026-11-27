/* RGBA32 pictures without SDL: see rgba.h. */
#include "rgba.h"
#include "clamp.h"
#include <math.h>
#include <stddef.h>

void rgba_resample(const uint8_t* src, int sw, int sh, int spitch, uint8_t* dst, int w, int h, int dpitch) {
	for (int y = 0; y < h; ++y) {
		int sy0 = y * sh / h, sy1 = S_MAX(sy0 + 1, (y + 1) * sh / h);
		uint8_t* out = dst + (size_t) y * dpitch;
		for (int x = 0; x < w; ++x) {
			int sx0 = x * sw / w, sx1 = S_MAX(sx0 + 1, (x + 1) * sw / w);
			double r = 0, g = 0, b = 0, a = 0;
			int cnt = 0;
			for (int yy = sy0; yy < sy1; ++yy) {
				const uint8_t* p = src + (size_t) yy * spitch + (size_t) sx0 * 4;
				for (int xx = sx0; xx < sx1; ++xx, p += 4) {
					r += p[0] * p[3], g += p[1] * p[3], b += p[2] * p[3], a += p[3];
					++cnt;
				}
			}
			uint8_t* q = out + x * 4;
			if (a > 0) {
				q[0] = (uint8_t) lround(r / a), q[1] = (uint8_t) lround(g / a), q[2] = (uint8_t) lround(b / a);
			} else {
				q[0] = q[1] = q[2] = 0;
			}
			q[3] = (uint8_t) lround(a / cnt);
		}
	}
}

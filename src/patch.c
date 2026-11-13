/* Patch files beside a ROM (GEN-9): IPS, UPS, BPS and xdelta, applied in memory.
 * (tools/romhack.py has the same in Python.) A patch is a file a player
 * found somewhere, so every read and write is checked against the sizes. */
#include "patch.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define PATCH_MAX_ROM (32u * 1024 * 1024) /* the GBA's largest */

uint32_t doCrc32(const void* buf, size_t size); /* (libmgba's: its header wants zlib.h) */

bool patch_find(const char* romPath, char* out, size_t outSize) {
	static const char* const exts[] = { ".ips", ".ups", ".bps", ".xdelta", ".vcdiff",
		                                ".IPS", ".UPS", ".BPS", ".XDELTA", ".VCDIFF" };
	const char* slash = strrchr(romPath, '/');
	const char* dot = strrchr(romPath, '.');
	size_t stem = (dot && (!slash || dot > slash)) ? (size_t) (dot - romPath) : strlen(romPath);
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
		if (stem + strlen(exts[i]) + 1 > outSize) return false;
		memcpy(out, romPath, stem);
		strcpy(out + stem, exts[i]);
		struct stat st;
		if (stat(out, &st) == 0 && S_ISREG(st.st_mode)) return true;
	}
	return false;
}

const char* patch_name(const char* path) {
	const char* slash = strrchr(path, '/');
	return slash ? slash + 1 : path;
}

static uint32_t le32(const uint8_t* p) {
	return (uint32_t) p[0] | (uint32_t) p[1] << 8 | (uint32_t) p[2] << 16 | (uint32_t) p[3] << 24;
}

/* UPS / BPS numbers: 7 bits a byte, the high bit ending it */
static bool vlq(const uint8_t* p, size_t end, size_t* i, uint64_t* n) {
	uint64_t v = 0, shift = 1;
	for (int k = 0; k < 10; ++k) {
		if (*i >= end) return false;
		uint8_t x = p[(*i)++];
		v += (uint64_t) (x & 0x7F) * shift;
		if (x & 0x80) {
			*n = v;
			return true;
		}
		shift <<= 7;
		v += shift;
	}
	return false;
}

static PatchResult apply_ips(const uint8_t* p, size_t n, const uint8_t* rom, size_t romSize, uint8_t** out,
                             size_t* outSize) {
	/* the result's size: the ROM's, or more where a record writes past it,
	 * or the truncation extension's (3 bytes after "EOF") */
	size_t size = romSize, i = 5, trunc = 0;
	bool hasTrunc = false;
	for (;;) {
		if (i + 3 > n) return PATCH_UNREADABLE;
		if (!memcmp(p + i, "EOF", 3)) {
			if (i + 6 <= n) {
				trunc = (size_t) p[i + 3] << 16 | (size_t) p[i + 4] << 8 | p[i + 5];
				hasTrunc = true;
			}
			break;
		}
		if (i + 5 > n) return PATCH_UNREADABLE;
		size_t off = (size_t) p[i] << 16 | (size_t) p[i + 1] << 8 | p[i + 2];
		size_t len = (size_t) p[i + 3] << 8 | p[i + 4];
		i += 5;
		if (len == 0) {
			if (i + 3 > n) return PATCH_UNREADABLE;
			len = (size_t) p[i] << 8 | p[i + 1];
			i += 3;
		} else {
			if (i + len > n) return PATCH_UNREADABLE;
			i += len;
		}
		if (off + len > size) size = off + len;
	}
	if (hasTrunc) size = trunc;
	if (size == 0 || size > PATCH_MAX_ROM) return PATCH_UNREADABLE;
	uint8_t* o = calloc(1, size);
	if (!o) return PATCH_UNREADABLE;
	memcpy(o, rom, romSize < size ? romSize : size);
	i = 5;
	for (;;) {
		if (!memcmp(p + i, "EOF", 3)) break;
		size_t off = (size_t) p[i] << 16 | (size_t) p[i + 1] << 8 | p[i + 2];
		size_t len = (size_t) p[i + 3] << 8 | p[i + 4];
		i += 5;
		if (len == 0) { /* a run: its length, then the byte */
			size_t run = (size_t) p[i] << 8 | p[i + 1];
			for (size_t k = 0; k < run && off + k < size; ++k) o[off + k] = p[i + 2];
			i += 3;
		} else {
			for (size_t k = 0; k < len && off + k < size; ++k) o[off + k] = p[i + k];
			i += len;
		}
	}
	*out = o;
	*outSize = size;
	return PATCH_APPLIED;
}

static PatchResult apply_ups(const uint8_t* p, size_t n, const uint8_t* rom, size_t romSize, uint8_t** out,
                             size_t* outSize) {
	if (n < 4 + 12) return PATCH_UNREADABLE;
	size_t end = n - 12, i = 4;
	uint64_t srcSize, dstSize;
	if (!vlq(p, end, &i, &srcSize) || !vlq(p, end, &i, &dstSize)) return PATCH_UNREADABLE;
	if (doCrc32(p, n - 4) != le32(p + n - 4)) return PATCH_UNREADABLE;
	if (srcSize != romSize || doCrc32(rom, romSize) != le32(p + n - 12)) return PATCH_WRONG_ROM;
	if (dstSize == 0 || dstSize > PATCH_MAX_ROM) return PATCH_UNREADABLE;
	uint8_t* o = calloc(1, (size_t) dstSize);
	if (!o) return PATCH_UNREADABLE;
	memcpy(o, rom, romSize < dstSize ? romSize : (size_t) dstSize);
	uint64_t pos = 0;
	while (i < end) {
		uint64_t skip;
		if (!vlq(p, end, &i, &skip)) goto bad;
		pos += skip;
		for (;;) {
			if (i >= end) goto bad;
			uint8_t x = p[i++];
			if (pos < dstSize) o[pos] ^= x;
			++pos;
			if (!x) break;
		}
	}
	if (doCrc32(o, (size_t) dstSize) != le32(p + n - 8)) goto bad;
	*out = o;
	*outSize = (size_t) dstSize;
	return PATCH_APPLIED;
bad:
	free(o);
	return PATCH_UNREADABLE;
}

static PatchResult apply_bps(const uint8_t* p, size_t n, const uint8_t* rom, size_t romSize, uint8_t** out,
                             size_t* outSize) {
	if (n < 4 + 12) return PATCH_UNREADABLE;
	size_t end = n - 12, i = 4;
	uint64_t srcSize, dstSize, meta;
	if (!vlq(p, end, &i, &srcSize) || !vlq(p, end, &i, &dstSize) || !vlq(p, end, &i, &meta)) return PATCH_UNREADABLE;
	if (meta > end - i) return PATCH_UNREADABLE;
	i += (size_t) meta;
	if (doCrc32(p, n - 4) != le32(p + n - 4)) return PATCH_UNREADABLE;
	if (srcSize != romSize || doCrc32(rom, romSize) != le32(p + n - 12)) return PATCH_WRONG_ROM;
	if (dstSize == 0 || dstSize > PATCH_MAX_ROM) return PATCH_UNREADABLE;
	uint8_t* o = calloc(1, (size_t) dstSize);
	if (!o) return PATCH_UNREADABLE;
	uint64_t at = 0, srcRel = 0, dstRel = 0;
	while (i < end) {
		uint64_t d;
		if (!vlq(p, end, &i, &d)) goto bad;
		uint64_t cmd = d & 3, len = (d >> 2) + 1;
		if (len > dstSize - at) goto bad;
		if (cmd == 0) { /* SourceRead: the ROM's bytes where they are */
			if (at + len > romSize) goto bad;
			memcpy(o + at, rom + at, (size_t) len);
		} else if (cmd == 1) { /* TargetRead: the patch's bytes */
			if (len > end - i) goto bad;
			memcpy(o + at, p + i, (size_t) len);
			i += (size_t) len;
		} else {
			uint64_t r;
			if (!vlq(p, end, &i, &r)) goto bad;
			int64_t delta = (r & 1) ? -(int64_t) (r >> 1) : (int64_t) (r >> 1);
			if (cmd == 2) { /* SourceCopy: from elsewhere in the ROM */
				srcRel += (uint64_t) delta;
				if (srcRel > romSize || len > romSize - srcRel) goto bad;
				memcpy(o + at, rom + srcRel, (size_t) len);
				srcRel += len;
			} else { /* TargetCopy: from what's been written, a byte at a time (it may overlap) */
				dstRel += (uint64_t) delta;
				if (dstRel >= at) goto bad;
				for (uint64_t k = 0; k < len; ++k) o[at + k] = o[dstRel++];
			}
		}
		at += len;
	}
	if (at != dstSize || doCrc32(o, (size_t) dstSize) != le32(p + n - 8)) goto bad;
	*out = o;
	*outSize = (size_t) dstSize;
	return PATCH_APPLIED;
bad:
	free(o);
	return PATCH_UNREADABLE;
}

/* xdelta (VCDIFF, RFC 3284, as xdelta3 writes it): windows, each built
 * from a segment of the ROM (or of what's been written) and the patch's
 * own bytes, with the standard code table. xdelta3's extension: a window
 * may carry the Adler-32 of what it writes (Win_Indicator bit 0x04),
 * checked here. Not read: secondary compression (xdelta3 -S djw / lzma /
 * fgk) and custom code tables: such a file is unreadable here
 * (patch_xdelta_compressed says so). VCDIFF has no checksum of the ROM it
 * was made for: a ROM that isn't it shows as windows whose checksum
 * fails (made for another ROM) when they have one. */
#define VCD_SOURCE 0x01
#define VCD_TARGET 0x02
#define VCD_ADLER32 0x04

static bool vcd_int(const uint8_t* p, size_t end, size_t* i, uint64_t* n) {
	uint64_t v = 0;
	for (int k = 0; k < 9; ++k) {
		if (*i >= end) return false;
		uint8_t x = p[(*i)++];
		v = v << 7 | (x & 0x7F);
		if (!(x & 0x80)) {
			*n = v;
			return true;
		}
	}
	return false;
}

static uint32_t adler32(const uint8_t* p, size_t n) {
	uint32_t a = 1, b = 0;
	while (n) {
		size_t k = n < 5552 ? n : 5552; /* (the most before b can overflow) */
		n -= k;
		while (k--) {
			a += *p++;
			b += a;
		}
		a %= 65521;
		b %= 65521;
	}
	return b << 16 | a;
}

/* the default code table: each code's one or two instructions as
 * type (0 none, 1 ADD, 2 RUN, 3 COPY), size (0: read from the stream) and
 * mode */
typedef struct {
	uint8_t type[2], size[2], mode[2];
} VcdCode;

static void vcd_table(VcdCode* t) {
	memset(t, 0, 256 * sizeof(*t));
	int k = 0;
	t[k++] = (VcdCode) { { 2, 0 }, { 0, 0 }, { 0, 0 } };
	for (int s = 0; s <= 17; ++s) t[k++] = (VcdCode) { { 1, 0 }, { (uint8_t) s, 0 }, { 0, 0 } };
	for (int m = 0; m < 9; ++m) {
		t[k++] = (VcdCode) { { 3, 0 }, { 0, 0 }, { (uint8_t) m, 0 } };
		for (int s = 4; s <= 18; ++s) t[k++] = (VcdCode) { { 3, 0 }, { (uint8_t) s, 0 }, { (uint8_t) m, 0 } };
	}
	for (int m = 0; m < 6; ++m) {
		for (int a = 1; a <= 4; ++a) {
			for (int c = 4; c <= 6; ++c) t[k++] = (VcdCode) { { 1, 3 }, { (uint8_t) a, (uint8_t) c }, { 0, (uint8_t) m } };
		}
	}
	for (int m = 6; m < 9; ++m) {
		for (int a = 1; a <= 4; ++a) t[k++] = (VcdCode) { { 1, 3 }, { (uint8_t) a, 4 }, { 0, (uint8_t) m } };
	}
	for (int m = 0; m < 9; ++m) t[k++] = (VcdCode) { { 3, 1 }, { 4, 1 }, { (uint8_t) m, 0 } };
}

bool patch_xdelta_compressed(const uint8_t* patch, size_t patchSize) {
	return patchSize >= 5 && !memcmp(patch, "\xD6\xC3\xC4", 3) && (patch[4] & 0x03);
}

/* One pass over the windows: with o NULL it only adds up their sizes
 * (*total); with o it writes them (its size *total). */
static PatchResult vcd_windows(const uint8_t* p, size_t n, size_t i, const uint8_t* rom, size_t romSize, uint8_t* o,
                               uint64_t* total, const VcdCode* tbl) {
	uint64_t at = 0;
	while (i < n) {
		uint8_t win = p[i++];
		if (win & ~(VCD_SOURCE | VCD_TARGET | VCD_ADLER32) || (win & VCD_SOURCE && win & VCD_TARGET))
			return PATCH_UNREADABLE;
		uint64_t segLen = 0, segPos = 0, encLen, tLen, aLen, iLen, cLen;
		if (win & (VCD_SOURCE | VCD_TARGET)) {
			if (!vcd_int(p, n, &i, &segLen) || !vcd_int(p, n, &i, &segPos)) return PATCH_UNREADABLE;
		}
		if (!vcd_int(p, n, &i, &encLen) || encLen > n - i) return PATCH_UNREADABLE;
		size_t end = i + (size_t) encLen;
		if (!vcd_int(p, end, &i, &tLen) || i >= end) return PATCH_UNREADABLE;
		if (p[i++]) return PATCH_UNREADABLE; /* (compressed sections: secondary compression) */
		if (!vcd_int(p, end, &i, &aLen) || !vcd_int(p, end, &i, &iLen) || !vcd_int(p, end, &i, &cLen))
			return PATCH_UNREADABLE;
		uint32_t sum = 0;
		if (win & VCD_ADLER32) {
			if (end - i < 4) return PATCH_UNREADABLE;
			sum = (uint32_t) p[i] << 24 | (uint32_t) p[i + 1] << 16 | (uint32_t) p[i + 2] << 8 | p[i + 3];
			i += 4;
		}
		if (aLen > end - i || iLen > end - i - aLen || cLen != end - i - aLen - iLen) return PATCH_UNREADABLE;
		if (tLen > PATCH_MAX_ROM - at) return PATCH_UNREADABLE;
		if (win & VCD_SOURCE && (segPos > romSize || segLen > romSize - segPos)) return PATCH_WRONG_ROM;
		if (win & VCD_TARGET && (segPos > at || segLen > at - segPos)) return PATCH_UNREADABLE;
		if (!o) {
			at += tLen;
			i = end;
			continue;
		}
		const uint8_t* data = p + i;
		const uint8_t* inst = data + aLen;
		const uint8_t* addr = inst + iLen;
		const uint8_t* seg = win & VCD_SOURCE ? rom + segPos : o + segPos;
		uint8_t* t = o + at; /* this window's bytes */
		size_t di = 0, ii = 0, ai = 0;
		uint64_t w = 0, near[4] = { 0 }, same[768] = { 0 };
		int nextNear = 0;
		while (ii < iLen) {
			const VcdCode* c = &tbl[inst[ii++]];
			for (int h = 0; h < 2; ++h) {
				if (!c->type[h]) continue;
				uint64_t size = c->size[h];
				if (!size && !vcd_int(inst, iLen, &ii, &size)) return PATCH_UNREADABLE;
				if (size > tLen - w) return PATCH_UNREADABLE;
				if (c->type[h] == 1) { /* ADD: the patch's bytes */
					if (size > aLen - di) return PATCH_UNREADABLE;
					memcpy(t + w, data + di, (size_t) size);
					di += (size_t) size;
				} else if (c->type[h] == 2) { /* RUN: one byte, again and again */
					if (di >= aLen) return PATCH_UNREADABLE;
					memset(t + w, data[di++], (size_t) size);
				} else { /* COPY: from the segment, or from this window's own bytes */
					uint64_t here = segLen + w, a;
					int m = c->mode[h];
					if (m < 6) {
						uint64_t v;
						if (!vcd_int(addr, cLen, &ai, &v)) return PATCH_UNREADABLE;
						if (m == 0) a = v;
						else if (m == 1) {
							if (v > here) return PATCH_UNREADABLE;
							a = here - v;
						} else a = near[m - 2] + v;
					} else {
						if (ai >= cLen) return PATCH_UNREADABLE;
						a = same[(m - 6) * 256 + addr[ai++]];
					}
					if (a >= here) return PATCH_UNREADABLE;
					near[nextNear] = a;
					nextNear = (nextNear + 1) % 4;
					same[a % 768] = a;
					for (uint64_t k = 0; k < size; ++k) { /* (a byte at a time: it may overlap) */
						uint64_t q = a + k;
						t[w + k] = q < segLen ? seg[q] : t[q - segLen];
					}
				}
				w += size;
			}
		}
		if (w != tLen || di != aLen || ai != cLen) return PATCH_UNREADABLE;
		if (win & VCD_ADLER32 && adler32(t, (size_t) tLen) != sum)
			return win & VCD_SOURCE ? PATCH_WRONG_ROM : PATCH_UNREADABLE;
		at += tLen;
		i = end;
	}
	if (!o) *total = at;
	return at == *total ? PATCH_APPLIED : PATCH_UNREADABLE;
}

static PatchResult apply_vcdiff(const uint8_t* p, size_t n, const uint8_t* rom, size_t romSize, uint8_t** out,
                                size_t* outSize) {
	if (n < 5 || p[3] != 0 || patch_xdelta_compressed(p, n) || p[4] & ~0x07) return PATCH_UNREADABLE;
	size_t i = 5;
	if (p[4] & 0x04) { /* the application's header (xdelta3: the file names): skipped */
		uint64_t len;
		if (!vcd_int(p, n, &i, &len) || len > n - i) return PATCH_UNREADABLE;
		i += (size_t) len;
	}
	VcdCode tbl[256]; /* (made each time: patches are applied on more than one thread) */
	vcd_table(tbl);
	uint64_t total = 0;
	PatchResult r = vcd_windows(p, n, i, rom, romSize, NULL, &total, tbl);
	if (r != PATCH_APPLIED) return r;
	if (total == 0 || total > PATCH_MAX_ROM) return PATCH_UNREADABLE;
	uint8_t* o = calloc(1, (size_t) total);
	if (!o) return PATCH_UNREADABLE;
	r = vcd_windows(p, n, i, rom, romSize, o, &total, tbl);
	if (r != PATCH_APPLIED) {
		free(o);
		return r;
	}
	*out = o;
	*outSize = (size_t) total;
	return PATCH_APPLIED;
}

PatchResult patch_apply(const uint8_t* patch, size_t patchSize, const uint8_t* rom, size_t romSize, uint8_t** out,
                        size_t* outSize) {
	*out = NULL;
	*outSize = 0;
	if (!patch || !rom || !romSize) return PATCH_UNREADABLE;
	if (patchSize >= 8 && !memcmp(patch, "PATCH", 5)) return apply_ips(patch, patchSize, rom, romSize, out, outSize);
	if (patchSize >= 4 && !memcmp(patch, "UPS1", 4)) return apply_ups(patch, patchSize, rom, romSize, out, outSize);
	if (patchSize >= 4 && !memcmp(patch, "BPS1", 4)) return apply_bps(patch, patchSize, rom, romSize, out, outSize);
	if (patchSize >= 5 && !memcmp(patch, "\xD6\xC3\xC4", 3)) return apply_vcdiff(patch, patchSize, rom, romSize, out, outSize);
	return PATCH_UNREADABLE;
}

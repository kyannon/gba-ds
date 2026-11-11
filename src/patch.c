/* Patch files beside a ROM (GEN-9): IPS, UPS and BPS, applied in memory.
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
	static const char* const exts[] = { ".ips", ".ups", ".bps", ".IPS", ".UPS", ".BPS" };
	const char* slash = strrchr(romPath, '/');
	const char* dot = strrchr(romPath, '.');
	size_t stem = (dot && (!slash || dot > slash)) ? (size_t) (dot - romPath) : strlen(romPath);
	for (size_t i = 0; i < sizeof(exts) / sizeof(exts[0]); ++i) {
		if (stem + 5 > outSize) return false;
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

PatchResult patch_apply(const uint8_t* patch, size_t patchSize, const uint8_t* rom, size_t romSize, uint8_t** out,
                        size_t* outSize) {
	*out = NULL;
	*outSize = 0;
	if (!patch || !rom || !romSize) return PATCH_UNREADABLE;
	if (patchSize >= 8 && !memcmp(patch, "PATCH", 5)) return apply_ips(patch, patchSize, rom, romSize, out, outSize);
	if (patchSize >= 4 && !memcmp(patch, "UPS1", 4)) return apply_ups(patch, patchSize, rom, romSize, out, outSize);
	if (patchSize >= 4 && !memcmp(patch, "BPS1", 4)) return apply_bps(patch, patchSize, rom, romSize, out, outSize);
	return PATCH_UNREADABLE;
}

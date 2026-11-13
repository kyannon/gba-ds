/* Patch files beside a ROM (GEN-9): IPS, UPS, BPS and xdelta, applied in memory. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	PATCH_NONE, /* no patch file beside the ROM */
	PATCH_APPLIED,
	PATCH_WRONG_ROM, /* a UPS / BPS made for another ROM (its CRC32 isn't this one's); an
	                  * xdelta whose ROM isn't big enough, or whose checksums fail where it
	                  * copies from the ROM */
	PATCH_UNREADABLE, /* not a patch, damaged, or its result doesn't match its own checksum */
	PATCH_PARTIAL, /* (several) some applied, one or more not: made for another ROM or unreadable */
} PatchResult;

/* The patch file beside a ROM: the ROM's path with its extension (.gba,
 * .agb, .bin, .zip, .7z) changed to .ips, .ups, .bps, .xdelta or .vcdiff
 * (either case), the first found. */
bool patch_find(const char* romPath, char* out, size_t outSize);

/* Applies a patch (its file's bytes) to a ROM; *out is malloc'd. */
PatchResult patch_apply(const uint8_t* patch, size_t patchSize, const uint8_t* rom, size_t romSize, uint8_t** out,
                        size_t* outSize);

/* An xdelta made with compression (xdelta3 -S) or its own code table:
 * unreadable here, said in the Patches menu's note. */
bool patch_xdelta_compressed(const uint8_t* patch, size_t patchSize);

/* A patch's file name without its folder, for messages. */
const char* patch_name(const char* path);

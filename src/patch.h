/* Patch files beside a ROM (GEN-9): IPS, UPS and BPS, applied in memory. */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
	PATCH_NONE, /* no patch file beside the ROM */
	PATCH_APPLIED,
	PATCH_WRONG_ROM, /* a UPS / BPS made for another ROM (its CRC32 isn't this one's) */
	PATCH_UNREADABLE, /* not a patch, damaged, or its result doesn't match its own checksum */
	PATCH_PARTIAL, /* (several) some applied, one or more not: made for another ROM or unreadable */
} PatchResult;

/* The patch file beside a ROM: the ROM's path with its extension (.gba,
 * .agb, .bin, .zip, .7z) changed to .ips, .ups or .bps (either case), the
 * first found. */
bool patch_find(const char* romPath, char* out, size_t outSize);

/* Applies a patch (its file's bytes) to a ROM; *out is malloc'd. */
PatchResult patch_apply(const uint8_t* patch, size_t patchSize, const uint8_t* rom, size_t romSize, uint8_t** out,
                        size_t* outSize);

/* A patch's file name without its folder, for messages. */
const char* patch_name(const char* path);

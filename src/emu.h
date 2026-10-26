/* Thin wrapper around a libmgba core. */
#ifndef GBADS_EMU_H
#define GBADS_EMU_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define GBA_W 240
#define GBA_H 160

/* GBA key bits as used by mCore::setKeys */
enum {
	K_A = 1 << 0,
	K_B = 1 << 1,
	K_SELECT = 1 << 2,
	K_START = 1 << 3,
	K_RIGHT = 1 << 4,
	K_LEFT = 1 << 5,
	K_UP = 1 << 6,
	K_DOWN = 1 << 7,
	K_R = 1 << 8,
	K_L = 1 << 9,
};

struct mCore;

/* A sprite to leave out when drawing (e.g. a HUD mini-map): matches when its
 * top-left corner is inside x0..x1 / y0..y1 (y may be negative), its tile
 * number is within tileMin..tileMax and its priority equals prio. */
typedef struct SpriteRule {
	int x0, y0, x1, y1;
	int tileMin, tileMax;
	int prio; /* -1: any */
	unsigned palMask; /* the palettes it applies to (bit n: palette n); 0: any */
	unsigned tagged; /* only sprites (not rotated ones) with one of these
	                  * attr1 bits (9-11, unused there) set: tags a ROM patch
	                  * adds (profiles.h romPatch) */
} SpriteRule;

typedef struct Emu {
	struct mCore* core;
	uint32_t* fb; /* GBA_W * GBA_H, mGBA native 32-bit color (XBGR8888, i.e. bytes R,G,B,X) */
	char gameCode[5];
	bool audio;
} Emu;

/* savePath may be NULL (no battery save attached). */
bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio);
/* ROMs may also be in a .zip or .7z (the first GBA ROM inside is used). */
bool emu_is_rom_name(const char* name);   /* .gba, .zip, .7z */
bool emu_is_archive_name(const char* name); /* .zip, .7z */
/* Reads the first n bytes of the ROM header (n <= 0xC0), from inside an
 * archive if need be. */
bool emu_rom_header(const char* path, uint8_t* out, size_t n);
void emu_close(Emu* e);
void emu_frame(Emu* e, uint32_t keys);
void emu_reset(Emu* e);

size_t emu_state_size(Emu* e);
bool emu_state_save(Emu* e, void* buf);
bool emu_state_load(Emu* e, const void* buf);
/* Rewind (mGBA's, one buffer of `entries` states for one core): append a
 * state now and then while playing; each step goes back one of them
 * (false when there are none left). */
void emu_rewind_init(Emu* e, size_t entries);
void emu_rewind_deinit(void);
void emu_rewind_append(Emu* e);
bool emu_rewind_step(Emu* e);
size_t emu_rewind_count(void);

/* Persist full savestates (including battery data) to disk. */
bool emu_state_save_file(Emu* e, const char* path);
bool emu_state_load_file(Emu* e, const char* path);

/* The whole ROM file (inside a .zip / .7z too), malloc'd; NULL on failure. */
uint8_t* emu_rom_read_all(const char* path, size_t* size);
/* A memory region of the running game (GBA address >> 24: 2 EWRAM, 3 IWRAM,
 * 0xE save RAM), or NULL. */
uint8_t* emu_memory(Emu* e, int region, size_t* size);
uint8_t emu_read8(Emu* e, uint32_t addr);
uint16_t emu_read16(Emu* e, uint32_t addr);
uint32_t emu_read32(Emu* e, uint32_t addr);
void emu_write8(Emu* e, uint32_t addr, uint8_t value);
void emu_write16(Emu* e, uint32_t addr, uint16_t value);

/* Pull up to maxFrames stereo frames of audio; returns frames read. */
size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames);
unsigned emu_audio_rate(Emu* e);

/* tools/png.c (host tools only) */
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

/* Hide sprites matching any of the n rules (n = 0 turns it off). Display only:
 * the game's memory and state are untouched. One emulator at a time. */
void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n);
/* Which layers are drawn: bits 0-3 BG0-3, bit 4 sprites (0x1F = all). Takes
 * effect when the display registers are next written (e.g. a state load). */
void emu_layers(Emu* e, unsigned mask);
/* HUD split (Mario Kart): while on, the BG layers in bgMask (bit 0 = BG0)
 * and the sprites matching any of objs are left out of the game picture and
 * drawn alone into hud (GBA_W x GBA_H, kept by the caller while the core is
 * open; emu_hud_clear where there's nothing). Can be called again while on
 * to change what moves. Renders nothing extra while off. */
bool emu_hud_split(Emu* e, bool on, unsigned bgMask, const SpriteRule* objs, int nObjs, uint32_t* hud);
/* The HUD picture's 'no pixel here' colour (the parts of the screen the
 * HUD doesn't cover), low 24 bits: compare with (pixel & 0xFFFFFF). */
uint32_t emu_hud_clear(void);
/* While split: render the next frame's BG layers in bgMask (no sprites)
 * into buf instead of the HUD (the HUD buffer keeps the last frame).
 * emu_hud_grabbed says, once, that it's done. */
void emu_hud_grab(Emu* e, unsigned bgMask, uint32_t* buf);
/* the same, the BG layer whose BGxCNT is cnt as that frame starts */
void emu_hud_grab_cnt(Emu* e, uint16_t cnt, uint32_t* buf);
/* While split: the HUD is the BG layers at a priority below p as each frame
 * is drawn, instead of bgMask (0: bgMask again; -1: no layer). */
void emu_hud_by_priority(int prioBelow);
/* While split by priority: these BG layers (bit 0 = BG0) stay out of the
 * HUD whatever their priority (from the next frame). */
void emu_hud_keep_bgs(unsigned mask);
/* While split: both renderers see these bits of the video register at
 * addr (offset from 0x04000000) cleared, the game's register untouched
 * (Advance Wars: WININ's window 0 effects, the darkened box behind the
 * terrain info, which moves to the second screen). bits 0: none. */
void emu_hud_game_reg(uint32_t addr, uint16_t bits);
/* While split: on the lines where sprites are off (and on elsewhere: a box
 * across part of the screen, Advance Wars' conversations), the BG layers
 * set up differently from the other lines (BGxCNT) go to the HUD too;
 * with copy, they stay in the game picture as well; with watch, nothing
 * moves, the band's lines are only found (emu_hud_band_rows). Kept across
 * splits. */
void emu_hud_band(bool on, bool copy, bool watch);
/* While the HUD split's renderer is installed: each line's BGxHOFS / VOFS
 * (hofsReg: the I/O offset of HOFS; -1 stops) as it was drawn, and whether
 * the screen was brightened or darkened there (Pokemon Pinball's table
 * rows: emu_line_regs after each frame). */
void emu_line_regs_watch(int hofsReg);
/* While split: the HUD's BG layers and sprites also stay in the game picture (copy) or
 * leave it again; the HUD picture is drawn either way. */
void emu_hud_copy(Emu* e, bool copy);
void emu_line_regs(uint16_t* hofs, uint16_t* vofs, bool* fx);
/* The lines the band was on in the last frame drawn split; false if none. */
bool emu_hud_band_rows(int* y0, int* y1);
/* An I/O register as last written (also the write-only ones: MOSAIC). */
uint16_t emu_io16(Emu* e, uint32_t offset);
/* VRAM (96 KB), for reading tile maps. */
const uint16_t* emu_vram(Emu* e);
bool emu_hud_grabbed(void);

/* Cheats. emu_cheats_clear removes them all; emu_cheats_add adds one set
 * (applied every frame) from codes in RetroArch's .cht form: hex words
 * joined with '+', paired up into "XXXXXXXX YYYY" (Code Breaker) or
 * "XXXXXXXX YYYYYYYY" (GameShark / Action Replay) lines. type: 0 = detect,
 * else EMU_CHEAT_*. Returns the number of lines mGBA accepted. */
enum { EMU_CHEAT_DETECT = 0, EMU_CHEAT_CODEBREAKER, EMU_CHEAT_GAMESHARK, EMU_CHEAT_ACTION_REPLAY };
void emu_cheats_clear(Emu* e);
int emu_cheats_add(Emu* e, const char* name, const char* const* codes, int n, int type);

#endif

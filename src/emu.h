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

/* the sunlight a cartridge's sun sensor reads (0-10; Boktai) */
extern int emu_solar_level;

/* The player's own GBA BIOS (issue #78; off unless Settings > System turns it on): the file's
 * path, or "" to run games with mGBA's built-in one, as always. emu_open gives it to every core. */
extern char emu_bios_path[512];
typedef enum { BIOS_FILE_OK, BIOS_FILE_MISSING, BIOS_FILE_SIZE, BIOS_FILE_NOT_BIOS, BIOS_FILE_BROKEN } BiosFile;
BiosFile emu_bios_check(const char* path);

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
	unsigned shapes; /* the shapes and sizes it applies to (bit shape * 4 +
	                  * size, as in the sprite's attributes: bit 2 is 32x32,
	                  * bit 8 8x16); 0: any (Lady Sia: its HUD pieces, not
	                  * the level's things that pass the same places) */
} SpriteRule;

typedef struct Emu {
	struct mCore* core;
	uint32_t* fb; /* GBA_W * GBA_H, mGBA native 32-bit color (XBGR8888, i.e. bytes R,G,B,X) */
	/* The core's platform (mPlatform: mPLATFORM_GBA, mPLATFORM_GB). A Game Boy / Game Boy Color game
	 * (issue #77) draws its 160x144 picture centred in the same GBA_W x GBA_H frame, at (picX, picY),
	 * so the rest of the app sees a GBA-sized picture; for a GBA game it is the whole frame. */
	bool gbBorder;    /* running as a Super Game Boy with its border (GB-12): the core draws the 256x224 picture into sgbFb */
	uint8_t* gbBorderDefault; /* mGBA's own frame's memory as the core starts it: the border is black while the game has not sent its own */
	bool gbBorderOwn;         /* the game has sent its own border */
	uint32_t* sgbFb;  /* SGB_W x SGB_H, the border with the game's window in it (fb has the window alone, as ever) */
	bool gbSgbHeader; /* the cartridge supports the Super Game Boy (0x146 = 3, not Game Boy Color alone) */
	bool gbSgb; /* a Game Boy game running as a Super Game Boy (GB-11): it supports one and its colours are the SGB's */
	int gbKind; /* a Game Boy game: 0 made for the plain Game Boy, 1 for either (dual-mode), 2 for the Game Boy Color alone */
	int platform;
	int picX, picY, picW, picH;
	char gameCode[5];
	uint32_t romCrc; /* the ROM's CRC32, as loaded, patched if it was (profile_dump) */
	int patch; /* PatchResult (patch.h): a patch file beside the ROM, and whether it was applied (GEN-9) */
	char patchName[256]; /* the applied patches' file names, " + " between (or the one that wasn't) */
	char patchSkipped[256]; /* (PATCH_PARTIAL) the ones left out, " + " between */
	uint32_t fileCrc; /* the ROM file's own CRC32, before the patch */
	bool audio;
	void* rumble; /* the live game's rumble (emu_rumble_level), NULL for the hidden copies */
	void* gyro; /* the cartridge's gyro source (emu_set_gyro): every GBA core has one, resting until it is given a value */
	unsigned long frames; /* frames run (emu_frame), for timing the captures */
	/* a hidden copy's capture called off (mapcap_cancel): while it's set,
	 * frames don't run, so whatever the capture waits for gives up at
	 * once; emu_cancelled says so, so it keeps nothing of that run */
	volatile bool* cancel;
} Emu;

/* savePath may be NULL (no battery save attached). */
bool emu_open(Emu* e, const char* romPath, const char* savePath, bool audio);
/* While on, this thread's emu_open loads the ROM file as it is: no patch
 * beside it, none planned (the logo of a game whose logos are per ROM file
 * is its unpatched file's). */
void emu_open_unpatched(bool on);
/* ROMs may also be in a .zip or .7z (the first GBA ROM inside is used). */
bool emu_is_rom_name(const char* name);   /* .gba, .gb, .gbc (.sgb .dmg .cgb), .zip, .7z */
bool emu_is_archive_name(const char* name); /* .zip, .7z */
/* Reads the first n bytes of the ROM header (n <= EMU_HEADER_MAX: 0x150 reaches the end of a Game
 * Boy header), from inside an archive if need be. */
#define EMU_HEADER_MAX 0x150
bool emu_rom_header(const char* path, uint8_t* out, size_t n);
/* The game's 4-letter code from a header read with emu_rom_header (n >= EMU_HEADER_MAX): a GBA game's
 * own; a Game Boy / Game Boy Color game's made from its header ("g" or "c" + 3 characters, lower case:
 * issue #77). Its title (up to 15 characters, unprintable ones "_"). */
/* Is the game a Game Boy / Game Boy Color one (issue #77)? */
bool emu_is_gb(const Emu* e);
int emu_gb_kind(const Emu* e); /* Emu.gbKind */
bool emu_gb_sgb(const Emu* e);  /* Emu.gbSgb */
bool emu_gb_sgb_header(const Emu* e); /* Emu.gbSgbHeader */
/* the Super Game Boy's picture (GB-12): 256x224, the game's 160x144 window at (48, 40) */
#define SGB_W 256
#define SGB_H 224
#define SGB_WIN_X 48
#define SGB_WIN_Y 40
void emu_gb_border_wanted(bool on); /* before emu_open: Display > Game Boy border = Super Game Boy */
bool emu_gb_border(const Emu* e);   /* the game runs with its border (Emu.gbBorder) */
const uint32_t* emu_gb_border_px(const Emu* e); /* SGB_W x SGB_H, or NULL */
bool emu_rom_code(const uint8_t* h, size_t n, char code[5]);
/* emu_rom_code for the ROM file at path; a GBA header with no code (4 zero bytes) gets the code
 * its dump is filed under, if the whole file's CRC32 is a known one (profile_codeless) */
bool emu_rom_file_code(const char* path, char code[5]);
void emu_rom_title(const uint8_t* h, size_t n, char title[17]);
void emu_close(Emu* e);
void emu_frame(Emu* e, uint32_t keys);

/* Rumble (issue #83): how hard the game is asking its motor to run right now, 0 to 1
 * (the share of the last frame it was on). Cartridge rumble (Drill Dozer, WarioWare
 * Twisted, and romhacks that use the same pin) lands here; Game Boy Player rumble
 * (Mario & Luigi, Pokemon Pinball R&S, SMA4) doesn't reach it yet, see
 * docs/TESTING.md. Only the live game (emu_open with audio) has it, 0 otherwise.
 * May be read from another thread than the one running frames. */
float emu_rumble_level(const Emu* e);
/* The same without drawing it (e->fb keeps the last picture): for waits on
 * a hidden copy. */
void emu_frame_blind(Emu* e, uint32_t keys);
/* the cartridge has a sun sensor (Boktai) */
/* background work on the caller's thread (map captures, logo extraction, the
 * profiles' workers, the climb copy's pictures waited for): exact runs.
 * Off unless set (or GBADS_SYNC=1 at the start). See emu.c. */
void emu_set_sync_work(bool on);
bool emu_sync_work(void);
bool emu_has_solar(Emu* e);
/* The cartridge's gyro reading for the frames to come, in mGBA's units (WarioWare: Twisted!):
 * 0 is at rest, 2^21 a count of the cartridge's 12-bit sample. Only that game reads it. */
void emu_set_gyro(Emu* e, int32_t value);
/* true while this core's capture has been called off (Emu.cancel) */
bool emu_cancelled(const Emu* e);
void emu_reset(Emu* e);

size_t emu_state_size(Emu* e);
/* a word of EWRAM or IWRAM in a saved state set to `word`, only if it holds `orig` */
bool emu_state_patch32(Emu* e, void* state, uint32_t addr, uint32_t orig, uint32_t word);
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

/* Full savestates (including battery data) on disk. Loading (and the
 * functions below) take GBA DS's / mGBA's own files and RetroArch's (and
 * BananaFront's: RASTATE, compressed or not) with its mGBA core's state. */
typedef enum {
	STATE_FORMAT_MGBA,           /* mGBA's own (GBA DS's states) */
	STATE_FORMAT_RETROARCH,      /* RetroArch's, uncompressed */
	STATE_FORMAT_RETROARCH_RZIP, /* RetroArch's, compressed (its savestate_file_compression) */
} StateFormat;
typedef enum {
	STATE_FILE_NONE,       /* no such file */
	STATE_FILE_MGBA,       /* mGBA's own */
	STATE_FILE_RETROARCH,  /* RetroArch's, with its mGBA core's state */
	STATE_FILE_OTHER_CORE, /* another emulator's (RetroArch's gpSP, VBA-M...): not loadable */
	STATE_FILE_UNREADABLE, /* compressed in a way GBA DS can't read, or damaged */
} StateFileKind;
bool emu_state_save_file(Emu* e, const char* path); /* (mGBA's format, nothing of GBA DS's own) */
/* extra: GBA DS's own data kept inside it (mGBA's extra data, a tag of
 * its own: other emulators skip it), or NULL */
bool emu_state_save_file_as(Emu* e, const char* path, const void* extra, size_t extraN, StateFormat fmt);
bool emu_state_load_file(Emu* e, const char* path);
/* what's in a state file; *fromGbads (may be NULL): it has GBA DS's own data (saved by GBA DS) */
StateFileKind emu_state_file_kind(Emu* e, const char* path, bool* fromGbads);
/* Why mGBA refuses a state of the GBA BIOS's kind (issue #78): a state saved while the
 * CPU was inside the real GBA BIOS (the waits for the next frame, the interrupt
 * handler) can't go on in a core without that BIOS, nor a state saved without it in
 * a core that has it. Called after emu_state_load_file failed; BIOS_REFUSAL_NONE
 * when that isn't why (nothing else of mGBA's refusals is told apart). */
typedef enum {
	BIOS_REFUSAL_NONE,
	BIOS_REFUSAL_STATE_HAS_BIOS, /* saved with the official BIOS, this core runs without it */
	BIOS_REFUSAL_CORE_HAS_BIOS,  /* saved without it, this core runs the official one */
} BiosRefusal;
BiosRefusal emu_state_file_bios_refusal(Emu* e, const char* path);
/* The CRC32 of the ROM a state file was saved with (mGBA keeps it in the
 * state's header); false if it isn't a GBA state */
bool emu_state_file_rom_crc(const char* path, uint32_t* crc);
/* GBA DS's own data kept inside a state file, as a malloc'd copy (NULL: none) */
void* emu_state_file_get_extra(Emu* e, const char* path, size_t* n);
/* The same in memory (a libretro core's retro_serialize / retro_unserialize):
 * the bytes of a state file in mGBA's format (STATE_FORMAT_MGBA, extra kept
 * inside as there), malloc'd, *n bytes, NULL on failure; loading them, as
 * emu_state_load_file loads a file (its battery save left as it is); and the
 * extra data inside such bytes, malloc'd (NULL: none). */
uint8_t* emu_state_save_mem(Emu* e, const void* extra, size_t extraN, size_t* n);
bool emu_state_load_mem(Emu* e, const void* b, size_t n);
void* emu_state_mem_get_extra(Emu* e, const void* b, size_t bn, size_t* n);

/* The whole ROM file (inside a .zip / .7z too), malloc'd; NULL on failure. */
uint8_t* emu_rom_read_all(const char* path, size_t* size);
/* The ROM as it's played: patched when a patch file beside it fits (GEN-9).
 * *patch: PatchResult; patchName (if not NULL, 256 bytes): the patch file's
 * name; *fileCrc (if not NULL): the ROM file's CRC32 before any patch.
 * malloc'd, NULL if the ROM can't be read. */
uint8_t* emu_rom_read_played(const char* path, size_t* size, int* patch, char* patchName, uint32_t* fileCrc);
/* The patches a game is played with (GEN-9), in order: whatever opens that
 * ROM from now on gets them; a NULL rom or no files: none planned (then the
 * patch file beside a ROM, if any). */
/* A Game Boy game's colours (GB-6, GB-7), set before emu_open: colors is mGBA's gb.colors
 * (0 none, 1 the Game Boy Color's palette for the game, 3 the Super Game Boy's else that);
 * rgb: four 0xRRGGBB shades for the whole picture (NULL: not used); mono: a dual-mode game
 * as on a plain Game Boy. The defaults are GB-1's. */
void emu_gb_colours(int colors, const uint32_t* rgb, bool mono);
void emu_patch_plan(const char* rom, char files[][1024], int n);
/* The ROM with the patches given applied, in order (the game list's
 * checksum of each game as it would be played) */
uint8_t* emu_rom_read_patched(const char* path, char files[][1024], int n, size_t* size, int* patch, char* patchName);
/* The battery save in memory (a libretro core's RETRO_MEMORY_SAVE_RAM, as
 * mGBA's libretro core keeps it): the game opened with no savePath, then
 * before its first frame given buf (size bytes: 128 KB, GBA_SIZE_FLASH1M, set
 * to 0xFF and filled from the frontend's save), which the game reads and
 * writes in place. emu_save_memory_size: how much of it is the save (all of
 * it until the game's save type is known), as that core reports it. */
bool emu_save_memory(Emu* e, void* buf, size_t size);
size_t emu_save_memory_size(Emu* e);
/* A memory region of the running game (GBA address >> 24: 2 EWRAM, 3 IWRAM,
 * 0xE save RAM), or NULL. */
uint8_t* emu_memory(Emu* e, int region, size_t* size);
uint8_t emu_read8(Emu* e, uint32_t addr);
uint16_t emu_read16(Emu* e, uint32_t addr);
uint32_t emu_read32(Emu* e, uint32_t addr);
void emu_write8(Emu* e, uint32_t addr, uint8_t value);
void emu_write16(Emu* e, uint32_t addr, uint16_t value);
void emu_write32(Emu* e, uint32_t addr, uint32_t value);

/* Pull up to maxFrames stereo frames of audio; returns frames read. */
size_t emu_audio_read(Emu* e, int16_t* out, size_t maxFrames);
unsigned emu_audio_rate(Emu* e);

/* tools/png.c (host tools only) */
bool write_png(const char* path, const uint32_t* px, int w, int h, int stride);

/* Hide sprites matching any of the n rules (n = 0 turns it off). Display only:
 * the game's memory and state are untouched. One emulator at a time. */
void emu_hide_sprites(Emu* e, const SpriteRule* rules, int n);
void emu_keep_sprites(Emu* e, const SpriteRule* rules, int n);
void emu_hide_grouped(Emu* e, bool on);
/* Also hide the sprites whose attributes 0-2 are exactly one of these
 * (Pokemon Pinball's live view: what the other copy draws on its screen). */
void emu_hide_exact(Emu* e, const uint16_t (*attrs)[3], int n);
/* Which layers are drawn: bits 0-3 BG0-3, bit 4 sprites (0x1F = all). Takes
 * effect when the display registers are next written (e.g. a state load). */
void emu_layers(Emu* e, unsigned mask);
/* Widescreen patches (docs/WIDESCREEN.md): the marker a GBA DS
 * widescreen patch puts in the ROM, and the wider (or taller) picture it asks
 * for. */
#define WIDE_MARGIN_MAX 64
typedef struct {
	bool found;
	int num, den;    /* the aspect ratio, as the patch names it (16:9) */
	int w, h;        /* the picture: 284 x 160 for 16:9 */
	uint32_t offset; /* where the marker is in the ROM */
} EmuWide;
bool emu_wide_parse(const uint8_t* rom, size_t size, EmuWide* out);
/* the picture for a ratio (as tools/widescreen.py's): one axis the GBA's */
bool emu_wide_ratio(int num, int den, EmuWide* out);
/* the game's ROM as loaded (patched) */
bool emu_wide_find(const Emu* e, EmuWide* out);
/* Draws the margins from now on (the game's own core, once per open). */
bool emu_wide_on(Emu* e, const EmuWide* w);
/* On with another picture, or off (w NULL or not found), while the game runs
 * (the wide web player's choice). */
bool emu_wide_set(Emu* e, const EmuWide* w);
bool emu_wide_active(void);
int emu_wide_w(void);
int emu_wide_h(void);
/* The whole picture: centre (GBA_W x GBA_H, the game's frame as shown) with
 * the margins around it; emu_wide_w() pixels a row. centre itself when off. */
const uint32_t* emu_wide_compose(const uint32_t* centre);
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
/* While every one of these (n, up to 2) holds as a frame starts drawing (the
 * game's vblank code done), these BG layers go into the HUD too, for that
 * frame (Dragon Ball Z: the counter's box, drawn the same vblank its flag
 * is set); addresses in EWRAM, IWRAM, the I/O registers or VRAM. n 0: off.
 * any: while any one of them holds instead (Mario & Luigi: either box's
 * tile map entry). */
/* (gamePal: palette + 1; on a line where one of those layers has a tile
 * in it, the layer stays on the game there instead; 0: none) */
void emu_hud_mem_layers(const uint32_t addr[2], const uint32_t mask[2], const uint32_t value[2], const int size[2], int n, bool any, unsigned layers, int gamePal);
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
/* While split: the HUD's BG layers and sprites also stay in the game
 * picture (copy) or leave it again; the HUD picture is drawn either way. */
void emu_hud_copy(Emu* e, bool copy);
/* While split: the HUD's BG layers also stay in the game picture above
 * this row (0: not at all; from the next line drawn). */
void emu_hud_copy_above(int row);
/* The split's layers leave the game only on lines above this row (0: all
 * lines); below it the game draws them where they are, at their own
 * priority (profile hud.rows: Super Mario Advance 2's status bar, its
 * layer the scenery further down in some levels). */
void emu_hud_rows(int rows);
/* The HUD split also draws the sprites with exactly these attributes 0-2
 * (the game's picture keeps them); n = 0: none. */
void emu_hud_keep_exact(const uint16_t (*attrs)[3], int n);
/* The HUD's sprites drawn elsewhere in its picture (only there: the game's sprite table
 * and screen are left as they are), by their number in the sprite table: how[i] 0 as the
 * game has it, 1 with attributes 0 and 1 (place, shape, size) ab[i] (its picture and
 * palette still the game's), 2 left out. NULL: as the game has them. Mario Pinball Land:
 * the counters where the game puts them when paused (MPL-3). */
void emu_hud_place(const uint8_t how[128], const uint16_t ab[128][2]);
/* The HUD split draws the sprites in OAM slots oamIndex[i] with tile tile[i] instead of their own
 * (the game's picture and its memory are untouched); n = 0: none. */
void emu_hud_retile(const int* oamIndex, const uint16_t* tile, int n);
/* The lines the band was on in the last frame drawn split; false if none. */
bool emu_hud_band_rows(int* y0, int* y1);
/* An I/O register as last written (also the write-only ones: MOSAIC). */
uint16_t emu_io16(Emu* e, uint32_t offset);
/* VRAM (96 KB), for reading tile maps. */
const uint16_t* emu_vram(Emu* e);
/* n bytes into VRAM at offset off at once (the renderer told once) */
void emu_vram_write(Emu* e, uint32_t off, const void* src, size_t n);
/* The sprite table as drawn (OAM, 1 KB): read, or written with the renderer told */
void emu_oam_read(Emu* e, uint16_t* out);
void emu_oam_write(Emu* e, const uint16_t* src);
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

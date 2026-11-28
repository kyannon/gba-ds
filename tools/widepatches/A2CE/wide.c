/* Castlevania: Aria of Sorrow (A2CE), GBA DS widescreen patch: the code it adds.
 *
 * The game keeps each of its background layers in a 256-pixel-wide map
 * (BGxCNT size 0, screen blocks 28-31) and streams one column of tiles just
 * past the screen's edge as the camera moves. A wider picture than 256 pixels
 * can only show that map wrapping round. This makes the two playfield layers
 * (BG1, BG2) 512 pixels wide in VRAM that is free during play (0x2000-0x3FFF:
 * only the pause screen uses it), streams their columns three tiles further
 * out on both sides, and makes the HUD layer (BG0) 512 wide with an empty
 * right half (BG1's old block) so it stops wrapping round too.
 *
 * Built by build.py at 0x08700000 (the ROM's unused zeros); the game reaches it
 * through long-call stubs that build.py writes over the game's full-redraw
 * routine (0x0803F710, replaced by full_redraw below). Every function here is
 * called with the calling convention of the code it replaces.
 *
 * Addresses are the USA release's (A2CE, CRC32 35536183).
 */
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef unsigned long long u64;

#define SHADOW ((volatile u16*) 0x03002C60)  /* the game's BG0-3CNT, DMA'd to 0x04000008 each vblank */
#define REG_BGCNT ((volatile u16*) 0x04000008)
#define OLD_MAPS 0x0600E000u                 /* screen block 28 + layer */
#define NEW_MAPS 0x06002000u                 /* BG1: blocks 4-5, BG2: blocks 6-7 */
#define EXTRA 3                              /* columns streamed past each edge (24 px > the 22 px of 16:9) */

/* the game's own routines (Thumb) */
#define QUEUE ((void* (*)(u32, u32, u16*, u32)) 0x08001581)    /* vblank copy queue: flags, count, src, VRAM address */
#define LOOKUP ((u32 (*)(void*, u32, u32)) 0x08001995)         /* a layer's tile at (col, row) */
#define VBLANK_NEXT ((void (*)(void)) 0x080015E5)              /* what the vblank handler called where the hook is */

/* The room's layout, as the game sets it for play: HUD in block 28, the
 * playfield in 29 and 30, all 256 wide. Anything else (the pause screen,
 * menus) is left alone. */
static int wide_now(void) {
	return (SHADOW[0] & 0xDF00) == 0x1C00 && (SHADOW[1] & 0xDF00) == 0x1D00 && (SHADOW[2] & 0xDF00) == 0x1E00;
}

static int wide_layer(u32 layer) {
	return (layer == 1 || layer == 2) && wide_now();
}

/* A map entry's address: the game's own, or the 512-wide map's (two blocks
 * side by side: columns 0-31 in the first, 32-63 in the second). */
u32 wide_addr(u32 layer, u32 col, u32 row) {
	if (wide_layer(layer))
		return NEW_MAPS + (layer - 1) * 0x1000 + ((col & 32) ? 0x800 : 0) + (col & 31) * 2 + (row & 31) * 64;
	return OLD_MAPS + layer * 0x800 + (col & 31) * 2 + (row & 31) * 64;
}

/* The tile lookup with the room's edges: past them (or before column 0, which
 * arrives as 0xFFFx) an empty tile, not whatever lies beyond the room's data. */
u32 wide_lookup(void* layerData, u32 col, u32 row) {
	col &= 0xFFFF;
	if (col >= (u32) * (u8*) layerData * 32)
		return 0;
	return LOOKUP(layerData, col, row & 0xFFFF);
}

/* Column streaming (camera moving right / left): the column the game picked,
 * moved EXTRA further out for a wide layer. Returns the address (r0) and the
 * column (r1). */
u64 wide_col_right(u32 layer, u32 col, u32 row) {
	if (wide_layer(layer))
		col = (col + EXTRA) & 0xFFFF;
	return wide_addr(layer, col, row) | ((u64) col << 32);
}

u64 wide_col_left(u32 layer, u32 col, u32 row) {
	if (wide_layer(layer))
		col = (col - EXTRA) & 0xFFFF;
	return wide_addr(layer, col, row) | ((u64) col << 32);
}

/* Row streaming (camera moving up / down): the producers now fill
 * 31 + 2 * EXTRA tiles from EXTRA columns left of the game's start. A wide
 * layer's row is queued in parts, one per block it crosses (37 tiles can
 * cross twice: 1 + 32 + 4); any other layer gets the game's own 31, from the
 * game's own column. */
void wide_row_queue(u32 flags, u32 count, u16* src, u32 addr) {
	if (addr >= NEW_MAPS && addr < NEW_MAPS + 0x2000) {
		while (count) {
			u32 n = 32 - ((addr >> 1) & 31);
			if (n > count)
				n = count;
			QUEUE(flags, n, src, addr);
			src += n;
			count -= n;
			addr = (addr ^ 0x800) & ~0x3Eu;
		}
		return;
	}
	QUEUE(flags, count - 2 * EXTRA, src + EXTRA, (addr & ~0x3Eu) | ((addr + EXTRA * 2) & 0x3E));
}

/* The game's full redraw of a layer (room loads, back from the pause screen),
 * 0x0803F710: as the game does it, 32 x 32 tiles from the camera's top-left
 * tile; for a wide layer 32 + 2 * EXTRA columns from EXTRA to the left, and
 * for BG1 the old block cleared, as it's now the HUD's empty right half. */
void full_redraw(u32 layer) {
	u8* base = *(u8**) 0x084F0B14;
	u8* s = base + 0xA078 + layer * 28;
	volatile u16* saved = (volatile u16*) (0x03002C68 + layer * 4);
	u16 cx = *(u16*) (s + 6), cy = *(u16*) (s + 10);
	saved[0] = cx;
	saved[1] = cy;
	void* data = *(void**) s;
	u32 c0 = cx >> 3, r0 = cy >> 3;
	if (!wide_layer(layer)) {
		for (u32 i = 0; i < 32; ++i) {
			u32 row = (r0 + i) & 0xFFFF;
			for (u32 j = 0; j < 32; ++j) {
				u32 col = (c0 + j) & 0xFFFF;
				*(volatile u16*) (OLD_MAPS + layer * 0x800 + (col & 31) * 2 + (row & 31) * 64) = (u16) LOOKUP(data, col, row);
			}
		}
		return;
	}
	if (layer == 1)
		for (volatile u32* p = (volatile u32*) (OLD_MAPS + 0x800); p < (volatile u32*) (OLD_MAPS + 0x1000); ++p)
			*p = 0;
	for (u32 i = 0; i < 32; ++i) {
		u32 row = (r0 + i) & 0xFFFF;
		for (u32 j = 0; j < 32 + 2 * EXTRA; ++j) {
			u32 col = (c0 - EXTRA + j) & 0xFFFF;
			*(volatile u16*) wide_addr(layer, col, row) = (u16) wide_lookup(data, col, row);
		}
	}
}

/* In the vblank handler, right after the game's DMA has copied its BGxCNT to
 * the hardware: while in play, the wide layout instead. The game's own copy
 * (SHADOW) stays as it was, so its vblank copy queue still wraps rows and
 * columns within one 32 x 32 block, which is how the hardware lays out a
 * 512-wide map. */
void wide_vblank(void) {
	if (wide_now()) {
		REG_BGCNT[0] = SHADOW[0] | 0x4000;
		REG_BGCNT[1] = (SHADOW[1] & ~0x1F00) | 0x4000 | (4 << 8);
		REG_BGCNT[2] = (SHADOW[2] & ~0x1F00) | 0x4000 | (6 << 8);
	}
	VBLANK_NEXT();
}

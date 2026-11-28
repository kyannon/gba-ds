/* Pokémon FireRed (BPRE, 1.0) widescreen: the overworld's map drawn 512 pixels wide.
 *
 * Replacements for field_camera.c's tilemap ring (pret/pokefirered names). The game keeps
 * the map round the camera in a ring of 16 x 16 metatiles (32 x 32 tiles, one 256 x 256
 * background); a 16:9 picture (284 pixels) needs more than 256 columns, so here the ring
 * is 32 metatiles wide (64 x 32 tiles: a 512 x 256 background, two screenblocks side by
 * side) and starts K metatiles left of the screen, so the margins on both sides show the
 * map. Vertically nothing changes.
 *
 * Each function here takes the place of the game's own (a jump at its start), with the
 * same arguments; the rest of the game calls them as before. No division, no library.
 */
typedef unsigned char u8;
typedef signed char s8;
typedef unsigned short u16;
typedef signed short s16;
typedef unsigned int u32;
typedef signed int s32;

struct FieldCameraOffset {
	u8 xPixelOffset;
	u8 yPixelOffset;
	u8 xTileOffset;
	u8 yTileOffset;
	u8 copyBGToVRAM;
};

struct Coords16 { s16 x, y; };

#define sFieldCameraOffset (*(struct FieldCameraOffset*) 0x03000E90)
#define sHorizontalCameraPan (*(volatile s16*) 0x03000E98)
#define sVerticalCameraPan (*(volatile s16*) 0x03000E9A)
#define gSaveBlock1Ptr (*(struct Coords16**) 0x03005008)
#define gCameraActive (*(volatile u8*) 0x02036E18 & 1)   /* gCamera.active: a map connection was just crossed */
#define gMapLayout (*(const void**) 0x02036DFC)        /* gMapHeader.mapLayout */
struct BackupMapLayout { s32 width, height; u16* map; };
#define gBackupMapLayout (*(struct BackupMapLayout*) 0x03005040)

typedef void (*DrawMetatileAtFn)(const void* mapLayout, u16 offset, int x, int y);
typedef void (*SetGpuRegFn)(u8 regOffset, u16 value);
#define DrawMetatileAt ((DrawMetatileAtFn) (0x0805A948 | 1))
#define SetGpuReg ((SetGpuRegFn) (0x08000A38 | 1))

#define K 8          /* metatiles of the ring left of the screen */
#define RING_W 64    /* tiles across the ring (32 metatiles) */

/* A tile's place in a 512 x 256 text background: two 32 x 32 screenblocks side by side. */
static inline u32 tile_index(u32 col, u32 row)
{
	return ((col & 32) << 5) | (row << 5) | (col & 31);
}

/* 0x0805A5B8 tilemap_move_something */
void ws_tilemap_move(struct FieldCameraOffset* c, u32 dx, u32 dy)
{
	c->xTileOffset = (u8) (c->xTileOffset + dx) & (RING_W - 1);
	c->yTileOffset = (u8) (c->yTileOffset + dy) & 31;
}

/* The background's horizontal scroll: the screen's left edge sits 2K tiles into the ring
 * from xTileOffset, plus the part of a step the pixel offset is ahead or behind. */
static u16 ws_hofs(void)
{
	struct FieldCameraOffset* c = &sFieldCameraOffset;
	s8 phase = (s8) (u8) (c->xPixelOffset - c->xTileOffset * 8);
	return (u16) (((c->xTileOffset + 2 * K) * 8 + phase + sHorizontalCameraPan) & 0x1FF);
}

/* 0x0805A5F4 FieldUpdateBgTilemapScroll */
void ws_update_scroll(void)
{
	u16 h = ws_hofs();
	u16 v = (u16) (sVerticalCameraPan + sFieldCameraOffset.yPixelOffset + 8);
	SetGpuReg(0x14, h);
	SetGpuReg(0x16, v);
	SetGpuReg(0x18, h);
	SetGpuReg(0x1A, v);
	SetGpuReg(0x1C, h);
	SetGpuReg(0x1E, v);
}

/* 0x0805A658 FieldCameraGetPixelOffsetAtGround (battle transitions) */
void ws_pixel_offset(s16* hofs, s16* vofs)
{
	*hofs = (s16) ws_hofs();
	*vofs = (s16) (sFieldCameraOffset.yPixelOffset + sVerticalCameraPan + 8);
}

/* 0x0805A6A8 DrawWholeMapViewInternal */
void ws_draw_whole(int x, int y, const void* layout)
{
	struct FieldCameraOffset* c = &sFieldCameraOffset;
	for (u32 i = 0; i < 32; i += 2) {
		u32 row = (c->yTileOffset + i) & 31;
		for (u32 j = 0; j < RING_W; j += 2) {
			u32 col = (c->xTileOffset + j) & (RING_W - 1);
			DrawMetatileAt(layout, tile_index(col, row), x - K + (int) (j >> 1), y + (int) (i >> 1));
		}
	}
}

/* 0x0805A778 RedrawMapSliceNorth: the row 14 metatiles down */
void ws_slice_north(struct FieldCameraOffset* c, const void* layout)
{
	u32 row = (c->yTileOffset + 28) & 31;
	struct Coords16* pos = gSaveBlock1Ptr;
	for (u32 i = 0; i < RING_W; i += 2) {
		u32 col = (c->xTileOffset + i) & (RING_W - 1);
		DrawMetatileAt(layout, tile_index(col, row), pos->x - K + (int) (i >> 1), pos->y + 14);
	}
}

/* 0x0805A7D8 RedrawMapSliceSouth: the top row */
void ws_slice_south(struct FieldCameraOffset* c, const void* layout)
{
	u32 row = c->yTileOffset;
	struct Coords16* pos = gSaveBlock1Ptr;
	for (u32 i = 0; i < RING_W; i += 2) {
		u32 col = (c->xTileOffset + i) & (RING_W - 1);
		DrawMetatileAt(layout, tile_index(col, row), pos->x - K + (int) (i >> 1), pos->y);
	}
}

/* A column of the ring: the metatiles at x, from pos.y down. */
static void ws_column(struct FieldCameraOffset* c, const void* layout, int x)
{
	struct Coords16* pos = gSaveBlock1Ptr;
	u32 col = (c->xTileOffset + 2 * (u32) (x - (pos->x - K))) & (RING_W - 1);
	for (u32 i = 0; i < 32; i += 2) {
		u32 row = (c->yTileOffset + i) & 31;
		DrawMetatileAt(layout, tile_index(col, row), x, pos->y + (int) (i >> 1));
	}
}

/* The screen shows pos.x to pos.x + 14 (15 metatiles); the margins (22 pixels each side)
 * reach into pos.x - 2 and pos.x + 16. As the game draws the column that comes into view
 * at the screen's edge, these draw the one at the picture's edge. */

/* 0x0805A824 RedrawMapSliceEast (moving left) */
void ws_slice_east(struct FieldCameraOffset* c, const void* layout)
{
	ws_column(c, layout, gSaveBlock1Ptr->x - 2);
}

/* 0x0805A87C RedrawMapSliceWest (moving right) */
void ws_slice_west(struct FieldCameraOffset* c, const void* layout)
{
	ws_column(c, layout, gSaveBlock1Ptr->x + 16);
}

/* 0x0805A72C RedrawMapSlicesForCameraUpdate. Crossing into the next map, the game keeps
 * what it had drawn: everything on screen was in the old map's grid, which holds 7
 * metatiles of the next map. The margins reach 9, so the 8th and 9th were drawn as the
 * old map's border: draw the ring again, but only where the new map's grid has the
 * metatile (the rest stays as the old map's grid had it). */
void ws_redraw_slices(struct FieldCameraOffset* c, int dx, int dy)
{
	const void* layout = gMapLayout;
	if (gCameraActive) {
		struct Coords16* pos = gSaveBlock1Ptr;
		s32 w = gBackupMapLayout.width, h = gBackupMapLayout.height;
		for (u32 i = 0; i < 32; i += 2) {
			int y = pos->y + (int) (i >> 1);
			if (y < 0 || y >= h)
				continue;
			u32 row = (c->yTileOffset + i) & 31;
			for (u32 j = 0; j < RING_W; j += 2) {
				int x = pos->x - K + (int) (j >> 1);
				if (x < 0 || x >= w)
					continue;
				u32 col = (c->xTileOffset + j) & (RING_W - 1);
				DrawMetatileAt(layout, tile_index(col, row), x, y);
			}
		}
	} else {
		if (dx > 0)
			ws_slice_west(c, layout);
		if (dx < 0)
			ws_slice_east(c, layout);
		if (dy > 0)
			ws_slice_north(c, layout);
		if (dy < 0)
			ws_slice_south(c, layout);
	}
	c->copyBGToVRAM = 1;
}

/* 0x0805AAE8 MapPosToBgTilemapOffset (a metatile changed: doors, Cut, scripts) */
s32 ws_map_pos(struct FieldCameraOffset* c, s32 x, s32 y)
{
	struct Coords16* pos = gSaveBlock1Ptr;
	x = (x - (pos->x - K)) * 2;
	if (x >= RING_W || x < 0)
		return -1;
	x += c->xTileOffset;
	if (x >= RING_W)
		x -= RING_W;
	y = (y - pos->y) * 2;
	if (y >= 32 || y < 0)
		return -1;
	y += c->yTileOffset;
	if (y >= 32)
		y -= 32;
	return (s32) tile_index((u32) x, (u32) y);
}

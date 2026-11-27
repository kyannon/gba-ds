# GBA DS widescreen test ROM

A small GBA program for trying GBA DS's widescreen patches (docs/WIDESCREEN.md) without a real game. It's GBA DS's own: no copyrighted code or graphics.

    tools/widetest/build.py [OUTDIR]       (default build/widetest; needs clang, ld.lld, llvm-objcopy)

makes:

- `GBA DS Widescreen Test.gba` (game code ZWSE)
- `GBA DS Widescreen Test (16-9).ips`: 284 × 160
- `GBA DS Widescreen Test (16-10).ips`: 256 × 160
- `GBA DS Widescreen Test (4-3).ips`: 240 × 180

Each patch writes only the marker, at 0x200 (the ROM keeps 32 bytes of 0xFF there for it). The ROM reads that place as it starts and draws for the picture the marker asks for, as a real game with its widescreen fixes would. A real game's patch is the game's fixes and the marker together: `tools/widescreen.py ips OFFSET 16:9 --with FIXES.ips -o OUT.ips`.

## On the device

1. Put the ROM in the GBA ROMs folder. It isn't a supported game, so the game list shows it only with **All** on (the Supported / All button).
2. Start it once: GBA DS makes its patches folder, `patches/GBADS WIDE (ZWSE)/`. Put the three patches there.
3. Pause > Extras > Patches: switch one on. The game restarts with it.

What to look for:

- **Without a patch:** the GBA's 240 × 160, a yellow frame at its edges. With Stretch, the circle is an oval: that's what a stretched picture looks like.
- **With a patch:** the label says which (`16:9 PATCH  284 X 160`). The circle is round and the grid's squares are square, whatever Game screen scale says. Green margins at the sides (16:9, 16:10) or above and below (4:3, `EXTRA ROWS`), the grid carrying on into them, the GBA's own edges dashed in cyan. The yellow frame is the whole picture's edge: it should be right at the picture's edge, not cut off. The balls travel into the margins.
- **A: scene 2.** A rotating checkerboard with rings. It should carry on into the margins with no seam at the cyan lines. The dark band at the top should reach the picture's edges. The dark hole should stay in the middle.

# Widescreen patches

Status: merged into `dev` (v1.6.5), Nov 27, 2026, after both steps were checked on the device. Step 1, tried on the device (BananaOS, RG DS Plus; all three test patches at full speed): the wider picture, the marker, the scaling it overrides, a test ROM and its patches. Step 2: GBA DS's own patches kept in the repository and offered for download in a game's Patches menu, for the shape of the player's screens (WS-6), and one widescreen patch on at a time (WS-1).

This is an exception to the first principle (wiki/Philosophy.md: a GBA with two 240×160 screens), asked for by the owner: for a game that has one, a GBA DS widescreen patch shows more of the game to its sides (or above and below), at the shape of the player's screen instead of the GBA's 3:2. It's the player's choice, game by game: nothing changes without a patch, and the patch is switched on like any other (GEN-9). The second screen is unchanged. It's also not covered by "display options are the player's choice" (principle 2): that line is about how the GBA's 240×160 picture is scaled and filtered, and this shows more than a GBA screen would.

| | |
|---|---|
| Games | the ones a GBA DS widescreen patch is made for (none yet); the test ROM |
| Code | `src/emu.c` (`emu_wide_*`: the marker, the margins' renderers), `src/game.c` (`game_tex_*`, `game_scale`, at the game's start; the Patches menu's offers, `PR_DOWNLOAD`, `patch_wide_alone`), `src/widedl.c` (the list and the downloads) |
| Patches | `widescreen/CODE/*.ips` and `widescreen/index.txt` in the repository, never in a release (README there) |
| Tools | `tools/widescreen.py` (the marker, a patch's IPS, `index` for the list); `tools/widetest/build.py` (the test ROM and its 16:9, 16:10 and 4:3 patches) |
| Tests | `tests/regress/widescreen.json` (the test ROM, fixture `roms/ZWSE.zip`, patches in `patches/`; the download cases `"widescreen_list": true`, served by regress.py from the repo's `widescreen/`) |

## How it works

A GBA game draws a 240×160 picture, but most of what it draws is laid out past the screen's edges as well: a background's map is 256 or 512 pixels wide, sprites can sit anywhere from -256 to 255, a rotating background goes on forever. GBA DS draws the game's own picture as always, and draws the margins around it with two more of mGBA's renderers, fed everything the game writes to its video hardware, each looking a margin's width to one side (or above and below). So the margins show what the game has there, in its own graphics, as the GBA would show it if its screen were wider.

What a game has there isn't always right, because it was never meant to be seen: scenery loaded only a tile past the edge, sprites hidden as they leave the screen, a HUD layer that repeats. The patch is what fixes those, game by game: it changes the game so it draws its world out to the wider picture's edges. And it carries the marker that tells GBA DS to draw the wider picture at all.

`GBADS_WIDE=16:9` (an environment variable, for writing a patch) draws the wider picture for any game, patched or not, to see what it has past its edges. The quickest way to look: the web player's wide view, https://kyannon.github.io/gba-ds/wide (docs/WEB-PLAYER.md, "The wide view"), the same with a ratio to pick in its bar that changes while the game plays.

### The marker

32 bytes, on a 16-byte boundary, anywhere in the ROM the game never reads (its padding at the end, usually 0xFF):

| Offset | Size | |
|---|---|---|
| 0x00 | 16 | `GBADS-WIDESCREEN` |
| 0x10 | 1 | version: 1 |
| 0x11 | 1 | the ratio's first number (16) |
| 0x12 | 1 | the ratio's second number (9) |
| 0x13 | 1 | flags: 0 |
| 0x14 | 2 | the picture's width, little-endian (284) |
| 0x16 | 2 | the picture's height (160) |
| 0x18 | 8 | 0 |

The picture keeps the GBA's 160 lines and is wider (284×160 for 16:9, 256×160 for 16:10), or keeps its 240 columns and is taller (240×180 for 4:3), never both; the margins are the same on both sides, at most 64 pixels each. `tools/widescreen.py size RATIO` gives the picture for a ratio, `ips OFFSET RATIO -o OUT --with FIXES.ips` writes a patch: the game's fixes and the marker. GBA DS looks for the marker in the ROM as it's played, patches applied, so it works with IPS, UPS and BPS alike; it doesn't look in an unpatched ROM.

One patch per ratio: a game can have a 16:9, a 16:10 and a 4:3 patch, each with its own marker (and its own fixes: a wider picture can need more than a narrower one). Only one is on at a time (WS-1).

### The test ROM

`tools/widetest/build.py` builds `GBA DS Widescreen Test.gba` (game code ZWSE, our own code, no copyrighted parts) and a patch per ratio, named as step 2 will offer them: `GBA DS Widescreen Test (16-9).ips`. The ROM reads its own marker's place (0x200, 0xFF until a patch writes there) and acts like a real game with and without a widescreen patch: without one, its background past the screen's edges is junk (a red hatch) and its balls turn round at the screen's edges; with one, it draws its scene out to the picture's edges and the balls travel the whole of it. Its two scenes (A switches): a circle and a square grid (round and square only at the right shape), the picture's frame in yellow, the GBA's own edges dashed in cyan, the margins in green; then a rotating background with two windows (a band across the top that should reach the picture's edges, a hole that should stay in the middle).

## Rules

**WS-1. A patch turns it on.** A game is shown wider (or taller) only when a GBA DS widescreen patch is on: in its Patches menu (GEN-9) or beside the ROM. Without one, nothing about the game's picture changes. A patch is otherwise a patch like any other: switching it on restarts the game, it gets its own battery save and states unless the player says otherwise, and a patched game earns no achievements. In the Patches menu its name says "widescreen 16:9", and its note what it does. **One at a time:** switching a widescreen patch on switches off any other that's on (the other patches stay as they are).

**WS-2. The picture.** The game's own 240×160 is in the middle, exactly as without the patch; around it, the margins the patch asks for, drawn from the game's own graphics as the game lays them out past its screen's edges: its backgrounds (moving ones, rotating ones), its sprites, its windows (a window that reaches the screen's edge reaches the margin's). Nothing in the margins is GBA DS's own.

**WS-3. Its shape wins.** The picture is shown at its own shape (16:9 for a 16:9 patch), whatever Game screen scale says: Pixel Perfect stays Pixel Perfect (the largest whole number of screen pixels to a game pixel that fits); Full Width, Zoom, Stretch and Edge stretch all show it as Full Width (as large as fits at its shape). On the RG DS Plus's 4:3 screens a 16:9 picture has bars above and below, and a 4:3 patch's fills the screen. Shader, bezel, overlays and messages follow the picture's size as they do the GBA's.

**WS-4. The second screen is as it was.** The hidden copy, its captures, the logo and every second-screen picture stay 240×160 (principle 1 holds there). A HUD moved to the second screen (a HUD split) leaves the margins too.

**WS-5. The game is unchanged by GBA DS.** GBA DS doesn't change the game for the wider picture: the patch does, as the player chose. The margins are drawn only; they don't change what the game does or how it runs.

**WS-6. Download.** GBA DS's widescreen patches are kept in its repository (`widescreen/`), not in the app or its zip. When a game's Patches menu opens, GBA DS reads the list of them (fetched once a run in the background, and kept for the next time it can't be reached) and offers, below the patches already in the folder, the one made for this game's ROM at the shape of the screen the game is on: "Widescreen 4:3: download" on the RG DS Plus. If the other screen is another shape, its patch is offered too, after it. A shape is matched within 2% (a 1920x1080 screen is 16:9); a screen no patch fits gets no offer, and a patch already in the folder isn't offered again. A on the offer asks first ("Sure?"), then downloads it into the game's patches folder, checks it against the list's SHA-256, switches it on (WS-1) and restarts the game with it. If it can't be downloaded or doesn't match, it says so and nothing changes. Once downloaded it's a patch file like any other. A release offers the patches on `main`, a test build (a `-dev` version) the ones on `dev`, a side build the ones on its own branch.

## Known limitations

- The wider picture costs two more of mGBA's renderers (about a quarter of a millisecond a frame on a PC; on the device, still to measure).
- Only the game's own picture is wider: the picture of a resume point (in the game list), RetroArch's state thumbnails and bug reports keep the GBA's 240×160. Take Screenshot saves the screens as shown, the wider picture included.
- LCD ghosting, the dot filter and run-ahead act on the middle 240×160 only (the margins come from the latest frame).
- A sprite a game's profile hides from the game screen (a cursor, a HUD sprite moved to the second screen) isn't hidden from the margins.
- A rotating background whose rotation changes partway down the screen without its position being written again is a pixel or two off in the margins; a window that wraps round the screen's edge isn't moved. Mosaic isn't moved.
- Android builds it but hasn't been tried (its downloads go through the app's own HTTP, as its updates do). The web player has no downloads: its Patches menu offers nothing.
- The list is read once a run: a patch added to the repository while GBA DS is running is offered from its next start.
- The new menu text isn't translated yet.

## Adding a game's patches

1. Make the patch: the game's fixes and the marker, `tools/widescreen.py ips OFFSET 16:9 --with FIXES.ips -o "widescreen/CODE/Game (16-9).ips"` (IPS or BPS: a UPS hides the marker from the list). Name it for the game and the ratio: that's the file name players get.
2. `widescreen/CODE/dumps.txt`: the CRC32 of each ROM file it fits, one a line (the tested dump from the game's rules file; without the file, any ROM with the code is offered it).
3. `tools/widescreen.py index`, and commit the patch, `dumps.txt` and `index.txt` together. `tools/widescreen.py index --check` says whether the list is up to date.
4. A rule for it in the game's rules file, and a case with the patch on.

# Pokémon Pinball (BPPE): status and handoff

Status as of Oct 27, 2026. The player-facing rules are in
`docs/games/pokemon-pinball.md` (PP-1 to PP-11, some retired); this file is
for the next session: what's built, what was learned, what's still wrong.

## What the owner wants

The table twice as tall over both screens, **live on both**, as if the RG DS
Plus had one tall screen. The owner's specs (State Saver notes on "Pinball:
Table behavior" 1 and 2, and "Pause screen"):
- The score bar and the other items at the bottom of the game always on the
  second screen.
- Two screens' worth of table at all times, moving with the ball.
- The ball on the lower screen only when the table is at its lowest;
  otherwise the camera keeps the ball on the top screen.
- When the game is paused or the screen is otherwise dimmed, that screen
  appears in full on the bottom (second) screen.

Owner's verdicts so far:
- The live view (copies of the game run alongside it) holds full speed on
  the device with one copy.
- The kept-rows approach (rows the game drew earlier, dimmed, flippers drawn
  separately) was dropped at the owner's request (PP-2, PP-7, PP-8 retired).
- Events go on whichever screen keeps transitions fewest.
- On the build of Oct 26 (one copy, the game flipping between the screens,
  instant retakes): "the camera still resets and has to scroll back to place
  basically every time the ball passes between screens"; "sprites disappear
  on the top screen a lot, and jitter/flash on the bottom screen". Both
  explained and fixed in the build of Oct 27 (below), not yet seen on the
  device.

## How it works now (build of Oct 27): two copies, no flip

The view's top row is `v = min(u, 112) + shake`, where `u` is the game's
view row less the screen shake (the game's own pans included) and 112 is
`tall.rows - 320`. The top screen shows rows `v`..`v+159`, the bottom one
the 160 below. Nothing ever flips: the view only scrolls (at most ~6 rows a
frame in the tests), and holds still at 112 once the game's view is further
down (the whole lower table on the two screens).

- `twins[0]` is held at row 112 (the lower view's top half), `twins[1]` at
  `min(u, 112) + 160`. Both run every frame, `twins[0]` on a worker thread
  (`twin_work_*`) while `twins[1]` runs on the emulation thread. On the host
  that's 1.9-2.3 ms of emulation a frame against 1.7-1.9 for the game plus
  one copy run in a row.
- Top screen: the game's own picture while `u <= 112` (it is those rows),
  else `twins[0]`'s with the game's screen-fixed things laid over it.
  Bottom screen: `twins[1]`'s. Each line comes from the first picture that
  has its row (`tall_line`), placed by the rows each picture really shows
  (its BG3VOFS), so a copy a row or two off still lines up.
- **Holding a copy:** the game's view (BG3VOFS shadow `0x0200B3B6` = `S+0x5A`)
  is its camera's row `S+0x4E` plus the screen shake (signed byte `S+0x121`)
  plus offsets of its own: the opening sweep (`S+0x5FC`), pans (`S+0xE6`,
  catch mode's reveal) and in catch mode `S+0x2AC` (routines `0x0804DB60`
  and `0x0804DF10`; other routines too). The pans depend on where the camera
  is, so a copy's own pan can differ from the game's. A copy's camera stores
  are no-ops, and before each frame its camera row is set to
  `target - (its view - its camera row - its shake) - last frame's pan step`;
  after the frame its view less its shake is checked against the target:
  more than 8 rows off, or a different ball position, and it's taken again.
  (Patching the pans out of the copies' formula was tried: the sprites use
  `S+0xE6` too, about 40 places, so they'd no longer line up.)
- **Retake** (`twin_step`): the copy loads the game's state from before
  this frame (`run_frame` saves it when one is due, `twinPre`), `twin_jump`
  sets its scroll and BG3 tile ring for its row at once, it runs the frame,
  and that frame's picture is not used (its sprites and scroll were the
  game's): the screen keeps its last picture (`Twin.pic`) for one frame. In
  six 50-second random games: 0 to 6 retakes each.
- **Sprite filter** (`twin_hide`, from the shadow OAMs before the frame):
  a copy's sprite alike one of the game's sprites *on the game's screen*
  (attributes 1, 2 and the high byte of 0) is the same thing on the table if
  its y is where the table puts it seen from the copy (game y + game view -
  copy view, mod 256): kept if that's on the copy's screen, hidden if it's
  only there by wrapping round; or at the same y with the views different:
  fixed to the screen, hidden. `twins[1]` also hides those found fixed
  against `twins[0]` (when its view is the game's it can't tell). The old
  filter matched any y and hid real Pokémon (the owner's "sprites disappear").
- **Screen-fixed things on the top screen** (lower view, PP-10): the HUD
  split (a second renderer fed the game's writes) draws BG0 and, via
  `emu_hud_keep_exact`, exactly the game's sprites found fixed to the screen
  (banners), on the clear colour; rows above the score bar are laid over the
  top screen. (Comparing the game's picture with the copies' was tried: the
  copies' animations aren't always in phase with the game's, e.g. the
  Makuhita, which left ghosts.)
- **PP-11:** BLDCNT brightness-down with BLDY > 0 (pause: `0x00CF`/10, the
  Mart: `0x00CE`/7), or more than 40 tiles of BG0's map (`0x06002000`, rows
  10-27) not tile `0x1FF` (the timer is 8, a message box 140-240): the
  bottom screen shows the game's picture plus its score bar; the top screen
  the table's rows, or, while those would be the game's picture too, the
  top screen from before it came up (`lastTop`).
- `GBADS_TALL_TRACE=1` logs a line a frame (the view, each copy's rows,
  retakes, sprites hidden, pixels laid over); `GBADS_TWIN_DEBUG=1` logs every
  retake.

## What was learned (don't redo)

- **Never change the game's camera or its limits.** The game treats things
  off its screen differently. Two cores given the same random input, one with
  the lowest camera target lowered (`0x02031538` → 186), part in catch mode
  after 25-40 s, though never on plain tables. Holding the game's camera up
  caused the owner's "catch resets to the reveal" loop. States saved by
  earlier test builds carry the lowered value (186), and `twin_step` puts
  `0x164` back.
- **A flip (the game moving between the screens) can't be made smooth:**
  the whole picture moves by a screen at each one (the owner's "camera
  resets"). With the game plus one copy, a continuous view needs the game's
  160 rows plus 160 next to them, which is only above or below: hence two
  copies.
- **A copy can be put anywhere at once** (`twin_jump`): the BG3 ring
  refilled from the table's tiles in EWRAM, every copy of the scroll set.
  But only on a retake: jumping a running copy (its camera ahead of the game's
  eased view) put its rows a few lines off for a frame and its sprites at
  the old place: the owner's "jitter on the bottom screen".
- **The copies' cosmetic animations can drift out of phase** with the game's
  (things off a screen animate differently); the ball and the play stay in
  step. Don't compare pictures to find what's the game's own.
- **Timing:** each copy is about one more core's emulation; the second runs
  on its own thread. The device held full speed with one copy; with two, to
  be checked.
- The tall-renderer idea (one core drawing 320 rows) was scouted on Oct 26:
  the background is easy (BG1/BG2 fixed, BG3's whole table in EWRAM), but
  objects off the game's screen get no sprites (220 call sites) and the
  lights are gated by the scroll. Not worth it.

## Facts about the game (USA, BPPE)

| What | Where / value |
|---|---|
| State pointer | `0x020314E0` (word) → S (`0x02000000` in every state seen) |
| Camera | `S+0x54` (24.8 fixed), row at `S+0x4E` |
| Camera stores, main routine | `0x0804DCFA` (str), `0x0804DD0A` (strh row) |
| Camera stores, catch-mode routine | `0x0804DA54`, `0x0804DA64` |
| Camera resets, not per frame | `0x0804D728`, `0x0804D888` (from a table, at a ball's start); `0x08035DA8` is jump-table data, not code |
| View (BG3VOFS shadow) | `S+0x5A` → `0x0200B3B6`: camera row + shake `S+0x121` (signed byte) + sweep `S+0x5FC` + pan `S+0xE6` (+ `S+0x2AC` in catch mode); `S+0x68` the same less `S+0x2AC`; `S+0x58` the horizontal one (from `S+0x4C`, `S+0x120`, `S+0x2AA`) |
| Camera target limits | `0x02031520`+0x18 (lowest, `0x164` on Ruby and Sapphire = camera row 272), +0x1A (highest) |
| Camera range | rows 0-272; table 432 rows; BG3 is 256x512 and wraps (rows past 432 or below 0 are garbage) |
| BG scroll | BG3VOFS (`0x0400001E`); BG1 and BG2 follow it; BG3HOFS moves a little sideways |
| Score bar | BG0, screen-fixed, from row 144 (`hudRow`); BG0VOFS 80 above it, 352 below; also the catch timer, message boxes |
| Ball | pointer at `S+0x1330`; x, y words at +0x34, +0x38 (24.8) |
| Shadow OAM | `0x03005000` (copied to OAM at the next vblank: the next frame's sprites) |
| Opening sweep | `0x020005FB` (byte) / `0x020005FC` (half) non-zero while the table's intro sweep runs |
| On a table | BG0CNT `0x8404` and BG3CNT `0x804B` |
| Dimmed | BLDCNT `0x00CF` BLDY 10 paused; `0x00CE` BLDY 7 the Mart; `0x00CE` BLDY 0 normal; `0x1E01` with a message box |
| Message box | BG0 map `0x06002000`, rows 10-27: blank tile `0x1FF`; the timer 8 tiles, a box 140-240 |
| Flippers | sprites tiles 0-15 (left) and 16-31 (right), palette 0, prio 2; left = L or Left, right = R or A |
| Title logo | BG1 at frame 1440, own white striped background |
| BG layers on a table | BG0 map `0x2000`, chars `0x4000`; BG1 map `0x3000`, chars `0x8000` (uses `0xD800`+); BG2 map `0x1000`, chars `0x4000`; BG3 map `0x0000`, chars `0x8000` (the ring). BG1-3 are 256x512 |
| Fixed in VRAM | BG1's and BG2's maps and tiles, and BG3's map: never written on a table |
| BG3 tile ring | `0x06008000`-`0x0600D7FF`, 22 slots of 1 KB (32 tiles, one tile row). Table tile row R is always in slot `(R + 10) % 22`; BG3's map is laid out to match |
| BG3 tiles, whole table | in EWRAM, never changed on a table: rows 0-31 at `0x0200FBB0 + R*0x400`, rows 32-55 at `0x020030A0 + (R-32)*0x400` |
| Streaming | `0x08047030`: one DMA3 a frame when the view's tile row changes (vs `S+0x64`); table `0x086B0010`. Rows `T`..`T+21` resident for tile row `T` |
| Scroll copies (for a jump) | BG1-3 VOFS shadows `0x0200B3AE`, `B3B2`, `B3B6`; `S+0x5A`, `S+0x68` (this frame's), `S+0x6A` (last frame's: the light gates), `S+0xCC`; `0x03007ED8` (+16, the main loop's, on the stack); the BGxVOFS registers |
| Lights | small DMAs straight into the ring (`0x0804ECxx`-`0x0804F8xx`), called from `0x0804E814` by ranges of `S+0x6A` |
| Raster effects | only BG0HOFS/VOFS at line 144 (VCount IRQ, DISPSTAT `0x9029`) for the score bar. DMA1/2 are the sound FIFOs |
| Sprites | each object writes its own shadow-OAM entries (220 references to `0x03005000` in the ROM). Objects far off the screen are left out, with a margin |

## Code map

- `src/profiles.h` `tall` (on probes, `hudBg`, `rows`, `hudRow`, `live`:
  `state`, `cam`, `camRow`, `nop`, `bound`, `ball`, `oam`, `view`, `viewBg`,
  `shake`, `msg`, `idle`, `jump`); the BPPE profile in `src/profiles.c`.
- `src/game.c`: `twins[]`, `twin_work_*`, `twin_jump`, `twin_hide`,
  `twin_step`, `twin_open` / `twin_close` near the top; `run_frame` (the
  sprite filters, the HUD split's fixed sprites, the pre-frame state for a
  retake); `update_tall` (`tall_line`, the overlay, PP-11, the score bar).
- `src/emu.c`: `emu_hide_exact`, `emu_hud_keep_exact` (exact sprites kept
  by the HUD split's renderer), `emu_hud_copy`, `emu_hud_copy_above`.
- `src/config.c`: `pinball_live` (default 1). Off means the game on one
  screen and the logo on the other.

## Tests and fixtures

- `tests/regress/pokemon-pinball.json`, 11 cases. Fixtures in
  `../gba-ds-tests/states/pokemon-pinball/`: `us-title`, `us-field-select`,
  `us-ruby-bottom`, `us-ruby-top`, `us-sapphire-start` (input A at frame 5
  starts the table), `us-ruby-catch` (device), `us-ruby-catch-loop` (device,
  saved with the limit at 186), `us-ruby-pause` (the owner's State Saver
  "Pause screen", saved paused: the copies taken from a paused game have no
  sprites of their own until it's unpaused). ROM: `roms/BPPE.zip`.
- Random games for checking by hand: a script of random flipper presses
  (A, L, A+L for 4-12 frames every 15-70 frames, 3000 frames) from
  `us-ruby-top`, `us-ruby-bottom`, `us-ruby-catch`, with
  `GBADS_TALL_TRACE=1`; check the view's step per frame, each copy's rows
  against where it should be, frames with missing lines, retakes.
- Experiment tools in `tools/scout/` (not built by the Makefile; usage at the
  top of each file): `scout.c` (VRAM writes and their PCs, mid-frame
  registers, the ring, the shadow OAM, BG0's scroll by line), `jump.c` (a
  copy held at an offset, each layer shot alone), `bench.c` (one, three and
  threaded cores). Build: ``FL=$(make -n tools -B | grep -o -- '-O2.*-D_REENTRANT' | head -1)``,
  then `gcc $FL -Isrc -o build/host/jump tools/scout/jump.c src/emu.c src/mapcap.c src/profiles.c src/logo_masks.c src/logo.c tools/png.c deps/mgba/build-host/libmgba.a deps/mgba/build-host/zlib/libz.a -lpng -lpthread -lm`
  (`bench.c` the same).
- `llvm-objdump --triple=thumbv4t-none-eabi` on the ROM wrapped with
  `objcopy -I binary -O elf32-little --change-section-address .data=0x08000000`
  disassembles it.

## Not done

- **The owner's verdict on the device**, and speed with two copies there.
- **Bonus stages:** the copies only run where the lowest camera target is
  `0x164`, so they play on one screen. Their tables and camera limits are
  unknown.
- **Other events** on the device (evolution, hatching, the bonus-stage
  entrances): unchecked. The catch reveal's picture differs between the
  game and a copy now and then (cosmetic).
- **Sapphire:** only its start is in the tests.
- **Android:** `twin_*` is shared code; not tried on Android.

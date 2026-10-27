# Pokémon Pinball (BPPE): status and handoff

Status as of Oct 26, 2026 (dev `12f6e54` and later; scouting notes added the same day). The player-facing rules are in
`docs/games/pokemon-pinball.md` (PP-1 to PP-10, some retired); this file is
for the next session: what's built, what was learned, what's still wrong.

## What the owner wants

The table twice as tall over both screens, **live on both**, as if the RG DS
Plus had one tall screen. The owner's earlier specs (State Saver notes on
"Pinball: Table behavior" 1 and 2):
- The score bar and the other items at the bottom of the game always on the
  second screen.
- Two screens' worth of table at all times, moving with the ball.
- The ball on the lower screen only when the table is at its lowest;
  otherwise the camera keeps the ball on the top screen.

Owner's verdicts so far:
- The live view (a second core) holds full speed on the device and "seems to
  work great in general".
- The kept-rows approach (rows the game drew earlier, dimmed, flippers drawn
  separately) was dropped at the owner's request (PP-2, PP-7, PP-8 retired).
- Events go on whichever screen keeps transitions fewest. Currently they're
  always on the game's screen, where the ball is.
- Newer State Saver note ("Pause screen", BPPE, Oct 25): when the game is
  paused or the screen is otherwise dimmed, that screen should always appear
  in full on the bottom (second) screen. Not done yet.

## The blackouts at every flip: fixed in the build of Oct 26 (to check on the device)

The owner's report on the build before: **one screen or the other blacked
out and refilled every time the ball crossed from one screen to the
other.** The copy was retaken from the game and its camera ramped from 0 to
±160 rows at 8 a frame: 20 frames with its rows not in place.

What the build of Oct 26 does instead (PP-9 changed):
- `twin_jump` (profile `tall.live.jump`) puts the copy at its camera at once:
  the BG3 tile ring refilled from EWRAM, every eased copy of the scroll set
  (see "Scouting" below). No ramp: `twinOff` is always `twinTarget`.
- A retake loads the game's state from **before** its frame (`run_frame`
  saves it into `twinState` when a retake is due, `twinPre`), so the copy
  runs the same frame at once and its picture is fresh. Its sprites are
  hidden on that first frame (its OAM is still the game's).
- A flip asks for the retake first and swaps the screens on the frame the
  copy is there (`update_tall`: `want` / `need`), so neither screen is ever
  without its rows.
- A drift (ball positions differ) asks for a retake and keeps the copy's
  picture up for that frame, instead of dropping the live view (the logo
  for a frame). No ball at all still turns the copy off, as before.
- The copy's BG3VOFS is read as negative when its camera is just above the
  table's top (it wraps to ~470): only those rows are black, not the whole
  screen (this blacked the top screen for a frame at each flip up).

Measured on six 50-second games with random flipper presses (Ruby top,
bottom and catch states): the game's flips identical to the old build (the
game is never changed); no frame with a screen more than half black,
against 75 in 82 frames around the events of one game before; the live view
never dropped, against 1-4 times a game; the copy drifts a little more
often (10 against 8 retakes over the six games), likely because it's at its
full offset from the first frame.

Ideas from before the fix, kept for reference (a copy can now be placed
anywhere at once, which makes 2 and 4 cheaper; 1 is moot):
1. **Don't retake on a flip.** Ramp the copy from +160 to -160 (or back)
   without reloading it. 320 rows at 8 a frame is 40 frames. During the ramp
   the copy's rows land somewhere else on its screen: show what fits, black
   the rest. It's still a visible transition, but the copy stays in step and
   costs nothing extra. Try faster steps: 8 was safe and an instant 160-row
   jump drew garbage, but 16 or 24 a frame were never tested.
2. **Two copies, one above and one below, both always running.** A flip then
   just picks which one is shown, with no ramp. That's three cores (the game
   plus two), roughly 3.5 ms of emulation per frame on the host against 1.2
   for the game alone. The device held full speed with two cores; three is
   unknown. Each copy's camera differs from the game's, so both drift now and
   then (see below).
3. **Wider hysteresis**, or flip only at the very bottom (camera ≥ ~260) and
   back only near the top. That means fewer flips but more black rows past
   the table's end: up to 160 at the extremes. Rows past the end are garbage
   from the BG wrapping, so they must stay black.
4. **Don't flip at all; frame both screens from copies.** The game runs
   unseen, with two copies at `c` and `c+160`, `c = min(game row, 112)`. Same
   cost as 2, and it gives the owner's original framing (ball on the bottom
   screen only at the lowest point). The copies drift more because neither
   camera matches the game's.

## Scouting: a tall renderer, and the instant jump (Oct 26)

The owner asked whether a modified mGBA that renders the table 320 rows tall
from **one** core (no copies, so nothing to resync or drift) could replace
the second copy. Findings, from `tools/scout/` on `us-ruby-top`,
`us-ruby-bottom` and `us-ruby-catch`:

1. **Background: easy.** BG1 and BG2 are whole-table and fixed in VRAM.
   BG3 is streamed into a 22-row ring, but the whole table's BG3 tiles sit
   in EWRAM all the time, and the slot of any row is a formula. A renderer
   can draw any of the 432 rows on any frame with no change to the game.
2. **Raster effects: trivial.** Only BG0's scroll at line 144.
3. **Sprites: the hard part.** Objects off the game's screen get no OAM
   entries, and each object writes its own (220 call sites). Showing them
   in the extra rows means patching table objects one by one, and OAM's
   8-bit y can't reach rows 256-319 anyway.
4. **Lights: also gated.** They're drawn into the ring only while
   `S+0x6A` is in each light's range, so extra rows would show them stale.

**Verdict: the pure tall renderer is a large, risky patching job** (3 and 4
are where it would go round in circles). The background half alone is no
better than the copy, which already draws sprites and lights correctly.

**What the scouting found instead: the copy can jump.** Its "8 rows a frame"
limit was the game's own scroll easing. Before the copy's frame, when its
camera's tile row is more than 1 from `S+0x64`:
- refill the ring for rows `T`..`T+21` from EWRAM (`T` = camera row / 8,
  rows past 55 skipped) and set `S+0x64 = T`;
- set every eased copy of the scroll to the camera row: `0x0200B3AE`, `B3B2`,
  `B3B6`, BG1-3VOFS, `S+0x5A`, `S+0x68`, `S+0x6A`, `S+0xCC`, and
  `0x03007ED8` = row + 16. Missing `S+0x68` (`S+0x6A` is reloaded from it
  each frame) leaves wrong light tiles in the reused slots for good.

Result (`jump.c`, `us-ruby-bottom`, jumped 160 rows up, against a copy that
eased there and settled): BG1, BG2 and BG3 identical from the first frame,
sprites from the second (OAM lags a frame), the whole picture identical
once animation phase matches. This is now the app's retake: see "The
blackouts at every flip" above.

Next steps, in order:
1. The owner's verdict on the device.
2. Check the lights and sprites at a jump in catch mode and on Sapphire
   (the jump is only checked on Ruby's plain table so far).
3. Reconsider ideas 2 and 4 above: a copy can now be placed anywhere at once.

## How it works now (code map)

- `src/profiles.h` `tall` (on probes, `hudBg`, `rows`, `hudRow`, `live`: the
  addresses below); the BPPE profile in `src/profiles.c` (logo-only, `tall`,
  logo with `lightEdgeClear`).
- `src/game.c`:
  - `twin_*` near the top. `twin_open`: second `Emu`, camera stores NOPed,
    BG0 off. `twin_step` runs from `run_frame`, so it also runs on the
    frame-buffer worker thread: same keys, its camera set to the game's ±
    `twinOff`, `twin_jump` when its camera's tile row is more than one
    from its ring's, the ball compared, retaken on a mismatch (from the
    game's state before the frame, `twinPre`, saved in `run_frame`).
  - The shadow-OAM hiding is in `run_frame`, before the game's frame:
    `emu_hide_exact` on the copy.
  - `update_tall` (before `update_snapshot`): the flip, which picture goes
    on which screen, the score bar painted from the HUD split onto the bottom
    screen, black for rows outside the table or not in place.
  - The main loop's `if (tallLive) shownFb = ...` puts the right picture on
    the game screen.
- `src/emu.c`:
  - `emu_hide_exact` (hides sprites by exact attributes, per renderer).
  - `emu_hud_copy` and `emu_hud_copy_above`: the HUD split's layers also stay
    in the game picture (all of them, or only above a row).
  - `emu_write32`.
- `src/config.c`: `pinball_live` (default 1). Off means the game on one
  screen and the logo on the other.

## Facts about the game (USA, BPPE)

| What | Where / value |
|---|---|
| State pointer | `0x020314E0` (word) → S |
| Camera | `S+0x54` (24.8 fixed), row at `S+0x4E` |
| Camera stores, main routine | `0x0804DCFA` (str), `0x0804DD0A` (strh row) |
| Camera stores, catch-mode routine | `0x0804DA54`, `0x0804DA64` |
| Other camera writers, not per frame | `0x0804D728`, `0x0804D888`, `0x08035DA8` (jump-table data, not code) |
| Camera target limits | `0x02031520`+0x18 (lowest, `0x164` on Ruby and Sapphire = camera row 272), +0x1A (highest) |
| Camera range | rows 0-272; table 432 rows; BG3 is 256x512 and wraps (rows past 432 or below 0 are garbage) |
| BG scroll | BG3VOFS (`0x0400001E`); BG1 and BG2 follow it; BG3HOFS moves a little sideways |
| Score bar | BG0, screen-fixed, from row 144 (`hudRow`); also catch timer, message boxes |
| Ball | pointer at `S+0x1330`; x, y words at +0x34, +0x38 (24.8) |
| Shadow OAM | `0x03005000` (copied to OAM at the next vblank: the next frame's sprites) |
| Opening sweep | `0x020005FB` (byte) / `0x020005FC` (half) non-zero while the table's intro sweep runs |
| On a table | BG0CNT `0x8404` and BG3CNT `0x804B` |
| Flippers | sprites tiles 0-15 (left) and 16-31 (right), palette 0, prio 2; left = L or Left, right = R or A |
| Title logo | BG1 at frame 1440, own white striped background |
| BG layers on a table | BG0 map `0x2000`, chars `0x4000`; BG1 map `0x3000`, chars `0x8000` (uses `0xD800`+); BG2 map `0x1000`, chars `0x4000`; BG3 map `0x0000`, chars `0x8000` (the ring). BG1-3 are 256x512 |
| Fixed in VRAM | BG1's and BG2's maps and tiles, and BG3's map: never written on a table |
| BG3 tile ring | `0x06008000`-`0x0600D7FF`, 22 slots of 1 KB (32 tiles, one tile row). Table tile row R is always in slot `(R + 10) % 22`; BG3's map is laid out to match |
| BG3 tiles, whole table | in EWRAM, never changed on a table: rows 0-31 at `0x0200FBB0 + R*0x400`, rows 32-55 at `0x020030A0 + (R-32)*0x400` |
| Streaming | `0x08047030`: one DMA3 a frame when the scroll's tile row changes (vs `S+0x64`); table `0x086B0010` (4 halves a tile row: source and slot going down, source and slot going up). Rows `T`..`T+21` resident for tile row `T` |
| Eased scroll | the game moves its scroll toward the camera at most 8 rows a frame. Copies: BG1-3 VOFS shadows `0x0200B3AE`, `B3B2`, `B3B6`; `S+0x5A`, `S+0x68` (this frame's), `S+0x6A` (last frame's: the light gates), `S+0xCC`; `0x03007ED8` (+16, the main loop's, on the stack); and the BGxVOFS registers |
| Lights | small DMAs straight into the ring (`0x0804ECxx`-`0x0804F8xx`), called from `0x0804E814` by ranges of `S+0x6A` (generous margins). 20-40 tiles in view differ from EWRAM on a typical frame |
| Raster effects | only BG0HOFS/VOFS at line 144 (VCount IRQ, DISPSTAT `0x9029`) for the score bar. DMA1/2 are the sound FIFOs |
| Sprites | each object writes its own shadow-OAM entries (220 references to `0x03005000` in the ROM). Objects far off the screen are left out (flippers absent with the camera at the top), with a margin below |

## What was learned (don't redo)

- **Never change the game's camera or its limits.** The game treats things
  off its screen differently. Two cores given the same random input, one with
  the lowest camera target lowered (`0x02031538` → 186), part in catch mode
  after 25-40 s, though never on plain tables. Holding the game's camera up
  caused the owner's "catch resets to the reveal" loop. States saved by
  earlier test builds carry the lowered value (186), and `twin_step` puts
  `0x164` back.
- **The copy's camera can jump, but only with all of the game's scroll state
  set** (corrected Oct 26; earlier: "can't jump"). The camera variable is only
  a target: the game eases its scroll toward it at 8 rows a frame, and streams
  BG3's tiles and gates the lights by that scroll, not the camera. Setting
  the camera alone gives the old garbage. See "Scouting" for what to set.
- **The copy drifts from the game now and then.** Its camera differs, and
  things off its screen behave differently. On plain tables it held a minute
  or more in every test. In the owner's catch state it drifts once at about
  23 s (frame ~1411 with the `pp-us-ruby-catch` inputs). Drift is detected by
  the ball's position.
- **Screen-fixed things appear in both copies:** BG0 (score bar, timer,
  messages), banner sprites, and message letters. The letters are placed from
  the catch hole; on the copy they're 160 rows off screen and the hardware
  wraps them round. They're all hidden on the copy. BG0 is off, and sprites
  matching the game's shadow OAM (attributes 1 and 2 equal, size bits equal,
  any y) are hidden with `emu_hide_exact`. The screen darkening behind a
  message (BLDCNT brightness) is still on both screens.
- **Timing:** the copy roughly doubles emulation (1.2 → 2.3 ms a frame on the
  host). The owner reports full speed on the RG DS Plus.

## Tests and fixtures

- `tests/regress/pokemon-pinball.json`, 10 cases, all passing at `12f6e54`.
  Fixtures are in `../gba-ds-tests/states/pokemon-pinball/`:
  - `us-title`, `us-field-select`
  - `us-ruby-bottom`, `us-ruby-top`
  - `us-sapphire-start`: picking Sapphire; input A at frame 5 starts the table
  - `us-ruby-catch`: the owner's catch mode, device
  - `us-ruby-catch-loop`: the reveal-loop state, device, saved with the limit
    at 186
- ROM: `roms/BPPE.zip`.
- The owner's State Saver states: "Pinball: Table behavior" 1 and 2, and
  "Logo" (BPPE).
- Experiment tools are in `tools/scout/` (not built by the Makefile; usage
  at the top of each file):
  - `scout.c`: per-frame VRAM writes with the PCs that made them, mid-frame
    register writes, the ring against EWRAM, the shadow OAM.
  - `jump.c`: the two-core runner. A copy with its camera stores NOPed and
    held at an offset, each layer shot alone; `FILL=1` for the instant jump.
  - Build either: ``FL=$(make -n tools -B | grep -o -- '-O2.*-D_REENTRANT' | head -1)``,
    then `gcc $FL -Isrc -o build/host/jump tools/scout/jump.c src/emu.c src/mapcap.c src/profiles.c src/logo_masks.c src/logo.c tools/png.c deps/mgba/build-host/libmgba.a deps/mgba/build-host/zlib/libz.a -lpng -lpthread -lm`.
  - `llvm-objdump --triple=thumbv4t-none-eabi` on the ROM wrapped with
    `objcopy -I binary -O elf32-little --change-section-address .data=0x08000000`
    disassembles it.

## Not done

- **Bonus stages:** the copy only runs where the lowest camera target is
  `0x164`, so they play on one screen. Their tables and camera limits are
  unknown.
- **Other events** on the device (evolution, hatching, the bonus-stage
  entrances): unchecked.
- **Android:** `twin_*` is shared code; not tried on Android.

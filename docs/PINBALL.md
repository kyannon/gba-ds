# Pokémon Pinball (BPPE): status and handoff

Status as of Oct 26, 2026 (dev `12f6e54` and later). The player-facing rules are in
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

## The open problem (owner's report on the latest build)

**The copy catches up constantly: one screen or the other blacks out and
refills every time the ball crosses from one screen to the other.** That
happens by design in the current code, and it's the thing to fix next.

When the game's camera passes `flipAt` (176) or `flipBack` (112), the game
moves screens. `update_tall` then sets `twinRetake`, and `twin_step` reloads
the copy from the game's state and ramps its camera from 0 to ±160 rows at 8
rows a frame. That takes 20 frames, and the copy's screen is black where its
rows aren't in place yet. The ball crosses those rows often, so it's
constant.

Ideas, none tried yet:
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

## How it works now (code map)

- `src/profiles.h` `tall` (on probes, `hudBg`, `rows`, `hudRow`, `live`: the
  addresses below); the BPPE profile in `src/profiles.c` (logo-only, `tall`,
  logo with `lightEdgeClear`).
- `src/game.c`:
  - `twin_*` near the top. `twin_open`: second `Emu`, camera stores NOPed,
    BG0 off. `twin_step` runs from `run_frame`, so it also runs on the
    frame-buffer worker thread: same keys, its camera set to the game's ±
    `twinOff`, the ball compared, retaken on a mismatch.
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

## What was learned (don't redo)

- **Never change the game's camera or its limits.** The game treats things
  off its screen differently. Two cores given the same random input, one with
  the lowest camera target lowered (`0x02031538` → 186), part in catch mode
  after 25-40 s, though never on plain tables. Holding the game's camera up
  caused the owner's "catch resets to the reveal" loop. States saved by
  earlier test builds carry the lowered value (186), and `twin_step` puts
  `0x164` back.
- **The copy's camera can't jump.** The game streams the table's tiles as
  the camera scrolls, so a 160-row jump draws garbage. Ramping 8 rows a frame
  works. Loading the game's state into the copy gives it the game's VRAM,
  hence the ramp from offset 0.
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
- Experiment tools from this session lived in a scratch folder and are gone.
  To rebuild one:
  - A two-core runner: open two `Emu`s, load the same state, NOP the copy's
    camera stores, set its camera each frame, compare ball positions, dump
    PPMs of both. About 80 lines against `src/emu.c`; build like the harness
    (`Makefile` `tools` target flags plus `-Isrc`).
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

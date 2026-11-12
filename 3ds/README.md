# GBA DS for the New Nintendo 3DS

GBA DS (the game on one screen, its own map, HUD or logo on the other) on the
3DS's two screens, with [armGBA](https://github.com/ratherDashing/armGBA) and
its ARM recompiler in place of libmgba. The game is on the top screen; the
bottom screen is the second screen and the touch screen.

It is the same app: `src/` is built as it is (the game list, the menus, the
per-game profiles, map captures on a hidden copy of the game). What differs
is underneath:

| | other systems | 3DS |
|---|---|---|
| emulator | libmgba (`src/emu.c`) | armGBA (`emu/emu_armgba.c`, the same `src/emu.h`) |
| screens, input, sound | SDL2 | `ctr/sdl_ctr.c`: the part of SDL2 the app uses, on the GPU (citro2d), hid and ndsp |
| threads | pthreads | `ctr/host.c`: the app's threads on the New 3DS's fourth core |

## Building

No devkitPro needed on the machine: the builds run in the
`devkitpro/devkitarm` Docker image.

    git submodule update --init third_party/armgba
    make -C 3ds 3ds      # 3ds/ctr/gba-ds.3dsx
    make -C 3ds cia      # 3ds/ctr/gba-ds.cia (makerom and bannertool in ~/3ds-tools/bin)

## On the console

- `gba-ds.3dsx` in `sdmc:/3ds/`, or install `gba-ds.cia`. A New 3DS or New
  2DS (the recompiler's and the hidden copy's threads use its extra cores; an
  Old 3DS starts it, far too slowly to play).
- Games (`.gba`, or one in a `.zip`) in `sdmc:/roms/gba/`. Another
  folder: `rom_dir` in `sdmc:/3ds/gba-ds/gba-ds.cfg`, which is written with
  the 3DS's defaults the first time the app runs.
- Only games GBA DS has second-screen support for are listed, as everywhere;
  `show_all = 1` in `gba-ds.cfg` lists every game.
- Sound needs the DSP firmware dump (`sdmc:/3ds/dspfirm.cdc`), as all 3DS
  homebrew does.
- Saves are beside the ROMs (`.sav`), save states and settings in
  `sdmc:/3ds/gba-ds/states/`, the log in `sdmc:/3ds/gba-ds/log.txt` (the run
  before it: `log-prev.txt`).

Buttons: A, B, L, R, START, SELECT and the D-pad (or the Circle Pad) are the
GBA's. **ZL** is GBA DS's menu button (its HOME: the pause menu, and held
with another button, the hotkeys: ZL + R saves a state, ZL + L loads it, ZL +
X the quick menu, ZL + Y swaps the screens, ZL + START closes the game).
**ZR** is fast-forward, without sound (`ff_mute = 0` in `gba-ds.cfg` plays
it sped up). The menus also work by touch.

The first time a folder of games is read, each file is opened once for its
game code ("Reading games" on the bottom screen: a few seconds per hundred
files); the codes are kept in `states/archives.cache`, and later starts list
the folder at once. Each supported game's logo is made the first time, in the
background, by running its title screen: the list is slower until those are
done.

## Logs over Wi-Fi

As armGBA-3DS and PokeDaisy do it: a diagnosis build (never a release) POSTs
its logs to a machine on the local network, so the SD card stays in.

    python3 3ds/tools/logserver.py                       # listens on port 8377, writes build/logs/
    touch 3ds/ctr/host.c
    make -C 3ds 3ds LOG_URL=http://<that machine's address>:8377/log

It sends the last run's log when the app starts (all of it, if that run
crashed), the log so far each time the game list comes up, and this run's
when the app closes. Build without `LOG_URL` again
(after `touch 3ds/ctr/host.c`) for anything given to someone else.

## What isn't there

- Rewind (a state is 0.8 MiB, and the memory is the game's and the
  recompiler's), RetroAchievements, the updater, `.7z` archives, the bug
  report's ten seconds of history.
- The game list's notes on romhacks and modified ROMs (the checksum means
  reading each game whole).
- Map screens that move keep 4 frames, not 16.
- Save states are armGBA's, not mGBA's: states from GBA DS on another system
  don't load here, nor these there. Battery saves (`.sav`) are the same.
- Screenshots of the screens as drawn (the GPU's picture isn't read back).

## Memory

A New 3DS gives homebrew 64 MiB: about 46 MiB of heap here, after 12 MiB of
linear memory for the GPU and the sound. The game's ROM, the recompiler's
code (what is left after the ROM and 20 MiB kept for the rest, up to 16 MiB),
the hidden copy and its code (up to 4 MiB) share it. The emulator tests run
with the same heap, not the 124 MiB an emulator would give.

## What has and hasn't been tested

On a New 3DS (2026-11-12, one console): the game list with 124 files,
Castlevania: Circle of the Moon at 60 frames a second with its map on the
bottom screen, save states and fast-forward.

In the Azahar emulator (`make -C 3ds azahar-test ROMS=...`) and on the desktop
build (`make -C 3ds sim`), with Pokémon Emerald and a profile made for the
purpose (`GBADS_TEST_PROFILE` in `src/profiles.c`): the menus' options (scale,
shader, color correction, LCD ghosting, overlays, frame buffer, swapped
screens, screenshots), save states, a map capture on the hidden copy, and the
HUD split (desktop only).

Not tested: **the other 55 profiles**, and a 32 MiB game with a hidden copy
(there is not the memory for both: the game runs, the second screen may not).
Where a profile depends on exactly how mGBA draws or times something, armGBA
may differ.

## armGBA

`third_party/armgba` is armGBA as it is (its `3ds-v0.0.3-alpha` release).
What GBA DS uses of it that other frontends don't:

- `struct RenderHooks` (`src/video/render.h`): layers and sprites left out
  of the picture, a callback before each line, and a second picture drawn by
  a renderer of its own beside the first (the HUD split). Kept beside the
  machine, not in it: states are unchanged.
- `videoDrawSkipped` per thread (the hidden copy runs frames it doesn't draw
  on a thread of its own).

`ctr/jitmem.c` is armGBA's `platform/3ds/source/jitmem.c` with code memory
for several machines at once (the game: up to 16 MiB; each copy: up to 4).

## Files

    emu/emu_armgba.c   src/emu.h on armGBA
    emu/armgba.mk      armGBA's sources, for both builds
    ctr/               the 3DS host: SDL's part (sdl/SDL.h, sdl_ctr.c), host.c, jitmem.c, the CIA's parts
    sim/               the desktop test build
    test/              the Azahar test's script and SD card files

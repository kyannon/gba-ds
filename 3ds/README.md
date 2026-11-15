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
  `show_all = 1` in `gba-ds.cfg` lists every game. A game is known by the
  four-letter code in its header: a translation or romhack that kept the
  original's code is listed (and treated as that release), one with a code of
  its own only with `show_all = 1`, on one screen.
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
- Lady Sia's game screen as GBA DS 1.5.3 had it: a level's sprite that has a
  HUD piece's place and shape (the first village's well) can be left out of
  the game screen with the HUD. GBA DS 1.5.4 tells them apart by where they
  are in the sprite table (`emu_hide_grouped`), which armGBA's render hook
  doesn't give.
- The pinball games' table over both screens (Pokémon Pinball, Sonic Pinball
  Party, Muppet Pinball Mayhem): it runs up to three copies of the game a frame,
  far beyond a New 3DS (0.0.1-alpha ran out of memory opening it and froze).
  They play on the top screen with their logo on the bottom one, whatever
  `pinball_live` says.

## Memory

A New 3DS gives homebrew 64 MiB: about 46 MiB of heap here, after 12 MiB of
linear memory for the GPU and the sound. The game's ROM, the recompiler's
code (what is left after the ROM and 20 MiB kept for the rest, up to 16 MiB),
the hidden copy and its code (up to 4 MiB) share it. The emulator tests run
with the same heap, not the 124 MiB an emulator would give.

## What has and hasn't been tested

On a New 3DS (2026-11-12, one console): the game list with 124 files,
Castlevania: Circle of the Moon at 60 frames a second with its map on the
bottom screen, save states and fast-forward. Pokémon Pinball froze on its table
with 0.0.1-alpha; 0.0.2-alpha's fix (see "What isn't there") has been checked in
Azahar, not yet on a console.

Every supported game, on the desktop build of the app on armGBA
(`3ds/sim/app.mk`) against the same app on libmgba, with
`3ds/test/dualrun.py` and a script per game in `3ds/test/play/` (2026-11-15,
GBA DS 1.5.4-dev). Most scripts get into play (a level, a race, a town or a
battle); some stop at a name entry, an intro or an attract demo (the Dragon Ball
Z games, Fire Emblem, Golden Sun: The Lost Age, Sword of Mana, Sonic Advance 1
and 3, Tony Hawk's Pro Skater 3), so their games' in-play rules weren't seen.
Each opens GBA DS's menu, saves and loads a state and swaps the screens. In all of them the app on armGBA
draws both screens exactly the same with and without its render thread, and
its second screen shows what it does on libmgba (the same captures succeed,
within a capture or two where the two emulators' runs drift a few frames
apart). Two games' second screens were wrong on armGBA and are fixed (Harry
Potter and the Prisoner of Azkaban's, Super Mario Advance's). In Azahar: the
game list, Pokémon Pinball on its table, and the menus' options.

Not tested on a console: most games' speed. Where a profile depends on exactly
how mGBA times something, armGBA may differ: the two emulators drift a few
frames apart within a minute of play (a game's timing, not its picture), so a
second screen that changes on a particular frame may change a frame or two
later. A 32 MiB game with a hidden copy hasn't been tried (there may not be the
memory for both: the game runs, the second screen may not).

## armGBA

`third_party/armgba` is armGBA-3DS: armGBA's main with the 3DS port on top
(ratherDashing/armGBA).
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

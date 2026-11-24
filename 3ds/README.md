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
- The game is drawn on two threads (the third and fourth cores, each every
  other line), as armGBA-3DS does, one frame behind the game, so the game
  never waits for the drawing. `render_workers = 1` in `gba-ds.cfg` draws
  it on one. The fourth core is also the hidden copy's (map captures), a
  step below.
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
- The pinball games' table over both screens (Pokémon Pinball, Sonic Pinball
  Party, Muppet Pinball Mayhem): it runs up to three copies of the game a frame,
  far beyond a New 3DS (0.0.1-alpha ran out of memory opening it and froze).
  They play on the top screen with their logo on the bottom one, whatever
  `pinball_live` says.

## Memory

A New 3DS gives homebrew 64 MiB: about 46 MiB of heap here, after 12 MiB of
linear memory for the GPU and the sound. The game's ROM, the recompiler's
code (what is left after the ROM and 15 MiB kept for the rest, up to 16 MiB:
about 13 MiB with Mario Kart; 22 MiB kept for a game with a hidden copy:
about 8 MiB with Yoshi's Island), the hidden copy and its code (up to 4 MiB)
share it. When the heap runs low, each machine's recompiler empties its own
code memory (`jitHostMemoryLow`, counted per thread). The emulator tests run
with the same heap, not the 124 MiB an emulator would give.

## What has and hasn't been tested

Every supported game (2026-11-16), on the desktop build drawn as the 3DS draws
(`3ds/test/dualrun.py --runs mgba,armgba-3ds`: two threads, a frame late, the
HUD split one frame in five) against GBA DS on libmgba: each game's script in
`3ds/test/play/` plays from power-on to its second screen (the map, status
screen or HUD its rules describe; Dragon Ball Z: The Legacy of Goku II's
Scouter through the game's Debug menu, `--cheats 3ds/test/cheats`; Mother 3 run
with the English translation's ROM by path), and the second screens match.
Fire Emblem: Fuuin no Tsurugi, new in GBA DS since, hasn't a script yet.

On a New 3DS (2026-11-16, one console): Mario Kart: Super Circuit's races
at 60 frames a second, several in a row (Peach Circuit, Boo Lake and others),
with the HUD and the course map on the bottom screen; the speed work is below.

On a New 3DS (2026-11-12, one console): the game list with 124 files,
Castlevania: Circle of the Moon at 60 frames a second with its map on the
bottom screen, save states and fast-forward. Pokémon Pinball froze on its table
with 0.0.1-alpha; 0.0.2-alpha's fix (see "What isn't there") has been checked in
Azahar, not yet on a console.

0.0.7-alpha (2026-11-24: GBA DS 1.6.2-dev and armGBA-3DS 1.0, which is
upstream armGBA 2.0's core) has not been on a console yet. On the desktop:
all 57 play scripts ran to the end on libmgba and on armGBA drawn as the 3DS
draws (`3ds/test/dualrun.py --runs mgba,armgba-3ds`), with the two emulators'
screens as close as before the merge, shot for shot, but for a few scenes
where their timing drifts (a fade or a flash caught a frame apart) and the
scripts' swap step, which the newer menu's layout moved for both. In Azahar:
the game list, Mario Kart: Super Circuit into its menus at 59.8 frames a
second with no sound underrun, and the pause menu.

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

## Speed (Mario Kart)

Mario Kart: Super Circuit is the heaviest game on the 3DS: the game, its HUD
split (a second picture of every frame) and the recompiler's new code. What
gets its races to 60 frames a second on a New 3DS:

- The game drawn on two threads, one frame behind the game, so the game never
  waits for them; the HUD's picture drawn one frame in six (10 a second), and
  made and uploaded a third at a time over three frames; the pieces that go
  back on the game screen (the place, the item box) copied every frame.
- Textures uploaded by the GPU's copy engine, or by the CPU without a
  division per pixel; the HUD's two views on one texture.
- The recompiler: a warm list per game (`sdmc:/3ds/gba-ds/warm/`, the code
  the game ran, compiled in the frames' spare time next time, up to 20% of the
  code memory); at most 1.5 ms of compiling in a frame; the code memory
  emptied between races once it's 75% full (when it's full the recompiler
  empties it itself, a 100-300 ms frame). armGBA's side: no blocks compiled
  from where an interrupt returns or an event falls, careful copies only
  for blocks events fall in often and in at most a third of the code
  memory, and a full part of a stretch's leftover room used as a stretch of
  its own; a race's code levels off at about 8 of its 13 MiB.
- The game's battery save written to the SD card on a thread of its own.
- The HUD's backdrop scaled in whole numbers (floats took 76 ms).

A race still has the odd frame of 18-22 ms here and there, in busy stretches
(about 9 ms of emulating on average, the HUD 2-4, drawing 2.5).

## Speed (Yoshi's Island)

Yoshi's Island's status screen is captured on the hidden copy every 3
seconds on the 3DS (`map_interval` 180; every second elsewhere): every
second, the captures kept that copy busy most of the time and slowed the
game. Its events fall inside its code so often that careful copies took half
of what it compiled, and the code memory filled every 20-40 s; with armGBA
0.0.6's changes, 4 minutes ran with no frame over 100 ms after starting.

Pictures go to the GPU's copy engine through two buffers in turn
(`SDL_UpdateTexture`): with one, the game's picture was written over the
second screen's while the copy was still reading it (a torn status screen on
the console; Azahar's copies finish at once).

## armGBA

`third_party/armgba` is armGBA-3DS 1.0: upstream armGBA 2.0's core, file for
file, with the 3DS port's `platform/3ds` on top (ratherDashing/armGBA). Everything
below that GBA DS needs of the core is in upstream armGBA now.
What GBA DS uses of it that other frontends don't:

- `struct RenderHooks` (`src/video/render.h`): layers and sprites left out
  of the picture, a callback before each line, and a second picture drawn by
  a renderer of its own beside the first (the HUD split). Kept beside the
  machine, not in it: states are unchanged. The second picture can be drawn
  one frame in `secondEvery` (`secondCount` says when there's a new one).
- `videoSetDrawSkipped` per thread (the hidden copy runs frames it doesn't draw
  on a thread of its own).

`ctr/jitmem.c` is armGBA's `platform/3ds/source/jitmem.c` with code memory
for several machines at once (the game: up to 16 MiB; each copy: up to 4).

## Files

    emu/emu_armgba.c   src/emu.h on armGBA
    emu/armgba.mk      armGBA's sources, for both builds
    ctr/               the 3DS host: SDL's part (sdl/SDL.h, sdl_ctr.c), host.c, jitmem.c, the CIA's parts
    sim/               the desktop test build
    test/              the Azahar test's script and SD card files

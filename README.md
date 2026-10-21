# GBA DS

A Game Boy Advance emulator for dual-screen handhelds (built for the Anbernic RG DS Plus, on ROCKNIX and on Android with GammaOS). The game runs on one screen, and the game's own map stays up on the other screen and keeps itself current as you explore, the way the DS Castlevanias work.

The idea here is "what if the Game Boy Advance had a second screen" -- part of what this means is that, at least for now, neither screen is treated like a touchscreen. Instead the secondary screen is used similarly to how the DS Castlevania games use it -- displaying maps or other secondary information to make the gameplay experience a little more pleasant on a device with the space to accommodate it. (This whole project was inspired by playing through all the DS Castlevania games and then feeling a little sad about the prospect of going back in time to the GBA games...)

<p>
<img src="docs/fusion.png" width="19%" alt="Metroid Fusion with its map">
<img src="docs/cotm.png" width="19%" alt="Castlevania: Circle of the Moon with its 99.8% map">
<img src="docs/mario-kart-race.png" width="19%" alt="Mario Kart: Super Circuit with its race HUD on the second screen">
<img src="docs/sma2.png" width="19%" alt="Super Mario Advance 2: Super Mario World with the island's level list on the second screen">
<img src="docs/thps2.png" width="19%" alt="Tony Hawk's Pro Skater 2 with the goal sheet on the second screen">
</p>

<sub>Stacked screens, pixel-perfect scaling, LCD grid on.</sub>

| Game (USA, Europe and Japan) | Second screen |
|---|---|
| Castlevania: Aria of Sorrow | map |
| Castlevania: Harmony of Dissonance | map |
| Castlevania: Circle of the Moon | map, with the completion rate |
| Metroid: Zero Mission | map |
| Metroid Fusion | map |
| The Legend of Zelda: A Link to the Past | world and dungeon maps |
| Tony Hawk's Pro Skater 2 (incl. SK8, Japan) | the goal sheet for the current run; optional four-button layout |
| Mario Kart: Super Circuit (incl. Mario Kart Advance, Japan) | the race HUD, with a large course map; results and the cup podium |
| Tony Hawk's Pro Skater 3 (incl. French and German) | the goal list during a run; four-button layout, or Auto (four buttons while skating) |
| Super Mario Advance 2: Super Mario World | the SELECT menu's level list on the world map; the status bar in levels; optional SNES layout |
| Final Fantasy Tactics Advance | in battles and their scenes: dialogue, menus and unit details; on the world map: notices, funds and the date |
| Advance Wars (USA; preliminary) | in battles: the unit list with the unit under the cursor highlighted, or the battle intel, with the minimap below |
| Fire Emblem (USA; preliminary) | on the map: the unit, terrain and goal windows in fixed corners, and the minimap |

Any other GBA game also runs; it just plays on one screen. See [Supported games](https://github.com/kyannon/gba-ds/wiki/Supported-games) for what each game does.

**Documentation is in the [wiki](https://github.com/kyannon/gba-ds/wiki):** [Playing](https://github.com/kyannon/gba-ds/wiki/Playing) · [Supported games](https://github.com/kyannon/gba-ds/wiki/Supported-games) · [Map screen surrounds and overlays](https://github.com/kyannon/gba-ds/wiki/Map-screen-surrounds-and-overlays) · [Settings](https://github.com/kyannon/gba-ds/wiki/Settings) · [Troubleshooting](https://github.com/kyannon/gba-ds/wiki/Troubleshooting) · [Adding second-screen compatibility for a game](https://github.com/kyannon/gba-ds/wiki/Adding-second-screen-compatibility-for-a-game)

## How it works

Emulation is [libmgba](https://mgba.io). GBA DS runs a second, silent copy of the emulator next to the real one. About every 1.5 seconds (adjustable: **Map Refresh** in the pause menu), and right after each room transition (detected as a fade to black), it does the following:

1. It copies the live game's state into the silent copy. The game you're playing is never paused or touched.
2. It presses the map button there, waits for the map screen to appear and settle, then records a short loop of frames so the blinking or pulsing player marker still animates.
3. It checks the result two ways before showing it: a memory flag that is set only while the map screen is open, and fixed pixels of the map screen's frame or background. Title screens, cutscenes, menus, and fades are rejected, and the previous map stays up.

You see the real in-game map with every detail the game draws (explored rooms, save and warp rooms, completion %, area names) at 4× integer scale. Circle of the Moon's map screen has no completion rate, so GBA DS copies the percentage shown after "RATE" on the game's pause screen onto the map, in the top-right corner, which stays clear of rooms even on a fully explored castle (the bottom-right corner is the fallback).

Games without a map use the second screen in other ways:

- **Mario Kart: Super Circuit** draws each frame twice: once without the HUD for the game screen, and once with only the HUD (coins, laps, time, the top four, the course map) for the second screen, so nothing behind the HUD is lost. The item box and your place are put back in the game screen's corners, and the course map is drawn twice as big over a dimmed picture of the course's scenery.
- **Tony Hawk's Pro Skater 2** copies the goal sheet for the current run to the second screen when the game shows it, and keeps it there while you skate.

Until there's something to show, the second screen shows the game's logo, extracted from the ROM in the background the first time a game appears in the list.

## Install

Download the latest release from the [Releases page](https://github.com/kyannon/gba-ds/releases), then follow the guide for your system:

- **[ROCKNIX](https://github.com/kyannon/gba-ds/wiki/Install-on-ROCKNIX)** (stock ROCKNIX or ROCKNIXDS): unzip `gba-ds-v…-linux-aarch64.zip` into `/storage/roms/ports/` and start **GBA DS** from Ports. The guide also covers the Ports icon, starting games from EmulationStation's Game Boy Advance list, and upgrading.
- **[BananaOS](https://github.com/kyannon/gba-ds/wiki/Install-on-BananaOS)**: the same zip, unzipped into the card's `roms/ports`; games in `roms/gba`.
- **[Android](https://github.com/kyannon/gba-ds/wiki/Install-on-Android)** (GammaOS): install `gba-ds-v…-android.apk`, allow All files access, and put your ROMs in `ROMs/gba`. To start games from GammaOS, Daijisho, ES-DE or another frontend, see [Launching from Android frontends](https://github.com/kyannon/gba-ds/wiki/Launching-from-Android-frontends).

## Building

On an x86_64 Linux host with git, cmake, make, and zip, run:

```sh
scripts/build-rocknix.sh            # -> dist/gba-ds-v<VERSION>-linux-aarch64.zip
```

The script fetches a GCC aarch64 toolchain, mGBA (pinned commit), and SDL2 into `deps/`.

To build for the host so you can develop and test (needs libpng-dev too):

```sh
scripts/setup-host.sh   # first time: mGBA and SDL2 into deps/, then builds
make && make tools      # afterwards: build/host/gba-ds, build/host/harness
```

Run it headless and drive it from a script (button combos, `HOME`, `TAP:x,y`, `SHOT:file.bmp`, `QUIT`; see `--help`):

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy GBADS_WINDOW=640x960 \
  build/host/gba-ds --rom-dir=roms --state-dir=/tmp/st --input-script=test.txt
```

To add second-screen support for another game, see [Adding second-screen compatibility for a game](https://github.com/kyannon/gba-ds/wiki/Adding-second-screen-compatibility-for-a-game).

## Roadmap

- RetroAchievements hardcore mode (RetroAchievements allows it once GBA DS has been public for six months)
- More games' HUDs on the second screen
- More games!

## AI Disclosure

GBA DS is written almost entirely by AI. The code is written by Claude, Anthropic's AI model, working in Claude Code sessions, and so are the documentation, the changelog and the commit messages. I decide what gets built and how it should behave, and I test it.

**How a change gets made**

1. **I ask for something,** like a new game, a bug fix or a change to how something looks or feels. I usually include save states, screenshots or photos of the device showing what I mean.
2. **Claude writes the change** and checks it on a desktop build before I see it. GBA DS can run without a screen and play scripted button presses from a save state, so Claude takes screenshots of both screens and looks at them before sending me anything. For new games it also uses a test harness that steps the emulator frame by frame and reads the game's memory.
3. **I test every build on real hardware.** That's an Anbernic RG DS Plus, on ROCKNIX and on Android (GammaOS). Nothing goes into a release until I've played it on the device and confirmed it works and that what already worked still does. When something's wrong, I send photos or screenshots back and we go around again. Some changes take several rounds before they're right.
4. **Releases are my call.** I decide when a version is ready and create each GitHub release myself. The ROCKNIX zip is built from a fresh copy of the release code. The Android APK is built and signed by GitHub Actions, not on anyone's computer.

**What you can see in the history:** since October 16, 2026, each commit shows "kyannon authored and Claude committed", with a Verified badge. "Authored" means I asked for and approved the change; "committed" means Claude wrote it. Earlier commits show me alone, but they were made the same way.

**What isn't AI-generated:** the game logos, map borders and second-screen art are extracted from your own ROMs while the app runs, not drawn by AI or shipped with the app. Emulation itself is [mGBA](https://mgba.io)'s, a long-established, human-written emulator, which GBA DS builds on.

**What this means for you:** I can't promise there are no bugs, but every feature in a release has been used on the device it was made for. A feature that can't be tested on my hardware is labeled *experimental* (for example, starting games from some Android frontends). If something doesn't work for you, please [open an issue](https://github.com/kyannon/gba-ds/issues); it gets tested on hardware the same way.

## Licence

GBA DS is free software: you can redistribute it and/or modify it under the terms of the [GNU General Public License](LICENSE), version 3, as published by the Free Software Foundation. It is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the [GNU General Public License](LICENSE) for more details.

The parts of GBA DS made by others keep their own licences, all compatible with the GPL:

GBA DS statically links libmgba, which is © Jeffrey Pfau and licensed under MPL-2.0. The mGBA source is at <https://github.com/mgba-emu/mgba> (pinned commit in `scripts/build-rocknix.sh`).

The interface text is set in [Jost](https://github.com/indestructible-type/Jost) (© The Jost Project Authors), bundled under the SIL Open Font License 1.1 (`assets/fonts/Jost-OFL.txt`), and drawn with [stb_truetype](https://github.com/nothings/stb) (public domain).

RetroAchievements support uses [rcheevos](https://github.com/RetroAchievements/rcheevos) (© RetroAchievements, MIT licence), vendored in `third_party/rcheevos`.

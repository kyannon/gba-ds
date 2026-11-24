# Game Boy and Game Boy Color games (issue #77)

Status: **in progress** on the branch `issue-77`. Phase 1 of the roadmap (`claude/issue-77-gb-gbc-roadmap.md` in the project) is done: the GB core is built in and a GB / GBC ROM starts and plays. The game list part of Phase 2 is done too (so a game can be started from the device's menu). The idle grey second screen (GB-3) is done, and Phase 3's first step: Game Boy size and scale (GB-2, GB-2a). Not done yet: the rest of Phase 2 (dump checksums, other launchers), the rest of the display options (Phase 3: palettes, colour correction, ghosting, the dot-matrix look; the plan is `claude/issue-77-display-options-proposal.md` in the project), saves / cheats / RetroArch states (Phase 4), ES-DE / BananaOS / Android launch, RetroAchievements and the wiki (Phase 5).

This is an extension of the idea behind GBA DS (wiki/Philosophy.md, principle 1: a Game Boy Advance with two screens). A real GBA played Game Boy and Game Boy Color cartridges itself, so GBA DS does too: **on one screen first**, with the other idle. These are the platform's rules, in the player's terms; the rule comes first and the code follows it. They sit beside the [general rules](games/README.md#general-rules), which hold for these games as well, so far as they apply.

| | |
|---|---|
| Games | any `.gb` / `.gbc` ROM mGBA's GB core runs (no per-game profiles) |
| Code | `src/emu.c` (`emu_open`: the core's platform, the model and colour defaults, the picture's place; `gba_of`: the GBA-only parts skip GB games) |
| Test ROMs | `tools/make_gb_test_rom.py OUTDIR` makes `gbads-test.gb` and `gbads-test.gbc` (stripes, scrolling, a tone, A inverts the DMG palette), no copyrighted code |
| Tests | none yet (Phase 5; GB cases with Phase 3's first step) |

## Rules

**GB-1. Played as the GBA played them.** A Game Boy or Game Boy Color game runs on the Game Boy Color hardware inside the GBA (mGBA's `AGB` model), whatever the cartridge: not as a plain Game Boy, and not as a Super Game Boy with its border. A game made for the plain Game Boy shows in the colours a Game Boy Color gave it (mGBA's table of the colour boot ROM's palettes; a game it doesn't list is in shades of grey). Later there will be settings to play as the Game Boy Color, the Super Game Boy or the plain Game Boy, with their colours and shaders: they are these same mGBA options (`gb.model`, `sgb.model`, `cgb.model`, `gb.colors`, `sgb.borders`).

**GB-2. The picture.** By default the game's 160x144 picture is shown at the GBA's own pixel size (one game pixel to one GBA pixel, like every GBA game, GEN-5), centred in the 240x160 screen; the rest of the screen is black ("GBA frame"). **Game Boy screen** (Display > Game Boy: Size) is an exception to the philosophy's first principle, asked for by the owner: the 160x144 picture is the whole frame, as a Game Boy's own screen, and Game Boy scale decides how it fills the screen (below). The GBA frame stays the default and is never changed by the other.

**GB-2a. Game Boy scale.** With the Game Boy screen size, the picture fills the game's screen one of three ways: *Pixel Perfect* (the largest whole number of screen pixels to a game pixel that fits: 5x on the RG DS Plus's 1024x768 screens, 3x on the RG DS's 640x480), *Fit* (as large as fits with the picture's own 10:9 shape: 853x768 and 533x480) or *Stretch* (the whole screen, so wider than the Game Boy's own shape). There is no Zoom: it would be the same as Fit on every landscape screen. The shader (the LCD grid and the others), the generated bezel, overlays, the FPS counter and messages follow the picture's size and place; a game pixel is a grid cell. The GBA frame's scale (Display > Game screen scale) is not used while the Game Boy screen is. The second screen stays as GB-3 says, in either size. The quick bar, the pause menu and touch work in the GBA frame's place as before.

**GB-3. The second screen.** The game is on one screen only. The second screen is idle: dark grey, as GEN-1 says for a screen with nothing live to show, with no logo (logos come later). It is drawn as the logo's idle screen is (the same picture size, scaling and LCD grid), so its edges match the game screen's (`game.c` `make_logo_canvas`, `draw_game`); with one screen there is nothing to show. *Not yet covered by a regression case (the fixtures have no GB ROMs yet): checked by looking and by the pixel value, 303030.*

**GB-4. Not supported means "single screen only".** A Game Boy / Game Boy Color game is listed like a GBA game with no second-screen profile: only with "Show all" on, marked "Single screen only: plays normally". "Supported" means a game with dual-screen support, not a game the emulator can run. The list takes `.gb`, `.gbc` (and `.sgb`, `.dmg`, `.cgb`) files and the same inside a `.zip` / `.7z`. A games folder named `gba` has its `gb` and `gbc` folders beside it read the same way (subfolders too, in the folder's own letter case: `GBA` / `GB` / `GBC`), so those games don't have to be moved into the GBA folder; nothing to set up, and a folder that isn't there is simply skipped (`library.c` `scan_siblings`).

**GB-5. The game is never touched.** As GEN-2: nothing is paused, sent input or changed for the second screen. (There is nothing on the second screen to capture yet.)

## Known limitations (so far)

- The Game Boy size and scale rows sit at the top of the pause menu's Display page for Game Boy games, and in Settings > Display (they have no effect on GBA games). Scale is shown even while the size is the GBA frame. Screenshots (Take Screenshot) and the quick bar's pictures still use the GBA frame.
- ES-DE / BananaOS / Android launchers don't hand `.gb` / `.gbc` games to GBA DS yet (Phase 5): start them from GBA DS's own game list.
- A Game Boy game has no 4-letter code like a GBA game's, so it gets its own from its header: `g` (plain Game Boy) or `c` (works on the Game Boy Color) and 3 characters, in lower case (`src/emu.c` `emu_rom_code`). The patch folder is named from its title and that code.
- The ROM checksum note under the list's logo ("Modified ROM") has no list of Game Boy dumps to compare with yet.
- Battery saves, RTC games (Pokémon Gold / Silver / Crystal), cheats, RetroAchievements, RetroArch's shared states: not checked or not available yet. Rumble, the Game Boy Camera, the link cable and Super Game Boy borders are not planned.
- mGBA's Game Boy core is mGBA's: timing-sensitive games may show problems that belong to it.

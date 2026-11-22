# Game Boy and Game Boy Color games (issue #77)

Status: **in progress** on the branch `issue-77`. Phase 1 of the roadmap (`claude/issue-77-gb-gbc-roadmap.md` in the project) is done: the GB core is built in and a GB / GBC ROM starts and plays when given to GBA DS by path. Not done yet: the game list (Phase 2), the idle second screen and display options (Phase 3), saves / cheats / RetroArch states (Phase 4), ES-DE / BananaOS / Android launch, RetroAchievements and the wiki (Phase 5).

This is an extension of the idea behind GBA DS (wiki/Philosophy.md, principle 1: a Game Boy Advance with two screens). A real GBA played Game Boy and Game Boy Color cartridges itself, so GBA DS does too: **on one screen first**, with the other idle. These are the platform's rules, in the player's terms; the rule comes first and the code follows it. They sit beside the [general rules](games/README.md#general-rules), which hold for these games as well, so far as they apply.

| | |
|---|---|
| Games | any `.gb` / `.gbc` ROM mGBA's GB core runs (no per-game profiles) |
| Code | `src/emu.c` (`emu_open`: the core's platform, the model and colour defaults, the picture's place; `gba_of`: the GBA-only parts skip GB games) |
| Test ROMs | `tools/make_gb_test_rom.py OUTDIR` makes `gbads-test.gb` and `gbads-test.gbc` (stripes, scrolling, a tone, A inverts the DMG palette), no copyrighted code |
| Tests | none yet (Phase 5) |

## Rules

**GB-1. Played as the GBA played them.** A Game Boy or Game Boy Color game runs on the Game Boy Color hardware inside the GBA (mGBA's `AGB` model), whatever the cartridge: not as a plain Game Boy, and not as a Super Game Boy with its border. A game made for the plain Game Boy shows in the colours a Game Boy Color gave it (mGBA's table of the colour boot ROM's palettes; a game it doesn't list is in shades of grey). Later there will be settings to play as the Game Boy Color, the Super Game Boy or the plain Game Boy, with their colours and shaders: they are these same mGBA options (`gb.model`, `sgb.model`, `cgb.model`, `gb.colors`, `sgb.borders`).

**GB-2. The picture.** The game's 160x144 picture is shown at the GBA's own pixel size (one game pixel to one GBA pixel, like every GBA game, GEN-5), centred in the 240x160 screen; the rest of the screen is black. The owner expects to change the size, the stretch to the whole screen the GBA offered and the shaders after trying this on the device. *Not decided yet: the stretch option.*

**GB-3. The second screen.** The game is on one screen only. The second screen is idle: dark grey, as GEN-1 says for a screen with nothing live to show, with no logo (logos come later). *Until Phase 3 it is plain black.*

**GB-4. Not supported means "single screen only".** A Game Boy / Game Boy Color game is listed like a GBA game with no second-screen profile: only with "Show all" on, marked "Single screen only: plays normally". "Supported" means a game with dual-screen support, not a game the emulator can run. *Not done: the game list doesn't take these files yet (Phase 2).*

**GB-5. The game is never touched.** As GEN-2: nothing is paused, sent input or changed for the second screen. (There is nothing on the second screen to capture yet.)

## Known limitations (so far)

- The game list doesn't list `.gb` / `.gbc` files yet; start one by giving its path to GBA DS (ES-DE's `gba` system already lists those extensions).
- The game's code is read from the GBA header position, so these games have an empty game code and share a patch folder and save names by file name only.
- Battery saves, RTC games (Pokémon Gold / Silver / Crystal), cheats, RetroAchievements, RetroArch's shared states: not checked or not available yet. Rumble, the Game Boy Camera, the link cable and Super Game Boy borders are not planned.
- mGBA's Game Boy core is mGBA's: timing-sensitive games may show problems that belong to it.

# Game rulesets

One file per supported game: what GBA DS shows on each screen, as numbered rules. They're the baseline for changing a game's profile (`src/profiles.c`): a change that breaks a rule is a regression unless the rule is changed with it, on purpose. `tools/regress.py` checks them against approved screenshots of real states (see [../TESTING.md](../TESTING.md)).

| Game | Prefix | Releases |
|---|---|---|
| [Castlevania: Aria of Sorrow](castlevania-aria-of-sorrow.md) | CVA | `A2CE` + 2 |
| [Castlevania: Harmony of Dissonance](castlevania-harmony-of-dissonance.md) | CVH | `ACHE` + 2 |
| [Castlevania: Circle of the Moon](castlevania-circle-of-the-moon.md) | CVC | `AAME` + 2 |
| [Metroid: Zero Mission](metroid-zero-mission.md) | MZM | `BMXE` + 2 |
| [Metroid Fusion](metroid-fusion.md) | MF | `AMTE` + 2 |
| [The Legend of Zelda: A Link to the Past & Four Swords](zelda-a-link-to-the-past.md) | LTTP | `AZLE` + 2 |
| [Tony Hawk's Pro Skater 2](tony-hawks-pro-skater-2.md) | TH2 | `ATHE` + 3 |
| [Mario Kart: Super Circuit](mario-kart-super-circuit.md) | MK | `AMKE` + 2 |
| [Tony Hawk's Pro Skater 3](tony-hawks-pro-skater-3.md) | TH3 | `AT3E` + 2 |
| [Super Mario Advance (Super Mario Advance: Super Mario USA + Mario Brothers in Japan)](super-mario-advance.md) | SMB2 | `AMAE` + 1 |
| [Super Mario Advance 2: Super Mario World](super-mario-advance-2.md) | SMA | `AA2E` + 2 |
| [Super Mario Advance 3: Yoshi's Island](super-mario-advance-3.md) | SM3 | `A3AE` + 2 |
| [Super Mario Advance 4: Super Mario Bros. 3](super-mario-advance-4.md) | SM4 | `AX4E` + 2 |
| [Final Fantasy Tactics Advance](final-fantasy-tactics-advance.md) | FT | `AFXE` + 2 |
| [Advance Wars](advance-wars.md) | AW | `AWRE` + 1 |
| [Advance Wars 2: Black Hole Rising](advance-wars-2.md) | AW2 | `AW2E` + 1 |
| [Game Boy Wars Advance 1+2](game-boy-wars-advance-1-2.md) | GBW | `BGWJ` |
| [Summon Night: Swordcraft Story](summon-night-swordcraft-story.md) | SN | `AB4E` + 1 |
| [Pokémon Pinball: Ruby & Sapphire](pokemon-pinball.md) | PP | `BPPE` |
| [Fire Emblem (Rekka no Ken in Japan)](fire-emblem.md) | FE | `AE7E` + 3 |
| [Fire Emblem: The Sacred Stones (Seima no Kouseki in Japan)](fire-emblem-sacred-stones.md) | FE8 | `BE8E` + 2 |
| [Sword of Mana (Shinyaku Seiken Densetsu in Japan)](sword-of-mana.md) | SOM | `AVSE` + 2 |
| [Golden Sun (Ougon no Taiyou in Japan)](golden-sun.md) | GS | `AGSE` + 5 |
| [Golden Sun: The Lost Age (Ougon no Taiyou: Ushinawareshi Toki in Japan)](golden-sun-2.md) | GS2 | `AGFE` + 5 |
| [Tactics Ogre: The Knight of Lodis (Tactics Ogre Gaiden in Japan)](tactics-ogre.md) | TO | `ATOE` + 1 |
| [F-Zero: GP Legend (F-Zero: Falcon Densetsu in Japan)](f-zero-gp-legend.md) | FZ | `BFZE` + 2 |
| [F-Zero: Maximum Velocity (F-Zero for Game Boy Advance in Japan)](f-zero-maximum-velocity.md) | FZM | `AFZE` + 2 |
| [River City Ransom EX (Downtown Nekketsu Monogatari EX in Japan)](river-city-ransom-ex.md) | RCR | `BDTE` + 1 |
| [Spider-Man: Mysterio's Menace (Spider-Man: Mysterio no Kyoui in Japan)](spider-man-mysterios-menace.md) | SMM | `ASEE` + 1 |
| [Summon Night: Swordcraft Story 2 (Craft Sword Monogatari 2 in Japan)](summon-night-swordcraft-story-2.md) | SN2 | `BSKE` + 1 |
| [Sonic Pinball Party](sonic-pinball-party.md) | SPP | `A3VE` + 1 |
| [Sonic Advance](sonic-advance.md) | SA | `ASOE` + 2 |
| [Sonic Advance 2](sonic-advance-2.md) | SA2 | `A2NE` + 2 |
| [Sonic Advance 3](sonic-advance-3.md) | SA3 | `B3SE` + 2 |
| [Kingdom Hearts: Chain of Memories](kingdom-hearts-chain-of-memories.md) | KHC | `B8CE` + 2 |
| [Lady Sia](lady-sia.md) | LS | `ALDE` + 1 |
| [Zoids: Legacy (Zoids Saga II in Japan)](zoids-legacy.md) | ZL | `AZ2E` + 1 |
| [Harvest Moon: Friends of Mineral Town (Bokujou Monogatari: Mineral Town no Nakama-tachi in Japan)](harvest-moon-friends-of-mineral-town.md) | HM | `A4NE` + 3 |
| [Yu-Gi-Oh! The Eternal Duelist Soul (Yu-Gi-Oh! Duel Monsters 5 Expert 1 in Japan)](yu-gi-oh-the-eternal-duelist-soul.md) | EDS | `AY5E` + 1 |
| [Wario Land 4 (Wario Land Advance in Japan)](wario-land-4.md) | WL4 | `AWAE` + 1 |
| [Mega Man Battle Network (Battle Network Rockman EXE in Japan)](mega-man-battle-network.md) | MMBN | `AREE` + 2 |
| [Muppet Pinball Mayhem](muppet-pinball-mayhem.md) | MPM | `AMWE` + 1 |
| [Ninja Five-O (Ninja Cop in Europe)](ninja-five-o.md) | NFO | `ANXE` + 1 |
| [V-Rally 3](v-rally-3.md) | VR3 | `AVRE` + 2 |
| [Dragon Ball Z: Buu's Fury](dragon-ball-z-buus-fury.md) | BF | `BG3E` |
| [GT Advance 3: Pro Concept Racing (Advance GT2 in Japan)](gt-advance-3-pro-concept-racing.md) | GT3 | `A2GE` + 2 |
| [Eragon](eragon.md) | ER | `BENE` + 1 |

## General rules

**GEN-1. Logo.** A game's logo is taken from its own title screen (per release: `logo` cases check each one) and shown on the second screen before the first capture and wherever a game's rules say so. It's always shown as the game draws it: at its own size (one of its pixels per GBA pixel of the second screen, GEN-5), centred; with no logo, the game's name. When it's all the second screen shows (nothing live to show), it's at half strength on dark grey, so the screen reads as idle and its edges plainly match the game screen's. Under something live (a status bar or other HUD moved there, a title card's pieces, Mario Kart's podium) the second screen is black and the logo at full strength: a live second screen reads apart from an idle one, and the game's own fades to black (a pipe, a door) fade into it. (Until v1.3.2's later test builds, some games showed it smaller and dimmed behind their windows, and the others on black; until v1.4.11's later test builds a live second screen was dark grey too.)

**GEN-2. The game is never touched.** Captures run on a hidden second copy of the game from a copy of the live state; the game being played is never paused, sent input or changed by them (sprite hiding and HUD splits only change what's drawn). *By hand: the tests only see the screens; check on the device that games play the same with `map = 0` and with it on (the capture copy has its own core and state). (checked on the device for v1.4.2)*

**GEN-3. Nothing half-drawn.** A capture is only shown once its checks pass (memory flags and fixed pixels of the screen it opened); otherwise the last good picture stays.

**GEN-4. Same input, same pictures.** With `GBADS_SYNC=1` a run is deterministic, so the tests compare screenshots exactly. *By hand: checked by running the suite twice with no differences, after any change to capture timing (mapcap.c, logo.c, GBADS_SYNC).*

**GEN-5. A GBA screen.** The second screen is a 240x160 picture like the game screen, scaled up the same way (Settings: Map Scale, as Scale is for the game screen), so with the same settings a GBA pixel is the same size on both screens: whatever a game puts there (a map, a status screen, a window, the logo) is at the game's own size. (Until v1.3.2's later test builds, Fire Emblem's, Final Fantasy Tactics Advance's and the Advance Wars games' second screens were bigger pictures, 320x240 or 512x384, so what they showed was smaller than on the game screen.) Mario Kart's race screen is the exception for now: its course map is drawn larger, and the scenery behind it smoothly scaled.

**GEN-6. Quick to follow.** When what a game's second screen is about changes (its profile's `room` probes: the cursor on another unit, Mario on another level, another room), a capture starts as soon as the change has held for 3 frames, without waiting for the regular refresh; one still running for the old place is called off. The hidden copy doesn't draw the frames it only waits through. *By hand: how soon the second screen follows on the device (the tests run captures inline, at fixed frames). (checked on the device for v1.4.2)*
**GEN-7. Saved with the state.** A state saved by GBA DS keeps what the second screen was showing (a capture with its pages, a copied screen, a level's name, Sonic Advance's zone card); loading it puts that back on the next frame, so the second screen matches the moment at once instead of waiting for the next capture or showing the logo. The game's own rules still decide whether it's shown (a state saved on the logo loads on the logo), and the next capture replaces it as usual. A state from before this (or from another emulator, or saved in another release of the game) loads as before, and whatever was kept from the last moment that only that moment can show (a zone card, a level's name) goes. Pinball's two-screen views and HUD pieces aren't kept: they're drawn again from the game within a frame or two.

**GEN-8. Say when a ROM isn't the game as released.** In the game list, the selected game's panel says, under the game's logo at the top right, when a supported game's ROM is a romhack it knows ("Romhack: Superstar Saga+ v1.5 (100%)", with "untested" after the name when the mod's tests don't run on that version) or a ROM it doesn't know ("Modified ROM: the second screen may not work"): patched, hacked, trimmed or a bad dump (see "Releases and dumps" below). An official dump, tested or not, says nothing. It's known once GBA DS has read the ROM, which it does in the background when the game is first selected (half a second or so on the device), then remembers it until the file changes; nothing is said until then.

**GEN-9. Patches, switched on in the game.** Patch files (`.ips`, `.ups`, `.bps`) for a game go in its own folder in GBA DS's `patches` folder, named after the release as No-Intro names it ("Mario & Luigi - Superstar Saga (USA)"; for a game with no list of dumps, its ROM header's title and code), whatever the ROM file is called; GBA DS makes the folder when the game starts. The pause menu's Extras page has **Patches**, which lists them with an On/Off switch each, as Cheats does: the patch's name and type ("Superstar Saga Plus (IPS)") and what GBA DS knows of it ("Tested romhack: Superstar Saga+ v1.5 (100%)", "Untested", or "Made for another version", which can't be switched on: a UPS or BPS whose checksum isn't this ROM's). Several can be on: they're applied in the list's order, the ones on at the top, numbered ("1. ..."), the rest below by name; a patch switched on goes last, and X (or a tap on its number) picks one up to move it with Up / Down (or a tap where it goes), A to put it down. With two or more applied, the list tries them together as the game will: a line under the folder's name says what they make together ("Together: Tested romhack: ..." / "Together: Untested"), a patch that can't follow the ones above it (a UPS or BPS is made for the unchanged ROM: it has to be first) shows "Fails" with a note to move it up and is left out when the game starts, and one that changes some of the same bytes as one above it has its name in gold and a note that they may not work together (the lower one wins there). Up to seven are on at a time. Switching one asks first ("Sure?", with a note to save the game first) and then restarts the game with the change (Y: not yet, for more changes first). While what's chosen isn't what the game started with (switched for later, or moved), the list's top row says so, and A on it ("Sure?", A again) restarts the game with it; otherwise it's used from the next start. With patches on:
- the game, its second screen and its logo are the patched game's;
- by default it has its own battery save and save states ("<ROM name> + <patch name>"; with several, their names by name, " + " between, so moving them doesn't change it), so a romhack that changes the whole game doesn't load the original's saves; two switches at the top of the list, "Separate battery save" and "Separate save states", turn either off (a colour patch, say, can carry on from the original's save);
- RetroAchievements are off (they aren't allowed for patched games), and their banner at the start says so instead;
- GEN-8 goes by the patched ROM ("Romhack: NAME (patch)", or "Patched: the second screen may not work"; "Patched, but a patch was left out" when one of them can't be applied where it is), and the log and bug reports name the patches applied, any left out, and the ROM file's own checksum.

A patch file beside the ROM with its name (`Mario & Luigi.ips` beside `Mario & Luigi.gba`), as RetroArch uses them, is always on, and shows in the list as "Always (beside the ROM)". A patch that can't be applied (made for another ROM, or not a patch) isn't: the game starts without it, and the list and the game list say why.

## Releases and dumps

Each game's file starts with a table. Its **Releases** row lists the game codes the profile handles (the 4 letters in the ROM's header). Its **Dumps** row lists, per release, every official dump of it known to No-Intro by CRC32: the cartridge, later revisions, Virtual Console and collection dumps (not prototypes, betas or demos). The one marked *tested* is the fixture the regression tests run on (`roms/CODE.zip`); the others are expected to behave the same but aren't tested. GBA DS checks the ROM a player loads against this list: the tested dump, another official one, or none of them, which means the ROM has been changed (a patch or a hack, or trimmed) or is a bad dump. It says which in its log and in bug reports. The Dumps row and `src/dumps_data.h` are written by `tools/make_dumps.py` from No-Intro's list and the fixtures: re-run it when a Releases row changes or a game is added, never edit them by hand. `tools/regress.py` won't run a case whose fixture ROM isn't the dump marked tested, so a patched ROM can't become a fixture by mistake.

A **Mods** row (only for games with one) lists the romhacks and patches checked to work with the profile, written by hand: per release, each mod's name and version and the CRC32 of the ROM once patched (onto the tested dump), items separated by commas (so none in the names), the ones the tests run on marked *tested*. A mod gets its own rule saying which rules hold for it and anything that differs, and its own cases, whose `rom` is the release's code, a `+` and the mod (`A88E+mlssplus-100`: the fixture `roms/A88E+mlssplus-100.zip`). `tools/make_dumps.py` puts the Mods rows into `src/dumps_data.h` too, so GBA DS knows the mod when it's loaded. `tools/romhack.py PATCH` (an IPS with `--base CODE`; a UPS or BPS finds its release itself; or an already patched ROM) is where to start with a mod someone asks for: it applies the patch, says what it changes and runs the release's cases on the patched ROM (checks that fail mean a rule broke; pictures that differ are often the mod's own content), and writes out the patched ROM and the Mods row entry to add. For players, a tested mod goes in the wiki's Supported games table, in its game's "Romhacks tested" column (never a row of its own: mods don't count as games), and its differences in the game's section there.

## Writing rules

- One behaviour per rule, in the player's terms, with where (which screen, where on it) and when (which game state).
- Rule IDs are `PREFIX-n`, numbered in order and never renumbered or reused; retire a rule by marking it *retired* and saying why.
- Every rule is either cited by a test case or marked *By hand:* with what to check and why the tests can't. `tools/regress.py --uncovered` lists rules that are neither (add a *Not covered yet* section to the game's file saying what state would cover them) and, separately, the by-hand ones to check before a release.

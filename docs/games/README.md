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

## General rules

**GEN-1. Logo.** A game's logo is taken from its own title screen (per release: `logo` cases check each one) and shown on the second screen before the first capture and wherever a game's rules say so. It's always shown as the game draws it: at its own size (one of its pixels per GBA pixel of the second screen, GEN-5), centred; with no logo, the game's name. When it's all the second screen shows (nothing live to show), it's at half strength on dark grey, so the screen reads as idle and its edges plainly match the game screen's. Under something live (a status bar or other HUD moved there, a title card's pieces, Mario Kart's podium) the second screen is black and the logo at full strength: a live second screen reads apart from an idle one, and the game's own fades to black (a pipe, a door) fade into it. (Until v1.3.2's later test builds, some games showed it smaller and dimmed behind their windows, and the others on black; until v1.4.11's later test builds a live second screen was dark grey too.)

**GEN-2. The game is never touched.** Captures run on a hidden second copy of the game from a copy of the live state; the game being played is never paused, sent input or changed by them (sprite hiding and HUD splits only change what's drawn). *By hand: the tests only see the screens; check on the device that games play the same with `map = 0` and with it on (the capture copy has its own core and state). (checked on the device for v1.4.2)*

**GEN-3. Nothing half-drawn.** A capture is only shown once its checks pass (memory flags and fixed pixels of the screen it opened); otherwise the last good picture stays.

**GEN-4. Same input, same pictures.** With `GBADS_SYNC=1` a run is deterministic, so the tests compare screenshots exactly. *By hand: checked by running the suite twice with no differences, after any change to capture timing (mapcap.c, logo.c, GBADS_SYNC).*

**GEN-5. A GBA screen.** The second screen is a 240x160 picture like the game screen, scaled up the same way (Settings: Map Scale, as Scale is for the game screen), so with the same settings a GBA pixel is the same size on both screens: whatever a game puts there (a map, a status screen, a window, the logo) is at the game's own size. (Until v1.3.2's later test builds, Fire Emblem's, Final Fantasy Tactics Advance's and the Advance Wars games' second screens were bigger pictures, 320x240 or 512x384, so what they showed was smaller than on the game screen.) Mario Kart's race screen is the exception for now: its course map is drawn larger, and the scenery behind it smoothly scaled.

**GEN-6. Quick to follow.** When what a game's second screen is about changes (its profile's `room` probes: the cursor on another unit, Mario on another level, another room), a capture starts as soon as the change has held for 3 frames, without waiting for the regular refresh; one still running for the old place is called off. The hidden copy doesn't draw the frames it only waits through. *By hand: how soon the second screen follows on the device (the tests run captures inline, at fixed frames). (checked on the device for v1.4.2)*
**GEN-7. Saved with the state.** A state saved by GBA DS keeps what the second screen was showing (a capture with its pages, a copied screen, a level's name, Sonic Advance's zone card); loading it puts that back on the next frame, so the second screen matches the moment at once instead of waiting for the next capture or showing the logo. The game's own rules still decide whether it's shown (a state saved on the logo loads on the logo), and the next capture replaces it as usual. A state from before this (or from another emulator, or saved in another release of the game) loads as before, and whatever was kept from the last moment that only that moment can show (a zone card, a level's name) goes. Pinball's two-screen views and HUD pieces aren't kept: they're drawn again from the game within a frame or two.

## Releases and dumps

Each game's file starts with a table. Its **Releases** row lists the game codes the profile handles (the 4 letters in the ROM's header). Its **Dumps** row lists, per release, every official dump of it known to No-Intro by CRC32: the cartridge, later revisions, Virtual Console and collection dumps (not prototypes, betas or demos). The one marked *tested* is the fixture the regression tests run on (`roms/CODE.zip`); the others are expected to behave the same but aren't tested. GBA DS checks the ROM a player loads against this list: the tested dump, another official one, or none of them, which means the ROM has been changed (a patch or a hack, or trimmed) or is a bad dump. It says which in its log and in bug reports. The Dumps row and `src/dumps_data.h` are written by `tools/make_dumps.py` from No-Intro's list and the fixtures: re-run it when a Releases row changes or a game is added, never edit them by hand. `tools/regress.py` won't run a case whose fixture ROM isn't the dump marked tested, so a patched ROM can't become a fixture by mistake.

## Writing rules

- One behaviour per rule, in the player's terms, with where (which screen, where on it) and when (which game state).
- Rule IDs are `PREFIX-n`, numbered in order and never renumbered or reused; retire a rule by marking it *retired* and saying why.
- Every rule is either cited by a test case or marked *By hand:* with what to check and why the tests can't. `tools/regress.py --uncovered` lists rules that are neither (add a *Not covered yet* section to the game's file saying what state would cover them) and, separately, the by-hand ones to check before a release.

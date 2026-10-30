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
| [Super Mario Advance 2: Super Mario World](super-mario-advance-2.md) | SMA | `AA2E` + 2 |
| [Final Fantasy Tactics Advance](final-fantasy-tactics-advance.md) | FT | `AFXE` + 2 |
| [Advance Wars](advance-wars.md) | AW | `AWRE` + 1 |
| [Advance Wars 2: Black Hole Rising](advance-wars-2.md) | AW2 | `AW2E` + 1 |
| [Game Boy Wars Advance 1+2](game-boy-wars-advance-1-2.md) | GBW | `BGWJ` |
| [Summon Night: Swordcraft Story](summon-night-swordcraft-story.md) | SN | `AB4E` + 1 |
| [Pokémon Pinball: Ruby & Sapphire](pokemon-pinball.md) | PP | `BPPE` |
| [Fire Emblem (Rekka no Ken in Japan)](fire-emblem.md) | FE | `AE7E` + 3 |
| [Sword of Mana (Shinyaku Seiken Densetsu in Japan)](sword-of-mana.md) | SOM | `AVSE` + 2 |
| [Golden Sun (Ougon no Taiyou in Japan)](golden-sun.md) | GS | `AGSE` + 5 |
| [Tactics Ogre: The Knight of Lodis (Tactics Ogre Gaiden in Japan)](tactics-ogre.md) | TO | `ATOE` + 1 |

## General rules

**GEN-1. Logo.** A game's logo is taken from its own title screen (per release: `logo` cases check each one) and shown on the second screen before the first capture and wherever a game's rules say so. It's always shown as the game draws it: at its own size (one of its pixels per GBA pixel of the second screen, GEN-5), centred, on dark grey, so the second screen's pixels plainly match the game screen's; with no logo, the game's name. When it's all the second screen shows (nothing live to show), it's at half strength, so the screen reads as idle; under something live (Super Mario Advance 2's status bar) it's at full strength. (Until v1.3.2's later test builds, some games showed it smaller and dimmed behind their windows, and the others on black.)

**GEN-2. The game is never touched.** Captures run on a hidden second copy of the game from a copy of the live state; the game being played is never paused, sent input or changed by them (sprite hiding and HUD splits only change what's drawn). *By hand: the tests only see the screens; check on the device that games play the same with `map = 0` and with it on (the capture copy has its own core and state). (checked on the device for v1.4.2)*

**GEN-3. Nothing half-drawn.** A capture is only shown once its checks pass (memory flags and fixed pixels of the screen it opened); otherwise the last good picture stays.

**GEN-4. Same input, same pictures.** With `GBADS_SYNC=1` a run is deterministic, so the tests compare screenshots exactly. *By hand: checked by running the suite twice with no differences, after any change to capture timing (mapcap.c, logo.c, GBADS_SYNC).*

**GEN-5. A GBA screen.** The second screen is a 240x160 picture like the game screen, scaled up the same way (Settings: Map Scale, as Scale is for the game screen), so with the same settings a GBA pixel is the same size on both screens: whatever a game puts there (a map, a status screen, a window, the logo) is at the game's own size. (Until v1.3.2's later test builds, Fire Emblem's, Final Fantasy Tactics Advance's and the Advance Wars games' second screens were bigger pictures, 320x240 or 512x384, so what they showed was smaller than on the game screen.) Mario Kart's race screen is the exception for now: its course map is drawn larger, and the scenery behind it smoothly scaled.

**GEN-6. Quick to follow.** When what a game's second screen is about changes (its profile's `room` probes: the cursor on another unit, Mario on another level, another room), a capture starts as soon as the change has held for 3 frames, without waiting for the regular refresh; one still running for the old place is called off. The hidden copy doesn't draw the frames it only waits through. *By hand: how soon the second screen follows on the device (the tests run captures inline, at fixed frames). (checked on the device for v1.4.2)*

## Writing rules

- One behaviour per rule, in the player's terms, with where (which screen, where on it) and when (which game state).
- Rule IDs are `PREFIX-n`, numbered in order and never renumbered or reused; retire a rule by marking it *retired* and saying why.
- Every rule is either cited by a test case or marked *By hand:* with what to check and why the tests can't. `tools/regress.py --uncovered` lists rules that are neither (add a *Not covered yet* section to the game's file saying what state would cover them) and, separately, the by-hand ones to check before a release.

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
| [Fire Emblem (Rekka no Ken in Japan)](fire-emblem.md) | FE | `AE7E` + 3 |
| [Sword of Mana (Shinyaku Seiken Densetsu in Japan)](sword-of-mana.md) | SOM | `AVSE` + 2 |

## General rules

**GEN-1. Logo.** A game's logo is taken from its own title screen (per release: `logo` cases check each one) and shown on the second screen before the first capture and wherever a game's rules say so; with no logo, the game's name.

**GEN-2. The game is never touched.** Captures run on a hidden second copy of the game from a copy of the live state; the game being played is never paused, sent input or changed by them (sprite hiding and HUD splits only change what's drawn). *By hand: the tests only see the screens; check on the device that games play the same with `map = 0` and with it on (the capture copy has its own core and state).*

**GEN-3. Nothing half-drawn.** A capture is only shown once its checks pass (memory flags and fixed pixels of the screen it opened); otherwise the last good picture stays.

**GEN-4. Same input, same pictures.** With `GBADS_SYNC=1` a run is deterministic, so the tests compare screenshots exactly. *By hand: checked by running the suite twice with no differences, after any change to capture timing (mapcap.c, logo.c, GBADS_SYNC).*

## Writing rules

- One behaviour per rule, in the player's terms, with where (which screen, where on it) and when (which game state).
- Rule IDs are `PREFIX-n`, numbered in order and never renumbered or reused; retire a rule by marking it *retired* and saying why.
- Every rule is either cited by a test case or marked *By hand:* with what to check and why the tests can't. `tools/regress.py --uncovered` lists rules that are neither (add a *Not covered yet* section to the game's file saying what state would cover them) and, separately, the by-hand ones to check before a release.

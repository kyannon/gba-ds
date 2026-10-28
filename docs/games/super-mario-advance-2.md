# Super Mario Advance 2: Super Mario World

| | |
|---|---|
| Releases | `AA2E` USA · `AA2P` Europe · `AA2J` Japan (game mode 0x10 lower) |
| Profile | `src/profiles.c`: the `AA2E` entry (with `AA2P`); variant `AA2J` |
| Tests | `tests/regress/super-mario-advance-2.json` (`tools/regress.py super-mario-advance-2`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SMA-1. World map: level list.** On the world map the second screen shows the SELECT menu's list of the island's levels with time and score, captured on the hidden copy, held still (HELP! and the current level shown, not blinking). It's taken again as soon as Mario stops on another level (GEN-6; USA and Europe; in Japan at the next refresh).

**SMA-2. Level: status bar.** In a level the status bar (lives, Dragon Coins, time, coins, score) moves to the top of the second screen, with the logo (GEN-1) centred on the screen below it (until v1.3.2's later test builds: half its size, on black); the blue item box stays at the top of the game screen with the reserve item.

**SMA-3. Boxes stay.** Message boxes and the pause box stay on the game screen.

**SMA-4. Through doors and castles.** The status bar stays on the second screen through doors, pipes, castle rooms and boss fights.

**SMA-5. SNES layout.** The Controls button offers the SNES layout.

**SMA-6. Logo.** The logo (GEN-1) is Super Mario World's own, SUPER MARIO WORLD alone, taken from its title screen (Start for single player, A on Super Mario World) without the wooden frame round it or the copyright line. (Until v1.3.2's later test builds it was the first title's, with SUPER MARIO ADVANCE 2 under it.)

**SMA-7. Level name.** In a level, the top of the second screen keeps the level list's title row (its name, as the world map's list showed it when Mario last stood still: the level he went into), held there all through the level, through doors and castles too (SMA-4); the status bar (SMA-2) is right below it, and the logo below that. With no list taken yet (a state loaded in a level), the status bar is at the top. *By hand: a level entered quickly, before the list has caught up with where Mario is, shows the level he was on before.*

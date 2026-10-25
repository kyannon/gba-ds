# Game Boy Wars Advance 1+2

| | |
|---|---|
| Releases | `BGWJ` Japan only: Advance Wars (Game Boy Wars Advance) and Advance Wars 2 (Game Boy Wars Advance 2) on one cartridge, the only Japanese release of either. |
| Profile | `src/profiles.c`: the `BGWJ` entry (the compilation's own menus) and its `pick`, which switches to the `BGW1` or `BGW2` variant (Advance Wars' and Advance Wars 2's profiles at this cartridge's addresses, in `awRegions`) |
| Tests | `tests/regress/game-boy-wars-advance-1-2.json` (`tools/regress.py game-boy-wars-advance-1-2`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list game-boy-wars-advance-1-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

The two games each run their own code at their own RAM addresses; which one is running is told by where the interrupt handler is (game 1 runs it from IWRAM, game 2 from the ROM).

## Rules

**GBW-1. The compilation's menus.** On the compilation's title and the screen picking a game, the second screen shows the logo (the compilation's title lettering, GAME BOY WARS ADVANCE 1+2, without PRESS START!). Both games use this logo on their own titles and menus too.

**GBW-2. Game 1 is Advance Wars.** Once Game Boy Wars Advance is picked, the second screen follows [Advance Wars' rules](advance-wars.md) (AW-1 to AW-7), with its Known issue: in Field Training the tutorial blocks SELECT and START, so the panels can't be brought up; the plate and info still leave the game screen. *By hand: no state outside Field Training yet (the panels, the unit intel, conversations): check a campaign or versus battle on the device.*

**GBW-3. Game 2 is Advance Wars 2.** Once Game Boy Wars Advance 2 is picked, the second screen follows [Advance Wars 2's rules](advance-wars-2.md) (AW2-1 to AW2-7): the unit intel or Battle Intel at the top, the minimap at the bottom left, the CO plate and the terrain and unit info moved to the bottom, conversations left on the game screen.

**GBW-4. Switching.** The second screen is set up again whenever the game running changes (a game picked, or back to the compilation's menus): it shows the logo until the new game's first picture.

## Not covered yet

Game 1 outside Field Training (GBW-2): a battle in its campaign or versus mode, with the cursor on a unit and with a conversation.

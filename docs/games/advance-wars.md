# Advance Wars

| | |
|---|---|
| Releases | `AWRE` USA · `AWRP` Europe (its language menu comes later). No Japanese release. |
| Profile | `src/profiles.c`: the `AWRE` entry; variant `AWRP` |
| Tests | `tests/regress/advance-wars.json` (`tools/regress.py advance-wars`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**AW-1. Unit list or intel.** In a battle on your turn the second screen shows the Unit list with the unit under the cursor highlighted when it's one of yours (still highlighted while it moves and picks an action), or else the Battle Intel; both are brought up on the hidden copy.

**AW-2. Minimap.** The START minimap is at the bottom left of the second screen.

**AW-3. Plate and info move.** The CO and day plate and the terrain and unit info leave the game screen for the bottom of the second screen (plate centred, info on the right on a grey box); the panels are drawn over them where they meet.

**AW-4. Enemy turn.** On the enemy's turn their plate shows; the panel keeps its last picture.

**AW-5. Fights.** In a fight the second screen is the dark background with the logo small and dimmed in the middle.

**AW-6. Title and menus.** On the title and menus the second screen shows the logo.

## Known issues

Known issue: in Field Training the tutorial blocks SELECT and START, so the panels can't be brought up; the plate and info still move.

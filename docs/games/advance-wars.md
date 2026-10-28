# Advance Wars

| | |
|---|---|
| Releases | `AWRE` USA · `AWRP` Europe (its language menu comes later). No Japanese release. |
| Profile | `src/profiles.c`: the `AWRE` entry; variant `AWRP` |
| Tests | `tests/regress/advance-wars.json` (`tools/regress.py advance-wars`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**AW-1. Unit intel or Battle Intel.** In a battle on your turn the second screen shows the intel of the unit under the cursor (the screen R opens: its panel and the text box beside it, where the game puts them) when there's a unit there, yours or the enemy's (still while yours moves and picks an action), or else the Battle Intel (the battle's status panel), centred on the screen; both are brought up on the hidden copy. (Until v1.3.2: the Unit list with the unit highlighted. Until v1.3.2's later test builds both were at the top, the minimap and the moved plate and info below them.)

**AW-2.** *Retired in v1.3.2's later test builds:* the START minimap at the second screen's bottom left.

**AW-3.** *Retired in v1.3.2's later test builds:* the CO and day plate and the terrain and unit info moved to the bottom of the second screen (they stay on the game screen: AW-8).

**AW-4. Enemy turn.** On the enemy's turn the second screen keeps its last picture.

**AW-5. Fights.** In a fight the second screen shows the logo (GEN-1).

**AW-6. Title and menus.** On the title and menus the second screen shows the logo.

**AW-7. Conversations.** Conversations during a battle stay on the game screen. (Until v1.3.2's later test builds they moved to the top of the second screen.)

**AW-8. Game screen untouched.** Nothing leaves the game screen: the CO and day plate, the terrain and unit info with its darkened box, the DAMAGE box and conversations stay where the game draws them, as they would on one screen.

## Known issues

Known issue: in Field Training the tutorial blocks SELECT and START, so the panels can't be brought up: the second screen keeps the logo.

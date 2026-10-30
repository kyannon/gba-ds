# Advance Wars

| | |
|---|---|
| Releases | `AWRE` USA · `AWRP` Europe (its language menu comes later). No Japanese release. |
| Profile | `src/profiles.c`: the `AWRE` entry; variant `AWRP` |
| Tests | `tests/regress/advance-wars.json` (`tools/regress.py advance-wars`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**AW-1. Unit intel or Battle Intel.** In a battle on your turn the second screen shows the intel of the unit under the cursor (the screen R opens: its panel and the text box beside it, where the game puts them; the yellow corners blinking round the unit's picture left out, since v1.4.2's later test builds) when there's a unit there, yours or the enemy's (still while yours moves and picks an action), or else the Battle Intel (the battle's status panel), centred on the screen; both are brought up on the hidden copy. (Until v1.3.2: the Unit list with the unit highlighted. Until v1.3.2's later test builds both were at the top, the minimap and the moved plate and info below them.)

**AW-2.** *Retired in v1.3.2's later test builds:* the START minimap at the second screen's bottom left.

**AW-3.** *Retired in v1.3.2's later test builds:* the CO and day plate and the terrain and unit info moved to the bottom of the second screen (they stay on the game screen: AW-8).

**AW-4. The computer's turn.** From the moment you end your turn, through the day banner and the computer's moves, the second screen shows the logo: the cursor isn't yours, so there's no unit intel to bring up. On your next turn the pictures come back. (Until v1.4.2's later test builds the last picture stayed; in those, until the last ones, the intel stayed through the day banner, until the computer's first move.) Your army is taken to be the first (in the campaigns you always are). *Known limitation: in Versus, if you play a later army, or two people share the GBA, those turns show the logo too: the game keeps no record GBA DS could find of which armies a person plays.*

**AW-5. Fights.** In a fight the second screen shows the logo (GEN-1).

**AW-6. Title and menus.** On the title and menus, and once a battle's over (its results), the second screen shows the logo.

**AW-7. Conversations.** Conversations during a battle stay on the game screen. (Until v1.3.2's later test builds they moved to the top of the second screen.)

**AW-8. Game screen untouched.** Nothing leaves the game screen: the CO and day plate, the terrain and unit info with its darkened box, the DAMAGE box and conversations stay where the game draws them, as they would on one screen.

## Known issues

Known issue (won't fix): in Field Training the tutorial blocks SELECT and START, so the panels can't be brought up: the second screen keeps the logo. That's the game's tutorial doing what it does on one screen, so it stays as it is.

# Advance Wars 2: Black Hole Rising

| | |
|---|---|
| Releases | `AW2E` USA · `AW2P` Europe (the same RAM; a language menu first). No Japanese release. |
| Profile | `src/profiles.c`: the `AW2E` entry (Advance Wars' code, its addresses in `awRegions`); variant `AW2P` |
| Tests | `tests/regress/advance-wars-2.json` (`tools/regress.py advance-wars-2`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-5). It's set up as [Advance Wars](advance-wars.md) is; the rules are the same.

## Rules

**AW2-1. Unit intel or Battle Intel.** In a battle on your turn the second screen shows the intel of the unit under the cursor (the screen R opens: its panel and the text box beside it, where the game puts them) when there's a unit there, yours or the enemy's, or else the Battle Intel (SELECT > Intel > Status: the battle's status, its panel centred on the screen); both are brought up on the hidden copy. (Until v1.3.2's later test builds both were at the top, the minimap and the moved plate and info below them.)

**AW2-2.** *Retired in v1.3.2's later test builds:* the START minimap at the second screen's bottom left.

**AW2-3.** *Retired in v1.3.2's later test builds:* the CO plate and the terrain and unit info (and the DAMAGE box) moved to the bottom of the second screen (they stay on the game screen: AW2-8).

**AW2-4. Enemy turn.** On the enemy's turn the second screen keeps its last picture. *By hand: no enemy-turn state yet (the first tutorial map won't end the turn early).*

**AW2-5. Fights.** In a fight the second screen shows the logo (GEN-1). *By hand: no fight state yet.*

**AW2-6. Title and menus.** On the title and menus the second screen shows the logo (the title's lettering, without PRESS START!).

**AW2-7. Conversations.** A conversation during a battle stays on the game screen. While it's up, and as it slides in and out, nothing on the second screen changes (the panels can't be brought up behind it). (Until v1.3.2's later test builds it moved to the second screen.)

**AW2-8. Game screen untouched.** Nothing leaves the game screen: the CO plate, the terrain and unit info, the DAMAGE box, the cursor and conversations stay where the game draws them, as they would on one screen.

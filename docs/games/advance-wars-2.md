# Advance Wars 2: Black Hole Rising

| | |
|---|---|
| Releases | `AW2E` USA · `AW2P` Europe (the same RAM; a language menu first). No Japanese release. |
| Profile | `src/profiles.c`: the `AW2E` entry (Advance Wars' code, its addresses in `awRegions`); variant `AW2P` |
| Tests | `tests/regress/advance-wars-2.json` (`tools/regress.py advance-wars-2`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4). It's set up as [Advance Wars](advance-wars.md) is; the rules are the same.

## Rules

**AW2-1. Unit intel or Battle Intel.** In a battle on your turn the second screen shows, at the top in the middle, the intel of the unit under the cursor (the screen R opens: its panel, not the text box beside it) when it's one of yours, or else the Battle Intel (SELECT > Intel > Status); both are brought up on the hidden copy.

**AW2-2. Minimap.** The START minimap is at the bottom left of the second screen.

**AW2-3. Plate and info move.** The CO plate (portrait and funds) and the terrain and unit info leave the game screen for the bottom of the second screen (plate centred, info on the right on a grey box); the panels are drawn over them where they meet. The cursor's corners stay on the game screen. While the unit intel (R) is open in the game, they stay as they were.

**AW2-4. Enemy turn.** On the enemy's turn their plate shows; the panel keeps its last picture. *By hand: no enemy-turn state yet (the first tutorial map won't end the turn early).*

**AW2-5. Fights.** In a fight the second screen is the dark background with the logo small and dimmed in the middle. *By hand: no fight state yet.*

**AW2-6. Title and menus.** On the title and menus the second screen shows the logo (the title's lettering, without PRESS START!).

**AW2-7. Conversations.** A conversation during a battle (the tutorial's) is copied to the top of the second screen, as wide as the screen, over the panels; it stays on the game screen too. While it's up nothing else changes (the panels can't be brought up behind it).

## Known issues

- The DAMAGE box (picking a target) isn't moved yet: no state to find its sprites with.
- Conversations are recognised by the layer settings seen at the top of the screen in the tutorial; one at the bottom may use others and stay on the game screen only.

# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

Unlike the other games, nothing is brought up on a hidden copy: the second screen shows more of the table itself. The game only draws the 160 rows its camera is on (and loads the table's graphics as it scrolls), so the rows beyond those are kept from what it drew earlier.

## Rules

**PP-1. The table over both screens.** On a table, the two screens together show 320 rows of it, the upper 160 on the top screen and the next 160 on the bottom one. The view starts at the game's camera and goes down from there, but stops at the table's bottom: so the ball stays on the top screen except near the flippers, when the camera's at the bottom of the table and the ball shows on the bottom screen.

**PP-2. Rows the game isn't drawing.** The rows outside the game's camera are as the game last drew them (their animations stand still until the camera comes back); rows it hasn't drawn since the table came up are black. Until the rows above the camera have been seen (after loading a state at the bottom of the table), the top screen shows the game's own picture. A fade forgets them all (another table may follow).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) leaves the game screen for the bottom of the second screen, where it was on the game's.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the upper rows stay on the top screen (the second screen then) and the game screen shows the lower ones, with the score bar on the top screen.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device.*

## Not covered yet

The bonus stages (their own tables, their size not known yet: the view is set up for Ruby's and Sapphire's 432 rows, and grows if a camera goes further).

# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

Unlike the other games, the second screen shows more of the table itself, live: a second copy of the game, run in step with yours (the same buttons every frame), draws the 160 rows below your game's camera (game.c `twin_*`).

## Rules

**PP-1. The table over both screens, live.** On Ruby's and Sapphire's tables one screen shows your game, as it is (its camera is never changed: the game treats things off its screen differently, and holding its camera up changed how catch mode played), and the other the 160 rows next to it, drawn live by the second copy: its flippers, Pokémon and animations as the game draws them. Your game is on the top screen with the copy's rows below it, until its camera comes down past row 176 of the table (the ball near the flippers): then it moves to the bottom screen with the copy's rows above it, until its camera goes back up past row 112. The ball is always on your game's screen. Rows outside the table (up to 48, between those two points) are black.

**PP-2.** *Retired in v1.3.2's later test builds:* the rows outside the camera kept from what the game drew, dimmed (replaced by PP-1's live view).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) is always at the bottom of the bottom screen: moved there from your game's picture while it's on the top screen.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen, as without.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device.*

**PP-7.** *Retired in v1.3.2's later test builds:* events shown on the game screen as the game draws them (replaced by PP-10).

**PP-8.** *Retired in v1.3.2's later test builds:* the flippers drawn over the kept rows (PP-1's copy draws them).

**PP-9. In step.** The two copies are compared every frame (the ball's position). When they differ, when your game changes screens, and after loading a state or rewinding, the copy is taken from your game again: its rows are in place on the next frame (the copy's table tiles and scroll are set for its camera at once: game.c `twin_jump`), with its sprites from the frame after. (Until v1.3.2's later test builds, its rows filled in from your game's side over about a third of a second, black until then.) During the table's opening sweep the copy isn't running: the game screen shows the game as it is (with its score bar), the second screen the logo. `pinball_live = 0` in `gba-ds.cfg` turns the copy off: the game plays on one screen with the logo on the other. States saved by v1.3.2's earlier test builds (which lowered the camera limit) are put right as the table comes up. *By hand: speed on the device (the copy doubles the emulation work).*

**PP-10. What's fixed to the screen.** What the game puts on the screen rather than on the table (the catch timer, messages such as a Pokémon caught, banners such as BALL SAVE, the Pokémon Mart) is only on your game's screen (where the ball is); the copy leaves it out, and its sprites placed from things on your game's screen (a message's letters, which the hardware would wrap round to the copy's other edge). The screen darkening behind a message is on both screens. (Until v1.3.2's later test builds, they went to the screen the ball was on as they appeared.)

## Not covered yet

The bonus stages (their own tables: the copy only runs on Ruby's and Sapphire's, whose camera limit it knows).

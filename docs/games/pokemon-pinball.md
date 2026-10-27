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

**PP-1. The table over both screens, live.** On Ruby's and Sapphire's tables, the top screen shows your game and the bottom screen the 160 rows below its camera, drawn live by the second copy: its flippers, Pokémon, ball and animations as the game draws them. Your game's camera is stopped 160 rows short of the table's bottom (its lowest camera target is changed, once the table's opening sweep is over: the one change made to your game), so at the bottom of the table the flippers and the ball are on the bottom screen, and the ball is on the top screen otherwise.

**PP-2.** *Retired in v1.3.2's later test builds:* the rows outside the camera kept from what the game drew, dimmed (replaced by PP-1's live view).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) leaves the game screen for the bottom of the second screen, where it was on the game's.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the upper rows stay on the top screen (the second screen then) and the game screen shows the lower ones, drawn by the copy.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device.*

**PP-7.** *Retired in v1.3.2's later test builds:* events shown on the game screen as the game draws them (replaced by PP-10).

**PP-8.** *Retired in v1.3.2's later test builds:* the flippers drawn over the kept rows (PP-1's copy draws them).

**PP-9. In step.** The two copies are compared every frame (the ball's position). When they differ (now and then in play, after loading a state or rewinding), the copy is taken from your game again and its camera scrolls down into place over about a third of a second. During the table's opening sweep the copy isn't running: the game screen shows the game as it is (with its score bar), the second screen the logo. `pinball_live = 0` in `gba-ds.cfg` turns the copy off: the game plays on one screen with the logo on the other. *By hand: speed on the device (the copy doubles the emulation work).*

**PP-10. What's fixed to the screen.** What the game puts on the screen rather than on the table (the catch timer, messages such as a Pokémon caught, banners such as BALL SAVE, the Pokémon Mart) is shown once, on the screen the ball is on when it appears, and stays there until it's gone; a message box goes where the ball is when it appears, even if the timer was put on the other screen. The screen darkening behind a message is only on that screen.

## Not covered yet

The bonus stages (their own tables: the copy only runs on Ruby's and Sapphire's, whose camera limit it knows).

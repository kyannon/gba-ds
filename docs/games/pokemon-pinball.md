# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

Unlike the other games, both screens show the table itself, live: two more copies of the game, run in step with yours (the same buttons every frame), draw the rows your game's camera doesn't (game.c `twin_*`).

## Rules

**PP-1. The table over both screens, live, scrolling smoothly.** On Ruby's and Sapphire's tables the two screens show 320 rows of the table in one piece, as if on one tall screen, and the view only ever scrolls: it never jumps. Your game itself is never changed (its camera neither: the game treats things off its screen differently, and holding its camera up changed how catch mode played). While your game's camera is in the table's upper part (rows 0 to 112), the top screen shows your game as it is and the bottom screen the 160 rows below it, drawn by a copy. Further down, the view stops at rows 112 to 432 (the whole lower table): the top screen shows rows 112 to 271 and the bottom screen rows 272 to 431, each drawn by a copy at that fixed place, with what your game puts on its screen laid over the top screen (PP-10). So the ball is on the top screen except when the table is at its lowest. (Until v1.3.2's later test builds, your game moved between the screens as the ball went down and up, and the whole table jumped by a screen each time.)

**PP-2.** *Retired in v1.3.2's later test builds:* the rows outside the camera kept from what the game drew, dimmed (replaced by PP-1's live view).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) is always at the bottom of the bottom screen: moved there from your game's picture.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen, as without.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device.*

**PP-7.** *Retired in v1.3.2's later test builds:* events shown on the game screen as the game draws them (replaced by PP-10).

**PP-8.** *Retired in v1.3.2's later test builds:* the flippers drawn over the kept rows (PP-1's copy draws them).

**PP-9. In step.** Each copy is compared with your game every frame (the ball's position, and that it shows the rows it should). When they differ, and after loading a state or rewinding, that copy is taken from your game again (its table tiles and scroll set for its place at once: game.c `twin_jump`); for that one frame its screen keeps the picture from the frame before, so nothing goes blank. The copies never move their camera faster than the game's own scrolling (at most 8 rows a frame), so they draw their rows and sprites as the game would. They run side by side with each other on a second thread. During the table's opening sweep the copies aren't running: the top screen shows the game as it is (with its score bar), the second screen the logo. `pinball_live = 0` in `gba-ds.cfg` turns the copies off: the game plays on one screen with the logo on the other. States saved by v1.3.2's earlier test builds (which lowered the camera limit) are put right as the table comes up. (Until v1.3.2's later test builds, one copy, its rows black while it caught up after being taken again.) *By hand: speed on the device (three cores' emulation, two of them at once).*

**PP-10. What's fixed to the screen.** What the game puts on the screen rather than on the table (the catch timer, banners such as BALL SAVE) stays at its place on the top screen: your game's own picture there in the table's upper part; in the lower part, the game's BG0 above the score bar and those sprites, drawn on their own, laid over the copy's rows (messages, pauses and the Mart: PP-11). The copies leave it out: BG0, the sprites the game draws at the same place on the screen whatever the camera, and sprites that only show on a copy because the hardware wraps them round from your game's screen (a message's letters). The copies' table sprites (Pokémon, flippers, the ball) are all kept, wherever they are. The screen darkening behind a message is on both screens. (Until v1.3.2's later test builds, the copy left out any sprite that matched one of your game's, wherever it was: Pokémon often went missing on the copy's screen.)

**PP-11. Paused, the Mart, messages.** While the game dims its screen (paused, the Pokémon Mart) or shows a message box (a Pokémon caught), the bottom screen shows your game's whole screen as it is, the score bar included, so the message or menu and the table under it stay together; the top screen goes on showing the table's rows as in PP-1, with nothing laid over it. (The owner's State Saver note "Pause screen".)

## Not covered yet

The bonus stages (their own tables: the copy only runs on Ruby's and Sapphire's, whose camera limit it knows).

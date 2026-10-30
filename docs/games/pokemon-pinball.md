# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

Unlike the other games, both screens show the table itself, live: two more copies of the game, run in step with yours (the same buttons every frame), draw the rows your game's camera doesn't (game.c `twin_*`).

## Rules

**PP-1. The table over both screens, live, scrolling smoothly.** On Ruby's and Sapphire's tables the two screens show 320 rows of the table in one piece, as if on one tall screen, and the view only ever scrolls: it never jumps. Your game itself is never changed (its camera neither: the game treats things off its screen differently, and holding its camera up changed how catch mode played). While your game's camera is in the table's upper part (rows 0 to 112), the top screen shows your game as it is and the bottom screen the 160 rows below it, drawn by a copy. Further down, the view stops at rows 112 to 432 (the whole lower table): the top screen shows rows 112 to 271 and the bottom screen rows 272 to 431, each drawn by a copy at that fixed place, with what your game puts on its screen laid over the top screen (PP-10). So the ball is on the top screen except when the table is at its lowest. (Until v1.3.2's later test builds, your game moved between the screens as the ball went down and up, and the whole table jumped by a screen each time.)

**PP-2.** *Retired in v1.3.2's later test builds:* the rows outside the camera kept from what the game drew, dimmed (replaced by PP-1's live view).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) is always at the bottom of the bottom screen: moved there from your game's picture.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen, as without.

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device. (checked on the device for v1.4.2)*

**PP-7.** *Retired in v1.3.2's later test builds:* events shown on the game screen as the game draws them (replaced by PP-10).

**PP-8.** *Retired in v1.3.2's later test builds:* the flippers drawn over the kept rows (PP-1's copy draws them).

**PP-9. In step.** Every frame each copy is taken from your game again (its state from the frame before last, run that frame and this one at the copy's own place), so everything that happens in your game happens on both screens: a bumper hit uncovering a square of catch mode's portrait shows on whichever screen the portrait is on, as do eggs hatching, the Pokémon you're catching and the like. The copies never drift from your game, and nothing is ever retaken or held. The top screen's copy only runs while its rows are shown (the lower view). The copies run on two more threads, side by side with your game. During the table's opening sweep (the view sliding down the whole table) the copies aren't running: the top screen shows the game as it is (with its score bar), the second screen the logo; they're back two frames after it ends. Nothing else stops them: on Sapphire, the ball spat back out of the Wailmer's mouth keeps both screens live, the view holding still over the lower table while the game's camera follows the ball down. (Until v1.3.2's later test builds, that moment turned the copies off for half a second, and they waited half a second after the sweep.) `pinball_live = 0` in `gba-ds.cfg` turns the copies off: the game plays on one screen with the logo on the other. States saved by v1.3.2's earlier test builds (which lowered the camera limit) are put right as the table comes up. (Until v1.3.2's later test builds, each copy ran on its own and was only taken from your game again when the balls' positions differed: a copy could miss what the game counts only on its own screen, such as the portrait's bumper hits.) *By hand: speed on the device (up to three cores' emulation, with two frames of each copy every frame). (checked on the device for v1.4.2: 60 fps)*

**PP-10. What's fixed to the screen.** What the game puts on the screen rather than on the table (the catch timer, banners such as BALL SAVE) stays at its place on the top screen: your game's own picture there in the table's upper part; in the lower part, the game's BG0 above the score bar and those sprites, drawn on their own, laid over the copy's rows (messages, pauses and the Mart: PP-11). The copies leave it out: BG0, the sprites the game draws at the same place on the screen whatever the camera, and sprites that only show on a copy because the hardware wraps them round from your game's screen (a message's letters). The copies' table sprites (Pokémon, flippers, the ball) are all kept, wherever they are. The screen darkening behind a message is on both screens. (Until v1.3.2's later test builds, the copy left out any sprite that matched one of your game's, wherever it was: Pokémon often went missing on the copy's screen.)

**PP-11. Paused, the Mart, messages.** While the game dims its screen (paused, the Pokémon Mart) or shows a message box (a Pokémon caught), the bottom screen shows your game's whole screen as it is, the score bar included, so the message or menu and the table under it stay together; the top screen goes on showing the table's rows as in PP-1, with nothing laid over it. (The owner's State Saver note "Pause screen".)

## Not covered yet

Nothing the owner wants changed. The bonus stages play on one screen (their own tables: the copy only runs on Ruby's and Sapphire's, whose camera limit it knows); that's how they're meant to be for now. Events (evolution, hatching, the bonus-stage entrances) were checked on the device for v1.4.2. The game always runs with the frame buffer on (Settings' Frame Buffer is shown as always on here): without it the device doesn't reach full speed.

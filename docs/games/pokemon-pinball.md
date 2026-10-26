# Pokémon Pinball: Ruby & Sapphire

| | |
|---|---|
| Releases | `BPPE` USA |
| Profile | `src/profiles.c`: the `BPPE` entry (logo-only, with `tall`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/pokemon-pinball.json` (`tools/regress.py pokemon-pinball`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

Unlike the other games, the second screen shows more of the table itself. The game only draws the 160 rows its camera is on (and loads the table's graphics as it scrolls), so the bottom screen is drawn by a second copy of the game, run in step with yours (the same buttons every frame), whose camera is held 160 rows below yours (PP-9). Until that copy is in step and has scrolled into place, and with `pinball_live = 0`, the rows beyond the camera are kept from what the game drew earlier (PP-2, PP-7, PP-8).

## Rules

**PP-1. The table over both screens.** On a table, the two screens together show 320 rows of it, the upper 160 on the top screen and the next 160 on the bottom one. The view starts at the game's camera and goes down from there, but stops at the table's bottom: so the ball stays on the top screen except near the flippers, when the camera's at the bottom of the table and the ball shows on the bottom screen.

**PP-2. Rows the game isn't drawing.** The rows outside the game's camera are as the game last drew them (their animations stand still until the camera comes back), each line where the game drew it (its scroll can change partway down a frame), never one the game had brightened or darkened at the time, and shown at half brightness, so it's clear they may be out of date; rows it hasn't drawn since the table came up are black. Until the rows above the camera have been seen (after loading a state at the bottom of the table), the top screen shows the game's own picture. A fade forgets them all (another table may follow).

**PP-3. Score bar.** The score bar (the score, the Pokémon caught, the coins and the balls) leaves the game screen for the bottom of the second screen, where it was on the game's.

**PP-4. Title and menus.** On the title, the menus and the field select, the second screen shows the logo (the title's, without its white background or PRESS START).

**PP-5. Swapped screens.** With the screens swapped, the upper rows stay on the top screen (the second screen then) and the game screen shows the lower ones, with the score bar on the top screen.

**PP-7. Events.** While the game puts something over the table on the score bar's layer (more than the catch timer: a Pokémon caught, the Pokémon Mart), the game screen shows the game's own picture, as it draws it, and the second screen the table's rows below it as last seen; nothing is kept from those frames. The table over both screens comes back a few frames after it's gone. (Until v1.3.2's later test builds the event was split between the screens.)

**PP-8. Flippers.** The flippers are always drawn where they are, at full brightness: as the game draws them when they're on its screen, and otherwise as they last looked up (while their button is held: L or left for the left one, R or A for the right one) or down. Until the game has shown a flipper whole in that position, it isn't drawn there.

**PP-9. Live view.** On Ruby's and Sapphire's tables, once their opening sweep is over, the bottom screen is a second copy of the game drawing the 160 rows below yours, live: its flippers, Pokémon, ball and animations as the game draws them. Your game's camera is stopped 160 rows short of the table's bottom (the one change made to it, to the camera's lowest target, which the game uses for nothing else), so at the bottom of the table the flippers and the ball are on the bottom screen. The copy leaves out what's fixed to the screen (the score bar's layer, sprites exactly like yours: banners such as BALL SAVE); the score bar is yours, moved to the bottom screen; a message the game puts over the table (a Pokémon caught) is drawn by the copy whose screen has its picture. The two are compared every frame (the ball's position) and the copy is taken from your game again when they differ (after loading a state, rewinding), scrolling into place in about a second, with PP-2's view meanwhile. `pinball_live = 0` in `gba-ds.cfg` turns it off. Known issue: the screen darkening behind such a message is on both screens. *By hand: speed on the device (the copy doubles the emulation work).*

**PP-6. One screen.** With one screen, the game plays as it is. *By hand: with `layout = single` on the device.*

## Not covered yet

The bonus stages (their own tables, their size not known yet: the view is set up for Ruby's and Sapphire's 432 rows, and grows if a camera goes further).

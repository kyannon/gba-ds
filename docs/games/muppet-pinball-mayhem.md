# Muppet Pinball Mayhem

| | |
|---|---|
| Releases | `AMWE` USA, `AMWP` Europe |
| Dumps | `AMWE` cartridge `58575f65` (tested) · `AMWP` cartridge `8d5034d7` (tested) |
| Profile | `src/profiles.c`: the `AMWE` entry (logo-only, with `tall` and `tall.live.carry`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/muppet-pinball-mayhem.json` (`tools/regress.py muppet-pinball-mayhem`) |
| Status | work in progress (game branch) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list muppet-pinball-mayhem`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The tables are twice as tall over both screens, live, as Pokémon Pinball's and Sonic Pinball Party's are (their rules: [pokemon-pinball.md](pokemon-pinball.md), [sonic-pinball-party.md](sonic-pinball-party.md)): two more copies of the game, taken from yours every frame, draw the rows your game's view doesn't. A table is 359 rows tall, so the two screens show most of it at once: everything but the top 55 rows or the bottom 160.

## Rules

**MPM-1. Logo.** On the title, the menus and the table choice, the second screen shows the logo: the title's name on its badge (the starburst and three bumpers), without the sky, the roller coaster or the Muppets around it.

**MPM-2. The table over both screens, live.** On a table (Kermit's, Miss Piggy's and Gonzo's, so far), the two screens show it in one piece, as if on one tall screen, scrolling smoothly with the ball and never jumping; your game itself is never changed. While your game's view is in the table's upper part, the top screen is your game and the bottom screen the 160 rows below it; further down, the view stops with the table's lowest rows on the bottom screen (the flippers and the drain), so the ball is on the top screen except when it's down near the flippers. Wherever your game's own picture has the rows, it's what's shown; two copies of your game, taken from it every frame and held at their rows, draw the rest, sprites included (the ball, the table's lights and figures). The game's own pans of the table (a ball lost, a new ball, the table coming up) are followed the same way: the two screens move as your game's view does. When the copies start (a table comes up, a state loaded) they take a few frames to reach their rows (at most 16 rows a frame); until then your game shows as it is, with the logo on the second screen.

**MPM-3. Score bar.** The score bar (the player, the balls, the score, and its messages such as BALL SAVED and the bonus count) stays at the top of the top screen, where the game has it, with the dark line under it, as on one tall screen. Nothing moves to the bottom screen: the game has nothing fixed at the bottom of its screen.

**MPM-4. Paused.** While the game is paused (its PAUSE menu of Continue, Restart, Sound and Quit Table over the darkened table), the top screen shows your game's screen as it is, menu and all, and the second screen the logo: a menu, with nothing live to show beside it. Back in play, both screens show the table again at once.

**MPM-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen.

**MPM-6. One screen.** With one screen, the game plays as it is; `pinball_live = 0` in `gba-ds.cfg` turns the copies off too (one screen, the logo on the other). *By hand: speed on the device (two copies, two frames each every frame, as on the other pinball games).*

## Exceptions

MPM-2 (the table over both screens, from copies of the game) is the same exception to the philosophy's GBA screen (a GBA has one 240x160 screen) and cost rules as the other pinball games', with `pinball_live = 0` as the way back.

## Not covered yet

The fourth table (locked until it's won in Adventure), Adventure mode's own screens, and games of two to four players: no states of them yet. If anything other than the pause menu is laid over a table (none seen in play), it shows on the top screen if your game's view is on the top screen; with the lower view it could show lower on the screen than on your game's: to check. Speed on the device.

## Facts found so far (for sessions)

| What | Where / value (USA, `AMWE`; Europe's are the same) |
|---|---|
| On a table | BG0CNT `0x0200`, BG1CNT `0x0202` (the score bar, map `0x1000`), BG3CNT `0x8083` (the table, 256 colours, 256x512, map `0x0000`, fixed: its tiles stream). Lines 0-15: DISPCNT `0xBB41` (mode 1: no BG3) with window 0 (WIN0V `0x0010`, WININ `0x27`: BG0-2); from line 16 (VCount IRQ, 0x08000A7A) `0xBB40`. Line 16 is black |
| Game mode | `0x030013B6`: 4 on a table, 5 paused, 6 then 3, 2 as a ball is lost and the next starts |
| Camera | row `0x030019C8` (BG3VOFS = it - 16, set at the vblank by 0x080008EA; HOFS `0x030019CA`): eased (speed `0x030019EC`) toward 0x120 less the ball's height (0x08004B4E), clamped to 0-0xD7; the game's own pans when `0x030019E8` is 0 (0x08004CC4) |
| Table height | 359 rows (45 tile rows; view rows -16 to 199) on every table: the clamp is a constant |
| BG3 tiles | VRAM `0x2D40` up; the map's tile rows far apart share tiles. 0x08004E40 queues the tile rows coming into view as the camera's tile row changes (`0x90` rows below it, two more) |
| Vblank queue | ring of 64 16-byte entries at `0x0300269C` (source, destination, size in bytes, kind: 0 DMA0, 3 DMA3), start `0x03002AA0`, count `0x03002A9C` (words); added by 0x08006820, done by 0x080064E8 (from 0x08005FBB) |
| Sprites | written to OAM by the vblank code from the objects (0x08005B5E on): no shadow table |
| Copies (profile patches) | the camera's target from `0x030019E0` (unused by the game), not eased, never the game's pans; DISPCNT without mode 1 or window 0 on lines 0-15; the top tile row streamed two rows higher |
| Title | BG0 alone (DISPCNT `0x1100`), from frame 1650 (USA) or 2050 (Europe, after a Ubi Soft screen); PRESS START a sprite |
| Menus | Competition: the table choice (Kermit, Miss Piggy, Gonzo, a locked fourth), balls, players. A held 60 frames launches the ball |

## For the merge

Changelog line (under the open version):

- New game: Muppet Pinball Mayhem (USA, Europe), with its tables over both screens as one tall table, live, like the other pinball games. Paused, the second screen shows the logo. Not checked yet: the fourth table, Adventure mode's own screens, games of two or more players.

Wiki, `wiki/Supported-games.md` (a row in the table and this section):

### Muppet Pinball Mayhem

The tables are spread over both screens as one tall table, live: the ball, flippers and everything on the table move on both screens, and the view scrolls smoothly with the ball. The score bar stays at the top of the top screen. While the game is paused, its pause menu is on the game screen and the second screen shows the logo. On the title and menus, the second screen shows the game's logo. `pinball_live = 0` in `gba-ds.cfg` plays it on one screen.

Wiki, `wiki/Philosophy.md`, rule 6's list of exceptions: "Pokémon Pinball's and Sonic Pinball Party's tables" becomes "Pokémon Pinball's, Sonic Pinball Party's and Muppet Pinball Mayhem's tables".

README: the supported games' count goes up by one.

Shared code (game.c, profiles.h: `tall.live.carry`): the view offset, the first table line of the game's picture, the table's height from the profile, the vblank queue as a ring of entries of any size, the pause probe (`plain`), `keepObj`, and up to 16 patches. Sonic Pinball Party's behaviour should be unchanged (its defaults), but the full suite has to run at the merge.

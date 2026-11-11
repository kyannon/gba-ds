# Muppet Pinball Mayhem

| | |
|---|---|
| Releases | `AMWE` USA, `AMWP` Europe |
| Dumps | `AMWE` cartridge `58575f65` (tested) · `AMWP` cartridge `8d5034d7` (tested) |
| Profile | `src/profiles.c`: the `AMWE` entry (logo-only, with `tall` and `tall.live.carry`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/muppet-pinball-mayhem.json` (`tools/regress.py muppet-pinball-mayhem`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list muppet-pinball-mayhem`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The tables are twice as tall over both screens, live, as Pokémon Pinball's and Sonic Pinball Party's are (their rules: [pokemon-pinball.md](pokemon-pinball.md), [sonic-pinball-party.md](sonic-pinball-party.md)): two more copies of the game, taken from yours every frame, draw the rows your game's view doesn't. A table is 359 rows tall, so the two screens show most of it at once: everything but the top 55 rows or the bottom 160.

## Rules

**MPM-1. Logo.** On the title, the menus and the table choice, the second screen shows the logo: the title's name on its badge (the starbursts, three bumpers and the swoosh under MAYHEM), cut along the logo's own outline, without the sky, the roller coaster or the Muppets around it. (The outline is the official logo's, scaled to the title's: tools/logo_masks/amw.png. The logo is saved as `AMWE-r1.png`, so one cut by an earlier test build is made again.)

**MPM-2. The table over both screens, live.** On a table (Kermit's, Miss Piggy's, Gonzo's and Animal's, in Competition and Adventure, with one to four players), the two screens show it in one piece, as if on one tall screen, scrolling smoothly with the ball and never jumping; your game itself is never changed. While your game's view is in the table's upper part, the top screen is your game and the bottom screen the 160 rows below it; further down, the view stops with the table's lowest rows on the bottom screen (the flippers and the drain), so the ball is on the top screen except when it's down near the flippers. Wherever your game's own picture has the rows, it's what's shown; two copies of your game, taken from it every frame and held at their rows, draw the rest, sprites included (the ball, the table's lights and figures). The game's own pans of the table (a ball lost, a ball saved and the table's run back up to the top, a new ball or the next player's, the table coming up) are followed the same way: the two screens move as your game's view does. Each row is taken from a picture where it's sure to be right: a copy's top 16 rows (the ones your game hides under its score bar, whose tiles it doesn't always keep) only when no other picture has the row. When the copies start (a table comes up, a state loaded) they take a few frames to reach their rows (at most 16 rows a frame); until then your game shows as it is, with the logo on the second screen. They start from the table as your game has it once it's set up, and keep nothing from before: after a state loaded during a game (another table, the menus), no tiles or sprites of the menus or the earlier table show on either screen.

**MPM-3. Score bar.** The score bar (the player, the balls, the score, and its messages such as BALL SAVED and the bonus count) stays at the top of the top screen, where the game has it, as on one tall screen, with its black edge under it: always black, never a row from elsewhere on the table or a sprite passing under the score bar (the game's own lets one show through it now and then). Nothing moves to the bottom screen: the game has nothing fixed at the bottom of its screen.

**MPM-4. Paused, and the game's end.** While the game is paused (its PAUSE menu of Continue, Restart, Sound and Quit Table over the darkened table), the top screen shows your game's screen as it is, menu and all, and the second screen the logo: a menu, with nothing live to show beside it. Back in play, both screens show the table again at once. The same when a game ends (the last ball lost, in Competition or Adventure): from then on your game's screen as it is (its run up the table, the stage curtains closing) and the logo, until the menus.

**MPM-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen.

**MPM-6. One screen.** With one screen, the game plays as it is; `pinball_live = 0` in `gba-ds.cfg` turns the copies off too (one screen, the logo on the other). *By hand: speed on the device (two copies, two frames each every frame, as on the other pinball games).*

**MPM-7. The stage curtains.** Before a table (a game starting, Adventure's next table), the stage curtains and the table's opening pan show on your game's screen as they are, with the logo on the second screen; the two screens show the table once the ball is ready to launch (MPM-2). The game's darkening at a game's end never puts its screen on the bottom one: both screens darken together.

## Exceptions

MPM-2 (the table over both screens, from copies of the game) is the same exception to the philosophy's GBA screen (a GBA has one 240x160 screen) and cost rules as the other pinball games', with `pinball_live = 0` as the way back.

## Not covered yet

Adventure's move from one table to the next (a table's goals completed): not reached yet (the fourth table, Animal's, was opened by memory for the tests: `0x03006865` = 1). If anything other than the pause menu is laid over a table (none seen in play), it shows on the top screen if your game's view is on the top screen; with the lower view it could show lower on the screen than on your game's: to check. Speed on the device.

## Facts found so far (for sessions)

| What | Where / value (USA, `AMWE`; Europe's are the same) |
|---|---|
| On a table | BG0CNT `0x0200`, BG1CNT `0x0202` (the score bar, map `0x1000`), BG3CNT `0x8083` (the table, 256 colours, 256x512, map `0x0000`, fixed: its tiles stream). Lines 0-15: DISPCNT `0xBB41` (mode 1: no BG3) with window 0 (WIN0V `0x0010`, WININ `0x27`: BG0-2); from line 16 (VCount IRQ, 0x08000A7A) `0xBB40`. Line 16 is black |
| Game mode | `0x030013B6`: 4 on a table, 5 paused, 6 then 3, 2 as a ball is lost and the next starts (the next player's in a game of two to four), 7 then 8 at a game's end (the run up the table, the curtains) |
| Ball saved | `0x030019E8` clear and `0x030019E9` set (0x0800A9F8): the view held at `0x030019D4` while the timer `0x030019E0` (32 bits) runs (0x08004D30), then eased to the top by 0x08004D76, every third frame (the game's logic runs at 20 fps then) |
| Menus' data | `0x0300685C`: +3 the table choice's cursor (0-3), +9 the fourth table open (1; checked by 0x08013828 when it's chosen, from the save). Adventure: `0x03001128` 1 |
| Tile lists | one per table, 45 entries of 12 bytes (source offset, VRAM destination, size): Kermit's `0x08090560`, Miss Piggy's `0x0804A324`, Gonzo's `0x0806EB2C`, Animal's `0x080D35E8` (pointer at `0x03007D78`). Rows 20 to 24 apart share VRAM: a run of 20 to 25 rows fits at once, the game needs about 19 |
| Camera | row `0x030019C8` (BG3VOFS = it - 16, set at the vblank by 0x080008EA; HOFS `0x030019CA`): eased (speed `0x030019EC`) toward 0x120 less the ball's height (0x08004B4E), clamped to 0-0xD7; the game's own pans when `0x030019E8` is 0 (0x08004CC4) |
| Table height | 359 rows (45 tile rows; view rows -16 to 199) on every table: the clamp is a constant |
| BG3 tiles | VRAM `0x2D40` up; the map's tile rows far apart share tiles. 0x08004E40 queues the tile rows coming into view as the camera's tile row changes (`0x90` rows below it, two more) |
| Vblank queue | ring of 64 16-byte entries at `0x0300269C` (source, destination, size in bytes, kind: 0 DMA0, 3 DMA3), start `0x03002AA0`, count `0x03002A9C` (words); added by 0x08006820, done by 0x080064E8 (from 0x08005FBB) |
| Sprites | written to OAM by the vblank code from the objects (0x08005B5E on): no shadow table |
| Copies (profile patches) | the camera's target from `0x03001A00` (never read or written by the game: not `0x030019E0`, the ball-saved timer), not eased, never the game's pans nor the ball-saved easing (0x08004CF4); DISPCNT without mode 1 or window 0 on lines 0-15 |
| Title | BG0 alone (DISPCNT `0x1100`), from frame 1650 (USA) or 2050 (Europe, after a Ubi Soft screen); PRESS START a sprite |
| Menus | Competition: the table choice (Kermit, Miss Piggy, Gonzo, Animal's locked), balls, players (DOWN to each row; A on any row starts). Adventure: New Game, Continue; the tables in turn from Kermit's, 5 balls, back to the menus at the end. A held 60 frames launches the ball |

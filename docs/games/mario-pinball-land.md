# Mario Pinball Land

| | |
|---|---|
| Releases | `BMVE` USA, Australia, `BMVP` Europe (Super Mario Ball), `BMVJ` Japan (Super Mario Ball) |
| Dumps | `BMVE` cartridge `70a6d2c1` (tested), Virtual Console `a0d418a4` · `BMVP` cartridge `3b035681` (tested), Virtual Console `3b15e886` · `BMVJ` cartridge `7eb4a336` (tested), Virtual Console `cf9914ae` |
| Profile | `src/profiles.c`: the `BMVE` entry; `BMVP` and `BMVJ` are variants of it, with nothing to move |
| Tests | `tests/regress/mario-pinball-land.json` (`tools/regress.py mario-pinball-land`) |
| Status | in progress |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-pinball-land`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

Every room is one screen, with its own flippers: none is taller than the GBA's screen, so the tall two-screen table of Pokémon Pinball doesn't fit. The game has no map and its pause screen is only three words over the room. What the second screen can hold is the HUD, which the game draws as sprites over the table.

## Rules

**MPL-1. Logo.** The logo is the title's lettering, MARIO PINBALL LAND (BG3 alone, without the sky and the hills or PRESS START). The title comes up after the intro film, which START cuts short. In Europe and Japan the game is Super Mario Ball and the lettering is SUPER MARIO BALL (the Japanese one with its katakana line under it).

**MPL-2. In a room: the HUD below.** In a room (a table, a boss's room, Toad's shop, a mini game) the game's HUD comes down to the second screen: the star counter at the top left, the coins at the bottom left, the B button and the item slot at the bottom right, and the lives and the counter along the top, each at the same place on the second screen as the game draws it. They sit on black, with the logo at its own size in the middle (the idle logo's grey made black, so the HUD reads on it). The game screen shows the room alone: the table, the flippers, the ball and the rest of what the room draws, with the blue drain pipe, and the ball as it goes down it, left where they are.

**MPL-3. Counters as in the game.** The lives and the counter along the top slide in for a moment at the start of a ball and when something changes, and slide off again: they do the same on the second screen, as the game does, and are never on the game screen. The star counter, the coins and the item slot stay up while you play.

**MPL-4. Pause, the shop's prices.** PAUSED, CONTINUE and SAVE & QUIT stay on the game screen, over the room, as do a shop's price with its A and B hints and the target mini game's counter in the middle: they belong to what is on the game screen, not to the HUD. The HUD stays on the second screen while paused.

**MPL-5. Elsewhere: the logo.** Away from a room (the title, the intro film, the ball bonus screens, and between rooms: the ball falling through the drain, the sphere Mario flies through) the second screen shows the logo at half strength on dark grey and the game screen is as the game draws it. The HUD is back with the room, within about half a second of its item slot showing.

**MPL-6. The room's own pieces stay on the game screen.** What a room draws as part of itself stays on the game screen, even where it passes the places the HUD uses: Toad running round the table, along its edges and past its corners, or riding the roller coaster (in whichever colours the room gives him), and the item roulette that slides down over the table, with its frame, its items and its A button. Only the HUD's own pieces go to the second screen. An item on its way to the slot (from a ? block or the roulette, it falls down the right side of the screen into the slot) goes with the slot: on the second screen as it falls, and once it's in, whatever its colours.

**MPL-7. Time Attack.** A Time Attack run is played in the game's rooms, and they are rooms as in MPL-2: the HUD is on the second screen over the logo for the whole run, with the timer at the top right. The star counter and the coins show for the run's first four seconds and then slide off, as the game does; the HUD stays below without them, also after loading a state from later in a run. The level select before a run, and the game over and best times after it, show the logo (MPL-5).

## Not covered yet

- Bosses played through (only their rooms are in the tests): a screen there with priority-1 sprites in its corners would show them on the second screen.
- A won Time Attack run: the runs checked, in all five levels, ended with the ball drained (the game over and best times), not with the level's last room cleared.
- Europe's and Japan's Virtual Console dumps (`3b15e886`, `cf9914ae`) are listed but not tested. Their tests cover the title, the Fun Fair (with Toad and the roulette), the pause, the drain and the ball bonus, and four rooms (Boo, the target game, the shop and Bowser's), the way the USA's do; the other rooms were only looked at in a survey of their sprites (Notes). Their Time Attack and an item falling into the slot weren't seen (the states have no saved progress); their slot, B button and HUD sprites are the USA's.

## Notes

| | |
|---|---|
| Rooms | DISPCNT 0x1340 (BG0, BG1 and sprites) for most tables, 0x1741 or 0x3741 for rooms with a BG2 (the Boo room, Bowser's): no scrolling in any of them. The title is 0x1B40 (BG3 on) |
| HUD | priority-1 sprites; the room's own are mostly 2 or 3 (Toad and the roulette are 1, below). Tile numbers change from room to room (the star counter is 182 in the Fun Fair, 100 in a shop), so the profile goes by place, size and palette: the 16x16 pieces in palettes 0 and 2 in the top 20 rows, the 16x16 pieces in palettes 0 to 4 in the corners below row 112, the 32x32 item in the slot's column (x 206) with a tile below 400, and the 64x64 item slot at the right. The HUD's own palettes are 0 to 4, the items' any; Toad uses 4, 12 or 13 (32x32), the item roulette's frame is 64x64 at x 88 and its A button 16x16 in palette 1 |
| Room or not | the item slot's frame, a 64x64 sprite at (176, 96) in palette 3, is in every room, shop and Time Attack table and not on the title, the level select, the ball bonus screens or the game over and best times (priority-1 sprites too); the title has BG3 on. The star counter's icon (16x16 at (-1, 0), palette 2) shows on exactly the same frames in the 58 rooms checked, but leaves four seconds into a Time Attack run. The adventure's game over (CONTINUE, SAVE & QUIT, QUIT) has the HUD up with BG3 on, so it shows the logo |
| Toad, the roulette | Toad is a 32x32 sprite (tile 566 in the rooms checked, palette 13 in most, 12 on the roller coaster and in Time Attack's first level, 4 in one room), priority 1 when he runs along the table's edges, so he crosses the top rows and both corners; he and the others that run along the right edge in some rooms (32x32, tiles 490-616, palettes 12-15) pass x 206 for a frame. The item roulette slides down from above at x 88: its frame is 64x64 (palette 3), its items 32x32 (palettes 0 and 2) and its A button 16x16 (palette 1) |
| Items | an item from a ? block or the roulette falls from y -26 straight down x 206, 6 pixels a frame, into the slot at (206, 125): 32x32, tiles 140, 206 or 222 in the rooms checked, palettes 4, 11, 12, 13 or 15 |
| Time Attack | after the title's TIME ATTACK, a level select (L and R, the best time below; DISPCNT 0x1340) of the five levels; a run is the level's rooms with the timer (five 16x16 pieces in palette 0 at x 176-224, y 0) in place of the lives and the counter. The star counter and the coins slide off 240 frames in. A drained ball ends the run: the sphere, GAME OVER, BEST TIMES, the title. The records are at 0x03006340, 12 bytes a level (the time in frames, the initials, 1 when the level is open) |
| The ball | at priority 1 only as it goes behind the drain pipe, in the middle (x 102-120, y 137 and down): outside the corners, so it stays on the game screen |
| RAM | lives 0x03000000, score 0x03000004, yellow coins 0x03000008, blue coins 0x0300000C, stars 0x03000014, item 0x0300002C, the room (0 to 0x36) 0x03000030 |
| Europe and Japan | Super Mario Ball. Both ROMs differ from the USA's in most bytes (the code moved), but the intro (the same frames: the title at about 2500, PRESS START at about 3000), the IWRAM layout (lives 0x03000000 and the rest), the sprites' palettes, sizes and places and the stageLinks table (0x087F61FC in Europe, 0x087F8064 in Japan, 90 entries as the USA's at 0x087F5618) are the same, so the variants have no shift and no patch. What differs is the picture: the title's lettering (BG3) is SUPER MARIO BALL, in Japan with a katakana line under it, the Japanese Nintendo logo is blue, and the Japanese HUD uses other tiles for some of its counters (47 different pieces seen in a survey against the USA's 50, all caught by the same rules), its pause words and ball bonus screen in Japanese (sprites in the middle, as before) |
| Pause | START draws PAUSED, CONTINUE and SAVE & QUIT as priority-1 sprites in the middle (y 40-114), tiles near the end of the sprite tiles (602-810, they move from room to room) |

## For the merge

- Shared code: the profile's `hud.onWith` with `onLost` (the split started only while a given sprite shows, kept by `on` alone until that many frames without one: game.c's update_hud), `tools/regress.py --changed` to run.
- Changelog (New games): Mario Pinball Land (USA, Australia, Europe and Japan; Super Mario Ball in Europe and Japan): in a room, in the adventure or Time Attack, the HUD (stars, coins, lives, the counter or the timer, the item slot) on the second screen over the logo, the table alone on the game screen.
- Supported games table: `| Mario Pinball Land (Super Mario Ball in Europe and Japan) | `BMVE` `BMVP` `BMVJ` | in a room: the HUD, with the logo | |`
- Supported games section: the USA and Australia, Europe and Japan releases, checked in the Fun Fair and the Frosty rooms and Time Attack (the USA's), and in the Fun Fair, the Boo room, the target game, the shop and Bowser's (Europe's and Japan's, from their own play; being filled in).

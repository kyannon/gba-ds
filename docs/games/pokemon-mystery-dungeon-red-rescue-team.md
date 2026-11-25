# Pokémon Mystery Dungeon: Red Rescue Team

| | |
|---|---|
| Releases | `B24E` USA, Australia |
| Dumps | `B24E` cartridge `dd0ac86c` (tested) |
| Profile | `src/profiles.c`: the `B24E` entry |
| Tests | `tests/regress/pokemon-mystery-dungeon-red-rescue-team.json` (`tools/regress.py pokemon-mystery-dungeon-red-rescue-team`) |
| Status | in progress |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-mystery-dungeon-red-rescue-team`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game has no map screen to open: in a dungeon, its floor map is the game's BG0 layer, drawn over the field (the minimap in the corner, the floor and level and HP line along the top), and the SELECT button hides the field so the map shows alone. The second screen takes that layer, live.

## Rules

**PMD-1. Logo.** The logo is the title's lettering, POKÉMON MYSTERY DUNGEON RED RESCUE TEAM (BG2 alone, without the sunset behind it or the envelope that flies across it). The title comes about 2000 frames after the health screen (A passes it), the intro's scenes first.

**PMD-2. In a dungeon: the map and the status line.** While you walk about a dungeon floor with nothing open, the second screen shows the game's BG0 as it is, on black: the status line (the floor, the level, HP and its bar) and the floor map, with the dot for where you are (the game's blinking sprite on the map), all live. The game screen shows the field alone, with no map and no status line over it. Both are in the same place on the second screen as on a GBA screen.

**PMD-3. Menus and messages.** While a menu is open (the main menu and the Moves, Items, Team, Others and Ground pages below it, the stairs prompt) or a message waits for A, its windows stay on the game screen, over the field, as the game draws them; the status line does not: it stays on the second screen, live (if something changes HP or the belly while a menu is up, it changes there at once), over the map as it was when the menu opened. The message box that shows what just happened (a move used, an item picked up) is only its box: it goes back onto the game screen where the game draws it, and the map and the status line carry on live on the second screen. (The game clears the whole map from BG0 for a menu or a waiting message, and only its bottom rows for the message box: that is how the profile knows.) Each change is on the frame the game makes it: no frame with half a menu or a flash of the status line. Taking the stairs, the second screen keeps the last map of the floor left through the fade and the new floor's banner, never the windows of the stairs menu; its status line is what the game draws meanwhile (nothing, while it clears the line for the fade).

**PMD-4. SELECT does nothing in the field.** SELECT would hide the field to show the floor map, which the second screen already shows, so on a dungeon's field the game doesn't get it. In the menus it keeps its jobs (Items: sort the list, Moves: set a move) and in the game's floor-map view (a state saved with it on, or a game played on one screen) it still turns the view off. (An exception to the game's own input, made because the second screen shows the map.) With one screen the game gets every button as it is.

**PMD-5. Elsewhere: the logo.** Away from a dungeon floor (the guild, the town, the title, the opening and the cutscenes outside a dungeon) the second screen shows the logo at half strength. Leaving a dungeon, the last map goes after about a third of a second and the logo comes back.

## Known issues

- The game's Map setting (Others > Game options > Dungeon) at Off draws no map: the second screen has the status line alone. At Shade the map shows as the game draws it, dithered.
- A state loaded with a menu open, with nothing shown before it, has the status line alone on black on the second screen until the field is back (there is no map to keep yet).
- Only the USA and Australia release (`B24E`): the European (`B24P`) and Japanese (`B24J`) releases keep their RAM elsewhere and have no profile.

## Not covered yet

- Dungeon cutscenes (a boss's, the end of a dungeon): the map of the floor before can stay on the second screen for a moment. After the stairs it does through the fade and the new floor's banner, until the new floor's own map comes up.
- Dungeon entry from the town and the guild's own screens.

## Notes

| | |
|---|---|
| Screen | DISPCNT 0x7F40 in a dungeon field, in its menus and its messages alike (0x6140 with SELECT's floor-map view on: only BG0 and the sprites). BG0CNT 0x0C00 (tile map 0x06006000), BG1 0x0D01, BG2 0x2E0A, BG3 0x2F0B (the field) |
| gDungeon | the pointer at 0x0203B418: 0 outside a dungeon, the floor's state in one (0x02003AF4 in the tested states) |
| BG0 | row 0 the status line, palette 15; rows 1-16 of columns 1-28 the minimap's tile canvas, palette 14; the game's windows use palette 15 and clear the canvas |
| Windows | a menu or a message waiting for A clears every canvas tile; the message box (rows 15-19) clears rows 15 and 16 of it only. The profile checks the tile at row 1, column 1 for palette 14 in the game's copy of the tile map (EWRAM 0x0202B038, 2 KB, copied to VRAM 0x06006000 at the next vblank), so it knows a frame before the picture changes, and the box by the window's tile at row 16, column 2 in VRAM |
| Menus | with the canvas gone the HUD is only BG0's first 8 rows, the status line (`hud.alt.rows`): the game draws the rest of BG0 itself, on the game screen, and the second screen keeps the last field picture below the line, taken only from frames with the canvas in VRAM |
| Message waiting | Dungeon + 0x1C05F is 1 while a dialogue waits for A (0x0201FB53 with gDungeon at 0x02003AF4) |
| The dot | sprite tile 533, palette 15, priority 0, 8x8; field sprites are priority 3 |
| Input | A attacks, B opens the main menu, SELECT toggles the floor-map view, START the arrows' mode |

## For the merge

- Philosophy page, its exceptions: Pokémon Mystery Dungeon: Red Rescue Team's SELECT kept from the game on a dungeon's field, where all it does is hide the field to show the floor map the second screen already shows; only with two screens, so one-screen play keeps it (PMD-4).
- Shared code: the profile's `swallow` (keys the game doesn't get while probes match: game.c's run_frame) and `hud.alt.rows` with `heldWhen` (the HUD on its other layer only its top rows, the game drawing the rest, the second screen keeping the last picture below them: hud_compose, update_hud), `tools/regress.py --changed` to run.
- Changelog (New games): Pokémon Mystery Dungeon: Red Rescue Team (USA, Australia): in a dungeon, the map and the status line on the second screen, live; menus and messages stay on the game screen, without the status line, which stays on the second screen; SELECT does nothing on the field.
- Supported games table: `| Pokémon Mystery Dungeon: Red Rescue Team | `B24E` | in a dungeon: the map and the status line, live | |`
- Supported games section: the USA and Australia release, checked in Tiny Woods (being filled in).
- For the owner: PMD-4 stops SELECT on the field, an exception to the game's own input (the exception above). Without it, SELECT on the field turns the game screen black until it is pressed again: the game's floor-map view hides everything but BG0, which is on the second screen.

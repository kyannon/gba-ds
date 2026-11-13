# Mario vs. Donkey Kong

| | |
|---|---|
| Releases | `BM5E` USA and Australia · `BM5P` Europe (English, French, German, Spanish, Italian) · `BM5J` Japan |
| Dumps | `BM5E` cartridge `25c394d1` (tested) · `BM5P` cartridge `ca030d61` (tested) · `BM5J` cartridge `7c2b5269` (tested) |
| Profile | `src/profiles.c`: the `BM5E` entry; variants `BM5P`, `BM5J` |
| Tests | `tests/regress/mario-vs-donkey-kong.json` (`tools/regress.py mario-vs-donkey-kong`) |
| Status | in testing (branch `game/mario-vs-donkey-kong`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-vs-donkey-kong`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-11).

## What the second screen is for

The game has no status screen of its own. START's pause box only names the level and its high score, over three choices (CONTINUE, RETRY, EXIT LEVEL); the level select shows a world's high scores and presents; L+R opens a viewer that scrolls round the level with arrows. What the player keeps an eye on while playing is the HUD along the top of the level: Mario's lives, the score, the presents found (or the T, O and Y blocks in a Mini-Mario level), the Mini-Marios left (in Mini-Mario levels and the Donkey Kong fights, where they're Mario's health), Donkey Kong's hits left in a fight, and the time. Those levels are puzzles where what's at the top of the screen (a switch, a ladder, a ledge above a spring) matters, and the HUD sits over the top 30 rows of them. So, as the game would have done on a two-screen GBA, the HUD goes to the second screen and the game screen shows the level alone. That's a part of the live screen moved over, the second of the simple approaches (as Ninja Five-O, [NFO rules](ninja-five-o.md)); nothing is opened on a hidden copy, so the second screen is never behind the game.

## Rules

**MVDK-1. Logo.** The logo (GEN-1) is the title's lettering: in the USA and Europe, MARIO (yellow to orange), vs. (white on its blue burst) and DONKEY KONG (red, outlined in yellow, the O's black stars), with their black shadows and outlines; in Japan, マリオ vs. ドンキーコング in the same colours. Never the sky behind them (light blue with white stars, the white band and its blue stripes), Mario's and Donkey Kong's faces in the corners, the ™, PRESS START or the copyright line; in Japan, not the line MARIO vs. DONKEYKONG in black under the katakana or the row of Mini-Marios either. The USA's and Europe's is 130 rows tall, Japan's 74.

**MVDK-2. In a level: the HUD on the second screen.** While a level is being played (both parts of a world's levels, the Mini-Mario levels, the Donkey Kong fights, the Plus worlds' levels and their fights, the Expert levels, and the two last fights against Donkey Kong), the HUD leaves the game screen and is drawn on the second screen, on black (a live screen, GEN-1), as the game draws it from frame to frame: Mario's lives (top left), the score with the presents under it (or the T, O and Y blocks), the time (top right), and on a second row the Mini-Marios and, in a fight, the M vs. DK badge and Donkey Kong's hits left. It keeps its own layout, moved up the three rows the game leaves above it, so it starts at the second screen's top row; the logo (MVDK-1) is centred in the room below it, at full strength, clear of the HUD's second row.

**MVDK-3. A level starting.** As a level (or its second part) starts, the game first shows it without a HUD: the camera's pan across it (Donkey Kong running off with a Mini-Mario, the key, the toy box) and HERE WE GO! (LET'S GO! in Europe, スタート! in Japan). The second screen shows the logo at half strength until the HUD appears on the game screen, then the HUD (MVDK-2). The same after losing a life, when the level starts again with its pan.

**MVDK-4. What stays on the game screen.** Everything that isn't the HUD stays where the game draws it, even at the top of the screen: the level, Mario and what he carries, the Mini-Marios following him, enemies, items, Donkey Kong and his rope in the fights, HERE WE GO! and TIME'S UP!!, and the level viewer's arrows and B BACK TO GAME (MVDK-5). Points and 1-UPs that float up from what Mario does are the level's own and stay.

**MVDK-5. The level viewer.** In the level viewer (L+R in a level), the HUD stays on the second screen, as in play; the arrows and B BACK TO GAME stay on the game screen over the level.

**MVDK-6. The game's pause box and help.** START's pause box (the level's name and high score, CONTINUE, RETRY and EXIT LEVEL) stays on the game screen, over the level. The game takes the score and time off its screen while the box is up; the second screen keeps the HUD as it was when the game paused, and Mario's lives leave the game screen as in play. A help sign's screen (Mario standing at a ? sign, or a level's first how-to) is the game's own and stays on the game screen whole; the second screen keeps the HUD as it was meanwhile. Losing a life (the moment the screen goes black) keeps it too.

**MVDK-7. Logo elsewhere.** The second screen shows the logo at half strength on dark grey (GEN-1) whenever no level is being played: the Nintendo screens, Europe's language list, the opening film and the other films, the title, Select Data, the options and e-Reader screens, a world's opening (Mario running past the world's scenery, World 1 Start!), the level select and the Expert level select, the how-to-play demo before some levels (LEVEL 1-1 with the buttons beside it; B skips it), the results after a level, the present-catching bonus games, GREAT! YOU WON! after a fight, and the ending.

**MVDK-8. Every release.** The same in the European and Japanese releases, whose HUDs and levels are the USA's; Europe's in any of its five languages.

## How it was checked

A file was unlocked (the RAM copy of file A's save: every level open, Plus and Expert too, from the CodeBreaker "unlock all stages" code's addresses), then every level of the six worlds, their Mini-Mario levels and fights, the Plus worlds and the two last fights was started from the level select and played for a few seconds of seeded random input, and the HUD's sprites listed (tools/oam.py): every HUD piece is a priority-0 sprite in the top rows, and nothing else of priority 0 is there while a level is played but the fights' rope (8x32 pieces, palette 2) and the viewer's arrows (32x32). A life was lost by setting the time to 1, the results and bonus screens reached by setting the game's next state.

## Not covered yet

- The Expert levels and e-Reader levels: expected as the others (the same HUD); not reached yet.
- The two last fights in full (the Toads' barrels, the robot's hands): their opening was checked.
- Europe in another language than English.

## Facts found so far (for sessions)

| What | Where / value |
|---|---|
| The game's state | USA, Japan 0x030009C0 (and the next one at 0x030009C4); Europe 0x030009C0 - 0x40 = 0x03000980. 0x03 Select Data, 0x05 how-to demo, 0x07 title, 0x08 level select, 0x09 Expert select, 0x0D a level (its pan and HERE WE GO too), 0x0E a life lost, 0x10 paused, 0x11 a help screen, 0x14 GREAT! YOU WON!, 0x15 results, 0x16 / 0x17 the bonus games, 0x18 Europe's language list, 0x1E a film, 0x1F a fade, 0x20 a world's opening, 0x21 the level viewer (decomp camthesaxman/mariovsdk, include/main.h; tcrf.net Notes page) |
| Level select | USA: 0x03000BD4 the world (0-5, 6 the last fight), 0x03000BD8 / 0x03000080 the cursor, 0x03000B80 1 on the Plus page (Europe and Japan: +0x60) |
| In a level | 0x0300010C the world (0-based), 0x0300010D the level's kind (Plus: 9 + the level), 0x0300010E 1 in Plus; the time 0x03000B54 (USA) |
| Save (RAM copy) | file A from 0x02032110 (USA): lives 0x02032110, stars 0x02032118; per level 8 bytes from 0x02032120 (0x0101 at +0, 0xC001 at +2: open), 95 of them, the Expert levels from 0x02032430 |
| HUD | all priority-0 sprites with their top in rows 0-17: the lives (tile 103, 32x16, at 8,0), SCORE and its figures (rows 1-20), the presents or TOY blocks (16x16 at row 12, palettes 5, 6, 8-10), the time, the Mini-Marios (16x16 at row 14 or 17, tile 194) and DK's hits (tile 218); visible rows 3-30. The fights' rope is priority 0 too (8x32, palette 2, tiles 266-282), as are the viewer's arrows (32x32, palette 10) and B BACK TO GAME (row 140); the level's own sprites are priority 1-3 |
| Title | the lettering on BG0 (left) and BG1 (right) with the sky; up about 120 frames after START skips the opening (USA, Japan: START at 600; Europe: START at 600 picks English, START again skips the film) |

## For the merge

Changelog line (under the open version's What's new):

- Mario vs. Donkey Kong (USA, Europe and Japan) is supported: in a level, the HUD (lives, score, presents, Mini-Marios, Donkey Kong's hits and the time) moves to the second screen, so the game screen shows the whole level; the logo elsewhere.

Wiki, `wiki/Supported-games.md` (table row and section):

| Mario vs. Donkey Kong | `BM5E` `BM5P` `BM5J` | in levels: the HUD (lives, score, presents or TOY blocks, Mini-Marios, Donkey Kong's hits, time) | |

### Mario vs. Donkey Kong

The USA, European and Japanese releases.

In a level, the HUD leaves the game screen and goes to the top of the second screen, with the logo below it: Mario's lives, the score, the presents (or T, O, Y in a Mini-Mario level), the Mini-Marios and Donkey Kong's hits in the fights, and the time. The game screen shows the level alone, so nothing at its top is hidden. As a level starts (its pan and HERE WE GO!) the second screen shows the logo until the HUD appears. Paused, or at a help sign, the second screen keeps the HUD as it was. Everywhere else (the title, the level select, the films, results and bonus games) it shows the logo.

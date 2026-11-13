# Mega Man Battle Network 2 (Battle Network Rockman EXE 2 in Japan)

| | |
|---|---|
| Releases | `AE2E` USA (and the Wii U Virtual Console, USA and Europe) · `AM2P` Europe · `AE2J` Japan (Battle Network Rockman EXE 2) |
| Dumps | `AE2E` cartridge `6d961f82` (tested), Virtual Console `5e88c34b` (tested) · `AM2P` cartridge `66341f3b` (tested) · `AE2J` cartridge `98e4f096` (tested), Mega Man Battle Network Legacy Collection `046eed8d`, Rev 1 `41576087` (tested), Rev 1, Virtual Console `44d44721` |
| Profile | `src/profiles.c`: the `AE2E` entry (`mmbn2_compose`; the net's maps shared with Mega Man Battle Network, `mmbn_live_view`, addresses in `mmbnNet`); variants `AM2P`, `AE2J` (its logo) |
| Tests | `tests/regress/mega-man-battle-network-2.json` (`tools/regress.py mega-man-battle-network-2`) |
| Status | testing (branch `game/mega-man-battle-network-2`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mega-man-battle-network-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The rules are [Mega Man Battle Network](mega-man-battle-network.md)'s, the game's PET, battles and net being built the same way. What's different: START's menu has a box with MegaMan's HP above his money (the first game's has the money alone), and the MegaMan screen has a Style row once he has a style.

How it fits the [philosophy](../../wiki/Philosophy.md): MegaMan's level, HP, style and buster are shown outside a battle only on the PET's MegaMan screen, a few button presses away (principle 2: information the player would otherwise find in a menu); it's the game's own screen, with the money from START's menu, opened on the hidden copy (principles 3 and 4). Battles are left as the game plays them, on the game screen. The same exceptions as the first game (see Exceptions): the money and the MegaMan screen's panel put together in one frame made from the money box's own orange edge, and on the net, a map of the area, which the game doesn't have, with MegaMan's head marking where he is.

## Rules

**MMBN2-1. Logo.** On the title, on the game over screen (from the moment it comes up, MegaMan's status forgotten) and until MegaMan's status is first taken (MMBN2-2), the second screen shows the logo: the title's MEGAMAN BATTLE NETWORK lettering with its emblem and its 2, without the grid behind it, PRESS START or the copyright lines; in Japan the title's バトルネットワーク ロックマン エグゼ 2 with its purple swoosh, its emblem and the streak of light behind the lettering (from edge to edge, as on the title), without the grid behind them, but for a few pixels of it closed in by the lettering's outline above ロ (a known issue: the logo is cut out from the grid by its colours, reached from outside).

**MMBN2-2. MegaMan's status and money, in the real world.** Walking about with Lan in the real world, and in battles (MMBN2-4; walking about on the net: the area's map, MMBN2-6), the second screen shows the PET's MegaMan screen (START, MegaMan) and the money from START's menu in one frame:
- at the top right, the money as the game draws it in START's menu, in the box at its top right (its dark band and the amount, right-aligned, at the same place across as in the game). That box also has MegaMan's HP above the money: that line is left out, the MegaMan screen showing his HP just below. The box's lighter grey behind the amount is made the dark grey-blue of the panel's top row (behind "MegaMan.exe" and the level), and the rest of the row, left of it, is filled with that colour too, so the money's row and the panel read as one piece;
- right below it, with no gap, the MegaMan screen's panel as the game draws it, the whole width of the screen: MegaMan.exe and his level, his HP as it is now, his style once he has one (STYLE and its name, with its small V2 or V3 beside it at the panel's right edge once it has grown; the game's grid there before), and his buster's Attack, Rapid and Charge, as many lights as the game draws (a Guts style's Attack runs past the usual five). A save edited past the game's limits (a level of 471 from a GameShark code) is shown as the game draws it, stray tiles and all. The green arrow by the rows (the screen's cursor, there while he has a PowerUP to use) is left out, the screen's own background there; so is the screen's message box below the panel ("My status!", "Upgrade what?", with MegaMan's face);
- above the two and below them, the money box's orange edge (its dark outline outside, its bright glow in the middle of the screen), as wide as the screen, made from the box's own top and bottom edges; none at the sides, which would cut into the panel;
- the whole frame centred top to bottom on the screen, black above and below it.

They're opened in the background on a silent copy of the game about once a second, and at once when MegaMan's HP or its maximum, the area or the money changes (back from a battle, an HPMemory, another area, a shop). Your game is never paused.

**MMBN2-3. Elsewhere, the last picture.** In conversations and scenes, in the PET's own menus (the MegaMan screen open in the game too), shops and chip traders, the second screen keeps the last picture: MegaMan's status (MMBN2-2) in the real world, the area's map (MMBN2-6) on the net. Back from a battle, the status is taken again at once and the map made again.

**MMBN2-4. Battles: on the game screen; MegaMan's status on the second.** A battle plays on the game screen as the game draws it: the field, MegaMan's HP, the custom gauge, the custom screen with its chips and cursor, the enemies' names, and the results. The second screen shows MegaMan's status and money (MMBN2-2) as they were when the battle began, on the net too (in place of the map): his HP there is the HP he went in with, his HP in the battle being the game's own, on the game screen; it's taken again as the battle ends. A battle with no status taken before it (a save state loaded in a battle) shows the logo.

**MMBN2-5. One screen.** With one screen, the game plays as it is.

**MMBN2-6. On the net, the area's map.** As the first game's (MMBN-6): walking about on the net, the second screen shows a map of the area MegaMan is in, at a third of its size, centred on MegaMan and following him as he walks, but stopping at the area's edges (an area smaller than the screen sits in its middle): the area's paths and panels as the game draws them, on black, drawn by a silent copy of the game from the layout the game holds for the whole area. Left out: the net's backdrop behind them (a homepage's patterned floor is its backdrop: only the floor's outline and its furniture are on the map), Navis, viruses, Mystery Data and other sprites, anything that animates. Where MegaMan is, his head marks the spot, at the game's own size: his standing sprite facing the camera, cut off at his chin, in his style's colours. It never animates or turns, and only moves with him. The map is whole as soon as MegaMan arrives in an area and is made again each time he does: half a second after he arrives, or once he has stepped off the warp pad he arrived on (the logo until then). It stays up in conversations and the PET's menus on the net; in battles, MegaMan's status instead (MMBN2-4), and the map again after. Tried on every net area an endgame save could be warped to (86, from the first areas to the Undernet's, its arenas and the homepages): each mapped, in under half a second on this machine's one core. *By hand: how the map looks on the device.*

## Exceptions

- **MegaMan's status and money in one frame (MMBN2-2).** As in the first game: the game shows them on two screens of the PET, the money in a box on START's menu, the status on the MegaMan screen. On the second screen they're put together, the money box's own orange frame taken off it and drawn again, from its own top and bottom edges, across the screen above and below both, and the box's HP line taken out (the panel below has the HP). The money and the panel are as the game draws them, but for the box's background, made the panel's top row's colour and carried across the money's row; only the frame and that fill are new, and they're the game's own colours.
- **MegaMan's head on the net's maps (MMBN2-6).** As in the first game (approved Nov 10, 2026): a marker, not a second MegaMan to steer, from his standing sprite facing the camera, never animated or turned.
- **The net's maps, drawn from each area's layout (MMBN2-6).** As in the first game (approved Nov 10, 2026): the game's pictures at a third of their size (principle 1), and a screen the game never draws, put together from the game's memory (principles 3 and 4); it earns its place because the net is a maze of look-alike paths and the game gives you no map of it. Only the game's own tiles and colours, placed by the layout the game itself holds for the area, drawn by the GBA's own graphics hardware on a copy of the player's own game.

## Not covered yet

States made by warping (see "Warping by memory" below) from a new game, in the USA, European and Japanese releases: Lan in ACDC Town and his classroom, START's menu and the MegaMan screen, net areas and a random battle there (its custom screen and results). From three GameShark saves on GameFAQs (USA, from the owner): an endgame save with all five stars (level 100, Hard mode open), one on the net in HeatGuts V3 (a random battle there), and one edited to level 471. Not tried: the story's boss battles, Hard mode's own play, net battles over a link cable, the credits.

## Facts found so far (for sessions)

The engine is the first game's, moved: the same structures at other addresses, found through the code's table of RAM pointers (the literal pool holding `gManager`, `gWorld`, `gCamera`... the first game's at `0x480`, this game's at `0x4B0` in USA's ROM: entry for entry the same). Every release has the same RAM addresses (Europe's table differs only in a ROM pointer; Japan's and its Rev 1's are the USA's).

| What | Where / value (every release) |
|---|---|
| Game state | `0x02009078` (`gManager` `0x02009070` + 8, byte): 0 title, 4 walking about (scenes and conversations too), 8 battle, 0x18 a PET menu; fade `0x02009073` (`gManager` + 3) |
| `gWorld` | `0x02000DC0`: its state (+0, byte: 0x10 starts a map change), area (+4, halfword: area, sub-area; the area's number 0x80 or more on the net: 0x80-0x85, 0x88, 0x90 tried; ACDC Town 0x0000, the classroom 0x0900), a copy of HP (+0x20, +0x22), money (+0x74, `0x02000E34`, a word). HP itself: `0x02001130` current, `0x02001132` maximum (halfwords; what the MegaMan screen prints; the GameShark list's Max HP codes write there) |
| START menu | `0x0200D580`: its state (+0: 0 closed, 4 open, 0x0C going into a menu), its cursor (+6: 0 Folder, 1 Subchips, 2 Library, 3 MegaMan, 4 Mail, 5 Key Items, 6 Network, 7 Save, 8 Return). Its box (BG0, at the top right, x 146-239, rows 0-47): outline `313939` on row 0, row 47 and column 146; its orange frame rows 1-3, 44-46 and columns 147-151 (`CE5A39`, with a glow of `E76310` / `F77B10` / `F78C00` in its top and bottom edges); inside, x 152-239, rows 4-43: a dark band (`4A5A63`, rows 4-7), the lighter grey (`5A6B6B`) behind HP (text rows 10-22) and the money (rows 26-38, right-aligned to x 239, with its Z). The location's name at the bottom right (ACDC Town, Room 5A) |
| PET menus | `0x02007EA0`: the menu (+0, 0x0C MegaMan) and its step (+1, 4 open) |
| MegaMan screen | the panel rows 0-95 (BG1 the panel, its grid and labels; BG2 the values, a patch of grid over STYLE until he has a style, the style's name; BG3 the level), the message box below (BG0, MegaMan's face in sprites). The cursor is a sprite (16x16, tile 1, palette 0, priority 1, x 88, Japan's x 96, by the row it's on); MegaMan is sprites, palette 11, priority 3, left of x 72. The cursor is taken out by copying x 72-119 from a frame drawn without sprites (hiding the sprite on the hidden copy didn't take there). Japan's panel is laid out a little differently ("rockman.exe", the stats' box further right), the probes the same |
| Battle | `gBattle` `0x02004EE0`: its step (+1: 4 the custom screen, 8 fighting). MegaMan's battle HP `0x02008A94`, the enemies' from `0x02008B54`, 0xC0 apart (as in the GameShark list's 1 Hit KO code) |
| The net's maps | `gCamera` `0x02006370` (cameraX +4, cameraY +6, the player's position pointer +0x10, bounds +0x14..+0x23, as in the first game); `gBgDataManager` `0x0200C1F0` (the layout's width +0 and height +1 in tiles; the layout pointer +0xC, `0x02011000`) |
| Warping by memory | `gMapChange` `0x02006880`: type (+0x10) 1, index (+0x12) 1, list (+0x14) pointing at 16-byte entrances (area halfword, pad, direction, x, y, z as 16.16), written to `0x0203FF00` and `0x0203FF10`, and `gWorld`'s state set to 0x10: the game changes map there, as in the first game. `tools/scout` has nothing for it: a harness script (poke lines) does it |
| GameShark | the codes in GameFAQs' list decrypt with GameShark Advance v1's keys (TEA, seeds `09F4FBBD 9681884A 352027E9 F3DEE5A7`): styles owned `0x02000F06`-`0x02000F19`, the style worn `0x02000DC1`, key items from `0x02000E87`, Max HP `0x02001132` |
| Saves | `saves/`: none (the cases' states carry the battery save). GameFAQs' `.sps` files convert with `tools/sharkport.py` (SRAM, 64 KB, no `--magic`); the app started with `--save_dir` and Continue on the title (cursor on CONTINUE, or on CONT. HARD once Hard mode is open: UP then A for the save as it was), its `AE2E.auto.state` on quitting is the state. `us-endgame-room` (2003, level 100), `us-endgame-net` (2017, "The great Lords", area `0x0794`), `us-cheated-save` (2003, level 471) |
| Title | US / Europe: BG2 a circuit pattern, BG3 the lettering and emblem over a grid of lines (`006B00`, `52B500`, `84D600`, `CEFF00`), sprites PRESS START and the copyright. Japan: BG3 the logo on its grid (dark `080808`); sprites PRESS START and the copyright. Up by frame 420 (Japan's fades in later) |

## For the merge

- Changelog line (under the open version): "New game: Mega Man Battle Network 2 (USA, Europe, Japan), as the first game: MegaMan's status with your money in the real world and in battles, a map of the area on the net."
- `wiki/Supported-games.md`, the table: `| Mega Man Battle Network 2 (Battle Network Rockman EXE 2 in Japan) | `AE2E` `AM2P` `AE2J` | MegaMan's status (level, HP, style, buster) with your money, in battles too; on the net, a map of the area | |`
- `wiki/Supported-games.md`, its section:

  ## Mega Man Battle Network 2

  The USA, European and Japanese (Battle Network Rockman EXE 2) releases, as Mega Man Battle Network.

  Walking about in the real world, the second screen shows the PET's MegaMan screen with your money above it, kept up to date about once a second (and at once when your HP, money or area changes): MegaMan's level, his HP, his style once he has one and his buster's Attack, Rapid and Charge, with the money from the PET's menu. They're put together in one frame edged with the money box's own orange border; the MegaMan screen's question box and cursor are left out.

  On the net, the second screen shows a map of the area you're in, at a third of the game's size, centred on MegaMan and following him, stopping at the area's edges; his head, facing you, marks where he is. Battles play on the game screen as the game draws them; the second screen shows MegaMan's status and money as they were when the battle began. In conversations, scenes, the PET's menus and shops it keeps the last picture; at the title and on the game over screen, the logo.

  Known issues: boss battles and link-cable net battles haven't been tried.
- `wiki/Philosophy.md`, its list of exceptions: "Mega Man Battle Network 2's status screen and money, and its maps of the net, as the first game's."
- README's count of games, one more.
- `tools/make_dumps.py` for the Dumps row.

# Mario & Luigi: Superstar Saga (Mario & Luigi RPG in Japan)

| | |
|---|---|
| Releases | `A88E` USA · `A88P` Europe (English, French, German, Spanish, Italian) · `A88J` Japan (Mario & Luigi RPG) |
| Dumps | `A88E` cartridge `e718d850` (tested), Virtual Console `f87ea3c3` · `A88P` cartridge `170cc574` (tested), Virtual Console `6a5bd4f0` · `A88J` cartridge `4ef93d41` (tested), Virtual Console `bee3055a` |
| Profile | `src/profiles.c`: the `A88E` entry (`ml_compose`); variants `A88P` (its language menu), `A88J` |
| Tests | `tests/regress/mario-and-luigi-superstar-saga.json` (`tools/regress.py mario-and-luigi-superstar-saga`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-and-luigi-superstar-saga`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**ML-1. Logo.** On the title and in battles (ML-3, behind the battle's boxes and meters: ML-5), the second screen shows the logo: MARIO & LUIGI and SUPERSTAR SAGA (in Japan マリオ&ルイージRPG), without the sky, PRESS START, the suitcase or the copyright line: no sky blue at the lettering's edge, no specks of the background around its shadow, no stray dots in its corners.

**ML-2. Bros. Info, always.** Walking about, the second screen is the Bros. Info screen (SELECT's menu, its third icon), as the game draws it, filling the screen, once it has settled (its opening and the brothers' turn over) and only if it is Bros. Info (its frame checked; never the field or a menu): the page of the brother you're controlling, the one in front (START swaps them; the page follows within a moment). It's opened in the background on a silent copy of the game about once a second; your game is never paused. (Until v1.4.5's later test builds, Mario's and Luigi's pages in turn, 5 seconds each.)

**ML-3. Elsewhere.** In a battle, the second screen shows the logo, with the battle's boxes and meters over it (ML-5); Bros. Info again once you're walking about after it. In scenes, conversations and the game's own menus it keeps the last picture. Nothing else leaves the game screen.

**ML-4. One screen.** With one screen, the game plays as it is.

**ML-5. Battles: the boxes and meters on the second screen.** In a battle, from its first frame to the results:
- Mario's and Luigi's meters (HP and BP, bottom left on the game screen) go to the top left corner of the second screen, beside the top right box and above the item list's cursor.
- The information boxes at the top right (what the block does: "Use an Item", "Solo Attack") and the bottom right (the buttons) go to the same places on the second screen, with their text. So do the menus that open on the right side (the item list), with their cursor and icons.
- The box at the top left while you aim (the enemy's name) stays on the game screen, by the enemies, where the meters would be on the second screen.
- The command blocks (RUN, ITEM, SOLO...) and the A button prompt stay on the game screen, over the brother whose turn it is, and so does everything else: the brothers, the enemies, the attacks and their action command prompts, and the damage numbers over an enemy or a brother.
- A conversation in a battle (the tutorials' speech balloons) stays on the game screen, by whoever is speaking.
- The results (EXP, coins, items won) stay on the game screen as the game draws them, with the logo alone on the second screen, until you're walking about again.

## Not covered yet

Battles (ML-5): tried in the first battles of the story (the Koopa Cruiser's Goombas, with its tutorial, and the Fighter Flies of Stardust Fields): choosing a block, the item list, aiming at an enemy, attacks and dodging, the results. Bros. Attacks and their list, boss battles, and battles with more than a few items haven't been tried; what the boxes and meters do there is to be checked.

Walking about (ML-2): every room from 0x000 to 0x1FF was tried in the USA release, started from an endgame save (see "Changing rooms" below): Bros. Info is taken in all 454 where you walk about, and in scenes and minigames (where the game takes no SELECT) the last picture stays. Only from the start of the story to the Koopa Cruiser and Stardust Fields was that by playing; the rooms after the first file came in from a save's file, not by walking in, so a room that only shows something special when you arrive the usual way hasn't been seen that way. Rooms above 0x1FF, and Europe and Japan beyond their titles, aren't tried: their screen bytes are taken to be at the USA's addresses. One early report showed the field on the second screen, from before the report's 10 seconds: not reproduced; the frame check keeps anything but Bros. Info off.

## Facts found so far (for sessions)

| What | Where / value (USA) |
|---|---|
| Screen | `0x02000008`-`0x0200000F`: the game's screen (`0x02000008`: 8 walking about, its menus too, 3 a battle, 2 the title), its mode (`0x02000009`: 21 walking about, 10 as SELECT's menu opens, 26 in the menus) and the stack of scenes it's in, in pairs (walking about `05 40 06 42 07 44`; SELECT's menu `02 09 03 08`; Bros. Info `02 0B 02 0C 03 08`, Luigi's page `02 0D 02 0E`): written only as they change, by the game's scene code. Walking about is set (as the halfword `0x1508`) by the one room setup (`0x08022FCC`, the only call of `0x080EFA14`), with constants: the same on every map. Seen on five rooms (`0x6F` and `0x13A` on the Koopa Cruiser, `0x71` into its battle, `0x18` and `0x15` in Stardust Fields), each taking Bros. Info. SELECT's menu is up some 50 frames after SELECT; DOWN is taken 10 to 15 frames after that. (Until v1.4.11's test builds `0x02000C99`: a DMA queue entry's transfer count, written every frame by `0x08018322`, which read 1 walking about on the first maps but flickers 0 / 1 elsewhere, as after a player's battle in Stardust Fields: every capture failed there) |
| Room | `0x03002332` (halfword): the room's number, from the exit's record as you go through it (`0x083A9644` for the Koopa Cruiser's to `0x13A`). Changing it (or the record) as you walk doesn't load another room's map: something else picks that |
| Changing rooms (for sessions) | Start a file in another room: at the file screen, the file's room is in RAM at `0x02001E90`, `0x02001E9C` and `0x0300231E` (and in the save at offsets 0x18 and 0x24 of the file's block); write the room there, then A twice (the first is swallowed), and the file starts in that room, its own map and graphics, the brothers where the file left them in its own room (off the path, behind scenery, in some rooms). From `us-endgame-file-screen.ss` (an endgame save at level 38, Bowser's Castle Great Door, `0x19F`): `poke 0x02001E90 2 ROOM`, `poke 0x02001E9C 2 ROOM`, `poke 0x0300231E 2 ROOM`, `wait 30`, `hold A 3`, `wait 30`, `hold A 3`, `wait 250` |
| Every room (Nov 6) | Rooms 0x000-0x1FF that way, then the capture (harness `map`): 454 walking about, Bros. Info taken; 45 scenes (conversations, the star into a battle, cut-scenes, minigames: mode 21 too, but the game takes no SELECT there, so the last picture stays, ML-3); 11 straight into a battle (the brothers on an enemy or a boss); `0x060` mode 28 and `0x1C1` state 12. Cases from eight of them (a town, Beanbean Castle outside, a jungle, a cave, a desert, an ice area, Bowser's Castle, a later town) and a scene (`0x00A`, a conversation). Not looked at: rooms above 0x1FF |
| Saves | `saves/A88E-*.sav` in the fixtures: five endgame files from GameFAQs' GameShark saves (`tools/sharkport.py --magic MALRPG`): levels 38, 44 and 45 at Bowser's Castle Great Door, 99 at Beanbean Castle Gates, and an edited one (file 3, level 55, Koopa Cruiser Hold #1). The save's data starts `MALRPG` in the game's order; mGBA's .sav has each 8-byte block reversed |
| Menu icon | `0x030040A9`: 0 Items, 1 Equipment, 2 Bros. Info |
| Game state | `0x02000008`: 8 walking about (its menus, Bros. Info, the star into a battle too), 3 in a battle, 2 the title |
| Brother in front | `0x02004F4C`: 0 Mario, 1 Luigi (START swaps); Bros. Info always opens on Mario's page, R turns to Luigi's |
| Display | walking about DISPCNT `0x1F40` (here), the menu `0x1D40`, Bros. Info `0x7F40`, the title `0x1741` |
| Results | `0x02036EB0` (4 bytes): 0 in a battle until its results come up (as the screen dims, after the meters have gone), then a pointer (`0x0203xxxx`) on into walking about. (`0x0200086C` is 0 in a battle, 12 in the tutorial one, and 15, 21 or 7 in the results, depending on the path: not used) |
| Battle layers | DISPCNT `0x7F40`: BG0 the sky, BG1 the battlefield, BG2 the text (the boxes' and the tutorials' balloons), BG3 the boxes' frames (screenblock 31, `0x0600F800`, palette 8; drawn as they slide in, cleared as they go; empty in a conversation). Sprites: priority 0 only for the meters (palettes 11, 12, tiles 73-111; their digits at x 4-93, rows 130-142), the damage numbers (the same digits: 8x16 sprites, tiles 73-91 for 0-9, palette 11, up to four of them, over whoever is hit: rows 44-112 seen, the lowest over Luigi at the bottom left), the menus' cursor and icons (palette 11, tiles 168-171, 240-244), a speech balloon's marker and edges (palette 15, tiles 4-17) and the results' pieces; everything else priority 2 |
| Boxes up | BG3's map entry at row 1, column 29 (`0x0600F87A`: the top right box's right edge) or row 18, column 29 (`0x0600FC7A`: the bottom box's), palette 8 (`0x8xxx`) while there |
| Stats | Mario's from `0x02004866` (halfwords: HP, its maximum, HP again; BP x3; POW x2 at `0x02004872`; DEF x2; SPEED; STACHE x2), Luigi's the same from `0x020048A2` |
| Damage | the value a number shows, set a few frames before it does: `0x02009382` for a brother's hit on an enemy, `0x02009AA2` for an enemy's on Luigi (poked to get any number, 0 to 9999: the 9870 and 6543 cases) |
| Title | BG1 the sky, BG2 MARIO & LUIGI, sprites SUPERSTAR SAGA, PRESS START and the suitcase; START past the intro by frame 600. Europe: a language menu first (A at frame 700) |

## For the merge

Changelog (under the open version, "Changes" or "New"):

- **Mario & Luigi: Superstar Saga:** in battles, the information boxes (what the block does, the buttons) and the menus that open on the right (the item list, with its cursor) move to the same places on the second screen, and Mario's and Luigi's HP and BP meters to its top left corner, over the logo. The command blocks and the A prompt stay over the brother whose turn it is, and so does the name of the enemy you're aiming at; the tutorials' speech balloons and the results after a battle stay on the game screen. Tried in the first battles of the story; Bros. Attacks and boss battles haven't been tried yet.

wiki/Supported-games.md, the game's section (replacing its battles line), and its table row ("Bros. Info of the brother you're controlling; in battles, the boxes and meters"):

- **In battles,** the second screen has the battle's boxes and meters, over the logo: Mario's and Luigi's HP and BP at the top left, what the highlighted block does at the top right, the buttons at the bottom right, and the menus that open on the right side (the item list) where they are on the game screen. The game screen keeps the command blocks and the A prompt over the brother whose turn it is, the enemy's name while you aim, and everything else. Speech balloons in a battle and the results after it stay on the game screen. Not tried yet: Bros. Attacks and their list, boss battles.
- On the title, the second screen shows the logo; in scenes, conversations and the game's own menus it keeps its picture.

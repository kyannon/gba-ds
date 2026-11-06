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

Tried in the USA release from the owner's reports (the title, walking about at the start of the story, Bros. Info open, a conversation). One report showed the field on the second screen, from before the report's 10 seconds: not reproduced; the frame check keeps anything but Bros. Info off. Conversations, later areas (more equipment, badges) and Europe and Japan beyond their titles aren't tried yet: their screen and menu bytes are taken to be at the USA's addresses (both read the USA's title value at their titles).

## Facts found so far (for sessions)

| What | Where / value (USA) |
|---|---|
| Screen | `0x02000CDD`: 0 walking about (also on the title and in battles), 1 SELECT's menu (some 50 frames after SELECT, as the suitcase opens; DOWN is taken 15 frames after that), 2 Bros. Info. (Until v1.4.11's test builds `0x02000C99`: a scratch byte that read 1 walking about on the first maps, but flickers 0 / 1 elsewhere, as after a player's battle in Stardust Fields: every capture failed there) |
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

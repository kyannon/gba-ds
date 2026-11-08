# Mega Man Battle Network (Battle Network Rockman EXE in Japan)

| | |
|---|---|
| Releases | `AREE` USA · `AREP` Europe · `AREJ` Japan (Battle Network Rockman EXE) |
| Dumps | `AREE` cartridge `1d347971` (tested), Virtual Console `1d5d0cb6` · `AREP` cartridge `1a7fb4fa` (tested), Virtual Console `1a16c13d` · `AREJ` cartridge `d9516e50` (tested), Virtual Console `109f133b` |
| Profile | `src/profiles.c`: the `AREE` entry (`mmbn_compose`); variants `AREP`, `AREJ` (its logo) |
| Tests | `tests/regress/mega-man-battle-network.json` (`tools/regress.py mega-man-battle-network`) |
| Status | testing (branch `game/mega-man-battle-network`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mega-man-battle-network`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

How it fits the [philosophy](../../wiki/Philosophy.md): MegaMan's level, HP, armor and buster are shown outside a battle only on the PET's MegaMan screen, a few button presses away (principle 2: information the player would otherwise find in a menu). In a battle, the custom screen (where the chips are picked) covers MegaMan's half of the field; with a second screen it has a screen of its own, and the field is whole on the game screen (principle 1: the only change to the game screen is something taken off it that now has its own screen). Both are the game's own screens: the MegaMan screen and the PET menu's money counter opened on the hidden copy, the custom screen moved over as the game draws it (principles 3 and 4). One exception (see Exceptions): the money counter and the MegaMan screen's panel put together in one frame made from the counter's own orange edge, as Final Fantasy Tactics Advance's turn order and Laws are.

## Rules

**MMBN-1. Logo.** On the title, in battles while the custom screen is closed (MMBN-4), and until MegaMan's status is first taken (MMBN-2), the second screen shows the logo: the title's MEGAMAN BATTLE NETWORK lettering with its emblem, without the grid behind it, PRESS START or the copyright lines; in Japan the title's ロックマン エグゼ with its バトルネットワーク banner, its purple swoosh and the emblem, without the blue behind them.

**MMBN-2. MegaMan's status and money, always.** Walking about (Lan in the real world, or MegaMan on the net), the second screen shows the PET's MegaMan screen (START, MegaMan) and its money counter (START's menu, its top right corner) in one frame:
- at the top right, the money counter as the game draws it, without its own orange frame (its dark band and the amount, at the same place across as in the game), its lighter grey behind the amount made the dark grey-blue of the panel's top row (behind "MegaMan.exe" and the level), and the rest of its rows, left of it, filled with that colour too, so the counter's row and the panel read as one piece;
- right below it, with no gap, the MegaMan screen's panel as the game draws it, the whole width of the screen: MegaMan.exe and his level, his HP as it is now (the game refills it after each battle, in part only while the PET's battery is low), his armor when he has one, and his buster's Attack, Rapid and Charge. The green arrow beside the armor (the screen's cursor) is left out, the screen's own grid there; so is the screen's message box below the panel (its question to the player, "Boost which feature?", with MegaMan's face);
- above the two and below them, the money counter's orange edge (its dark outline outside, its bright glow in the middle of the screen), as wide as the screen, made from the counter's own top and bottom edges; none at the sides, which would cut into the panel;
- the whole frame centred top to bottom on the screen, black above and below it.

(In the first test build: the panel alone at the top, without the money; in the second, the counter kept its own lighter grey and had black left of it; in the third, whatever was behind the counter in the game (the net's paths, a Navi walking by) showed through it in its lighter grey now and then: it's taken without it since.) They're opened in the background on a silent copy of the game about once a second, and at once when MegaMan's HP or its maximum, the area or the money changes (back from a battle, an HPMemory, another area, Mystery Data, a shop). Your game is never paused. The counter is as the game draws it with nothing behind it: the net's paths or a Navi walking behind it in the game never show through it.

**MMBN-3. Elsewhere, the last picture.** In conversations and scenes, in the PET's own menus (the MegaMan screen open in the game too), shops and the chip trader, the second screen keeps the last picture of MMBN-2. Back from a battle it shows the logo until MegaMan's status has been taken again, so an old HP or level is never shown.

**MMBN-4. Battles: the custom screen on the second screen.** In a battle, from the moment the custom screen starts sliding in until it has slid out again, the whole custom screen goes to the second screen, where the game draws it (the left half), on black: the chip's picture with its name, code and damage (or its description, with R), the chips dealt and the ones picked, OK and ADD, the cursor, and MegaMan's HP beside it; in the tutorial battles, the arrows that point into it go with it. The game screen shows the whole battlefield instead, MegaMan and his side of the field included, as it stands while the custom screen is up. Everything else in a battle stays on the game screen as the game draws it: MegaMan's HP, the custom gauge and the chip's name while fighting, the battle's start, and its results (the second screen shows the logo then); and, in the tutorial battles, the teacher's messages with her portrait and the arrows pointing at the viruses on the field, also while the custom screen is up. (In the first three test builds the portrait and those arrows went to the second screen, away from her message and the viruses; in the fourth, once she started talking over the custom screen, it came back to the game screen and the second showed the logo.)

**MMBN-5. One screen.** With one screen, the game plays as it is.

## Exceptions

- **MegaMan's status and money in one frame (MMBN-2).** The game shows them on two screens of the PET, the money in a small box on START's menu, the status on the MegaMan screen. On the second screen they're put together, the money counter's own orange frame taken off it and drawn again, from its own top and bottom edges, across the screen above and below both. The counter and the panel are as the game draws them, but for the counter's background, made the panel's top row's colour and carried across the counter's row; only the frame and that fill are new, and they're the game's own colours, so the whole still reads as the PET's. As Final Fantasy Tactics Advance's turn order and Laws (FT-9).

## Not covered yet

Tried on three endgame saves from GameFAQs (two USA, one European: walking about on the net and in Lan's room, a conversation, the PET's menus, a battle with its custom screen, its results and back), the first tutorial battle's custom screen (the owner's states: the teacher's message, her arrows over the viruses) and the first minutes of a new game in the USA release (Lan in his room before he has his PET: START opens nothing, the logo stays). Not tried: the tutorial battles' arrows pointing into the custom screen (they'd go with it), the later tutorials, boss battles, the ElecMan chapter's PET battery (HP only partly refilled after a battle), net battles over a link cable, the final battle, the credits, and the Japanese release beyond its title and the start of a new game (its RAM is at the USA's addresses there).

## Facts found so far (for sessions)

The disassembly at github.com/Silenthal/bn1 (USA) names the RAM (its `ld_script.ld`) and the code; the European and Japanese releases have the same addresses.

| What | Where / value (USA) |
|---|---|
| Game state | `0x02006CB8` (`gManager.gameState`, byte): 0 title and file menu, 4 walking about (`World_Main`; scenes and conversations too), 8 battle, 0x0C transition, 0x10 demo, 0x14 splash (Capcom logo), 0x18 a PET menu, 0x1C shop, 0x20 game over, 0x24 chip trader, 0x28 credits |
| HP | `0x0200022C` current, `0x0200022E` maximum (halfwords, `gWorld.hpCurrent` / `hpMax`; what the MegaMan screen prints). In a battle the battle's own HP (MegaMan's actor, `0x020066D0`; the viruses' from `0x02006790`, 0xC0 apart); after it, `Battle_SetPostBattleHP` sets this to the battle's HP plus the maximum times the PET's battery charge (`0x020003D5`, 10 out of 10 but in the ElecMan chapter) over 10, at most the maximum: full, as a rule |
| Area | `0x02000214` (halfword: area, sub-area) |
| START menu | `gStartMenu` `0x0200ACF0`: its state (byte +0: 0 sliding in over 13 frames, 4 open, 0x0C going into a menu), its cursor (+6: 0 Folder, 1 Library, 2 MegaMan, 3 Email, 4 Items, 5 Network, 6 Save, 7 Return). It takes a press held 2 frames or more |
| PET menus | `gMenu` `0x020062E0`: the menu (+0: 0 Folder, 4 Library, 8 MegaMan, 0x0C Email, 0x10 Items, 0x14 Network, 0x18 Save) and its step (+1: 0 setting up, 4 open, 8 closing). Fade: `0x02006CB3` (`isFadeActive`) 1 while the screen fades |
| Money counter | START's menu (BG0, palette 13, map rows 0-3, columns 21-29: x 168-239, rows 0-31; at the screen's right edge, so it has no right end): outline `313939` on row 0, row 31 and column 170; its orange frame rows 1-3, 28-30 and columns 171-175 (`CE5A39`, with a glow of `E76310` / `F77B10` / `F78C00` in its top and bottom edges, tiles 0xB0-0xBC, 0xB4 the brightest at x 200-207); inside, x 176-239, rows 4-27: a dark band (`4A5A63`, rows 4-7) and the amount (`5A6B6B` behind, right-aligned to x 238). The same on every frame once the menu is open (state 4). The amount is `gWorld.zenny`, `0x02000284` (a word; the endgame saves hold 0x0FFFFF, shown cut to its last digits). The counter isn't opaque in the game: what's behind it (BG2, the world; sprites) shows through it in places in the lighter grey (the net's path edges, a Navi), so it's taken from a frame drawn with BG0 alone |
| MegaMan screen | DISPCNT `0x1F40`; BG1 (prio 3) the panel and its grid, BG2 (prio 2) the values, the armor's name and the cursor arrow (tiles 0x209-0x20C, x 122-134, rows 27-39, bobbing), the message box on rows 96-159 (BG0/BG2) with MegaMan's face (sprites, priority 0, palette 14, blinking). The panel is rows 0-95 (a dark line, `292929`, from row 96) |
| Battle | `gBattle` `0x02003710`: its step (+1: 0 setting up, 4 the custom screen, 8 fighting) and sub-step (+2: in the custom screen 4 open, 8 and 0x0C closing waits). Custom gauge full: L or R opens it. The results (Delete time, Busting level, the data) slide in on BG3 too, BG3HOFS 0xF0 to 0, still in step 8; A twice back to walking about |
| Battle layers | DISPCNT `0x1F40`: BG1 the net's background, BG2 the panels, BG3 (prio 1, 512 wide) the custom screen and, to its right, MegaMan's HP box, the custom gauge and the chip's name. BG3HOFS is 0 while fighting (the HP box at the top left) and slides 0x1F8 to 0x188 (8 a frame) as the custom screen comes in (the HP box beside it), back as it goes: the custom screen is up exactly while BG3HOFS is 0x188-0x1F8 (a multiple of 8). In the tutorial battles the custom screen stays up while the teacher talks over it (her message on BG0 at the bottom) with the battle's step already 8, so the step isn't checked |
| Custom screen sprites | priority 0: the cursor (tiles 936-939, palette 13, its four corners); in the tutorial battles also the teacher's portrait by her message (BG0, at the bottom; portrait priority 0, palette 14, tiles 960-988, x 14-62, rows 104-152) and her arrows (palette 13, tiles 954-958: over the viruses at x 180-220 in the first). So the HUD's sprites are priority 0, palette 13, with their left edge at x 119 or less (the custom screen's width). The chips' icons, picture and the OK / ADD buttons are BG3. Everything on the field (MegaMan, viruses, their HP) is priority 2 |
| Title | US / Europe: BG2 the grid, BG3 the lettering and emblem (rows 20-90), sprites PRESS START and the copyright. Japan: BG3 the logo on a blue (`52B5FF`) fill, the whole screen; sprites PRESS START BUTTON and the copyright. Up by frame 300 |
| Saves | `saves/AREE-endgame-lv79-gameshark.sav` (MegaMan on the net, "Last Battle"), `AREE-endgame-lv100-gameshark.sav` (Lan's room), `AREP-endgame-lv100-gameshark.sav` (on the net) in the fixtures, from GameFAQs' GameShark saves (`tools/sharkport.py`, no `--magic`: SRAM, 64 KB, with `ROCKMAN EXE 20010727` in it, Europe `20010807`). The harness doesn't load a battery save: the states were saved by the app started with `--save_dir` (Continue on the file menu) |
| Viruses' HP (for sessions) | the busted results came from `eu-battle-gauge-full.ss` with both Rattys' HP set to 1 (`poke 0x02006790 2 1`, `poke 0x02006850 2 1`), then the buster up and down a row |

## For the merge

Changelog line (under the open version's What's new):

- Mega Man Battle Network (USA, Europe and Japan) is supported: walking about, the second screen shows MegaMan's status and money from the PET (his level, HP, armor and buster, and your zenny) in one frame; in battles, the custom screen moves to the second screen, so the battlefield is whole while you pick your chips.

Wiki, `wiki/Supported-games.md` (table row and section):

| Mega Man Battle Network | `AREE` `AREP` `AREJ` | MegaMan's status (HP, level, armor, buster) and money; the custom screen in battles |

### Mega Man Battle Network

The USA, European and Japanese (Battle Network Rockman EXE) releases.

Walking about, in the real world or on the net, the second screen shows the PET's MegaMan screen with your money above it, kept up to date about once a second: MegaMan's level, his HP, his armor and his buster's Attack, Rapid and Charge, and the zenny counter from the PET's menu. They're put together in one frame drawn from the money counter's own orange edge; the MegaMan screen's question box and cursor are left out. In conversations, scenes, the PET's menus and shops it keeps the last picture.

In a battle, the custom screen (where you pick the chips) slides in on the second screen instead of over the battlefield, with MegaMan's HP beside it, so the whole field shows on the game screen while you choose; you pick the chips with the buttons as usual. While fighting, the battle is on the game screen as the game draws it and the second screen shows the logo.

Known issues: the tutorial battles and link-cable net battles haven't been tried.

Philosophy page, `wiki/Philosophy.md` (rule 6's list of exceptions, after Final Fantasy Tactics Advance's):

  - Mega Man Battle Network's status screen and money counter, from two of the PET's screens, in one frame made from the counter's own orange edge;

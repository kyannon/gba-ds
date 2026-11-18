# Final Fantasy VI Advance

| | |
|---|---|
| Releases | `BZ6E` USA · `BZ6P` Europe (English, French, German, Spanish, Italian) · `BZ6J` Japan |
| Dumps | `BZ6E` cartridge `d708f5ab` (tested) · `BZ6P` cartridge `d15deba5` (tested) · `BZ6J` cartridge `d67478fe` (tested), Virtual Console `c8d9aacb` |
| Profile | `src/profiles.c`: the `BZ6E` entry (`ff6_compose`) |
| Tests | `tests/regress/final-fantasy-vi-advance.json` (`tools/regress.py final-fantasy-vi-advance`) |
| Status | in progress (branch `game/final-fantasy-vi-advance`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-vi-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-11).

## Why these screens

Asked what the game would have put on a second GBA screen: the one screen a player keeps opening while walking about is START's menu, which shows the whole party at once (portrait, name, class, level, HP and MP), where the party is, and the play time, steps and gil. The game has no map screen of its own: on the world map it draws a small map in a corner of the game screen, which stays where it is (FF6-8). So the second screen follows the Dawn of Souls games ([final-fantasy-1-2-dawn-of-souls.md](final-fantasy-1-2-dawn-of-souls.md)): START's menu without its command list, taken from a hidden copy, kept as the party screen through battles, and the logo when nothing is going on. (The owner's choices there: the party shown in a battle as it was before it, and the logo once the party has been wiped out.)

## Rules

**FF6-1. Logo.** The logo is the title's FINAL FANTASY VI lettering with the ADVANCE bar under it (BG0), without PRESS ANY BUTTON, the New Game / Load Game / Extra list or its hand, or the copyright line.

**FF6-2. Outside the game.** On the title, the Load Game and Extra screens (Bestiary, Music Player) and before them, the second screen shows the logo, any pictures from a game played before are dropped, and no captures are tried. Told by the party roster not being set up: the game keeps its characters in 16 records of 37 bytes from 0x02001600 (the first byte of each the character's number, from 0; the name from the third byte, 6 bytes, in the game's own letter codes; the level at +8, HP at +9, maximum HP at +0xB, MP at +0xD, maximum MP at +0xF; the same in all three releases), set up only in a game: outside one the first byte is 0xFF or 0 and the level 0. In a game: the first record's character number is 0 and its level above 0.

**FF6-3. A game just started.** On the first scenes of a new game, before any picture has been taken, the second screen keeps the logo (nothing opens there: the hidden copy's menu never comes up).

**FF6-4. Walking about: the party screen.** Walking about anywhere (the world map, towns, dungeons, the airship, a Veldt), the second screen shows START's menu on the hidden copy, as the party screen: the party's portraits, names, classes, levels, HP and MP, the place name, and the time, steps and gil, without the command list (Items, Abilities, Equip... Save) and without the hand. The command window's frame stays, empty, where the list was; the game draws the list as text on the same layer as the party's, so the list's tiles are cleared on the hidden copy (the live game is never touched). It's taken again at every refresh, and at once on entering another map. Told by START's menu being up: the display registers' values in its main screen (DISPCNT 0x1540 with BG0's control 0x0F49 in all three releases).

**FF6-5. A menu or screen open in the game.** With START's menu, one of its screens (Items, Abilities, Equip, Relics, Status, Formation, Config) or the Save screen open in the game, the second screen shows what it would walking about (FF6-4): the hidden copy closes them first (B, as many times as it takes for START to bring the menu up, at most five times).

**FF6-6. Last picture stays.** In a conversation, a scene the story plays, a shop, an inn, the fade between maps, and anywhere else the menu doesn't come up on the hidden copy, the last picture stays; before the first, the logo.

**FF6-7. Battles.** In a battle, from its first frame to the end of its results, the second screen shows the party kept from walking about (FF6-4): its HP, MP and play time are from before the battle and don't change during it. When no picture of this party has been kept (a state loaded straight into a battle, another file's party), the last picture stays: the logo if there's none yet. After a battle, the party screen is taken again at once. Told by the halfword at 0x02000002 being 0 (0xFFFF outside battles; the same in all three releases). The kept party is shown only for the party it was taken of, told by the 16 records' character numbers and names and each character's party byte (0x02001850 + the character's number: bits 0-2 the party it belongs to), which a battle doesn't change.

**FF6-8. Game screen untouched.** Nothing leaves the game screen. (The world map's own small map in the corner stays there: it's part of what the game draws, and the second screen doesn't repeat it.)

**FF6-9. Game over.** When the whole party falls in a battle (every character in it at 0 HP), the second screen shows the logo from that moment, and the picture kept is dropped, until the party screen is taken again after the game is loaded or restarted. Told in a battle (FF6-7) by the party's HP in the game's battle memory, four halfwords from 0x02003BF4 (the party's four places in order; the same in all three releases), with the places that hold a character told by their maximum HP, four halfwords from 0x02003C1C, being above 0: every such place at 0 HP. A party turned to stone, or out some other way that leaves HP above 0, isn't seen: the party kept stays until the game goes back to its title.

## Not covered yet

- **The world map's own map**: the game draws a small map in the corner of the game screen on the world map (SELECT hides it); nothing is done with it. A bigger one on the second screen would be a rebuilt picture (Philosophy 3 and 4).
- **Characters in other parties**: where the story splits the party (the Narshe defence, the floating continent, the final dungeon), the party screen shows whichever party the menu shows; the game-over rule only looks at the characters in the battle.
- **The Bestiary and the Esper, Abilities and Equip screens in a battle's place**: not shown; the second screen keeps the party.
- **The Europe release's other languages (French, German, Spanish, Italian)**: the cases play it in English.

## How the states were made

All from the owner's GameShark saves (`tools/sharkport.py`: `saves/BZ6E-gogo-wob.sav` from GameFAQs, notes: "Start game in WoB near Narshe right after Kefka battle w/Gogo/Umaro in party"; `saves/BZ6P-kefkas-tower.sav`: file 1 in Kefka's Tower, four characters at level 99; `saves/BZ6J-narshe-mines.sav`: file 1 with Terra and two Imperial soldiers in the Narshe mines), loaded in the app (the harness has no battery save: Load Game, file 1, Yes) and played on in the harness. The title's states are from power-on (a press of A through each screen). Battles: LEFT and RIGHT (UP and DOWN in the dungeons) until the encounter, `us-wob-battle.ss`, `eu-kefka-tower-battle.ss`, `jp-narshe-mines-battle.ss`.

## For the merge

Changelog line (under the open version):

- Final Fantasy VI Advance (USA, Europe, Japan): the second screen shows START's party screen (portraits, HP and MP, place, play time, steps and gil) as you walk about, keeps it through battles, and shows the logo on the title and after a game over. The world map's own corner map stays on the game screen.

Wiki section for `Supported-games.md` (table row and section):

> **Final Fantasy VI Advance** (`BZ6E` USA, `BZ6P` Europe, `BZ6J` Japan). Second screen: START's menu without its command list, as the party screen, kept through battles; the logo on the title and after a game over. Known: the command window's frame stays, empty; scenes and conversations keep the last picture; a party that's out in a way other than HP 0 (stone) isn't noticed.

# Final Fantasy V Advance

| | |
|---|---|
| Releases | `BZ5E` USA · `BZ5P` Europe (English, French, German, Spanish, Italian) · `BZ5J` Japan |
| Dumps | `BZ5E` cartridge `7a24ab0c` (tested) · `BZ5P` cartridge `7197117e` (tested) · `BZ5J` cartridge `fc7909a7` (tested), Virtual Console `bb4a2de0` (tested) |
| Profile | `src/profiles.c`: the `BZ5E` entry (`ffv_compose`) |
| Tests | `tests/regress/final-fantasy-v-advance.json` (`tools/regress.py final-fantasy-v-advance`) |
| Status | in progress (branch `game/final-fantasy-v-advance`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-v-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-11).

## Why these screens

Asked what Final Fantasy V Advance would have put on a second GBA screen: it has no map screen (the world map is only what the game draws while you walk on it), so the one thing a player keeps opening while walking about is START's menu, which shows the whole party at once (portrait, name, job, level, HP and MP), the place and the play time and gil. It's the same choice as Final Fantasy I & II: Dawn of Souls' party screen ([final-fantasy-1-2-dawn-of-souls.md](final-fantasy-1-2-dawn-of-souls.md): FF-5, FF-8, FF-12, FF-13), taken the same way: START's menu opened on the hidden copy and captured, without its list of commands (the commands are something to press, which the second screen leaves out: Philosophy, principle 2), and without the hand cursor. In a battle the second screen shows the party as it was just before the fight (the HP and play time from then): who's in the party, their jobs and their MP help more than the logo, as in Dawn of Souls (the owner's choice for that game, Nov 16–17, 2026; to be confirmed for this one on the device).

Dawn of Souls' START menu is one picture with its command window on a separate layer, so the commands can be left out by not drawing the layer. Here every window of the menu is drawn on one layer (BG0), so the commands are left out by leaving the part of the picture they sit in black: a mechanical change (a rectangle left empty, as Philosophy allows for a part of the game's screen not shown), nothing drawn or moved.

## Rules

**FV-1. Logo.** The logo is the cartridge's title: the FINAL FANTASY V lettering with the dragon behind it and the ADVANCE bar (BG2), without the PRESS ANY BUTTON line or the copyright line. The same in every release (the Japanese title has the same art).

**FV-2. Outside the game.** On the health warning, the Square Enix screens, the title, its New Game, Load Game and Extra screens, the second screen shows the logo, any picture of a game played before is dropped, and no captures are tried. Told by the word at 0x02000924 (the first of the characters' pointers, the game's set-up of a party) being 0 (the same address in all three releases; a soft reset clears it). It is set as soon as New Game starts, so the opening scenes count as in the game: no menu opens there, so the last picture (FV-5), or the logo, stays.

**FV-3. The party screen.** Walking about anywhere (the world map, towns, castles, dungeons, on a ship or the airship), the second screen shows the party screen: START's menu as the game draws it (the four characters with their portraits, names, jobs, levels, HP and MP, the place, the play time and the gil), without the command window (Job and Ability, Item to Save) and without the hand cursor. The commands' place, the top right, is left black; the place and the play time and gil keep theirs. It's taken again at every refresh, and at once on entering another map (the word at 0x0200EA7E, which changes with the map).

**FV-4. A menu open in the game.** With START's menu or one of its screens (Item, Magic, Equip, Status, Config, Job, Ability) open in the game, the second screen shows what it would walking about (FV-3): the hidden copy closes them first (B, as many times as it takes for START to bring the menu up, at most three).

**FV-5. Last picture stays.** In a conversation, a shop, an inn, a save point's menu, a scene the story plays, on the way between maps, and wherever else START brings up no menu, the last picture stays; before the first, the logo. START's menu is told by its windows' frames.

**FV-6. Battles.** In a battle, from its first frame to the end of its results, the second screen shows the party kept from walking about (FV-7): its HP, MP and play time are from before the battle and don't change during it. When no picture of this party has been kept (a state loaded straight into a battle, another file's party), the last picture stays: the logo if there's none yet. After a battle, the party screen is taken again at once. Told by the word at 0x02000EC0 being non-zero (it is from the battle's first frame, through its results; the same address in all three releases).

**FV-7. The party kept for battles.** While walking about, the hidden copy takes START's menu as FV-3 and keeps the latest one: that's the picture FV-6 shows. It's shown only for the party it was taken of, told by the characters' ids and jobs in the game's party records (from 0x0200DAB0 in the USA release, one every 0x64 bytes; a battle never shows a party from another file).

**FV-8. Game over.** When the whole party falls in a battle (every character's HP 0), the second screen shows the logo from that moment and the pictures kept are dropped, until the game is under way again (FV-2, FV-3). Told in a battle (FV-6) by the party's HP in the game's battle block (0x0201E1A0 in the USA release: four records of HP, maximum HP, MP and maximum MP, 8 bytes each). A party all turned to stone or out some other way that leaves HP above 0 isn't seen: the last picture stays.

**FV-9. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- The opening scenes of a new game and everything else before the first START menu show the logo (FV-5); the second screen first shows a party once START's menu can be opened.
- Only the party's HP at 0 counts as a game over (FV-8); a party turned to stone leaves the last picture.
- Not tried: the Japan Virtual Console dump's cases beyond its logo.

## How the states were made

All from the owner's GameShark saves (`saves/BZ5E-endgame.sav`, `BZ5P-endgame.sav`, `BZ5J-endgame.sav`, converted with `tools/sharkport.py`), loaded in the app (the harness has no battery save): file 1, on the world map, and the quicksave (Lethe Court, the Stairs of Destiny), then played on in the harness (a battle from the world map, the defeat by setting the battle HP to 0, a settled 250 frames).

## For the merge

- Changelog (under the open version, What's new): "Final Fantasy V Advance (USA, Europe, Japan): the second screen shows the party screen from START's menu (the party with their jobs, levels, HP and MP, the place, the play time and the gil, without the list of commands) while you walk about, and in battles the party as it was just before. Outside the game, and from a game over on, it shows the logo; in conversations, shops and scenes it keeps the last picture."
- `wiki/Supported-games.md`, table: Final Fantasy V Advance | USA, Europe, Japan | the party screen while walking about and in battles.
- `wiki/Supported-games.md`, section:

  ### Final Fantasy V Advance

  The game has no map screen, so the second screen shows the party screen from START's menu: the four characters with their jobs, levels, HP and MP, where you are, the play time and the gil, without the list of commands. It follows you (taken again every few seconds and on every new map). In a battle it shows the party as it was just before (the HP and play time don't change until the battle's over). On the title and the file screens, and after a game over, it shows the logo; in conversations, shops and scenes the last picture stays.
- README: the supported game count goes up by one; `docs/games/README.md`: its row in the table (prefix FV, `BZ5E` + 2).

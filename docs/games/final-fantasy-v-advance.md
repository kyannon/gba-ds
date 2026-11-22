# Final Fantasy V Advance

| | |
|---|---|
| Releases | `BZ5E` USA · `BZ5P` Europe (English, French, German, Spanish, Italian) · `BZ5J` Japan |
| Dumps | `BZ5E` cartridge `7a24ab0c` (tested) · `BZ5P` cartridge `7197117e` (tested) · `BZ5J` cartridge `fc7909a7` (tested), Virtual Console `bb4a2de0` (tested) |
| Profile | `src/profiles.c`: the `BZ5E` entry (`ffv_compose`) |
| Tests | `tests/regress/final-fantasy-v-advance.json` (`tools/regress.py final-fantasy-v-advance`) |
| Toggle | `views`, `HELD`: the party screen (FV-6, FV-10) and the world map (FV-3), where both exist; the party picture is already kept (FV-7) · GEN-14 |
| Status | supported (merged into dev Nov 18, 2026, for v1.5.6; tested on the device) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-v-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-11).

## Why these screens

Asked what Final Fantasy V Advance would have put on a second GBA screen: the game has a world map screen of its own (R, on the world map and the airship), shown first where it opens (the simplest approach: a screen the game already draws, opened on the hidden copy and captured). Elsewhere the thing a player keeps opening while walking about is START's menu, which shows the whole party at once (portrait, name, job, level, HP and MP), the place and the play time and gil. It's the same choice as Final Fantasy I & II: Dawn of Souls' party screen ([final-fantasy-1-2-dawn-of-souls.md](final-fantasy-1-2-dawn-of-souls.md): FF-5, FF-8, FF-12, FF-13), taken the same way: START's menu opened on the hidden copy and captured, without its list of commands (the commands are something to press, which the second screen leaves out: Philosophy, principle 2), and without the hand cursor. In a battle the second screen shows the party as it was just before the fight (the HP and play time from then): who's in the party, their jobs and their MP help more than the logo, as in Dawn of Souls (the owner's choice for that game, Nov 16–17, 2026; to be confirmed for this one on the device).

Dawn of Souls' START menu is one picture with its command window on a separate layer, so the commands can be left out by not drawing the layer. Here every window of the menu is drawn on one layer (BG0), so the commands' place can't be left to the layer below. The first version left it black; a black block looked unfinished, so by the owner's request (Nov 18, 2026) it was a box of its own made from the game's own window pieces, and by the owner's second request (Nov 18, 2026) no box at all: the party's big window going on across it (FV-10, an exception to principle 4 written down with the rule): nothing the game doesn't draw, nothing moved.

## Rules

**FV-1. Logo.** The logo is the cartridge's title: the FINAL FANTASY V lettering with the dragon behind it and the ADVANCE bar (BG2), without the PRESS ANY BUTTON line or the copyright line. The same in every release (the Japanese title has the same art).

**FV-2. Outside the game.** On the health warning, the Square Enix screens, the title, its New Game, Load Game and Extra screens, the second screen shows the logo, any picture of a game played before is dropped, and no captures are tried. Told by the word at 0x02000924 (the first of the characters' pointers, the game's set-up of a party) being 0 (the same address in all three releases; a soft reset clears it). It is set as soon as New Game starts, so the opening scenes count as in the game: no menu opens there, so the last picture (FV-5), or the logo, stays.

**FV-3. The world map.** Walking about where R opens the game's world map (on the world map, and on the airship), the second screen shows that map: the game's own picture of it, as the game draws it (the whole world on its parchment border, the party's mark and the towns' marks as the game has them), nothing taken out. Told by the hidden copy: R is pressed on it and the game's screen byte (0x020096E0 in the USA and Japanese releases, 0x02009464 in Europe's) says the map is open (0x0C; 5 walking about, 7 START's menu, 0x0A a battle); where R opens nothing (towns, castles, dungeons), the party screen is shown instead (FV-10). It's taken again at every refresh, and at once on entering another map (the game's room number, the word at 0x0200E2AE in the USA and Japanese releases, 0x0200E062 in Europe's; not the word at 0x0200EA7E, which changes with the tile the party stands on).

**FV-4. A menu open in the game.** With START's menu or one of its screens (Item, Magic, Equip, Status, Config, Job, Ability) or the world map open in the game, the second screen shows what it would walking about (FV-3, FV-10): the hidden copy closes them first (B, as many times as it takes, at most four) and waits for the game to listen again.

**FV-5. Last picture stays.** In a conversation, a shop, an inn, a save point's menu, a scene the story plays, on the way between maps, and wherever else START brings up no menu, the last picture stays; before the first, the logo. START's menu is told by its windows' frames.

**FV-6. Battles.** In a battle, from its first frame to the end of its results, the second screen shows the party kept from walking about (FV-7): its HP, MP and play time are from before the battle and don't change during it. When no picture of this party has been kept (a state loaded straight into a battle, another file's party), the last picture stays: the logo if there's none yet. After a battle, the party screen is taken again at once. Told by the word at 0x02000EC0 being non-zero (it is from the battle's first frame, through its results; the same address in all three releases).

**FV-7. The party kept for battles.** While walking about, the hidden copy takes START's menu as FV-10 (also where the map is what is shown, FV-3: for its first capture, for another party, and for every fourth capture after, as Final Fantasy I & II's does) and keeps the latest one: that's the picture FV-6 shows. It's shown only for the party it was taken of, told by the characters' ids and jobs in the game's party records (from 0x0200DAB0 in the USA release, one every 0x64 bytes; a battle never shows a party from another file).

**FV-8. Game over.** When the whole party falls in a battle (every character's HP 0), the second screen shows the logo from that moment and the pictures kept are dropped, until the game is under way again (FV-2, FV-3, FV-10). Told in a battle (FV-6) by the party's HP in the game's battle block (0x0201E1A0 in the USA release: four records of HP, maximum HP, MP and maximum MP, 8 bytes each). A party all turned to stone or out some other way that leaves HP above 0 isn't seen: the last picture stays.

**FV-10. The party screen.** Walking about where R opens no map (towns, castles, dungeons, a battle's party kept: FV-6), the second screen shows the party screen: START's menu as the game draws it (the four characters with their portraits, names, jobs, levels, HP and MP, the place, the play time and the gil), without the hand cursor and without the command list (Job and Ability, Item to Save). There is no box in the top right: the big window the party sits in goes on across the top right as the game draws it behind the commands, the commands' place being part of it. *An exception* (Philosophy principle 4: the game doesn't draw that part, as nothing hides it in the game): the part the commands covered is made from the game's own pieces, its top edge carried across from the left, its right edge straight down from the game's own corner, and the screen's own blue gradient inside (from the party window's left margin, row by row); the place and the play time and gil windows keep theirs. Nothing is drawn that the game doesn't draw. It's taken again at every refresh, and at once on entering another map (FV-3).

**FV-9. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- The opening scenes of a new game and everything else before the first START menu show the logo (FV-5); the second screen first shows a party once START's menu can be opened.
- Only the party's HP at 0 counts as a game over (FV-8); a party turned to stone leaves the last picture.
- Not tried: the Japan Virtual Console dump's cases beyond its logo.
- Europe's world map (FV-3) isn't tested: its save has only the Rift, so no state of it walking about the world map; its screen byte (0x02009464) is checked at 5, 7 and 0x0A only, 0x0C by analogy.
- The map's marks pulse; the picture is taken a fixed time after the map opens, so the marks' size isn't chosen.

## How the states were made

All from the owner's GameShark saves (`saves/BZ5E-endgame.sav`, `BZ5P-endgame.sav`, `BZ5J-endgame.sav`, converted with `tools/sharkport.py`), loaded in the app (the harness has no battery save): file 1, on the world map, and the quicksave (Lethe Court, the Stairs of Destiny), then played on in the harness (a battle from the world map, the defeat by setting the battle HP to 0, a settled 250 frames).

## Cross-checked against

samurai goroh's GameFAQs RAM list for the USA release (v2.3, 2019; the owner's copy, not kept in the repo), after the profile was written: the party records (base 0x0200DAB4 + 8: sprite/id, job, level, HP and MP at +0x10..+0x16, stride 0x64) and the battle's true character structs (HP at 0x0201EE04, stride 0x94) match it. Its screen byte 0x020096E0 (03 title, 12 Load Game, 05 field and the opening scenes, 07 START's menu, 0A battle) agrees with FV-2, FV-5 and FV-6 on the USA and Japan states. Its room words (0x0200E2AE / E2B0) number the maps differently from the 0x0200EA7E the profile watches, which works the same in all three releases. The list is for the USA release only: Europe's addresses (shifted) were found by experiment.

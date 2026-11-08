# Super Mario Advance 3: Yoshi's Island

| | |
|---|---|
| Releases | `A3AE` USA · `A3AP` Europe (a language menu first) · `A3AJ` Japan |
| Dumps | `A3AE` cartridge `40a48276` (tested), Virtual Console `3d803e41` · `A3AP` cartridge `639e9d3b` (tested), Virtual Console `2c79c2de` · `A3AJ` cartridge `26321120` (tested), Virtual Console `10f9eda4` |
| Profile | `src/profiles.c`: the `A3AE` entry (with `A3AJ`; `sm3_compose`, `sm3_virt`); variant `A3AP` (`sm3_eur`: the language menu before the logo) |
| Tests | `tests/regress/super-mario-advance-3.json` (`tools/regress.py super-mario-advance-3`) |
| Status | in testing (branch `game/super-mario-advance-3`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Super Mario Advance 2 ([SMA rules](super-mario-advance-2.md)) and 4 ([SM4 rules](super-mario-advance-4.md)): something the game opens with a button, captured on the hidden copy, in levels and on the map. Unlike those two, Yoshi's Island has no status bar along the edge of the screen in its levels, so nothing leaves the game screen.

## Rules

**SM3-1. Level: the status screen.** In a level the second screen shows the status screen SELECT opens there, captured on the hidden copy, whole, as the game draws it: the world's points, the level's high score, the stars, red coins and flowers found in the level, the points so far, and along the bottom the lives, coins, stars and the items. It's taken again every second (`map_interval`), so a red coin, a flower or a star found shows within a second. While the game is paused, the status screen is open on the game screen, or the level can't open it (a fade, a door, the goal), the last picture stays.

**SM3-2. Level: no item cursor.** The status screen's item cursor (the box round one item, cycling through colours in the game) is left out: the slot it's on looks like the others. Nothing on the second screen can be picked.

**SM3-3. World panel: the score page.** On a world's panel (the levels of one world, with the world tabs and SCORE above them and the map below), the second screen shows that world's score page, as the panel shows it after SCORE is picked: each level's score card and the bar with the world's total under them, with the map's scenery behind them, as the game draws them, but without anything that moves on the map (Yoshi, the levels' markers) or the hand, and without the GBA box with #1 at the panel's bottom right (it opens another menu, not a score): the panel's own colour there instead. (In the first test build the map was left out, black round the panel and the bar, and the #1 box was kept.) The game turns the cards over one at a time (about 5 seconds in all) and then makes the 100-point cards blink red and green: the second screen shows them still, in green. It's taken as the panel comes up (from the island map or back from a level, with the level's new score) and when another world's panel is picked; while the scores and the world stay the same, the picture does too. While the game itself shows the score page, that's what's taken. While the panel isn't ready for SCORE (sliding in, Yoshi walking to a level, a level starting), the last picture stays.

**SM3-4. The logo elsewhere.** Anywhere but a level or a world's panel (the title screens, the game select, Europe's language menu, the file menu, the opening story, the island map where a world is picked, the bonus games, and Mario Bros.), the second screen shows the logo (GEN-1), and the last status screen or score page is forgotten, so a world's panel never shows another world's scores.

**SM3-5. Logo.** The logo (GEN-1) is Yoshi's Island's own, YOSHI'S ISLAND with its ™ (in Japan ヨッシーアイランド), taken from its title screen (Yoshi's Island picked on the game select; in Europe, English picked on the language menu first), without the copyright line under it or the island behind it.

**SM3-6. The game screen as the game draws it.** Nothing is taken off the game screen: the star countdown, the eggs following Yoshi, message boxes and the pause box stay as they are.

**SM3-7. SNES layout.** The quick bar (swiped up from the bottom of the touchscreen) has Controls above its status row: GBA or SNES, as for Super Mario Advance 2 (SMA-5). SNES puts the face buttons where the SNES game had them, by position: the bottom one jumps (GBA A), the left one sticks out the tongue (GBA B), the right one aims and throws an egg (GBA R), and both shoulders lock the aim (GBA L). The top one, which the SNES game left to the bonus games, throws eggs too. START pauses and SELECT opens the status screen in both layouts (as on the GBA; the SNES game had its items under START). The SNES layout is used in levels only: everywhere else (the menus, the island map, a world's panel, the bonus games, Mario Bros.) it's the GBA layout, where A picks and B goes back.

## Known issues

- Every release is tried with a 100% file (a GameShark save of the USA release, `saves/A3AE-100-percent.sav`, made into Europe's and Japan's by changing the region letter in its blocks and their sums): in World 6's 6-7, paused, with the status screen open, on its panel, World 5's, the island map, the bonus game and Mario Bros. Not tried yet: a file part way through (scores under 100, levels not open yet), boss fights, Yoshi's transformations, Baby Mario floating off (the star countdown), the ending, the Virtual Console dumps.
- On a world's panel the score page is taken on the hidden copy as the game shows it (about 5 seconds of turning cards, some 360 of the hidden copy's frames): after another world is picked, or back from a level with a new score, the second screen keeps the last picture until it's ready.

## For the merge

Changelog (under the open version, "New games"):

- **Super Mario Advance 3: Yoshi's Island** (USA, Europe, Japan): in a level, the second screen shows the game's status screen (stars, red coins and flowers found, the level's high score, lives, coins and items), kept up to date about once a second; on a world's panel, that world's score page (every level's score and the world's total). The logo elsewhere. Controls in the quick bar: GBA or SNES (the SNES game's buttons, in levels).

`docs/games/README.md`'s table, prefix SM3, releases A3AE + 2.

Wiki, `Supported-games.md`, table row:

    | Super Mario Advance 3: Yoshi's Island | `A3AE` `A3AP` `A3AJ` | the status screen in levels; a world's score page on its panel |

and section:

## Super Mario Advance 3: Yoshi's Island

The USA, European and Japanese releases.

- **In a level,** the second screen shows the game's own **status screen** (SELECT): the world's points, the level's high score, the stars, red coins and flowers you've found in it, your points so far, and your lives, coins, stars and items. It's kept up to date about once a second. The item cursor is left out.
- **On a world's panel** (where you pick a level), it shows that world's **score page**, as SCORE shows it: every level's score and the world's total, over the map's scenery (without Yoshi, the levels' markers or the #1 box). It's taken again when you come back from a level or pick another world.
- On the title, the file menu, the opening, the island map, the bonus games and in Mario Bros., the second screen shows the logo.
- Nothing is taken off the game screen.
- **Controls** in the quick bar: GBA or SNES. SNES puts the buttons where the SNES game had them, in levels: bottom jumps, left sticks out the tongue, right (and top) throws an egg, the shoulders lock the aim.

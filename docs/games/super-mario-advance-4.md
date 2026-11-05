# Super Mario Advance 4: Super Mario Bros. 3

| | |
|---|---|
| Releases | `AX4E` USA · `AX4P` Europe · `AX4J` Japan (where Mario is, 4 bytes lower in Europe and Japan) |
| Dumps | `AX4E` cartridge `a44ba79f` (tested), Rev 1 `88dab27f`, Rev 1, Switch Online `22e12d0e`, Rev 1, Virtual Console `d4c13ac3` · `AX4P` cartridge `37141f32` (tested), Rev 1 `8d84acfc`, Rev 1, Switch Online `e3847e32`, Rev 1, Virtual Console `d4f45b01` · `AX4J` cartridge `f3c87306` (tested), Rev 1 `5a676424`, Rev 2 `7b1a4c22`, Rev 2, Switch Online `aa312980`, Rev 2, Virtual Console `4aee9c77` |
| Profile | `src/profiles.c`: the `AX4E` entry; variants `AX4P`, `AX4J` (`sma4_eur`, `sma4_jpn`) |
| Tests | `tests/regress/super-mario-advance-4.json` (`tools/regress.py super-mario-advance-4`) |
| Status | testing (branch `game/super-mario-advance-4`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance-4`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Super Mario Advance 2 ([SMA rules](super-mario-advance-2.md)): something the game opens with a button on the world map, captured on the hidden copy; the status bar moved off the game screen in levels.

## Rules

**SM4-1. World map: the item inventory.** On the world map the second screen shows the item inventory (the blue panel L opens), captured on the hidden copy: the panel alone, where the game draws it (its lower 100 rows), on dark grey, with the items as the game shows them (outlines, the first one in colour, as the game's cursor starts there). Nothing of the map, Mario or the L and R signs comes with it. The game has two sets of colours (a colour patch players use picks the other one, with its darker blues): the inventory is found and shown the same in either. It's taken again as soon as Mario stops on another spot (GEN-6), and every second while he stands still, so an item won in a Toad House or used from the inventory shows within a second of being back on the map. While the inventory, the START menu or the e-Reader menu (R) is open on the game screen, or Mario is walking, the last picture stays.

**SM4-2. Level: status bar.** In a level the status bar along the top (world, lives, time, coins, score) and the P meter at the bottom left leave the game screen and go to the second screen: the status bar where the game draws it, at the top, the P meter centred along the bottom, with the logo (GEN-1) at full strength centred between. (In the first test build the P meter stayed at the left, where the game draws it.) They stay there through pipes, doors and bonus rooms, and fade with the game as it fades out and in.

**SM4-3. What stays in a level.** The goal's LEVEL CLEARED! and the card won (on the status bar's layer, between the status bar and the P meter) stay on the game screen, and so do the three card slots shown at the start and end of a level (sprites).

**SM4-4. Houses and castles keep the inventory.** In a Toad House, a Spade panel game or a king's room the game screen is as the game draws it (with the world map's status bar along its bottom), and the second screen keeps the last inventory picture (SM4-1); with none yet (a state loaded there), the logo at half strength.

**SM4-5. Logo.** The logo (GEN-1) is Super Mario Bros. 3's own, SUPER MARIO BROS. 3 alone, taken from its title screen (Super Mario Bros. 3 picked on the Super Mario Advance 4 title, START through the curtain), without the copyright line. On the title, game select and file screens the second screen shows it at half strength.

**SM4-6. World map: status bar on top.** On the world map the status bar along the bottom of the game screen (world, lives, coins, score and the three cards) leaves the game screen, which then shows the map under it, and goes to the top of the second screen, above the inventory (SM4-1), as it is on the game screen, updated as it changes. With no inventory picture yet, the logo below it. While the inventory, the START menu or the e-Reader menu is open on the game screen, the status bar there is the game's own and the second screen keeps its last picture. In Toad Houses and other rooms the status bar is the game's own (SM4-4).

## Known issues

- Only the first cartridge dumps are tested. The later revisions (Rev 1 in the USA and Europe, Rev 1 and 2 in Japan, and the Virtual Console and Switch Online dumps, all made from them) may keep their RAM elsewhere; if so, the second screen stays on the logo there.
- During a fade (through a pipe or door, at the goal) the status bar fades with the game, so it shows as dark shapes for a moment.
- Checked at the start of every level of worlds 1 to 6 and 8 that the warp zone reaches (underwater, desert, giant, sky, ice, cave and fortress levels): the status bar is always the same layer. Not tried yet: airships, fortresses' boss rooms, Kuribo's shoe, World 8's tanks and Bowser, World-e (e-Reader levels), Luigi's turns in Mario & Luigi mode, the Wii U Virtual Console dump.

## For the merge

- Changelog (What's new): "Super Mario Advance 4: Super Mario Bros. 3 is supported: on the world map the second screen shows the status bar and your item inventory, and in a level the status bar and P meter move to the second screen."
- Wiki (`Supported-games.md`), section:

  ### Super Mario Advance 4: Super Mario Bros. 3

  USA, Europe and Japan.

  - **World map:** the status bar moves to the top of the second screen, above your item inventory (what L opens), kept up to date as you move, win items and use them.
  - **Levels:** the status bar (world, lives, time, coins, score) and the P meter move to the second screen, with the logo between them, so the game screen shows the whole level.
  - **Toad Houses and other rooms:** the second screen keeps your inventory.

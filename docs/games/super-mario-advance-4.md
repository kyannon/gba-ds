# Super Mario Advance 4: Super Mario Bros. 3

| | |
|---|---|
| Releases | `AX4E` USA · `AX4P` Europe · `AX4J` Japan (where Mario is, 4 bytes lower in Europe, Japan and the USA's Rev 1: variant AX4R, picked by the ROM's version byte) |
| Dumps | `AX4E` cartridge `a44ba79f` (tested), Rev 1 `88dab27f`, Rev 1, Switch Online `22e12d0e`, Rev 1, Virtual Console `d4c13ac3` · `AX4P` cartridge `37141f32` (tested), Rev 1 `8d84acfc`, Rev 1, Switch Online `e3847e32`, Rev 1, Virtual Console `d4f45b01` · `AX4J` cartridge `f3c87306` (tested), Rev 1 `5a676424`, Rev 2 `7b1a4c22`, Rev 2, Switch Online `aa312980`, Rev 2, Virtual Console `4aee9c77` |
| Profile | `src/profiles.c`: the `AX4E` entry (its `pick`, `sma4_pick`, switches a later USA revision to AX4R); variants `AX4P` (`sma4_eur`), `AX4J` and AX4R (`sma4_moved`) |
| Tests | `tests/regress/super-mario-advance-4.json` (`tools/regress.py super-mario-advance-4`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance-4`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Super Mario Advance 2 ([SMA rules](super-mario-advance-2.md)): something the game opens with a button on the world map, captured on the hidden copy; the status bar moved off the game screen in levels.

## Rules

**SM4-1. World map: the item inventory.** On the world map the second screen shows the item inventory (the blue panel L opens), captured on the hidden copy: the panel alone, where the game draws it (its lower 100 rows), on dark grey, with the items as the game shows them (outlines, the first one in colour, as the game's cursor starts there). Nothing of the map, Mario or the L and R signs comes with it. The game has two sets of colours (a colour patch players use picks the other one, with its darker blues): the inventory is found and shown the same in either. It's taken again as soon as Mario stops on another spot (GEN-6), and every second while he stands still, so an item won in a Toad House or used from the inventory shows within a second of being back on the map. While the inventory, the START menu or the e-Reader menu (R) is open on the game screen, or Mario is walking, the last picture stays.

**SM4-2. Level: status bar.** In a level (airships, boss rooms, World 8's tanks and Bowser's castle too) the status bar along the top (world, lives, time, coins, score, and in World-e's levels the coin count under it) and the P meter at the bottom left leave the game screen and go to the second screen: the status bar where the game draws it, at the top, the P meter centred along the bottom, with the logo (GEN-1) at full strength centred between. (In the first test build the P meter stayed at the left, where the game draws it.) They stay there through pipes, doors and bonus rooms, and fade with the game as it fades out and in. (Until v1.4.10's later test builds an airship was taken for a Toad House: its status bar stayed on the game screen and the P meter went to the top of the second screen.)

**SM4-3. What stays in a level.** The goal's LEVEL CLEARED! and the card won (on the status bar's layer, between the status bar and the P meter) stay on the game screen, and so do the three card slots shown at the start and end of a level (sprites).

**SM4-4. Houses and castles: like the world map.** In a Toad House, a Spade panel game or a king's room the status bar along the bottom of the game screen (the world map's, with the cards) goes to the top of the second screen, as on the world map (SM4-6), above the last inventory picture (SM4-1); with none yet (a state loaded there), the logo at full strength below it. The rest of the game screen is as the game draws it. (Until the third test build the status bar stayed on the game screen there and the second screen had the inventory alone.)

**SM4-5. Logo.** The logo (GEN-1) is Super Mario Bros. 3's own, SUPER MARIO BROS. 3 alone, taken from its title screen (Super Mario Bros. 3 picked on the Super Mario Advance 4 title, START through the curtain), without the copyright line, and without the thin rim of its orange shadow along the letters' upper and left sides (it shows against the title's yellow, but on the second screen's grey it looks like stray specks; the shadow below and right of the letters stays). (Until the fourth test build that rim was kept.) On the title, game select and file screens the second screen shows it at half strength.

**SM4-6. World map: status bar on top.** On the world map the status bar along the bottom of the game screen (world, lives, coins, score and the three cards) leaves the game screen, which then shows the map under it, and goes to the top of the second screen, above the inventory (SM4-1), as it is on the game screen, updated as it changes. With no inventory picture yet, the logo below it. While the inventory, the START menu or the e-Reader menu is open on the game screen, the status bar there is the game's own and the second screen keeps its last picture. The same goes in Toad Houses, Spade games and kings' rooms (SM4-4).

**SM4-7. Every revision.** The later revisions behave as the first cartridges do: the USA's Rev 1 (and the Virtual Console and Switch Online dumps made from it), whose RAM is laid out as Europe's and Japan's, Europe's Rev 1 and Japan's Rev 1 and 2. The logo comes out the same from each (the start is left to play by itself up to the opening, as each revision takes a different time to get there). *By hand: the tests run only the first cartridge dumps (the fixtures); checked in the harness and headless app on Nov 5, 2026 with the USA's Rev 1, Europe's Rev 1 Wii U Virtual Console dump and Japan's Rev 2 (world map, a level, the logo), not yet on the device.*

**SM4-8. World-e's map.** On World-e's map (the e-Reader levels) the second screen is as on the other worlds' maps (SM4-1, SM4-6): its two-row status bar at the top of the second screen, above the inventory, and off the game screen. With Mario on a level's spot, where the game shows that level's box and takes no L, the inventory is taken the way a player gets to it there: B (the Level Card list), B again (back to the map), then L. While the inventory is open on the game screen, the second screen shows the inventory alone (World-e's status bar and panel are one layer, so the status bar can't be held there as on the other maps). World-e's levels are like any other (SM4-2). *By hand: only the Wii U Virtual Console dump has World-e's levels without the cards, and the tests run the first cartridge dumps only; checked on Nov 5, 2026 in the headless app with Europe's Rev 1 Virtual Console dump, not yet on the device.*

**SM4-9. The opening.** The opening after Super Mario Bros. 3 is picked (the clouds inside the checkered frame, before the curtain and the title) is drawn whole on the game screen, its frame too: none of it is taken for the world map's status bar. The second screen shows the logo at half strength, as on the title. (The game says Mario is on the world map there; it's told apart by BG2, whose tiles start at character block 2 in the opening and at block 0 on the world map, World-e's map, in Toad Houses, Spade games and kings' rooms. Found from a player's bug report, v1.4.10-dev.)

**SM4-10. The language menu (Europe).** The European release's Choose a Language menu, before the title, is drawn whole on the game screen, its checkered frame too: none of it is taken for the world map's status bar. The second screen shows the logo. (It's told apart by BG1, which the menu leaves off, while the world map, World-e's map, Toad Houses, Spade games and kings' rooms all show it. Until v1.4.11 the frame's top strip went to the top of the second screen and was missing from the game screen: a player's bug report, Europe's Rev 1 on the Wii U Virtual Console.)

## Known issues

- Not tried: Europe's Rev 1 cartridge and Switch Online dumps, the USA's Virtual Console and Switch Online dumps, Japan's Rev 1 and its Virtual Console and Switch Online dumps. Each is expected to behave as the revision it's made from (SM4-7).
- During a fade (through a pipe or door, at the goal) the status bar fades with the game, so it shows as dark shapes for a moment.
- Checked at the start of every level of worlds 1 to 6 and 8 that the warp zone reaches (underwater, desert, giant, sky, ice, cave and fortress levels), in World 1's airship and Larry's room, World 5-3 with Kuribo's Shoe, World 8's tank and Bowser's castle: the status bar is always the same layer. Not tried yet: Luigi's turns in Mario & Luigi mode.
- World-e (SM4-8) is tried only with Europe's Rev 1 Virtual Console dump; the USA's and Japan's World-e (with the cards scanned) aren't.

## For the merge

Changelog (under the open version, "Fixes"):

- **Super Mario Advance 4** (Europe): on the Choose a Language menu, the top of its checkered frame was taken for the world map's status bar: it showed on the second screen and was missing from the game screen. The menu is now drawn whole, with the logo on the second screen.

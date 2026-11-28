# Super Mario Advance 2: Super Mario World

| | |
|---|---|
| Releases | `AA2E` USA · `AA2P` Europe · `AA2J` Japan (game mode 0x10 lower) |
| Dumps | `AA2E` cartridge `5206880a` (tested) · `AA2P` cartridge `fcfef343` (tested) · `AA2J` cartridge `901ce373` (tested) |
| Profile | `src/profiles.c`: the `AA2E` entry (with `AA2P`); variant `AA2J` |
| Tests | `tests/regress/super-mario-advance-2.json` (`tools/regress.py super-mario-advance-2`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SMA-1. World map: level list.** On the world map the second screen shows the SELECT menu's list of the island's levels with time and score, captured on the hidden copy, held still (HELP! and the current level shown, not blinking), without the ◁ ▷ arrows at the screen's left and right edges that show once there are other worlds' lists to turn to (left out since v1.4.2's later test builds). It's taken again as soon as Mario stops on another level (GEN-6; in Japan too since v1.4.2).

**SMA-2. Level: status bar.** In a level the status bar (lives, Dragon Coins, time, coins, score) moves to the top of the second screen, with the logo (GEN-1) centred on the screen below it (until v1.3.2's later test builds: half its size, on black); the blue item box stays at the top of the game screen with the reserve item.

**SMA-3. Boxes stay.** Message boxes and the pause box stay on the game screen, and so does anything else a level puts on the status bar's layer below the status bar (some levels' scenery, a cave's rocks), drawn by the game as it draws it, in front or behind. (Until v1.4.1 that layer was laid back over the game's picture: a cave's scenery came out in front of everything.)

**SMA-4. Through doors and castles.** The status bar stays on the second screen through doors, pipes, castle rooms and boss fights.

**SMA-5. SNES layout.** The quick bar (swiped up from the bottom of the touchscreen) has Controls above its status row, centred: GBA or SNES. The second screen itself shows no picker. (Until v1.4.1: a Controls row at the bottom of the second screen.)

**SMA-6. Logo.** The logo (GEN-1) is Super Mario World's own, SUPER MARIO WORLD alone, taken from its title screen (Start for single player, A on Super Mario World) without the wooden frame round it or the copyright line. (Until v1.3.2's later test builds it was the first title's, with SUPER MARIO ADVANCE 2 under it.)

**SMA-7. Level name.** In a level, the top of the second screen keeps the level list's title row (its name, as the world map's list showed it when Mario last stood still: the level he went into), held there all through the level, through doors and castles too (SMA-4); the status bar (SMA-2) is 2 rows below it, and the logo centred in the room left under the status bar. With no list taken yet (a state loaded in a level), the status bar is at the top.

**SMA-8. Widescreen 16:9 (USA).** With GBA DS's widescreen patch "Super Mario Advance 2 (16-9)" on (docs/WIDESCREEN.md, WS-1; the USA cartridge dump only), the game screen is 284×160: the level goes on into the margins on both sides as the game would draw it on a wider screen, scenery and sprites alike. An enemy coming from the right appears at the margin's outer edge, and one leaving to the left stays drawn until it's past it. The middle 240×160 is the game's own picture, frame for frame as without the patch, and the second screen is as in SMA-1 to SMA-7 (the status bar stays there). This is a GEN-2 exception the player chooses. The game already does most of it, since it's the SNES game's engine, built for a 256-wide screen: every level layer is a 512-pixel map streamed well past the GBA's edge, and sprites come in and are drawn out to x 271. The patch (`widescreen/AA2E/`, built by `tools/widepatches/AA2E/build.py`) only changes:
- the left edge of the game's sprite drawing checks (about 75 checks, 161 halfwords, found in the code by their shape: x + 0x20 against 0x12F and the like): a sprite or tile is drawn until 32 pixels further left (-48 or -64 instead of -16 or -32); the right edge is unchanged;
- two of the distances at which sprites are removed once off screen: 256 to 288 on the right, -16 to -48 on the left (the others are already past the margins), so a sprite isn't removed inside a margin. Those sprites stay in play a little longer off screen, as in the SNES widescreen hack (wide-snes) the idea comes from.

Known limitations (for the changelog when it merges): on the world map, the frame round the map is the game's own 256-pixel picture, so the left margin shows the map beyond the frame and the right one a little frame and then the map; at a level's left or right end the margins show what's past the end (the sky, no ground); a few sprite routines with their own checks (about a dozen) still stop drawing a sprite a few pixels into the left margin; the title screen, menus, bonus games and the ending haven't been checked with it, and vertical levels, ghost houses and boss fights haven't been tried. Tried in Yoshi's Island 1, a cave, a castle's rooms and doors and the world map. *Case:* `sma-us-wide-16-9`.

## Exceptions

- SMA-7's level name: the title row of the world map's level list, held above the status bar all through the level, is a piece of one of the game's screens moved onto another (as Super Mario Advance's course board is, SMB2-8), so the second screen says which level you're in.
- SMA-8's widescreen patch: the game screen wider than the GBA's (docs/WIDESCREEN.md), when the player switches the patch on.


## Not covered yet

- Europe has a level only, Japan the world map and a level; their castles, doors and message boxes haven't been run.
- Ghost houses, Yoshi's houses, the Star Road and Special Zone, boss fights (Koopalings and Bowser's final fight) and the ending haven't been tried; castle rooms and doors are checked in the USA.

## For the merge

- Changelog (under the open version, "Widescreen patches"): "Super Mario Advance 2 (USA) has a 16:9 widescreen patch: levels go on into the wider screen's sides, enemies included, and enemies come and go at its edges. On the world map the sides show the map past its frame, and past a level's ends you see the sky."
- docs/WIDESCREEN.md, the Games row: add "Super Mario Advance 2 (USA, 16:9; SMA-8, its source in `tools/widepatches/AA2E/`)".
- `widescreen/index.txt`: run `tools/widescreen.py index` again after merging `dev` (Aria of Sorrow's and FireRed's patches touch it too).

# GT Advance 3: Pro Concept Racing

| | |
|---|---|
| Releases | `A2GE` USA · `A2GP` Europe · `A2GJ` Japan (Advance GT2) |
| Dumps | `A2GE` cartridge `0a331e11` (tested) · `A2GP` cartridge `03898e7f` (tested) · `A2GJ` cartridge `edcb7d46` (tested) |
| Profile | `src/profiles.c`: the `A2GE` entry (with `A2GP`; `A2GJ` a variant) |
| Tests | `tests/regress/gt-advance-3-pro-concept-racing.json` (`tools/regress.py gt-advance-3-pro-concept-racing`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list gt-advance-3-pro-concept-racing`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The second screen follows the other racing games' ([V-Rally 3](v-rally-3.md), [Mario Kart: Super Circuit](mario-kart-super-circuit.md), [F-Zero: Maximum Velocity](f-zero-maximum-velocity.md)): the race's HUD moves there, the course map twice as big, and what you watch while you drive (the rev counter with your speed and gear, your place) stays on the game screen. If the game had been made for a two-screen GBA, the times and the map are what would have gone up there: the FAQ's advice for the harder courses is to "keep an eye on your map while racing".

## Rules

**GT3-1. Logo.** The logo is the title screen's lettering, taken from power-on (past the disclaimer): GT3 ADVANCE in silver and black with PRO CONCEPT RACING under it (USA, Europe), Advance GT2 with its bar and katakana (Japan). The title paints its lettering over a picture of a highway: the GT3 is cut out of the white sky; behind ADVANCE and PRO CONCEPT RACING the highway's blue is kept, in the lettering's own shape, since black lettering on the dark grey of an idle second screen couldn't be read. Only the logo's outline is stored in GBA DS (`src/logo_masks.c`); its pixels come from the game.

**GT3-2. HUD moved, not copied.** While a race is on (Championship races and licence tests, Quick Race, Time Attack, Drift-Combo, Practice and the Extra modes: from the moment the race's picture comes up, before the countdown, to the moment the game takes its HUD down after the finish), each frame is drawn twice: the game screen without the HUD pieces that leave it, and the second screen with only those, so nothing behind them is lost. The HUD is the game's sprites, told apart by their palettes, tiles and places, and the bottom bar (LAP-TIME, TIME LIMIT or MAX-COMBO, the rev counter's dial and the speed box), told by its layer's priority on those lines. What the game puts over the track stays on the game screen: the course name and the countdown (3, 2, 1, GO!), the corner signs, WRONG WAY, GHOST OVER, a finished lap's time, GOAL! and PASSED, Drift-Combo's drift mark, the rain, and the pause menu.

**GT3-3. Racing: game screen.** The game screen keeps the rev counter with its needle, the speed and the gear where the game draws them (bottom right), and your place (POSITION, in the modes that have it) moved into the top-left corner, where TOTAL TIME was.

**GT3-4. Racing: second screen.** On the second screen (dark, as the other racing games'), where the game draws them: TOTAL TIME (top left) and LAP with the laps of the race (top right); LAP-TIME, TIME LIMIT (licence tests) or MAX-COMBO (Drift-Combo) bottom left; and the course map with every car's dot (Practice: its own PRACTICE map), twice as big, in the middle. The map comes up when the game draws it (as the race starts, after GO!) and goes when the game takes it away (at the finish).

**GT3-5. Pause.** The pause menu (CANCEL, RESTART, RETIRE) stays on the game screen; the second screen keeps the HUD as it was before the game hid it. The game screen shows the pause menu over the track alone (the game hides the needle and figures; the rev counter's dial and the bottom bar go with the HUD).

**GT3-6. Finish.** At the finish, GOAL! (PASSED after a licence test) stays on the game screen with the rev counter while the camera turns round the car; the second screen keeps the times (the game takes the map away) until the game fades out.

**GT3-7. Logo elsewhere.** Wherever the race's HUD is gone, the second screen shows the logo: the intro, the title, every menu (the modes, the classes, the races, the garage, the courses, options), the black loading screens, the replay after a race (REPLAY, no HUD), the results and RETRY menu after it.

**GT3-8. One screen.** With `layout = single` the HUD stays where the game draws it.

## How it's told apart

All three releases have the same race screen, the same RAM where it's used, and the same sprites; Japan's words are spelled differently (TOTALTIME, LAPTIME, Km/h) in the same places.

A race is display mode 1 with its picture split line by line by the game (an hblank interrupt): the sky on BG0 (lines 0-51), the scenery on BG1 at priority 1 (BG1CNT 0x5E49) and the road on BG2 (affine, priority 2); from line 80 BG1 is the bottom bar at priority 0 (BG1CNT 0x5E48), which is what the registers hold as a frame ends. The menus and the results are mode 0. The replay after a race uses the race's registers but draws no HUD (nothing for a third of a second: the logo, GT3-7).

The HUD's sprites are priority 0 in palettes 11-14; the figures (palette 14, tiles 998-1023) are shared by every number on the screen, so each piece is told by place:

- the top row (y 0-4): TOTAL TIME (x 0-113), POSITION and its figure (x 120-183, the figure a 16x16 in palette 13), LAP and its figures (x 184-239);
- the course map, one 64x64 at (0, 16), tiles 736-799, palette 11; its dots 8x8, tile 806 (yours, red) and 807 (the rivals, blue), palette 12;
- LAP-TIME's or TIME LIMIT's figures at (56-113, 143), palette 14; MAX-COMBO's count a 16x16 at (88, 142), tile 448, palette 13;
- the rev counter's needle (tiles 808, 812, palette 13, around (209-227, 115-136)), the speed (161, 168) and the gear (202) at y 143, palette 14.

Over the track (stay): the countdown, GOAL!, GHOST OVER, WRONG WAY, a lap's time (at y about 90), the course name, the cars (priority 1, palettes 0-5).

Other addresses: the pause flag, bit 0x20 of 0x0203EE58 (0x31 paused, 0x11 racing and in the menus, every release). The byte at 0x03003A21 (0x3D, 0x3E racing, 0x3C paused) is the low half of a pointer, not a state number: not used.

## Accepted (not listed as a known issue)

- While paused, the rev counter's dial and the empty bottom bar aren't on the game screen (GT3-5): the game shows them empty there, with the needle and figures hidden; the pause menu is clearer without.
- As the game fades in before the countdown and out after the finish, the second screen's HUD fades with it (the game's own brightness).

## Not covered yet

- Link play needs a link cable: not tried.
- Only the first course of most modes was tried (Hornet in Quick Race, Time Attack and Kart; Slip Rough in Drift-Combo; Flash Speed at night in the rain; Licence Basic 1); the other courses use the same HUD. The Wireframe and Radio Con extra modes weren't tried.
- A licence test failed (the time running out) wasn't seen.

## Exceptions

- The course map drawn twice as big (as the other racing games' are): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## For the merge

Changelog line (under the open version's What's new):

- GT Advance 3: Pro Concept Racing (USA, Europe, and Japan's Advance GT2) is supported: in a race, the HUD moves to the second screen (the total time, the lap, the lap time, time limit or combo, and the course map twice as big); the rev counter with your speed and gear, and your place, stay on the game screen. The logo elsewhere. Not tried yet: link play.

Wiki, `wiki/Supported-games.md` (table row and section):

| GT Advance 3: Pro Concept Racing | `A2GE` `A2GP` `A2GJ` | the race's HUD, with the course map twice as big |

### GT Advance 3: Pro Concept Racing

The USA and European releases, and Japan's Advance GT2.

The HUD is moved off the game screen rather than copied, so nothing behind it is lost.

- **Racing** (Championship races and licence tests, Quick Race, Time Attack, Drift-Combo, Practice and the Extra modes):
  - The game screen keeps the **rev counter** with your speed and gear (bottom right) and your **place** (POSITION) in the top-left corner.
  - The second screen has **TOTAL TIME** and **LAP** along the top, **LAP-TIME** (TIME LIMIT in a licence test, MAX-COMBO in Drift-Combo) at the bottom left, and the **course map** with every car's dot, twice as big, in the middle.
  - The countdown, the corner signs, WRONG WAY, a finished lap's time and GOAL! are drawn over the track and stay on the game screen.
- The game's **pause menu** stays on the game screen, with the HUD on the second screen.
- Everywhere else (the menus, loading, the replay and results after a race) the second screen shows the logo, the title's lettering.
- With one screen (`layout = single`), the HUD stays where the game draws it.

Known issues: link play hasn't been tried.

Wiki, `wiki/Philosophy.md`, its list of exceptions: add GT Advance 3 to the race screens' course maps drawn larger ("Mario Kart's, the F-Zero games', Crash Nitro Kart's, V-Rally 3's and GT Advance 3's race screen course maps, drawn larger than the game's").

# V-Rally 3

| | |
|---|---|
| Releases | `AVRE` USA · `AVRP` Europe · `AVRJ` Japan |
| Dumps | `AVRE` cartridge `d3ae3d37` (tested) · `AVRP` cartridge `02d20287` (tested) · `AVRJ` cartridge `a0aa46b9` (tested) |
| Profile | `src/profiles.c`: the `AVRE` entry (with `AVRP`; `AVRJ` a variant) |
| Tests | `tests/regress/v-rally-3.json` (`tools/regress.py v-rally-3`) |
| Status | testing (on its own branch, `game/v-rally-3`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list v-rally-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The second screen follows the other racing games' ([F-Zero: Maximum Velocity](f-zero-maximum-velocity.md), [Crash Nitro Kart](crash-nitro-kart.md), [Mario Kart: Super Circuit](mario-kart-super-circuit.md)): the stage's HUD moves there, the course map twice as big, and what you watch while you drive (the rev counter with your gear and speed, your place, the pace notes) stays on the game screen.

## Rules

**VR3-1. Logo.** The logo is the game's title lettering, V-RALLY 3 with its heartbeat line, taken from the top of the main menu (the game has no title screen of its own: the intro goes straight to the language and save-slot screens), without the purple stripes behind it; taken from power-on, through the language and save-slot screens on the hidden copy. Only the logo's outline is stored in GBA DS (`src/logo_masks.c`); its pixels come from the game.

**VR3-2. HUD moved, not copied.** While a stage or race is on (Time Trial, V-Rally Mode, V-Rally Cross: from the moment the game puts its HUD up, before the countdown, to the moment it takes it down after the finish), each frame is drawn twice: the game screen without the HUD pieces that leave it, and the second screen with only those, so nothing behind them is lost. The HUD is the game's sprites, told apart from what goes over the track by their tiles, palettes and places. Everything the game draws into its picture stays on the game screen with the track and the car: the countdown (3, 2, 1, GO), NEW RECORD, the hazard boards over the road, the pause box and the in-car view's dashboard.

**VR3-3. Driving: game screen.** The game screen keeps, where the game draws them: the rev counter with the gear and speed (bottom left), the pace-note arrows (top centre), the car's exhaust flashes and the dust over the track; and your place (POS, in V-Rally Cross and V-Rally Mode), moved up into the top-left corner, where BEST TIME was.

**VR3-4. Driving: second screen.** On the second screen (dark, as F-Zero: Maximum Velocity's, the HUD's pieces on it, where the game draws them unless said): BEST TIME (in its place, as the game has it, the CHECK POINT split against the best time, green) in the top-left corner; LAP (V-Rally Cross) under it, moved up into POS's place; the stage clock in the top-right corner; V-Rally Mode's damage lights (the car's body, wheels, brakes, steering and engine) down the right edge; and the course map with its round backing and every car's dot, twice as big, at the bottom centre.

**VR3-5. Pause.** The pause menu (RESUME GAME, CHANGE CARS, CHANGE STAGE, GHOST, QUIT) stays on the game screen; the HUD stays on the second screen.

**VR3-6. In-car view.** In the in-car view (SELECT), the HUD moves as in VR3-4 and VR3-3; the dashboard (with its own speed readout) and the driver's hands stay on the game screen. (The game has no rev counter in that view.)

**VR3-7. Logo elsewhere.** Wherever the stage's HUD is gone, the second screen shows the logo: the intro, the language and save-slot screens, the main menu and every menu after it (the cars, the rallies and licences, the stage choice, the service station), the black loading screens, a stage's first moments before the game puts its HUD up, and "The game is being saved" after a stage.

**VR3-8. One screen.** With `layout = single` the HUD stays where the game draws it.

## How it's told apart

All three releases draw the stage as a bitmap (display mode 4, DISPCNT 0x1444, 0x1454 with the frame flipped) with the HUD, the sky and the scenery as sprites; the menus use the same display mode (their pictures are bitmaps too). The stage is on while the game's V-count interrupt is (DISPSTAT bit 5: 0x0029 through the countdown, the stage, the pause and the finish; 0x0009 in the menus, 0x0001 on the save screen; the loading screens are mode 4 with only BG2 off, DISPCNT 0x0004). Before the HUD is up the game has the stage's registers set for a few frames: nothing drawn by the HUD then shows the logo (VR3-7).

The HUD is priority-0 sprites; the scenery's are priority 1. Tiles are handed out per mode, so the same tile can be another piece in another mode (808-815 is LAP in V-Rally Cross, the damage lights in V-Rally Mode): each piece is told by tile, palette, place and shape:

- BEST TIME / CHECK POINT and its time: tiles 816-831, palette 5, two 32x16 at (7, 6) and (39, 6);
- the clock: 1000-1015, palette 8, two 32x16 at (167, 6) and (199, 6);
- POS (stays): 1016-1023, palette 5, two 32x8 at (7, 30), (39, 30);
- LAP: 808-815, palette 5, two 32x8 at (7, 46), (39, 46);
- the damage lights: 800-815, palette 8, two 16x32 at (215, 28), (215, 60);
- the course map: its line 832 (palette 6, one 64x64 at (176, 96)), its backing 900-909 (palette 7, semi-transparent, twelve pieces round it), the dots 896-899 (palette 6: yours 896, the rivals' 897-899);
- the rev counter (stays): 913, palette 0, one 64x64 at (-3, 94), its needle, gear and speed drawn into its tiles;
- the pace-note arrow (stays): 996, palette 5, 16x16 at (112, 32), (112, 16) in the in-car view;
- over the track (stays): the exhaust flash 977-979 (palette 1), the dust clouds 980 (palette 2, semi-transparent).

Other addresses found (for tests): the car's segment along the stage 0x03001CE4 (a halfword; the game works it out again from the car's place each frame, so writing it moves the car along the stage: on Time Trial's first France stage, 300 puts it over the finish line; on longer stages it wraps round at about 285); the stage clock, in hundredths, is not that halfword (0x02039490 runs on through the finish).

## Accepted (not listed as a known issue)

- After a stage, the game blanks its screen about a quarter of a second before "The game is being saved" comes up: the last HUD stays on the second screen through it, as the second screen does for a moment between scenes in every game (it goes back to the logo once the HUD has been gone for a third of a second). To check on the device.

## Not covered yet

- **The end of a V-Rally Mode stage** (the stage's standings) and **of a V-Rally Cross race** (the results): the segment trick above doesn't reach their finish lines, and steering a whole stage by script didn't work out. They're expected to show the logo (they're menus, without the stage's interrupt). *Needs a state just before the finish of each, from the owner.*
- **V-Rally Cross's boost** (the second race of each licence, after leading laps in the first): its bar wasn't reached. *Needs a state with the bar up.* Until then it stays on the game screen (not one of the HUD's tiles).
- **Damage** in V-Rally Mode: the lights were only seen green (no damage). They're the same sprites when they change colour, so they're expected to move alike.
- Weather (rain, snow, night) and the other rallies' stages: only France (Time Trial), Kenya (V-Rally Mode) and Departmental's first track (V-Rally Cross) were tried.
- Link play needs a link cable: not tried.

## Exceptions

- The course map drawn twice as big (as Mario Kart's, the F-Zero games' and Crash Nitro Kart's): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## For the merge

Changelog line (under the open version's What's new):

- V-Rally 3 (USA, Europe and Japan) is supported: in a stage, the HUD moves to the second screen (the best time or checkpoint split, the clock, the lap in V-Rally Cross, the damage lights in V-Rally Mode, and the course map twice as big); the rev counter, your place and the pace notes stay on the game screen. The logo elsewhere. Not tried yet: the end of a V-Rally Mode stage and of a V-Rally Cross race, and V-Rally Cross's boost.

Wiki, `wiki/Supported-games.md` (table row and section):

| V-Rally 3 | `AVRE` `AVRP` `AVRJ` | the stage's HUD, with the course map twice as big |

### V-Rally 3

The USA, European and Japanese releases.

The HUD is moved off the game screen rather than copied, so nothing behind it is lost.

- **Driving** (Time Trial, V-Rally Mode and V-Rally Cross):
  - The game screen keeps the **rev counter** with your gear and speed (bottom left), the **pace-note arrows** (top centre), and your **place** (POS) in the top-left corner, where BEST TIME was.
  - The second screen has **BEST TIME** (or the green CHECK POINT split) in the top-left corner with **LAP** under it in V-Rally Cross, the **stage clock** in the top-right corner, V-Rally Mode's **damage lights** down the right edge, and the **course map** with every car's dot, twice as big, at the bottom centre.
  - The countdown, NEW RECORD, the hazard boards and, in the in-car view, the dashboard are part of the game's picture and stay on the game screen.
- The game's **pause menu** stays on the game screen, with the HUD on the second screen.
- Everywhere else (the intro, the menus, the service station, loading, the save screen after a stage) the second screen shows the logo, the title lettering from the main menu.
- With one screen (`layout = single`), the HUD stays where the game draws it.

Known issues: the end of a V-Rally Mode stage and of a V-Rally Cross race, V-Rally Cross's boost bar, damage, and link play haven't been tried.

Wiki, `wiki/Philosophy.md`, its list of exceptions: add V-Rally 3 to the race screens' course maps drawn larger ("Mario Kart's, the F-Zero games', Crash Nitro Kart's and V-Rally 3's race screen course maps, drawn larger than the game's").

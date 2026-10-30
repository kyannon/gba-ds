# F-Zero: GP Legend

| | |
|---|---|
| Releases | `BFZE` USA · `BFZP` Europe · `BFZJ` Japan (F-Zero: Falcon Densetsu) |
| Profile | `src/profiles.c`: the `BFZE` entry (with `BFZP`, `BFZJ`) |
| Tests | `tests/regress/f-zero-gp-legend.json` (`tools/regress.py f-zero-gp-legend`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list f-zero-gp-legend`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**FZ-1. Logo.** The logo is the title's F-ZERO GP LEGEND lettering (BG1 alone, without the starburst behind it or PRESS START), taken from power-on.

**FZ-2. HUD moved, not copied.** While racing (every mode: Grand Prix, Story, Time Trial, Training), each frame is drawn twice: the game screen without the HUD pieces that leave it, and the second screen with only those, so nothing behind them is lost. The HUD is the game's sprites: told apart from the machines, the countdown, the lap sign and the track by their palettes, tiles and places.

**FZ-3. Racing: game screen.** The game screen keeps the speedometer where the game draws it (bottom right) and RANK, moved to the bottom left (where the course map was).

**FZ-4. Racing: second screen.** On the second screen (dark, the HUD's pieces on it): the POWER meter at the top centre; the course map at the bottom centre, twice as big, with the racers' dots on it (it blinks as on the GBA, where the game draws it every other frame); every other piece in the top corner of its side, as close up as they go: on the left SPARE MACHINE, then LAST (laps left) moved up into RANK's place; on the right the time, the lap times and the KO count (☠), moved up into POWER's place.

**FZ-5. Pause.** The pause menu (CONTINUE, GIVE UP) stays on the game screen; the HUD stays on the second screen.

**FZ-6. Logo elsewhere.** Wherever the race HUD is gone the second screen shows the logo: the title and menus, a story's objective screen and cutscenes, the end of a race (once the HUD's gone, the RESULTS over the track, the results screen), after GIVE UP, and the course name as a race fades in.

**FZ-7. One screen.** With `layout = single` the HUD stays where the game draws it.

**FZ-8. Time Trial.** Time Trial has no RANK: its BEST TIME (the label and the time under it, where RANK is in other races) goes to the second screen's top left, with LAST below it where the game draws it; nothing moves to the game screen's bottom left.

## Exceptions

- The course map drawn twice as big (as Mario Kart's): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## Not covered yet

- The European and Japanese releases are checked for their logo; their races use the USA release's addresses and sprites (not tried).

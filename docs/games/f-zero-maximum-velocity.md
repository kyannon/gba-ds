# F-Zero: Maximum Velocity

| | |
|---|---|
| Releases | `AFZE` USA / Europe · `AFZJ` Japan (F-Zero for Game Boy Advance) · `AFZC` China (Jisu F-Zero Weilai Saiche) |
| Dumps | `AFZE` cartridge `bd5e9798` (tested), Virtual Console `4b5f1cdd` · `AFZJ` cartridge `25e3fc9a` (tested), Virtual Console `d3cc19cc` · `AFZC` cartridge `0e5c38b7` (tested) |
| Profile | `src/profiles.c`: the `AFZE` entry (with `AFZJ`; `AFZC` a variant) |
| Tests | `tests/regress/f-zero-maximum-velocity.json` (`tools/regress.py f-zero-maximum-velocity`) |
| Status | testing (branch `game/f-zero-maximum-velocity`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list f-zero-maximum-velocity`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The second screen follows [F-Zero: GP Legend](f-zero-gp-legend.md)'s: the race HUD moves there, the course map twice as big, and what you watch while you drive (your place and your speed) stays on the game screen.

## Rules

**FZM-1. Logo.** The logo is the title's lettering (F-ZERO and MAXIMUM VELOCITY; in Japan F-ZERO FOR GAME BOY ADVANCE; in China its own title), the title's sprites without PRESS START, the copyright line or the Earth behind, taken from power-on.

**FZM-2. HUD moved, not copied.** While racing (Grand Prix and Training: from the countdown to the moment the game takes its HUD down), each frame is drawn twice: the game screen without the HUD pieces that leave it, and the second screen with only those, so nothing behind them is lost. The HUD is the game's sprites, told apart from the machines, the numbers over rivals ahead, the signs over the track (READY, the laps left, FINAL LAP, TURN BACK, LOW POWER, the place at the finish) and CHECK by their tiles, palettes and places.

**FZM-3. Racing: game screen.** The game screen keeps the speed where the game draws it (top right), the CHECK sign under a machine close behind, and RANK with the race's limit (1/15, then /10, /7, /5, red when you're outside it), moved to the bottom left, where the course map was. Training has no RANK; nothing moves there.

**FZM-4. Racing: second screen.** On the second screen (dark, the HUD's pieces on it): the POW meter at the top centre, with BOOST and its three marks centred just under it (in every race that has them, Training too; until v1.4.10's second test build of this game BOOST stayed on the game screen); the course map at the bottom centre, twice as big; the game draws the map every other frame and your dot every frame, so the map is held steady between and the dot moves every frame; SPARE MACHINES in the top left corner with LAST (laps left) two rows under it, lined up with its left edge, or in the corner itself when there's no SPARE MACHINES (Training); the time, with each lap's time under it as a lap ends, at the top right, moved up into the speed's place.

**FZM-5. Pause.** The pause menu (CONTINUE, GIVE UP) stays on the game screen; the HUD stays on the second screen.

**FZM-6. Logo elsewhere.** Wherever the race HUD is gone the second screen shows the logo: the title and menus, the course name as a race fades in, the end of a race once the game takes its HUD down (the place sign over the finish line keeps it till then), the results (once A is pressed on YOU LOST, FZM-8), and while the camera circles a destroyed machine (the game's HUD is gone then too).

**FZM-7. One screen.** With `layout = single` the HUD stays where the game draws it.

**FZM-8. YOU LOST.** After GIVE UP, or once a destroyed machine's wreck has been circled, YOU LOST is shown over the track with the HUD: the HUD stays on the second screen, as while racing, until A takes the game on to the results. (Until v1.4.10's second test build of this game the second screen showed the logo there, and the HUD came back onto the game screen beside YOU LOST.)

## Known issues

- As a race ends, and as A is pressed on YOU LOST, the HUD shows again on the game screen for 2 frames (a thirtieth of a second) before the game takes it down: the game lowers the flag GBA DS goes by two frames before its sprites go, and no byte of its memory marks those two frames. Hiding them would need another change to shared code (keeping a split on while a given sprite tile is still shown); left for now. (A machine destroyed has no such flash: the split carries on into FZM-8's.)

## Not covered yet

- The multiplayer modes (Multipak and Single-Pak Link) need a link cable: not tried. Their races use the race HUD's registers; what they show is unknown.
- Courses past the Pawn Cup (Knight, Bishop, Queen, Champion series) are expected to behave alike: only Bianca City (Stretch Circuit) has cases.

## Exceptions

- The course map drawn twice as big (as F-Zero: GP Legend's and Mario Kart's): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## For the merge

Shared code changed on this branch, so the merge runs the full suite: `src/profiles.h` and `src/game.c` (a split HUD's moves: up to 6 instead of 4, `HUD_MOVES`, for BOOST under POW; the racing composer's loops and arrays only, hud.overMap's pieces stay 4).

Changelog line (under the open version's What's new):

- F-Zero: Maximum Velocity is supported (USA / Europe, Japanese and Chinese releases), with F-Zero: GP Legend's second screen: while racing, the HUD moves to the second screen (POW, spare machines, laps left, the time and lap times, and the course map twice as big), leaving the track clear, with your rank and speed on the game screen; the HUD stays there through YOU LOST.

Rules index, `docs/games/README.md` (table row, after F-Zero: GP Legend):

| [F-Zero: Maximum Velocity (F-Zero for Game Boy Advance in Japan)](f-zero-maximum-velocity.md) | FZM | `AFZE` + 2 |

Wiki, `wiki/Philosophy.md` (the exceptions list): "Mario Kart's and F-Zero: GP Legend's race screen course maps" becomes "Mario Kart's and the F-Zero games' race screen course maps".

Wiki, `wiki/Supported-games.md` (table row and section):

| F-Zero: Maximum Velocity (F-Zero for Game Boy Advance in Japan) | `AFZE` `AFZJ` `AFZC` | racing: the HUD and the course map, 2x |

### F-Zero: Maximum Velocity

Grand Prix and Training races have been tried in every release (USA / Europe, Japanese, Chinese); the link-cable modes haven't.

- **While racing,** the HUD moves to the second screen: POW at the top with BOOST under it, SPARE MACHINES and LAST at the top left, the time and lap times at the top right, and the course map at the bottom, twice as big. The game screen keeps the track clear, with RANK (and the race's limit) at the bottom left and your speed at the top right.
- Paused, and on YOU LOST (after GIVE UP, or a destroyed machine), the HUD stays on the second screen until the results.
- On the title, the menus, the course name before a race, after a race (once the HUD's gone), while the camera circles a destroyed machine, and the results, the second screen shows the logo.

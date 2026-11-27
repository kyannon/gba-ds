# F-Zero Climax

| | |
|---|---|
| Releases | `BFTJ` Japan |
| Dumps | `BFTJ` cartridge `dbdc71e3` (tested), Virtual Console `13e65eb4` |
| Mods | `BFTJ` F-Zero Climax English Patch 1.1 `80366a0a` (tested) |
| Profile | `src/profiles.c`: the `BFTJ` entry |
| Tests | `tests/regress/f-zero-climax.json` (`tools/regress.py f-zero-climax`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list f-zero-climax`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game came out only in Japan. Many players play it in English with the F-Zero Climax Translation Project's English Patch 1.1 (an IPS patch for the Japanese ROM): the rules hold for it as for the Japanese game (FZC-9).

It runs on F-Zero: GP Legend's engine, and its second screen is GP Legend's ([FZ rules](f-zero-gp-legend.md)), so the three F-Zero games work the same way. Its HUD is drawn from its own tiles, so it has a profile of its own.

## Rules

**FZC-1. Logo.** The logo is the title's lettering: エフゼロ クライマックス above F-ZERO CLIMAX (BG2 alone, without the machines behind it or PRESS START BUTTON), taken from power-on, through the screen the game shows while it sets up a new save.

**FZC-2. HUD moved, not copied.** While racing, each frame is drawn twice: the game screen without the HUD pieces that leave it, and the second screen with only those, so nothing behind them is lost. The HUD is the game's sprites, told apart from the machines, the countdown and the messages over the track (TURN BACK!, YOU LOST) by their tiles, palettes and places.

**FZC-3. Racing: game screen.** The game screen keeps the speed where the game draws it (bottom right) and RANK, moved to the bottom left, where the course map was.

**FZC-4. Racing: second screen.** On the second screen, dark, with the HUD's pieces on it: the POWER meter at the top centre; the course map at the bottom centre, twice as big and whole, with the racers' dots on it (the game draws the map every other frame and the dots every frame, so the map is held steady between and the dots move every frame); LAST (laps left) in the top left corner; on the right the time and the KO count, moved up into POWER's place.

**FZC-5. Pause.** The pause menu (CONTINUE, GIVE UP) stays on the game screen; the HUD stays on the second screen.

**FZC-6. Logo elsewhere.** Wherever the race HUD is gone the second screen shows the logo: the title and menus, the course card before a race, the course fly-in with its name, and the end of a race once the game takes its HUD away. Past the finish line and through GIVE UP's YOU LOST, the game still draws its HUD, so it stays on the second screen; the logo comes with the RESULTS over the track (the lap times, or RETIRE after YOU LOST) and stays through the standings and RETRY?.

**FZC-7. One screen.** With `layout = single` the HUD stays where the game draws it.

**FZC-8. Other modes.** Grand Prix is checked at Novice: Mute City to the finish, and Sand Ocean and Big Blue racing. Time Attack and Survival open only once the game's story has gone far enough, so they weren't checked in a session; the player who contributed the game support has tried them, and Course Edit's TEST RUN, and reports that all three work well (Nov 23, 2026). Their races run on the same engine, so the HUD's pieces they share with Grand Prix move the same way, and any piece of their own stays on the game screen (RANK moves only when it's there). No fixtures or cases for these modes: the checks are the player's.

**FZC-9. The English translation.** On the English Patch 1.1 (as a patched ROM or with its IPS on the Japanese ROM, GEN-9) FZC-1 to FZC-8 hold as they are: the race HUD is the Japanese game's, sprite for sprite. Its logo is the translation's own title, F-ZERO CLIMAX without the katakana line, taken the same way and kept apart from the Japanese game's (GEN-20), so with both in the game list each shows its own. (Until GEN-20 both showed the logo made first.)

## Exceptions

- The course map drawn twice as big (as GP Legend's and Mario Kart's): a GBA pixel per 2x2 of the second screen's GBA screen, so still the game's own picture at its own resolution.

## Not covered yet

- Time Attack, Survival and Course Edit's TEST RUN (FZC-8): tried and working according to the player who contributed the game support, but with no state or case here (Time Attack and Survival are locked on a new save, and the test run needs a finished course of the player's own). A save with them open would let a case be written.

## Notes

| | |
|---|---|
| Racing | display mode 2 with BG2 (the track), BG3 (the backdrop) and sprites: DISPCNT 0x3C62, as in GP Legend |
| HUD sprites | priority 0: RANK 666-668 and LAST 655-657 (palette 15) with their digits 970-980 (palettes 14, 15); POWER 339-357 (palettes 12, 13); the time 992-1016 (palette 14); the KO icon 620 (palette 15) with its count (970-980, palette 14); the speed 992-1016 (palette 14) with km/h 336-338 (palette 15); the course map one 64x64 sprite, tile 882 (palette 15) at 0, 96, every other frame, with the dots 654 (any palette) |
| HUD rows | RANK 3-9 and LAST 17-23 at the left; POWER 2-9 (x 161-234), the time 15-23 (x 177-227), KO 40-47 (x 210-227) at the right; the speed 147-155 |
| Messages | TURN BACK! and YOU LOST are tiles 256 and 260 (palette 15) at y 72: the same tiles for every message, so they can't tell when a race is over |
| Race over | 0x02018EDB: 0xFF while racing (the fly-in, the countdown, every lap, the pause menu, the machine blowing up and coming back), 0 once finished (the frame the line is crossed), 1 once GIVE UP is confirmed; 0 outside a race |
| Laps | 0x0201557A counts the laps done (LAST is 3 minus it); 0x02015578 and 0x02015579 count with it: all three set to 2, the next crossing finishes the race |
| Results | the RESULTS header is tiles 304-310 (palette 15) at y 12, on the lap times and on RETIRE; no race uses tiles 300-332 |
| Courses | the course map is in the same place (0, 96) on Mute City, Sand Ocean and Big Blue |
| Title | the lettering on BG2 from about frame 1080 of a fresh start (a new save's set-up screen first, about 480 frames) |

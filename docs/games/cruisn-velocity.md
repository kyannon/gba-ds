# Cruis'n Velocity

| | |
|---|---|
| Releases | `ACFE` USA, Europe (one ROM for both) |
| Dumps | `ACFE` cartridge `adf14db5` (tested) |
| Profile | `src/profiles.c`: the `ACFE` entry |
| Tests | `tests/regress/cruisn-velocity.json` (`tools/regress.py cruisn-velocity`) |
| Status | merged into `dev` for v1.6.4 (pull request #98 by cmyksoda); tried on the RG DS Plus |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list cruisn-velocity`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game has no map and no rear-view mirror: its race screen is the road with seven HUD pieces, so the second screen takes the two you read rather than watch, and the logo fills the rest. Its lap times are a page of their own after the race, on the game screen.

## Rules

**CRV-1. Logo.** The logo is the title's lettering, Cruis'n over VELOCITY with its base bar (the silver dashes on black, the orange line under them), cut out of the title's one picture: without the grey and the light burst behind it, the pale glow round the script's strokes, the shadow under the bar, the TM or PRESS START. Every pixel is the title's own; the script ends on its yellow and orange, and the Y's white arm is kept as the game draws it. Taken from power-on, with the title fully faded in.

**CRV-2. In a race: the time and the lap.** While a race is on, from the course's intro to the end of the last lap (the finish is CRV-7), the second screen shows the time (the dark box with its six digits, top left) and the lap box (LAP and the lap out of the laps, top centre), each at the place the game draws it, over the logo at full strength on black (GEN-1). They leave the game screen. Until the first of them is drawn (the track fading in behind the course name), the logo as on any idle second screen.

**CRV-3. The rest of the race screen stays.** The game screen keeps what you watch as you drive: your place (the red 8th at the top right), the speed with the turbos under it (bottom left), the gear and rev bar (bottom right), the start lights, WRONG WAY and the checkpoint signs.

**CRV-4. Paused.** START turns the race into the PAUSED page (CONTINUE, RESTART RACE, QUIT) on the game screen; the second screen keeps the time and lap as they were before the game faded to the page, so the same page is never on both. They go live again when the race carries on (held through its fade back in). QUIT's question keeps them too; once the race is left for the menu, the logo is back (CRV-5).

**CRV-5. Everywhere else: the logo.** The title, the menus (the main menu, game and car types, the track, the race's intro page), the cup standings and GAME OVER after a race, and the transitions show the logo on the second screen, at half strength on dark grey (GEN-1).

**CRV-6. One screen.** With `layout = single` the time and the lap box stay where the game draws them.

**CRV-7. The finish.** The game itself clears its time box to 00 00 00 a few frames after the car crosses the line for the last time, and leaves it so through the flag, the fade and its results page (the lap times and the total), with the lap box still on the last lap. From the line, the second screen's time box shows the race's total, the one the results page gives (02:05:45 for laps of 30:38, 28:58, 27:04 and 39:45), drawn with the game's own digits, and the lap box stays on the last lap (LAP 4/4), through the flag, the fade and the results page; it never shows a cleared 00 00 00. A total the game's page misprints (its adding leaves the hundredths at exactly 100, or the seconds at exactly 60) is the right total in the box. When the game goes on from the results (the cup standings, GAME OVER, the menus), the logo is back (CRV-5); the next race's own time and lap box come back live from its first frame drawn (CRV-2). The total is an exception to the Philosophy page (only the game's own graphics, in the places it uses them): the digits are the game's, but the figure is the results page's, in the time box.

## Not covered yet

- The finish of a race that places in the cup (1st to 3rd): the cup's podium, the best lap and the pages after a won cup were not reached. Checked: 8th of 8 in the Amateur Cup's first race on Vegas (`crv-finish`, a race driven to the end), which ends on the standings and GAME OVER, and 1st in a Freestyle race on Vegas (`crv-finish-1st`), which ends on the standings, RACE ANOTHER TRACK and the track menu.
- `crv-finish-1st` isn't a race driven to the end: its state is the first race's countdown with the lap byte set to the last lap (4) and laps 1 to 3 written in (30:20, 28:15, 27:20), so crossing the start line ends the race in 1st with a last lap of 11:45 and a results page that misprints the total (01:36:1:0 for 01:37:00). It is there for the 1st place and the misprint, which the driven race doesn't reach.
- Cup mode, the other tracks and Multi Player.
- The reverse view of "WRONG WAY": it is drawn in the picture, so stays on the game screen.

## Notes

| | |
|---|---|
| Display | mode 4 bitmap, DISPCNT 0x1F54 (0x1F44 for a few frames of a fade; page flipped): the road, traffic, menus and the title are BG2's bitmap; BG0, BG1 and BG3 are unused |
| Screen | 0x03000020: 3 in a race (after a pause too), 1 on the PAUSED page and in the menus and the title; 0x03000002 is also 0x10 on the PAUSED page, and stays 0x10 in the main menu after QUIT, so 0x03002448 (0 once the race is unloaded) tells them apart. 0x03000000 is the counter of the game's fades, 1 to 16: its whole picture, sprites' palette too, goes to black as START opens the PAUSED page and as a race starts or carries on (the HUD is held lit meanwhile); 0x03000004 and 0x03000006 look like it but stay 1 after a pause |
| HUD | sprites only, priority 0, OBJ tiles from 512 (bitmap mode): the time's six digits (512-524, y 6) and its box (736, 64x32 at 2, 2); the lap box (576, 32x32 at 104, 2) with its two digits (514, 520; y 14); the place (536 at 206, 2); the speed's digits (y 137, the same tiles as the time's), MPH (810), the turbos (808; y 149), its box (608); the gear (524 at 192, 129), the rev bar (568), its box (672); the start lights (826, 32x32 at y 40) |
| Finish | the lap byte 0x0300355A (1-based) goes past the lap box's total digit (the sprite at x 125, y 14) as the line is crossed; the next frame the six time digits are tile 512 (0). The game fades (0x03000000 counts 1 to 16 and then stays 16 on every page after), the screen flag 0x03000020 goes to 1 for the results, and the HUD's sprites stay in OAM, moved to y 160. The page is 0x03002054: 0x0B in the intro and race, 0x10 the lap-time results, 0x11 the standings, 0x13 and 0x1B the next ones after a cup race (GAME OVER), 0x12 RACE ANOTHER TRACK after a Freestyle race and 0x0F its track menu, 4 the menus, 3 the title, 0x1D the PAUSED page; 0x03000006 stays 4 on all of them. The box is the running lap's time: for the run checked its last reading was 39.44, and the results list that lap as 00:39:45. The laps' times stay in the car's block, 0x03003565 + 3 * (lap - 1): minutes, seconds, hundredths, each as a number (the last lap's written as the line is crossed, the lap byte's frame). The page's TOTAL is their sum (checked by poking them before the page drew: 59:99 four times gives 03:59:96, 10:07 + 20:08 + 30:09 + 40:95 gives 01:41:19), the carries done with `>` where `>=` was meant, so hundredths that add to exactly 100 or seconds to exactly 60 stay as they are (30:50 twice prints 00:60 with a stray mark in the hundredths). No total is kept: the second screen adds the laps (`crv_retile`) and draws it with the digit tiles 512 + 2 * the digit on the time box's six sprites in the HUD renderer's copy of the sprite list (`hud.retile`), the game's own memory and screen untouched |
| Race start | the pause the course name shows over: bars of VEGAS and FREESTYLE on black while the track fades in, with no HUD sprites yet; then the lights |
| Title | one raw 240x160 bitmap in the ROM (0x000D728C, 256 colours, uncompressed): flat grey 132 with a light burst behind the logo, dithered; up and fully faded in by frame ~720, PRESS START blinking every 240 frames. There is no separate art of the logo in the ROM (searched whole), and the menus' header bars have only the small script |

# Kirby: Nightmare in Dream Land

| | |
|---|---|
| Releases | `A7KE` USA, `A7KP` Europe, `A7KJ` Japan |
| Dumps | `A7KE` cartridge `20ef3f64` (tested), Virtual Console `1af07ac8` · `A7KP` cartridge `3b7a7477` (tested), Virtual Console `7336b1fa` · `A7KJ` cartridge `c55a3c2c` (tested), Virtual Console `15235965` |
| Profile | `src/profiles.c`: the `A7KE` entry; `A7KP` and `A7KJ` are variants (`kdl_jpn`) |
| Tests | `tests/regress/kirby-nightmare-in-dream-land.json` (`tools/regress.py kirby-nightmare-in-dream-land`) |
| Status | in progress |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list kirby-nightmare-in-dream-land`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game has no map. Its pause screen is a page about the copy ability Kirby has, so that page is the second screen in a level, kept up to date the way Wario Land 4's pause screen is ([WL4 rules](wario-land-4.md)).

## Rules

**KND-1. Logo.** The logo is the title's lettering: Kirby over its red swoosh, and NIGHTMARE IN DREAM LAND (BG1 and BG2 above row 116, without the sky behind them, the copyright line or PRESS START). In Japan it is HOSHI NO KIRBY: YUME NO IZUMI DELUXE over its crest, on the same layers.

**KND-2. In a level: the ability's page.** While Kirby is in a level, the second screen shows the game's pause page, opened on the hidden copy and taken again as Kirby goes into another room and about once a second: the copy ability Kirby has, its name, what the game says about it and its picture; with no ability, the game's tip about copying. A new ability is taken at once: dropped with SELECT, its page shows within a few frames; copied, as soon as Kirby's copy pose is over (the game ignores START during it, so the hidden copy presses it again until the page opens).

**KND-3. The game screen as the game draws it.** The HUD (lives, health, the score, and the ability's card at the bottom left) stays where the game draws it.

**KND-4. Logo elsewhere.** The second screen shows the logo at half strength before the first page and wherever Kirby isn't in a level: the title, the file and game select, the opening, a level's title card and the hubs of doors between levels. Back in a level, the last page comes back until a new one is taken.

**KND-5. The game's own pause screen.** While the game's pause page is open on the game screen, the second screen shows the logo at half strength, so the same page is never on both (as Muppet Pinball Mayhem's pause, MPM-4). The second screen does not fade: while the game screen fades to white and back as the pause page opens and closes, the second screen changes at once, the logo in as the page opens and the last page back as it closes.

## Not covered yet

- Boss fights, the Sub-Games, the museums and arenas, and the Extra Mode.
- Multiplayer.

## Notes

| | |
|---|---|
| Regions | Europe (`A7KP`): the same addresses as the USA release. Japan (`A7KJ`): the three IWRAM variables 0x18 lower (screen mode 0x030023D0, paused 0x030023B8, room 0x03002450), found with `tools/romxref.py`; the ability byte 0x02004B50 is the same. All the rules hold in each |
| Screen mode | 0x030023D8: 3 the title, 4 the menus, 7 a level's title card, 5 a hub, 8 in a level (still 8 while paused) |
| Paused | 0x030023C0: 8 while the pause page is open (in a level or a hub), 0 otherwise; set the frame after START, cleared as the level's fade in ends |
| Room | 0x03002468: 0 in a level's first room, then the room's number |
| Health | 0x03002808: 8 a cell, 0x30 full |
| Copy ability | 0x02004B50: 0 none, 2 Spark; changes the frame the ability does (0x02005591 follows about a second later, with the HUD card) |
| Pause page | a whole screen (BG1-BG3, sprites off), drawn and still about 34 frames after START; one page only (L, R and SELECT do nothing there) |
| Level | display mode 0, DISPCNT 0x1D00: the HUD is BG0 (priority 0), the level BG2 and BG3, Kirby and the enemies sprites |
| Title | the lettering on BG1 (x 36-215, y 11-115) over the swoosh on BG2 (y 0-100) from about frame 470 to 1170 of a fresh start; the copyright on BG3, PRESS START sprites |

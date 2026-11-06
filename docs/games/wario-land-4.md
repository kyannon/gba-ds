# Wario Land 4 (Wario Land Advance: Youki no Otakara in Japan)

| | |
|---|---|
| Releases | `AWAE` USA and Europe · `AWAJ` Japan (the same addresses and screens) |
| Dumps | `AWAE` cartridge `d6141609` (tested), Virtual Console `8bb73d8a` · `AWAJ` cartridge `f56ebb7a` (tested), Virtual Console `a8cd90f9` |
| Profile | `src/profiles.c`: the `AWAE` entry (with `AWAJ`) |
| Tests | `tests/regress/wario-land-4.json` (`tools/regress.py wario-land-4`) |
| Status | testing (branch `game/wario-land-4`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list wario-land-4`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Lady Sia ([LS rules](lady-sia.md)): the game's own pause screen, opened on the hidden copy and kept up to date while Wario is in a level, its menu left out. The game has no map; its pause screen is the one place that shows what's still to find in a level, which is what the player would otherwise pause to check.

## Rules

**WL4-1. Logo.** The logo (GEN-1) is the title's lettering with its ™: WARIO LAND 4 in the USA and Europe, ワリオランドアドバンス (Wario Land Advance) in Japan, without PUSH START, the desert, Wario's car or the copyright line.

**WL4-2. In a level: the pause screen.** While Wario is in a level (any of the Entry, Emerald, Ruby, Topaz, Sapphire and Golden passages' levels, the Hall of Hieroglyphs too), the second screen shows the game's pause screen (START), opened on the hidden copy and taken again about once a second, as the game draws it once it has slid fully into place: the bar with Wario's hearts and heart meter, the money he carries and the level's coins against its best, the wall below it with the jewel (each of the four gem pieces found so far in its place), the Keyzer once he has it and the CD once he has it (in the passages that have one: not the Hall of Hieroglyphs or the Golden Passage), and the plate with the level's name. The menu along the top (RETURN, QUIT, GIVE UP and the pointing hand) is left out: its rows, down to the wall's first row of bricks, are dark grey, and the rest is where the game draws it. With `map_crop = 0` (gba-ds.cfg) the whole pause screen is shown, the menu too.

**WL4-3. Found treasure shows at once.** When Wario opens a chest for a gem piece, takes the Keyzer or finds the CD, or goes into another level, a new picture is taken as soon as the pause screen can be opened (GEN-6), without waiting for the next second.

**WL4-4. The game screen is the game's.** The game screen is as the game draws it: the hearts and heart meter, the coins and, once the Frog Switch is hit, the countdown stay there. (The pause screen has no countdown, so it has nowhere else to go.)

**WL4-5. The escape.** After the Frog Switch, while the countdown runs, the second screen shows the pause screen as in the rest of the level (WL4-2), taken from the hidden copy as the countdown goes on.

**WL4-6. The game's own pause.** The pause screen opened in the game stays on the game screen (its QUIT and GIVE UP questions too); the second screen keeps its last picture meanwhile. Wherever else in a level the pause screen can't be opened (going through a door or pipe, the wavy screen as a level is left), the last picture stays.

**WL4-7. Logo elsewhere.** The second screen shows the logo at half strength before the first pause-screen picture and whenever Wario isn't in a level: the title, the opening, the file screen, the passage select (the hub), a passage's level select, the vortex and the tally as a level is entered or left, the boss's corridor (the Item Shop and the mini-games) and the boss fights (where the game's pause screen is just PAUSE on black), and the ending.

**WL4-8. Every passage.** The pause screen is taken the same way in every passage, whose wall has its own colour (purple in the Entry Passage, green in Emerald, red in Ruby, brown in Topaz, blue in Sapphire, yellow in the Golden Passage), and in either release.

## How it was checked

Every level of every passage was loaded in both releases (on a passage's level select, the passage at 0x03000002 and the level at 0x03000003 set, then A) and its pause screen opened; the gem pieces, Keyzer and CD were set in memory (0x03000C07-0x03000C0A, 0x03000C0C, 0x03000C0B) and the pause screen shows them; the escape was started by putting Wario over the Frog Switch. A boss's corridor was reached by setting its passage's level records to cleared (0x03000A68 + 24 × passage + 4 × level; 0x2F for a cleared level, 0x08 for the boss door). The Normal difficulty of a new game was used throughout.

## Not covered yet

- Wario's transformations (Puffy, Zombie, Fat, Flaming...), the bonus rooms behind the pipes and the moment a gem chest opens: the pause screen is expected to open as usual there, or the last picture to stay; not tried.
- Losing every heart (Wario thrown out of the level): expected to go to the logo with the level select.
- Hard and Super Hard difficulty.
- The ending and the Sound Room.

## Facts found so far (for sessions)

| What | Where / value (both releases) |
|---|---|
| The screen | 0x03000C3A (halfword): 0 the opening and scenes, 1 the hub, a passage's level select and the vortex and tally, 2 in a level (the boss corridor and fight too), 4 the pause screen up, 9 the file screen. 0x03000C3C: 2 while playing (6 as a level is left, 7 with a boss's plain PAUSE) |
| Where | 0x03000002 the passage (0 Entry, 1 Emerald, 2 Ruby, 3 Topaz, 4 Sapphire, 5 Golden), 0x03000003 the level (0-3; 4 the boss) |
| This run's finds | 0x03000C07-0x03000C0A the four gem pieces, 0x03000C0B the CD, 0x03000C0C the Keyzer (nonzero once found) |
| Level records | 0x03000A68: 4 bytes per level, 6 per passage (bits 0-3 the pieces, 4 the CD, 5 the Keyzer; the boss slot 0x08 opens its door) |
| Pause screen | slides down from the top over about 40 frames; rows 0-23 its menu (BG0 words, BG1 frames, the hand a sprite), the wall (BG2) in 8-row courses from row 24; the plate 141-158 |
| Debug | 0x03000C3C = 8 in a level: free movement (tcrf.net); L+UP there clears the level with all four pieces and the Keyzer |
| Title | the lettering is BG0 alone, up about 240 frames after START skips the intro (START at frame 900) |

## For the merge

- Changelog: "Wario Land 4 (USA, Europe and Japan): the second screen shows the game's pause screen while you're in a level: your hearts and money, the level's coins against its best, and the gem pieces, Keyzer and CD you've found so far."
- Wiki (Supported-games.md):

  ### Wario Land 4

  Releases: USA and Europe, Japan (Wario Land Advance).

  In a level, the second screen shows the game's pause screen, kept up to date about once a second: your hearts, the money you carry, the level's coins against its best, the jewel with the gem pieces you've found, the Keyzer and the CD, and the level's name. A piece shows as soon as you find it. The menu along the top of the pause screen is left out. Everywhere else (the title, the hub, the level select, boss fights) it shows the game's logo.

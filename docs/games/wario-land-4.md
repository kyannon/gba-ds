# Wario Land 4 (Wario Land Advance: Youki no Otakara in Japan)

| | |
|---|---|
| Releases | `AWAE` USA and Europe · `AWAJ` Japan (the same addresses and screens) |
| Dumps | `AWAE` cartridge `d6141609` (tested), Virtual Console `8bb73d8a` · `AWAJ` cartridge `f56ebb7a` (tested), Virtual Console `a8cd90f9` |
| Profile | `src/profiles.c`: the `AWAE` entry (with `AWAJ`) |
| Tests | `tests/regress/wario-land-4.json` (`tools/regress.py wario-land-4`) |
| Status | supported (new in v1.4.11) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list wario-land-4`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Lady Sia ([LS rules](lady-sia.md)): the game's own pause screen, opened on the hidden copy and kept up to date while Wario is in a level, its menu left out, and the HUD taken off the game screen as it's on the second. In the boss fights, which have no such pause screen, the HUD moves over as in Super Mario Advance 2 ([SMA rules](super-mario-advance-2.md)). The game has no map; its pause screen is the one place that shows what's still to find in a level, which is what the player would otherwise pause to check.

## Rules

**WL4-1. Logo.** The logo (GEN-1) is the title's lettering with its ™: WARIO LAND 4 in the USA and Europe, ワリオランドアドバンス (Wario Land Advance) in Japan, without PUSH START, the desert, Wario's car or the copyright line.

**WL4-2. In a level: the pause screen.** While Wario is in a level (any of the Entry, Emerald, Ruby, Topaz, Sapphire and Golden passages' levels, the Hall of Hieroglyphs too, and in any of Wario's forms: swimming, on fire, fat, frozen, zombie, a snowball, bouncy, puffy, a bat or flattened), the second screen shows the game's pause screen (START), opened on the hidden copy and taken again about once a second, as the game draws it once it has slid fully into place: the bar with Wario's hearts and heart meter, the money he carries and the level's coins against its best, the wall below it with the jewel (each of the four gem pieces found so far in its place), the Keyzer once he has it and the CD once he has it (in the passages that have one: not the Hall of Hieroglyphs or the Golden Passage), and the plate with the level's name. The menu along the top (RETURN, QUIT, GIVE UP and the pointing hand) is left out: the wall's bricks, which the game draws behind it, show there instead, so the wall runs from the top of the screen; the rest is where the game draws it. With `map_crop = 0` (gba-ds.cfg) the whole pause screen is shown, the menu too.

**WL4-3. Finds and hits show at once.** When Wario opens a chest for a gem piece, takes the Keyzer or finds the CD, gains or loses a heart, or goes into another level, a new picture is taken as soon as the pause screen can be opened (GEN-6), without waiting for the next second.

**WL4-4. In a level, the game screen without the hearts and coins.** While Wario is in a level, the hearts and heart meter (top left) and the coins (top right) are left off the game screen: they're on the second screen's pause screen (WL4-2). The coins there are at most a second behind; the hearts change at once (WL4-3). Once the Frog Switch is hit, the countdown stays on the game screen (the pause screen has none). Everything else is as the game draws it; in the game's own pause screen, its menus, while Wario is dying (WL4-7) and everywhere outside a level the HUD is drawn as usual, and with the second screen off (Map: Off) it's back on the game screen.

**WL4-5. The escape.** After the Frog Switch, while the countdown runs, the second screen shows the pause screen as in the rest of the level (WL4-2), taken from the hidden copy as the countdown goes on.

**WL4-6. The game's own pause.** The pause screen opened in the game stays on the game screen (its QUIT and GIVE UP questions too); the second screen keeps its last picture meanwhile. Wherever else in a level the pause screen can't be opened (going through a door or pipe, the wavy screen as a level is left), the last picture stays.

**WL4-7. Logo elsewhere.** The second screen shows the logo at half strength before the first pause-screen picture and whenever Wario isn't in a level: the title, the opening, the file screen, the passage select (the hub), a passage's level select, a level from the moment Wario loses his last heart (his fall, the fade out and the level select after), the vortex and the tally as a level is entered or left, the boss's corridor (the Item Shop and the mini-games, where the game's pause screen is just PAUSE on black), the Sound Room, and the ending. (The Sound Room, the Mini-Game Shop and the mini-games were checked on the device.) (The boss fights: WL4-9.)

**WL4-8. Every passage.** The pause screen is taken the same way in every passage, whose wall has its own colour (purple in the Entry Passage, green in Emerald, red in Ruby, brown in Topaz, blue in Sapphire, yellow in the Golden Passage), and in either release.

**WL4-9. Boss fights: the HUD on the second screen.** In a boss fight, where the game's pause screen is just PAUSE on black, the HUD moves from the top of the game screen to the top of the second screen, where the game draws it: Wario's hearts and heart meter, the stopwatch and the countdown, and the boss's health bar, on dark grey with the logo below at its own size, whole (the countdown's big figures too). It moves once the fight's opening is over: the big countdown shown in the middle of the room and the stopwatch bouncing about play on the game screen, with the hearts and the boss's bar, and the HUD goes over as the countdown reaches its place at the top. From then on it stays on the second screen to the end of the fight, through the fade after the boss is beaten, until CLEAR! (the logo from there). TIME UP!!, when the countdown runs out, stays on the game screen. (The Golden Diva's countdown comes later in the fight: the HUD stays on the game screen till then.) The boss's corridor before the fight has no HUD: the logo there (WL4-7).

## How it was checked

Every level of every passage was loaded in both releases (on a passage's level select, the passage at 0x03000002 and the level at 0x03000003 set, then A) and its pause screen opened; the gem pieces, Keyzer and CD were set in memory (0x03000C07-0x03000C0A, 0x03000C0C, 0x03000C0B) and the pause screen shows them; the escape was started by putting Wario over the Frog Switch. A boss's corridor was reached by setting its passage's level records to cleared (0x03000A68 + 24 × passage + 4 × level; 0x2F for a cleared level, 0x08 for the boss door). Wario's hearts are at 0x03001910. The boss fights were checked from the owner's bug reports (Spoiled Rotten, the Entry Passage's boss: its opening, and its countdown run out for TIME UP!!) and, for the others, from the owner's GameShark saves (converted with tools/sharkport.py, on the Mario & Luigi branch): on a Normal file with every passage cleared, a beaten boss's record set back to 0x08 (its door open, not beaten), then from the corridor RIGHT for 440 frames, LEFT for 30 and UP at the boss door: Cractus, Cuckoo Condor, Aerodent, Catbat and the Golden Diva, and Cractus in the Japanese release. A Super Hard file's level and the Item Shop (screen 7) were checked from the same saves. Wario's forms were set through 0x03001898, and his death through his hearts set to 0, in the Hall of Hieroglyphs. The Normal difficulty of a new game was used throughout.

## Not covered yet

- The bonus rooms behind the pipes and the moment a gem chest opens: the pause screen is expected to open as usual there, or the last picture to stay; not tried.
- Hard difficulty (Super Hard was checked; the screens are the same).
- Items from the Item Shop used in a fight (expected to stay on the game screen: they're not in the HUD's palettes).
- The ending and the Sound Room.

## Facts found so far (for sessions)

| What | Where / value (both releases) |
|---|---|
| The screen | 0x03000C3A (halfword): 0 the opening and scenes, 1 the hub, a passage's level select and the vortex and tally, 2 in a level (the boss corridor and fight too), 4 the pause screen up, 9 the file screen. 0x03000C3C: 2 while playing (6 as a level is left, 7 with a boss's plain PAUSE) |
| Where | 0x03000002 the passage (0 Entry, 1 Emerald, 2 Ruby, 3 Topaz, 4 Sapphire, 5 Golden), 0x03000003 the level (0-3; 4 the boss) |
| This run's finds | 0x03000C07-0x03000C0A the four gem pieces, 0x03000C0B the CD, 0x03000C0C the Keyzer (nonzero once found) |
| Level records | 0x03000A68: 4 bytes per level, 6 per passage (bits 0-3 the pieces, 4 the CD, 5 the Keyzer; the boss slot 0x08 opens its door) |
| Pause screen | slides down from the top over about 40 frames; rows 0-23 its menu (BG0 words, BG1 frames, the hand a sprite), the wall (BG2) in 8-row courses from row 24; the plate 141-158 |
| Hearts | 0x03001910 (the full hearts, 0-8; set to 0 in a level, Wario dies at once) |
| Wario's form | 0x03001898 (DataCrystal's "effect state"): 0 normal, 1 swimming, 2 on fire, 3 fat, 4 frozen, 5 zombie, 6 snowball, 7 bouncy, 8 puffy, 9 bat, 10 flattened. Set in a level, Wario takes that form as if the game had done it |
| HUD | all priority-0 sprites in the top rows. In a level: the hearts and heart meter palette 7 (x 9-72), the coin palette 6 and its figures 4 (x 179-240), the escape's stopwatch and countdown palette 4 (x 80-128). In a boss fight: the hearts 7, the stopwatch and countdown 4, the boss's bar 14 (x 161-240); TIME UP!! palette 2 from row 32. The level's own sprites are priority 1-2 |
| Boss fights | the boss's health: Spoiled Rotten 0x0300014D, Cractus 0x03000255 (set to 1, the next hit beats it; the game checks it only on a hit). 0x03000003 goes 4 to 6 as a boss is beaten. The countdown's figures: tile 128, palette 4, at (96, 2) once in place (WL4_V_CLOCKSET); it slides up from row 50 over the frames before. A beaten boss's record: 0x3F (0x08 opens the door again) |
| Debug | 0x03000C3C = 8 in a level: free movement (tcrf.net); L+UP there clears the level with all four pieces and the Keyzer |
| Title | the lettering is BG0 alone, up about 240 frames after START skips the intro (START at frame 900) |

# Castlevania: Harmony of Dissonance

| | |
|---|---|
| Releases | `ACHE` USA · `ACHP` Europe · `ACHJ` Japan (RAM 0x30 lower) |
| Dumps | `ACHE` cartridge `88c1b562` (tested), Castlevania Advance Collection `3a1e789a` · `ACHP` cartridge `521b3091` (tested), Castlevania Advance Collection `e0c4fd69` · `ACHJ` cartridge `379b3248` (tested), Castlevania Advance Collection `247eeb20` |
| Profile | `src/profiles.c`: the `ACHE` entry; variants `ACHP`, `ACHJ` |
| Tests | `tests/regress/castlevania-harmony-of-dissonance.json` (`tools/regress.py castlevania-harmony-of-dissonance`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-harmony-of-dissonance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**CVH-1. Map during play.** While the game is being played, the second screen shows the game's pause-screen map, captured on the hidden copy every `map_interval` frames (60: every second) and after each room transition.

**CVH-2.** *Retired in v1.5's test builds:* the map drawn inside a border built from the game's own menu artwork (the generated map surround). Overlays are now only bezels around the picture, the same for every game; the map is on the screen's background colour like every other game's.

**CVH-3. Last good map stays.** When the map can't be opened, the last good map stays up.

**CVH-4. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

**CVH-5. Game screen untouched.** The game screen is the game as it draws itself.

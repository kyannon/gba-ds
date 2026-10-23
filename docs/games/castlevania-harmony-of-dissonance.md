# Castlevania: Harmony of Dissonance

| | |
|---|---|
| Releases | `ACHE` USA · `ACHP` Europe · `ACHJ` Japan (RAM 0x30 lower) |
| Profile | `src/profiles.c`: the `ACHE` entry; variants `ACHP`, `ACHJ` |
| Tests | `tests/regress/castlevania-harmony-of-dissonance.json` (`tools/regress.py castlevania-harmony-of-dissonance`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-harmony-of-dissonance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**CVH-1. Map during play.** While the game is being played, the second screen shows the game's pause-screen map, captured on the hidden copy at the Map Refresh interval and after each room transition.

**CVH-2. Map surround.** The map is drawn inside the generated surround built from the game's menu artwork.

**CVH-3. Last good map stays.** When the map can't be opened, the last good map stays up.

**CVH-4. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

**CVH-5. Game screen untouched.** The game screen is the game as it draws itself.

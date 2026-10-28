# Castlevania: Aria of Sorrow

| | |
|---|---|
| Releases | `A2CE` USA · `A2CP` Europe (language menu first) · `A2CJ` Japan (RAM 0x30 lower) |
| Profile | `src/profiles.c`: the `A2CE` entry; variants `A2CP`, `A2CJ` |
| Tests | `tests/regress/castlevania-aria-of-sorrow.json` (`tools/regress.py castlevania-aria-of-sorrow`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-aria-of-sorrow`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**CVA-1. Map during play.** While the game is being played, the second screen shows the game's own pause-screen map, captured on the hidden copy, every `map_interval` frames (60: every second) and right after each room transition.

**CVA-2. Map surround.** The map is drawn inside a border built from the game's own menu artwork (the generated surround), not on bare black.

**CVA-3. Last good map stays.** When the map can't be opened (a cutscene, a menu, a fade), the last good map stays up; nothing half-drawn is ever shown.

**CVA-4. Logo before the first map.** Until the first map is captured, and back at the title and file menus, the second screen shows the game's logo.

**CVA-5. Game screen untouched.** The game screen shows the game exactly as it draws itself; nothing is moved or hidden there.

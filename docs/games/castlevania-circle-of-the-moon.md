# Castlevania: Circle of the Moon

| | |
|---|---|
| Releases | `AAME` USA · `AAMP` Europe · `AAMJ` Japan |
| Profile | `src/profiles.c`: the `AAME` entry; variants `AAMP`, `AAMJ` |
| Tests | `tests/regress/castlevania-circle-of-the-moon.json` (`tools/regress.py castlevania-circle-of-the-moon`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-circle-of-the-moon`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**CVC-1. Map during play.** While the game is being played, the second screen shows the game's pause-screen map, captured on the hidden copy at the Map Refresh interval and after each room transition.

**CVC-2. Completion rate.** The map screen has no completion rate of its own, so the percentage after RATE on the pause screen is copied onto the map, in its top-right corner (bottom-right if the top-right is taken).

**CVC-3. Map surround.** The map is drawn inside the generated surround.

**CVC-4. Last good map stays.** When the map can't be opened, the last good map stays up.

**CVC-5. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

## Not covered yet

Rules no test case checks yet, and the fixture that would cover each:

- **CVC-5:** a state at the title or file menu.

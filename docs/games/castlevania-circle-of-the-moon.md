# Castlevania: Circle of the Moon

| | |
|---|---|
| Releases | `AAME` USA · `AAMP` Europe · `AAMJ` Japan |
| Dumps | `AAME` cartridge `1cc059a4` (tested), Castlevania Advance Collection `14c1ab7c`, Virtual Console `d5412e91` · `AAMP` cartridge `611535dc` (tested), Castlevania Advance Collection `050e2164`, Virtual Console `dba05255` · `AAMJ` cartridge `f3e41d73`, Castlevania Advance Collection `1e60ec88` (tested), Virtual Console `2c56982d` |
| Profile | `src/profiles.c`: the `AAME` entry; variants `AAMP`, `AAMJ` |
| Tests | `tests/regress/castlevania-circle-of-the-moon.json` (`tools/regress.py castlevania-circle-of-the-moon`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-circle-of-the-moon`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**CVC-1. Map during play.** While the game is being played, the second screen shows the game's pause-screen map, captured on the hidden copy every `map_interval` frames (60: every second) and after each room transition.

**CVC-2. Completion rate.** The map screen has no completion rate of its own, so the percentage after RATE on the pause screen is copied onto the map, in its top-right corner (bottom-right if the top-right is taken).

**CVC-3.** *Retired in v1.5's test builds:* the map drawn inside a border built from the game's own menu artwork (the generated map surround). Overlays are now only bezels around the picture, the same for every game; the map is on the screen's background colour like every other game's.

**CVC-4. Last good map stays.** When the map can't be opened, the last good map stays up.

**CVC-5. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

## Not covered yet

- Each release has one moment of play, its pause screen and the file menu. Boss fights, the Battle Arena, later areas and a scene in mid-play (CVC-4) haven't been run.
- The other modes (Magician, Fighter, Shooter, Thief) haven't been tried.

# Metroid: Zero Mission

| | |
|---|---|
| Releases | `BMXE` USA · `BMXP` Europe (language menu first) · `BMXJ` Japan |
| Profile | `src/profiles.c`: the `BMXE` entry; variants `BMXP`, `BMXJ` |
| Tests | `tests/regress/metroid-zero-mission.json` (`tools/regress.py metroid-zero-mission`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list metroid-zero-mission`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**MZM-1. Map during play.** While the game is being played, the second screen shows the pause screen's map in its frame, captured on the hidden copy every `map_interval` frames (60: every second) and after each room transition.

**MZM-2. Tidied map (map_crop on).** With `map_crop` on (the default) the button labels and area name are erased, the scroll arrows and play-time clock left out, the corner minimap gives way to the map underneath with its SELECT / CHANGE AREAS prompt covered by bezel, and the coloured plates are recoloured to the frame's greys.

**MZM-3. No mini-map on the game screen.** While the map is shown, the game's 3x3 mini-map in the game screen's top-right corner is hidden (left out of the drawing, the game itself unchanged).

**MZM-4. Last good map stays.** When the map can't be opened, the last good map stays up.

**MZM-5. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

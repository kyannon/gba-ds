# Metroid Fusion

| | |
|---|---|
| Releases | `AMTE` USA · `AMTP` Europe (language menu first) · `AMTJ` Japan (RAM 0x34 higher) |
| Profile | `src/profiles.c`: the `AMTE` entry; variants `AMTP`, `AMTJ` |
| Tests | `tests/regress/metroid-fusion.json` (`tools/regress.py metroid-fusion`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list metroid-fusion`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**MF-1. Map during play.** While the game is being played, the second screen shows the pause screen's map in its frame, captured on the hidden copy every `map_interval` frames (60: every second) and after each room transition.

**MF-2. Tidied map (map_crop on).** With `map_crop` on, the button labels and area name are erased, the scroll arrows and clock left out, the A OBJECTIVE prompt and its row of item icons left out with the grid rebuilt behind them, and the coloured plates recoloured to the frame's greys.

**MF-3. No mini-map on the game screen.** While the map is shown, the game's mini-map in the game screen's top-right corner is hidden.

**MF-4. Last good map stays.** When the map can't be opened (the intro, a briefing), the last good map stays up, or the logo if there's none yet.

**MF-5. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo.

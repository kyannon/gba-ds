# Metroid: Zero Mission

| | |
|---|---|
| Releases | `BMXE` USA · `BMXP` Europe (language menu first) · `BMXJ` Japan |
| Dumps | `BMXE` cartridge `5c61a844` (tested), Virtual Console `8825294a` · `BMXP` cartridge `f1d92e63` (tested), Virtual Console `3ab66d36` · `BMXJ` cartridge `44b79e2b` (tested), Virtual Console `220b68f5` |
| Profile | `src/profiles.c`: the `BMXE` entry; variants `BMXP`, `BMXJ` |
| Tests | `tests/regress/metroid-zero-mission.json` (`tools/regress.py metroid-zero-mission`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list metroid-zero-mission`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**MZM-1. Map during play.** While the game is being played, the second screen shows the pause screen's map in its frame, captured on the hidden copy every `map_interval` frames (60: every second) and after each room transition.

**MZM-2. Tidied map (map_crop on).** With `map_crop` on (the default) the button labels and area name are erased, the scroll arrows and play-time clock left out, the corner minimap gives way to the map underneath with its SELECT / CHANGE AREAS prompt covered by bezel, and the coloured plates are recoloured to the frame's greys.

**MZM-3. No mini-map on the game screen.** While the map is shown, the game's 3x3 mini-map in the game screen's top-right corner is hidden (left out of the drawing, the game itself unchanged).

**MZM-4. Last good map stays.** When the map can't be opened, the last good map stays up.

**MZM-5. Logo before the first map.** Until the first map is captured, and at the title and file menus, the second screen shows the logo. Back at the title or the file menus after playing (a soft reset), the last map is dropped and the logo shows again (before v1.5.1 the last map stayed).

**MZM-6. The original Metroid.** While the original Metroid is played (OPTIONS > ORIGINAL METROID on the file menu, once unlocked), the second screen shows the logo, any map from before is dropped, and the game screen is as the game draws it (nothing left out of its top-right corner). Before v1.5.1, a map from Zero Mission played before it stayed on the second screen throughout.

## Not covered yet

- Each release has one moment of play, its pause screen and the file menu. Boss fights (Kraid, Ridley, Mother Brain), Chozodia's stretch without the Power Suit and the ending haven't been run.
- The original Metroid is tried in the USA release only (unlocked in memory: 0x03000018, the decompilation's gFileScreenOptionsUnlocked.soundTestAndOrigMetroid, set to 1 before the file menu). It's told apart by the interrupt handler at 0x03007FFC, which isn't Zero Mission's own (0x03000C7C, the same in Europe and Japan) while the NES game runs; the game-mode byte reads 7 there, as in the Chozodia escape, so it can't be used.

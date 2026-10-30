# Spider-Man: Mysterio's Menace

| | |
|---|---|
| Releases | `ASEE` USA and Europe · `ASEJ` Japan (Spider-Man: Mysterio no Kyoui) |
| Profile | `src/profiles.c`: the `ASEE` entry; variant `ASEJ` |
| Tests | `tests/regress/spider-man-mysterios-menace.json` (`tools/regress.py spider-man-mysterios-menace`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list spider-man-mysterios-menace`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SMM-1. Logo.** The logo is the title's lettering (SPIDER-MAN and MYSTERIO'S MENACE; in Japan the Japanese subtitle), without the city behind it or the menu under it. The title comes after the intro (skipped with START).

**SMM-2. Select menu.** While playing a level, the second screen shows the Select menu (the level's picture, its objective and password, the suits, upgrades and web containers; in Japan, ステータス with the objective under it), opened on the hidden copy and kept up to date about once a second, as the game draws it.

**SMM-3. Logo elsewhere.** The second screen shows the logo before the first Select menu picture, and again at the title and through the screens between levels (the intro and story pictures, the city map with the next mission).

**SMM-4. The game's own menus.** The pause menu (START) and the Select menu opened in the game stay on the game screen; the second screen keeps its last picture meanwhile.

## Not covered yet

- Only the first level has been tried (USA / Europe and Japan); later levels, bosses, a lost life and the game over screen are untried.

# Spider-Man: Mysterio's Menace

| | |
|---|---|
| Releases | `ASEE` USA and Europe · `ASEJ` Japan (Spider-Man: Mysterio no Kyoui) |
| Dumps | `ASEE` cartridge `6e135b4d` (tested) · `ASEJ` cartridge `f8125c4b` (tested) |
| Profile | `src/profiles.c`: the `ASEE` entry; variant `ASEJ` |
| Tests | `tests/regress/spider-man-mysterios-menace.json` (`tools/regress.py spider-man-mysterios-menace`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list spider-man-mysterios-menace`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SMM-1. Logo.** The logo is the title's lettering (SPIDER-MAN and MYSTERIO'S MENACE; in Japan the Japanese subtitle), without the city behind it or the menu under it. The title comes after the intro (skipped with START).

**SMM-2. Select menu.** While playing a level (every level, a lost life included), the second screen shows the Select menu (the level's picture, its objective and password, the suits, upgrades and web containers; in Japan, ステータス with the objective under it), opened on the hidden copy and kept up to date about once a second, as the game draws it.

**SMM-3. Logo elsewhere.** The second screen shows the logo before the first Select menu picture, and again as soon as a level is left: at the title, through the screens between levels (the intro and story pictures, the city map with the next mission) and on the continue screen ("With great power comes great responsibility...") after QUIT GAME or the last life.

**SMM-4. The game's own menus.** The pause menu (START) and the Select menu opened in the game stay on the game screen; the second screen keeps its last picture meanwhile.

## Not covered yet

- The bosses (Hammerhead, Rhino, Electro, Scorpion, Mysterio...) weren't fought: each level's case starts at the level's beginning. A boss's fight is in its level, so the Select menu is expected to stay.
- Japan has its first level only; its other levels are expected to be as the USA's (the same profile, its addresses 0x34 lower).

## Facts for tests

| | |
|---|---|
| Passwords | Entered at the title (PASSWORD); the letters are a 8x4 grid (no vowels). JV37T opens every level with all but one power-up; the city map then picks any level (LEFT, RIGHT, UP, DOWN, START). |
| Health | 0x02003158 (halfword, 0x96 full) and 0x02003164: 1 in both, and one hit loses a life. |

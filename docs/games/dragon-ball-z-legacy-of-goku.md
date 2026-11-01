# Dragon Ball Z: The Legacy of Goku

| | |
|---|---|
| Releases | `ALGE` USA · `ALGP` Europe (English, French, German, Spanish, Italian) |
| Profile | `src/profiles.c`: the `ALGE` entry (`dbz_compose`, the `hud` with `overMap`, a logo mask with the Z drawn again); Europe the `ALGP` variant (its language menu) |
| Tests | `tests/regress/dragon-ball-z-legacy-of-goku.json` (`tools/regress.py dragon-ball-z-legacy-of-goku`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list dragon-ball-z-legacy-of-goku`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**DBZ-1. Logo.** On the title, the menus and in the intro, the second screen shows the logo: DRAGON BALL Z and THE LEGACY OF GOKU from the title screen, without Goku, the FUNimation badge or the copyright lines. The title paints Goku's hair over the Z's lower left (the game never draws the Z without it), so there the Z is drawn again, in the game's own red and black, to the shape of the Dragon Ball Z logo's Z (the owner's SVG of it, fitted onto the game's Z): see Exceptions.

**DBZ-2. The pause menu, always.** Walking about, the second screen is the pause menu (START) as the game draws it: Level, Experience and To Next Level at the top, HP, Ki and Time at the bottom right. It's opened in the background on a silent copy of the game about once a second; your game is never paused. Its list of options (Quests to Exit, with the cursor) is left out: the panel is there, empty, for the HUD (DBZ-3, DBZ-4).

**DBZ-3. The HP and Ki bars.** Walking about, the HP and Ki bars leave the game screen's top left for the top of that empty panel, live, whole in a conversation too (where the game's box covers them).

**DBZ-4. The counter SELECT shows.** The counter the game shows at the top left while SELECT is held (an icon and a count, such as 00/06) is always on the second screen, below the HP and Ki bars, taken with the pause menu: the counter alone, not whatever else is in that corner then (a ki blast). On the game screen, holding SELECT shows it as before.

**DBZ-5. Elsewhere.** In the game's own pause menu and its screens, the second screen keeps the last picture. Conversations and anything else over the field stay on the game screen.

**DBZ-6. One screen.** With one screen, the game plays as it is.

## Exceptions

DBZ-1's rebuilt Z is an exception to the philosophy's "only the game's own graphics": about 730 of the logo's pixels, where Goku's hair covers the Z, aren't the game's. The owner's request (Oct 31, 2026), with the official logo's Z as the reference, so the logo reads as the game's own. Its shape comes from the owner's SVG, fitted onto the game's Z where the hair doesn't cover it (`tools/dbz_fit_z.py`, kept in `tools/dbz-z.json`); its colours are the game's (its Z's red and outline black); `tools/make_logo_masks.py` stores the drawn pixels with the mask (`patchAt` / `patchRgb`).

## Not covered yet

Tried in the USA release from the owner's reports (the title, the pause menu, walking about, charging ki and a conversation on the first island). Fights, other areas, the Europe release beyond its title and the game's ending aren't tried yet; in a situation that keeps the field's display setup they get the same treatment, untried.

## Facts found so far (for sessions)

| What | Where / value (USA) |
|---|---|
| Walking about | DISPCNT `0x1F40`, BG0CNT `0x1C00` |
| Pause menu | DISPCNT `0x1F00`, BG0CNT `0x1C0F`: BG1 the text, BG2 the panels (translucent over BG3), BG3 the title art, the cursor a sprite |
| Options panel | the bottom-left panel: border at x 9 / 150, y 81 / 150; inside x 11-147, y 84-147 |
| Title | mode 4 (DISPCNT `0x0404`) with the subtitle, then mode 0 (`0x0900`, BG3 the picture, BG0 Press Start); A skips the intro |
| HP / Ki bars | four sprites at y 8, x 8-87 (palettes 14, 15, priority 1); tiles 1-16, or 17-32 in a conversation (the portrait takes the first) |
| SELECT's counter | with SELECT held: an icon sprite (palette 15) at 6,12 and a box on BG0 |

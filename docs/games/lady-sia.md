# Lady Sia

| | |
|---|---|
| Releases | `ALDE` USA · `ALDP` Europe (both in English, French, German, Spanish, Italian and Dutch) |
| Profile | `src/profiles.c`: the `ALDE` entry; variant `ALDP` |
| Tests | `tests/regress/lady-sia.json` (`tools/regress.py lady-sia`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list lady-sia`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**LS-1. Logo.** The logo is the title's lettering (Lady Sia and its ™), without the picture of Sia flying in behind it or "Press START" under it.

**LS-2. Status.** While playing a level, the second screen shows the game's pause screen (START), opened on the hidden copy and kept up to date about once a second, as the game draws it: Sia's lives, the gems and the rescued villagers along the top, the level's time under them, and the ring and heart at the bottom. The pause menu's own lines are left out: the rows of "Pause" and of its choice (Return to the game, Options, Quit to the main menu, with the arrows either side) are the pause screen's black. With `map_crop = 0` (gba-ds.cfg), the whole pause screen is shown, those lines too. The ring is taken as it is between its glows, without the white outline the game pulses round it.

**LS-3. Logo elsewhere.** The second screen shows the logo before the first status picture, at the title and in the menus before a game (the language list, the main menu, Play, New Game and the other menus there), on the story text that starts a new game, and again from the title on after Quit to the main menu.

**LS-4. The game's own pause menu.** The pause screen opened in the game stays on the game screen (with its Options); the second screen keeps its last picture meanwhile.

**LS-5. Conversations.** While someone is talking (a line of text along the bottom with their portrait in the corner, "Follow me, Sia!"), the game doesn't open its pause screen, so there's no new status picture: the second screen keeps its last one, never a picture of the level.

**LS-6. The heart leaves the game screen.** The heart in the bottom right of the game screen (Sia's health, which the game shows while she's hurt) isn't drawn there: the second screen's status picture has it. That also leaves it out of the game's own pause screen on the game screen (the same heart, in the same place). With `map = 0` (no second screen pictures), the game screen is as the game draws it.

## Not covered yet

- Only the first level has been tried (USA and Europe); later levels, the screens between levels, losing a life, the game over screen, bosses and the Options menu's own screens are untried. If any of them shows something wrong on the second screen, a state of that moment would show what the game is doing there.

## For the merge

Changelog line (under the open version's What's new):

- Lady Sia (USA and Europe) is supported: in a level, the second screen shows the game's pause screen (lives, gems, rescued villagers, the level's time, the ring and heart) without its menu lines, and the heart (health) leaves the game screen; the logo elsewhere.

Wiki, `wiki/Supported-games.md` (table row and section):

| Lady Sia | `ALDE` `ALDP` | the pause screen without its menu: lives, gems, rescued villagers, the level's time, the ring and heart |

### Lady Sia

The USA and European releases.

In a level, the second screen shows the game's own pause screen, kept up to date about once a second: Sia's lives, the gems and rescued villagers, the level's time, the ring and the heart. The pause menu's lines (Pause and its choice, with the arrows) are left out, so it reads as a status screen (`map_crop = 0` shows them). At the title, in the menus and on the story text, the second screen shows the logo. Pausing in the game, and conversations, keep the last picture on the second screen. The heart (health) isn't drawn on the game screen, since the second screen shows it; that includes the game's own pause screen.

Known issues: only the first level has been tried so far.

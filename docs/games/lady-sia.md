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

**LS-6. The HUD leaves the game screen.** What the second screen's status picture shows isn't drawn on the game screen when the game puts it up during play: the lives, gems and rescued villagers along the top and the ring in the bottom left (shown for a few seconds as a level starts, and as Sia picks something up), and the heart in the bottom right (Sia's health, shown while she's hurt or picks up a heart, and as a level starts). They're found by where the game puts each piece, in every level whatever its colours; anything else on the game screen stays, the menus before a game and a conversation's text and portrait included. The game's own pause screen on the game screen draws them in the same places, so it loses them too: there it shows only the level's time, Pause and its choice, while the second screen still has the rest. With `map = 0` (no second screen pictures), the game screen is as the game draws it.

## Not covered yet

- A sprite of the level whose corner lands exactly where a HUD piece's does (a few pixels' leeway, for the ring's bob and a second digit) is hidden while it's there; none was seen in the levels tried.
- Only the first two levels have been tried (the first in Europe); later levels, the screens between levels, losing a life, the game over screen, bosses and the Options menu's own screens are untried. If any of them shows something wrong on the second screen, a state of that moment would show what the game is doing there.

## For the merge

Changelog line (under the open version's What's new):

- Lady Sia (USA and Europe) is supported: in a level, the second screen shows the game's pause screen (lives, gems, rescued villagers, the level's time, the ring and heart) without its menu lines, and the HUD the game puts up during play (lives, gems, villagers, ring, heart) leaves the game screen; the logo elsewhere.

Wiki, `wiki/Supported-games.md` (table row and section):

| Lady Sia | `ALDE` `ALDP` | the pause screen without its menu: lives, gems, rescued villagers, the level's time, the ring and heart |

### Lady Sia

The USA and European releases.

In a level, the second screen shows the game's own pause screen, kept up to date about once a second: Sia's lives, the gems and rescued villagers, the level's time, the ring and the heart. The pause menu's lines (Pause and its choice, with the arrows) are left out, so it reads as a status screen (`map_crop = 0` shows them). At the title, in the menus and on the story text, the second screen shows the logo. Pausing in the game, and conversations, keep the last picture on the second screen. The lives, gems, villagers, ring and heart the game puts up during play (as a level starts, or the heart while Sia is hurt) aren't drawn on the game screen, since the second screen shows them; the game's own pause screen loses them too, keeping only the time and its menu.

Known issues: only the first level has been tried so far.

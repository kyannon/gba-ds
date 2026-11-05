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

**LS-2. Status.** While playing a level (any of the game's levels: the boss fights and the bird rides too), the second screen shows the game's pause screen (START), opened on the hidden copy and kept up to date about once a second, as the game draws it: Sia's lives, the gems and the rescued villagers along the top, the level's time under them, and the ring and heart at the bottom. The pause menu's own lines are left out: the rows of "Pause" and of its choice (Return to the game, Options, Quit to the main menu, with the arrows either side) are the pause screen's black. With `map_crop = 0` (gba-ds.cfg), the whole pause screen is shown, those lines too. The ring is taken as it is between its glows, without the white outline the game pulses round it.

**LS-3. Logo elsewhere.** The second screen shows the logo before the first status picture and whenever the game isn't in a level: at the title and in the menus before a game (the language list, the main menu, Play, New Game and the other menus there), on the story text and the story scenes between levels, on the maps between levels, in the jail after the last life (its "Continue?" countdown), in the ending's credits, and from the title on after Quit to the main menu.

**LS-4. The game's own pause menu.** The pause screen opened in the game stays on the game screen (with its Options); the second screen keeps its last picture meanwhile.

**LS-5. Conversations.** While someone is talking (a line of text along the bottom with their portrait in the corner), the second screen never shows a picture of the level. Some conversations don't let the pause screen open ("Follow me, Sia!"): there the second screen keeps its last picture. Others do (most of those as a level starts), and there the status picture is taken as usual.

**LS-6. The HUD leaves the game screen.** What the second screen's status picture shows isn't drawn on the game screen when the game puts it up during play: the lives, gems and rescued villagers along the top and the ring in the bottom left (shown for a few seconds as a level starts and after Sia loses a life; the gem and its count as she picks up a gem), and the heart in the bottom right (Sia's health, shown while she's hurt or picks up a heart, and as a level starts). The game puts each piece in the same place in every level, whatever its colours; they're found by that place and by the piece's shape, so anything else on the game screen stays, even where it passes the same place (a pedestal by the heart, a log by the ring), and so do the menus, the story scenes and a conversation's text and portrait. The game's own pause screen on the game screen draws them in the same places, so it loses them too: there it shows only the level's time, Pause and its choice, while the second screen still has the rest. With `map = 0` (no second screen pictures), the game screen is as the game draws it.

## How it was checked

Every level was loaded by holding the level byte (0x03000B50, as tcrf.net/Lady_Sia describes) through a new game's loading, its conversations moved on with A, and played for a few minutes of seeded random input: the status picture is taken in every level; the HUD's pieces were found at the same places in all of them, and nothing else stopped there with a HUD piece's shape. The random play also lost lives, reached the jail and (by way of the game's end glitch) the credits.

## Not covered yet

- In the ending's credits, gems and hearts float across the screen; one that passes exactly where a HUD piece goes, with its shape, isn't drawn for that frame. Elsewhere a thing of a level that stops exactly on a HUD piece's place with its shape would be hidden too; none was seen.
- The spells (water, fire and air, given in the second area) and the bonus levels weren't reached: a new game was used throughout. If the spells add to the HUD or the pause screen, a state of that moment would show what to do.
- The unused level 0x2E (only reachable by changing memory) gets no status pictures.

## For the merge

Changelog line (under the open version's What's new):

- Lady Sia (USA and Europe) is supported: in a level, the second screen shows the game's pause screen (lives, gems, rescued villagers, the level's time, the ring and heart) without its menu lines, and the HUD the game puts up during play (lives, gems, villagers, ring, heart) leaves the game screen; the logo elsewhere.

Wiki, `wiki/Supported-games.md` (table row and section):

| Lady Sia | `ALDE` `ALDP` | the pause screen without its menu: lives, gems, rescued villagers, the level's time, the ring and heart |

### Lady Sia

The USA and European releases.

In a level, the second screen shows the game's own pause screen, kept up to date about once a second: Sia's lives, the gems and rescued villagers, the level's time, the ring and the heart. The pause menu's lines (Pause and its choice, with the arrows) are left out, so it reads as a status screen (`map_crop = 0` shows them). At the title, in the menus, in the story scenes, on the maps between levels, in the jail and in the credits, the second screen shows the logo. Pausing in the game, and conversations, keep the last picture on the second screen. The lives, gems, villagers, ring and heart the game puts up during play (as a level starts, or the heart while Sia is hurt) aren't drawn on the game screen, since the second screen shows them; the game's own pause screen loses them too, keeping only the time and its menu.

Known issues: in the ending's credits, an item floating past a HUD piece's place can vanish for a frame. The spells and the bonus levels haven't been tried.

# Banjo-Kazooie: Grunty's Revenge

| | |
|---|---|
| Releases | `BKZE` USA and Europe (English) · `BKZX` Europe (English, French, German) · `BKZI` Italy (La Vendetta di Grunty) · `BKZS` Spain (La Venganza de Grunty) |
| Dumps | `BKZE` cartridge `dfb5aba3` (tested) · `BKZX` cartridge `fb4f38e2` (tested) · `BKZI` cartridge `67ad698f` (tested) · `BKZS` cartridge `32544634` (tested) |
| Profile | `src/profiles.c`: the `BKZE` entry and `bk_compose`; variants `BKZX`, `BKZI`, `BKZS` |
| Tests | `tests/regress/banjo-kazooie-gruntys-revenge.json` (`tools/regress.py banjo-kazooie-gruntys-revenge`) |
| Status | in testing (branch `game/banjo-kazooie-gruntys-revenge`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list banjo-kazooie-gruntys-revenge`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

## What the game has, and what the second screen is for

The game has no map. Its own status screen is the pause screen (START): Banjo's health (the honeycombs) and the notes along the top, the eggs of each kind, the gold feathers and the Hollow Honeycombs down the left, the Jiggies, Jinjos, the rest of the collected things down the right, and the pause menu (Continue, Totals, Options, Save) in the middle. Its Totals page is the goal list for the world Banjo is in: the world's name, then what's been found there out of what there is (Hollow Honeycombs, notes, Jiggies, Jinjos, Mumbo Totems, the world's own things: Cliff Farm's chicks...). Both are what a two-screen version of the game would keep up beside the play: the second screen shows them, kept up to date, so the player never has to pause to look.

## Rules

**BK-1. Logo.** The logo is the title's lettering (Banjo-Kazooie and its ®, and the subtitle under it, as the release writes it: Grunty's Revenge, La Vendetta di Grunty, La Venganza de Grunty), without the scene behind it or Press Start. Europe's English, French and German release has the English lettering (its title is in the language chosen, and English is the first).

**BK-2. Status, two pages.** While Banjo is about in Spiral Mountain or a world, the second screen shows two pages in turn, five seconds each, opened on the hidden copy and kept up to date about once a second:
- the **pause screen**, as the game draws it, without its menu: the rows of Continue, Totals, Options and Save and the Jiggy beside each are left out (they're the game's choice to make, on the game screen), leaving the health and notes along the top, the left column and the right column;
- the **Totals page** of the world Banjo is in (the game opens Totals on it): its name and what's been found there.

Both are on black: the pause screen's own pieces only, without the level the game darkens behind them, which is the game screen's. As in the game, what hasn't been found yet isn't there (on a new game the pause screen has only the health, and Spiral Mountain's Totals only its name).

**BK-3. Following Banjo.** When Banjo goes into another area (Spiral Mountain to Cliff Farm, one of a world's areas to another), a new status picture is taken at once (GEN-6), so the Totals page is the new world's.

**BK-4. Logo elsewhere, the last pictures in scenes.** The second screen shows the logo before the first status picture and away from play: at the title, on the file screen and in the opening story and its scenes (the game's own story text and the scenes before Bozzeye). Where Banjo is about but the pause screen won't open (a conversation, a scene in play), the second screen keeps its last pictures.

**BK-5. The game's own pause screen.** The pause screen opened in the game, and its pages (Totals, Options, Save), stay on the game screen as the game draws them; the second screen keeps its last pictures meanwhile.

## How it was checked

The owner's two 100% saves (USA, BKZE; Europe, BKZX, played in French) and new games in every release, played from power-on to the first free moment after Bozzeye (USA, Italy, Spain, Europe in German). Spiral Rise, Spiral Mountain and Cliff Farm's first area were reached by walking. The addresses (the same in every release): 0x0200030C is 0 at the title, the file screen and the opening story, non-zero wherever Banjo is about; 0x02000318 is 0xFFFFFFFF while the pause screen or one of its pages is up; 0x02000004 is the area.

## Not covered yet

- The mini-games (the slides, the fishing games, Battle Boats and Snowy Sleds, the credits' slide), the boss fights and Gruntilda's Lair's quiz: not reached (the 100% saves have every boss beaten; the arcade in Spiller's Harbor, which plays the mini-games again, wasn't reached). If the pause screen opens there as in a world, the status pages are taken there too; if it doesn't, the last pictures stay (BK-4). A state in each would say which.
- Mumbo's transformations (mouse, candle, octopus, tank), underwater and the egg aiming view: not tried.
- Back to the title after the credits: expected to show the logo again (the play byte goes back to 0), not seen.
- The HUD the game puts up during play (the health as Banjo is hurt, the notes and Jiggies as he picks them up, the egg list on R) stays on the game screen.

## For the merge

Changelog line (under the open version's What's new):

- **Banjo-Kazooie: Grunty's Revenge** (USA and Europe, Europe's English, French and German release, Italy and Spain): the second screen shows the game's own pause screen as a status screen, without its menu (health, notes, eggs, feathers, Hollow Honeycombs, Jiggies, Jinjos and the rest), and the Totals page of the world Banjo is in, five seconds each, kept up to date about once a second. The logo is the title's lettering, shown at the title, on the file screen and in the opening story.

The games' index (`docs/games/README.md`), table row:

| [Banjo-Kazooie: Grunty's Revenge](banjo-kazooie-gruntys-revenge.md) | BK | `BKZE` + 3 |

Wiki (`wiki/Supported-games.md`), table row:

| Banjo-Kazooie: Grunty's Revenge | `BKZE` `BKZX` `BKZI` `BKZS` (no Japanese release) | the pause screen as a status screen and the world's Totals, in turn | |

Wiki section:

## Banjo-Kazooie: Grunty's Revenge

The USA and Europe release, Europe's English, French and German release, and the Italian and Spanish releases.

- **While you play,** the second screen shows two of the game's own screens in turn, five seconds each: the **pause screen** (START), without its menu, with Banjo's health, the notes, eggs, feathers, Hollow Honeycombs, Jiggies, Jinjos and the rest; and the **Totals** page of the world you're in, with what you've found there out of what there is. They're opened in the background on a silent copy of the game about once a second, and again as soon as you go into another area; your game is never paused. Both are shown on black, without the level behind them.
- Pausing in the game, and conversations and scenes, keep the last pictures on the second screen.
- At the title, on the file screen and in the opening story, the second screen shows the logo: the title's lettering.
- Not tried yet: the mini-games, the boss fights and Mumbo's transformations.

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

The game has no map. Its own status screen is the pause screen (START): Banjo's health (the honeycombs) and the notes along the top, the eggs of each kind, the gold feathers and the Hollow Honeycombs down the left, the Jiggies, Jinjos, the rest of the collected things down the right, and the pause menu (Continue, Totals, Options, Save) in the middle. Its Totals page is the goal list for the world Banjo is in: the world's name, then what's been found there out of what there is (Hollow Honeycombs, notes, Jiggies, Jinjos, Mumbo Totems, the world's own things: Cliff Farm's chicks...). The Totals page and the pause screen's health bar are what a two-screen version of the game would keep up beside the play: the second screen shows them together, kept up to date, so the player never has to pause to look.

## Rules

**BK-1. Logo.** The logo is the title's lettering (Banjo-Kazooie and its ®, and the subtitle under it, as the release writes it: Grunty's Revenge, La Vendetta di Grunty, La Venganza de Grunty), without the scene behind it or Press Start. Europe's English, French and German release has the English lettering (its title is in the language chosen, and English is the first).

**BK-2. Status: the world's Totals with Banjo's health.** While Banjo is about in Spiral Mountain or a world, the second screen shows one picture, opened on the hidden copy and kept up to date about once a second: the **Totals page** of the world Banjo is in (the game opens Totals on it), everything it shows but the world's name, with the pause screen's **health bar** (Banjo's head and his honeycombs) centred at the top where the name was. It's on black: the game's own pieces only, without the level the game darkens behind them, which is the game screen's. As in the game, what hasn't been found yet isn't there (on a new game, Spiral Mountain's Totals has nothing yet, so the health bar is all there is). The owner asked for this one picture in place of the two pages in turn (the pause screen without its menu, then the Totals page) of the first test build: the Totals page is what's left to find, and the health is the one thing from the pause screen worth keeping in view. Two of the game's own windows placed together, the name left out, with nothing redrawn: no exception to the philosophy's rules.

*Jumping:* mid-jump changes nothing on the second screen (the first test build put the logo up for each jump: the byte it took for "Banjo is about", 0x0200030C, is 0 while he's in the air).

**BK-3. Following Banjo.** When Banjo goes into another area (Spiral Mountain to Cliff Farm, one of a world's areas to another), a new status picture is taken at once (GEN-6), so the Totals are the new world's.

**BK-4. Logo elsewhere, the last pictures in scenes.** The second screen shows the logo before the first status picture and away from play: at the title, on the file screen and in the opening story and its scenes (the game's own story text and the scenes before Bozzeye). Where Banjo is about but the pause screen won't open (a conversation, a scene in play), the second screen keeps its last picture. After a scene (Bozzeye teaching a move: Pack Whack...), the pictures go on as before (the first test build stopped taking them there for good: the word it took for "paused", 0x02000318, isn't 0 again after it).

**BK-5. The game's own pause screen.** The pause screen opened in the game, and its pages (Totals, Options, Save), stay on the game screen as the game draws them; the second screen keeps its last picture meanwhile.

## How it was checked

The owner's two 100% saves (USA, BKZE; Europe, BKZX, played in French) and new games in every release, played from power-on to the first free moment after Bozzeye (USA, Italy, Spain, Europe in German). Spiral Rise, Spiral Mountain and Cliff Farm's first area were reached by walking. The addresses (the same in every release but the pause byte), checked on 65 moments of play (walking, jumping, attacking, after the Pack Whack scene, in five places) against 15 paused and 32 at the title and in the story, and frame by frame through jumps and the walk into Cliff Farm: the word at 0x02000188 points into EWRAM (0x02xxxxxx) wherever Banjo is about and is 0 at the title, the file screen and the opening story; 0x0203F4D8 (0x0203F4DC in the Europe, Italy and Spain releases, found with tools/romxref.py and checked in each) is 1 from the frame START is pressed while the pause screen and its pages are up, else 0; 0x02000004 is the area.

## Not covered yet

- The mini-games (the slides, the fishing games, Battle Boats and Snowy Sleds, the credits' slide), the boss fights and Gruntilda's Lair's quiz: not reached (the 100% saves have every boss beaten; the arcade in Spiller's Harbor, which plays the mini-games again, wasn't reached). If the pause screen opens there as in a world, the status picture is taken there too; if it doesn't, the last picture stays (BK-4). A state in each would say which.
- Mumbo's transformations (mouse, candle, octopus, tank), underwater and the egg aiming view: not tried.
- Back to the title after the credits: expected to show the logo again (the pointer at 0x02000188 goes back to 0), not seen.
- The HUD the game puts up during play (the health as Banjo is hurt, the notes and Jiggies as he picks them up, the egg list on R) stays on the game screen.

## For the merge

Changelog line (under the open version's What's new):

- **Banjo-Kazooie: Grunty's Revenge** (USA and Europe, Europe's English, French and German release, Italy and Spain): the second screen shows the Totals of the world Banjo is in (Hollow Honeycombs, notes, Jiggies, Jinjos, Mumbo Totems and the world's own things, found out of what there is) with his health bar above them, kept up to date about once a second. The logo is the title's lettering, shown at the title, on the file screen and in the opening story.

The games' index (`docs/games/README.md`), table row:

| [Banjo-Kazooie: Grunty's Revenge](banjo-kazooie-gruntys-revenge.md) | BK | `BKZE` + 3 |

Wiki (`wiki/Supported-games.md`), table row:

| Banjo-Kazooie: Grunty's Revenge | `BKZE` `BKZX` `BKZI` `BKZS` (no Japanese release) | the world's Totals with Banjo's health | |

Wiki section:

## Banjo-Kazooie: Grunty's Revenge

The USA and Europe release, Europe's English, French and German release, and the Italian and Spanish releases.

- **While you play,** the second screen shows the game's own **Totals** page for the world you're in (what you've found there out of what there is: Hollow Honeycombs, notes, Jiggies, Jinjos, Mumbo Totems and the world's own things), with Banjo's **health bar** from the pause screen at the top in place of the world's name. It's opened in the background on a silent copy of the game about once a second, and again as soon as you go into another area; your game is never paused. It's shown on black, without the level behind it.
- Pausing in the game, and conversations and scenes, keep the last picture on the second screen.
- At the title, on the file screen and in the opening story, the second screen shows the logo: the title's lettering.
- Not tried yet: the mini-games, the boss fights and Mumbo's transformations.

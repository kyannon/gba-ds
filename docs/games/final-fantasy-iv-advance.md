# Final Fantasy IV Advance

| | |
|---|---|
| Releases | `BZ4E` USA · `BZ4P` Europe (English, French, German, Spanish, Italian) · `BZ4J` Japan |
| Dumps | `BZ4E` cartridge `feeebde4` (tested) · `BZ4P` cartridge `27f2bc67` (tested) · `BZ4J` cartridge `17036e02` (tested), Rev 1 `80f1133b` (tested), Rev 1, Virtual Console `a68de435` |
| Profile | `src/profiles.c`: the `BZ4E` entry (`ff4_compose`); variants `BZ4P`, `BZ4J` |
| Tests | `tests/regress/final-fantasy-iv-advance.json` (`tools/regress.py final-fantasy-iv-advance`) |
| Status | in progress (branch `game/final-fantasy-iv-advance`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-iv-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why this screen

Asked what the game would have put on a second GBA screen: Final Fantasy IV Advance has no map screen and no status display outside its menus, and the one thing a player keeps opening START's menu for is the party at a glance: every character's level, class, HP and MP, the place, the play time and the gil. That is the game's own screen, so it's opened on the hidden copy and taken as it is. Of the game's two layers of that menu, the command list (Items, Magic, Equipment...) is only there to act on, so it's left out (Philosophy, 2: nothing there that looks like it's waiting for a button), and the party window the game draws beneath it fills its place, as in Final Fantasy I & II: Dawn of Souls. Battles keep the party from just before, as there.

The game has no map the second screen could show; making one would be drawing something the game doesn't have (Philosophy, 3 and 4).

## Rules

**FF4-1. Logo.** The logo is the title's art and lettering: the FINAL FANTASY IV lettering, the ADVANCE bar under it and the knight above them (all one layer, BG2), without PUSH ANY BUTTON, the copyright line or the title's menu. In Europe, taken after the language screen on a first start; in Japan, without the katakana line under the lettering. The same for both Japanese revisions.

**FF4-2. The title.** On the title screen and its menu (New Game, Load Game, Extra), the second screen shows the logo, any picture from a game played before is dropped, and no captures are tried. Told by the display's video mode being 1 (the title; the game itself uses modes 0 and 2).

**FF4-3. Walking about: the party screen.** In the game (the world map, towns, castles, dungeons), the second screen shows START's menu as a party screen: each character's portrait, name, level, class, HP and MP (up to five on the Moon), the place, and the play time and gil, in the game's own window colour (Config's setting), without the command list at the top right (its place is the party window's own background) and without the hand cursor. It's taken on the hidden copy: START, then BG0 and the sprites alone with the hand hidden. It's taken again about once a second while the menu can be opened, so HP, the place and the play time follow the game, and again when the screen comes back from black (a new map, the end of a battle).

**FF4-4. Battles.** From the first frame of a battle to the end of its results the second screen keeps the picture it had (the party from before the battle: its HP, MP and play time don't change during it), told by the battle's own window frames at the bottom of the screen (no menu opens in a battle). With no picture yet (a state loaded straight into a battle), the logo stays. After the battle the party screen is taken again.

**FF4-5. Conversations, shops and scenes.** Where START's menu can't be opened (conversations, a shop, an inn, scenes the story plays, the Load Game screen), the last picture stays; before the first, the logo. (The capture tries START up to four times, B between them, and gives up.)

**FF4-6. The menu open in the game.** With START's menu, or one of its screens (Items, Magic, Equipment, Status, Order, Config...), open in the game, the second screen shows the same party screen as walking about: the hidden copy closes them first (B, as often as it takes).

**FF4-7. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- **Game over**: after a defeat the last party screen stays until the game's title, which has the logo. (Dawn of Souls shows the logo from the moment the party falls: ask the owner if wanted here too.)
- **Bosses, the airship and other vehicles, the Lunar Subterrane's rooms**: the same START menu works there; only some are in the cases.
- **Japan, Rev 1**: the same profile (`BZ4J`); the cases check its logo and party screen.

## How the states were made

Europe's battle (a Gold Dragon in the Lunar Subterrane) was reached by walking the corridor away from the save pad in the harness: the pad's own room has no encounters.

All in the harness from a title state made in the app (the game's battery save goes in the state): the game's Load Game, then the data slot. USA: the GameFAQs saves "AK FF4 Saves Volume I" (slot 1 near the start on the world map, 2 characters; slot 2 in the Lunar Subterrane, 5 characters, in the green window colour). Europe: a GameFAQs save in the Lunar Subterrane B7 at level 99. Japan: a save at the start. The saves work in every release: the Europe one loaded in the Japanese ROM gives the Japanese menu late in the game. (The two `.duc` files in the owner's upload are Nintendo DS saves, for Final Fantasy IV DS: not used.)

## For the merge

Changelog line (under the open version): *Added Final Fantasy IV Advance (USA, Europe, Japan): the second screen shows START's party screen (everyone's level, HP and MP, the place, play time and gil) and keeps it through battles.*

Wiki (`Supported-games.md`): table row and a section, "Final Fantasy IV Advance": the second screen is the party screen from START's menu, without the command list, taken again when you enter a new area or leave a battle; in battles and conversations the last one stays; the title shows the game's logo. Known issues: after a game over the last party screen stays until the title.

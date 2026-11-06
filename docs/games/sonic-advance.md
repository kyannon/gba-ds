# Sonic Advance

| | |
|---|---|
| Releases | `ASOE` USA · `ASOP` Europe · `ASOJ` Japan (Rev 1): the same sprites, registers and title steps |
| Dumps | `ASOE` cartridge `63f70fd8` (tested) · `ASOP` cartridge `6232839b` (tested) · `ASOJ` cartridge `5f512223`, Rev 1 `85957a24` (tested), Rev 1, Virtual Console `431e55ac` |
| Profile | `src/profiles.c`: the `ASOE` entry (logo-only, with `hud` and `hud.card`) |
| Tests | `tests/regress/sonic-advance.json` (`tools/regress.py sonic-advance`) |
| Status | supported (tested on the device for v1.4.4) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SA-1. Logo.** On the title and menus (character select and the course select too), the second screen shows the logo (the title's SONIC ADVANCE, without the copyright line or Sonic behind it).

**SA-2. The zone card.** As a level starts, the game shows its card on the game screen (the coloured stripe with the character's name, the zone's name scrolling along a white band, the act). The second screen keeps that card, as the game draws it, on black, without the level behind it, put together from two moments: the zone's name from the frame where it's most nearly centred across the band, and the character's name in the stripe (KNUCKLES with THE ECHIDNA beside it...) from the frame where it's whole and most nearly centred top to bottom. The game scrolls the two separately, and the zone's name is often best centred as the card leaves, when the character's name is already sliding away (until v1.4.8 the card was that one frame, so a long name such as Knuckles' could be cut off at the top: issue #15); it stays there through the level and the act's results, until the next level's card. While a card is up the game screen shows it as usual, and the second screen keeps the last level's card (the logo the first time) until the new one is taken. With no card seen yet (a state loaded in a level), the logo instead, below the HUD.

**SA-3. The HUD.** In a level, the rings, score and time leave the game screen for the top right of the second screen, over the card, with the lives counter just below them. The act's results (GOT THROUGH ACT, the bonuses) stay on the game screen.

**SA-4. One screen.** With one screen, the game plays as it is.

**SA-5. Pause and bosses.** The pause screen (PAUSE) stays on the game screen; the second screen keeps the card and the moved HUD. In a boss fight (at the end of a zone's act 2) the second screen keeps the act's card and the moved HUD as in the rest of the act.

**SA-6. Every character and zone.** The card and the moved HUD are the same with Sonic, Tails, Knuckles and Amy (the stripe in the character's colour, the character's name in it), and in every zone. In Secret Base Zone, where the game darkens the level outside its searchlights, the card and the HUD on the second screen are darkened with it, and light up with the level later on, when the searchlights go: the effect mirrored on the second screen, as intended.

## Not covered yet

Tried in Neo Green Hill Zone (both acts, Sonic) from the owner's USA states, and in the demo in each release. In the European release also: every zone's first act (Neo Green Hill, Secret Base, Casino Paradise, Ice Mountain, Angel Island, Egg Rocket), Cosmic Angel's act 2, the X Zone, Neo Green Hill act 1's results and act 2's boss, pause, and Neo Green Hill act 1 with Tails, Knuckles and Amy; the course select. Not tried yet: special stages, the Moon Zone, Time Attack, VS and the Tiny Chao Garden: in a level that keeps the same display setup they get the same treatment, untried.

## Facts found so far (for sessions)

| What | Where / value (all three releases) |
|---|---|
| In a level | DISPCNT mode 0 with BG1-3 (`0x1E40`; `0x3E40` with the card's window); the title `0x1541`, menus `0x7240` / `0x3240` |
| Zone card | sprites: the stripe and the vertical name in palette 15 (the name from tile 64; the stripe's tiles move: 400/416 in a level, 452/468 in the demo), the band, scrolling name (rows 128-143), Japanese name (row 114) and act (row 144) in palette 6 from tile 400 |
| HUD | sprites in palette 6, tiles 64-127: the ring icon (68) at -2,-1, the score and time from 9,5 to 81,27; the lives: the icon (palette 0, tile 64) at 2,144, the count (palette 6) at 22,143. The act's results use the same tiles mid-screen |
| The character | palette 0, tile 0 |
| RAM (Europe) | from the SAT-R/sa1 decompilation (it builds the European ROM) and its literal pools: gCurrentLevel 0x0300514C (zone * 2 + act; 12 the X Zone), gSelectedCharacter 0x03005084, gGameMode 0x0300505C, gPlayer 0x03005A20 (+0 x, +4 y, 24.8), gCamera 0x030059D0 (x, y: halfwords), gRingCount 0x03004FEC, gNumLives 0x03005024, gCourseTime 0x03005034, the save (gLoadedSaveGame, a struct) 0x03005160: +8 unlockedLevels (4 halfwords) |
| Reaching places | with unlockedLevels 13 written on the title, GAME START and a character open the course select (DOWN steps act by act). The goal: interactable 0 in the act's list in EWRAM (7-byte entries; a column of them across the goal, Neo Green Hill act 1's at x 12184); putting the player and the camera 120 pixels before it and holding RIGHT runs into it. Act 2's boss: put 700 pixels before that column (Neo Green Hill's at 10864) and hold RIGHT |
| Boot | a language menu (A: English) on a game with no save; START skips the intro; the title waits for START, then plays a demo of Neo Green Hill (with its card and DEMO label) |

## For the merge

Changelog line (under the open version's Changes):

- **Sonic Advance:** tried in every zone, with every character, in the act's results, pause and the bosses (the European release): no changes needed.

`wiki/Supported-games.md`, the Sonic Advance section's "tried" line and its not-tried list: tried in every zone and with every character; not tried yet: special stages, the Moon Zone, Time Attack, VS and the Tiny Chao Garden.

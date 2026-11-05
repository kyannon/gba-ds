# Sonic Advance 3

| | |
|---|---|
| Releases | `B3SE` USA · `B3SP` Europe · `B3SJ` Japan: the same sprites, registers, RAM and title steps (the Japanese title has its own katakana logo, its cards their names in katakana) |
| Dumps | `B3SE` cartridge `49dda5e6` (tested) · `B3SP` cartridge `5bf83456` (tested) · `B3SJ` cartridge `4375f1d6` (tested), Virtual Console `1f36892f` |
| Profile | `src/profiles.c`: the `B3SE` entry (logo-only, with `hud` and a whole-screen `hud.card`) |
| Tests | `tests/regress/sonic-advance-3.json` (`tools/regress.py sonic-advance-3`) |
| Status | in testing (branch `game/sonic-advance-3`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-advance-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The model is Sonic Advance 2: the game's own act card kept on the second screen, the HUD moved over it. Sonic Advance 3's card is plainer (a white band with the zone's name across a black screen) and the game draws its HUD over the card, so the HUD goes to the same places on the second screen as on the card: the second screen reads as the game's card come alive. The card it keeps is the band as it settles at the bottom, with the team and the act, on black.

## Rules

**SA3-1. Logo.** On the language and name screens, the intro, the title, the menus, character select, the story text and the demo, the second screen shows the logo: the title's SONIC ADVANCE 3 (ソニック アドバンス 3 in the Japanese release), without PRESS START, the copyright lines or the green background.

**SA3-2. The act card.** As each act or boss starts, the game shows its card: the screen black, the zone's name (ROUTE 99, SUNSET HILL...) on a white band across the middle, the rings, time and lives over it; then the band slides down to the bottom over the level, the zone's name to the right, with the team above it (SONIC & TAILS) and the act below (ACT 01, VS BOSS). The second screen takes the card as it is once all of that has settled at the bottom (165 frames after the name comes in): the band where it rests, the zone's name, the team and the act as the game draws them there, on black as in the card's first moments (the level behind it darkened away, as the game does then), without the rings, time, lives or characters. It keeps it through the act and the boss. A new card (the next act, or the same one again after a lost life) is taken in turn; the last one stays up until it is. With no card seen yet (a state loaded in an act, or before the first take), the logo instead, centred, on dark grey. (Until this build the second screen took the card's first moments: the name centred on the band, without the team or the act.)

**SA3-3. The rings, time and lives.** Once the card has gone, the rings and time (top left) and the lives (bottom left) leave the game screen for the same places on the second screen, over the card: where the game drew them on its card. The ring count flashing red at no rings flashes there too.

**SA3-4. The hubs.** In a zone's hub (where the acts' rings are) and in the Sonic Factory, there is no card: the second screen shows the logo, centred on dark grey, and the hub's HUD leaves the game screen for the same places there: the icon and key count top left, the lives bottom left, the zone's label (ZONE 1: ROUTE 99) bottom right. The last act's card isn't kept there, as a hub leads on to other zones.

**SA3-5. Pause.** PAUSE stays on the game screen; the second screen keeps the card and the moved HUD.

**SA3-6. One screen.** With one screen, the game plays as it is.

**SA3-7. The act's results.** At the goal the game takes its HUD away and shows the results on the game screen (SONIC & TAILS, ACT 01 CLEAR!!, CLEAR TIME): they stay there. The second screen keeps the card, without the HUD, through the results and the fade to white, until the hub (SA3-4).

## Not covered yet

Tried in every zone's first act (Route 99, Sunset Hill, Ocean Base, Toy Kingdom, Twinkle Snow, Cyber Track, Chaos Angel), Route 99's other acts, its boss and act 1's goal and results back to the hub, Sunset Hill's boss, and the hubs of Route 99, Sunset Hill and Ocean Base, with Sonic and Tails, from states played in from power-on and moved between levels by changing the level number in RAM as an act ring would (all three releases alike: the logo, the card, the hub). Not tried yet: the results' later moments (a special key's, a bonus game's capsule), bonus games after the goal (left out: the logo), special stages, the boss fights' later moments, water (Ocean Base), Altar Emerald and the ending, other teams, Time Attack and multiplayer (left out: the logo), and the Chao Playground. In an act or hub with the same display setup they get the same treatment, untried; elsewhere the logo.

Known: for about 6 frames as an act starts (the screen still black, before the card's name comes in), the HUD is off the game screen.

## Facts found so far (for sessions)

| What | Where / value (USA; the others the same as far as tried) |
|---|---|
| Stage data | `gStageData` at 0x030008A0 (the SAT-R/sa3 decompilation, which builds the European ROM; the same address in all three): +3 game mode (0 the main game, 1 the demo, 3 Time Attack), +9 zone (0-6, 7 the final zone), +0xA act (1 the Sonic Factory, 2 the hub, 3-5 acts 1-3, 7 the boss, 8-9 bonus games), +0xB the entrance, +0xE the level (zone * 10 + act), +0x12 the next level (an act ring's), +0xAC rings, +0xB4 lives. It keeps its values on the title after a reset, so the display is checked too |
| Players | `gPlayers` at 0x030015C0, 0x150 each; +0x10 x, +0x14 y (24.8 fixed point). The camera at 0x03001D10 (x, y; +0x18 / +0x1C its left and right limits) |
| Display | acts and the card `0x7F40` (mode 0, BG0-3, windows 0-1), hubs `0x3E40`, the factory `0x5F40`, bosses `0x7341` (mode 1, BG0-1), bonus games `0x3E40`; the title `0x3341`, the intro `0x1341` / `0x1541` / `0x1441` / `0x7140`, the story text `0x7640` |
| Card | black by BLDCNT (`0x3FEF`, BLDY 16) outside window 1; the band is the backdrop through it. The zone's name: sprites in palette 9, priority 3, from tile 620 (622 in some zones), at y 72 while centred; from about frame 13 of the act to 224. The band then slides down (y 128) with the team (palette 6, tile 728) and ACT 01 / VS BOSS (palette 9, tiles 712-726) |
| HUD | sprites of priority 0: the ring icon (palette 7, tiles 488-496, at 8,0), the ring count (palettes 6-7, tile 448, from 36,2), TIME and the time (palette 6, tiles 448-497, from 88,1); the lives: the faces (palette 7, tiles 472-478) at 5,140, the count (palette 6, tile 452) at 32,140. In a hub: the key (palette 6, tile 484) and count at 11-40,24, and the zone's label (palette 6, from tile 620) at 144,144. PAUSE: palette 15, from tile 620, at 88,72 |
| Boot | a language menu (A: the highlighted language, English or Japanese), name entry (A a letter, START to END, A), SEGA and Sonic Team, the intro, the title about 2500 frames after the name (START skips the intro); MAIN GAME, SINGLE PLAYER, the character and partner (A each), the story text (START skips it), Route 99's hub |
| Goal | the goal ring is interactable 55 in the level's list (decompressed into EWRAM: a word, then the regions across and down (51 and 13 in Route 99 act 1), an offset for each region from the second word, then 8-byte entries: x, y (8-pixel units in the region), the index, 5 bytes of data; 0xFF ends a region). Route 99 act 1's at 12864,1408: putting the player there finishes the act |
| Card's take | at the take, the frame is drawn again with the game's BLDY shadow (`gBldRegs` +4, 0x03002BFC; BLDCNT 0x3FEF there: BG0-3 and the backdrop) at 16, which turns everything but the band (window 0: sprites alone, no effects) black as in the card's first moments; the sprites (not darkened) other than the card's are left out: priority 1-2, the HUD (palettes 6-7 to tile 711), anything at priority 3 not in palette 9, anything at priority 0 not in palettes 6, 7 or 9 |
| Moving between levels | in a hub, touch a ring (the Sonic Factory's, above Route 99's hub's left end: x 50, y 599), and 99 frames later, as the level changes to 1, set the zone, act, entrance (0) and level: the game loads that one instead, with its card |
| Logo | BG1 alone on the title (the copyright lines are sprites) |

## For the merge

Changelog line (under the open version's What's new):

- Sonic Advance 3 (USA, Europe, Japan): each act's card on the second screen (the zone's name on its white band, with the team and the act), with the rings, time and lives moved over it where the game draws them on the card, off the game screen; in the hubs, the logo with the hub's keys, lives and zone label. Tried in every zone's first act, Route 99's results and the hubs; special stages, bonus games, water, Time Attack and multiplayer untried.

`wiki/Supported-games.md`, the table row (after Sonic Advance 2's) and the section (after Sonic Advance 2's):

| Sonic Advance 3 | `B3SE` `B3SP` `B3SJ` | the act card, with the rings, time and lives; the hubs' keys and zone label |

### The wiki section

The USA, European and Japanese releases, tried in every zone's first act and the hubs so far.

- As each act or boss starts, the second screen keeps the game's **act card**: its white band with the zone's name, the team and the act, as it settles at the bottom of the screen, on black.
- In the act, the **rings, time and lives** move from the game screen to the same places on the second screen, over the card.
- In a zone's **hub**, there's no card: the second screen shows the logo, with the hub's **key count, lives and zone label** moved there.
- PAUSE and the act's results stay on the game screen. On the title, menus and the demo, the second screen shows the logo; after loading a state mid-act that wasn't saved with the card, the logo stays (with the moved HUD) until the next act's card.
- Not tried yet: bonus games, special stages, water, other teams, Altar Emerald, Time Attack and multiplayer.

(When merging, this section goes into the wiki under `## Sonic Advance 3` and comes out of this file with the rest of "For the merge".)

Also at the merge: the README's game count, a row in `docs/games/README.md`'s table (`| [Sonic Advance 3](sonic-advance-3.md) | SA3 | `B3SE` + 2 |`), the Status line above ("supported"), `tools/make_dumps.py` again, and the full suite (shared code: `hud.card.tileMax`, `hud.card.drop`, `hud.card.poke` and `hud.card.hide` (the whole card's take drawn again with a value written and sprites left out) in game.c and profiles.h).

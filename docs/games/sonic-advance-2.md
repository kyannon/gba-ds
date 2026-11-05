# Sonic Advance 2

| | |
|---|---|
| Releases | `A2NE` USA · `A2NP` Europe · `A2NJ` Japan: the same sprites, registers and title steps (the Japanese title has its own katakana logo) |
| Dumps | `A2NE` cartridge `7efee7f7` (tested) · `A2NP` cartridge `89509891` (tested) · `A2NJ` cartridge `513804ff` (tested), Virtual Console `6c6c65a3` |
| Profile | `src/profiles.c`: the `A2NE` entry (logo-only, with `hud` and a whole-screen `hud.card`) |
| Tests | `tests/regress/sonic-advance-2.json` (`tools/regress.py sonic-advance-2`) |
| Status | supported (new in v1.4.6) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-advance-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**SA2-1. Logo.** On the title and menus (the language and name screens, the title, the main menu, character select, the course select), the second screen shows the logo: the title's SONIC ADVANCE 2 badge (ソニック アドバンス 2 in the Japanese release), without PRESS START, the copyright lines or the sky behind it.

**SA2-2. The zone card.** As each act or boss starts, with any character, the game fills its screen with the zone's card (the blue strip along the top, the zone's emblem and the row of zone icons, the zone's name on its black band, blue below). Once the card has settled, on the frame its turning emblem shows the zone's symbol face on with its ring of ticks (nearly) full (61 frames after the swirl comes in beside the name), the second screen takes the card as the game draws it, the whole screen, at full brightness, and keeps it through the act, the act's results and the next act's card, until that card is taken in turn. The card still plays on the game screen as usual; the second screen's copy doesn't darken as the game wipes the card off and fades the level in. With no card seen yet (a state loaded in a level), the logo instead, on dark grey, centred, above the HUD along the bottom. Nothing in the level is taken for a card: the bosses' sprites look like the card's to a quick check, and until this build each boss fight put a frozen picture of the fight on the second screen and the HUD back on the game screen; with Cream or Tails the card was never taken.

**SA2-3. The rings, score and time.** In a level, the ring counter (flashing red at no rings too), score and time leave the game screen for the bottom of the second screen, over the card's blue, as far from its bottom edge as they were from the game screen's top (three pixels), at the same distance from the left.

**SA2-4. The lives.** In a level, the lives counter (the character's face and the count) leaves the game screen's bottom left for the second screen's bottom right, over the card's blue: at the same height, as far in from the right edge as it was from the left (six pixels), on the same row as the rings, score and time.

**SA2-5. Pause.** The pause window (PAUSE, CONTINUE, QUIT) stays on the game screen; the second screen keeps the card and the moved HUD.

**SA2-6. One screen.** With one screen, the game plays as it is.

**SA2-7. Water.** With water on the screen, or the character under it, the second screen keeps the card and the moved HUD as in the rest of the level (the game drops one of its layers there; until this build, the second screen went back to the logo and the HUD back to the game screen).

**SA2-8. The act's results.** At the goal, the results (SONIC GOT THROUGH ACT 1, the bonuses) play on the game screen; the second screen keeps the card and the moved HUD (its score counting up with the bonuses) through them, until the next act's card is taken.

**SA2-9. Bosses.** A boss has its card (BOSS ATTACK!), taken as an act's (SA2-2); through the fight the second screen keeps it, with the rings, score, time and lives moved there as in an act (until this build they stayed on the game screen in a boss fight, where the game draws them with other tiles).

## Not covered yet

Tried in Leaf Forest act 1 (Sonic) in all three releases, from the owner's bug reports (the card, play and the title; the water; states saved with the card) and from states played in from power-on (the Europe and Japan titles and cards). Each release is checked alike: the logo, the title, the card with the HUD moved, pause, one screen, the water and a state saved with the card (GEN-7). In the USA release also: every zone's first act (Leaf Forest, Hot Crater, Music Plant, Ice Paradise, Sky Canyon, Techno Base, Egg Utopia) and XX, Leaf Forest's act 2, its results and its boss, with Sonic; Leaf Forest act 1 with Cream, Tails, Knuckles and Amy; the course select (the zone map). Not tried yet: special stages, the other bosses (they use the same sprites' palette, so the same fix), True Area 53, Time Attack, multiplayer and the Tiny Chao Garden; in a level with the same display setup (display mode 0 with BG1-3; BG0 off under water) they get the same treatment, untried; elsewhere (other display setups) the logo.

## Facts found so far (for sessions)

| What | Where / value (USA; the others the same as far as tried) |
|---|---|
| Display | DISPCNT: menus and the title `0x5641` (mode 1), character select `0x3740`, the zone card `0x3F40` (window 0 on), play and pause `0x1F40`; with water on screen `0x1E40` (BG0 off). The language menu is `0x3740` too, name entry `0x5441` / `0x5541`; so a level is mode 0 with BG1-3 on (mask `0x0E07`) |
| Zone card | the whole screen: BG0-3 black shapes over the blue backdrop, sprites for the emblem, the zone icons and the zone's name. The swirl beside the name (tile 464, palette 8) slides in from the right about frame 48 of the card's sequence, rests at 24,71 from frame 60 to 146; the picture is still (bar the emblem's spin) from frame 73. Blue strip rows 0-15, the emblem x 14-58, y 16-53 |
| Emblem | turns on a cycle of about 36 frames; face on, ring full, about 58-62 frames after the swirl first shows |
| After the card | BLDCNT `0x30EF`, BLDY 16 (the level darkened) while window 0 shows the card's panels; then `0x3FCF` with BLDY 16 to 0 (the level fading in). It wipes off (blue panels) and ACT 1 (palette 12 from tile 464, palette 15 from tile 764) flies in over the level; the HUD appears about 90 frames after the card goes |
| HUD | sprites, palette 6 (the ring count palette 7 while it flashes red at no rings), tiles 484-547: the ring box (tile 512, 64x32 at -3,0), the ring icon (544), the digits (484, 504); drawn x 1-146, y 3-29. The lives: the face (palette 0, tile 508) at 6,142, the count (palette 6, tile 488) at 30,140; drawn x 6-36, y 142-155 |
| The character | palette 0, tile 0 |
| HUD in a boss fight | the same pieces from tile 704 (the face 728), the boss's sprites loaded first |
| Card's swirl | the first sprite (OAM 0), palette 8, priority 0, at 24,71: tile 464 with Sonic, Knuckles and Amy, 480 with Cream and Tails (their partners' tiles come first). The bosses (Leaf Forest's Egg Hammer Tank; XX's) use palette 8 from tile 464 at priority 2 |
| RAM (USA) | from the SAT-R/sa2 decompilation's literal pools (its non-matching assembly's labels are the USA ROM's addresses): gCurrentLevel 0x030055B4 (zone * 4 + act; 2 the boss), gGameMode 0x030054CC, gPlayer 0x030059E0 (+8 x, +0xC y, 24.8), gCamera 0x03005960 (x, y), gRingCount 0x030053F0, gNumLives 0x03005448, gBldRegs 0x030018E8; the course time 0x030053E4 (frames; 35950 is a few frames from TIME OVER). gLoadedSaveGame 0x03005B64 points to the save (0x02002658 on the fixtures' game): +7 unlockedLevels[5], +0x13 unlockedCharacters (31: all) |
| Reaching places | with unlockedCharacters 31 and unlockedLevels 28 written on the title, GAME START opens character select with all five and then the course select (RIGHT steps act by act: 1-1, 1-2, 2-1...). The goal: interactable 46 (a column of goal triggers) in the act's list in EWRAM (7-byte entries); putting the player 120 pixels before it, the camera too, and holding RIGHT runs into it. The results then lead into the boss after act 2 |
| Boot | a language menu (A: the highlighted language); with no save, name entry (a letter, then START to END and A); SEGA and Sonic Team, then the title about 900 frames after the name; the logo is BG2 alone |

## For the merge

Changelog lines (under the open version's Fixes):

- **Sonic Advance 2:** with Cream or Tails, the zone card now shows on the second screen (it stayed on the logo). In boss fights, the card and the rings, score, time and lives stay on the second screen (a frozen picture of the fight came up there, and the HUD went back to the game screen). Every zone's first act, XX, the results and Leaf Forest's boss have now been tried, with each character.

`wiki/Supported-games.md`, the Sonic Advance 2 section, in place of the one there:

The USA, European and Japanese releases, tried in every zone and with every character.

- As each act or boss starts, the second screen keeps the game's **zone card** (the zone's name, its emblem and the row of zone icons, on the character's colour and black), the whole screen as the game draws it, once it has settled.
- In the level, the **rings, score and time** move from the top of the game screen to the bottom left of the second screen, over the card's colour, and the **lives** from the bottom left to the card's bottom right. They stay there under water, through the act's results and in boss fights.
- The pause window and the act's results stay on the game screen. On the title, menus and the zone map, the second screen shows the logo; after loading a state mid-act that wasn't saved with the card, the logo stays (above the moved HUD) until the next act's card.
- Not tried yet: special stages, True Area 53, Time Attack, multiplayer and the Tiny Chao Garden.

(The table row stays as it is.)

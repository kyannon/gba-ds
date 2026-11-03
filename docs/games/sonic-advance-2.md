# Sonic Advance 2

| | |
|---|---|
| Releases | `A2NE` USA · `A2NP` Europe · `A2NJ` Japan: the same sprites, registers and title steps (the Japanese title has its own katakana logo) |
| Profile | `src/profiles.c`: the `A2NE` entry (logo-only, with `hud` and a whole-screen `hud.card`) |
| Tests | `tests/regress/sonic-advance-2.json` (`tools/regress.py sonic-advance-2`) |
| Status | preliminary (on its branch, `game/sonic-advance-2`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-advance-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**SA2-1. Logo.** On the title and menus (the language and name screens, the title, the main menu, character select), the second screen shows the logo: the title's SONIC ADVANCE 2 badge (ソニック アドバンス 2 in the Japanese release), without PRESS START, the copyright lines or the sky behind it.

**SA2-2. The zone card.** As each act starts, the game fills its screen with the zone's card (the blue strip along the top, the zone's emblem and the row of zone icons, the zone's name on its black band, blue below). Once the card has settled, on the frame its turning emblem shows the zone's symbol face on with its ring of ticks (nearly) full (61 frames after the swirl comes in beside the name), the second screen takes the card as the game draws it, the whole screen, at full brightness, and keeps it through the act, the act's results and the next act's card, until that card is taken in turn. The card still plays on the game screen as usual; the second screen's copy doesn't darken as the game wipes the card off and fades the level in. With no card seen yet (a state loaded in a level), the logo instead, on dark grey, centred, above the HUD along the bottom.

**SA2-3. The rings, score and time.** In a level, the ring counter (flashing red at no rings too), score and time leave the game screen for the bottom of the second screen, over the card's blue, as far from its bottom edge as they were from the game screen's top (three pixels), at the same distance from the left.

**SA2-4. The lives.** In a level, the lives counter (the character's face and the count) leaves the game screen's bottom left for the second screen's bottom right, over the card's blue: at the same height, as far in from the right edge as it was from the left (six pixels), on the same row as the rings, score and time.

**SA2-5. Pause.** The pause window (PAUSE, CONTINUE, QUIT) stays on the game screen; the second screen keeps the card and the moved HUD.

**SA2-6. One screen.** With one screen, the game plays as it is.

**SA2-7. Water.** With water on the screen, or the character under it, the second screen keeps the card and the moved HUD as in the rest of the level (the game drops one of its layers there; until this build, the second screen went back to the logo and the HUD back to the game screen).

## Not covered yet

Tried in Leaf Forest act 1 (Sonic) from the owner's USA bug reports, its water from a Japanese one, and on the title in each release. Other zones and acts, other characters (Cream, Tails, Knuckles, Amy), bosses, special stages, Time Attack, multiplayer and the Tiny Chao Garden aren't tried yet: in a level with the same display setup (display mode 0 with BG1-3; BG0 off under water) they get the same treatment, untried; elsewhere (other display setups) the logo.

## Facts found so far (for sessions)

| What | Where / value (USA; the others the same as far as tried) |
|---|---|
| Display | DISPCNT: menus and the title `0x5641` (mode 1), character select `0x3740`, the zone card `0x3F40` (window 0 on), play and pause `0x1F40`; with water on screen `0x1E40` (BG0 off). The language menu is `0x3740` too, name entry `0x5441` / `0x5541`; so a level is mode 0 with BG1-3 on (mask `0x0E07`) |
| Zone card | the whole screen: BG0-3 black shapes over the blue backdrop, sprites for the emblem, the zone icons and the zone's name. The swirl beside the name (tile 464, palette 8) slides in from the right about frame 48 of the card's sequence, rests at 24,71 from frame 60 to 146; the picture is still (bar the emblem's spin) from frame 73. Blue strip rows 0-15, the emblem x 14-58, y 16-53 |
| Emblem | turns on a cycle of about 36 frames; face on, ring full, about 58-62 frames after the swirl first shows |
| After the card | BLDCNT `0x30EF`, BLDY 16 (the level darkened) while window 0 shows the card's panels; then `0x3FCF` with BLDY 16 to 0 (the level fading in). It wipes off (blue panels) and ACT 1 (palette 12 from tile 464, palette 15 from tile 764) flies in over the level; the HUD appears about 90 frames after the card goes |
| HUD | sprites, palette 6 (the ring count palette 7 while it flashes red at no rings), tiles 484-547: the ring box (tile 512, 64x32 at -3,0), the ring icon (544), the digits (484, 504); drawn x 1-146, y 3-29. The lives: the face (palette 0, tile 508) at 6,142, the count (palette 6, tile 488) at 30,140; drawn x 6-36, y 142-155 |
| The character | palette 0, tile 0 |
| Boot | a language menu (A: the highlighted language); with no save, name entry (a letter, then START to END and A); SEGA and Sonic Team, then the title about 900 frames after the name; the logo is BG2 alone |

## For the merge

Changelog line (under the open version's What's new):

- Sonic Advance 2 (USA, Europe, Japan): each zone's card on the second screen, with the rings, score and time along its bottom left and the lives in its bottom right, off the game screen; the logo on the title and menus. Tried in Leaf Forest; other zones, characters, bosses and special stages untried.

`wiki/Supported-games.md`, the table row (after Sonic Advance's, or at the end) and the section (after Sonic Advance's):

| Sonic Advance 2 | `A2NE` `A2NP` `A2NJ` | the zone card, with the rings, score, time and lives |

### The wiki section

The USA, European and Japanese releases, tried in Leaf Forest so far.

- As each act starts, the second screen keeps the game's **zone card** (the zone's name, its emblem and the row of zone icons, on blue and black), the whole screen as the game draws it, once it has settled.
- In the level, the **rings, score and time** move from the top of the game screen to the bottom left of the second screen, over the card's blue, and the **lives** from the bottom left to the card's bottom right.
- The pause window stays on the game screen. On the title and menus, the second screen shows the logo; after loading a state mid-act, the logo stays (above the moved HUD) until the next act's card.
- Not tried yet: other zones and characters, bosses, special stages, Time Attack, multiplayer and the Tiny Chao Garden.

(When merging, this section goes into the wiki under `## Sonic Advance 2` and comes out of this file with the rest of "For the merge".)

Also at the merge: the README's game count, a row in `docs/games/README.md`'s table (`| [Sonic Advance 2](sonic-advance-2.md) | SA2 | `A2NE` + 2 |`), the Status line above ("supported"), and the full suite (shared code: `hud.card.whole` in game.c and profiles.h).

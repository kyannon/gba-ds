# Sonic Advance

| | |
|---|---|
| Releases | `ASOE` USA · `ASOP` Europe · `ASOJ` Japan (Rev 1): the same sprites, registers and title steps |
| Profile | `src/profiles.c`: the `ASOE` entry (logo-only, with `hud` and `hud.card`) |
| Tests | `tests/regress/sonic-advance.json` (`tools/regress.py sonic-advance`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SA-1. Logo.** On the title and menus, the second screen shows the logo (the title's SONIC ADVANCE, without the copyright line or Sonic behind it).

**SA-2. The zone card.** As a level starts, the game shows its card on the game screen (the coloured stripe with the character's name, the zone's name scrolling along a white band, the act). The second screen keeps that card, as the game draws it, from the frame where the zone's name is most nearly centred, on black, without the level behind it; it stays there through the level and the act's results, until the next level's card. While a card is up the game screen shows it as usual, and the second screen keeps the last level's card (the logo the first time) until the new one is taken. With no card seen yet (a state loaded in a level), the logo instead, below the HUD.

**SA-3. The HUD.** In a level, the rings, score and time leave the game screen for the top right of the second screen, over the card, with the lives counter just below them. The act's results (GOT THROUGH ACT, the bonuses) stay on the game screen.

**SA-4. One screen.** With one screen, the game plays as it is.

## Not covered yet

Tried in Neo Green Hill Zone (both acts, Sonic) from the owner's USA states, and in the demo in each release. Other zones, other characters, bosses, special stages, the pause screen, Time Attack, VS and the Tiny Chao Garden aren't tried yet: in a level that keeps the same display setup they get the same treatment, untried.

## Facts found so far (for sessions)

| What | Where / value (all three releases) |
|---|---|
| In a level | DISPCNT mode 0 with BG1-3 (`0x1E40`; `0x3E40` with the card's window); the title `0x1541`, menus `0x7240` / `0x3240` |
| Zone card | sprites: the stripe and the vertical name in palette 15 (the name from tile 64; the stripe's tiles move: 400/416 in a level, 452/468 in the demo), the band, scrolling name (rows 128-143), Japanese name (row 114) and act (row 144) in palette 6 from tile 400 |
| HUD | sprites in palette 6, tiles 64-127: the ring icon (68) at -2,-1, the score and time from 9,5 to 81,27; the lives: the icon (palette 0, tile 64) at 2,144, the count (palette 6) at 22,143. The act's results use the same tiles mid-screen |
| The character | palette 0, tile 0 |
| Boot | a language menu (A: English) on a game with no save; START skips the intro; the title waits for START, then plays a demo of Neo Green Hill (with its card and DEMO label) |

# Mario Kart: Super Circuit

| | |
|---|---|
| Releases | `AMKE` USA · `AMKP` Europe · `AMKJ` Japan (Mario Kart Advance) |
| Profile | `src/profiles.c`: the `AMKE` entry (with `AMKP`, `AMKJ`) |
| Tests | `tests/regress/mario-kart-super-circuit.json` (`tools/regress.py mario-kart-super-circuit`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-kart-super-circuit`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-5).

## Rules

**MK-1. HUD moved, not copied.** Each frame is drawn twice: the game screen without the HUD and the second screen with only the HUD, so nothing behind the HUD is lost.

**MK-2. Racing: game screen.** While racing, the game screen shows the track and karts, with the item box (and item) in its top-left corner and the place in its top-right corner; turn-ahead signs stay where the game puts them.

**MK-3. Racing: second screen.** The second screen has the coins, lap and time along the top, the top-four faces centred down the left and the course map with every racer's dot, twice as big (1.5x if it wouldn't fit), in the middle, over a dimmed picture of the course's sky and scenery captured as the race starts.

**MK-4. Results.** After the finish the lap times and the results table are on the second screen.

**MK-5. Podium.** After a cup, CONGRATULATIONS!, the cup name, the portrait, place, rank and cup table are on the second screen on dark grey (until v1.3.2's later test builds over the game's logo, as large as fitted and dimmed: at its own size, GEN-1, the place and cup table couldn't be read over it); the podium, crowd and trophy stay on the game screen.

**MK-6. Pause and menus.** The pause menu stays on the game screen with the last HUD below it; in the menus the second screen shows the logo.

**MK-7. One screen.** With `layout = single` the HUD stays where the game draws it.

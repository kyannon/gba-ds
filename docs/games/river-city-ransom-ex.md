# River City Ransom EX

| | |
|---|---|
| Releases | `BDTE` USA · `BDTJ` Japan (Downtown Nekketsu Monogatari EX) |
| Profile | `src/profiles.c`: the `BDTE` entry (with `BDTJ`) |
| Tests | `tests/regress/river-city-ransom-ex.json` (`tools/regress.py river-city-ransom-ex`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list river-city-ransom-ex`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**RCR-1. Logo.** The logo is the title card's lettering (River City Ransom EX; in Japan Downtown Nekketsu Monogatari EX on its red panel), without the copyright lines under it. The title card comes after a story's opening conversation (skipped with START).

**RCR-2. Status screen.** While a story is being played (on the street, not at the Story / Manage Data menu or the title card), the second screen shows the Status screen (pause, Status), opened on the hidden copy and kept up to date about once a second: the name, stamina bar and money along the top, the character's stats, on solid blue, without the street behind it. Its message box along the bottom (the "status" line and OK / Name) is left black, without its text (with `map_crop` on, the default, as for other games' prompts).

**RCR-3. No HUD on the game screen.** While a story is being played, and in the pause menu and its screens, the bar along the top of the game screen (name, stamina bar, money) isn't drawn: the street shows there instead. The message box along the bottom stays.

**RCR-4. Logo elsewhere.** The second screen shows the logo before the first Status picture (through a story's opening conversation, where START skips the conversation instead of pausing) and again back at the title card or the Story / Manage Data menu.

**RCR-5. One screen.** With `layout = single` the top bar stays where the game draws it.

## Exceptions

- The Status screen's background is a plain blue (one of the colours the game tints the street with behind its pause screens) instead of the tinted street: the owner's choice, so the stats read cleanly.

## Not covered yet

- Shops, restaurants and other places with their own screens, two players, and the game over screen are untried: the top bar is hidden on the game screen through every pause screen and while on the street, judged by the game's screen number (5 to 15).
- The Japanese release has a logo test only; its RAM and menus look the same as the USA release's (checked on one state).
- How quickly the stamina bar on the second screen follows a hit on the device (a capture about once a second).

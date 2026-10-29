# Golden Sun (Ougon no Taiyou: Hirakareshi Fuuin in Japan)

| | |
|---|---|
| Releases | `AGSE` USA, Europe (English) · `AGSD` Germany · `AGSF` France · `AGSI` Italy · `AGSS` Spain · `AGSJ` Japan (the other releases: the same addresses assumed, untested beyond the logo) |
| Profile | `src/profiles.c`: the `AGSE` entry (gs_compose) |
| Tests | `tests/regress/golden-sun.json` (`tools/regress.py golden-sun`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list golden-sun`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**GS-1. Status screen.** Walking about (where SELECT opens the menu), the second screen shows the Status screen (SELECT, the last icon, Status, then A), the whole screen at the game screen's size, as the game draws it (its L-R / A prompt and the hand included). It's opened on the hidden copy at every map refresh, so it stays up to date without the player opening it. With the Status screen open in the game, the same.

**GS-2. Every party member in turn.** The Status screen turns through the party (RIGHT on the hidden copy, as many as there are, back to the first), one every 5 seconds, and keeps turning at that pace through each refresh.

**GS-3. Last one stays.** Where the menu can't be opened (a conversation, a scene, a fight), the last Status pictures stay; before the first, the logo. *By hand: no state of a fight or a conversation yet; check on the device that they keep the last Status pictures.*

**GS-4. Title.** On the title screen the second screen shows the logo, and no captures are tried there.

**GS-5. Logo.** The logo is the title's GOLDEN SUN emblem with its ™ (BG2, taken from power-on, frame 420).

**GS-6. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- The releases other than `AGSE`: only their logos are checked; their Status screen uses the USA release's addresses (`GS_MODE`, `GS_SCREEN`, `GS_TITLE` in profiles.c), which may well be the same. A state from one of them would settle it; if they differ, the checks fail and the logo stays.

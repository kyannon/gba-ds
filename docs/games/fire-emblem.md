# Fire Emblem (Rekka no Ken in Japan)

| | |
|---|---|
| Releases | `AE7E` USA · `AE7X` Europe (En/Fr/De) · `AE7Y` Europe (En/Es/It) · `AE7J` Japan. Addresses per release in FeRegion. |
| Profile | `src/profiles.c`: the `AE7E` entry; variants `AE7X`, `AE7Y`, `AE7J` |
| Tests | `tests/regress/fire-emblem.json` (`tools/regress.py fire-emblem`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list fire-emblem`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-5).

## Rules

**FE-1. Logo.** Whenever the second screen has no unit's status to show (FE-3), it shows the logo (GEN-1): on the title and menus, in story scenes (FE-7), in fights and on the other armies' turns (FE-9), and on the map with the cursor on no unit or with a unit selected. (Until v1.3.2's later test builds: dark navy with the logo small and dimmed in the middle, behind the windows.)

**FE-2.** *Retired in v1.3.2's later test builds:* the terrain and goal windows moved to the second screen, with the START minimap (they stay on the game screen: FE-10; the minimap isn't shown).

**FE-3. Unit status.** With the cursor free on a unit, yours or another army's, the second screen is that unit's status screen (R), as the game draws it, filling the screen, turning through its three pages every 5 seconds; taken again only when the cursor or a unit changes. (Until v1.3.2's later test builds, a unit's status that didn't open while the cursor was still gliding onto it, often an enemy's, wasn't tried again until the cursor moved.) Once a unit is selected (moving it, its menu, the forecast) the logo comes back until the cursor is free again. (Until v1.3.2's later test builds it was at the top left, without its moving background, and the game's small unit window was on neither screen.)

**FE-4.** *Retired in v1.3.2's later test builds:* a unit's menu and the battle forecast moved to the second screen (FE-10).

**FE-5.** *Retired in v1.3.2's later test builds:* the second screen kept what it showed while a unit was selected (FE-3: the logo).

**FE-6.** *Retired in v1.3.2's later test builds:* conversations on the battle map and the tutorial's boxes moved to the second screen (FE-10).

**FE-7. Story scenes.** Story scenes before and after the battle stay on the game screen, and the second screen shows the logo.

**FE-8.** *Retired in v1.3.2's later test builds:* the phase banners moved across the second screen with the game screen dimmed (FE-10).

**FE-9. Fights and other turns.** In a fight and on the other armies' turns, the second screen shows the logo.

**FE-10. Game screen untouched.** Nothing leaves the game screen: the unit, terrain and goal windows, menus, the battle forecast, conversations, the tutorial's boxes and the phase banners stay where the game draws them, as they would on one screen.

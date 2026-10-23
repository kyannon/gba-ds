# Fire Emblem (Rekka no Ken in Japan)

| | |
|---|---|
| Releases | `AE7E` USA · `AE7X` Europe (En/Fr/De) · `AE7Y` Europe (En/Es/It) · `AE7J` Japan. Addresses per release in FeRegion. |
| Profile | `src/profiles.c`: the `AE7E` entry; variants `AE7X`, `AE7Y`, `AE7J` |
| Tests | `tests/regress/fire-emblem.json` (`tools/regress.py fire-emblem`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list fire-emblem`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**FE-1. Background and logo.** Always, the second screen is dark navy with the game's logo, small and dimmed, in the middle (the title and menus too).

**FE-2. Map windows.** On the map, the terrain window moves to the second screen's bottom left and the goal window to its bottom centre; the START minimap is at the bottom right (the chapter's last one when it can't be opened).

**FE-3. Unit status.** With the cursor free on a unit, its status screen (R) is at the second screen's top left without its moving background, turning through its three pages every 5 seconds; taken again only when the cursor or a unit changes. Once a unit is selected it goes. The game's small unit window is on neither screen.

**FE-4. Menus and forecast.** A unit's menu with its hand, and the battle forecast with its arrows and x2, are at the second screen's top centre.

**FE-5. Selected, other menus.** While a unit is selected and in the other menus, the second screen keeps what it showed and those menus stay on the game screen.

**FE-6. Conversations.** Conversations on the battle map (bubble, text, portraits), before or after a fight too, move to the second screen with the portraits on its bottom edge; the tutorial's boxes move as they are; the windows step aside meanwhile. A unit's small speech bubble with no portrait stays on the map.

**FE-7. Story scenes.** Story scenes before and after the battle stay on the game screen, and the second screen goes back to the logo.

**FE-8. Phase banners.** ENEMY PHASE / PLAYER PHASE and their sparkles go across the second screen while the game screen dims; the windows are cleared and come back with control.

**FE-9. Fights and other turns.** In a fight and on the other armies' turns, the second screen is the background with the logo alone.

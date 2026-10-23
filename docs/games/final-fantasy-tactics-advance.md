# Final Fantasy Tactics Advance

| | |
|---|---|
| Releases | `AFXE` USA · `AFXP` Europe (language menu first) · `AFXJ` Japan (each with its own sprite-tagging patch) |
| Profile | `src/profiles.c`: the `AFXE` entry; variants `AFXP`, `AFXJ` |
| Tests | `tests/regress/final-fantasy-tactics-advance.json` (`tools/regress.py final-fantasy-tactics-advance`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-tactics-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**FT-1. Battle overlays move.** In battles and the scenes on the battle maps, what the game draws over the map moves to the second screen: dialogue and portraits, name entry, banners (GET READY!, BATTLE WON!), the unit's details and the command menus.

**FT-2. Scene still behind them.** Behind them is the scene as it looked when it began (the map and units, no menus or tile height), grey and dimmed; it's taken again only after a fade to black or white.

**FT-3. Game screen keeps the battle.** The game screen keeps the map, units, the markers on the map (move and target ranges, the active unit's arrow, the target cursor), the tile height, and spells, effects and damage numbers.

**FT-4. Status stays whole.** A unit's Status screen stays whole on the game screen.

**FT-5. World map.** On the world map, notices, an area's info box, clan funds, the date and the L / R prompts move to the second screen over the map in grey; the game screen keeps the map, Marche and the towns.

**FT-6. Logo elsewhere.** Everywhere else (the title, menus) the second screen shows the logo.

## Not covered yet

Rules no test case checks yet, and the fixture that would cover each:

- **FT-4:** a state with a unit's Status screen open in a battle.

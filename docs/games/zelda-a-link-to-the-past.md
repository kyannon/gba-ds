# The Legend of Zelda: A Link to the Past & Four Swords

| | |
|---|---|
| Releases | `AZLE` USA · `AZLP` Europe (language menu after the title) · `AZLJ` Japan |
| Profile | `src/profiles.c`: the `AZLE` entry; variants `AZLP`, `AZLJ` |
| Tests | `tests/regress/zelda-a-link-to-the-past.json` (`tools/regress.py zelda-a-link-to-the-past`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list zelda-a-link-to-the-past`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-5).

## Rules

**LTTP-1. World map outdoors.** Outdoors, the second screen shows the zoomed-out whole-world map (the one L opens), which doesn't scroll with Link.

**LTTP-2. Dungeon map.** In a dungeon, once Link has its map, the second screen shows the dungeon map.

**LTTP-3. Where L does nothing.** Indoors (houses, caves, dungeons without their map) the last map stays up, dimmed; with no map yet, the logo stays.

**LTTP-4. Logo at the menus.** At the title and file menus, and after Save and Quit, the second screen shows the logo.

**LTTP-5. A Link to the Past only.** The game list shows only A Link to the Past (Four Swords needs linked GBAs), and maps are only captured while A Link to the Past is played. *By hand: Four Swords needs linked GBAs, which the tests can't emulate; check on the device that the game list shows one entry and that Four Swords gets no map.*

**LTTP-6. Still, with every marker showing.** The map never animates, whatever `map_animations` says: everything that blinks on the game's map screen (the floor buttons, Link's and the goal's markers, the room brackets), each at its own rate, is shown in its visible state, all at once and without moving. The X marking where to go, which fades between red and white, is shown at its reddest.

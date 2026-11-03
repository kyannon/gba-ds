# Kingdom Hearts: Chain of Memories

| | |
|---|---|
| Releases | `B8CE` USA (Europe, B8CP, and Japan, B8CJ: not handled yet, see Not covered yet) |
| Profile | `src/profiles.c`: the `B8CE` entry (khc_compose) |
| Tests | `tests/regress/kingdom-hearts-chain-of-memories.json` (`tools/regress.py kingdom-hearts-chain-of-memories`) |
| Status | preliminary (on its own branch, `game/kingdom-hearts-chain-of-memories`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list kingdom-hearts-chain-of-memories`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**KHC-1. World Map.** Walking about in a world (where START opens the menu), the second screen shows the World Map (START, World Map), the whole screen at the game screen's size, as the game draws it once its rooms are all in: the floor's rooms, the hand on the room Sora is in with that room's name at the top right, the floor at the bottom left and the world's emblem. The hand's red A button bubble is left out (the map underneath shows). It's opened on the hidden copy at every map refresh (about once a second), so it keeps up with rooms as they're opened, without the player opening it.

**KHC-2. Last one stays.** Where the menu can't be opened on the hidden copy (a conversation, a scene, the menu or one of its screens already open in the game, the level-up screens after a battle), the last World Map stays; before the first, the logo.

**KHC-3. Battles.** In a battle, from the flash as it starts until the battle screen goes (through its conversations and tutorial boxes, and the LEVEL UP! after the last enemy falls), the second screen shows the logo, idle; no World Map is taken. After the battle the World Map comes back: the last one at once, then a fresh one.

**KHC-4. Title.** On the title screen (told by its picture) the second screen shows the logo, any World Map is forgotten, and no captures are tried there. In the opening and the scenes before the first world the menu doesn't open, so the logo stays (KHC-2).

**KHC-5. Logo.** The logo is the KINGDOM HEARTS CHAIN OF MEMORIES emblem as the game shows it full size on black before the title: once it has faded all the way in, before it flashes and shrinks into the title screen's corner (BG2 alone, frame 700 from power-on).

**KHC-6. Game screen untouched.** Nothing leaves the game screen.

## Facts about the game

- A byte at `0x02034AA4` (USA; `0x02034AC0` Europe, `0x02034A04` Japan) looked like a game mode (0 on the title, 7 walking about, 6 fighting), but it's also 0 on a new game's first Traverse Town screens and 6, 1, 3 through the tutorial battle: not used.
- Battle pointer (`0x02039DD4` USA, `0x0203A3E4` Europe, `0x02039D44` Japan): non-zero (an EWRAM address) while a battle is running, its conversations and tutorial boxes included; 0 elsewhere. It's 0 during the battle's opening flash and once the last enemy falls; the display is in mode 2 then (and through the whole battle, but for conversation boxes, which are mode 1). Rooms, the menu and the World Map are mode 0.
- The menu (START) always opens on Review Decks; World Map is the second entry. Its cursor is `0x020156A9` (USA).
- The World Map draws its frame at once and its rooms over about 70 frames; after that only the hand and the room under it bob.
- The release's ROMs: USA `f7b81e3f` (MD5 prefix), Europe `d369f791`, Japan `90cb614e`.

## Known issues

- Mode 2 outside battles hasn't been seen, but the whole game hasn't been played: if a scene in a world uses it, the second screen shows the logo there (KHC-3's check).
- Castle Oblivion's floors between worlds (the halls with the doors to each world, after the first) haven't been tried: if the menu opens there with World Map in the same place, they show it too; if not, the last one stays.

## Not covered yet

- Europe and Japan: the same rules, with their own addresses (above, found by romxref); not tried in a world yet.

## For the merge

Changelog line (under the open version's "New games"):

- **Kingdom Hearts: Chain of Memories** (USA): walking about in a world, the second screen is the World Map (the menu's World Map, with the hand on the room you're in), updated about once a second. In battles, and on the title and in the opening, it shows the logo, taken from the full-size emblem before the title. Tried in Traverse Town.

`docs/games/README.md` table row:

| [Kingdom Hearts: Chain of Memories](kingdom-hearts-chain-of-memories.md) | KHC | `B8CE` |

Wiki (`wiki/Supported-games.md`) table row:

| Kingdom Hearts: Chain of Memories | `B8CE` `B8CP` `B8CJ` | the World Map; in battles, the logo |

Wiki section:

```
## Kingdom Hearts: Chain of Memories

Tried in the USA release, in Traverse Town.

- **Walking about in a world,** the second screen is the **World Map** (the one in START's menu), as the game draws it, filling the screen: the floor's rooms, the hand on the room you're in with its name, and the floor you're on. It's opened in the background in a silent copy of the game about once a second, so new rooms show up without you opening it; your game is never paused. The hand's A button bubble is left out.
- **In battles,** from the flash as one starts until the last enemy falls, the second screen shows the logo; the World Map comes back afterwards.
- On the title, in the opening and in the scenes before the first world, it shows the logo; in conversations, scenes and the game's own menus it keeps its last picture.
```

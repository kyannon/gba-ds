# Kingdom Hearts: Chain of Memories

| | |
|---|---|
| Releases | `B8CE` USA · `B8CP` Europe (En/Fr/De/Es/It) · `B8CJ` Japan |
| Profile | `src/profiles.c`: the `B8CE` entry (khc_compose); variants `B8CP`, `B8CJ` (their battle pointer and logo, khc_eur / khc_jpn) |
| Tests | `tests/regress/kingdom-hearts-chain-of-memories.json` (`tools/regress.py kingdom-hearts-chain-of-memories`) |
| Status | supported (new in v1.4.8) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list kingdom-hearts-chain-of-memories`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**KHC-1. World Map.** Walking about in a world (where SELECT opens the World Map), the second screen shows the World Map (SELECT; until the later test builds, START's menu, World Map, which in some rooms the checks didn't recognise), the whole screen at the game screen's size, as the game draws it once its rooms are all in: the floor's rooms (the one Sora is in lit blue) with that room's name at the top right, the floor at the bottom left and the world's emblem. The map's cursor (the pointing hand) and its red A button bubble are left out, the map underneath showing. (In the first test builds the hand stayed, and the bubble showed in some rooms.) With the World Map already open in the game, it's taken as it is there (SELECT leaves it up), its cursor left out and its room name the one the player has the cursor on. It's opened on the hidden copy at every map refresh (about once a second), so it keeps up with rooms as they're opened, without the player opening it.

**KHC-2. Last one stays.** Where the World Map can't be opened on the hidden copy (a conversation, a scene, Room Synthesis at a door, the menu or one of its screens open in the game, the level-up screens after a battle), the last World Map stays; before the first, the logo.

**KHC-3. Battles.** In a battle, from the flash as it starts until the battle screen goes (through its conversations and tutorial boxes, and the LEVEL UP! after the last enemy falls), the second screen shows the logo, idle; no World Map is taken. After the battle the World Map comes back: the last one at once, then a fresh one.

**KHC-4. Title.** On the title screen (told by its picture) the second screen shows the logo, any World Map is forgotten, and no captures are tried there. In the opening and the scenes before the first world the World Map doesn't open, so the logo stays (KHC-2).

**KHC-5. Logo.** The logo is the KINGDOM HEARTS CHAIN OF MEMORIES emblem as the game shows it full size on black before the title: once it has faded all the way in, before it flashes and shrinks into the title screen's corner (BG2 alone, frame 700 from power-on).

**KHC-6. Game screen untouched.** Nothing leaves the game screen.

**KHC-7.** *Retired:* Europe and Japan showed the logo only while they were untried in a world; with the owner's states from both they follow KHC-1 to KHC-6 like the USA release. Their logo is the same emblem, taken the same way (KHC-5): in Europe after A on its language menu (English), frame 850 from power-on; in Japan, with no licence screen, frame 500.

## Facts about the game

- A byte at `0x02034AA4` (USA; `0x02034AC0` Europe, `0x02034A04` Japan) looked like a game mode (0 on the title, 7 walking about, 6 fighting), but it's also 0 on a new game's first Traverse Town screens and 6, 1, 3 through the tutorial battle: not used.
- Battle pointer (`0x02039DD4` USA, `0x0203A3E4` Europe, `0x02039D44` Japan, found by romxref and checked in each release's battles): non-zero (an EWRAM address) while a battle is running, its conversations and tutorial boxes included; 0 elsewhere. It's 0 during the battle's opening flash and once the last enemy falls; the display is in mode 2 then (and through the whole battle, but for conversation boxes, which are mode 1). Rooms, the menu and the World Map are mode 0.
- SELECT opens the World Map straight from walking about (START opens the menu, always on Review Decks, World Map second; its cursor `0x020156A9`, USA). The menu's top edge is see-through, so its pixels there take the room's colours: not a reliable check. The World Map's checks hold in all three releases (Japan's floor badge reads F 1).
- The World Map draws its frame at once and its rooms over about 70 frames; after that only the hand and the room under it bob.
- The hand (tiles 396, 400, 401) and its A bubble are the only sprites in palette 0 at priority 0 on the World Map; their tile numbers move with what the game has loaded (the bubble is 454 in one room, 450 in another), so they're hidden by palette and priority.
- The release's ROMs: USA `f7b81e3f` (MD5 prefix), Europe `d369f791`, Japan `90cb614e`.

## Known issues

- Mode 2 outside battles hasn't been seen, but the whole game hasn't been played: if a scene in a world uses it, the second screen shows the logo there (KHC-3's check).
- Castle Oblivion's floors between worlds (the halls with the doors to each world, after the first) haven't been tried: if SELECT opens the World Map there, they show it too; if not, the last one stays.

## Not covered yet

- Nothing yet.

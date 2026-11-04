# Zoids: Legacy (Zoids Saga II in Japan)

| | |
|---|---|
| Releases | `AZ2E` USA · `AZ2J` Japan |
| Profile | `src/profiles.c`: the `AZ2E` entry; variant `AZ2J` (its RAM and routines elsewhere, zl_jpn) |
| Tests | `tests/regress/zoids-legacy.json` (`tools/regress.py zoids-legacy`) |
| Status | in testing (branch `game/zoids-legacy`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list zoids-legacy`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**ZL-1. World map.** Walking about, in a town or out on the world (where SELECT or R opens the map), the second screen shows the game's map screen (SELECT), the whole screen at the game screen's size, as the game draws it: the world map with the spinning marker where Zeru's carrier is, the list of towns visited beside it, and the line under them. The list's cursor (the triangle by the first town) is left out. The marker spins as it does in the game (a short loop of the map's frames). It's opened on the hidden copy at every map refresh (about every 1.5 seconds) and as soon as Zeru goes into a town or back out onto the world, so it keeps up without the player opening it.

**ZL-2. Last one stays.** Where the map can't be opened on the hidden copy (a conversation or a scene, the pause menu or one of its screens, a shop, the map already open in the game), the last map stays; before the first, the logo.

**ZL-3. Battles.** In a battle, from its start until the field comes back (its menus, its attacks, the money and experience after it), the second screen shows the logo, idle; no map is taken. After the battle the map comes back: the last one at once, then a fresh one.

**ZL-4. Title.** On the title screen and its menu (START A GAME, RESUME A GAME, OPTIONS) the second screen shows the logo, any map is forgotten, and no captures are tried. Name entry and the opening scenes don't open the map either, so the logo stays (ZL-2).

**ZL-5. Logo.** The logo is the ZOIDS LEGACY lettering from the title screen (sprites over the title's picture, frame 1300 from power-on; the copyright line and PRESS START below it left out). In Japan it's the ZOIDS SAGA II lettering with ゾイドサーガII under it, taken the same way (frame 1300 there too).

**ZL-6. Game screen untouched.** Nothing leaves the game screen.

## Facts about the game

- A byte at `0x02030664` (USA; `0x0202FDB0` Japan, by romxref) is the game's mode: 0 walking about (and on the title), 1 in a scene or conversation run by the story, 2 in a battle. The byte 3 after it is the place: `0x0C` Cebu Town, `0x1F` the world, 0 in a battle.
- The word at `0x03005B54` (both releases) is the routine running the screen in front: the map screen `0x080ED8F8` (Japan `0x080BAAE0`), the pause menu `0x08105A20` (`0x080EADD8`), the title `0x08102ED0` (`0x080E8330`), a story conversation `0x08359850`; walking about it takes a value that depends on the place and the story (`0x08279EE4` before the first battle, `0x0842558C` after it), so it's only used for the map and the title.
- SELECT and R both open the map, in towns too (both guides say so for the world; the map is the same screen in a town). The map screen is drawn at once; only the marker (two sprites, tiles 936-945, around 93-104, 112-120 on the first map) and the list's cursor (one sprite at 128, 8, tiles 1012-1022) move: both spin, their tiles stepping every two frames. The marker's spin doesn't repeat within 240 frames; the cursor's every 24.
- Zeru's position in a town is a pair of 24.8 fixed-point words at `0x02032758`; the people in it follow in records of 0x48 bytes below it (their spawn points at +8). Moving them next to Zeru and pressing A is how the first states were reached (the town's exit opens once he's talked to enough people).
- In a battle, R (or L) on a Zoid's action menu (Attack, Waiting, Item) opens that Zoid's Status screen (HP, EP, GEP, SP, MM, IV, DF, AM, DCP, SR, the Zoid and its pilot) over the battle; B closes it. A candidate for a later rule (battles showing the acting Zoid's status), not used yet.
- The release's ROMs: USA `220a6f1b` (MD5 prefix), Japan `f7bdd0eb`.

## Known issues

- The Past Overworld (from leaving Seez Town until Riv Town) has no map in the game, so the last map from before it stays up there. A state from there would let it show the logo instead.
- Dungeons (ruins and caves) haven't been tried: if SELECT opens the map there, they follow ZL-1; if not, the last map stays.
- Battles show the logo for now (ZL-3).

## Not covered yet

- The Past Overworld and a dungeon floor (Known issues).

## For the merge

Changelog line (under the open version's What's new):

- Zoids: Legacy (USA) and Zoids Saga II (Japan): the map screen on the second screen while walking about, in towns and on the world, kept up to date without opening it; the logo in battles and scenes.

`wiki/Supported-games.md`, the table row and the section:

| Zoids: Legacy | `AZ2E`, `AZ2J` | the map screen |

### Zoids: Legacy

Walking about, in a town or out on the world, the second screen shows the game's own map screen (the one SELECT or R opens): where Zeru's carrier is, with the towns visited listed beside it. It's opened on the hidden copy every second or so and whenever Zeru enters or leaves a town. In battles and the story's scenes it shows the logo, or keeps the last map. Known issues: the Past Overworld, where the game has no map, keeps the last map from before it; dungeons haven't been tried yet.

# Baldur's Gate: Dark Alliance

| | |
|---|---|
| Releases | `BGDE` USA · `BGDP` Europe (English, French, German, Spanish, Italian; a language list on the title; the same addresses) |
| Dumps | `BGDE` cartridge `deedd7d9` (tested) · `BGDP` cartridge `494492dc` (tested) |
| Profile | `src/profiles.c`: the `BGDE` / `BGDP` entry (`bgd_live_view`, `bgd_worker`) |
| Tests | `tests/regress/baldurs-gate-dark-alliance.json` (`tools/regress.py baldurs-gate-dark-alliance`) |
| Status | supported (new in v1.5) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list baldurs-gate-dark-alliance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The game has no map. The second screen shows one, made from the game's own screens by a copy of the game (BGD-10). Until Nov 13, 2026 it showed the hero's sheet from START's menu instead (BGD-1 to BGD-4, BGD-8, retired).

## Rules

**BGD-1.** *Retired Nov 13, 2026 (the area's map took its place, BGD-10): walking about, START's character tab, taken on the hidden copy.*

**BGD-2.** *Retired Nov 13, 2026: the character list's first seven lines in one picture (until Nov 12's second build, all thirteen in two pages).*

**BGD-3.** *Retired Nov 13, 2026: the list's cursor, scroll arrows and tab marker left out.*

**BGD-4.** *Retired Nov 13, 2026 (now part of BGD-10): where START opened nothing, the last picture stayed.*

**BGD-5. Title.** On the title screen (and the European release's language list on it) and while choosing a class, the second screen shows the logo. A map from before (after Quit Game) is dropped.

**BGD-6. Logo.** The logo is the title's lettering, "Forgotten Realms", "Baldur's Gate" and "Dark Alliance" with its red crescent, on the piece of the title's parchment map behind it (a box round the lettering): the lettering is painted into the map, with translucent edges that no outline can cleanly follow.

**BGD-7. Game screen.** Nothing leaves the game screen: the health and energy bars and the three quick slots stay where the game draws them.

**BGD-8.** *Retired Nov 13, 2026: the character tab taken again at once when health, energy or experience changed.*

**BGD-9. GAME OVER: the logo.** When the hero dies, as the screen goes black for GAME OVER, the second screen drops the map and shows the logo; while the hero falls, the map stays.

**BGD-10. The area's map.** In every area (the tavern, its cellar, the town, the sewers, dungeons, camps), the second screen shows a map of the whole area: its floors, walls, furniture, lamps and raised floors as the game draws them, at a third of their size, on black. It's centred on the middle of the game screen and follows it as the hero walks, but stops at the area's edges: near an edge the map stays put rather than show the black beyond the area's last rooms, and an area smaller than the screen sits in its middle. The game has no map, so GBA DS makes one from the game's own screens: a silent copy of the game, its camera held still and moved over the whole area a screen at a time, draws each part as the game would if the hero stood there, with the game's own tiles and colours. Only the background layers are kept: the hero, enemies, barrels, pots and the HUD (all sprites) aren't on it. Nothing is added: no marker, no names, no frame. The map is whole as soon as it's made, rooms the hero hasn't reached yet too, and is made each time he arrives in an area, once he's been in it under his control for half a second (not from START's menu, whose pictures are on the same layers: a game loaded with the menu open shows the logo until it's closed); the logo until it's ready. It stays up in fights, conversations, scenes in the area and START's menu. Doors opened and things broken after it's made aren't on it until he comes back to the area. *By hand: how long a map takes on the device (here: under a second for the tavern; the device is slower, and it's made beside the game, which never waits for it).*

## Exceptions

- **The areas' maps, made from the game's own screens (BGD-10).** The game has no map, so this goes against three principles:
  - principle 1, because the map is the game's pictures at a third of their size, not GBA pixels;
  - principles 3 and 4, because it's a screen the game never draws, put together by GBA DS.

  It earns its place because the sewers, dungeons and the tavern's own rooms are mazes of look-alike corridors and the game gives you no map, as Mega Man Battle Network's net (MMBN-6). It's kept as close to the game as it can be: only what the game itself draws, each part drawn by the game on a copy of the player's own game, its camera moved by GBA DS but nothing else changed, and nothing new on it. The owner asked for it (Nov 13, 2026) in place of the hero's sheet (BGD-1 to BGD-4, BGD-8, retired).

## Not covered yet

- Only the start of Act I has been played (the Elfsong Tavern); later acts, other classes (the Wizard, the Archer, the Elven Fighter), shops, a level up and the game's own save book haven't been run.
- Only the tavern and its cellar have been mapped; the town, the sewers, later acts and their outdoor areas (maybe much bigger) haven't.
- No marker for the hero: near an area's edge, where the map stops following, he isn't in its middle. A marker (as Mega Man Battle Network's head) needs where he is, which moves about in memory.
- Europe is checked in English; the other four languages share its addresses but haven't been run.

## Facts found so far (for sessions)

The same addresses in both releases.

| What | Where / value |
|---|---|
| In a game | `0x030077D8` (byte): 1 from the start of a new game (the abilities screen) on, through scenes, conversations and the menu; on the title, the European language list and the class choice (and after Quit Game) 0x14 in the USA release, 0x07 in Europe. (`0x0200004C` looked like one in single dumps, but it flips every frame) |
| Under control | `0x03002C02` (byte): 1 walking about (a conversation too: START opens nothing there), 2 in a fight (enemies about; START works), a scripted scene in the world (the guards at the start) and while the hero falls dead, 0 from START on (the menu loading and open), a black-screen scene, GAME OVER (from the frame the screen is black, ~20 frames before the text), the title |
| START's menu | `0x0300456A` (byte): 1 while its picture is up (~32 frames after START, black between), 0 elsewhere. (`0x03001598` is 1 in the menu only sometimes) |
| Its tab | `0x03000AA0` (byte): 0 weapons, 1 armour, 2 potions, 3 the character (yellow emblem), 4 spells and feats, 5 the book, 6 options. The menu always opens on 0. LEFT / RIGHT wrap round, but presses are dropped while it settles; written (in the hidden copy) once the menu is up, the tab is drawn 2 frames later |
| The character list | `0x03000AA1`: the first line shown (0-9; 13 lines, 4 shown), written alike: drawn from it 2 frames later; `0x03000ACB`: the cursor's line within the four shown (0-3) |
| Its sprites | everything on the tab but the background (BG2) is a sprite: each letter and digit an 8x8 one (palette 13); the lines at y 68, 83, 98, 113 (15 apart), the class and level at y 38; the cursor's brackets tile 759 (32 tall, x 0 and 152); the scroll arrows tile 784 / 785 (x 137); the tab marker tile 512 (y 20) |
| The character | `0x03000B50` experience (word), `0x03000B54` health (0 from the hero's death on), `0x03000B56` energy (halves), `0x03000B5E`-`B63` the six abilities (bytes), `0x03000BA9` points to spend: from the attached CodeBreaker codes (the first, `00008751 000A`+`106A3372 0007`, is the enable code) |
| Rooms | mode 0; BG0 floors and walls, BG1 furniture and wall pieces in front, BG2 lamps, upper edges, raised floors (256x256 maps each, scrolled together); the hero, enemies, barrels, pots and the HUD are sprites |
| Camera | `0x030017D0` across, `0x030016B0` down (words, pixels). Each frame the scroll routine (0x086A1600) adds the pending deltas `0x03003538` / `0x03002BF0` (halves) to it, draws the 16-px strips newly shown (one per layer a frame: 16 px a step at most) and clears them. The follow routine (0x08690E60) sets them from the hero (stores at 0x08690FD2, 0x0869108A, 0x0869109C); another routine pushes the camera back toward the hero once he's off screen. Blanking the three stores in a copy and writing the deltas every frame pans it anywhere in the level, drawn correctly (`tools/scout/bgdpan.c`, Nov 13, 2026) |
| Level | header at the pointer in `0x030035B4`; per layer a rectangle in 16-px blocks at header + 2 + 4 × the half at header + 0x10 + 2 × layer: the tavern 73 × 77, the cellar 90 × 89. The camera stops a pixel short of width − 240 / height − 160; past the top or left it shows garbage |
| The hero | his position (fixed point, ~1/170 px) is in an entity whose address changes (`0x0201AC38` / `0x0201AC3C` in some states, nothing there in others); a write he can't walk to (through a table or wall) is undone |
| Title | BG2 a bitmap: the parchment map with the lettering painted in; the lettering in the box x 32-211, y 6-96 (the crescent's tip at y 93) |

## For the merge

- Changelog line (under the open version's What's new): "New game: Baldur's Gate: Dark Alliance (USA, Europe). The game has no map, so GBA DS makes one: a copy of the game draws each area's rooms and corridors with the game's own pictures, and the second screen shows them at a third of their size, following you. The logo on the title and at GAME OVER."
- `wiki/Supported-games.md`, table: `| Baldur's Gate: Dark Alliance | `BGDE` `BGDP` | the area's map (made from the game's own screens) | |`
- `wiki/Supported-games.md`, section:

  ### Baldur's Gate: Dark Alliance

  The USA and European releases.

  The game has no map, so GBA DS makes one. Each time you arrive in an area, a copy of the game, running silently beside yours, looks over the whole area a screen at a time; the second screen then shows the area's floors, walls and furniture at a third of their size, following your view and stopping at the area's edges. Rooms you haven't reached yet are on it too. Enemies, barrels, pots and you yourself aren't. The logo shows on the title, while the map is being made, and at GAME OVER.

  Known issues: there's no marker for where you are (near an area's edge you aren't in the map's middle); doors you open aren't shown open until you come back to the area; only Act I's tavern and cellar have been tried.
- `wiki/Philosophy.md`, its list of exceptions: "Baldur's Gate: Dark Alliance's maps, made from the game's own screens by a copy of the game, as Mega Man Battle Network's net maps."
- `docs/games/README.md`: its row in the table (prefix BGD, `BGDE` + 1).
- README's count of games, one more; `tools/make_dumps.py` for the Dumps row.

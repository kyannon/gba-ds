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

**BGD-5. Title.** On the title screen (and the European release's language list on it), while choosing a class and on the abilities screen of a new game, the second screen shows the logo, whether the game was started new or continued from a save. (A map from before is kept for when its area comes back; another area's is made anew.)

**BGD-6. Logo.** The logo is the title's lettering, "Forgotten Realms" on its plaque, "Baldur's Gate" and "Dark Alliance" with its red crescent, cut out of the title's parchment map along the outline of the game's logo artwork, so none of the map behind or between the letters comes with it; the ™ marks, which the artwork doesn't have, are left out. The same for both releases (the European language list is below the lettering). The lettering is painted into the map with soft edges that no colour can follow, so the outline is the artwork's: the owner's full-size logo, each part (the plaque, "Baldur's Gate", the crescent with "Dark", "Alliance") scaled and placed on the title's letters on its own, as the game draws them a little apart, a pixel in where the artwork covers about a third of it or more, so the letters' dark rims and the crescent's red edge come along. Only the logo's outline is stored in GBA DS (`src/logo_masks.c`, from `tools/logo_stencils/bgd.png`); its pixels come from the game. (Until Nov 15, 2026: a box round the lettering, with the map behind it. The logo is saved as `BGDE-r1.png` / `BGDP-r1.png`, so one cut by an earlier build is made again.)

**BGD-7. Game screen.** Nothing leaves the game screen: the health and energy bars and the three quick slots stay where the game draws them.

**BGD-8.** *Retired Nov 13, 2026: the character tab taken again at once when health, energy or experience changed.*

**BGD-9. GAME OVER: the logo.** When the hero dies, as the screen goes black for GAME OVER, the second screen drops the map and shows the logo; while the hero falls, the map stays.

**BGD-10. The area's map.** In every area (the tavern, its cellar, the town, the sewers, dungeons, camps), the second screen shows a map of the whole area: its floors, walls, furniture, lamps and raised floors as the game draws them, at a quarter of their size, on black (a third until Nov 13, 2026's fourth build: the owner wanted more of the open rooms and the corridors between them at once, fuzzier as that is). It's centred on the hero and follows him as he walks, but stops at the area's edges: near an edge the map stays put rather than show the black beyond the area's last rooms, and an area smaller than the screen sits in its middle. The game has no map, so GBA DS makes one from the game's own screens: a silent copy of the game, its camera held still and moved over the whole area a screen at a time, draws each part as the game would if the hero stood there, with the game's own tiles and colours. Only the background layers are kept: the hero, enemies, barrels, pots and the HUD (all sprites) aren't on it. Where the hero is, a red X marks the spot (BGD-11). Nothing else is added: no names, no frame. The map is whole as soon as it's made, rooms the hero hasn't reached yet too, and is made each time he arrives in an area, once the game has shown the area itself for half a second (walking about, in a fight or a conversation, standing at a save book; not START's menu, whose pictures are on the same layers, nor a black scene: a game loaded with the menu open shows the logo until it's closed); the logo until it's ready. It stays up in fights, conversations, scenes in the area and START's menu. Doors opened and things broken after it's made aren't on it until he comes back to the area. (Until Nov 13, 2026's fourth build: centred on the game screen's middle, 20 pixels above him, with no marker.) *By hand: how long a map takes on the device (here: under half a second for the tavern; the device is slower, and it's made beside the game, which never waits for it).*

**BGD-11. X marks the spot.** The hero's place on the map is marked with the red X the game puts on an unusable quick slot (bottom left of its HUD), at the game's own size (not a quarter): the X alone, with the one-pixel edge of the slot's dark face around it, without the rest of the slot. It's taken from the game itself (the empty slot's picture and its colours, as the cartridge holds them), so it's there whatever the hero's slots hold. Its middle is where his feet are, as the game keeps that place for its camera (which is always on him), on the map at a quarter. It doesn't blink or move but with him. (Until Nov 13, 2026's fifth build: his head, from his sprite as it was when the map was made; hard to read, and other characters standing over him came with it.)

## Exceptions

- **The areas' maps, made from the game's own screens (BGD-10).** The game has no map, so this goes against three principles:
  - principle 1, because the map is the game's pictures at a third of their size, not GBA pixels;
  - principles 3 and 4, because it's a screen the game never draws, put together by GBA DS.

  It earns its place because the sewers, dungeons and the tavern's own rooms are mazes of look-alike corridors and the game gives you no map, as Mega Man Battle Network's net (MMBN-6). It's kept as close to the game as it can be: only what the game itself draws, each part drawn by the game on a copy of the player's own game, its camera moved by GBA DS but nothing else changed, and nothing new on it. The owner asked for it (Nov 13, 2026) in place of the hero's sheet (BGD-1 to BGD-4, BGD-8, retired).
- **The X on the maps (BGD-11).** Principles 2 and 4: something on the second screen standing for the hero, and a picture of the game's used for a purpose the game doesn't give it. The map stops at the areas' edges, so the hero isn't always in its middle; the owner wanted to see where he is (Nov 13, 2026), as on Mega Man Battle Network's maps, and chose the game's own red X from its quick slots for it. It's kept a marker, not a second hero: a still X, at the game's own size, nothing animated or steered.

## Not covered yet

- Every area has been mapped from the owner's endgame save (Nov 14, 2026, the area number poked as Continue loads; see the facts); the owner played the last two areas, the Black Forge and Eldrith's Watch, on the device. Other classes (the Wizard, the Archer, the Elven Fighter), shops and a level up haven't been run.
- Known issue (on the device, Nov 14): making a big area's map takes up to about 2 seconds on the RG DS Plus (about twice the time here) and the game stutters while it's made; the owner finds it acceptable, as it comes as you arrive, like loading. Doors opened after mapping show closed until the area is mapped again.
- Europe is checked in English; the other four languages share its addresses but haven't been run.

## Facts found so far (for sessions)

The same addresses in both releases.

| What | Where / value |
|---|---|
| The title | the display in mode 3 (`DISPCNT` 0x1443 USA, 0x..43 Europe: the title, the language list, the class choice, the abilities screen) without START's menu (`0x03004161` 0x40); START's menu is mode 3 too, with it; play (walking, fights, conversations, a save book) mode 0 (0x1740); black scenes and GAME OVER mode 4 (0x1B44, 0x0044). Not `0x030077D8`: 1 in a game started new, but 0x14 (the title's value) all through a game continued from a save (the owner's endgame save, Nov 14, 2026). Not `0x0200004C`: it flips every frame |
| The area | `0x030001F0` (byte): 1 the tavern, 2 its cellar, 3 the city ... 23 the Black Forge, 25 Eldrith's Watch; 22 areas in all (1-7, 9, 11-15, 17-23, 25, 27; 27 a sky view with no area to map, the logo). Written from the save as Continue is chosen and read some 60 frames later, so poking it in between (harness: `load` a state on the title, `hold A 6`, `wait 10`, `poke 0x030001F0 1 N`) starts the save in area N: how the spot check of Nov 14, 2026 reached every area (all mapped, 0.06-1.05 s here) |
| Under control | `0x03002C02` (byte): 3 standing at a save book ("B SAVE"), 1 walking about (a conversation too: START opens nothing there), 2 in a fight (enemies about; START works), a scripted scene in the world (the guards at the start) and while the hero falls dead, 0 from START on (the menu loading and open), a black-screen scene, GAME OVER (from the frame the screen is black, ~20 frames before the text), the title |
| START's menu | `0x0300456A` (byte): 1 while its picture is up (~32 frames after START, black between), 0 elsewhere. (`0x03001598` is 1 in the menu only sometimes) |
| Its tab | `0x03000AA0` (byte): 0 weapons, 1 armour, 2 potions, 3 the character (yellow emblem), 4 spells and feats, 5 the book, 6 options. The menu always opens on 0. LEFT / RIGHT wrap round, but presses are dropped while it settles; written (in the hidden copy) once the menu is up, the tab is drawn 2 frames later |
| The character list | `0x03000AA1`: the first line shown (0-9; 13 lines, 4 shown), written alike: drawn from it 2 frames later; `0x03000ACB`: the cursor's line within the four shown (0-3) |
| Its sprites | everything on the tab but the background (BG2) is a sprite: each letter and digit an 8x8 one (palette 13); the lines at y 68, 83, 98, 113 (15 apart), the class and level at y 38; the cursor's brackets tile 759 (32 tall, x 0 and 152); the scroll arrows tile 784 / 785 (x 137); the tab marker tile 512 (y 20) |
| The character | `0x03000B50` experience (word), `0x03000B54` health (0 from the hero's death on), `0x03000B56` energy (halves), `0x03000B5E`-`B63` the six abilities (bytes), `0x03000BA9` points to spend: from the attached CodeBreaker codes (the first, `00008751 000A`+`106A3372 0007`, is the enable code) |
| Rooms | mode 0; BG0 floors and walls, BG1 furniture and wall pieces in front, BG2 lamps, upper edges, raised floors (256x256 maps each, scrolled together); the hero, enemies, barrels, pots and the HUD are sprites |
| The hero | `0x03001520` / `0x03001524` (words, 1/256 px): where his feet are in the area; the follow routine keeps the camera 120 px left of and 100 px above it (his feet at 120, 100 on the game screen in every state tried) |
| The red X | the empty quick slot's picture: 16x16, 4 bpp, its four tiles in order, at `0x0844E0CA` (its first and last bytes, transparent corners, differ from what's in VRAM; the rest is as drawn); its palette (OBJ 13) at `0x0860900C`: the X is entry 2 (0x20FF), the slot's dark face entry 15. The same addresses in both releases. The slots are 16x16 sprites at y 142, x 2 / 19 / 36 |
| Camera | `0x030017D0` across, `0x030016B0` down (words, pixels). Each frame the scroll routine (0x086A1600) adds the pending deltas `0x03003538` / `0x03002BF0` (halves) to it, draws the 16-px strips newly shown (one per layer a frame: 16 px a step at most) and clears them. The follow routine (0x08690E60) sets them from the hero (stores at 0x08690FD2, 0x0869108A, 0x0869109C); another routine pushes the camera back toward the hero once he's off screen. Blanking the three stores in a copy and writing the deltas every frame pans it anywhere in the level, drawn correctly (`tools/scout/bgdpan.c`, Nov 13, 2026) |
| Level | header at the pointer in `0x030035B4`; per layer a rectangle in 16-px blocks at header + 2 + 4 × the half at header + 0x10 + 2 × layer: the tavern 73 × 77, the cellar 90 × 89. The camera stops a pixel short of width − 240 / height − 160; past the top or left it shows garbage |
| The hero | his position (fixed point, ~1/170 px) is in an entity whose address changes (`0x0201AC38` / `0x0201AC3C` in some states, nothing there in others); a write he can't walk to (through a table or wall) is undone |
| Title | BG2 a bitmap: the parchment map with the lettering painted in; the lettering in the box x 32-211, y 6-96 (the crescent's tip at y 93) |

# The Legend of Zelda: The Minish Cap

| | |
|---|---|
| Releases | `BZME` USA · `BZMP` Europe (English, French, German, Spanish, Italian; a Language button on the file screen; the same addresses) · `BZMJ` Japan (Zelda no Densetsu: Fushigi no Boushi; the same addresses) |
| Dumps | `BZME` cartridge `abcebbb1` (tested) · `BZMP` cartridge `e8637292` (tested) · `BZMJ` cartridge `6ce771a5` (tested) |
| Profile | `src/profiles.c`: the `BZME` entry (`tmc_compose`); variants `BZMP`, `BZMJ` |
| Tests | `tests/regress/zelda-the-minish-cap.json` (`tools/regress.py zelda-the-minish-cap`) |
| Status | working (tested on the device: USA, Europe and Japan) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list zelda-the-minish-cap`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## What it's for

The game's own Map tab in the START menu (L and R turn between its tabs: Items, Quest Status, Map) is what this game has that a second screen can show without anyone opening it: a map of Hyrule outdoors and, in a dungeon, the dungeon's map. The Items and Quest Status tabs are menus to act on (the cursor, the equipment) and stay in the pause menu.

Checked against the [philosophy](../../wiki/Philosophy.md): the game's own screen, taken from a hidden copy (1, 3, 4); nothing on it to press or to look twice at (2: the page arrows and the "Choose" prompt are left out; the red marks are the map's own *you are here*, see TMC-5); no touch (5); no exception to any principle is asked for (6).

## Rules

**TMC-1. Outdoors and indoors: the map of Hyrule.** Wherever the game's Map tab shows the map of Hyrule (the fields and towns, the Minish Woods, and under a roof too: houses, caves, the Minish world; and the Royal Crypt, which has no dungeon map: the game's Map tab shows the Royal Valley there), the second screen shows it, the whole screen at the game screen's size as the game draws it: Hyrule's regions, the one Link is in marked by the map's red corner marks, and its name in the bar under the map ("Hyrule Town"). Indoors, the marks stay on the region Link last left outdoors, as the game's map does.

**TMC-2. Dungeons: the dungeon's map.** In a dungeon (Deepwood Shrine, the Cave of Flames, the Fortress of Winds, the Temple of Droplets, the Palace of Winds, Dark Hyrule Castle), the second screen shows the dungeon's map as the Map tab draws it: the floor plan of the floor chosen (Link's own: its corner marks are on it in the list of floors), Link's head by that list, the dungeon's keys, compass and map icons below it, the chests and the boss mark on the plan, and the dungeon's name in the bar.

**TMC-3. Opened on the hidden copy, wherever the player left the menu.** The map is taken from a hidden copy of the game (GEN-2): START opens its menu on the Map tab, and the picture is taken once the menu has settled. The menu opens on the tab the player last used, so the copy is first told to open on the Map (the hidden copy's own memory: the game being played isn't touched, and the Items and Quest Status tabs are never taken). Where the game's menu won't open (TMC-8), nothing is taken. Before the map the hidden copy is told to open on Quest Status instead (TMC-4).

**TMC-4. Before the map: Quest Status.** Until Link has the map (the game has no Map tab until then: L and R turn only between Items and Quest Status), the second screen shows the game's Quest Status tab instead (the elements, the sword techniques and the heart pieces collected, as the menu draws it), indoors and in dungeons too, taken the same way as the map (TMC-3) and with the same page arrows left out (TMC-5). It's the one tab of the menu with nothing in it to act on; the Items tab stays in the menu. It follows the game as the map does (TMC-6), and the map takes its place the moment Link has it.

**TMC-5. Nothing to press.** On the map and on Quest Status, left out of the picture: the L and R tab arrows at its sides (they turn the menu's pages) and the "Choose" prompt with its button at the top right (it warps to a Wind Crest); on Quest Status the "Check" prompt's the same, the cursor round an item and the SLEEP and SAVE buttons (the game's own cursor and buttons: nothing here to press), and the name bar at the bottom, which names the item the cursor was left on, is left out as well: what the game draws behind it shows, the whole scroll with its corners. The page holds perfectly still. Everything else of the game's is there: the MAP heading, the bar with the name and the map's own marks (the red corner marks round Link's region, the corner marks round his floor, Link's head, the chests, the boss mark, the dungeon's icons, the blinking markers of kinstone fusions where the game shows them).

**TMC-6. Quick to follow.** When Link crosses into another region of Hyrule, a capture starts as soon as it has held for 3 frames (GEN-6); in a dungeon, when he goes into another room or floor, the same. Going into a house or a cave keeps the marks where they were (the game's own map does the same, see TMC-1); leaving a dungeon takes the map of Hyrule again.

**TMC-7. Blinking as the game does.** The marks on the map blink as in the game (the corner marks round the region, a dungeon's floor marks and chests, each at its own rate), except Link's head: it blinks in the game, but on the second screen it stays on, in the same place, in every frame. The second screen shows a loop of the game's own frames, and, with Map Animations off, one still frame.

**TMC-8. Where the menu won't open.** While the game won't open its menu (a conversation, a cutscene, a door's fade, a boss's entrance), or while Link isn't in play (the game's own menu open, a room being loaded), the last picture stays; before the first, the logo (GEN-3).

**TMC-9. Title, file select, game over and credits: the logo.** On the title, the file select (and in Europe the Language menu), after a game over and in the credits, the second screen shows the logo, and the map of the last game is forgotten (another file is another map): no captures are tried there, and the first one is taken once play begins.

**TMC-10. The logo.** The logo is the title's lettering: "THE LEGEND OF ZELDA" with "The Minish Cap" below it (sprites on the title; the sword's pommel at the lower left is cut off), and in Japan "ZELDA" with "ゼルダの伝説" and "ふしぎのぼうし" and the bird in its hat (ZELDA is a background layer there, the rest sprites), without the forest, the light rays, the ribbon or the "PRESS START" and copyright lines.

**TMC-11. The game screen.** Nothing leaves the game screen: the hearts, rupees and keys, the items and the messages stay where the game draws them.

## Not covered yet

- Japan has no save of its own: its tests run on states made from a new game with the USA endgame's save data copied in (the same addresses), so its first hours are as untested as the others'. Europe and Japan have no Language-menu or game-over state.
- The tests run on states made by warping Link (Hyrule Town, Deepwood Shrine; and, from the owner's Deepwood Shrine and North Hyrule Field states, the Cave of Flames, the Fortress of Winds, the Temple of Droplets, the Palace of Winds and Dark Hyrule Castle with and without their maps, the Royal Crypt, Dark Hyrule Castle's grounds and the stone Hyrule Town, with the story flags of Vaati's takeover set), not on a played game: crossing into another region or room (TMC-6's speed), a house or a cave, and leaving a dungeon are checked by hand on the device.
- Game over and the credits (TMC-9), and the Language menu in Europe, have no state yet.
- The map with the menu refused (TMC-8) is covered by the game's own menu being open and by Ezlo talking in the Minish Woods; a cutscene isn't.
- Dark Hyrule Castle's grounds: the area has the map flag of a dungeon, but on the warped state the game's own Map tab shows the Castle Garden's map of Hyrule (as the second screen does); a played game's grounds should confirm it.
- Europe is checked from the end of the game (the Hyrule Town area and a dungeon); the first hours aren't.
- The Cloud Tops and the Vaati fights haven't been tried. Link as a Minish has only the Minish Woods (a device state), not the Minish world's houses and paths.

## Facts found so far (for sessions)

The same addresses in all three releases (they follow the decompilation's linker script: github.com/zeldaret/tmc `linker.ld`).

| What | Where / value |
|---|---|
| The game's state | `gMain` `0x03001000`: task `+2` (0 title, 1 file select, 2 the game, 3 game over, 4 credits), state `+3` (2 in play), substate `+4` (2 walking about, 7 a menu open, 1 a room being loaded) |
| The pause menu | `gPauseMenuOptions` `0x02034490`: `+1` the tab (1 Items, 2 Quest Status, 4 the map of Hyrule, 5 a dungeon's map; the one last used, kept between openings), `+0x11` the menu's step (2: open and idle; 0, 1 opening, 3-5 changing tab, 6 closing) |
| Which map | `gArea.areaMetadata` `0x02033A90`: bit 0 the overworld, bit 2 a dungeon, bit 3 has a dungeon map. START opens on the map of Hyrule when the tab byte is 4 (a dungeon's when the area has one) |
| The map item | `gSave.inventory` `0x02002A40 + 0xF2 + 17`, bits 6-7: `ITEM_MAP` (2 bits per item). Without it the menu has no Map tab (L and R: Items, Quest Status) |
| Where | `gRoomControls` `0x03000BF0`: area `+4`, room `+5`; `gRoomTransition.player_status` `0x030010AC`: Link's place on the map of Hyrule at `+0x14` / `+0x16` (pixels; its region is `x >> 4`, `y >> 4` in `gOverworldLocations`' 17 boxes), in a dungeon at `+0x10` / `+0x12`; they only update outdoors and in dungeons |
| Warping for tests | poke `gRoomTransition` `0x030010A8` = 1 (leaving), `+9` = 5 (the fade), `0x030010AC` area, `+1` room, `+3` = 3 (instant), `+4` / `+6` x / y (16 bits), `+8` layer 1: the game does the rest. Areas: 0x02 Hyrule Town, 0x48 Deepwood Shrine (room 0x0B) |
| More warps (the `tmc-us-warp-*` states) | area / room / x, y: Cave of Flames 0x50 / 0x03 / 0x88, 0xA8; Fortress of Winds 0x58 / 0x18 / 0x88, 0x22; Temple of Droplets 0x60 / 0x20 / 0x58, 0x38; Palace of Winds 0x70 / 0x15 / 0x218, 0xE8; Dark Hyrule Castle 0x88 / 0x00 / 0x198, 0x1F0; Royal Crypt 0x68 / 0x04 / 0x128, 0x48 (its entrance room's Gibdo holds Link, and the menu won't open while he's held); Dark Hyrule Castle's grounds 0x89 / 0x01; Royal Valley 0x09, Castle Garden 0x07. The map of Hyrule marks where Link last was outdoors, so warp to the area outside first, then in. A few A presses clear Ezlo's words on arrival |
| A dungeon's map item | `gSave.dungeonItems` `0x02002EAC` + the dungeon's number (Deepwood 1, Cave of Flames 2, Fortress 3, Temple 4, Palace 5, Dark Hyrule Castle 6), bit 0. The Royal Crypt's area (0x68) has no map flag: the Map tab shows the map of Hyrule there |
| Vaati's takeover (Dark Hyrule) | `gSave.global_progress` `0x02002A48` (9) is worked out again from flags at each area change: set `gSave.flags` (`0x02002C9C`, bit = bank + flag) `0x02002C9C` = 0x74 (LV1, 3, 4, 5 cleared), `0x02002D0B` bit 0 (bank 3 + 0x78, the stained glass scene) and `0x02002CD0` bit 0 (bank 1 + 0xA0, Vaati takes the king) |
| Quest Status | tab byte 2; shown before `ITEM_MAP` (without it the menu has only Items and Quest Status) |
| Sprites on the Map tab | the L and R arrows (tile 32 at the left, tile 16 at the right, priority 1, around y 56), "Choose" and its button (tiles 256-300, priority 0, top right) |

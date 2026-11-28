# Castlevania: Aria of Sorrow

| | |
|---|---|
| Releases | `A2CE` USA · `A2CP` Europe (language menu first) · `A2CJ` Japan (RAM 0x30 lower) |
| Dumps | `A2CE` cartridge `35536183` (tested), Castlevania Advance Collection `ca9b82dd`, Virtual Console `28baa30e` · `A2CP` cartridge `d0c91f74` (tested), Castlevania Advance Collection `e8156354`, Virtual Console `d97dbfec` · `A2CJ` cartridge `284e3092` (tested), Castlevania Advance Collection `f5c8d8e8`, Virtual Console `93b6599e` |
| Profile | `src/profiles.c`: the `A2CE` entry; variants `A2CP`, `A2CJ` |
| Tests | `tests/regress/castlevania-aria-of-sorrow.json` (`tools/regress.py castlevania-aria-of-sorrow`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list castlevania-aria-of-sorrow`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**CVA-1. Map during play.** While the game is being played, the second screen shows the game's own pause-screen map, captured on the hidden copy, every `map_interval` frames (60: every second) and right after each room transition.

**CVA-2.** *Retired in v1.5's test builds:* the map drawn inside a border built from the game's own menu artwork (the generated map surround). Overlays are now only bezels around the picture, the same for every game; the map is on the screen's background colour like every other game's.

**CVA-3. Last good map stays.** When the map can't be opened (a cutscene, a menu, a fade), the last good map stays up; nothing half-drawn is ever shown.

**CVA-4. Logo before the first map.** Until the first map is captured, and back at the title and file menus, the second screen shows the game's logo.

**CVA-5. Game screen untouched.** The game screen shows the game exactly as it draws itself; nothing is moved or hidden there.

**CVA-6. Widescreen 16:9 (USA).** With GBA DS's widescreen patch "Castlevania - Aria of Sorrow (16-9)" on (docs/WIDESCREEN.md, WS-1; the USA cartridge dump only), the game screen is 284×160: the room's scenery goes on into the margins on both sides as the game would draw it if its screen were wider, scrolling with the room, and the status bar at the top left stays where it is without repeating at the right. Past the room's own left or right end the margins show the background behind the room (the next room isn't there until you walk through the door). This is a CVA-5 exception the player chooses. The patch (`widescreen/A2CE/`, built by `tools/widepatches/A2CE/build.py` from `wide.c`) only changes how the game streams its scenery's tiles, not how it plays:
- the playfield's two layers (BG1, BG2) get maps 512 pixels wide in video memory that's free during play (0x2000-0x3FFF; only the pause screen uses it, and the game redraws the playfield after it), set by the vblank handler after the game's own registers are copied, and only while the game's layers are laid out for play;
- the tiles stream three columns further out on each side (rows 37 tiles instead of 31), each looked up only within the room;
- the status bar's layer (BG0) is 512 wide too, its right half the playfield's old, now empty, map.

Known limitations (for the changelog when it merges): the far background (BG3) is the game's own 256-pixel layer, so where it doesn't repeat by itself it repeats in the margins; enemies and items may only appear once they're within the GBA's own 240 columns; the pause screen and menus are as without the patch; a state saved without the patch shows no scenery until the next room or a pause. Tried in the castle's first rooms only. *Case:* `cva-us-wide-16-9`.

## Not covered yet

- Each release has one moment of play (an early room), its pause screen and the file menu. Boss fights, the castle's later areas (a map scrolled further), warp rooms, and a scene in mid-play (where CVA-3's last good map should stay) haven't been run.
- Julius Mode and Boss Rush haven't been tried.

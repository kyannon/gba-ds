# Kirby & The Amazing Mirror

| | |
|---|---|
| Releases | `B8KE` USA, `B8KP` Europe, `B8KJ` Japan (Rev 1) |
| Dumps | `B8KE` cartridge `9f2a3048` (tested) · `B8KP` cartridge `4f07c618` (tested), Virtual Console `7c74487c` · `B8KJ` Rev 1 `683d772c` (tested), Rev 1, Virtual Console `3da3603b` |
| Profile | `src/profiles.c`: the `B8KE` entry; `B8KP` and `B8KJ` are variants of it |
| Tests | `tests/regress/kirby-and-the-amazing-mirror.json` (`tools/regress.py kirby-and-the-amazing-mirror`) |
| Status | in progress |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list kirby-and-the-amazing-mirror`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game is a maze of rooms over nine areas, with a map of each area on its pause screen, so its second screen is that map, as in the Metroids and the Castlevanias.

## Rules

**KAM-1. Logo.** The logo is the title's lettering: Kirby with its rainbow and & The Amazing Mirror on its ribbon, with its ™ (BG1 above the copyright line, without the landscape behind it or PRESS START).

**KAM-2. In an area: its Area map.** While Kirby is in a room of one of the areas, the second screen shows the pause screen's Area map of that area, opened on the hidden copy (on the Area map page whichever page the player last looked at) and taken again as Kirby goes into another room and about once a second: the rooms Kirby has been through (every room once the area's map, its big chest, has been found), the paths between them, the four Kirbys' marker with its pulsing bar, and the area's name at the top.

**KAM-3. No prompts.** The pause screen's prompts are left out of the map: SELECT SWITCH, R ZOOM and B QUIT along the bottom, the L and R tabs in the top corners once the area's map has been found, and the white arrows at the map's edges that say it scrolls further; the map's own picture and rooms show there instead. With `map_crop = 0` (gba-ds.cfg) the whole pause screen is shown, prompts too.

**KAM-4. The game screen as the game draws it.** The HUD (the copy ability, lives, health, an enemy's health and the area's name along the bottom, the phone's battery at the top left) stays where the game draws it.

**KAM-5. Logo before a map, and the last map where there's none.** The second screen shows the logo at half strength before the first Area map: the title, the file and game select, the opening, and the tutorial at the start (where the pause screen has no map until its chest is found). Where the pause screen has no Area map later (the Dimension Mirror, the rooms outside the areas' maps), the last Area map stays. Back at the title or the file select, the logo comes back.

**KAM-6. The game's own pause screen: the logo.** While the game's own pause screen is open, on whichever page (Help, the Area map or the World map), the second screen shows the logo at half strength, so the same page is never on both screens (as Nightmare in Dream Land's pause, KND-5, and Muppet Pinball Mayhem's, MPM-4). The game's own pause screen is untouched, SELECT and all. When it closes, the last Area map is back at once. The second screen does not fade: as the pause screen opens and closes, and as SELECT turns its pages, the game screen fades to white and back while the second screen changes at once.

## Not covered yet

- Multiplayer (four players over link cables): only one player is checked.
- Cutscenes inside the areas and the boss fights.

## Notes

| | |
|---|---|
| Regions | Europe (`B8KP`) and Japan (`B8KJ`, Rev 1) have the USA release's addresses (checked with `tools/romxref.py` and on the owner's states). Their own states are the title, file select, tutorial and the pause screen's Area and World map pages; the hub and the Area map taken from it are tested in the USA release only |
| Pause page | 0x0203ACCD (gPauseMenus[0].menuId): 1 Help, 2 World map, 4 Area map, in turn Help, Area map, World map (set as SELECT is pressed, the page drawn at the fade's white point); kept from the page last looked at, written to 4 on the hidden copy before START (`openPoke`). In the tutorial, the Dimension Mirror and the rooms outside the areas' maps the game opens Help whatever it holds |
| Area map up | gDispCnt 0x03003690: video mode 1 (`& 7 == 1`) only on the Area map; the map is drawn and still about 54 frames after START |
| Pause open | 0x0203AD4C (u32) non-zero while a pause page is open |
| Fades | the pause screen's opening and closing: to white by its colours, 0x020229A1 from 0 to 0x1F (2 a frame, about 19 frames held; 0xFD once done, and a twin at 0x02022931 for doors); SELECT's pages: BLDCNT 0x00BF, BLDY 0 to 16. gDispCnt's video mode changes inside the white hold, the real DISPCNT 3 frames later |
| Playing | gNumKirbys 0x0203AD44: 1-4 in the game, 0 at the title and in the opening |
| Room | 0x02020F40 (u16): the hub is 0x321, the tutorial 0x323-0x325 |
| Maps found | bigChestField 0x0203897C (u32): bit N, area N's map (1 Rainbow Route ... 9); visited rooms gVisitedDoors, u16[0x120] at 0x02028CA0 |
| Map layers | BG0 the art, BG1 the lettering (the area's name rows 3-14, the prompts rows 145-157), BG2 the rooms and paths, sprites the Kirbys' marker; only a 12x4 bar under the marker pulses (54 frames) |
| Decompilation | jiangzhengwenjz/katam (its linker.ld names the RAM above; the ROM's SHA-1 matches) |

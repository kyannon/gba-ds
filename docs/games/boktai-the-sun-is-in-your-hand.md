# Boktai: The Sun Is in Your Hand (Bokura no Taiyou in Japan)

| | |
|---|---|
| Releases | `U3IE` USA · `U3IP` Europe (English, French, German, Spanish, Italian) · `U3IJ` Japan (the USA release walked about in; Europe and Japan on their logos and the same addresses) |
| Dumps | `U3IE` cartridge `e715ac45` (tested) · `U3IP` cartridge `9686c36b` (tested) · `U3IJ` cartridge `a09c0807` (tested) |
| Profile | `src/profiles.c`: the `U3IE` entry (bt_compose; the Map page of SELECT's menu); variants `U3IP`, `U3IJ` (as `U3IE`) |
| Tests | `tests/regress/boktai-the-sun-is-in-your-hand.json` (`tools/regress.py boktai-the-sun-is-in-your-hand`) |
| Toggle | `none`: the Map is the only picture worth showing, the SELECT menu's other pages being interactive (the owner, Nov 21, 2026) · GEN-14 |
| Status | working (tested on the device, USA; Europe and Japan on their logos) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them. Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused.

Every game also follows the [general rules](README.md#general-rules). The game draws its own Map (SELECT, then the Map page: the area you are in, with an arrow for Django), so by the [Philosophy](../../wiki/Philosophy.md) it is the second screen: a screen the game already draws, opened on the hidden copy and captured.

## Rules

**BT-1. Map.** While Django walks about (no menu, no conversation), the second screen shows the Map page of the game's SELECT menu, the whole screen at the game screen's size, as the game draws it: the area's map with its name below and the arrow for Django (and, at the top, BT-8's lines). It is opened on the hidden copy (SELECT, then L or R to the Map page), about once a second, so the arrow follows him.

**BT-2. No prompts.** The Map's button prompts (its title bar with the L and R tabs at its ends, the A + up / down glyph) and the arrows at its edges that say it scrolls are left out, so the Map shows whole (what lies under the title bar is the Map itself) and nothing on the second screen makes the player look twice.

**BT-3. Last one stays.** Where the menu can't be opened (a conversation, a scene, the area's title card), or with the game's own menu or Map open on the game screen, the last Map stays; before the first, the logo.

**BT-4. Title.** On the title screen, the file menu and the first start's set-up (sensor and clock notes, Konami logos, caution), the second screen shows the logo, and no captures are tried.

**BT-5. Logo.** The logo is the title's lettering (boktai, ボクらの太陽 in Japan) with its banner, cut out of the sun behind it, the tan blends along the yellow outline left out so that the outline is its edge; PUSH START, the copyright line and the sun are left out. The first start asks about the sensor and the clock before the title, so the hidden copy answers those (A through them, START to end).

**BT-6. Game screen untouched.** Nothing leaves the game screen.

**BT-7. The sun.** The cartridge's solar sensor reads a level from 0 (dark) to 10, 6 to begin with (the `solar_level` setting in `gba-ds.cfg`), so the sun gauge isn't empty. This is shared code (the emulator hands every core, the hidden copies included, the same light level). BT-9 changes it while playing.

**BT-9. Sun row in the quick bar.** Where other games have Controls, the quick bar of a game whose cartridge has a sun sensor (Boktai 1, 2 and 3) has a **Sun** row: **Dark** (level 0), **-** and **+** (one level down or up, as RetroArch's Darken and Brighten Solar Sensor buttons, L3 and R3 by default, do), the level between them (0 to 10, not a button) and **Full** (10). A tap on a button changes the sunlight the game reads straight away, so the game's own sun gauge follows and Django's sun-sensitive moments (ice melting, enemies, the Gun del Sol's charge) respond as in RetroArch; it stays between 0 and 10. The level is kept for the game (`states/<game>.sun`), so it is as left the next time, and starts from `solar_level`. The row, its buttons and the level are in the quick bar only (nothing is added to either game screen: GEN-2, GEN-5), and a tap on the row never opens the menu. The bug report's info says the level.

**BT-8. Clock, sunrise and sunset.** With the Map, the second screen has three of the game's own lines of text, as its own digits and letters (just the glyphs and their yellow outline, nothing of the page behind them): the clock at the top left, and SUNRISE and SUNSET with their times stacked at the top right, the sunrise above. They are the lines the ITEM page shows above its health bar (the clock; START on it turns it into the sunrise time, START again into the sunset), cut out on the hidden copy, so GBA DS draws none of it (rule 4). The space around them is equal: the clock is as far from the screen's left edge as the sunrise and sunset lines are from its right edge, and the topmost of them from its top edge (four pixels of the game's screen each); the clock sits on the same line as the sunrise's times. They are looked at about every 30 seconds (the Map's capture itself runs about once a second), and a line the game doesn't give is left out. It is a deliberate exception to the Philosophy's "nothing on both screens" for the moment the ITEM page is open on the game screen (the second screen keeps its last Map and lines then, BT-3).

## Not covered yet

- BT-9 (the Sun row) was tried on the device for the USA release (the owner, Nov 21, 2026); it is to be tried again on Boktai 2 and 3 once they have app support (they have no profile yet, so the row is untried there).
- Europe's and Japan's walking about: they share the USA release's addresses (checked: the Map opens, the logos extract); only their logos have cases until a state from each arrives.
- Bosses, the Solar Station and the World map: not reached; the Map is only taken where SELECT opens it, and BT-3 covers the rest.
- A fixed area key for refreshing the moment the area changes: none found, so BT-1 refreshes about once a second.

## Facts about the game

USA addresses (EWRAM): `0x02001B3A` menu open (0 = playing, 1 = SELECT's menu), `0x020034DC` its page (2 = Map), `0x02001B38` 0 on the title and file menu, 40 in the game. SELECT's menu remembers its last page, so the capture presses L or R until the Map shows. The areas' colours follow the cartridge clock (day roughly 8:00 to 17:59, dusk, night), so the tests pin the clock. The cartridge's light sensor is GPIO; mGBA's levels give the reading 0xFF - (0x16 + level table).

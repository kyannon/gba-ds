# Boktai: The Sun Is in Your Hand (Bokura no Taiyou in Japan)

| | |
|---|---|
| Releases | `U3IE` USA · `U3IP` Europe (English, French, German, Spanish, Italian) · `U3IJ` Japan (the USA release walked about in; Europe and Japan on their logos and the same addresses) |
| Dumps | `U3IE` cartridge `e715ac45` (tested) · `U3IP` cartridge `9686c36b` (tested) · `U3IJ` cartridge `a09c0807` (tested) |
| Profile | `src/profiles.c`: the `U3IE` entry (bt_compose; the Map page of SELECT's menu); variants `U3IP`, `U3IJ` (as `U3IE`) |
| Tests | `tests/regress/boktai-the-sun-is-in-your-hand.json` (`tools/regress.py boktai-the-sun-is-in-your-hand`) |
| Status | in progress (side build) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them. Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused.

Every game also follows the [general rules](README.md#general-rules). The game draws its own Map (SELECT, then the Map page: the area you are in, with an arrow for Django), so by the [Philosophy](../../wiki/Philosophy.md) it is the second screen: a screen the game already draws, opened on the hidden copy and captured.

## Rules

**BT-1. Map.** While Django walks about (no menu, no conversation), the second screen shows the Map page of the game's SELECT menu, the whole screen at the game screen's size, as the game draws it: the area's map with its name below and the arrow for Django (and, at the top, BT-8's lines). It is opened on the hidden copy (SELECT, then L or R to the Map page), about once a second, so the arrow follows him.

**BT-2. No prompts.** The Map's button prompts (its title bar with the L and R tabs at its ends, the A + up / down glyph) and the arrows at its edges that say it scrolls are left out, so the Map shows whole (what lies under the title bar is the Map itself) and nothing on the second screen makes the player look twice.

**BT-3. Last one stays.** Where the menu can't be opened (a conversation, a scene, the area's title card), or with the game's own menu or Map open on the game screen, the last Map stays; before the first, the logo.

**BT-4. Title.** On the title screen, the file menu and the first start's set-up (sensor and clock notes, Konami logos, caution), the second screen shows the logo, and no captures are tried.

**BT-5. Logo.** The logo is the title's lettering (boktai, ボクらの太陽 in Japan) with its banner, cut out of the sun behind it, the tan blends along the yellow outline left out so that the outline is its edge; PUSH START, the copyright line and the sun are left out. The first start asks about the sensor and the clock before the title, so the hidden copy answers those (A through them, START to end).

**BT-6. Game screen untouched.** Nothing leaves the game screen.

**BT-7. The sun.** The cartridge's solar sensor reads a fixed level, 6 of 10 (the `solar_level` setting in `gba-ds.cfg`, 0 to 10; 0 is dark), so the sun gauge isn't empty. This is shared code (the emulator hands the core a light level); the game list has no row for it yet.

**BT-8. Clock, sunrise and sunset.** With the Map, the second screen has three of the game's own lines of text, as its own digits and letters (just the glyphs and their yellow outline, nothing of the page behind them): the clock at the top left, and SUNRISE and SUNSET with their times stacked at the top right, the sunrise above. They are the lines the ITEM page shows above its health bar (the clock; START on it turns it into the sunrise time, START again into the sunset), cut out on the hidden copy, so GBA DS draws none of it (rule 4). The space around them is equal: the clock is as far from the screen's left edge as the sunrise and sunset lines are from its right edge, and the topmost of them from its top edge (four pixels of the game's screen each); the clock sits on the same line as the sunrise's times. They are looked at about every 30 seconds (the Map's capture itself runs about once a second), and a line the game doesn't give is left out. It is a deliberate exception to the Philosophy's "nothing on both screens" for the moment the ITEM page is open on the game screen (the second screen keeps its last Map and lines then, BT-3).

## Not covered yet

- Europe's and Japan's walking about: they share the USA release's addresses (checked: the Map opens, the logos extract); only their logos have cases until a state from each arrives.
- Bosses, the Solar Station and the World map: not reached; the Map is only taken where SELECT opens it, and BT-3 covers the rest.
- A fixed area key for refreshing the moment the area changes: none found, so BT-1 refreshes about once a second.

## For the merge

Changelog line: Boktai: The Sun Is in Your Hand: the second screen shows the game's own Map (SELECT, Map page), following Django; the sun sensor reads a fixed level (solar_level in gba-ds.cfg, 6 by default). USA, Europe and Japan.

Wiki section (Supported-games): **Boktai: The Sun Is in Your Hand.** The second screen shows the Map page, taken from a hidden copy of the game while you walk about; in conversations and menus the last Map stays, and on the title screen the logo. The cartridge's sun sensor is set to a middle level. Known: Europe and Japan are checked on their logos and by start-up only.

Philosophy's list of exceptions (rule 6) gets: Boktai's clock, sunrise and sunset, from the ITEM page, beside its Map (its title bar left out).

Shared code touched (full suite has to run at the merge): `src/emu.c` / `emu.h` (the light source), `src/config.c` / `config.h` (`solar_level`), `src/main.c`, and the test clock: `GBADS_RTC_FIXED` in `src/emu.c` (applied again after every state load) set to noon by `tools/regress.py`, since Boktai tints its areas by the time of day.

## Facts about the game

USA addresses (EWRAM): `0x02001B3A` menu open (0 = playing, 1 = SELECT's menu), `0x020034DC` its page (2 = Map), `0x02001B38` 0 on the title and file menu, 40 in the game. SELECT's menu remembers its last page, so the capture presses L or R until the Map shows. The areas' colours follow the cartridge clock (day roughly 8:00 to 17:59, dusk, night), so the tests pin the clock. The cartridge's light sensor is GPIO; mGBA's levels give the reading 0xFF - (0x16 + level table).

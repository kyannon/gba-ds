# Boktai: The Sun Is in Your Hand (Bokura no Taiyou in Japan)

| | |
|---|---|
| Releases | `U3IE` USA · `U3IP` Europe (English, French, German, Spanish, Italian) · `U3IJ` Japan (the USA release walked about in; Europe and Japan on their logos and the same addresses) |
| Dumps | `U3IE` cartridge `e715ac45` (tested) · `U3IP` cartridge `9686c36b` (tested) · `U3IJ` cartridge `a09c0807` (tested) |
| Profile | `src/profiles.c`: the `U3IE` entry (bk_compose; the Map page of SELECT's menu); variants `U3IP`, `U3IJ` (as `U3IE`) |
| Tests | `tests/regress/boktai-the-sun-is-in-your-hand.json` (`tools/regress.py boktai-the-sun-is-in-your-hand`) |
| Status | in progress (side build) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them. Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused.

Every game also follows the [general rules](README.md#general-rules). The game draws its own Map (SELECT, then the Map page: the area you are in, with an arrow for Django), so by the [Philosophy](../../wiki/Philosophy.md) it is the second screen: a screen the game already draws, opened on the hidden copy and captured.

## Rules

**BK-1. Map.** While Django walks about (no menu, no conversation), the second screen shows the Map page of the game's SELECT menu, the whole screen at the game screen's size, as the game draws it: the area's map with its name below and the arrow for Django. It is opened on the hidden copy (SELECT, then L or R to the Map page), about once a second, so the arrow follows him.

**BK-2. No prompts.** The Map's button prompts (the L and R tabs at the title's ends, the A + up / down glyph) and the arrows at its edges that say it scrolls are left out (black there), as nothing on the second screen should make the player look twice.

**BK-3. Last one stays.** Where the menu can't be opened (a conversation, a scene, the area's title card), or with the game's own menu or Map open on the game screen, the last Map stays; before the first, the logo.

**BK-4. Title.** On the title screen, the file menu and the first start's set-up (sensor and clock notes, Konami logos, caution), the second screen shows the logo, and no captures are tried.

**BK-5. Logo.** The logo is the title's lettering (boktai, ボクらの太陽 in Japan) with its banner, cut out of the sun behind it; PUSH START, the copyright line and the sun are left out. The first start asks about the sensor and the clock before the title, so the hidden copy answers those (A through them, START to end).

**BK-6. Game screen untouched.** Nothing leaves the game screen.

**BK-7. The sun.** The cartridge's solar sensor reads a fixed level, 6 of 10 (the `solar_level` setting in `gba-ds.cfg`, 0 to 10; 0 is dark), so the sun gauge isn't empty. This is shared code (the emulator hands the core a light level); the game list has no row for it yet.

## Not covered yet

- Europe's and Japan's walking about: they share the USA release's addresses (checked: the Map opens, the logos extract); only their logos have cases until a state from each arrives.
- Bosses, the Solar Station and the World map: not reached; the Map is only taken where SELECT opens it, and BK-3 covers the rest.
- A fixed area key for refreshing the moment the area changes: none found, so BK-1 refreshes about once a second.

## For the merge

Changelog line: Boktai: The Sun Is in Your Hand: the second screen shows the game's own Map (SELECT, Map page), following Django; the sun sensor reads a fixed level (solar_level in gba-ds.cfg, 6 by default). USA, Europe and Japan.

Wiki section (Supported-games): **Boktai: The Sun Is in Your Hand.** The second screen shows the Map page, taken from a hidden copy of the game while you walk about; in conversations and menus the last Map stays, and on the title screen the logo. The cartridge's sun sensor is set to a middle level. Known: Europe and Japan are checked on their logos and by start-up only.

Shared code touched (full suite has to run at the merge): `src/emu.c` / `emu.h` (the light source), `src/config.c` / `config.h` (`solar_level`), `src/main.c`.

## Facts about the game

USA addresses (EWRAM): `0x02001B3A` menu open (0 = playing, 1 = SELECT's menu), `0x020034DC` its page (2 = Map), `0x02001B38` 0 on the title and file menu, 40 in the game. SELECT's menu remembers its last page, so the capture presses L or R until the Map shows. The cartridge's light sensor is GPIO; mGBA's levels give the reading 0xFF - (0x16 + level table).

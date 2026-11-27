# Shantae Advance: Risky Revolution

| | |
|---|---|
| Releases | `CSAE` World (Limited Run Games' 2024 cartridge: no code in its header, see SAR-2) |
| Dumps | `CSAE` Limited Run Games `31265c5d` (tested) |
| Profile | `src/profiles.c`: the `CSAE` entry |
| Tests | `tests/regress/shantae-advance-risky-revolution.json` (`tools/regress.py shantae-advance-risky-revolution`) |
| Status | supported (the opening, the first area, Risky's airship and the town after it checked; dances, transformations, dungeons and Battle Mode not yet) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list shantae-advance-risky-revolution`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

WayForward finished the game in 2024, and Limited Run Games released it as a GBA cartridge. The game has no map of its own, so the second screen takes its HUD: the held item's circle with the magic bar below it, the hearts, the gems and the dance indicator move off the game screen, and no second copy of the game runs beside it.

## Rules

**SAR-1. Logo.** The logo is the title's lettering: Shantae with its red swoosh, ADVANCE and "Risky Revolution!" (rows 3 to 113, the tail of the y in Risky the lowest), without the purple behind it, the menu or the copyright line. The title is painted as one picture with its purple gradient, so a mask cuts the lettering out (`tools/make_logo_masks.py`, `sar`). It's taken 900 frames after power-on, with no button pressed (the title is up from about frame 660).

**SAR-2. A cartridge with no game code.** The cartridge's header has no game code (nor a title): its four bytes are blank. GBA DS knows this dump by its CRC32 and files it under `CSAE`, the code players gave it: its profile, its logo, its place in the game list (shown with the supported games, not only with Show All), its patches folder ("Shantae Advance - Risky Revolution (World)") and the dumps check in the log and bug reports are all `CSAE`'s. Only this dump is known that way: a copy changed in the file itself has no code GBA DS can use, and plays as an unsupported game. A patch switched on in GBA DS (GEN-9) is applied after the dump is known, so the game stays `CSAE` with it.

**SAR-3. In play: the HUD and the logo.** While Shantae is in play, the HUD leaves the game screen and is drawn in the same corners of the second screen, on black (a live screen, GEN-1), with the logo in the middle at its own size: the held item's circle (the item equipped shows in it) with the magic bar below it at the top left, the hearts beside it, the gems at the top right with the dance indicator below them (lit while Shantae dances), each where the game draws it. Each piece is the game's own, drawn as it changes (a heart lost, a gem picked up, an item equipped). During the airship fight the airship's top, which the game draws under the gem counter, shows whole on the game screen.

**SAR-4. What stays on the game screen.** Everything but the HUD stays where the game draws it. The HUD's pieces are the only sprites the game draws at priority 0 in palette 0 (tiles 0 to 43); everything else in play, Shantae, the enemies, the bombs and signposts' prompts, is at priority 2. With the one-screen layout nothing moves: the HUD stays on the game screen.

**SAR-5. Talks.** For a talk (a signpost, a character) the game slides its HUD up off the top of the screen, a piece at a time over 8 frames, and back down when the talk ends. On the second screen the pieces slide off its top and back the same way. While none of them shows, the second screen shows the logo at half strength on dark grey (nothing live to show), as in the opening and other scenes.

**SAR-6. The game's own pause screen.** While the pause screen is open (Dance List, Key Items and Magic Items) and while it wipes away, the second screen shows the logo at half strength on dark grey, so the same screen is never on both (as Muppet Pinball Mayhem's pause, MPM-4). It changes at once, without fading with the game (as KND-5 and KAM-6): as the pause screen opens, the game fades to black by its colours and the HUD's pieces, the game's own, dim with it while the logo stays as it is; the logo on dark grey comes in as the pause screen starts wiping in, once the game has faded to black. The HUD and the logo come back on black the frame the game screen does.

**SAR-7. Title, opening and scenes.** On the title, the file select, the opening and any scene without the HUD (Scuttle Town's, the game over screen), the second screen shows the logo at half strength on dark grey.

## Not covered yet

- Checked by hand on the desktop build: the first area, the first boss (Risky's airship) and past it into the town, with an item equipped: the item in the held item's circle on the second screen. The cases cover the first area and the airship fight; the town has no case.
- Dances (the dance indicator lit) and transformations, dungeons, shops and more hearts: not seen yet. The HUD rule (SAR-4) holds as long as the game keeps drawing its HUD with the same sprites.
- Battle Mode (up to four players over the link cable): the logo, as for any screen without the HUD; not tried.
- The game list reads a 16 MB ROM with no game code in its header whole at each scan, unless it's this cartridge (which is remembered as `CSAE`): a changed copy of it, or homebrew of that size. A short wait, nothing more.

## Notes

| | |
|---|---|
| Header | title and code all blank (12 and 4 zero bytes), maker `01`, version 0, checksum F0; EEPROM save (`EEPROM_V122`) |
| HUD | sprites only, OAM 0-12 at priority 0 in palette 0, tiles 0-43: the held item's circle (x 2, rows 2-25) and the magic bar below it (x 5, rows 22-53), three hearts (x 25-48, rows 2-9), the gem box and counter (x 204-237, rows 2-17), the dance indicator (x 214, rows 18-33). It slides up 8 rows a frame for a talk (to row -62) and down again after |
| Pause screen | 0x03000F34: 0 in play, 2 opening (a fade to black by the colours over frames 1-8 after START, black 2 frames), 3 open (from frame 11, the page wiping in), 4 closing (a wipe), 5 for the first frames back |
| Play | DISPCNT 0x1F40 (mode 0): BG0 the back plane, BG1 the front plane, BG2 the sky; the pause screen 0x7F40 |
| Hearts | 0x03003066 (halfword, half hearts: 6 is three hearts) |
| Gems | 0x030062C4 (halfword; a copy at 0x0300631C) |
| Camera | 0x0300126E, 0x03001272 (halfwords: the first area runs from x 544 to 2864) |
| Title | BG0 alone, a 256-colour picture: the lettering over a 26-colour purple gradient, the menu box and the copyright line; the cursor a sprite; up from about frame 660 with no input |

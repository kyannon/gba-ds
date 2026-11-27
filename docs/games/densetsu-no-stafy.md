# Densetsu no Stafy

| | |
|---|---|
| Releases | `ASTJ` Japan |
| Dumps | `ASTJ` cartridge `fae94c8b` (tested) |
| Mods | `ASTJ` The Legendary Starfy v1.1 (Starfy Translation Team) `e324050c` (tested), Starfy Origins v1.1 (Starfy Translation Team) `f7cb8ee3` (tested), Densetsu no Starfy v1.1 (Starfy Translation Team; Japanese names kept) `a047b091` (tested) |
| Profile | `src/profiles.c`: the `ASTJ` entry |
| Tests | `tests/regress/densetsu-no-stafy.json` (`tools/regress.py densetsu-no-stafy`) |
| Status | supported (the first stage checked; the map between stages, boss fights and the minigames not yet) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list densetsu-no-stafy`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game came out only in Japan. The Starfy Translation Team's English translation comes as three patches, and all three are supported as fully as the Japanese game (STF-6).

The game has no map. In a stage the second screen shows the HUD along its top, over the game's logo (STF-2), so no second copy of the game runs beside it. The game's own pause screen (the vehicles, the items and the hint for where to go) stays on the game screen.

## Rules

**STF-1. Logo.** The logo is the title's lettering: 伝説のスタフィー with its ® (BG0 above row 96, without the reef behind it, the mascots and PRESS START flying in, or the copyright line), taken from power-on with START pressed through the opening.

**STF-2. In a stage: the HUD and the logo.** While Starfy is in a stage, the HUD leaves the game screen and is drawn along the top of the second screen, on black (a live screen, GEN-1), with the game's logo in the middle at its own size, where the idle logo sits (STF-5): Starfy's face at the left edge, the hearts in the middle, and the pearls with the TOTAL counter at the right edge, the left and right pieces 8 pixels in. The pieces are as far from the top edge (4 rows, rows 4 to 19) as the game draws them from the bottom edge. Each piece is the game's own, drawn as it changes (a heart lost, a pearl found), so it dims with the game as a stage fades out and in; the logo under it stays as it is.

**STF-3. What stays on the game screen.** Everything but the HUD stays where the game draws it. The HUD's pieces are told apart by where they are: only sprites at the HUD's priority whose top is in rows 136 to 159 move, so a signpost's label ("To dash!" in the translations), which the game draws at the same priority higher up (rows 112 to 122), stays on the game screen. With the one-screen layout nothing moves: the HUD stays on the game screen.

**STF-4. The game's own pause screen.** While the game's pause screen is open on the game screen, the second screen shows the logo at half strength on dark grey, with no HUD on it (the game draws none on its pause screen), so the same screen is never on both (as Muppet Pinball Mayhem's pause, MPM-4). It changes at once, without fading with the game (as KND-5 and KAM-6): to the logo on dark grey once the stage has faded to black, and back to the HUD and the logo once the pause screen has faded out.

**STF-5. Logo elsewhere.** The second screen shows the logo at half strength on dark grey wherever Starfy isn't in a stage: the opening, the title, the file select and the story scenes.

**STF-6. The English translations.** On the Starfy Translation Team's three patches, as patched ROMs or with their `.bps` on the Japanese ROM (GEN-9), STF-2 to STF-5 hold as they are: the same screens, RAM and timing as the Japanese game, the HUD's pieces in the translation's own words (TOTAL). Each one's logo is its own title, taken the same way: Each translation's logo is kept apart from the Japanese game's and the other translations' (GEN-20), so with more than one in the game list each shows its own.
- *The Legendary Starfy*: THE LEGENDARY STARFY.
- *Starfy Origins* (the same text): THE LEGENDARY STARFY above ORIGINS.
- *Densetsu no Starfy* (the characters keep their Japanese names, Rob-jiisan for Old Man Lobber): THE LEGENDARY STARFY, the same lettering as *The Legendary Starfy*.

## Not covered yet

- The map between stages (not reached in the emulator yet): the logo or the HUD shows there, whichever 0x03000EC1 gives.
- Talking to a character in the middle of a stage (none in the first stage): the profile asks only for the stage (0x03000EC1), so the HUD should stay on the second screen; not seen yet. A signpost's label is not a conversation (STF-3).
- Boss fights and the minigames.

## Notes

| | |
|---|---|
| Paused | 0x03000EB0, EB1, EBA: 02 10 DF while the pause screen is open, 00 playing (the same on all four ROMs) |
| In a stage | 0x03000EC1: 00 in a stage and its pause screen, FF on the title, the file select and in story scenes |
| Stage | 0x03000EC0: 0 in the first stage |
| Hearts | 0x03000F7A (0-5; kept with the save, so 5 on the title too) |
| Pause timing | START: a fade to black over frames 4-34, black to 45, a fade in to 77; drawn and still from frame 78 (only the sprites move: the whale's spout, the girl) |
| Fades | the pause screen's opening and closing: to black by its colours (no level byte; the brightest colour is 30 unfaded, the stage's and the pause screen's), with BLDCNT 0x00FF and BLDY at its black hold while the pause screen's colours go in; counters 0x03000EBA (stage), 0x030011C9 (pause screen in), 0x030049D9 (pause screen out) |
| HUD | sprites only: OAM 0-15 at priority 0, tiles 256 to 282 in palettes 0 and 1: the face (x 72, row 140), five hearts (x 100 to 164), an arrow (x 180) and the pearl ring and TOTAL counter (x 196 to 235, rows 140 and 148); the same in all four ROMs. A signpost's label (tiles 376 to 394, palette 5, x 89 to 153, row 112) is drawn at priority 0 too, so rows decide |
| Title | BG0 the lettering (Japanese rows 8-87, English 17-81, Origins 8-86) and the copyright line; the reef BG1-BG3; PRESS START and the mascots sprites; up about 70 frames after START in the opening, else from about frame 8200 (Japanese) or 10270 (English) |
| Translation | Starfy Translation Team v1.1 (September 2023): three `.bps` patches for the Japanese ROM, the same code and screens |

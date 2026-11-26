# Densetsu no Stafy 2

| | |
|---|---|
| Releases | `AVFJ` Japan |
| Dumps | `AVFJ` cartridge `ea73eb54` (tested), Virtual Console `bb1370f1` |
| Mods | `AVFJ` The Legendary Starfy 2 English v0.9.9 (Starfy translation patches) `ec61d726` (tested) |
| Profile | `src/profiles.c`: the `AVFJ` entry |
| Tests | `tests/regress/densetsu-no-stafy-2.json` (`tools/regress.py densetsu-no-stafy-2`) |
| Status | supported (the first stage checked; the stage select, the shop and the minigames not yet) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list densetsu-no-stafy-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game came out only in Japan. The English patch (v0.9.9) is supported as fully as the Japanese game (ST2-6). The second screen is the same as [Densetsu no Stafy](densetsu-no-stafy.md)'s: the HUD along its top over the logo in a stage (ST2-2), never the game's pause screen.

## Rules

**ST2-1. Logo.** The logo is the title's lettering: 伝説のスタフィー2 with its ® (BG1 above row 96, without the reef behind it, the characters and PRESS START, or the copyright line), taken from power-on with START pressed through the opening.

**ST2-2. In a stage: the HUD and the logo.** While Starfy is in a stage, the HUD leaves the game screen and is drawn along the top of the second screen, on black (a live screen, GEN-1), with the game's logo in the middle at its own size, where the idle logo sits (ST2-5): Starfy's face at the left edge, the hearts in the middle, and the pearls with the TOTAL counter at the right edge, the left and right pieces 8 pixels in, on the same rows as in Densetsu no Stafy (STF-2, rows 4 to 19). Each piece is the game's own, drawn as it changes, so it dims with the game as a stage fades out and in; the logo under it stays as it is.

**ST2-3. What stays on the game screen.** Everything but the HUD stays where the game draws it. The HUD is the sprites at its priority (1) whose top is in rows 136 to 159; nothing else was seen at that priority in the stage. With the one-screen layout nothing moves: the HUD stays on the game screen.

**ST2-4. The game's own pause screen.** While the game's pause screen is open on the game screen (its Hints tab or any other), the second screen shows the logo at half strength on dark grey, with no HUD on it (the game draws none on its pause screens), so the same screen is never on both (as Densetsu no Stafy's, STF-4). It changes at once, without fading with the game (as KND-5 and KAM-6): to the logo on dark grey as the pause screen is set up, and back to the logo on black once the pause screen has faded out, the HUD coming in as the stage does.

**ST2-5. Logo elsewhere.** The second screen shows the logo at half strength on dark grey wherever Starfy isn't in a stage: the opening, the title and the file select.

**ST2-6. The English translation.** On the English patch v0.9.9, as the patched ROM or with its `.bps` on the Japanese ROM (GEN-9), ST2-2 to ST2-5 hold as they are: the same screens and RAM as the Japanese game, the HUD's pieces in the translation's own words (TOTAL). Its logo is its own title, THE LEGENDARY STARFY 2, taken the same way. Its logo is kept apart from the Japanese game's (GEN-20), so with both in the game list each shows its own.

## Not covered yet

- The stage select between stages, the shop, talking to a character in the middle of a stage and the minigames: not reached in the emulator yet (the first stage has no way out in the scouting). The profile asks only for the stage (0x0200002C), so the HUD should stay on the second screen through a conversation.
- Signpost boards (UP at a sign: a full-screen board over the stage, DISPCNT 0x1F40 and 0x3F40): the game hides its HUD then, and the second screen keeps the logo on black until the board closes; not tuned.

## Notes

| | |
|---|---|
| HUD | sprites only: OAM 0-16 at priority 1, tiles 256 to 282 in palettes 0 and 1: the face (x 64, row 140), five hearts (x 84 to 148), an arrow (x 172) and the pearl ring and TOTAL counter (x 188 to 235, rows 140 and 148); a stage's other sprites are at priority 2 |
| Screen registers | DISPCNT 0x04000000: 0x7D40 in a stage, 0x7F40 on every pause screen (also while it fades in and out); BG0CNT 0x04000008: 0x0F42 in a stage, 0x1C00 on the Hints tab and the lists, 0x1F03 on the Stuff page |
| In a stage | 0x0200002C: 00 in a stage and its pause screens, FF on the title and the file select |
| Fades | the stage's and the pause screen's fades are by their colours, as Densetsu no Stafy's: the brightest BG colour is 31 unfaded and goes down one a frame, with BLDY at 16 (black) while the layers are off |
| Pause timing | START: the stage fades to black by its colours over about 34 frames, the pause screen is drawn from frame 58 and fades in to frame 94; closing, it fades out over 34 frames, the screen is blank (DISPCNT 0x7D40 already, the palette still full) for about 13 frames, and the stage fades in from frame 52 to 82 |
| Title | BG1 the lettering (rows 5-85) and the copyright line (rows 145-157), PRESS START and the characters sprites, the reef BG2 and BG3; START cuts the opening short from about frame 1600 |
| Translation | the Starfy translation patches: Starfy.2.Eng.v0.9.9.bps for the Japanese ROM, the same code and screens |
| Fixtures | the stage states are 240 frames into the stage: a state taken in its first frames has START ignored for a while |

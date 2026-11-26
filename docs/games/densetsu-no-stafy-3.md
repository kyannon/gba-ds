# Densetsu no Stafy 3

| | |
|---|---|
| Releases | `B3DJ` Japan |
| Dumps | `B3DJ` cartridge `fcaf1aa8` (tested), Rev 1 `2d6e4c3b` (tested), Rev 1, Virtual Console `59ca95c6` |
| Mods | `B3DJ` The Legendary Starfy 3 English v1.0.5 (Starfy translation patches) `7d5ec993` (tested) |
| Profile | `src/profiles.c`: the `B3DJ` entry |
| Tests | `tests/regress/densetsu-no-stafy-3.json` (`tools/regress.py densetsu-no-stafy-3`) |
| Status | supported (the first stage checked, on the first release and Rev 1; the shop, Moe's House and the minigames not yet) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list densetsu-no-stafy-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game came out only in Japan. The English patch (v1.0.5) is supported as fully as the Japanese game (ST3-6). The second screen is the same as [Densetsu no Stafy](densetsu-no-stafy.md)'s: the HUD along its top over the logo in a stage (ST3-2), never the game's pause screen.

## Rules

**ST3-1. Logo.** The logo is the title's lettering: 伝説のスタフィー3 with its ® (BG0 above row 96, without the islands behind it, the characters and PRESS START, or the copyright line), taken from power-on with START pressed through the opening.

**ST3-2. In a stage: the HUD and the logo.** While Starfy is in a stage with the controls his, the HUD leaves the game screen and is drawn along the top of the second screen, on black (a live screen, GEN-1), with the game's logo in the middle at its own size, where the idle logo sits (ST3-5): Starfy's face at the left edge, the hearts in the middle, and the pearls with the TOTAL counter at the right edge, the left and right pieces 8 pixels in, as in Densetsu no Stafy (STF-2). The game draws this HUD two rows lower than the other two games; along the top its pieces sit on the same rows (4 to 19), so the three look alike. Each piece is the game's own, drawn as it changes, so it dims with the game as a stage fades out and in; the logo under it stays as it is. Once the HUD has come in, the black stays for the rest of the stage: a conversation (talking to Moe) or a signpost's board takes the HUD off the game screen, and the second screen keeps the logo alone on black until the game draws the HUD again, which goes back to the second screen at once, not onto the game screen.

**ST3-3. What stays on the game screen.** Everything but the HUD stays where the game draws it. The HUD is the sprites of palettes 0 and 1 at its priority (0) whose top is in rows 136 to 159, so a signpost's board, which draws its prompts ("L R", "B: back") at the same priority and in the same rows, in palette 15, stays whole on the game screen, and so does the sprite-window sprite at the top left corner. With the one-screen layout nothing moves: the HUD stays on the game screen.

**ST3-4. The game's own pause screen.** While the game's pause screen is open on the game screen (its Hint tab, a list or the Stuff page), the second screen shows the logo at half strength on dark grey, with no HUD on it (the game draws none on its pause screens), so the same screen is never on both (as Densetsu no Stafy's, STF-4). It changes at once, without fading with the game (as KND-5 and KAM-6): to the logo on dark grey as the pause screen is set up, and back to the HUD and the logo once the pause screen has faded out.

**ST3-5. Logo elsewhere.** The second screen shows the logo at half strength on dark grey wherever Starfy isn't in a stage: the opening, the title, the file select and the stage select; and in a stage until the game draws its HUD (the first stage's opening conversation and the walk-off after it, about 280 frames, or a conversation the first moment a stage is loaded in), where the black and the HUD arrive together.

**ST3-6. The English translation.** On the English patch v1.0.5, as the patched ROM or with its `.bps` on the Japanese ROM (GEN-9), ST3-2 to ST3-5 hold as they are: the same screens and RAM as the Japanese game, the HUD's pieces in the translation's own words (TOTAL). Its logo is its own title, THE LEGENDARY STARFY 3, taken the same way. Its logo is kept apart from the Japanese game's (GEN-20), so with both in the game list each shows its own.

## Not covered yet

- The shop, Moe's House and the minigames: not reached in the emulator yet (the first stage has Moe's conversation, tested from a state after it); the profile needs nothing from them but a stage's registers, and the HUD sprites' own appearance.
- Signpost screens (a move shown on a board, from a signpost in a stage): the board stays on the game screen whole (ST3-3); the HUD isn't drawn there, so the second screen shows the logo on black.

## Notes

| | |
|---|---|
| HUD | sprites only: OAM 6-19 (and 0-5) at priority 0, palettes 0 and 1: the face (x 64, row 142), five hearts (x 84 to 148), an arrow (x 170), the pearl ring and TOTAL counter (x 186 to 236, rows 142 and 150, the last piece partly off the screen's right edge); two rows lower than in the other two games. A signpost's board is in palette 15 (prompts from row 145), and OAM 0 is a sprite-window sprite at the top left corner |
| Screen registers | no flag was found in RAM, so the screen registers say where the game is. DISPCNT 0x04000000: 0xFF40 in a stage (bit 15, the sprite window, set; 0xF740 in some rooms), 0x7D40 on the Hint tab, 0x5F40 on the lists, 0x1F60 on the Stuff page, 0x3F40 the title, 0x1E00 the file select, 0x1F20 the stage select. BG0CNT 0x04000008: 0x0F02 (0x0F00 in a conversation) in a stage, 0x1E00 on the Hint tab and the lists, 0x1E01 on the Stuff page |
| Fades | by their colours, as Densetsu no Stafy's: the brightest BG colour is 31 unfaded and goes down one a frame (about 33 frames), black for about 18 more (BLDY 16, the pause screen's first frames BLDY 7), then the pause screen fades in 0 to 31; closing mirrors it |
| Controls | 0x02001630 (EWRAM, a byte): FF in play and 01 through the first stage's opening conversation and the walk-off after it, until the HUD comes in; but it stays 01 after a later conversation in the same stage (talking to Moe), with the controls back, START working and the HUD drawn: so it isn't used, the profile's `hud.onWith` (the HUD's face sprite) says when the HUD is in |
| Title | BG0 the lettering (rows 2-85), the copyright line (rows ~150-157) and, on the English patch, an ENG V1.0 tag at the bottom right; PRESS START, Starfy, Moe and Starly sprites. START cuts the opening short from about frame 900 to 2000 of a boot (at 3000 and later it was lost on both the Japanese game and the English patch), so the logo is taken from START at frame 1300: about 1500 frames, where waiting for the opening to end took 6200 |
| Translation | the English patch v1.0.5 in the Starfy translation patches: Starfy.3.Eng.v1.0.5.bps for the Japanese ROM (Rev 1), the same code and screens. The first release (before Rev 1) has its code moved about, but the same screen registers and HUD sprites: the profile works on it as it is (cases `st3-rev0-*`) |
| Fixtures | the stage states are 240 frames into the stage: a state taken in its first frames has START ignored for a while |

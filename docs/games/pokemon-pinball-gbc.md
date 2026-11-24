# Pokémon Pinball (Game Boy Color)

| | |
|---|---|
| Releases | `cjng` USA (cartridge code `VPHE`) · `cjl9` Europe (`VPHP`) · `c9wf` Japan (`VPHJ`) |
| Dump CRC32s (not a Dumps row: make_dumps.py and regress.py read GBA dumps only; Game Boy dumps are for the merge) | `cjng` `03ce8d9a` · `cjl9` `39c432a4` · `c9wf` `13c70de9` |
| Profile | `src/profiles.c`: the `cjng` entry (logo-only, `gbpin`: the table over both screens, `src/gbpin.c`); the other releases are `variants[]` rows of it (the game list's code and mGBA's cartridge code both answer) |
| Tests | `tests/regress/pokemon-pinball-gbc.json` (`tools/regress.py pokemon-pinball-gbc`) |
| Status | in progress (branch `game/pokemon-pinball-gbc`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list pokemon-pinball-gbc`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6) and the Game Boy rules (GB-1 to GB-10, `docs/GAME-BOY.md`).

The Game Boy Color cartridge's tables are two Game Boy screens tall, and the game shows one at a time, with a white flash when the ball crosses from one to the other. Like Pokémon Pinball: Ruby & Sapphire (PP-1), this game has both screens show the table, live: the table's upper screen on the top screen, its lower screen on the bottom one. A hidden copy of the game, run in step with yours (the same buttons every frame), loads the other half with the game's own code and draws it; the game you play is never changed.

## Rules

**PPG-1. The table over both screens, live.** On the Red and Blue tables, while the ball is in play, the top screen shows the table's upper Game Boy screen and the bottom screen its lower one, both live and drawn by the game itself. Each is the game's own 160x144 picture, placed as the game screen is (GB-2). *This is an exception to the philosophy's second principle (nothing split across the screens), like Pokémon Pinball: Ruby & Sapphire's tall view, and is listed with the philosophy's exceptions.*

**PPG-2. The crossing.** When the ball passes to the other half, the game fades both screens through white for about a fifth of a second. Both screens stay as they were until it's over (nothing half-drawn, GEN-3) and then show the new halves; the ball goes on through the other screen.

**PPG-3. Score bar.** The score and message bar is the game's, at the bottom of the bottom half only; the top half has none.

**PPG-4. Everything else on one screen.** On the title, the menus, the field select, the pause screen, the bonus stages and between balls, the game is on its own screen as in GB-3 and the other screen is idle (dark grey). The table view starts when the ball is back in play and goes back when it's not; if a half can't be taken for more than a second and a half, the game goes back to its own screen.

**PPG-5. Swapped screens.** With the screens swapped, the upper half stays on the top screen and the lower one on the bottom screen, as without.

**PPG-6. Off.** With `pinball_live = 0` (the same switch as Ruby & Sapphire's), or one screen, the game plays as it is on one screen (GB-3). *By hand: on the device.*

**PPG-7. Known issues.** The view costs about 14 emulated Game Boy frames per displayed frame; on a slow device it may not keep up (set `pinball_live = 0`). The bonus stages stay on one screen.

## For the merge

Changelog line (under New games): **Pokémon Pinball (Game Boy Color)**: the table over both screens, live (USA, Europe and Japan), the first Game Boy Color game with its own second screen. Turned off with `pinball_live = 0`.

Wiki: `Supported-games.md`: a table row and section as above; add "Pokémon Pinball (Game Boy Color)" to the philosophy's list of exceptions (the tall tables).

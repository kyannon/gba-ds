# Sonic Pinball Party

| | |
|---|---|
| Releases | `A3VE` USA, `A3VP` Europe (both in six languages) |
| Profile | `src/profiles.c`: the `A3VE` entry (logo-only for now) |
| Tests | `tests/regress/sonic-pinball-party.json` (`tools/regress.py sonic-pinball-party`) |
| Status | preliminary: the logo; the table over both screens is being built |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-pinball-party`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The owner wants the tables twice as tall over both screens, live, as Pokémon Pinball's are (its rules: [pokemon-pinball.md](pokemon-pinball.md)). Rules marked *planned* say what that will do; they aren't built yet, and the second screen shows the logo there for now.

## Rules

**SPP-1. Logo.** On the title, the menus, and wherever the table isn't up over both screens, the second screen shows the logo (the title's: the name on its badge, without PRESS START, the copyright lines or the checkered background).

**SPP-2. The table over both screens, live** *(planned)*. On the Sonic, NiGHTS and Samba de Amigo tables (Arcade and Story) the two screens show 320 rows of the table in one piece, as if on one tall screen, scrolling smoothly with the ball and never jumping; your game itself is never changed. The tables are 416 (Sonic) and 432 (NiGHTS) rows tall. As on Pokémon Pinball, copies of your game taken from it every frame draw the rows your game's view doesn't.

**SPP-3. Score bar and message line** *(planned)*. The score bar (rings, score) is at the top of the top screen and the message line ("Aim for the arrow...") at the bottom of the bottom screen, as on one tall screen.

**SPP-4. Story and pauses** *(planned)*. What the game lays over the table (Story mode's conversations, the pause menu, results) appears on the top screen, as your game draws it, with the table's rows below on the bottom screen. (The owner's instructions: "any dialogue or story options that would usually be overlaid on the game screen should appear on the top (primary) screen".)

**SPP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen. *(planned, with SPP-2)*

**SPP-6. One screen.** With one screen, the game plays as it is.

## Not covered yet

The two-screen view (SPP-2 to SPP-5). Bonus stages and the party games are to be looked at once the tables work.

## Facts found so far (for sessions)

| What | Where / value (USA, `A3VE`) |
|---|---|
| On a table | DISPCNT `0x1F40`; BG0CNT `0x8C03` (the table, 4bpp, map `0x6000`, 256x512), BG1CNT `0x8E0A` (rails, map `0x7000`), BG2CNT `0x1F09` (the message line), BG3CNT `0x9D08` / `0x5D08` / `0x5E08` (the score bar) |
| View | BG0/BG1 VOFS, shadows `0x03000784`/`786` and `0x03002992` (HOFS `0x0300077C`/`77E`, `0x03002990`); written by `0x0801FA30` |
| Camera | row `0x02031AE4` (half); view = clamp(row - 80, 8, height - 168) + shake (`0x0203524E`) |
| Table height | `0x030040E0` (half): 416 Sonic, 432 NiGHTS |
| BG0 tiles | a 22-row ring at `0x06000000` (1 KB a tile row, table tile row R in slot R % 22), streamed by `0x0801F7F0` (one row, from ROM via the table at `0x080AC94C`) as the view crosses a tile row (`0x02031AD4`, `0x02031ADC` counters) |

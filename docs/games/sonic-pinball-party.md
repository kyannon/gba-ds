# Sonic Pinball Party

| | |
|---|---|
| Releases | `A3VE` USA, `A3VP` Europe (both in six languages) |
| Profile | `src/profiles.c`: the `A3VE` entry (logo-only, with `tall` and `tall.live.carry`: the table over both screens, game.c `update_tall`) |
| Tests | `tests/regress/sonic-pinball-party.json` (`tools/regress.py sonic-pinball-party`) |
| Status | work in progress (v1.4.4): released with a note that glitches may occur |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sonic-pinball-party`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The tables are twice as tall over both screens, live, as Pokémon Pinball's are (its rules: [pokemon-pinball.md](pokemon-pinball.md)): two more copies of the game, taken from yours every frame, draw the rows your game's view doesn't.

## Rules

**SPP-1. Logo.** On the title and the menus, the second screen shows the logo (the title's: the name on its badge, without PRESS START, the copyright lines or the checkered background).

**SPP-2. The table over both screens, live.** On a table (Sonic and NiGHTS, Arcade and Story, so far), the two screens show 320 rows of it in one piece, as if on one tall screen, scrolling smoothly with the ball and never jumping; your game itself is never changed. While your game's view is in the table's upper part, the top screen is your game and the bottom screen the 160 rows below it; further down, the view stops with the table's lowest rows on the bottom screen (the Sonic table shows rows 8 to 408, NiGHTS 8 to 424), so the ball is on the top screen except when it's down near the flippers. Wherever your game's own picture has the rows, it's what's shown; two copies of your game, taken from it every frame and held at their rows, draw the rest, sprites included (the ball, flippers, Sonic's enemies). When the copies start (a table comes up, a state loaded) the second screen shows the logo for a moment while they reach their rows; when the game loads its table's graphics afresh (a stage's intro, a new ball) they start again from it, and a row no copy has for a few frames keeps what it showed. During a table's opening sweep (the view panning down the table after the intro), and for two frames after it, the rows the copies draw show the table without its sprites: the game moves its sprites with the view then rather than placing them afresh, so a copy's would be where they are on your game's screen. On a frame where the game's logic runs long and it shows its last frame's sprites again, each copy shows its own last ones, never your game's.

**SPP-3. Score bar and message line.** The score bar (rings, score, and banners such as BALL SAVED in its place) is at the top of the top screen and the message line ("Aim for the arrow...") at the bottom of the bottom screen, as on one tall screen Something over the table that comes down into the message line's rows from above (the zone card's ribbon down the left edge, a banner sliding up through them) stays in one piece on the top screen; only what starts in those rows, such as the line's own text, goes to the bottom screen. A banner rising from the bottom of the game's screen shows at the bottom of the bottom screen for its first frame or two, until it reaches the rows above.

**SPP-4. Boxes and banners over the table.** What the game lays over the table (the pause menu, banners such as NiGHTS's SHOOT BONUS, with the sprites drawn on them, such as the pause menu's arrow) appears on the top screen where it is on your game's screen, over the table's rows there, with the rest of the table below, whether the top screen is your game's own picture or a copy's. (The owner's instructions: "any dialogue or story options that would usually be overlaid on the game screen should appear on the top (primary) screen". Story mode's conversations aren't checked yet: no state of one.)

**SPP-5. Swapped screens.** With the screens swapped, the table's upper rows stay on the top screen and the lower ones on the bottom screen.

**SPP-6. One screen.** With one screen, the game plays as it is; `pinball_live = 0` in `gba-ds.cfg` turns the copies off too (one screen, the logo on the other). *By hand: speed on the device (two copies, two frames each every frame, as on Pokémon Pinball). (checked on the device for v1.4.4: 60 fps on the RG DS Plus, from the owner's bug reports)*

## Exceptions

SPP-2 (the table over both screens, from copies of the game) is an exception to the philosophy's GBA screen (a GBA has one 240x160 screen) and cost rules, as Pokémon Pinball's is: the owner's request, with `pinball_live = 0` as the way back.

## Not covered yet

The Samba de Amigo table, bonus stages, the party games, Story mode's conversations and results screens: no states of them yet. If one of them uses the same layers as a table, it gets the two-screen view too, untried. Speed on the device.

## Facts found so far (for sessions)

| What | Where / value (USA, `A3VE`) |
|---|---|
| On a table | DISPCNT `0x1F40`; BG0CNT `0x8C03` (the table, 4bpp, map `0x6000`, 256x512), BG1CNT `0x8E0A` (rails, map `0x7000`), BG2CNT `0x1F09` (the message line), BG3CNT `0x9D08` / `0x5D08` / `0x5E08` (the score bar) |
| View | BG0/BG1 VOFS, shadows `0x03000784`/`786` and `0x03002992` (HOFS `0x0300077C`/`77E`, `0x03002990`); written by `0x0801FA30` |
| Camera | row `0x02031AE4` (half); view = clamp(row - 80, 8, height - 168) + shake (`0x0203524E`) |
| Table height | `0x030040E0` (half): 416 Sonic, 432 NiGHTS |
| BG0 tiles | a 22-row ring at `0x06000000` (1 KB a tile row, table tile row R in slot R % 22), streamed by `0x0801F7F0` (one row, from ROM via the table at `0x080AC94C`) as the view crosses a tile row (`0x02031AD4`, `0x02031ADC` counters) |
| Ball sprite | placed by 0x08029D38 from the ball's row (`0x03002A1A`, the game object `0x03002980` + 0x9A) by the camera's own clamp, as if the camera followed it: patched in the copies to the ball's row less the copy's view |
| Vblank queue | 0x0800162C queues copies (count byte `0x030004F8`, 12-byte entries at `0x030020B0`: destination, source, size) done at the next vblank; a copy keeps its own entries into BG0's and BG1's tiles and maps (VRAM `0x0000`-`0x7FFF`), not the game's. A whole-ring entry (`0x6000` bytes, from 0x0801F449) is the table loaded afresh |
| BG maps | BG0's map rows (`0x6000`) are rewritten through the queue as the view moves (each table row's tiles in its ring slot) and BG1's (`0x7000`) by the CPU (0x0805FECC): both kept per copy, with BG0's ring |
| Opening sweep | `0x020319AC` is 1 (the intro and the view panning down the table): the game's code at 0x0800A45C skips the objects' update, and the sprites move with the view |
| Shadow sprites | `0x03000E50` (the next frame's), copied to the sprite table at the vblank only once the frame's logic is done; on a long frame the last frame's stay, or a part-built shadow is copied: the copies keep their own shadow and sprite table (from the frame they last drew) and put them back before each frame |
| Boxes over the table | BG2 (the pause menu, banners) and BG3 (the score bar); the copies leave both out |

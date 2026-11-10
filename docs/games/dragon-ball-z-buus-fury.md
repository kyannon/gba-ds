# Dragon Ball Z: Buu's Fury

| | |
|---|---|
| Releases | `BG3E` USA |
| Dumps | `BG3E` cartridge `01c1707f` (tested) |
| Profile | `src/profiles.c`: the `BG3E` entry (`bf_compose`, a logo mask) |
| Tests | `tests/regress/dragon-ball-z-buus-fury.json` (`tools/regress.py dragon-ball-z-buus-fury`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list dragon-ball-z-buus-fury`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7). The model is [The Legacy of Goku II](dragon-ball-z-legacy-of-goku-2.md), whose engine this game shares: its Scouter's map of the area you're in (SELECT here, R there) is what a two-screen GBA would have shown, so it's the second screen, and flying's small world map moves over as it does there. Unlike The Legacy of Goku II, the Scouter works from the start of the game, so there's no Status page before it.

## Rules

**BF-1. Logo.** On the title, the file menu (Select Game), the options and the story scenes the game opens with, the second screen shows the logo: DRAGON BALL Z and BUU'S FURY from the title screen, cut out of the picture they're painted on (none of Buu, the fighters, the sun behind the lettering or the sky; not the ® and ™, the START / OPTIONS box or the copyright line).

**BF-2. The area map.** Walking about, the second screen is the Scouter's map of the area (SELECT) as the game draws it: the scouter frame, the area's shape (the parts you've been to in blue), your dot and the other marks, centred on you as the game centres it when it opens. It's opened in the background on a silent copy of the game about once a second (and as soon as you're in a new area, GEN-6), so your dot moves as you do; your game is never paused. Its button prompts are left out (BF-7).

**BF-3. The HUD stays.** The technique icon, the Super Saiyan gauge, the HP, EP and EXP bars, a boss's bar, the fusion timer and the area's name as you enter it stay on the game screen, where the game draws them (the Scouter's map has no free place for them).

**BF-4. Elsewhere, the last picture.** In a conversation or a scene played on the field, at a save circle's menu (Save / Switch Characters), in the game's own pause menu, its own Scouter (map, world map and scan pages), a shop's menu, and wherever SELECT doesn't open the map, the second screen keeps the last picture; before the first, the logo.

**BF-5. Flying.** Flying over the world (from a World Map Circle or a flight pad), the small map of the world at the game screen's top right (its outline, your arrow and the places' marks) moves to the second screen, twice as big and in the middle, on the plain dark background; the game screen goes on without it, the haze over the horizon below it as the game draws it. The bar of button help at the bottom (A button: descend / land, B button: ascend) stays on the game screen. The map of the area you left is forgotten: landing in an area brings that area's map. (An exception to the second screen being drawn at the game's size, as The Legacy of Goku II's is: the small map is hard to read at its own size.) The world map's frame is grown from what's drawn: if your arrow goes past the outline's edge, the picture moves a few pixels to stay in the middle.

**BF-6. One screen.** With one screen, the game plays as it is.

**BF-7. No button prompts.** The second screen can't be scrolled or paged, so the prompts for doing that are left out of the area map: the four arrows round its edge (the d-pad scrolls it) and the L and R letters on the scouter frame's top corners (the corners' own green instead).

**BF-8. No map here.** Where the game's Scouter has no map to show (inside a shop such as West City's Z-Mart, where it opens on an empty frame with no dot), the second screen keeps the last area's map, dimmed, until a map can be shown again (as A Link to the Past does indoors); before the first, the logo.

## Not covered yet

Tried with the owner's save (Kid Buu Saga, West City: Goku at level 200, all five fighters): walking about West City, its save circle, the pause menu, the Scouter's pages, a World Map Circle, flying and landing; and from a new game, the story scenes it opens with and Snake Way (Chapter 1, Goku), where the Scouter's map already works. Fights (the Scouter during one), boss fights, fusion (Gotenks, Gogeta), the Instant Transmission map, Chapter 6's Dragon Ball search while flying, the game's ending and its Game Over aren't tried yet; in a situation that keeps the field's display setup they get the same treatment, untried. The Buu's Fury and Dragon Ball GT: Transformation double pack is another ROM (another game code) and isn't supported.

## Facts found so far (for sessions)

| What | Where / value (USA) |
|---|---|
| Walking about | DISPCNT `0x1F40` (BG0CNT `0x1C82`); the story scenes, conversations and the save circle's menu too (boxes on BG0 / BG1) |
| Area | `0x03001980`: a pointer to the area's map data (0 on the title, file menu and before the first scene; `0x0808E708` flying, West City `0x0808F930`, Snake Way `0x0808EA18`); the byte at `0x0300197D`, 0 there and `0x32` flying, others 1 to 7 so far (West City and Snake Way 1); Data Crystal: the area is set at `0x08008A1C` (r1 the map data) |
| The Scouter's map (SELECT) | DISPCNT `0x1D40`, BLDALPHA counts up to `0x0010` as it fades in (settled about 30 frames after SELECT); BG0 the frame, BG2 the area; the marks and the frame's arrows sprites (palette 0); R the whole world, L the scan page (a cursor); B closes it |
| Its scroll arrows | sprites at 4,72 and 228,72 (8x16) and 112,4 and 112,148 (16x8), priority 0, palette 0 (as The Legacy of Goku II's) |
| Its L and R | BG0, the frame's top corners: dark green (0, 74, 0) letters at x 5-10 and 229-234, rows 4-11, on the corner plates' (0, 165, 0) |
| No map | in the shops (zones 91 to 95: West City's Z-Mart is 92) the Scouter opens on its frame alone, no area or dot (all of its pixels greens: red and blue at most 66) |
| Pause menu (START) | DISPCNT `0x0400`; opens on Status (Journal, Inventory with L / R; Cancel and Skills prompts) |
| HUD | sprites at the top left: the technique icon, the Super Saiyan gauge and the HP / EP / EXP bars |
| Flying | DISPCNT `0x1741` (mode 1), the world in perspective with an inset map at the top right: its outline BG0 rows 6-51, x 168-230 (blended, BLDCNT `0x1641`, BLDALPHA `0x1006`), BG0's haze over the horizon from row 56; your arrow an 8x8 sprite (priority 0); the help bar sprites at row 136 (priority 1) |
| Title | DISPCNT `0x0200`, BG1 alone (the logo painted into the picture), from about frame 800 with no input; Select Game `0x1240` |
| Save | 8 KB EEPROM; GameFAQs' raw `.sps` dumps of it are already in mGBA's byte order (copy as `.sav`) |

## For the merge

Changelog line (New games): "Dragon Ball Z: Buu's Fury: walking about, the Scouter's map of the area on the second screen, following you; flying, the small world map moved there, twice as big."

Philosophy's exceptions: add "Dragon Ball Z: Buu's Fury's" to The Legacy of Goku II's small world map, shown while flying, drawn twice as big.

Wiki, Supported games (table row and section):

### Dragon Ball Z: Buu's Fury

USA. Walking about, the second screen is the Scouter's map of the area you're in (the one SELECT opens), kept up to date about once a second as you move; its scroll arrows and L / R letters are left out. Flying over the world, the small world map from the game screen's top right moves to the second screen, twice as big. In conversations, menus and the game's own Scouter, the last map stays. The HUD stays on the game screen.

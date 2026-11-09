# Dragon Ball Z: The Legacy of Goku II

| | |
|---|---|
| Releases | `ALFE` USA · `ALFP` Europe (English, French, German, Spanish, Italian) · `ALFJ` Japan (The Legacy of Goku II International) |
| Dumps | `ALFE` cartridge `204142e1` (tested) · `ALFP` cartridge `20684433` (tested) · `ALFJ` cartridge `b3297d59` (tested) |
| Profile | `src/profiles.c`: the `ALFE` entry (`dbz2_compose`, a logo mask); Europe the `ALFP` variant (its language menu, its area bytes), Japan `ALFJ` (as the USA's) |
| Tests | `tests/regress/dragon-ball-z-legacy-of-goku-2.json` (`tools/regress.py dragon-ball-z-legacy-of-goku-2`) |
| Status | testing (branch `game/dragon-ball-z-legacy-of-goku-2`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list dragon-ball-z-legacy-of-goku-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7). The model is [The Legacy of Goku](dragon-ball-z-legacy-of-goku.md) (a screen of the game's own, opened on the hidden copy about once a second while walking about; the last picture kept where it can't be opened), but this game has something the first didn't: the Scouter's map of the area you're in (R), which follows you. That's what a two-screen GBA would have shown, so it's the second screen here, with the pause menu's Status page only until you have the Scouter.

## Rules

**DBZ2-1. Logo.** On the title, the file menu (Select Game), the options, the intro and the opening's story text, the second screen shows the logo: DRAGON BALL Z and THE LEGACY OF GOKU II from the title screen, cut out of the picture they're painted on (none of Goku, the Androids or the sky; not the START / OPTIONS box, the ® and ™, or the copyright line).

**DBZ2-2. The area map.** Walking about, once you have the Scouter, the second screen is the Scouter's map of the area (R) as the game draws it: the scouter frame, the area's shape, your dot and the other marks, centred on you as the game centres it when it opens. It's opened in the background on a silent copy of the game about once a second (and as soon as you're in a new area, GEN-6), so your dot moves as you do; your game is never paused. Its button prompts are left out (DBZ2-8).

**DBZ2-3. The Status page before the Scouter.** Walking about where R doesn't open a map (before the Scouter, from the start of the game until West City), the second screen is the pause menu's Status page (START) instead, as the game draws it: the fighter you're playing, level, HP, EP, STR, POW, END, EXP and NEXT, under the STATUS tab; its button prompts are left out (DBZ2-8). Each refresh tries the map first, so the map takes over as soon as you have the Scouter.

**DBZ2-4. The HUD stays.** The HP and EP bars, the attack icon and the Super Saiyan bar stay on the game screen, where the game draws them. (Unlike The Legacy of Goku, neither the map nor the Status page has a free place for them, and the Status page shows the same figures.)

**DBZ2-5. Elsewhere, the last picture.** In a conversation, at a save point's menu (Save / Switch Characters), in the game's own pause menu, map, Dragon Radar or any other screen of its own, and wherever neither R nor START opens anything (a scene played on the field), the second screen keeps the last picture; before the first, the logo.

**DBZ2-6. Flying.** Flying over the world (after reading a World Map sign), the small map of the world at the game screen's top right (its outline, your arrow and the places' marks) moves to the second screen, twice as big and in the middle, on the plain dark background; the game screen goes on without it, the haze over the horizon below it as the game draws it. The map of the area you left is forgotten: landing in an area brings that area's map. (An exception to the second screen being drawn at the game's size, as the racing games' course maps are: the small map is hard to read at its own size.) The world map's frame is grown from what's drawn: if your arrow goes past the outline's edge, the picture moves a few pixels to stay in the middle.

**DBZ2-7. One screen.** With one screen, the game plays as it is.

**DBZ2-8. No button prompts.** The second screen can't be scrolled or paged, so the prompts for doing that are left out: on the area map, the four blinking arrows round its edge (the d-pad scrolls it) and the L and R buttons in the scouter frame's top corners (painted the frame's own dark green; its bright lines stay as they are, so a line can stop short there); on the Status page, the arrows in its top corners and in the panel's left side (painted the panel's own blue), and the L JOURNAL and ITEMS R tabs either side of STATUS (the stars behind them instead; STATUS and its orange line stay, the line drawn whole where the L and R icons covered its ends, from its own pixels beside them).

## Not covered yet

Tried in the USA release with a save at the Northern Mountains (Gohan at level 39, all five fighters), a new game's first area (Trunks, before the Scouter), the debug menu's Map Test in zones 1 to 14 (area 1), a conversation, a save point's menu, the pause menu, the Scouter's map and Dragon Radar, and flying from the Northern Wastelands; the European and Japanese releases from a new game's first area. Boss fights, the Hyperbolic Time Chamber, the Cell Games, Hercule, the game's ending and its Game Over aren't tried yet; in a situation that keeps the field's display setup they get the same treatment, untried.

## Facts found so far (for sessions)

| What | Where / value (USA; Japan the same) |
|---|---|
| Walking about | DISPCNT `0x1F40` (BG0CNT `0x1C83` or `0x1C81` by area); conversations and the save point's menu too (boxes on BG0 / BG1) |
| Zone, area | `0x03002080` (zone, 0 on the title, file menu and intro), `0x03002081` (area: 0 there, 99 flying); Europe `0x03000F0C` / `0x03000F0D` |
| The Scouter's map (R) | DISPCNT `0x1D40`, BLDALPHA counts `0x0000` to `0x0010` as it fades in (settled about 37 frames after R); BG0 the frame, BG2 the area; the marks and the frame's arrows and readouts sprites (palette 0); R again: the whole world; B closes it |
| Its scroll arrows | sprites at 4,72 and 228,72 (8x16) and 112,4 and 112,148 (16x8), priority 0, palette 0 |
| Pause menu (START) | DISPCNT `0x0E00`; opens on Status (tabs Journal, Status, Items, Options with L / R); BG1 the tab bar and L / R arrows, BG2 the panel and text, BG3 the stars; B closes it |
| HUD | three sprites (OAM 0-2) at 8,8 / 40,8 / 56,8, priority 0, palette 0, tiles moving by area; gone in conversations |
| SELECT | the Dragon Radar |
| L / B | the attack (icon at the HUD's right) / Super Saiyan |
| Flying | DISPCNT `0x1741` (mode 1), the world in perspective with an inset map at the top right: its outline BG0 rows 4-49, x 168-230 (blended, BLDCNT `0x1641`, BLDALPHA `0x1006`), BG0's haze over the horizon from row 56; its marks 8x8 sprites (your arrow, a star, pink dots) |
| Prompts | the map's L and R: BG0 tiles in the frame's top corners (greys with a black outline on the frame's (24, 66, 0)); the Status page's: BG1's corner arrows (x 7-24 and 216-234, rows 9-26) and L / R icons under the tab line's ends, BG2's JOURNAL and ITEMS and its UP / DOWN arrows (x 24-39, rows 36-52 and 100-116, on the panel's (107, 181, 222)) |
| Title | DISPCNT `0x0C00`, BG2 alone (the logo painted into the picture); Select Game `0x1E40`; intro `0x0800` |
| Debug menu | hold START on the title with `0x0202B32D` = 2 (Japan `0x0202B371`); Map Test's Zone / Area / Variation (rows 3-5) and A: a field with every fighter |
| A save point's menu | START there is Save (harmless on the hidden copy, which has no battery save) |

## For the merge

Changelog line (under the open version's What's new):

- Dragon Ball Z: The Legacy of Goku II (USA, Europe and Japan) is supported: walking about, the second screen is the Scouter's map of the area you're in, your dot moving as you do (before you have the Scouter, the pause menu's Status page); flying over the world, the game's small world map, twice as big; the logo on the title and the menus.

`wiki/Supported-games.md`, the table row and the section:

| Dragon Ball Z: The Legacy of Goku II | `ALFE` `ALFP` `ALFJ` | the Scouter's area map (before the Scouter, the Status page); flying, the world map at 2x |

### Dragon Ball Z: The Legacy of Goku II

The USA, European and Japanese (International) releases.

- **Walking about,** the second screen is the **Scouter's map** of the area you're in (R), as the game draws it, centred on you. It's opened in the background in a silent copy of the game about once a second, and as soon as you reach a new area, so your dot moves as you do; your game is never paused. The arrows round its edge (the d-pad scrolls the map) are left out.
- **Before you have the Scouter,** the second screen is the pause menu's **Status page** (START) instead: your fighter's level, HP, EP, stats and experience.
- The HP and EP bars stay on the game screen.
- **Flying over the world,** the small world map at the top right of the game screen moves to the second screen, twice as big, with your arrow on it.
- Button prompts for scrolling and turning pages are left out.
- In conversations, at save points, and in the game's own pause menu, map and Dragon Radar, the second screen keeps its picture. On the title and menus it shows the logo.
- Not tried yet: boss fights, the Hyperbolic Time Chamber, the Cell Games and the ending.

`wiki/Philosophy.md`, its list of current exceptions (after the racing games' course maps):

  - Dragon Ball Z: The Legacy of Goku II's small world map, shown while flying, drawn twice as big;

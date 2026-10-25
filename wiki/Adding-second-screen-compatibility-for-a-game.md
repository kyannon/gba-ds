Every game GBA DS knows is one `MapProfile` entry in [`src/profiles.c`](https://github.com/kyannon/gba-ds/blob/main/src/profiles.c); the fields are documented in [`src/profiles.h`](https://github.com/kyannon/gba-ds/blob/main/src/profiles.h). Adding a game mostly means writing a profile and testing it with the harness. You'll need a host build ([README → Building](https://github.com/kyannon/gba-ds#building)) and the ROM.

There are three kinds of second screen, and a profile can use the parts it needs:

| Kind | Games | What GBA DS does |
|---|---|---|
| **Map capture** | Castlevanias, Metroids, A Link to the Past | Opens the game's own map on a hidden copy of the game and shows it. |
| **Screen snapshot** (`logoOnly` + `snap`) | Tony Hawk's Pro Skater 2 | Copies a screen the game shows (the goal sheet) to the second screen while it's useful. |
| **HUD split** (`hud`) | Mario Kart: Super Circuit; Super Mario Advance 2 (with a map capture); Fire Emblem; Advance Wars | Draws some layers and sprites on the second screen instead of the game screen. |

Every profile also gets a **logo** for the game list and the second screen.

## The basics

```c
{
	.codes = { "A2CE" },                 /* game codes (header offset 0xAC) */
	.title = "Castlevania: Aria of Sorrow",
	...
},
```

- `codes`: the four-letter codes from the ROM header. The harness prints a ROM's code as it loads it.
- `listName`: optional name for the game list instead of the file name.
- **Other regions** reuse the USA profile through the `variants[]` table at the bottom of `profiles.c`: `memShift` is added to every memory address when the region's RAM layout moved (find it by matching the code that reads an address in the two ROMs, or by searching RAM dumps), and a `patch` function changes anything else (an extra language menu at boot, a different title-screen timing).

## The logo

```c
.logo = { .wait = 1700, .layer = 1, .maxY = 160 },
```

GBA DS boots the game silently, waits `wait` frames (optionally `pressStart` then `wait2`, or a button sequence in `steps`), and keeps BG layer `layer` above row `maxY`, transparent where the layer is. `blackIsClear` for a layer with a black fill; `mask` (from `tools/make_logo_masks.py`) when the logo is painted into the whole title picture. Bump `rev` when you change a profile's logo, so cached logos are made again.

Find the layer with the harness: `wait` to the title screen, then `layershot title` writes each layer alone (`title_L0` ... `title_L3`, `title_L4` for sprites). `logo NAME` runs the real extraction.

## Map capture

The hidden copy is given the live game's state, the map button is pressed, and the result is checked before it's shown.

- `open`: the button sequence (`{ K_SELECT, 4 }`: hold Select 4 frames). `open2If` / `open2Delay` / `open2` press a second button once the map is up, when a condition holds (A Link to the Past zooms out the world map).
- `settleMax`, `openWithin`, `stableFrames`, `maxDiff`: how long to wait for the map, and when it counts as settled (consecutive frames with at most `maxDiff` changed pixels, so blinking markers are allowed).
- **Checks** (all must pass, so a title screen or cutscene is never shown):
  - `mem`: memory values that hold only while the map screen is open, read on the hidden copy. Find them with `ramdiff.py` (below).
  - `probes` / `minProbes` / `tolerance`: screen pixels with a fixed colour while the map is up (the frame or background). `bgColor` / `minForeground`: the map isn't empty.
  - `live`: values that must hold on the game you're playing before a capture is tried at all (in gameplay, not in a menu).
  - `menu`: values that mean "back at the title or file menu": the map is forgotten and the logo comes back.
  - `room`: values that change on a room change, which triggers a capture straight away.
- **Crop and clean-up:** `crop_*` (what's shown), and with `map_crop` on: `tight_*`, `glyphs` (lettering to erase), `recolor`, `masks` with `gridX`/`gridY`/`gridCell` (rebuild the map grid), `captureHide` (sprites drawn over the map), `underRect`/`underLayers`/`underHide` (pieces of bezel that give way to the map under them) and `tab`. `captureLayers` keeps only some BG layers in the capture, over black, with `captureHide` always applied (Tony Hawk's Pro Skater 3's goal list without the level behind it).
- **Animation:** `animFrames` × `animStep` frames are recorded so markers blink; `animCycle` and `stillPick` choose the frame for each blinking spot when the map is still (`map_animations = 0`, or `mapStill` for a game whose map is always still: A Link to the Past, Super Mario Advance 2's level list).
- `hideSprites`: sprites hidden on the game screen while a map is shown (the Metroids' mini-map). Sprite rules give a screen box, a tile range and a priority (`-1`: any).
- `frame`: a border from one of the game's menus, for the generated surround (button `steps` from power-on, then a frame of thickness `t` around `x0,y0`–`x1,y1` on BG `layer`). `gridBackground` fills the screen with the map's own grid tile (Metroid). `rate` copies text from another screen onto the map (Circle of the Moon's completion rate).
- `dimWhenUnavailable`: dim the last map while it can't be opened.

## Screen snapshot

For games without a map but with a screen worth keeping (`logoOnly = true`): `snap.show` probes (at least `showMin`, for `stable` frames) say the screen is up; it's copied to the second screen and stays until one of the `revert` screens matches. `snap.hide` leaves sprites out of the copy. `controls` adds the four-face-button picker (Tony Hawk's); `controls.snes` an SNES-layout picker instead (Super Mario Advance 2: face buttons by position, both shoulders as GBA L, with the camera pan direction in RAM set from the shoulder held, where `snes.when` matches).

## HUD split

For games whose HUD is better on the second screen, logo-only or alongside a map capture (Super Mario Advance 2: the HUD in levels, the captured SELECT menu on the world map; the HUD picture wins while it's up). A second renderer receives the same video writes as the game's and draws only the HUD layers and sprites; the game screen draws everything else, so nothing behind the HUD is lost.

- `hud.on`: while all match (I/O registers like DISPCNT at `0x04000000`, or RAM), the split is on. `hud.keep`: while these match (the pause menu), the last HUD picture stays.
- `hud.bgMask` (bit 0 = BG0) and `hud.objs`: what moves.
- `hud.layout`: while these also match (racing, not results; no probes: always), `sign` (a box left on the game screen when nothing else of the HUD is beside it), `map` (scaled up 2× or 1.5×), the `backdrop` (BG layers grabbed above row `backdropRows`, dimmed) and `moves` apply.
- `hud.moves`: `HudMove` boxes measured each frame and placed by an edge or centre (`'L' 'C' 'R'` at `x`, `'T' 'M' 'B'` at `y`), on the second screen or with `toGame` back on the game screen (Mario Kart's item box and place). With `toGame` and no `how` / `vhow`, the box goes back where it was (Super Mario Advance 2's item box, and BG3 below the status bar: message and pause boxes).
- `hud.bgPrioBelow`: instead of `bgMask`, the BG layers below this priority, picked line by line (written for Final Fantasy Tactics Advance's first second screen, which moved its menus between layers; no game uses it now). `hud.effects`: while these match, layers whose tiles mostly use palettes below `hud.effectsPal` stay on the game screen (its spells are drawn on the menu layers, in other palettes than the text boxes).
- `SpriteRule.tagged` and `romPatch`: when the game draws world and HUD sprites from the same tiles and palettes, a ROM patch can tag the HUD ones (attr1 bits 9-11, unused on sprites that aren't rotated) and the rules take only tagged sprites. Final Fantasy Tactics Advance's first second screen (on the branch `archive/ffta-hud`, with `tools/ffta_tag.s` and `tools/make_ffta_tag.sh`) hooked the routine all its sprite lists go through and tagged what it adds, except for its world-drawing systems (animations, weapon swings, damage numbers), found by tracing which code drew each sprite; units and thrown objects are written by other code and are never tagged. Each patch is checked against the ROM's bytes first; if it doesn't go in, the rules apply without the tags.
- `hud.mapCnt`: the HUD is on only while a BG layer has this BGxCNT (written for Final Fantasy Tactics Advance's first second screen: its battle maps); the backdrop is then a still of the game screen without the HUD, in grey with `backdropGrey`. `hud.world`: another such layer (its world map) with its own sprite rules (there: priority 0, by tile), the backdrop being the layers below the HUD's alone.
- `hud.logoBelow`: the second screen is the HUD on black with the logo at half size below it, instead of a backdrop.
- `hud.scene`: another scene with its own sprites and moves, with the logo as the backdrop (Mario Kart's podium).

The second screen is a 480×320 canvas (2× GBA pixels), so moved and scaled pieces stay sharp.

## Tools

### harness

`build/host/harness ROM OUTDIR SCRIPT` runs a ROM headless, frame by frame. One command per line (`#` comments):

| Command | |
|---|---|
| `hold KEYS N` | hold buttons for N frames (`A`, `B`, `L`, `R`, `START`, `SELECT`, `UP`... joined with `+`; `-` for none) |
| `tap KEYS [COUNT]` | press and release (3 frames down, 9 up), COUNT times |
| `wait N` | run N frames |
| `shot NAME` / `seq NAME N STEP` | screenshot / N screenshots STEP frames apart |
| `save NAME` / `load NAME` | save states in OUTDIR (`NAME.ss`); GBA DS's own `.state` files load too when renamed |
| `layershot NAME` | each BG layer and the sprites alone, transparent as magenta |
| `peek ADDR LEN` / `poke ADDR LEN VAL` | read / write memory |
| `dumpram NAME` | EWRAM then IWRAM to `NAME.ram`, for `ramdiff.py` |
| `watch ADDR LEN` then `diff` | what changed in a range since `watch` |
| `map NAME` | run the real map capture against the profile (prints why it failed) |
| `logo NAME` / `frameatlas NAME` | run the logo / border extraction |
| `split BGMASK PRIO` / `split off` / `hudshot NAME` | try a HUD split: BG layers and sprites of one priority |

### The app, headless

```sh
SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy GBADS_WINDOW=1024x1536 GBADS_DEBUG=1 \
  build/host/gba-ds --state-dir=/tmp/st --load-state=race.ss --input-script=test.txt ROM
```

Script lines: `TICK BUTTONS` (`A`, `B+UP`, `-`), `TICK SHOT:file.bmp` (both screens), `TICK TAP:x,y` (fractions of the window), `TICK QUIT`. Check how it looks on both screens before trying it on a device.

### Python helpers

- **`tools/ramdiff.py --on a.ram b.ram --off c.ram d.ram`**: addresses whose value is the same in every "on" dump (map open) and different from all "off" dumps (gameplay). Take dumps in several places of the game to narrow it down.
- **`tools/explore_bits.py a.ram b.ram c.ram ...`**: bytes whose bits are only ever set across an ordered series of dumps (progress flags, collected items).
- **`tools/sheet.py OUT.png [--cols N] [--scale S] FILES...`**: tiles screenshots into a contact sheet.
- **`tools/ra_mock.py`**: a stand-in RetroAchievements server on 127.0.0.1:8099, for testing without an account.

## A typical workflow

1. Play to a point where the map can be opened and make a save state in GBA DS; copy it into the harness's OUTDIR as `NAME.ss` and `load NAME`.
2. With the harness, `hold` the map button, `shot` until the map is up, and note how many frames it takes.
3. `dumpram` with the map open and closed in a few places; `ramdiff.py` gives the `mem` candidates. Check them with `peek`.
4. Pick `probes` from the screenshots: frame or background pixels that never change.
5. Write the profile, then `map NAME` in the harness until captures pass, and try it from a title screen and a cutscene too, where captures must fail.
6. Add `logo`, test the other regions (`variants`), run the app headless, and send it for testing on the device.

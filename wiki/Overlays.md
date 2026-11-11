An overlay is a picture drawn over the space around the game, never over the game itself: a console bezel, or art of your own.

**Overlays** has a screen of its own in the pause menu (Display → Overlays):

- **Overlays**: **Off**, **Generated** (GBA DS's own bezel, below) or **Custom (in /overlays)** (your own PNGs, further down).
- **Which Screen**: **Both**, **Top Only** or **Bottom Only**. It goes by the screens themselves, so when you swap screens the top screen keeps the top screen's bezel (the logo below the picture) and the bottom screen the bottom one's.
- With Generated: **GBA DS Logo** (Show or Hide), **Inner Bezel Color**, **Outer Bezel Color** and **Reset Bezels**, which puts the logo and both colours back as they came.

While you're on that screen, the other screen isn't dimmed, so you see the bezel as it will be.

## GBA DS's own bezel

Out of the box, GBA DS draws a bezel round the picture: a dark grey bezel (`#1A1A1A`) on the navy of the GBA DS logo's outline (`#102060`), with the logo in the bezel by the hinge (below the picture on the top screen, above it on the bottom screen). It's drawn whatever the screen shows, the second screen's logo too.

It's drawn by the app for your screens and the scaling you use, so it fits any device and any setting. Where there's little room (Full Width, or the sides in Pixel Perfect on a 4:3 screen), you see only the part of the bezel that fits, and the logo shrinks or is left out; with Stretch, or Zoom on a 4:3 screen, the picture fills the screen and there's no bezel.

### Picking its colours

Choose **Inner Bezel Color** (round the picture) or **Outer Bezel Color** (outside it). The picker shows the colour on the bezel as you pick it:

- **Touch**: drag in the square for the shade and along the strip beside it for the hue, tap a preset, or tap the hex value to type one.
- **Buttons**: the D-pad moves in the square (left / right: how strong the colour is; up / down: how bright), **L1** / **R1** move along the hue, **X** steps through the presets, **Y** brings up a keypad for a hex value (six digits, or three: `ABC` is `AABBCC`).
- **A** or **OK** keeps it; **B** or **Cancel** leaves the colour as it was.

The settings are saved with your other pause-menu choices; in `gba-ds.cfg` they're `overlays`, `overlay_screens`, `bezel_logo`, `bezel_inner` and `bezel_outer` (see [[Settings]]).

## Your own overlays

Set **Overlays** to **Custom (in /overlays)** and put PNGs in `gba-ds/overlays/` (Android: `GBA DS/overlays/`). A screen with no file of its own gets GBA DS's bezel in its default colours. Each one is drawn over the whole screen, stretched to its size, so make it the screen's size (**1024×768** on the RG DS Plus, **640×480** on the RG DS) and leave it transparent where the picture should show. Like GBA DS's own bezel, your overlays belong to a screen: `top` or `bottom`. Swapping screens doesn't move them, and **Which Screen** decides which screens have one.

A file's name is made of these parts, in this order, joined by dashes; only `<screen>` is needed:

`<CODE>` - `<screen>` - `<scale>` - `<shapes>` `.png`

- `<CODE>`: the game's code, e.g. `A2CE` (Aria of Sorrow, USA), for an overlay for that game only. Each region has its own code: see the table in [[Supported games]].
- `<screen>`: `top` or `bottom`.
- `<scale>`: the scaling of the picture on that screen, for an overlay for that scaling only: `integer` (Pixel Perfect), `fit` (Full Width), `zoom` or `stretch`.
- `<shapes>`: **Screen shapes** (pause menu → Display; experimental), for an overlay for that setting only: `aspect` (Match screen aspect ratios) or `size` (Match screen sizes and aspect ratios). Only looked for while that setting is on and your two screens' shapes differ. With **Keep device aspect ratios**, or two screens of a shape (the RG DS), names with a `<shapes>` part are left alone, so the overlays you have keep working as they are.

For each screen the most specific file found is used, so a game's own overlay beats a global one, and one made for your scaling and screen shapes beats one without them:

| | Top screen | Bottom screen |
|---|---|---|
| this game, this scaling and screen shapes | `<CODE>-top-<scale>-<shapes>.png` | `<CODE>-bottom-<scale>-<shapes>.png` |
| this game, these screen shapes | `<CODE>-top-<shapes>.png` | `<CODE>-bottom-<shapes>.png` |
| this game, this scaling | `<CODE>-top-<scale>.png` | `<CODE>-bottom-<scale>.png` |
| this game | `<CODE>-top.png` | `<CODE>-bottom.png` |
| every game, this scaling and screen shapes | `top-<scale>-<shapes>.png` | `bottom-<scale>-<shapes>.png` |
| every game, these screen shapes | `top-<shapes>.png` | `bottom-<shapes>.png` |
| every game, this scaling | `top-<scale>.png` | `bottom-<scale>.png` |
| every game | `top.png` | `bottom.png` |

For example, on an AYN Thor with Stretch and **Match screen aspect ratios**: on the top screen, `top-stretch-aspect.png` is used if you have it, then `top-aspect.png`, then `top-stretch.png`, then `top.png`.

Overlays named the old way still work: `primary` (the game's screen) and `secondary` (the second screen) in place of `top` and `bottom`, and these follow their picture when you swap screens. At each step, a `top` / `bottom` file is used first.

- On the screen without the game, the overlay is drawn whatever that screen shows: the map, the HUD, the logo, or nothing.
- An overlay is always drawn over its whole screen, whatever the screen shapes setting: make it the screen's own size (1920×1080 for the Thor's top screen), transparent where the picture goes.

Areas to keep transparent, where the game or map picture is (the same for every game, since everything is shown at full GBA size):

| Scaling | RG DS Plus (1024×768) | RG DS (640×480) |
|---|---|---|
| Pixel Perfect (`integer`) | x=32, y=64, 960×640 | x=80, y=80, 480×320 |
| Full Width (`fit`) | x=0, y=43, 1024×682 | x=0, y=27, 640×426 |
| Zoom, Stretch | the whole screen | the whole screen |

**With Screen shapes on** (screens of different shapes, such as the AYN Thor's 1920×1080 top screen over its 1240×1080 bottom one), the wider screen's picture goes in a smaller area first, and its scaling then works inside that area as it would on a whole screen:

- **Match screen aspect ratios** (`aspect`): the largest area of the other screen's shape, centred. On the Thor's top screen: x=340, y=0, 1240×1080 (with Stretch, the picture fills it).
- **Match screen sizes and aspect ratios** (`size`): the other screen's real size, worked out from the two screens' pixels per inch as the system reports them, against the edge nearest the other screen (a top screen's bottom edge), centred across. On the Thor's top screen, with the sizes Android gives (about 367 and 419 pixels per inch): x=417, y=134, 1086×946.

Since the size area depends on what your device reports, check it in `gba-ds.log`: the line `screen shapes: size 1086x946 on screen 0` gives the area's size (screen 0 is the top screen); it's centred across, and at the bottom of a top screen. `screen_ppi` in `gba-ds.cfg` (see [[Settings]]) sets the pixels per inch yourself if they're wrong.

`dist/gba-ds-overlay-templates-preview.png` in the repository shows the areas.

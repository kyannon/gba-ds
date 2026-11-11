The **Overlays** setting (pause menu → Display, or `overlays` in `gba-ds.cfg`) cycles **Show Top Only / Show Bottom Only / Show Both / None**: which of your device's screens have an overlay. It goes by the screens themselves, so when you swap screens the top screen keeps the top screen's bezel (the logo below the picture) and the bottom screen the bottom one's.

An overlay is a picture drawn over the space around the game, never over the game itself: a console bezel, or art of your own.

## GBA DS's own bezel

Out of the box, GBA DS draws a bezel round the picture: a dark grey bezel on the navy of the GBA DS logo's outline, with the logo in the bezel by the hinge (below the picture on the top screen, above it on the bottom screen). With **Show Both**, it's drawn on the second screen whatever that shows, logo and all.

It's made for every screen of the devices GBA DS knows (the Anbernic RG DS and RG DS Plus, the AYN Thor, the Retroid Pocket Duo and Duo Lite, the AYANEO Pocket DS and the Anbernic RG DDS), in every scaling that leaves room round the picture, and with each **Screen shapes** setting. Where there's little room (Full Width, or the sides in Pixel Perfect on a 4:3 screen), you see only the part of the bezel that fits; with Stretch, or Zoom on a 4:3 screen, the picture fills the screen and there's no bezel.

- Your own overlays (below) always win over it.
- With **Match screen sizes and aspect ratios**, the bezel is made for the screen sizes GBA DS knows for your device; if you set `screen_ppi` yourself, the picture may land where no bezel was made, and then there's none.
- **Overlays: None** turns it off; **Show Top Only** or **Show Bottom Only** keeps it on one screen.
- The files are in `gba-ds/overlays/default/` (on Android, inside the app), one for each screen size and picture position; its `README.txt` says which device and setting uses each. They're replaced by every update, so don't edit them there: copy one into `overlays/` under one of the names below and change that.

## Your own overlays

Put PNGs in `gba-ds/overlays/` (Android: `GBA DS/overlays/`), not in its `default/` folder. Each one is drawn over the whole screen, stretched to its size, so make it the screen's size (**1024×768** on the RG DS Plus, **640×480** on the RG DS) and leave it transparent where the picture should show. Your files are named for what's on the screen rather than which screen it is: `primary` for the game screen and `secondary` for the second screen, so they follow the picture when you swap screens. The setting still decides which screens have one: with **Show Top Only**, only the top screen's overlay is drawn, whichever picture is there.

A file's name is made of these parts, in this order, joined by dashes; only `<side>` is needed:

`<CODE>` - `<side>` - `<scale>` - `<shapes>` `.png`

- `<CODE>`: the game's code, e.g. `A2CE` (Aria of Sorrow, USA), for an overlay for that game only. Each region has its own code: see the table in [[Supported games]].
- `<side>`: `primary` (the game screen) or `secondary` (the second screen).
- `<scale>`: that screen's scaling, for an overlay for that scaling only: `integer` (Pixel Perfect), `fit` (Full Width), `zoom` or `stretch`.
- `<shapes>`: **Screen shapes** (pause menu → Display; experimental), for an overlay for that setting only: `aspect` (Match screen aspect ratios) or `size` (Match screen sizes and aspect ratios). Only looked for while that setting is on and your two screens' shapes differ. With **Keep device aspect ratios**, or two screens of a shape (the RG DS), names with a `<shapes>` part are left alone, so the overlays you have keep working as they are.

For each screen the most specific file found is used, so a game's own overlay beats a global one, and one made for your scaling and screen shapes beats one without them:

| | Primary (game screen) | Secondary (second screen) |
|---|---|---|
| this game, this scaling and screen shapes | `<CODE>-primary-<scale>-<shapes>.png` | `<CODE>-secondary-<scale>-<shapes>.png` |
| this game, these screen shapes | `<CODE>-primary-<shapes>.png` | `<CODE>-secondary-<shapes>.png` |
| this game, this scaling | `<CODE>-primary-<scale>.png` | `<CODE>-secondary-<scale>.png` |
| this game | `<CODE>-primary.png` | `<CODE>-secondary.png` |
| every game, this scaling and screen shapes | `primary-<scale>-<shapes>.png` | `secondary-<scale>-<shapes>.png` |
| every game, these screen shapes | `primary-<shapes>.png` | `secondary-<shapes>.png` |
| every game, this scaling | `primary-<scale>.png` | `secondary-<scale>.png` |
| every game | `primary.png` | `secondary.png` |

For example, on an AYN Thor with the game on the top screen, Stretch and **Match screen aspect ratios**: `primary-stretch-aspect.png` is used if you have it, then `primary-aspect.png`, then `primary-stretch.png`, then `primary.png`.

- Map overlays named the old way (`<CODE>-<scale>.png`, `<CODE>.png`) still work, ahead of the global ones.
- With **Show Both**, the second screen's overlay is drawn whatever it shows: the map, the HUD, the logo, or nothing. With only that screen's overlays on, only while a map (or HUD) is showing.
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

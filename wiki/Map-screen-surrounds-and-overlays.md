The **Overlays** setting (pause menu → Display, or `overlays` in `gba-ds.cfg`) cycles **Primary / Secondary / Both / None**. "Primary" is the game screen and "secondary" the second (map) screen, whichever physical screen they're on: swap screens and each overlay moves with its picture.

## Generated map surrounds

**Secondary** turns on the generated map surround (and your own second-screen overlay). The surround fills the space around the map with graphics from the game itself. Nothing is shipped: like the logos, it's extracted from the ROM once in the background and cached in `states/frames/`.

- **Castlevania (all three):** the map's background colour, plus a border from the game's own menus:
  - Aria of Sorrow and Harmony of Dissonance use their name-entry frame;
  - Circle of the Moon uses the bevelled boxes from its file menu.

  In Pixel Perfect scaling the border goes all the way round the map; in Full Width it spans the top and bottom.
- **Metroid: Zero Mission / Fusion, A Link to the Past:** none. These map screens are shown whole, with the game's own frame, so there's nothing to fill.
- **Tony Hawk's Pro Skater 2, Mario Kart: Super Circuit:** none; their second screens are full GBA screens (Mario Kart's has the course's scenery behind it; see [[Supported games]]).

The surround is assembled in the game's own pixel space and scaled together with the map, so it lines up exactly and the LCD grid covers it too. `border_color` (hex, default `000000`) sets the colour behind the map where there's no surround or overlay.

## Your own overlays

Put PNGs in `gba-ds/overlays/` (Android: `GBA DS/overlays/`). Each one is drawn over the whole screen, stretched to its size, so make it the screen's size (**1024×768** on the RG DS Plus, **640×480** on the RG DS) and leave it transparent where the picture should show. The primary (game screen) overlay is a good place for a console bezel.

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
- The secondary overlay is only drawn while a map (or HUD) is showing, not over the logo.
- An overlay is always drawn over its whole screen, whatever the screen shapes setting: make it the screen's own size (1920×1080 for the Thor's top screen), transparent where the picture goes.

Areas to keep transparent, where the game or map picture is (the same for every game, since everything is shown at full GBA size):

| Scaling | RG DS Plus (1024×768) | RG DS (640×480) |
|---|---|---|
| Pixel Perfect (`integer`) | x=32, y=64, 960×640 | x=80, y=80, 480×320 |
| Full Width (`fit`) | x=0, y=43, 1024×682 | x=0, y=27, 640×427 |
| Zoom, Stretch | the whole screen | the whole screen |

**With Screen shapes on** (screens of different shapes, such as the AYN Thor's 1920×1080 top screen over its 1240×1080 bottom one), the wider screen's picture goes in a smaller area first, and its scaling then works inside that area as it would on a whole screen:

- **Match screen aspect ratios** (`aspect`): the largest area of the other screen's shape, centred. On the Thor's top screen: x=340, y=0, 1240×1080 (with Stretch, the picture fills it).
- **Match screen sizes and aspect ratios** (`size`): the other screen's real size, worked out from the two screens' pixels per inch as the system reports them, against the edge nearest the other screen (a top screen's bottom edge), centred across. On the Thor's top screen, with the sizes Android gives (about 367 and 419 pixels per inch): x=417, y=134, 1086×946.

Since the size area depends on what your device reports, check it in `gba-ds.log`: the line `screen shapes: size 1086x946 on screen 0` gives the area's size (screen 0 is the top screen); it's centred across, and at the bottom of a top screen. `screen_ppi` in `gba-ds.cfg` (see [[Settings]]) sets the pixels per inch yourself if they're wrong.

`dist/gba-ds-overlay-templates-preview.png` in the repository shows the areas.

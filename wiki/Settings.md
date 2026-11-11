Settings live in **`gba-ds.cfg`**: `/storage/roms/ports/gba-ds/gba-ds.cfg` on ROCKNIX (commented, with every option), `GBA DS/gba-ds.cfg` on Android (create it, with just the lines you want to change). Lines are `key = value`; lines starting with `#` are ignored. Every option can also be given on the command line with dashes for underscores, e.g. `launch.sh --map-interval=60 --swap`.

Three files are read in order, later ones winning:
1. `gba-ds.cfg`;
2. `states/prefs.cfg`: what you chose in the pause menu (screens swapped, scaling, shader, color correction, LCD ghosting, overlays, the second screen's logo, FPS counter, frame buffer). GBA DS rewrites this file; to change those settings, use the menu, or delete the line from `prefs.cfg`.
3. `states/library.cfg`: the game list's view (list or grid) and, on Android, the ROM folder, the save folder and the saves' type (Settings > Folders).

Updating from the game list (Settings > Updates) keeps your `gba-ds.cfg`. Unzipping a new release by hand replaces it, so keep a copy of your changes first.

## Game list

| Option | Default | |
|---|---|---|
| `rom_dir` | `/storage/roms/gba` (Android: `ROMs/gba`) | Folders to list, subfolders included. Several: separate with `:`. |
| `show_all` | `0` | `1` lists every GBA ROM, not just games with second-screen support. The others play on one screen. |
| `name_tags` | `0` | `1` keeps tags like `(USA)` and `(En,Fr,De)` in game names. |
| `library_view` | `list` | `list` (names) or `grid` (logos). Also switched with X in the list; remembered in `library.cfg`. |
| `save_on_quit` | `1` | Save a state (a resume point) when you leave a game. Also in the pause menu. |
| `quick_bar_hint` | `1` | At a game's start, "Swipe up for quick menu" at the bottom of the touchscreen for 3 seconds (or until you touch it); `0` turns it off. |
| `quick_bar_pinned` | `0` | The quick bar stays up, as with its pin (top right of the bar) on; the pin sets this too. |
| `auto_load` | `1` | Pick up from the resume point when you start a game. Also in the pause menu. (`auto_resume`, from older versions, sets both.) |
| `update_check` | `1` | Look for a new release once a day when the game list opens, and ask before downloading it. The **Automatic checks** switch in Settings > Updates overrides this (kept in `states/update.cfg`). See [[Playing#updates]]. |

## Screens

| Option | Default | |
|---|---|---|
| `layout` | `auto` | `auto` detects the screens. On ROCKNIX's sway, GBA DS makes its window full screen across both outputs and splits it. `span-h` / `span-v`: one window over two screens side by side / stacked. `dual`: one window per display. `single`: one screen; HOME + Y (or the menu's Swap Screens) flips between game and second screen. |
| `swap` | `0` | `1` puts the second screen first (top) and the game second. The menu's Swap Screens and its hotkey (HOME + Y) are remembered in `prefs.cfg` and win over this. |
| `touch_screen` | `second` | `first` or `second`: the screen with the touch panel. The menu and game list are drawn there. |
| `screen_match` | `keep` | **Experimental.** When your two screens' shapes differ (a widescreen top screen over a narrower bottom one, as on the AYN Thor): `keep` draws each screen's picture as its own screen's shape allows; `aspect` gives the wider screen's picture the other screen's shape (black bars at the sides); `size` gives it the other screen's real size too, at the edge nearest the other screen, so a picture that carries on across both (Pokémon Pinball's table) lines up. The pause menu's **Display** page has it as Keep device aspect ratios / Match screen aspect ratios / Match screen sizes and aspect ratios. Does nothing on two screens of a shape. |
| `screen_ppi` | (unset) | `top,bottom`, e.g. `367,419`: your screens' pixels per inch, for `screen_match = size`. GBA DS knows them for the AYN Thor, Retroid Pocket Duo and Duo Lite, AYANEO Pocket DS and Anbernic RG DDS (recognised by their screens' resolutions; the Duo and Duo Lite by the nearer of the two to what Android reports); on other devices it uses what the system reports. Set this if the size match comes out wrong on yours. |
| `top_screen` | `auto` | Android only: which display is the top screen. `auto`: the larger one when they differ in size (the AYN Thor's 1920x1080 screen), else the second display (the RG DS). `app`: the display GBA DS opens on. `second`: the other one. Set it if the game and the second screen come up the wrong way round, or Pokémon Pinball's table is upside down. |
| `sway` | `1` | On ROCKNIX, ask the sway compositor to make the window full screen across both outputs. `0` leaves the window as it is. |
| `touch_map` | `window` | How touch coordinates map: across the whole window, or `screen` (just the touch screen). Try `screen` if taps land in the wrong place. |

## Picture

| Option | Default | |
|---|---|---|
| `scale` / `map_scale` | `integer` | Game screen / second screen: `integer` (pixel perfect: sharp, whole-number scale, 4× on the RG DS Plus, with a small border), `fit` (full width, keeps the shape, stays sharp: scaled up by a whole number on the GPU, then filtered down), `zoom` (fills the height, the left and right edges are cut off), `stretch` (fills the screen, distorts the shape). |
| `shader` | `off` | A pattern over every GBA pixel, on both screens: `off`, `grid` (lines between the pixels, like a GBA screen), `scanlines` (a dark line under each row), `soft` (each pixel a little darker toward its edges), or red, green and blue stripes like a GBA screen's: `subpixels-light` (gentle, for dim screens), `subpixels`, `subpixels-strong` (for bright screens). Each is drawn on the GPU as one pattern made once, so none costs more than the grid did. With 3 or more screen pixels per GBA pixel (the RG DS Plus) the patterns are drawn a screen pixel at a time; with fewer (2× on the RG DS) they're drawn finer, at the screen's own resolution: there `soft` looks like `grid`, and the subpixel stripes don't fit, so they're shown in grey (their texture and darkness, without colour). Before v1.4.12 this was `lcd_grid` (`1` still means `grid`). |
| `lcd_strength` | `35` | How dark the lines of `grid`, `scanlines` and `soft` are, 0-100. |
| `color_correction` | `0` | `1`: the game's colours as a Game Boy Advance's screen showed them (mGBA's GBA Color shader, sRGB, Darken Screen 0.5), on both screens. Also in the pause menu (Display). |
| `lcd_ghosting` | `0` | `1`: each frame of the game is shown half and half with the one before, as a GBA's slow screen blurred them: moving things leave a short trail, and sprites a game flickers every other frame (to make them see-through) look see-through, as they did on a GBA. The game screen only. Also in the pause menu (Display). |
| `smooth` | `0` | Plain bilinear filtering instead of sharp scaling (blurry; usually leave off). |
| `vsync` | `0` | Present frames in step with the display. Off is smoother on these devices. |
| `overlays` | `both` | `top`, `bottom`, `both` or `none`: which screens have an overlay. See [[Overlays]]. |
| `border_color` | `000000` | Colour behind the map image where nothing else is drawn (RRGGBB). |
| `show_fps` | `0` | Frames per second and % of full speed above the game, all the time (also HOME + X). The quick bar shows the frames per second whenever you swipe it up. |
| `match_display` | `0` | **Match Display Rate**: run the game at the display's refresh rate instead of the GBA's 59.73 frames a second, when the two are within 1.5% (a 60 Hz screen: 0.46% faster, sound resampled to match, which you can't hear), and present each frame in step with the display. The GBA's own rate on a 60 Hz screen shows one frame twice about every 3.7 seconds, a small regular stutter; matched, every frame gets exactly one refresh. Also in the pause menu. |
| `rewind` | `1` | Keep a rewind buffer: hold **L2** to go back in time. Also in the pause menu (Extras). |
| `rewind_seconds` | `30` | How far back rewind goes (5-300). The buffer holds the differences between states, about 1 MB a second. |
| `frame_buffer` | `0` | Prepare the next frame while the current one is shown: smoother, one frame (about 17 ms) more input delay. Also in the pause menu. Always on in Pokémon Pinball, which needs it for full speed. |

## Second screen

| Option | Default | |
|---|---|---|
| `map` | `1` | `0` turns the second screen off: every game plays like an ordinary GBA game (the Metroids' corner mini-map comes back). |
| `map_interval` | `60` | Frames (60 a second) between map refreshes: 60 is every second. The map also refreshes right after every room transition. Each refresh runs the hidden copy of the game for a moment on another CPU core, so on a slower device very short intervals can make the sound crackle (the log reports it). |
| `map_crop` | `1` | The Metroid map screens, tidied (labels erased, prompts and minimap removed; see [[Supported games]]), and Lady Sia's pause screen without its menu lines. `0` shows them exactly as the games draw them. |
| `show_logo` | `1` | The game's logo on the second screen when there's nothing else to show (title screens, menus, before the first map). `0` keeps it blank. |
| `map_animations` | `1` | Blinking map markers. `0` shows one still frame, with every marker that blinks showing. A Link to the Past's map is always still that way. |
| `pinball_live` | `1` | Pokémon Pinball: two more copies of the game draw the rest of the table live on both screens. `0` turns them off (for slower devices): the game plays on one screen, with the logo on the other. |

What `map_crop` changes in the Metroid map screens:
- **Zero Mission** keeps its whole pause screen, frame included, and only the lettering is erased: the L and R key letters, SLEEP, the area name, STATUS, and the A on the button beside the minimap. The keys, plates and button stay; the blank plates (magenta) and L/R keys (orange) are recoloured to the dark grey-green of the curved pieces beside the area name. The corner minimap gives way to the map underneath, and its "SELECT / CHANGE AREAS" prompt is covered by a piece of the bezel.
- **Fusion** also keeps its whole pause screen. The L and R key letters, SLEEP, the area name and STATUS are erased, and the plates (which cycle through four colours) are recoloured to the frame's greys. The "A OBJECTIVE" prompt and the row of ticked item icons are removed, and the map grid behind them rebuilt.

## Buttons

| Option | Default | |
|---|---|---|
| `ab_swap` | `0` | GBA A is the right face button and GBA B the bottom one, like a GBA. `1` flips them. |
| `hotkey` | `home` | The hotkey button, held with another button for a shortcut (see [[Playing#hotkeys]]): `home`, `select`, `start` or `l3`. SELECT or START as the hotkey still reach the game when tapped on their own, and L3 tapped on its own still opens the quick bar. HOME always opens the menu. (`guide` and `back`, the names before v1.5, still work.) |
| `hotkey_quit` | `start` | The button held with the hotkey to quit GBA DS (`select` when the hotkey is START). Each shortcut takes one of `a`, `b`, `x`, `y`, `l`, `r`, `l2`, `r2`, `select`, `start`, `l3`, `up`, `down`, `left`, `right`, or `none` to turn it off. |
| `hotkey_save_state` | `r` | Hotkey + this button saves the quick state (the quick bar's, not the resume point or the menu's slots). |
| `hotkey_load_state` | `l` | Hotkey + this button loads the quick state. |
| `hotkey_quick_menu` | `x` | Hotkey + this button brings up the quick bar, as swiping up does (again: puts it away). |
| `hotkey_swap_screens` | `y` | Hotkey + this button swaps the screens. |
| `hotkey_shader` | `b` | Hotkey + this button toggles the selected shader: off, and back on (the one last picked in the pause menu, or the LCD grid). |
| `home_button` | `-1` | If HOME does nothing: launch once with `GBADS_DEBUG=1` (see [[Troubleshooting]]), press HOME, and put the "raw joystick ... button N" number from `gba-ds.log` here. |
| `menu_combo` | `none` (Android: `select+start`) | `select+start`: SELECT and START pressed together also open the menu. The game doesn't see the pair; either button alone still reaches the game. |
| `fast_forward` | `1` | Hold **R2** to fast-forward. `0`: R2 does nothing. Also in the pause menu (Extras). |
| `ff_speed` | `3` | Fast-forward speed (hold R2). |

## Files and folders

| Option | Default | |
|---|---|---|
| `save_dir` | empty | Battery saves (`.sav`). Empty: next to the ROM. An existing RetroArch `.srm` is copied to `.sav` the first time you play a game, and so is a save next to the ROM when this is another folder. Android: Settings > Folders. |
| `save_ext` | `sav` | The battery saves' extension: `sav`, or `srm` to use RetroArch's own save files in `save_dir` (BananaOS: `srm` in `/data/saves/gba`, set by GBA DS's launcher). An existing save in the other form is copied the first time you play a game. |
| `save_from` | empty | A folder where GBA DS kept its `.sav` saves before (BananaOS: `/data/saves`). A game's save there is carried over the first time you start it, or when it's newer than the one in `save_dir` (that one kept as `.bak`). |
| `state_dir` | empty | Save states, resume points, menu settings and extracted art. Empty: `gba-ds/states`. |
| `screenshot_dir` | `/storage/roms/screenshots` | Where Take Screenshot saves its PNGs. |
| `cheat_dir` | RetroArch's cheat folders | RetroArch's `.cht` folders, searched two levels deep, `:`-separated. GBA DS's own `cheats` folder (`gba-ds/cheats`) is always searched first. |
| `asset_dir` | empty | The folder holding your `logos/` and `overlays/`. Empty: the GBA DS folder. |
| `es_config_dir` | `/storage/.config/emulationstation` | Where **GBA DS in EmulationStation** puts its two files. |

## RetroAchievements

| Option | Default | |
|---|---|---|
| `retroachievements` | `auto` | `auto`: on when RetroAchievements is switched on in EmulationStation with an account signed in (ROCKNIX's `system.cfg`). `1` on, `0` off. |
| `ra_username`, `ra_password` | empty | Use another account than EmulationStation's. After the first sign-in GBA DS keeps a token instead (`states/ra/login.cfg`). |

## Sound and timing

| Option | Default | |
|---|---|---|
| `audio_latency` | `64` | Sound buffer in milliseconds. Raise it if the sound crackles; lower is snappier. |

## Android only

| Option | Default | |
|---|---|---|
| `pin_cores` | `0` | `1` keeps the game thread on a CPU core of its own. It made no difference on the RG DS Plus; there for testing. |

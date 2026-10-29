Settings live in **`gba-ds.cfg`**: `/storage/roms/ports/gba-ds/gba-ds.cfg` on ROCKNIX (commented, with every option), `GBA DS/gba-ds.cfg` on Android (create it, with just the lines you want to change). Lines are `key = value`; lines starting with `#` are ignored. Every option can also be given on the command line with dashes for underscores, e.g. `launch.sh --map-interval=60 --swap`.

Three files are read in order, later ones winning:
1. `gba-ds.cfg`;
2. `states/prefs.cfg`: what you chose in the pause menu (screens swapped, scaling, LCD grid, overlays, the second screen's logo, FPS counter, frame buffer). GBA DS rewrites this file; to change those settings, use the menu, or delete the line from `prefs.cfg`.
3. `states/library.cfg`: the game list's view (list or grid) and, on Android, the ROM folder.

Updating from the game list (Settings > Updates, Linux) keeps your `gba-ds.cfg`. Unzipping a new release by hand replaces it, so keep a copy of your changes first.

## Game list

| Option | Default | |
|---|---|---|
| `rom_dir` | `/storage/roms/gba` (Android: `ROMs/gba`) | Folders to list, subfolders included. Several: separate with `:`. |
| `show_all` | `0` | `1` lists every GBA ROM, not just games with second-screen support. The others play on one screen. |
| `name_tags` | `0` | `1` keeps tags like `(USA)` and `(En,Fr,De)` in game names. |
| `library_view` | `list` | `list` (names) or `grid` (logos). Also switched with X in the list; remembered in `library.cfg`. |
| `save_on_quit` | `1` | Save a state (a resume point) when you leave a game. Also in the pause menu. |
| `auto_load` | `1` | Pick up from the resume point when you start a game. Also in the pause menu. (`auto_resume`, from older versions, sets both.) |
| `update_check` | `1` | Linux: look for a new release once a day when the game list opens, and ask before downloading it. The **Automatic checks** switch in Settings > Updates overrides this (kept in `states/update.cfg`). See [[Playing#updates-linux]]. |

## Screens

| Option | Default | |
|---|---|---|
| `layout` | `auto` | `auto` detects the screens. On ROCKNIX's sway, GBA DS makes its window full screen across both outputs and splits it. `span-h` / `span-v`: one window over two screens side by side / stacked. `dual`: one window per display. `single`: one screen; L3 flips between game and second screen. |
| `swap` | `0` | `1` puts the second screen first (top) and the game second. The menu's Swap Screens and L3 are remembered in `prefs.cfg` and win over this. |
| `touch_screen` | `second` | `first` or `second`: the screen with the touch panel. The menu and game list are drawn there. |
| `sway` | `1` | On ROCKNIX, ask the sway compositor to make the window full screen across both outputs. `0` leaves the window as it is. |
| `touch_map` | `window` | How touch coordinates map: across the whole window, or `screen` (just the touch screen). Try `screen` if taps land in the wrong place. |

## Picture

| Option | Default | |
|---|---|---|
| `scale` / `map_scale` | `integer` | Game screen / second screen: `integer` (pixel perfect: sharp, whole-number scale, 4× on the RG DS Plus, with a small border), `fit` (full width, keeps the shape, stays sharp: scaled up by a whole number on the GPU, then filtered down), `zoom` (fills the height, the left and right edges are cut off), `stretch` (fills the screen, distorts the shape). |
| `lcd_grid` | `0` | A pixel grid like a GBA screen's, on both screens. Drawn on the GPU, so it costs almost nothing. It adapts to the screen: with 3 or more screen pixels per GBA pixel (the RG DS Plus) each line is one screen pixel; with fewer (2× on the RG DS) the lines are drawn finer, at the screen's own resolution, so the picture stays sharp. |
| `lcd_strength` | `35` | How dark the grid lines are, 0-100. |
| `smooth` | `0` | Plain bilinear filtering instead of sharp scaling (blurry; usually leave off). |
| `vsync` | `0` | Present frames in step with the display. Off is smoother on these devices. |
| `overlays` | `both` | `primary`, `secondary`, `both` or `none`: see [[Map screen surrounds and overlays]]. |
| `border_color` | `000000` | Colour behind the map image where nothing else is drawn (RRGGBB). |
| `show_fps` | `0` | Frames per second and % of full speed above the game, all the time (also HOME + X). The quick bar shows the frames per second whenever you swipe it up. |
| `match_display` | `0` | **Match Display Rate**: run the game at the display's refresh rate instead of the GBA's 59.73 frames a second, when the two are within 1.5% (a 60 Hz screen: 0.46% faster, sound resampled to match, which you can't hear), and present each frame in step with the display. The GBA's own rate on a 60 Hz screen shows one frame twice about every 3.7 seconds, a small regular stutter; matched, every frame gets exactly one refresh. Also in the pause menu. |
| `rewind` | `1` | Keep a rewind buffer: hold **L2** to go back in time. Also in the pause menu (Extras). |
| `rewind_seconds` | `30` | How far back rewind goes (5-300). The buffer holds the differences between states, about 1 MB a second. |
| `frame_buffer` | `0` | Prepare the next frame while the current one is shown: smoother, one frame (about 17 ms) more input delay. Also in the pause menu. |

## Second screen

| Option | Default | |
|---|---|---|
| `map` | `1` | `0` turns the second screen off: every game plays like an ordinary GBA game (the Metroids' corner mini-map comes back). |
| `map_interval` | `60` | Frames (60 a second) between map refreshes: 60 is every second. The map also refreshes right after every room transition. Each refresh runs the hidden copy of the game for a moment on another CPU core, so on a slower device very short intervals can make the sound crackle (the log reports it). |
| `map_crop` | `1` | The Metroid map screens, tidied (labels erased, prompts and minimap removed; see [[Supported games]]). `0` shows them exactly as the games draw them. |
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
| `hotkey` | `guide` | The button that opens the menu and is the modifier for the HOME + button shortcuts: `guide` (HOME) or `back` (SELECT; then SELECT alone still reaches the game when tapped). |
| `home_button` | `-1` | If HOME does nothing: launch once with `GBADS_DEBUG=1` (see [[Troubleshooting]]), press HOME, and put the "raw joystick ... button N" number from `gba-ds.log` here. |
| `menu_combo` | `none` (Android: `select+start`) | `select+start`: SELECT and START pressed together also open the menu. The game doesn't see the pair; either button alone still reaches the game. |
| `ff_speed` | `3` | Fast-forward speed (hold R2). |

## Files and folders

| Option | Default | |
|---|---|---|
| `save_dir` | empty | Battery saves (`.sav`). Empty: next to the ROM. An existing RetroArch `.srm` is copied to `.sav` the first time you play a game. |
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

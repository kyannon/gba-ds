Settings live in **`gba-ds.cfg`**: `/storage/roms/ports/gba-ds/gba-ds.cfg` on ROCKNIX (commented, with the main options; the rest are below), `GBA DS/gba-ds.cfg` on Android (create it, with just the lines you want to change). Lines are `key = value`; lines starting with `#` are ignored. Every option can also be given on the command line with dashes for underscores, e.g. `launch.sh --map-interval=60 --swap`.

Most of these can also be changed in the app: **Settings** in the game list (all games), kept in `states/prefs.cfg`. A game can have settings of its own too: lines in `states/<game>.cfg` (the ROM's file name without its extension, `Golden Sun (USA).cfg`), put over the others while that game runs. They can hold the options that make sense for one game: `swap`, `scale`, `map_scale`, `smooth`, `shader`, `lcd_strength`, `color_correction`, `lcd_ghosting`, `overlays`, `overlay_screens`, `bezel_logo`, `bezel_inner`, `bezel_outer`, `border_color`, `frame_buffer`, `match_display`, `screen_match`, `screen_placement`, `map`, `map_interval`, `custom_logos`, `pinball_live`, `rewind`, `rewind_seconds`, `fast_forward`, `ff_speed`, `ff_mute`, `ab_swap`, `show_fps`, `audio_latency`, `save_on_quit`, `auto_load`, `left_stick_dpad`, `turbo_rate`, `rumble_strength` and `button_right`, `button_bottom`, `button_top`, `button_left`, `button_l`, `button_r`, `button_dpad_up`, `button_dpad_down`, `button_dpad_left`, `button_dpad_right`. Folders, accounts, updates and the like are for all games, and are ignored there. An option given on the command line wins over it.

Four files are read in order, later ones winning:
1. `gba-ds.cfg`;
2. `states/prefs.cfg`: what you chose in the pause menu (screens swapped, scaling, shader, color correction, LCD ghosting, overlays and the bezel, frame buffer, match display rate, screen shapes, screen placement, rewind, fast forward and its speed, save on quit, auto-load, the quick bar's pin) and the game list's Settings > Performance. GBA DS rewrites this file; to change those settings, use the menu, or delete the line from `prefs.cfg`.
3. `states/library.cfg`: the game list's view (list or grid) and, on Android, the ROM folder, the save folder and the saves' type (Settings > Folders);
4. `states/hotkeys.cfg`: what you chose in Settings > Hotkeys.

Updating from the game list (Settings > Updates) keeps your `gba-ds.cfg`. Unzipping a new release by hand replaces it, so keep a copy of your changes first.

In the game list's Settings > System, **Reset settings to defaults** puts every setting back to its default (folders, accounts, hotkeys and each game's own settings stay) and **Delete per-game settings** removes every game's own settings file (`states/<game>.cfg`). Both ask "Are you sure?" first.

## Game list

| Option | Default | |
|---|---|---|
| `rom_dir` | `/storage/roms/gba` (Android: `ROMs/gba`) | Folders to list, subfolders included. Several: separate with `:`. |
| `show_all` | `0` | `1` lists every GBA ROM, not just games with second-screen support. The others play on one screen. The **Supported** / **All** button in the game list switches it. |
| `language` | `auto` | The interface's language: `auto` (the system's), or `en`, `es`, `de`, `fr`, `it`, `pt`, `ja`, `ko`, `zh_CN`, `zh_TW`. In Settings > System > Language (its top row), for all games. Text that isn't translated stays in English. Asked the first time GBA DS starts after it's installed (`language_asked = 1` in `states/prefs.cfg` once answered). See [[Languages]]. |
| `name_tags` | `0` | `1` keeps tags like `(USA)` and `(En,Fr,De)` in game names. |
| `theme` | `dark` | **Light/dark mode**: the look of the menus' buttons: `dark` (dark grey keys with a heavy edge) or `light` (light grey, like the web player's). The sun / moon button in the game list switches it too. In Settings > Display. For all games (not a game's own file). |
| `accent` | `purple` | **Color**: the menus' color for what's selected, the edges and glows, the panels' tint and the highlight on chosen values and pills: `purple`, `blue`, `teal`, `green`, `red`, `orange` or `pink`. Each has a dark and a light version, made to read in either mode. In Settings > Display. For all games. |
| `library_view` | `list` | `list` (names), `grid` (two columns of logos) or `grid3` (three columns). Also switched with X in the list; remembered in `library.cfg`. |
| `save_on_quit` | `1` | Save a state (a resume point) when you leave a game. Also in the pause menu. |
| `quick_bar_hint` | `1` | At a game's start, "Swipe up for quick menu" at the bottom of the touchscreen for 3 seconds (or until you touch it); `0` turns it off. |
| `quick_bar_pinned` | `0` | The quick bar stays up, as with its pin (top right of the bar) on; the pin sets this too. |
| `auto_load` | `1` | Pick up from the resume point when you start a game. Also in the pause menu. (`auto_resume`, from older versions, sets both.) |
| `gba_bios` | `0` | `1` runs games with your own GBA BIOS (`bios/gba_bios.bin` in the data folder) instead of the built-in one; if the file isn't a usable BIOS, the built-in one is used. Set by Settings > System > GBA BIOS in the game list (kept in `library.cfg`, which only switches it on when the file is there). See [[Playing#a-state-saved-with-a-gba-bios]]. |
| `show_rom_path` | `0` | `1` to `4` show the selected game's full path and file name under the Play / Quit hints on the game list's other screen, its letters at 1x (small) to 4x their size; `0` is off. Set by Settings > System > Show ROM Path on Game List in the game list (kept in `library.cfg`). |
| `update_check` | `1` | Look for a new release once a day when the game list opens, and ask before downloading it. The **Automatic checks** switch in Settings > Updates overrides this (kept in `states/update.cfg`). See [[Playing#updates]]. |

## Screens

| Option | Default | |
|---|---|---|
| `layout` | `auto` | `auto` detects the screens. On ROCKNIX's sway, GBA DS makes its window full screen across both outputs and splits it. `span-h` / `span-v`: one window over two screens side by side / stacked. `dual`: one window per display. `single`: one screen; HOME + D-pad up (or the menu's Swap Screens) flips between game and second screen. |
| `brightness_top`, `brightness_bottom` | `100` | How bright each screen is, `20` to `100` (steps of 10). It darkens GBA DS's picture only: it's separate from the device's own brightness, which it doesn't change or exceed. The pause menu's Screens tab sets them per game or for all games. |
| `swap` | `0` | `1` puts the second screen first (top) and the game second. The menu's Swap Screens and its hotkey (HOME + D-pad up) are remembered in `prefs.cfg` and win over this. |
| `touch_screen` | `second` | `first` or `second`: the screen with the touch panel. The menu and game list are drawn there. |
| `screen_match` | `keep` | **Experimental.** When your two screens' shapes differ (a widescreen top screen over a narrower bottom one, as on the AYN Thor): `keep` draws each screen's picture as its own screen's shape allows; `aspect` gives the wider screen's picture the other screen's shape (black bars at the sides); `size` gives it the other screen's real size too, at the edge nearest the other screen, so a picture that carries on across both (Pokémon Pinball's table) lines up. The pause menu's **Display** page has it as Keep device aspect ratios / Match screen aspect ratios / Match screen sizes and aspect ratios. Does nothing on two screens of a shape. |
| `single_layout` | `off` | With one display (a tablet, a phone, a desktop window): `off` is the game alone, the second screen a flip away (Swap Screens); `side` puts both screens on the display side by side, `stacked` one above the other, `auto` side by side on a wide display and stacked on a tall one. Settings > Screens has it as **One display**. Not used with two displays. Where the game goes is **Screens** / Swap Screens, as with two screens. |
| `pad_cover` | `auto` | A controller that folds over the bottom of a phone held upright, such as the 8BitDo FlipPad: the screens (both stacked, the game alone or the pause menu) move up into the part of the display above it. `auto` while one is plugged in (known by its name), `on` always (a controller of another name), `off` never. Not used with two displays or with the phone held wide. Settings > Screens has it as **Controller over the screen**. |
| `pad_cover_mm` | `64` | How much of the display such a controller covers, in millimetres from the bottom edge (no menu row). |
| `single_size` | `equal` | With both screens on one display: the second screen's share of it, `equal` (half), `medium` (40 %) or `small` (30 %); the game gets the rest. Settings > Screens: **Second screen size**. |
| `screen_placement` | `center` | With two screens: `center` puts each screen's picture in the middle of its screen; `no_gap` puts the top screen's picture against its bottom edge and the bottom screen's against its top, as close together as they go. Only moves pictures that don't fill the screen's height (Pixel Perfect, Full Width). The pause menu's **Display** page has it as **Screen Placement**. |
| `screen_ppi` | (unset) | `top,bottom`, e.g. `367,419`: your screens' pixels per inch, for `screen_match = size`. GBA DS knows them for the AYN Thor, Retroid Pocket Duo and Duo Lite, AYANEO Pocket DS and Anbernic RG DDS (recognised by their screens' resolutions; the Duo and Duo Lite by the nearer of the two to what Android reports); on other devices it uses what the system reports. Set this if the size match comes out wrong on yours. |
| `top_screen` | `auto` | Android only: which display is the top screen. `auto`: the larger one when they differ in size (the AYN Thor's 1920x1080 screen), else the second display (the RG DS). `app`: the display GBA DS opens on. `second`: the other one. Set it if the game and the second screen come up the wrong way round, or Pokémon Pinball's table is upside down. |
| `sway` | `1` | On ROCKNIX, ask the sway compositor to make the window full screen across both outputs. `0` leaves the window as it is. |
| `touch_map` | `window` | How touch coordinates map: across the whole window, or `screen` (just the touch screen). Try `screen` if taps land in the wrong place. |

## Picture

| Option | Default | |
|---|---|---|
| `scale` / `map_scale` | `integer` | Game screen / second screen: `integer` (pixel perfect: sharp, whole-number scale, 4× on the RG DS Plus, with a small border), `fit` (full width, keeps the shape, stays sharp: scaled up by a whole number on the GPU, then filtered down), `zoom` (fills the height, the left and right edges are cut off), `stretch` (fills the screen, distorts the shape), `edges` (Edge stretch: fills the screen like `stretch`, but stretches the edges more than the middle, so what you look at keeps closer to its shape; across on a wide screen, up and down on a 4:3 one such as the RG DS Plus). |
| `shader` | `off` | A pattern over every GBA pixel, on both screens: `off`, `grid` (lines between the pixels, like a GBA screen), `scanlines` (a dark line under each row), `soft` (each pixel a little darker toward its edges), or red, green and blue stripes like a GBA screen's: `subpixels-light` (gentle, for dim screens), `subpixels`, `subpixels-strong` (for bright screens). Each is drawn on the GPU as one pattern made once, so none costs more than the grid did. With 3 or more screen pixels per GBA pixel (the RG DS Plus) the patterns are drawn a screen pixel at a time; with fewer (2× on the RG DS) they're drawn finer, at the screen's own resolution: there `soft` looks like `grid`, and the subpixel stripes don't fit, so they're shown in grey (their texture and darkness, without colour). Before v1.4.12 this was `lcd_grid` (`1` still means `grid`). |
| `lcd_strength` | `35` | How dark the lines of `grid`, `scanlines` and `soft` are, 0-100. |
| `color_correction` | `0` | `1`: the game's colours as a Game Boy Advance's screen showed them (mGBA's GBA Color shader, sRGB, Darken Screen 0.5), on both screens. Also in the pause menu (Display). |
| `lcd_ghosting` | `0` | `1`: each frame of the game is shown half and half with the one before, as a GBA's slow screen blurred them: moving things leave a short trail, and sprites a game flickers every other frame (to make them see-through) look see-through, as they did on a GBA. The game screen only. Also in the pause menu (Display). |
| `gb_size` | `gba` | For Game Boy / Game Boy Color games: `gba` (the 160×144 picture at the GBA's pixel size, in the GBA's 240×160 frame) or `native` (the picture is the whole screen). Per game too. |
| `gb_scale` | `integer` | With `gb_size = native`: `integer` (Pixel Perfect), `fit` or `stretch`. |
| `gb_palette` | `own` | The shades of a monochrome Game Boy game: `own`, `dmg`, `pocket`, `light`, `gray` or `sgb`. Counts from the next start of the game. |
| `gb_dual` | `colour` | Games made for both a Game Boy and a Game Boy Color: `colour` or `mono`. Counts from the next start. |
| `gb_correction` | `same` | Colour correction for Game Boy games: `same` (as `color_correction`), `off`, `gbc` or `gba`. |
| `gb_ghosting` | `same` | Ghosting for Game Boy games: `same` (as `lcd_ghosting`), `off`, `light`, `smart` or `strong`. |
| `gb_shader` | `same` | The pattern over Game Boy games: `same` (as `shader`), `off`, `grid`, `scanlines`, `soft` or `dots` (grid with the Game Boy's shadow). |
| `smooth` | `0` | Scaling filter: how the game's pixels are enlarged. `0` sharp, `1` plain bilinear filtering (blurry), or an upscaling filter that redraws pixel-art edges on the GPU before scaling smoothly to the screen: `scale2x`, `scale4x`, `hq2x`, and the heavier `mmpx`, `xbr-lv2`, `xbr-lv3`, `omniscale`. Every shader counts as Off while one is on, on both screens, and the pause menu greys the Shader rows. The heavier four are for stronger devices: where the graphics chip is known to be too slow for them (such as the RG DS Plus) they're greyed and draw as Sharp, and where it hasn't been tried they work with a warning that they may not run at full speed. All of them need OpenGL ES (the device, Android) or, in the web player, WebGL2 (slower there: each step goes through the browser's graphics chip and back); elsewhere they're greyed and draw as `0`. A greyed choice is still saved, for a device that can run it. |
| `vsync` | `0` | Present frames in step with the display. Off is smoother on these devices. |
| `overlays` | `generated` | `off`, `generated` (GBA DS's bezel round the picture) or `custom` (your own PNGs in `overlays/`). See [[Overlays]]. |
| `overlay_screens` | `both` | Which screens have them: `both`, `top` or `bottom`. |
| `bezel_logo` | `1` | The GBA DS logo in the generated bezel. |
| `bezel_inner` | `1A1A1A` | The generated bezel's colour round the picture (RRGGBB). |
| `bezel_outer` | `102060` | The generated bezel's colour outside that (RRGGBB). |
| `border_color` | `000000` | Colour behind the map image where nothing else is drawn (RRGGBB). |
| `show_fps` | `0` | Frames per second and % of full speed above the game, all the time. The quick bar (HOME + X) shows the frames per second whenever it's up. |
| `match_display` | `0` | **Match Display Rate**: run the game at the display's refresh rate instead of the GBA's 59.73 frames a second, when the two are within 1.5% (a 60 Hz screen: 0.46% faster, sound resampled to match, which you can't hear), and present each frame in step with the display. The GBA's own rate on a 60 Hz screen shows one frame twice about every 3.7 seconds, a small regular stutter; matched, every frame gets exactly one refresh. A game that can't keep up (frames late for their refresh three seconds in a row) goes back to the GBA's rate until it's started again or the setting is turned on again (the menu says "Off for now"). Also in the pause menu. |
| `rewind` | `1` | Keep a rewind buffer: hold **L2** (`rewind_button`) to go back in time. Also in the pause menu (Extras). |
| `rewind_seconds` | `30` | How far back rewind goes (5-300). The buffer holds the differences between states, about 1 MB a second. |
| `frame_buffer` | `0` | Prepare the next frame while the current one is shown: smoother, one frame (about 17 ms) more input delay. Also in the pause menu. Always on in the pinball games' two-screen tables (Pokémon Pinball, Sonic Pinball Party, Muppet Pinball Mayhem), which need it for full speed. |

## Second screen

| Option | Default | |
|---|---|---|
| `map` | `1` | `0` turns the second screen off: every game plays like an ordinary GBA game (the Metroids' corner mini-map comes back). |
| `map_interval` | `60` | Frames (60 a second) between map refreshes: 60 is every second. The map also refreshes right after every room transition. Each refresh runs the hidden copy of the game for a moment on another CPU core, so on a slower device very short intervals can make the sound crackle (the log reports it). |
| `logo_folder` | empty | A folder of scraped logos (EmulationStation's marquees, Skraper's wheels), found by the ROM's file name whatever the details in brackets (`Golden Sun.png`, `Golden Sun (USA)-marquee.jpg`...), PNG, JPG or WebP. Used instead of the logo GBA DS takes from the game, at its height. See [Playing](Playing#logos). |
| `custom_logos` | `pixelated` | Your own and scraped logos: `pixelated` shrinks them to the GBA's pixels, drawn like the game's own; `full` keeps their detail, at the same size; `off` doesn't use them (the logo GBA DS takes from the game instead). Also in the pause menu (Display > Custom Logos). |
| `pinball_live` | `1` | Pokémon Pinball, Sonic Pinball Party and Muppet Pinball Mayhem: two more copies of the game draw the rest of the table live on both screens. `0` turns them off (for slower devices): the game plays on one screen, with the logo on the other. |
| `toggle` | `0` | `1` lets you turn the second screen by hand in the games that have pages or views to turn: a status screen's pages, the next party member, the map or the party screen. HOME + A (`hotkey_toggle`) does it, and so does a small button on the second screen (`toggle_button`). Nothing changes in the game; with it off, every game shows what it always did. |
| `auto_rotate` | `1` | With `toggle` on: `1` keeps timed pages turning by themselves, a press moving them on at once; `0` keeps a page until you press. Only used when `toggle` is on. |
| `toggle_button` | `left` | Where the toggle button sits on the second screen: `left`, `center`, `right` or `off` (the hotkey still works). It isn't shown in games with nothing to turn, is dim while the second screen has nothing to turn to (the logo, a one-page picture), and is only drawn on a screen that takes touch. Turning a picture that the game has to find first fades the picture to black until it's ready. Only used when `toggle` is on. |

How the Metroid map screens are tidied:
- **Zero Mission** keeps its whole pause screen, frame included, and only the lettering is erased: the L and R key letters, SLEEP, the area name, STATUS, and the A on the button beside the minimap. The keys, plates and button stay; the blank plates (magenta) and L/R keys (orange) are recoloured to the dark grey-green of the curved pieces beside the area name. The corner minimap gives way to the map underneath, and its "SELECT / CHANGE AREAS" prompt is covered by a piece of the bezel.
- **Fusion** also keeps its whole pause screen. The L and R key letters, SLEEP, the area name and STATUS are erased, and the plates (which cycle through four colours) are recoloured to the frame's greys. The "A OBJECTIVE" prompt and the row of ticked item icons are removed, and the map grid behind them rebuilt.

## Buttons

| Option | Default | |
|---|---|---|
| `left_stick_dpad` | `1` | `0`: the left stick does nothing in games (it still moves around the menus). Settings > Controls. |
| `button_right`, `button_bottom`, `button_top`, `button_left`, `button_l`, `button_r`, `button_dpad_up`, `button_dpad_down`, `button_dpad_left`, `button_dpad_right` | by `ab_swap` | What each button gives the game: `a`, `b`, `l`, `r`, `up`, `down`, `left`, `right`, `start`, `select`, `turbo_a`, `turbo_b` or `none`. By default the right and bottom face buttons are GBA A and B, the top and left ones nothing, and L, R and the D-pad give their own GBA buttons. A game's own choices in the pause menu (Controls tab) replace them for that game. Settings > Controls. |
| `turbo_rate` | `15` | How many times a second Turbo A and Turbo B press: `10`, `15` or `20`. |
| `rumble_strength` | `100` | How hard the device's motor runs in games that rumble, 0 to 100 (0 turns it off). Rumble is on for every game that has the cartridge's own rumble: Drill Dozer, WarioWare: Twisted and romhacks with a rumble patch (the Pokémon Pinball: Ruby & Sapphire rumble patch, for one: turn Rumble on in the game's own Options, and after switching the patch on, use Restart Game once, since a state saved before the patch was on hasn't set its rumble up; some other games have an on/off option of their own in their menus too). The Game Boy Player's rumble (the unpatched Mario & Luigi, Pokémon Pinball and Super Mario Advance 4) isn't supported yet. It goes to a controller's motor if one is connected, else to the device's own. Settings > Controls; it buzzes once when you change it. |
| `ab_swap` | `0` | GBA A is the right face button and GBA B the bottom one, like a GBA. `1` flips them. A game's own choices in the pause menu (Controls tab, see [Playing](Playing#changing-the-face-buttons-for-a-game)) replace this for that game. |
| `hotkey` | `home` | The hotkey button, held with another button for a shortcut (see [[Playing#hotkeys]]): `home`, `select`, `start` or `l3`. SELECT or START as the hotkey still reach the game when tapped on their own, and L3 tapped on its own still opens the quick bar. HOME always opens the menu. (`guide` and `back`, the names before v1.5, still work.) |
| `hotkey_quit` | `start` | The button held with the hotkey to close GBA DS (`select` when the hotkey is START). Each shortcut takes one of `a`, `b`, `x`, `y`, `l`, `r`, `l2`, `r2`, `select`, `start`, `l3`, `up`, `down`, `left`, `right`, or `none` to turn it off. |
| `hotkey_close_game` | `b` | The button held with the hotkey to close the game and go back to the game list. When GBA DS was started straight into a game (from a launcher or a frontend, so there is no game list), it closes GBA DS, the same as `hotkey_quit`. |
| `hotkey_save_state` | `r` | Hotkey + this button saves the quick state (the quick bar's, not the resume point or the menu's slots). |
| `hotkey_load_state` | `l` | Hotkey + this button loads the quick state. |
| `hotkey_quick_menu` | `x` | Hotkey + this button brings up the quick bar, as swiping up does (again: puts it away). |
| `hotkey_swap_screens` | `up` | Hotkey + this button swaps the screens. |
| `hotkey_shader` | `down` | Hotkey + this button toggles the selected shader: off, and back on (the one last picked in the pause menu, or the LCD grid). |
| `hotkey_toggle` | `a` | Hotkey + this button turns the second screen's page or view by hand (see `toggle` under Second screen). |
| `hotkey_rewind_toggle` | `none` | Hotkey + this button starts rewinding until it's pressed again (or `none`). |
| `hotkey_fast_forward_toggle` | `none` | Hotkey + this button starts fast-forwarding until it's pressed again (or `none`). |
| `rewind_button` | `l2` | The button held to rewind, on its own or with the hotkey button (any of the buttons above but HOME and the hotkey button, or `none`). The game doesn't get it. |
| `fast_forward_button` | `r2` | The button held to fast-forward, as `rewind_button`. |
| `home_button` | `-1` | If HOME does nothing: launch once with `GBADS_DEBUG=1` (see [[Troubleshooting]]), press HOME, and put the "raw joystick ... button N" number from `gba-ds.log` here. |
| `menu_combo` | `none` (Android: `select+start`) | `select+start`: SELECT and START pressed together also open the menu. The game doesn't see the pair; either button alone still reaches the game. |
| `fast_forward` | `1` | Hold **R2** (`fast_forward_button`) to fast-forward. `0`: R2 does nothing. Also in the pause menu (Extras). |
| `ff_speed` | `3` | Fast-forward speed (hold R2): how many times as fast, or `max` for as fast as the device can. Fast Forward in the pause menu goes through 2 to 5 and Max. |
| `ff_mute` | `1` | Mute while fast forwarding: no sound while the game is fast-forwarded. `0`: the sped-up sound plays. In Settings > Speed and sound and the pause menu's settings, below Fast forward. |

## Files and folders

| Option | Default | |
|---|---|---|
| `save_dir` | empty | Battery saves (`.sav`). Empty: next to the ROM. Any other folder is made if it isn't there, so you can keep saves where you sync them to other devices (for example `/storage/roms/saves/gba`, with `save_ext = srm` if those devices run RetroArch). An existing RetroArch `.srm` is copied to `.sav` the first time you play a game, and so is a save next to the ROM when this is another folder (the one next to the ROM is left there). Android: Settings > Folders. |
| `save_ext` | `sav` | The battery saves' extension: `sav`, or `srm` to use RetroArch's own save files in `save_dir` (BananaOS: `srm` in `/data/saves/gba`, set by GBA DS's launcher). An existing save in the other form is copied the first time you play a game. |
| `save_from` | empty | A folder where GBA DS kept its `.sav` saves before (BananaOS: `/data/saves`). A game's save there is carried over the first time you start it, or when it's newer than the one in `save_dir` (that one kept as `.bak`). |
| `data_dir` | empty | GBA DS's own folder for its files: `states/`, `cheats/`, `overlays/`, `logos/`, `patches/`, `bug-reports/`. Empty: the app's folder (`gba-ds/`). Set it in a file of its own, `data-folder.cfg` beside `gba-ds.cfg` (one line, for example `data_dir = /storage/roms/gba-ds-data`; the folder it goes in must already exist, and GBA DS makes the last folder itself), which updates and unzipping a new version leave alone. GBA DS moves its files there the next time it starts, and back if you take the line out. If the folder's card isn't in, GBA DS says so in the game list and uses the app's folder for that run. Android: Settings > Folders > Choose GBA DS folder. |
| `state_dir` | empty | Save states, resume points, menu settings and extracted art. Empty: `states` in the data folder (`gba-ds/states`). |
| `state_folder` | `gbads` | `retroarch` shares the pause menu's slots and the resume point with RetroArch (see [[Playing#save-states-shared-with-retroarch]]). Set by Settings > Save States in the game list (kept in `library.cfg`). |
| `retroarch_state_dir` | empty | RetroArch's state folder, for when GBA DS doesn't find it from RetroArch's settings. Empty: worked out from `retroarch.cfg`. Set by Settings > RetroArch's Folder in the game list (kept in `library.cfg`). |
| `retroarch_cfg` | empty | RetroArch's settings file. Empty: ROCKNIX's, BananaOS's, or `~/.config/retroarch/retroarch.cfg` (Android: RetroArch's own, where Android lets GBA DS read it, which is mostly not the case). |
| `screenshot_dir` | `/storage/roms/screenshots` | Where Take Screenshot saves its PNGs. |
| `cheat_dir` | RetroArch's cheat folders | RetroArch's `.cht` folders, searched two levels deep, `:`-separated. GBA DS's own `cheats` folder (`gba-ds/cheats`) is always searched first. |
| `asset_dir` | empty | The folder holding your `logos/`, `overlays/` and `patches/`. Empty: the data folder (`data_dir`). |
| `patch_dir` | empty | Patches, in a folder for each game (see [Playing](Playing#patches)). Empty: `patches` in the data folder (`gba-ds/patches`). |
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
| `audio_auto` | `1` | If the sound runs dry three times within about ten seconds (the slowing-down of a device on a low battery, say), GBA DS raises the sound delay one step (32, 64, 96, 128, 192 ms), says so on screen, and goes on to the next if it keeps happening. Only for that run: your `audio_latency` isn't changed. Not counted while fast-forwarding, rewinding or just back from the menu. `0`: never. |
| `keep_sound` | `1` | If the system holds the screen up for a moment (on some setups the display pauses for half a second every few seconds), the game and its sound carry on and the picture catches up. `0`: the game waits for the screen, and the sound breaks up while it does. |
| `performance` | `system` | Linux (not BananaOS, which sets its own) and Android 13 or later: **Performance** in the game list's Settings. `system` (**Let the System Decide**) leaves the CPU, memory and graphics speed to the system, as for its other emulators. `max` (**Set to Max**) holds them at full speed while GBA DS is open and puts them back when it closes (on Android, which doesn't let an app set the processor's speed, it asks for a high clock, and the system can still hold it back if the device gets hot): smoother on devices where games stutter or the sound breaks up, but the battery runs down faster and the device gets warmer. Kept in `states/prefs.cfg`. (`cpu_performance`, from v1.5.3, is no longer used.) |
| `watch_system` | `1` | ROCKNIX only: while GBA DS runs, `gba-ds.log` also gets kernel messages, system services' messages and the screens' events (at most 200 lines of each), timed like GBA DS's own notes on pauses, for reports of stutters. `0` leaves them out. |
| `pause_services` | `1` | ROCKNIX only: background services that get in GBA DS's way are stopped while it runs and started again when it closes. That's add-ons (installed under `/storage`) that keep crashing and restarting, and ones known to fight over the second screen (RetroDualStation's control center, which held the screen up every few seconds on one player's device). ROCKNIX's own services are never stopped. `gba-ds.log` lists what was stopped and anything else that looked in the way. `0`: only logged. |

## Android only

| Option | Default | |
|---|---|---|
| `pin_cores` | `0` | `1` keeps the game thread on a CPU core of its own. It made no difference on the RG DS Plus; there for testing. |
| Rotation | Auto | With one display (a phone, the Surface Duo): Settings > System > **Rotation**. **Portrait** holds GBA DS upright, the two screens one above the other; **Landscape** on its side, side by side; **Auto** leaves it to the phone and follows Android's rotation lock. Either way up still turns. Not shown on devices with two displays. |

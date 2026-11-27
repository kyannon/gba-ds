# Menu rework (branch `menu-ui-rework`)

Status: design, with the data layer started (stage 1). Written for issues
#53 (per-game settings and screen layouts), #54 (play every game, two
lists), #55 (turbo A and B, Select and Start on the face buttons), #56
(brightness of each screen) and #57 (left stick as D-pad on or off), and
for the owner's wish that everything in `gba-ds.cfg` and `prefs.cfg` can be
changed in the app, for all games or for one.

## What is wrong today

- The pause menu has three pages of buttons that cycle a value on each
  press (about 20 settings). A button can't hold a brightness, a number of
  seconds or a list of ten choices, and a page can't grow.
- Every pause-menu choice goes to `prefs.cfg` for all games. A game can't
  have its own scale, shader or layout. Only the face buttons (`.buttons`),
  controls (`.controls`) and patches are per game.
- `prefs_save` writes a fixed list of keys. Anything else in `gba-ds.cfg`
  can only be changed by editing the file; the game list's Settings screen
  is a few big buttons with a paragraph under each, and it's full at five.
- A setting changed in `gba-ds.cfg` is silently shadowed once `prefs.cfg`
  has written its own copy of the key.

## Principles

1. **One list of settings, two places to change it.** The game list's
   Settings changes things for all games; the pause menu changes them for
   this game (or for all). Both show the same rows, in the same order, with
   the same words.
2. **Only what was changed is kept.** `prefs.cfg` and a game's file hold
   the keys the player changed, not a dump of everything, so a line in
   `gba-ds.cfg` still counts until someone changes that setting in the app.
3. **Say where a value comes from**, and make going back one tap.
4. **Big rows, few on screen, no typing where a choice will do.** Everything
   is reachable with a finger on the touchscreen and with the D-pad.
5. Nothing new on the second screen (Philosophy: it still says "Paused").

## Layers (later wins)

1. built-in defaults
2. `gba-ds.cfg` (and `~/.config/gba-ds/gba-ds.cfg`, `data-folder.cfg`)
3. `states/prefs.cfg`: changed in the app **for all games**
4. `states/library.cfg`, `states/hotkeys.cfg` (already there: the list view,
   Android's folders, hotkeys)
5. command line
6. `states/<game>.cfg`: changed in the app **for this game** (new; only the
   per-game keys, below). A key given on the command line still wins.

`<game>` is the ROM's file name without its extension, as for `.cheats`,
`.patches` and the save states, so a ROM that moves folders keeps its
settings and a patched ROM shares them. The file is plain `key = value`
lines, the same names as `gba-ds.cfg`, so it can be edited by hand. Removing
a line, or the file, goes back to the settings for all games.

Per-game keys: `swap`, `scale`, `map_scale`, `smooth`, `border_color`,
`overlays`, `overlay_screens`, `bezel_*`, `shader`, `lcd_strength`,
`color_correction`, `lcd_ghosting`, `frame_buffer`, `match_display`,
`screen_match`, `screen_placement`, `map`,
`map_interval`, `custom_logos`, `pinball_live`, `rewind`,
`rewind_seconds`, `fast_forward`, `ff_speed`, `ff_mute`, `ab_swap`, `show_fps`,
`audio_latency`, `save_on_quit`, `auto_load`, and the new `brightness_top`,
`brightness_bottom`, `left_stick_dpad`, `turbo_rate`, `rumble_strength`, `button_right`,
`button_bottom`, `button_top`, `button_left`, `button_l`, `button_r`, `button_dpad_up`,
`button_dpad_down`, `button_dpad_left`, `button_dpad_right`. For all games only: folders,
accounts, updates, `layout`, `touch_*`, `sway`, `vsync`, hotkeys,
`performance`, `show_all`, `name_tags`, the game list's view,
EmulationStation / BananaOS integration.

While a game runs, its file is put over the settings and taken off again
when it ends (game.c `game_run`), so the game list always shows the
settings for all games.

## Choosing the scope: A and Y

Every row in the pause menu has two ways to be changed:

| | this game | all games |
|---|---|---|
| D-pad / controller | **A** (or Left / Right to step) | **Y** (Left / Right with Y held) |
| touch | tap the row, with the **This game** pill showing | tap the pill once (it reads **All games**), then the row; it goes back to This game when the menu closes |

- A row whose value is set for this game shows a small gold **Game** tag
  after its value. **X** (or a tap on the tag) takes the override away:
  the row goes back to the value for all games, with a one-line note.
- A row changed for all games while this game has its own value keeps the
  game's value, and says so in the note ("This game has its own: X to
  remove it").
- Quick actions that aren't on a row (the swap-screens hotkey, the shader
  hotkey, the quick bar's pin) change the layer already in force: this
  game's if it has its own value, else all games'.
- Actions (Resume, Save State, Cheats...) have no scope.

## Game list > Settings (all games)

Today's single panel becomes a short list of categories, each a big button
with its current state under it, opening a page of rows (below). Back
returns one step. B/Back and HOME work as now.

| Category | Rows |
|---|---|
| **Games** | Show all games: Supported only / All games (#54), Game name tags, Game folders (Android: choose; Linux: shown), Logo folder, Custom logos |
| **Display** | Scale (game, second screen), Shader and strength, Brightness: top, Brightness: bottom (#56), Color correction, LCD ghosting, Scaling filter (Sharp, Smooth, Scale2x, Scale4x, hq2x, MMPX, xBR-lv2, xBR-lv3, OmniScale: issue #89), Swap screens, Screen shapes, Screen placement, Overlays and bezel, Border colour |
| **Controls** | Map Buttons, Hotkeys, Left stick as D-pad (#57), Face buttons (#55), Turbo rate, Rumble strength (#83), GBA A/B swap, menu combo |
| **Speed and sound** | Show FPS, Performance, Frame buffer, Match display rate, Rewind and its length, Fast forward and its speed, Mute while fast forwarding (#109), Sound delay |
| **Second screen** | Map on/off, Map refresh, Pinball live view |
| **Saves** | Save on quit, Auto-load, Save folder and type, GBA DS data folder |
| **System** | Language, RetroAchievements, Updates, EmulationStation / BananaOS list (as now) |

The existing special screens (Map Buttons, Hotkeys, Updates, Folders,
RetroAchievements, ES integration) are rows inside their category and
keep their own code.

Row anatomy (all pages): label on the left, value on the right between
`‹ ›` arrows (a tap on an arrow steps, a tap in the middle cycles or opens
a picker), a one-line note under the selected row. 6 rows fit on a
page at the RG DS Plus's 1024x768 without small text; longer pages scroll
by dragging or with L1/R1... (page up / down), with a thin scroll bar.

## Pause menu (this game, or all)

Six tabs (Game, Display, Screens, Controls, Speed, Map; L1 / R1 and taps); a page shows at most 8 rows and scrolls past that (D-pad or a finger dragged). The Settings screens carry the same pictures as the tabs (Settings > System: a toolbox):

| Tab | Holds |
|---|---|
| **Game** | Resume / Restart Game and Save State / Load State (two to a row), the slot, then Cheats / Patches and Take Screenshot / Submit a Bug Report (two to a row; Achievements between them when on), then Close Game and Close GBA DS last, double height and side by side (Close GBA DS alone, full width, when the game wasn't started from the game list) (actions) |
| **Display** | the Display category's rows; Overlays opens its screen as now |
| **Controls** | the Controls category's rows; Face buttons becomes four rows here instead of its own screen |
| **More** | Speed and sound, Second screen and Saves rows that make sense for one game |

The **This game / All games** pill sits at the right of the tab row's
second line, always visible on the four settings tabs.

## What each issue becomes

- **#53** the per-game file and the A / Y scope, above. Screen layout (scale,
  shapes, placement, swap, overlays) is all per game.
- **#54** `show_all` is a row in Games, and the game list gets a
  **Supported / All** switch beside the list / grid button: the list shows
  one or the other (the first is today's), kept as `show_all` in `prefs.cfg`.
  Games without second-screen support get the logo-less single-screen play they
  have now. (`show_all = 1` in `gba-ds.cfg` keeps meaning All.)
- **#56** `brightness_top` / `brightness_bottom` (20-100, steps of 10), rows
  in Screens (both menus; per game or all games). `ui_brightness` puts a
  black layer over each screen at the end of the frame (`ui_present`, once per
  cleared frame, after screenshots), so it's never the system's brightness and
  never brighter than it.
- **#55** the four face buttons gain choices: GBA A, B, L, R, Start,
  Select, **Turbo A**, **Turbo B**, Nothing. Turbo repeats at `turbo_rate`
  presses a second (Slow 10, Medium 15, Fast 20, default Medium: a 60 Hz
  frame count, so exact in the harness). `.buttons` moves into the game's
  file as `button_*` lines the first time it's read.
- **#56** `brightness_top` / `brightness_bottom`, 20 to 100 % in steps of
  10, drawn as a black layer under the menu and the quick bar's text so
  the menus stay readable (a dimmer only; brighter than the panel's own
  maximum isn't possible). Top and bottom are the physical screens.
- **#57** `left_stick_dpad` (on by default, as now), General in Controls;
  the stick's analog-to-D-pad step in ui.c is skipped when off.

## Stages (each ends with a test build)

1. **Data layer** (done, this branch): the per-game file and its layering;
   no visible change. Test: put `scale = zoom` in `states/<game>.cfg`.
2. **Rows widget and the game list's Settings categories**, with the new
   settings that need no drawing work: Show all, Left stick (#57), Turbo
   and the new face-button choices (#55). Prefs written as changed keys.
3. **Pause menu** (done): seven tabs, the This game / All games bar, A / Y / X, the Game tag. The Buttons screen became four Controls rows. Settings > System is in the same row style, Map Buttons and Hotkeys moved to Controls.
4. **Brightness** (#56) and the Supported / All switch in the game list (#54).
5. Wiki (Settings, Playing), changelog, regression cases for the file
   layering and the new settings, Android check.

## Decisions to confirm

1. Tabs: Game / Tools / Display / Controls / More (five). Fewer, with
   scrolling, is possible, but a tab is one tap and a page that scrolls is
   a drag.
2. Pill for touch, A / Y for buttons (the pill is the only visible addition
   to the pause menu).
3. `prefs.cfg` keeps only changed keys from now on. The keys it holds today
   stay as they are (they count as changed).
4. Text rows (paths, RetroAchievements password) stay read-only in the app
   in this version; Android's folder pickers and the sign-in box stay.

## System: reset and delete

System's layout (library.c `settings_geom`, slots from `row_slot` / `row_second`): Language (each press steps to the next language); Updates; room; Save States | RetroArch's State Folder; the OS's integration row; room; Reset | Delete. Left / Right switch between the two on a row, Up / Down go by slot. Every Settings page's help text is wrapped beside Back and centred on its height (`set_notes`); BananaOS's long text raises Back a little.

`Reset settings to defaults` (every key of the settings table removed from `prefs.cfg`, the in-memory values set from `config_defaults`, paths and other files untouched) and `Delete per-game settings` (every `*.cfg` in the state folder except prefs, library, hotkeys and update). Both use `confirmRow`: the first activation says "Are you sure?", the second acts; moving off the row or activating another cancels.

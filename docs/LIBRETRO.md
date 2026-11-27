# The second-screen module in a libretro core

GBA DS's second screen (the game's map, a HUD, the logo, the two-screen views)
comes from a module that builds without SDL, so a libretro core can run it as the
app does (discussion #75). This page is for whoever writes that core.

## Settings

The module reads its settings from a `Config` (config.h), through the pointer
`sec.cfg` the core sets before `second_game_start()`. Of its fields the module
and the core files it runs on read only the ones below; the rest are the app's
(its menus, screens, buttons and folders) and can stay as `config_defaults()`
leaves them. These are the candidates for core options. A field read "at open"
takes effect at the next `second_open()` (the next game, or the game started
again); one read "each frame" takes effect at once.

<!-- module-settings -->
| Field | gba-ds.cfg key | Default | Choices | Read | What it changes | As a core option |
|---|---|---|---|---|---|---|
| `map` | `map` | 1 (on) | 0 off, 1 on | at open and each frame | Off: no second screen at all. No captures start and `second_shown()` says `SHOWN_OFF`; the game plays like an ordinary GBA game. | Yes ("Second screen") |
| `mapInterval` | `map_interval` | 60 | 30, 60, 90, 180 (frames: every 0.5, 1, 1.5, 3 s) | each frame | How often a captured screen is taken again. A profile's own shorter interval wins (Swordcraft Story); after failed captures it backs off up to 8 times longer. Shorter can make the sound crackle on a slow device. | Yes ("Map refresh") |
| `swap` | `swap` | 0 | 0 game on top, 1 game below | each frame | In the two-screen pinball view, which half of the table goes on the second screen. Where the screens go is the core's own layout; this only has to agree with it. | Yes, as the core's screen order ("Screens") |
| `tallLive` | `pinball_live` | 1 (on) | 0 off, 1 on | at open | The live two-screen views: Pokémon Pinball's and Sonic Pinball Party's table over both screens, DK: King of Swing's level above the game. Off: those games play on one screen with the logo on the other (for slower devices: the views run extra copies of the game). | Yes ("Pinball two-screen view"), applied at the next game start |
| `toggle` | `toggle` | 0 (off) | 0 off, 1 on | each frame | The contextual toggle: the player turns the second screen's pages or view by hand. The core calls `toggle_press()` for its button (the app's is HOME + A, or a button on the screen). | Yes ("Contextual toggle"), with a button for `toggle_press()` |
| `autoRotate` | `auto_rotate` | 1 (on) | 0 off, 1 on | each frame | With the toggle on: timed pages keep turning by themselves. Off: a page stays until the player presses. | Yes ("Auto-rotate"), only meaningful with the toggle on |
| `stateDir` | `state_dir` | the data folder's `states/` | a folder | when debugging | Only with `sec.debug` on: where the HUD backdrop is written as `hud-backdrop.bmp`. | No: point it at any folder the core may write to (its save folder) |
<!-- /module-settings -->

Two values outside `Config` are read by emu.c for every copy of the game it opens
(the game, the hidden copy that captures, the logo's and the two-screen views'
copies). The core sets them before `emu_open()`:

<!-- module-globals -->
| Variable | gba-ds.cfg key | Default | Choices | What it changes | As a core option |
|---|---|---|---|---|---|
| `emu_bios_path` | `gba_bios` (on: the data folder's `bios/gba_bios.bin`) | empty: mGBA's built-in BIOS | a GBA BIOS file's path, or empty | The player's own BIOS, for save states made with one (RetroArch's mGBA core with a BIOS file). | Yes: the frontend's system folder's `gba_bios.bin`, as mGBA's libretro core offers it |
| `emu_solar_level` | `solar_level` | 6 | 0 (dark) to 10 (full sun) | The sun sensor's reading in Boktai, for every copy alike. | Yes, or the frontend's sensor interface feeding it |
<!-- /module-globals -->

Not settings, but the core gives them too: the number of screens it shows
(`second_open()` and `second_step()`'s `screens`: the two-screen views need 2), `sec.debug` (the app's `GBADS_DEBUG=1`: log lines on stderr) and
`emu_set_sync_work()` (whether captures and logo extraction run on threads of
their own or inline). The patches a player has switched on go to
`emu_patch_plan()` before `emu_open()`.

`tools/module_settings.py` checks these two tables against the code (CI runs it
with `make module-check`): a module or core file that starts reading another
`Config` field, or another `extern` in emu.h, fails it until the field is listed
here.

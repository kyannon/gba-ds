# The second-screen module in a libretro core

GBA DS's second screen (the game's map, a HUD, the logo, the two-screen views)
comes from a module that builds without SDL, so a libretro core can run it as the
app does (discussion #75). This page is for whoever writes that core,
`libretro.c`: what to compile, what to call and when, what comes out, and what's
left to the core. `tools/second_driver.c` (about 220 lines) makes every call a
core makes, in order, with no libretro glue: read it beside this page.

The first version's scope, as settled in the discussion: ROM loading, audio,
battery saves, states, the map capture, the logo and the HUD split. The Pinball
two-screen view, the contextual toggle and anything like GBA DS's quick bar can
come later; the module already does the first two, so they cost the core little
(a setting, a button).

## Building

Compile, with the same flags:

- **The module:** the Makefile's `MODULE_SRC`: `src/workers.c`, `twins.c`,
  `toggle.c`, `second.c`, `hud.c`, `rgba.c`.
- **The core it runs on:** `CORE_SRC`: `src/emu.c`, `patch.c`, `mapcap.c`,
  `profiles.c`, `logo_masks.c`, `climb.c`; and `src/logo.c`.
- **Libraries:** the vendored zstd decoder `third_party/rzstd/src/*.c` (RetroArch's
  compressed states), `libmgba.a` built with `scripts/mgba-options.sh` (its
  `MGBA_OPTS` at `MGBA_COMMIT`), its zlib, `-lpthread -lm`.
- **Flags:** `-std=gnu11`, mGBA's own defines (the Makefile reads them from
  mGBA's build, `CMakeFiles/mgba.dir/flags.make`'s `C_DEFINES`: `-DM_CORE_GBA
  -DM_CORE_GB -DUSE_ZLIB -DUSE_PTHREADS ...`), `-DGBADS_VERSION='"x.y.z"'`
  (logo.c tries again a logo an older version couldn't find), the include paths
  `deps/mgba/include`, mGBA's build `include`, `third_party/rzstd/include`, and
  `-fPIC` for a shared library.

`make module-check` is exactly this build (plus `tools/module_check.c`), linked
with no SDL; CI runs it on every push. None of the app's files (`game.c`, `ui.c`,
`config.c`, `paths.c`, `cheats.c`...) are needed. `src/stb_image.h` (in the repo)
decodes the logo's PNG, as the driver does.

## The calls

Everything the module keeps is in two structs, `SecondState sec` (second.h) and
`HudState hud` (hud.h); a core holds one game at a time, as the app does. The
calls are second.h's first section. On the thread that makes them, in this order:

**Loading a game** (`retro_load_game`):

1. The values emu.c reads (see [Settings](#settings)): `emu_bios_path`,
   `emu_solar_level`; `emu_set_sync_work()` if wanted (see [Threads](#threads)).
2. `profile_set_rom(NULL, 0)`, `emu_open(&emu, path, NULL, true)` (the game's
   own copy: `true` gives it sound; the save is given next), then
   `profile_set_rom(emu.gameCode, emu.romCrc)` (a known colour patch's colours).
   A patch file beside the ROM (`.ips`, `.ups`, `.bps`, named as RetroArch names
   them) is applied by `emu_open` to every copy of the game.
3. The battery save: `emu_save_memory(&emu, buf, 128 KB)` (see
   [Battery saves](#battery-saves)).
4. What the module reads, set once per game: `sec.emu = &emu`, `sec.cfg =
   &cfg`, `sec.debug`, `sec.romPath = path` (the module opens its own copies of
   the game from that path, so the core needs `need_fullpath`), `sec.hooks` and
   `hud.hooks` (see [The pictures](#the-pictures)).
5. `second_game_start()`; `second_open(path, emu.gameCode, 2)` (2: two screens,
   which the two-screen views need; `emu.gameCode` is as `emu_open` set it, empty
   for a game with no code unless a profile knows it by its CRC); then
   `second_loop_start()`; then `second_idle_picture()` for the logo (below).

**Each frame** (`retro_run`):

1. `second_frame_start()` (puts back a loaded state's second screen, below).
2. `second_run_frame(keys)` instead of `emu_frame`: the game's frame, with the
   Pinball and climb copies run around it. `keys` is the GBA's key bits (mGBA's
   `GBAKey`: A 0, B 1, Select 2, Start 3, Right 4, Left 5, Up 6, Down 7, R 8, L 9).
3. `++sec.frame` (the frame count is the core's: the app counts once a frame,
   after the game's frame).
4. `second_step(2)`: captures picked up, the refresh, the HUD, the views.
5. A compilation (Game Boy Wars Advance 1+2): every 15 frames, if
   `profile_find(emu.gameCode)` has `pick`, `pick(&emu)` names the game running
   in it (NULL: the compilation's own code); when that differs from
   `sec.subCode`, `second_drop()` then `second_open(path, code, 2)` and the idle
   picture again. (The app's `ss_switch`, game.c.)
6. `second_shown(&dim)` says which picture the second screen shows now; draw
   both screens (see [The pictures](#the-pictures)), then the sound (see
   [Audio](#audio)).

**Loading a state** (`retro_unserialize`, a normal load): `emu_state_load_mem`,
then `second_jump()` (the copies start again from the new moment),
`second_state_loaded(block, n)` (GBA DS's own block from the state, or NULL: it
takes the malloc'd block over and puts it back at the next
`second_frame_start()`), `second_new_subject()` (the map captured again as soon
as it can be). See [States](#states) for the bytes, and for run-ahead's loads.

**Saving a state** (`retro_serialize`): `second_state_save(&n)` gives the block
(malloc'd), kept inside the state; see [States](#states).

**Reset** (`retro_reset`): `emu_reset(&emu)`, `second_jump()`,
`second_restarted()`.

**The logo changed** (it finished extracting, or the core switched it):
`second_logo_changed()`, then the idle picture again.

**The toggle's button** (with `toggle` on): `toggle_press()` when it's pressed.
It does nothing while the toggle is off or a turn is under way.

**Unloading** (`retro_unload_game`): `second_close()` (the capture, the copies,
the pictures), then `emu_close(&emu)`.

## The pictures

The module hands its pictures to the core through hooks, called during its calls
on the same thread; each fills a buffer the core keeps, and the core picks one
to show each frame from `second_shown()`. Every picture is 32-bit pixels in
mGBA's order, `XBGR8888` (bytes R, G, B, X), as the game's own picture
(`emu.fb`, 240 x 160) is. libretro's `RETRO_PIXEL_FORMAT_XRGB8888` has R and B
the other way round, so the core swaps them as it copies.

`sec.hooks` (`SecondHooks`):

- `present(px)`: the map picture, `sec.mapW` x `sec.mapH` (the capture's size;
  at most 240 x 160 in every profile today, smaller when a profile crops it).
- `presentGba(px)`: a picture of the game's own size, 240 x 160 (the screen copy
  of a logo-only game, the two-screen views' second half). It goes in the same
  buffer as `present`'s: `sec.mapW` x `sec.mapH` is 240 x 160 for those games.
- `logoLoad(&pic, &sub)`: the game's logo, as RGBA bytes (R, G, B, A; `pic.px`,
  `w`, `h`, `pitch`, and `own` for the core's own use), `*sub = 1`; false if
  there's none (yet). `logoFree(&pic)` releases it. The app's comes from its
  logos folder; a core's can be the one `logo.c` makes from the ROM:
  `logo_extract(path, emu.gameCode, png)` writes it as a PNG (it runs the game to
  its title screen, so keep the file, in the save or system folder), decoded with stb_image as
  the driver does. `logo_request` does the same on a thread of its own;
  `logo_generation()` changes when it's done (then `second_logo_changed()`).

`hud.hooks` (`HudHooks`):

- `presentHud(canvas)`: the HUD picture, 480 x 320 (`HUD_CW` x `HUD_CH`: two
  pixels to a GBA pixel), shown at the size of a 240 x 160 picture.
- `presentCombat(px)`: the fights' picture, `sec.mapW` x `sec.mapH`.
- `hasCombat()`: whether the core has a place for the fights' picture (true, if
  it keeps a buffer for it).
- `saveBmp(px, w, h, path)`: only with `sec.debug` on (a debug picture); may do
  nothing.

What `second_shown(&dim)` returns, and what to show:

| Returned | The second screen shows |
|---|---|
| `SHOWN_OFF` | Nothing: the `map` setting is off, or the game has no second screen. Black. |
| `SHOWN_COMBAT` | The fights' picture (`presentCombat`'s). |
| `SHOWN_HUD` | The HUD picture (`presentHud`'s). |
| `SHOWN_MAP` | The map picture (`present` / `presentGba`'s). |
| `SHOWN_LOGO` | The idle picture: the logo at half strength on grey. |

`dim` true: darken the whole screen, as the app does with black at alpha 120 of
255 over it (the last map kept while the game has none, A Link to the Past
indoors). A picture smaller than 240 x 160 is centred, on black.

The idle picture comes from `second_idle_picture(&px, &w, &h, &sub)` (malloc'd,
the core's to free), asked for after `second_open` and after each
`second_logo_changed`: `IDLE_PIC` gives it (240 x 160 with `sub` 1: the logo
drawn at half strength on grey, `0x303030`; a Game Boy game's plain grey);
`IDLE_NOPIC` a logo too big to draw on it; `IDLE_NONE` no logo yet (grey).

## Settings

The module reads its settings from a `Config` (config.h), through the pointer
`sec.cfg` the core sets before `second_game_start()`. Of its fields the module
and the core files it runs on read only the ones below; the rest are the app's
(its menus, screens, buttons and folders). A core can start from a zeroed
`Config` and set these, as the reference driver does (config.c, which has
`config_defaults()`, builds without SDL too, but the module doesn't need it).
These are the candidates for core options. A field read "at open" takes effect
at the next `second_open()` (the next game, or the game started again); one
read "each frame" takes effect at once.

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
(`second_open()` and `second_step()`'s `screens`: the two-screen views need 2),
`sec.debug` (the app's `GBADS_DEBUG=1`: log lines on stderr) and
`emu_set_sync_work()` (whether captures and logo extraction run on threads of
their own or inline). The patches a player has switched on go to
`emu_patch_plan()` before `emu_open()`.

`tools/module_settings.py` checks these two tables against the code (CI runs it
with `make module-check`): a module or core file that starts reading another
`Config` field, or another `extern` in emu.h, fails it until the field is listed
here.

## Threads

By default the module and the core it runs on use threads of their own:

- each map capture, on a hidden copy of the game (`mapcap.c`);
- logo extraction (`logo.c`, when asked with `logo_request`);
- three profiles' own workers (`profiles.c`: Mega Man Battle Network's map,
  Baldur's Gate's, Mario Pinball Land's counters);
- the climb copy (`climb.c`, DK: King of Swing);
- the Pinball copies' workers (`workers.c`), waited for every frame;
- the battery save's writer, with a save file (`emu.c`; none with
  `emu_save_memory`).

`emu_set_sync_work(true)` runs the first four on the caller's thread instead:
every capture, extraction and worker runs inside the call that starts it, and
the climb copy's pictures are waited for. Runs are then exact and repeatable
(the same pictures at the same frames: the regression tests run so), at the cost
of frames that take longer when a capture runs (a capture runs many of the
game's frames on its hidden copy at once). Off, captures come in a few frames later, as in the app.
A core for slow devices leaves it off; a frontend that wants no threads, or a
test, sets it on. The Pinball copies' workers and the save writer run either way.

## Frame layout and `SET_VIDEO_VIEWS`

A suggested layout: one frame of 480 x 640, two pixels to a GBA pixel, the two
screens stacked (as the app's default puts them), each half 480 x 320:

- the game in one half (240 x 160 at two pixels to one), the second screen in
  the other: the HUD picture as it is (it's already 480 x 320), every other
  picture at two pixels to one, centred, the rest black;
- `swap` 0: the game in the top half; 1: in the bottom half.

`retro_get_system_av_info`: base and max 480 x 640, aspect 0.75, 59.7275 frames a
second (16777216 / 280896, a Game Boy game's too), the sample rate from
`emu_audio_rate(&emu)`.

With views (RetroArch's `RETRO_ENVIRONMENT_SET_VIDEO_VIEWS`, in libretro.h): ask
`RETRO_ENVIRONMENT_GET_VIDEO_VIEWS_STATUS` each `retro_run`, and while it has
`RETRO_VIDEO_VIEWS_STATUS_PRESENTS`, send two views before the frame, every
frame (the header asks for that: it covers state loads and run-ahead):

| View | x, y | width x height | screen | eye | aspect_ratio |
|---|---|---|---|---|---|
| top half | 0, 0 | 480 x 320 | 0 | `RETRO_VIDEO_VIEW_EYE_NONE` | 0 (1.5) |
| bottom half | 0, 320 | 480 x 320 | 1 | `RETRO_VIDEO_VIEW_EYE_NONE` | 0 (1.5) |

Screen 0 is the frontend's primary screen, so it's the top half, the game's by
default; `swap` moves the game to screen 1 the same way it moves it in the
packed frame, and a frontend without views shows the packed frame as it is.
Without `PRESENTS`, send a map with no views once, to clear it. Touch isn't
needed: GBA games aren't played by touch, and the toggle is a button.

## What's left to `libretro.c`

### Environment and options

`RETRO_ENVIRONMENT_SET_PIXEL_FORMAT` (`XRGB8888`); `need_fullpath = true` in
`retro_get_system_info` (the module opens the game by its path; extensions
`gba|gb|gbc|zip|7z`, as `emu_open` takes); `GET_SAVE_DIRECTORY` /
`GET_SYSTEM_DIRECTORY` (the logo's PNG, the BIOS); input descriptors; the core
options from [Settings](#settings) (`SET_CORE_OPTIONS_V2`, read with
`GET_VARIABLE` / `GET_VARIABLE_UPDATE`); the views above; optionally
`SET_MEMORY_MAPS` for achievements (mGBA's libretro core has them, and
`emu_memory()` gives the regions).

### Input

The RetroPad's A, B, Select, Start, the d-pad, L and R to the GBA's key bits
above (the GBA's A is the RetroPad's A, as in mGBA's libretro core). The toggle:
a free RetroPad button (X, say) calling `toggle_press()` on its press, while the
`toggle` option is on; the app's HOME + A has no RetroPad equivalent.

### Audio

After each frame, `emu_audio_read(&emu, buf, frames)` until it returns 0 (16-bit
stereo, interleaved; it returns frames read) and hand each piece to
`audio_batch_cb`. Only the game's own copy (opened with `true`) has sound; the
hidden copies are silent.

### Battery saves

As mGBA's libretro core: keep a 128 KB buffer (`GBA_SIZE_FLASH1M`) set to 0xFF;
`retro_get_memory_data(RETRO_MEMORY_SAVE_RAM)` returns it and
`retro_get_memory_size` returns `emu_save_memory_size(&emu)` (128 KB until the
game's save type is known, then its size; a Game Boy game's cartridge RAM). The
frontend fills the buffer after `retro_load_game`, so give it to the game with
`emu_save_memory(&emu, buf, size)` before its first frame (mGBA's core does it
at the first `retro_run`, for the same reason); the game then reads and writes
it in place, and the frontend saves it as its `.srm`. The files are mGBA's
core's, so they're shared with it. The hidden copies never get a save (they
start from the game's state). (`emu_open` with a path instead keeps the save in
a file the core manages, as the app does.)

### States

The bytes are a GBA DS state file's, in memory: mGBA's own format (as
RetroArch's mGBA core writes, so states go both ways between the two cores),
with GBA DS's block (the second screen as it was: the map's pictures, the screen
copy, the HUD's held pieces) inside it as mGBA's extra data, which other
emulators skip.

- `retro_serialize_size`: make the state, `second_state_save(&n)` then
  `emu_state_save_mem(&emu, block, n, &size)`, keep it, return `size`. Its size
  changes (the block holds the map's pictures, up to 16 frames), so make it again
  here every time rather than keep a number.
- `retro_serialize`: copy the bytes just made (or make them again if a frame
  ran in between); false if they don't fit.
- `retro_unserialize`: `block = emu_state_mem_get_extra(&emu, data, size, &n)`,
  `emu_state_load_mem(&emu, data, size)`, then the calls under "Loading a state"
  above.

Not `emu_state_save` / `emu_state_load`: those are the raw states the module
uses for its own copies, and an older mGBA's state doesn't go through them byte
for byte (step 28 of the extraction log).

`RETRO_ENVIRONMENT_GET_SAVESTATE_CONTEXT` says why the frontend asks:
`RETRO_SAVESTATE_CONTEXT_NORMAL` is the player's state, which needs all of the
above. Run-ahead's contexts are below.

### Cheats

`retro_cheat_reset` / `retro_cheat_set`: `emu_cheats_clear` / `emu_cheats_add`
(GameShark, Action Replay, CodeBreaker, as RetroArch's `.cht` files hold them).
Only the game's copy gets them.

## Checking a core against the app

The app writes every call it makes into the module when it runs with
`GBADS_MODULE_TRACE=DIR` (`trace.txt`, the states it loaded, and at each
screenshot what both screens show); `build/host/second-driver ROM DIR OUT
[SAVE]` replays that with the module alone and writes the same; regress.py's
`"driver": true` cases (16) check the two are byte for byte the same
(docs/TESTING.md). A core can be checked the same way: put the same trace calls
in it (`second_trace_begin(dir)` once the hooks are set; `second_trace_shot(name)`
at a picture), play the same input, and compare with the app's files. Two
settings make the driver work as a core would, and still match the app:
`GBADS_DRIVER_SAVE_MEM=1` (the battery save through `emu_save_memory`) and
`GBADS_DRIVER_SERIALIZE=1` (the game's state through `emu_state_save_mem` /
`emu_state_load_mem` after every frame).

## Open questions

- **Run-ahead.** A software core can't refuse it. Plain run-ahead serializes,
  runs frames ahead and unserializes every frame, on the same instance; the
  module must not take those loads as the player's (a normal load starts the
  copies again and asks for a new capture: every frame, the map would never
  come). The frontend says which it is: `GET_SAVESTATE_CONTEXT` gives
  `RETRO_SAVESTATE_CONTEXT_RUNAHEAD_SAME_INSTANCE` for run-ahead's states, and
  `RETRO_ENVIRONMENT_GET_EXECUTION_PHASE` says whether a frame is kept
  (`RETAINED`), replayed (`REPLAY`) or thrown away (`SPECULATIVE`). A first
  approach to try: in a run-ahead context, serialize and unserialize only the
  game (no block, no module calls); run speculative frames with `emu_frame`
  alone (no copies, no `second_step`), so the module only ever sees the kept
  frames, in order. The second screen then lags the game by the run-ahead's
  frames, which for a map is fine. Second-instance run-ahead
  (`RUNAHEAD_SAME_BINARY`) loads a second copy of the core library, whose module
  is separate and only ever sees loaded states: untried. Preemptive frames
  (`REPLAY`) likewise. Davey's point from the discussion: gating captures on
  `GET_AUDIO_VIDEO_ENABLE` would move them into the instance that shows.
  `emu_set_sync_work(true)` makes such a test exact.
- **What the app does that a core leaves out**, by design or for later: the
  player's own and scraped logos (GEN-11), the bezels and overlays, the SNES
  layout's camera pan in Super Mario Advance 2 (the app writes into the game's
  memory for it), WarioWare: Twisted!'s sensor screens, rumble
  (`emu_rumble_level`, which a core could hand to the rumble interface), the
  Super Game Boy border, Game Boy palettes (`emu_gb_colours`, GB-6 and GB-7), and
  GBA DS's own run-ahead-like frame worker.
- **The RG DS Plus:** whether RetroArch's views drive both screens on stock
  ROCKNIX (which keeps the bottom panel off) is unknown.
- **RetroAchievements:** hardcore with a new core needs their approval, as the
  app's does (GEN-16).
- **Licence and the buildbot:** the repository is GPLv3, libmgba MPL-2.0; not
  yet checked against the libretro buildbot's rules.

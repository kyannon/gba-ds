# The web player

GBA DS in a browser, in beta: for players (published to GitHub Pages from
each release, below) and for trying builds without the device. It runs a
build's `web/gba-ds.wasm` (the one that comes with it, or a zip with a web
build dropped on it: the `-dev` zips have one, release zips don't; the same
file State Saver's GBA DS tab runs) for the screens of any device GBA DS
knows (offered by resolution, not by device: Settings > Screens >
Resolution, Two GBA screens by default), with sound, every button,
remappable keys, controllers and on-screen controls, and the bottom screen
as the touchscreen. Merged into
`dev` on Nov 10, 2026; work on it since happens on a `web-player` branch
made from `dev` (merge `dev` in before merging back, when tested): the last
merged Nov 13, 2026 (resolutions, follow rotation, L2 / R2, the folder).

## Building it

`scripts/build-web-player.sh` packs `web/player/` into `build/web-player/`:

- `gba-ds-web-player-vX.html`: one file, everything inline (styles, font,
  logo, scripts; the worker and the audio worklet start from blobs). Opens
  from disk.
- `gba-ds-web-player-vX.zip`: a folder of static files for a web server
  (`index.html` and the scripts beside it, `README.txt`, `licenses/`). It
  needs http(s): from disk, a page can't start the scripts beside it.

`--build ZIP` (a dev zip, or a bare `gba-ds.wasm`) or `--build` alone (this
tree's `build/web/gba-ds.wasm`, from `scripts/build-web.sh`) puts that build
inside both (`script#bundled-build` / `build/gba-ds-web.zip`); the names end
in `-with-VERSION`. Only `web/gba-ds.wasm`, `web/VERSION`, `gba-ds.cfg` and
`gamecontrollerdb.txt` are kept from the zip.

## On GitHub Pages

`.github/workflows/web-player.yml` builds the web build and the player from
`main` (each release; or "Run workflow") and publishes the folder version
to https://kyannon.github.io/gba-ds/, with the one-file player beside it
(`gba-ds-web-player.html`). Pages must be set to "GitHub Actions" once
(Settings > Pages > Source: the owner; sessions can't). The workflow shares
regress.yml's host-deps cache and keeps the WASI toolchain in its own.
Games are never published: players bring their own.

## Phones

- **On-screen controls** (`controls.js`, `GBATouch`): the D-pad, X / Y /
  A / B as on the device (X top, Y left, A right, B bottom), L, R, L2 and R2
  (inside L and R: held, rewind and fast-forward, as on the device), Select,
  Start and Menu (Home). Settings > On-screen controls: show them on
  touchscreens until a controller connects (default), always, or never;
  placement following the screen's rotation (default: below upright, beside
  on its side), below the screens (a band: L, L2, Menu, R2, R over the
  D-pad, Select / Start, the face buttons) or beside them (a column each
  side: L and L2, the D-pad, Select and Menu | R2 and R, the face buttons,
  Start); size 70-140 %;
  vibrate on press (Android: iPhones have no web vibration). The controls
  take their space first and the screens fit into the rest, so the
  placement holds whichever way the screens are arranged. They shrink to
  fit (below: the width; beside: the columns at most 62 % of it).
  Multi-touch; a finger slides from button to button and round the D-pad
  (8 ways); buttons have some slack around them. Their mask is OR-ed into
  the keyboard's and controllers'. L3 (swap) isn't on screen: GBA DS's
  menu has it.
- **Follow screen rotation** (the default for Arrangement and for the
  controls' placement; player.js `rotation()`): portrait stacks the screens
  with the controls below, landscape puts them side by side with the
  controls beside. The rotation is `screen.orientation.type`, else
  `window.orientation`, else `screen.width` against `screen.height`; it
  changes with `screen.orientation`'s change event and `orientationchange`.
  `?rotation=portrait|landscape` stands in for it (tests). On a computer the
  screen is landscape, so the screens sit side by side. Settings v3 (Nov 13,
  2026) moved what was still at the old defaults once: the RG DS Plus's
  screens to Two GBA screens, "Whichever fits bigger" to follow rotation,
  the gap 4 to 0, controls below to follow rotation.
- **No zooming the page:** the viewport tag (`maximum-scale=1,
  user-scalable=no`), `touch-action: pan-x pan-y` on every element (none on
  `#stage` and the controls), and player.js's guards for what those miss:
  Safari's `gesture*` events (it ignores user-scalable), a touchmove with two
  fingers, `dblclick`, and Ctrl/Cmd with the wheel (a trackpad pinch) or
  + / -. Ctrl/Cmd + 0 is left alone. Headless Chromium never zooms a page,
  so tests can only check that these events come back `defaultPrevented`.
- **Home Screen app** (the folder version over http(s) only): a manifest,
  icons (the logo on navy, made by pack.py) and `sw.js`, which keeps every
  file for offline use (cache-first; the page network-first, so a new
  version shows at the next start; the cache is named by version, older
  ones deleted). `?nosw` skips it (tests).
- **iPhone saves**: Safari can clear a page's storage when it isn't opened
  for a while, unless it's on the Home Screen, so iPhones and iPads not
  started from there get a notice saying so (dismissible, remembered).
- **Safe areas** (`viewport-fit=cover`): `#app` is padded by the top and
  sides' insets, the controls keep clear of the bottom's (`--sat` / `--sar`
  / `--sab` / `--sal`; `?safe=top,right,bottom,left` stands in for a
  phone's, to model one in a desktop browser). On a phone held sideways the
  toolbar hides while playing; the pill at the top brings it back. Upright
  and narrow, the game's name gives way to the toolbar's buttons.
- **Modelled on an iPhone 17** (402 x 874 pt, 3x; insets 62 / 34 upright,
  62 each side and 20 top and bottom sideways, from Apple's), Nov 9, 2026,
  RG DS Plus screens: upright, controls below, screens stacked gives each
  screen 335 x 251 pt (the best upright); beside leaves 137 x 103.
  Sideways, beside with screens stacked gives 226 x 170, side by side
  208 x 156; below leaves 99 x 74 stacked. Playwright with `is_mobile`,
  `has_touch`, the safe area by `?safe=`, then each shot in the phone's
  outline (corners, Dynamic Island, home bar) with Pillow. Touches tested
  with CDP `Input.dispatchTouchEvent` (one finger, two, a slide, a
  diagonal). Not yet tried on a real phone.

## How it works

- `player.js` (the page): files dropped or chosen (`takeFiles`: a dev zip,
  a game or zipped game, `.sav` / `.srm`, `.ss` / `.state`, a bug report zip,
  whose `GAME-DATE.state` it loads), IndexedDB (`gba-ds-player`: the last
  dropped build, the last 10 games, and the app's own files), the screens'
  resolution (`DEVICES`, from src/ui.c knownDevices, offered as
  `RESOLUTIONS`: each pair of resolutions once, the first device with it
  giving its inches and id, the others' ids aliased to it; plus custom ones)
  and where they go on the page, input, sound and the panels.
- `worker.js` runs the app with `tools/statesaver/gbads-runtime.js`, off the
  page's thread. It draws the app's window into the page's canvas (an
  OffscreenCanvas) with WebGL2, uploading the window's pixels straight from
  the wasm memory (XRGB8888 swapped to RGB in the shader), and draws each
  screen into its place, so the arrangement (stacked or side by side), gap,
  and sizes (by inches, as on the device, or by pixels) need no restart.
  The device or the resolution changes the window GBA DS gets
  (`GBADS_WINDOW`, `GBADS_SCREENS`, `--screen_ppi`), so those restart it
  from the same moment (a state).
- Kept between visits: everything under `/saves`, `/states` (with GBA DS's
  `prefs.cfg`) and `/home`, synced to IndexedDB every 4 s, on pause and on
  closing.
- **A folder of the player's** (Settings > Your files; File System Access:
  Chrome and Edge on computers, Chrome on Android 132+, not Firefox or
  Safari), as GBA DS's data folder on a handheld: `roms/`, `saves/`,
  `states/`, `screenshots/` (the app's `/shots`), `cheats/`, `overlays/`,
  `logos/`, `patches/`, `bug-reports/` (the app's `/gba-ds/<name>`) and
  `web-player-settings.json` (the player's settings and controls). The
  worker reports changes under all of them (`KEEP`); the page writes every
  change to the folder and the saves / states / home ones to IndexedDB as
  before (`keepFiles`), so the browser keeps a copy. With a folder, a run
  starts from the folder's files (plus IndexedDB's `/home`), the game list
  from its `roms/` too; screenshots and bug reports are only written.
  The folder's games aren't copied into memory: each goes to the worker as
  a `File` (from `getFile()`), and the runtime (`MemFS.write` with a Blob)
  reads only what the app reads, when it reads it, with the worker's
  `FileReaderSync` (synchronous, so no asyncify or C change; the app's
  reads are synchronous WASI calls). The capture worker gets only the game
  it captures. Measured Nov 13, 2026 (two games, the game list for 4 s):
  the first run reads each game whole once (its logo, then the selected
  one's checksum), the next only each header (176 bytes), the logos and
  `roms.cache` coming from the folder's `states/`.
- **Files keep their dates**: `MemFS.write(path, bytes, mtime)` (the
  folder's `lastModified`, a dropped file's, kept with a recent game in
  IndexedDB). Before, every file got the time of the run, so GBA DS's
  `roms.cache` (by size and date) never matched and the selected game's
  checksum was worked out again on every run. The
  handle is kept in IndexedDB (`kv` "folder"); permission is asked at Play
  (a click) when the browser hasn't kept it, or with Reconnect. Choosing a
  folder copies in what the browser kept (games too) where the folder has
  none of its own, and takes the folder's player settings if it has them.
- **Sound**: the web build's SDL has a driver of its own
  (`web/sdl_audio_web.c`, in SDL2's Emscripten driver slot; no thread). The
  app queues its sound as on a device; `worklet.js` asks for what it has
  played, and only that much is pulled from SDL's queue (`web_audio_pull`),
  so the queue drains in real time and game.c's pacing keeps it near
  `audio_latency` unchanged. Pulled after each frame (from a microtask: the
  runtime takes no calls while the app is inside `web_sleep`). Without a
  playing worklet (no click on the page yet), the worker drains by the clock.
  Chrome won't load a worklet for a page opened from disk, so there the same
  ring (`GbadsRing`) plays in a ScriptProcessorNode.
- **Map captures in a worker of their own**: the web build has no threads,
  so a capture (hundreds of frames on a hidden copy of the game: Tactics
  Ogre's unit list takes 350-590) used to stop the game until it was done.
  The player runs them in a second worker instead, a second copy of the
  same build started in capture-only mode (main.c:
  `GBADS_CAPTURE_SERVE=CODE`, the ROM last, `GBADS_CAPTURE_TIGHT`;
  `mapcap_web_serve`), as a device runs them on a thread beside the game.
  The live game is started with `GBADS_CAPTURE_OFFLOAD=1` (a game can want
  its first capture before its first frame): `mapcap_request` then leaves
  the snapshot for the page (`web_capture_job`), the page hands it to the
  capture worker (started on the first job, again if the game's ROM, code
  or crop changes), and the pictures come back (`web_capture_inbox`,
  `web_capture_deliver`) into the capture as a thread's would be.
  `mapcap_cancel` only forgets the job (the worker can't be interrupted:
  it finishes, its result let go). One job at a time; a newer one waiting
  replaces an older. If the capture worker fails, `web_capture_offload(0)`
  puts captures back in the game. All of it is inside `#ifdef __wasi__`:
  Linux and Android compile exactly the same code as before (checked by
  comparing `cc -E -P` of mapcap.c, main.c and game.c before and after).
  The buffers carrying snapshots and pictures go round and come back
  (`stateSpare`, `outSpare`), so an update makes no garbage for the browser
  to collect. With `?debug`, the live worker logs each frame more than
  40 ms after the last, with the game's own work in it and what came
  before ("request", "update"): a slow frame whose own work is small was
  waiting for the CPU or the browser, not the game.
  State Saver's tab doesn't ask for it, so it runs captures in the game as
  before. Captures arriving a moment later is as on a device.
- **Buttons**: the page sends a mask of ui.h's buttons (`web_buttons`, in
  ui.c, OR-ed into `btnNow` like an input script's). Builds before it get
  ui.c's keyboard keys instead (X and Y have none).
- **Controls**: keyboard by `KeyboardEvent.code`; controllers through the
  Gamepad API, per controller (`gamepad.id`): the browser's standard layout
  by position (as GBA DS reads face buttons), else a guess and a prompt to
  map it. Bindings are buttons, axis directions, or a hat on one axis
  (Firefox and Chrome on macOS report many Bluetooth pads' D-pads so: 8
  positions from -1 to 1, rest beyond 1; a direction also counts its two
  diagonals). "Map all controller buttons" asks for each in turn, like the
  app's Map Buttons. Kept in localStorage (`gbadsPlayer.controls`).
- Touches: the bottom screen only, as fractions of the app's window
  (`web_touch`). Pause when hidden (on by default) pauses the run.
- `window.gbadsPlayer` (`P`, `S`, `sound`, `capture` (with `jobs` /
  `results` counts), `currentMask()`) is there for tests and the console.
  Page addresses: `?debug` runs both copies with `GBADS_DEBUG=1` (capture
  times in the log, the capture worker's lines marked `[capture]`);
  `?nocapw` runs captures in the game, to compare.

Needs WebAssembly, WebGL2, OffscreenCanvas and AudioWorklet: Firefox 105,
Chrome 69, Safari 17 and later.

## Testing it

Games' ROMs and states are in the fixtures repo (`../gba-ds-tests`):
`tactics-ogre/us-field.ss` with `ATOE.zip`'s ROM given to `#fileInput`
together starts on the battle field; moving the cursor every 1.5 s keeps
the unit list capturing.

Headless Chromium with Playwright (Firefox isn't installed in sessions):
`file://` for the one file, `python3 -m http.server` in the unzipped folder
for the other. Launch with `--autoplay-policy=no-user-gesture-required
--use-gl=angle --use-angle=swiftshader --enable-unsafe-swiftshader`, give a
game to `#fileInput`, click once for sound. A controller: an init script
that replaces `navigator.getGamepads`. The app's health lines (`P.log`,
"last 5 s: ... audio queue N ms") show whether sound drains (a queue pinned
near 3 × `audio_latency` means nothing pulls it). The folder: an init script
that makes `showDirectoryPicker` return a folder of the origin's private
file system (`navigator.storage.getDirectory()`) tests choosing it and
what's written (Nov 13, 2026: the game, its battery save, the extracted
logo, the settings file); a new page can't test reconnecting, as this
headless Chromium crashes reading such a handle back from IndexedDB (plain
code does too), so that's checked in a real browser. No ROMs here: a few lines
of ARM assembled with clang (`--target=armv4t-none-eabi`) and linked at
0x08000000 make a test ROM (colour bars, a tone, red while A is held).

## Speed

Measured Nov 10, 2026 (Eragon in a town, the Party screen refreshing about
once a second, the capture worker on its own thread, Node): emulating 2.3
ms a frame; drawing GBA DS's window in software 7-8 ms at the RG DS
Plus's whole 1024 x 1536, 1.8 ms at half (512 x 768), 3.4 ms at 720 x 960;
the median frame 10.5 ms whole, 4.2 ms at half. Half is the default since
(settings v2 moved saved settings once): the graphics card scales the
screens to fit, and half still holds over twice the game's 240 x 160, so
games look the same; GBA DS's own menus are slightly softer. In headless
Chromium on 2 cores the same moment ran 27 fps whole, 52 at half. Builds
without the capture worker (before 1.4.13-dev (v1.5's first dev builds), or game branches not yet
merged with dev since) pause the game at each capture: the player says so
when one is dropped.

## Known limitations

- The capture worker needs a free CPU core to help: on a computer with
  only two (a session's container), the game and it share them, so games
  that capture often (Tactics Ogre) still run below 60 fps there (33-38
  fps against 10-13 with captures in the game, measured Nov 7, 2026).

- Map captures and logo extraction run inline (the web build has no
  threads), so a capture can make a frame late, as in State Saver's tab.
- The first time the game list shows new games (a folder of the
  player's, most of all), it takes each one's logo, one a frame (logo.c
  `logo_step`, `#ifdef __wasi__`: the web build has no thread for them;
  only a game's own logo, asked for while it's played, is taken at once).
  The list shows at once and the logos fill in, the ones on screen first,
  but each holds the list up while it's taken: measured Nov 13, 2026 in
  Node with 12 games, the list's first frame after 0.14 s (before, after
  every logo), then frames of 0.6-2 s while the logos came, none on the
  next run (the logos are kept in `states/logos/`). Slower on a phone.
- Memory, measured Nov 13, 2026 (Node, Superstar Saga, 5 minutes): the
  wasm memory levels at 104 MB once the 30 s of rewind are full, the same
  with the game read from a folder or given whole; the file isn't read
  again once the game has started. Choosing a folder copies the browser's
  files in one at a time.
- No RetroAchievements and no updates in the browser (no network from the
  page). The app's Settings that write the device's own files (es_systems,
  BananaOS) do nothing useful here.
- The browser may not show a Bluetooth controller until one of its buttons
  is pressed with the page open.
- Sound starts with the first click or key on the page (browsers' rule).

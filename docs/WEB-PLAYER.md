# The web player

GBA DS in a browser, for testing builds without the device (and, later,
perhaps for players). It runs a dev build's `web/gba-ds.wasm` (the same file
State Saver's GBA DS tab runs) for the screens of any device GBA DS knows,
with sound, every button, remappable keys and controllers, and the bottom
screen as the touchscreen. Work on it happens on the `web-player` branch
until it's merged into `dev`.

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

## How it works

- `player.js` (the page): files dropped or chosen (`takeFiles`: a dev zip,
  a game or zipped game, `.sav` / `.srm`, `.ss` / `.state`, a bug report zip,
  whose `GAME-DATE.state` it loads), IndexedDB (`gba-ds-player`: the last
  dropped build, the last 10 games, and the app's own files), the device's
  screens (`DEVICES`, from src/ui.c knownDevices, plus custom ones) and where
  they go on the page, input, sound and the panels.
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
near 3 × `audio_latency` means nothing pulls it). No ROMs here: a few lines
of ARM assembled with clang (`--target=armv4t-none-eabi`) and linked at
0x08000000 make a test ROM (colour bars, a tone, red while A is held).

## Known limitations

- The capture worker needs a free CPU core to help: on a computer with
  only two (a session's container), the game and it share them, so games
  that capture often (Tactics Ogre) still run below 60 fps there (33-38
  fps against 10-13 with captures in the game, measured Nov 7, 2026).

- Map captures and logo extraction run inline (the web build has no
  threads), so a capture can make a frame late, as in State Saver's tab.
- No RetroAchievements and no updates in the browser (no network from the
  page). The app's Settings that write the device's own files (es_systems,
  BananaOS) do nothing useful here.
- The browser may not show a Bluetooth controller until one of its buttons
  is pressed with the page open.
- Sound starts with the first click or key on the page (browsers' rule).

## For the merge

Changelog line (under the open version):

- New: a web player for GBA DS, one .html file or a folder for a web
  server. Drop a dev build and a game on it to play with the screens of any
  supported device, with sound, remappable keys and Bluetooth or USB
  controllers, and the bottom screen as the touchscreen.

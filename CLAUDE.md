# GBA DS: notes for Claude sessions

GBA DS is a GBA emulator (libmgba) for dual-screen handhelds, built for the
**Anbernic RG DS Plus on ROCKNIX** (two 1024x768 screens, the bottom one a
touchscreen; the RG DS has two 640x480 ones). The game runs on one screen; the other shows the game's own
map (captured from a hidden second copy of the game) or the game's logo.
User docs live in `wiki/` (published to the GitHub wiki by
`.github/workflows/wiki.yml` when `main` moves, i.e. at release time; edit
the pages there for user-visible changes, never the wiki directly); the
README is the overview and build guide; CHANGELOG.md the history.

The owner (kyannon) tests every build on real hardware and reports back
with photos and screenshots; nothing ships untested on the device.

## Conventions

- **Branches:** ROCKNIX and shared work goes to `dev`; Android work to
  `android` (see "More than one session" below). `main` only moves at
  release time, fast-forwarded to the release commit. The owner creates
  GitHub releases and tags by hand (sessions get HTTP 403 on releases and
  can't push tags).
- **Commits:** kyannon is the author and Claude the (signed) committer, so
  GitHub shows "kyannon authored and Claude committed" with a Verified
  badge. Commit with the session's own git identity (Claude
  <noreply@anthropic.com>, which signs) and give the author on each commit:
  `git commit --author="kyannon <341221214+kyannon@users.noreply.github.com>"`.
  Don't set `user.name` / `user.email` to kyannon (in the clone's config or
  with `git -c`): that makes kyannon the committer too, and the signature no
  longer matches. Add the Co-Authored-By / Claude-Session trailers the
  session asks for. Commits before Oct 16, 2026 were made as kyannon alone
  and stay that way (never rewrite pushed history).
- **VERSION** holds `X.Y.Z-dev` (or `X.Y-dev`) on `dev`; CHANGELOG.md has a
  `## vX.Y.Z (not yet released)` section on top. Add a line there for every
  user-visible change, written for players (not implementation notes).
- **Release:** merge `android` into `dev` if it has anything new. Set
  VERSION to `X.Y.Z` and date the changelog section, commit
  "Release vX.Y.Z", push `dev`, push that commit to `main`. Two files:
  - the Linux zip, `gba-ds-vX.Y.Z-linux-aarch64.zip` (ROCKNIX and BananaOS),
    built from a fresh clone of the release commit (`scripts/build-rocknix.sh`);
  - the APK, `gba-ds-vX.Y.Z-android.apk`: the artifact of the Android
    workflow run for the release commit on `dev` (built and signed with the
    release key by CI; the owner downloads it from the run page, the zip
    holds the APK). Check that run shows no "debug-signed" warning.
  Write release notes (What's new / Fixes / Install / upgrade, for both
  platforms, with the SHA-256 of both files; the APK's is the run's "APK
  SHA-256" annotation, readable from the run page or the public API:
  check-runs/<job id>/annotations) and send them with the zip.
  Then bump `dev` to the next `-dev` version with an empty changelog
  section.
- Test builds for the device: `dist/gba-ds-vX.Y.Z-dev-linux-aarch64.zip`,
  sent to the owner after each change; Android test APKs are the artifacts
  of the workflow runs on `android` / `dev` (link the run).

## More than one session

The owner runs a main session (ROCKNIX work, `dev`, `main`, releases) and an
Android session at the same time. To keep them from colliding:

- **The main session owns `dev`, `main`, the changelog and releases.** The
  Android session works on `android` only. When an Android milestone is
  done and tested on the device, it merges `android` into `dev` in one step
  (and nothing else on `dev`). Before starting new Android work, it merges
  `dev` into `android` so it builds on current shared code.
- **Only the main session edits CHANGELOG.md.** The Android session lists
  its player-facing changes under "For the next release" in
  `docs/ANDROID.md`; the main session moves them into the changelog and the
  release notes at release time, and empties that list. Android-specific
  notes for sessions go in `docs/ANDROID.md`, not here, except for build and
  release steps both sessions need.
- **Generated files are regenerated, never merged by hand.** For
  `src/brand_data.c` (artwork only now; menu text needs no regenerating),
  each side edits `tools/make_brand.py`; whoever merges re-runs it (needs the
  fonts; ask the owner) and commits the result. Same for `src/logo_masks.c`
  and `tools/make_logo_masks.py`, and `src/fonts_data.c` /
  `tools/embed_fonts.py`.
- **Android-only code stays out of the shared paths:** `src/android.c`,
  `android/`, or `#ifdef __ANDROID__`. A change to how shared code behaves
  (`ui.c`, `game.c`, `library.c`, `emu.c`...) affects ROCKNIX too: build
  and test the ROCKNIX side as well, and say so in the commit message.
- **Fetch before starting and before pushing.** `git fetch` the branch
  you're on and the other one; if the push is rejected, rebase onto (or
  merge) the remote and resolve conflicts, keeping both sessions' work.
  Never force-push or drop the other session's commits.
- One session at a time on a given task; when the owner switches sessions,
  read the other session's latest commits on `dev` / `android` first.

## Building

- Desktop (for development and tests): `scripts/setup-host.sh` once (fetches
  and builds mGBA and SDL2 into `deps/`), then `make` and `make tools`.
- Device: `scripts/build-rocknix.sh` (fetches an aarch64 toolchain, mGBA
  and SDL2 into `deps/`) produces the release zip in `dist/`.
- Android: `scripts/build-android.sh` (needs the Android SDK/NDK, so it
  runs on GitHub Actions: `.github/workflows/android.yml`, APK in the run's
  artifacts). See `docs/ANDROID.md`.
- libmgba options live in `scripts/mgba-options.sh` (shared by all builds). It's
  a full mGBA configuration with everything optional off, not
  `LIBMGBA_ONLY` (that forces zlib off; .zip ROMs need it), and LTO off.
- **UI text is drawn in Jost** (OFL, `assets/fonts/`, embedded into
  `src/fonts_data.c` by `tools/embed_fonts.py`) with stb_truetype
  (`src/textfont.c`): `ui_label` (bold, centred), `ui_font_text` /
  `ui_font_wrap` (TF_BOLD or TF_BODY, any UTF-8). New text is just a string;
  write it in normal sentence case. The pixel font (`ui_text`) is left for
  the FPS counter and version number.
- Generated sources: `src/fonts_data.c` (above), `src/brand_data.c` (artwork
  only: the logo, button prompts and flags; `tools/make_brand.py` needs the
  Futura Bold Italic and Gamecuben fonts, which aren't in the repo: ask the
  owner for them) and `src/logo_masks.c` (`tools/make_logo_masks.py`).

## Testing without the device

- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  GBADS_WINDOW=1024x1536 build/host/gba-ds --state-dir=DIR [--load-state=X.ss]
  --input-script=SCRIPT ROM`. Script lines: `TICK BUTTONS` (A, B+UP, - for
  none), `TICK SHOT:file.bmp` (both screens), `TICK TAP:x,y` (fractions of
  the whole window), `TICK QUIT`. `GBADS_DEBUG=1` logs map captures,
  snapshot and control-scheme switches, taps.
- `build/host/harness ROM OUTDIR SCRIPT` drives the core frame by frame:
  `load/save NAME`, `wait N`, `hold BUTTONS N`, `shot NAME`, `peek ADDR LEN`,
  `poke ADDR LEN VAL`, `io OFFSET N` (I/O registers), `dumpram NAME`, `layershot NAME` (each BG layer and
  OBJ alone), `map NAME` (run a map capture), `logo NAME`.
- ROMs and save files are not in the repo. The owner attaches them; ask.
- State Saver (`tools/statesaver/`, published as the owner's artifact) has a
  GBA DS tab: it runs a dev zip's `gba-ds/web/gba-ds.wasm` (`scripts/build-web.sh`,
  run by build-rocknix.sh for `-dev` versions; wasm32-wasi, no threads, so map
  captures and logo extraction run inline; asyncify around `web_sleep`) with
  `tools/statesaver/gbads-runtime.js` (in-memory WASI files). Republish it with
  that file beside the page when either changes. `web_*` exports in web/web.c,
  game.c and ui.c (`#ifdef __wasi__`).
- Verify visual changes by rendering screenshots and looking at them before
  sending a build.

## Architecture (src/)

- `main.c` start-up; `config.c` settings (`gba-ds.cfg`, `prefs.cfg`, `--key=value`).
- `emu.c` the libmgba wrapper: frames, states, audio, cheats, per-renderer
  sprite hiding (SpriteRule), layer toggles, ROM headers (also inside
  .zip / .7z), and the HUD split (`emu_hud_split`: a second software
  renderer fed the same register / palette / OAM writes draws only some
  layers and sprites into its own buffer; Mario Kart's race HUD). The
  harness has `split BGMASK PRIO` / `split off` and `hudshot NAME`.
- `profiles.c/.h` everything game-specific (MapProfile): how to open the
  map, screen and memory probes, crops, captureHide/underHide sprite rules,
  logo extraction, regional variants (`variants[]` + patch functions), and
  for logo-only games (Tony Hawk's Pro Skater 2) the `snap` screen copy and
  `controls` four-face layout (Super Mario Advance 2: `controls.snes`, with the camera pan poke done in game.c's run_frame), (Mario Kart; Super Mario Advance 2 in levels, beside its map capture) the `hud` split and when it's on. Adding a game mostly means a profile.
- `mapcap.c` map capture on a hidden second core in a background thread.
- `mapframe.c` generated map surrounds; `logo.c` logo / frame extraction.
- `game.c` a play session: input, the second screen (map, logo, THPS2
  snapshot and Controls picker), pause menu, cheats screen, screenshots.
- `ra.c` RetroAchievements (casual only) via rcheevos' rc_client, vendored in
  `third_party/rcheevos` (v12.5.0, trimmed: no hashing beyond md5.c, no
  libretro/RAIntegration). Server calls run the system `curl` in a worker
  thread; responses go back to rc_client on the main thread (`pump`).
  Account: ROCKNIX system.cfg `global.retroachievements.*` (or
  ra_username/ra_password); token cached in `states/ra/login.cfg`. Popups on
  the second screen. Built where GBADS_RA is defined (Makefile); `ra.h` has
  no-op stubs elsewhere (Android: no curl yet). Test offline with
  `tools/ra_mock.py` (a stand-in server on 127.0.0.1:8099) and
  `--retroachievements=1 --ra-username=tester --ra-password=secret
  --ra-host=http://127.0.0.1:8099`. Hardcore is not allowed before the app
  has been public for 6 months (from Oct 13, 2026).
- `library.c` the game list and its Settings screen; `esinteg.c` the
  EmulationStation integration; `cheats.c` RetroArch .cht files;
  `ui.c` SDL windows, input, drawing, the pause-menu text images.

## Device facts

- ROCKNIX on the RG DS Plus, sway compositor; the app spans both screens
  with one window ("fullscreen global"). Face buttons arrive by position:
  SDL "B" is the right button (GBA A), "A" the bottom, "X" the top, "Y" the
  left.
- Outputs: DSI-2 is the top panel (ROCKNIX's main output, `$WLR_CON`), DSI-1
  the bottom. **Stock ROCKNIX keeps DSI-1 powered off**, gives it no fixed
  position, and its sway-touch service maps the touchscreen to DSI-2. ROCKNIX's
  udev calibration puts bottom-panel touches in the right half of the layout.
  `dist/screens.sh` (run by launch.sh) powers DSI-1 on, places it right of
  DSI-2, maps touch to the whole layout, and restores all of it on exit.
- ROCKNIXDS (github.com/JorreFog/ROCKNIXDS, no longer developed) is ROCKNIX
  with dual-screen fixes (DSI-1 on, side by side, sway-touch off). The owner
  developed against it at first; GBA DS must work on stock ROCKNIX on its own.
  For how ROCKNIX drives the screens, see its `sway` package (111-sway-init),
  `vertical-check` and the RG DS device quirks.
- Installed at `/storage/roms/ports/gba-ds/`, ROMs in `/storage/roms/gba/`.
- **BananaOS** (github.com/JorreFog/BananaLinux; Buildroot, glibc, no compositor,
  no udev, libcurl but no curl program): the same zip in `/data/roms/ports`,
  run by Banana's `run-port` (bash, SDL_KMSDRM_FIRST, bottom panel's
  backlight off). launch.sh detects `/etc/banana`, turns the backlight back
  on, and by default uses SDL's KMSDRM driver with the dual layout (a window
  per panel, GPU straight to each; --touch-map=screen): full speed on the RG
  DS Plus. GBADS_BANANA_VIDEO=evdev / offscreen use Banana's SDL patch
  (SDL_BANANA_PANELS=side, one window over both panels, copied each frame:
  24-30 / under 5 fps). It passes Banana's paths (`/data/roms/gba`,
  `/data/saves`, `/data/screenshots`, `/data/retroarch/cheats`) for settings
  still at ROCKNIX's defaults, plus the RA account from
  `/data/banana/accounts.conf`; ra.c falls back to libcurl (SDL_LoadObject)
  when there's no curl program. `gamecontrollerdb.txt` in the zip maps
  retrogame_joypad (Banana ships no SDL mapping). Cheats: `gba-ds/cheats`
  (assetDir) is searched before cheat_dir everywhere but Android.
  Banana's own GBA list (esinteg.c banana_*; the game list's Settings row on
  Banana): writes /data/banana/systems.conf (a copy of /etc/banana's, which
  banana-ui-session uses instead at the menu's next start), [gba] run line
  -> `bash '<dir>/banana-play.sh' %ROM%`, the original kept as
  `# gba-ds: run = ...`. banana-play.sh runs `gba-ds --supported ROM` (exit
  0 if a profile exists): GBA DS (banana-perf performance), else the
  original line. Off restores it; the copy is deleted if ours and unchanged.
  banana-ui hands both panels to any run line without "run-retroarch".
- EmulationStation: `/usr/bin/runemu.sh` launches games; the GBA command is
  `/usr/bin/runemu.sh %ROM% -P%SYSTEM% --core=%CORE% --emulator=%EMULATOR%
  --controllers="%CONTROLLERSCONFIG%"`.

## Ongoing work

- Android version: see `docs/ANDROID.md`.
- Experimental since v1.1 (untestable on the owner's RG DS Plus with GammaOS):
  starting games from Android frontends, and the pause menu on returning to
  the app. Needs a tester with a more capable Android device.

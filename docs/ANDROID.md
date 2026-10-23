# Android version: plan

Status: released in v1.1 (milestones 1-3). Starting games from frontends
and the pause menu on returning to the app are experimental (untested on a
device). The APK ships with the ROCKNIX zip (CLAUDE.md, Release).
See "Progress" at the end.

## Goal

An APK of GBA DS for Android-based firmware on dual-screen handhelds (first
target: GammaOS on the owner's device; see "Facts from the owner" below),
with the same behaviour as the ROCKNIX build: game on one screen, map or
logo (and the Tony Hawk goal sheet and Controls picker) on the other, the
pause menu and game list on the touchscreen.

The ROCKNIX build stays the main product. Android work happens on an
`android` branch off `dev` and is merged into `dev` once it builds and the
ROCKNIX build is unaffected.

## What carries over

Everything game-side is portable C: `emu.c`, `mapcap.c`, `profiles.c`,
`logo.c`, `mapframe.c`, `cheats.c`, and most of `game.c` / `library.c`.
libmgba builds with the Android NDK (mGBA has its own Android-friendly
CMake), SDL2 runs on Android (SDLActivity), audio and gamepads come through
SDL.

## What needs work

1. **The second screen.** SDL2 on Android gives one window on the main
   display only. The other display needs a small Java layer: an Android
   `Presentation` (a window on a secondary `Display`) holding a SurfaceView,
   whose Surface is passed to native code (ANativeWindow). Plan: keep SDL for
   the main screen; render the second screen into a software
   `SDL_Renderer` (on an SDL_Surface) with the existing ui.c drawing calls,
   and copy it into the ANativeWindow each frame. `ui.c` already treats each
   screen as a "view" with its own renderer, so the second view becomes the
   software one.
2. **Touch on the second screen.** Touches on the Presentation arrive in
   Java; forward them to the native pointer queue (ui.c `queue_pointer`).
3. **Buttons.** Android keeps HOME (and often BACK) for itself. The pause
   menu needs another trigger (tap the touchscreen already works; add a
   combo such as SELECT+START, configurable). Check how GammaOS maps the
   face buttons (SDL's positional names; see CLAUDE.md "Device facts").
4. **Files.** Ask for "all files access" (MANAGE_EXTERNAL_STORAGE; fine for
   a sideloaded app) so the game list can read a ROM folder anywhere, with
   Android-style defaults: ROMs in `/storage/emulated/0/ROMs/gba`, data
   (states, logos, prefs, screenshots) in the app's own folder. Make the ROM
   folder choosable in Settings.
5. **ROCKNIX-only parts** become no-ops on Android: sway window placement,
   `esinteg.c` (EmulationStation), the `launch.sh` wrapper.
6. **Launching from frontends** (optional, later): an intent filter so
   frontends like Daijisho / ES-DE can start GBA DS with a ROM path.


- **Map Buttons (from the main session, Oct 22, 2026):** the game list's
  Settings on Linux has a button mapper (`src/mapper.c`, shared; raw input
  from `ui_raw_capture` / `ui_next_raw` in ui.c; mappings kept in
  `<state_dir>/controllers.txt`, loaded in `ui_init`). Now on Android too
  (Oct 22): mapper.c in CMakeLists, a third Settings row (rows a bit
  shorter when there are three, so Back fits). The A/B swap for GammaOS's
  label-based layout and the raw 15/16 L2/R2 buttons now apply per
  controller, and not to one whose mapping came from controllers.txt
  (ui.c padUserMapped: the GUIDs in that file, plus `ui_mapped_by_user()`
  from the mapper), since the mapper records by position. BACK
  (SDL_SCANCODE_AC_BACK) cancels the mapper like Escape; the mapping's
  comment names the device from ro.product.* / ro.build.*. Fixed for every
  system: the done screen couldn't be closed with a button (it checked that
  everything was released in the same frame as the press).
## RetroAchievements

`src/ra.c` and `third_party/rcheevos` are compiled into libmain with
GBADS_RA (CMakeLists: rcheevos as its own static library, the Makefile's
RC_SRC). Differences from ROCKNIX:

- HTTP: Android has no curl, so `run_job()` calls `android_http()`
  (android.c), which calls `Http.request()` (Java, HttpURLConnection, the
  system's certificates) from ra.c's worker thread (SDL attaches it to the
  JVM). The Http class is looked up in libmain's JNI_OnLoad: FindClass on
  a native thread can't see the app's classes. The manifest has INTERNET.
- Account: no system.cfg. The game list's Settings has a RetroAchievements
  row: Sign In opens a dialog (GbaDsActivity.raSignInDialog, username and
  password, on-screen keyboard); the answer goes to `ra_sign_in()`, which
  signs in with the password and keeps only the token
  (`<state_dir>/ra/login.cfg`, as on ROCKNIX). At start-up `ra_init` signs
  in with that token. Sign Out (`ra_sign_out`) deletes it. ra_username /
  ra_password in gba-ds.cfg still override. The library loop calls
  `ra_idle()` so sign-in answers are handled there too.
- User agent: "GBADS/x (Android)".
- CLAUDE.md's ra.c line still says Android has no-op stubs (the main
  session's file): it should say ra.c is built on Android too, with HTTP
  through `android_http()`.
- Tested on the desktop with -D__ANDROID__ and stand-ins for android.c (HTTP
  through curl, a dialog answering with a test account) against
  tools/ra_mock.py: wrong password, right password, token sign-in on
  restart, unlock, badge downloads, sign-out; and the ROCKNIX build
  unchanged. Not yet on the device.

## Build pipeline

The Claude workspace can't download Google's Android SDK/NDK (blocked), so
APKs are built by **GitHub Actions** (its Ubuntu runners include the SDK and
NDK): a workflow at `.github/workflows/android.yml` builds on every push to
`android`/`dev` and uploads the APK as a run artifact. The owner downloads
it from the Actions tab and sideloads it.

- If a session can't push workflow files (GitHub needs the `workflow`
  permission for that), the session gives the owner the file to commit
  through the GitHub web UI.
- **Signing:** use one fixed keystore, stored as repository secrets
  (`ANDROID_KEYSTORE_B64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`,
  `ANDROID_KEY_PASSWORD`). A debug key generated per CI run would differ
  every build, and Android refuses to install an update signed with a
  different key (you'd have to uninstall and lose saves each time). The
  session generates the keystore (keytool is available) and hands it to the
  owner to add under Settings > Secrets and variables > Actions; the
  keystore itself is never committed.

## First milestone (proof of concept)

The APK starts a ROM from a fixed path, plays it on the main screen with
sound and buttons, and shows the game's logo on the second screen, with a
tap on the second screen logged. That settles the second-display question
before the rest is ported. Then: game list, pause menu, map capture,
settings, file access.

## Facts from the owner (fill in before starting)

- Device model and GammaOS version: RG DS Plus, GammaOS 1.4.4 (core)
- Android version (Settings > About): Android 14 (20261002)
- Output of `adb shell dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"`
  (how the second screen is exposed):

1|tdgsi_arm64_ab:/ # dumpsys display | grep -E "mDisplayId|DisplayDeviceInfo"
  DisplayDeviceInfo{"Built-in Screen (port 0)": uniqueId="local:0", 1024 x 768, modeId 1, renderFrameRate 60.000004, defaultModeId 1, userPreferredModeId -1, supportedModes [{id=1, width=1024, height=768, fps=60.000004, vsync=60.000004, alternativeRefreshRates=[], supportedHdrTypes=[]}], colorMode 0, supportedColorModes [0], hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[], mMaxLuminance=500.0, mMaxAverageLuminance=500.0, mMinLuminance=0.0}, allmSupported false, gameContentTypeSupported false, density 240, 218.568 x 136.413 dpi, appVsyncOff 1000000, presDeadline 16666666, touch INTERNAL, rotation 0, type INTERNAL, address {port=0}, deviceProductInfo null, state ON, committedState ON, frameRateOverride , brightnessMinimum 0.0, brightnessMaximum 1.0, brightnessDefault 0.39763778, hdrSdrRatio NaN, FLAG_ALLOWED_TO_BE_DEFAULT_DISPLAY, FLAG_ROTATES_WITH_CONTENT, FLAG_SECURE, FLAG_SUPPORTS_PROTECTED_BUFFERS, FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS, FLAG_TRUSTED, installOrientation 0, displayShape DisplayShape{ spec=-1440493287 displayWidth=1024 displayHeight=768 physicalPixelDisplaySizeRatio=1.0 rotation=0 offsetX=0 offsetY=0 scale=1.0}}
  DisplayDeviceInfo{"Built-in Screen (port 1)": uniqueId="local:1", 1024 x 768, modeId 2, renderFrameRate 60.000004, defaultModeId 2, userPreferredModeId -1, supportedModes [{id=2, width=1024, height=768, fps=60.000004, vsync=60.000004, alternativeRefreshRates=[], supportedHdrTypes=[]}], colorMode 0, supportedColorModes [0], hdrCapabilities HdrCapabilities{mSupportedHdrTypes=[], mMaxLuminance=500.0, mMaxAverageLuminance=500.0, mMinLuminance=0.0}, allmSupported false, gameContentTypeSupported false, density 240, 113.579 x 136.413 dpi, appVsyncOff 1000000, presDeadline 16666666, touch INTERNAL, rotation 0, type INTERNAL, address {port=1}, deviceProductInfo null, state ON, committedState ON, frameRateOverride , brightnessMinimum 0.0, brightnessMaximum 1.0, brightnessDefault 0.5, hdrSdrRatio NaN, FLAG_ALLOWED_TO_BE_DEFAULT_DISPLAY, FLAG_ROTATES_WITH_CONTENT, FLAG_SECURE, FLAG_SUPPORTS_PROTECTED_BUFFERS, FLAG_PRESENTATION, FLAG_SHOULD_SHOW_SYSTEM_DECORATIONS, FLAG_TRUSTED, FLAG_OWN_DISPLAY_GROUP, installOrientation 2, displayShape DisplayShape{ spec=-1440493287 displayWidth=1024 displayHeight=768 physicalPixelDisplaySizeRatio=1.0 rotation=0 offsetX=0 offsetY=0 scale=1.0}}
    mDisplayId=0
    mDisplayId=2
  mDisplayId=0
  mDisplayId=0
  mDisplayId=0
  mDisplayId=: 0
  mDisplayId= 0
  mDisplayId=0
  mDisplayId=0
  mDisplayId=2
  mDisplayId=2
  mDisplayId=2
  mDisplayId=: 2
  mDisplayId= 2
  mDisplayId=2
  mDisplayId=2
  
- Which screen has touch, and does touch on it reach apps? Bottom screen. Yes.
- Does the device have the RG DS Plus's button layout? Anything special
  about HOME / BACK / SELECT+START in GammaOS? It is an RG DS Plus.
- Where ROMs live on the device: Uri is content://com.android.externalstorage.documents/document/primary%3AROMs

## Progress

### First milestone (branch `android`)

How it's put together:

- `android/`: a Gradle project. `GbaDsActivity` (an SDLActivity) runs the
  game on its display; `SecondScreen` (a Presentation) covers the other
  display with a SurfaceView. `LauncherActivity` (the app icon) asks for
  All files access once, then starts the game on display 0. SDL's Java
  files are compiled straight from the SDL source in `deps/SDL2`, so they
  always match libSDL2.so.
- `android/app/src/main/cpp/CMakeLists.txt` builds libSDL2.so from source
  and libmain.so from `src/` (the same files as the ROCKNIX build) plus
  `src/android.c`; libmgba is built first by `scripts/build-android.sh`
  with the ROCKNIX build's options (`scripts/mgba-options.sh`).
- `ui.c`: on Android, view 1 is a software SDL_Renderer on an SDL_Surface
  the size of the second display; `ui_present` copies it to the
  Presentation's ANativeWindow. Second-screen touches are pushed into SDL's
  event queue as finger events with window id `GBADS_SECOND_WINDOW_ID`, so
  `queue_pointer` sees them as view 1. Every touch down/up is logged. The
  touch screen (menus) is whichever screen was touched last. BACK acts as
  HOME (SDL traps it). All Android code is `#ifdef __ANDROID__`.
- `main.c`: config, states and the log live in the app's folder
  (`Android/data/io.github.kyannon.gbads/files`, passed as `GBADS_HOME`);
  stderr goes to logcat (tag `GBADS`) and `gba-ds.log` there. With no game
  given it plays the first game in `/storage/emulated/0/ROMs/gba` (or
  `GBA`) that has a map profile, else the first game. Another game:
  `adb shell am start -n io.github.kyannon.gbads/.LauncherActivity --es rom /path/to/game.gba`.
- CI: `.github/workflows/android.yml` (runs `scripts/build-android.sh`;
  APK in the run's artifacts; on failure the end of the log is posted as
  an annotation, readable through the public API without logs access).
- Signing keystore generated by the session that built this and handed to
  the owner (alias `gbads`, SHA-256 fingerprint
  95:24:1F:DD:11:B5:67:F6:D6:80:C4:C7:67:8E:BB:F6:46:3C:52:A6:E3:C9:50:5B:14:C6:A7:96:2F:04:69:58).

What the device test should answer (`adb logcat -s GBADS`):

- Do both screens show (game on display 0, logo then map on display 2)?
- Which display is the touchscreen: the log says `touch down on screen N
  (game activity's / second display)`. If display 0 is the bottom one,
  the game is on the bottom: L2 or the menu's Swap moves it (the pause
  menu opens on the screen that was touched).
- Sound, buttons (face buttons by position, as on ROCKNIX?), speed.
- Do buttons keep working after touching the second screen? (The
  Presentation is non-focusable so it shouldn't take key focus.)

First device test (2026-10-14): game playable, buttons, menu and touch
work; both screens only with GammaOS's "Run on primary screen"; A and B
swapped; 20-30 fps. Changes since:

- A/B: GammaOS reports face buttons by their Nintendo labels (SDL A =
  right). ui.c swaps SDL A/B back on Android, so everything stays
  positional as on ROCKNIX (X top, Y left already match).
- Speed: the second screen's frame is handed to the display by a thread
  of its own (ANativeWindow_lock waits for that display's refresh; doing it
  on the game loop on top of the game screen's own wait capped it at 30).
  Logged every 5 s: fps from game.c, and from ui.c the time spent per
  frame on the game screen, drawing the second screen (software), and
  handing it over.
- Speed, second round: the log showed ~50 ms a frame with the presents at
  ~3 ms; the software renderer draws during the draw calls (no batching),
  so the second screen's cost wasn't being timed. Now ui.c records the
  draw calls aimed at the software view and replays them at ui_present
  only when they differ from the last frame's (calls + texture versions;
  game.c updates textures through `ui_update_texture` so changes count),
  plus a redraw once a second as a safety net. Logged every 5 s: emulating
  and drawing time per frame (game.c) and how many frames the second
  screen was redrawn and at what cost (ui.c). Checked on the desktop with
  the Android paths and stub glue: the replayed pause menu matches the
  directly drawn one, and it redraws only on changes.
- Speed, third round: a map redraw in software took 41 ms (log), so map
  updates and animations still dropped frames. The second screen is now
  drawn by the GPU: android.c makes SDL's OpenGL ES context current on an
  EGL window surface on the Presentation's window (same EGLConfig, buffers
  the game screen's size so SDL's viewport fits; swap interval 0), and
  ui.c replays the recorded calls through the game screen's renderer
  between `android_second_begin` / `_end`. View 1 shares view 0's
  renderer (`View.offwin`). The software renderer remains the fallback
  (no GLES renderer, or `GBADS_SOFT_SECOND` set). Tested on the desktop
  with the offscreen driver (also GLES/EGL) and a pbuffer standing in for
  the window: menus match the software path, render targets and the LCD
  grid work, the game screen is unaffected. Screenshots read the second
  screen back while it is the current surface.
- Start-up (5-10 s of black reported): the ROM pick reads every ROM's
  header (unpacking archives) until it finds one with a map profile; the
  result is now kept in `last-rom.txt` in the app folder and reused.
  Start-up steps are logged (`startup:` lines; logcat timestamps time them).
- Display: GbaDsActivity has a task affinity of its own and is started
  with FLAG_ACTIVITY_NEW_TASK, so the display-0 launch request isn't
  ignored when the launcher's task sits on the other display.

Device result (2026-10-14, Circle of the Moon, GPU second screen): a
steady 59.6-60 fps, no audio underruns, emulation ~6 ms and drawing ~3 ms
a frame; the second screen redraws only on changes (5 per 5 s with the
map still, ~76 with map animations) at 2-4 ms each. Start-up from the
icon to the first frame ~3.2 s with last-rom.txt (about 0.75 s in the
activity loading libraries, ~0.9 s before SDL starts the native thread,
~1 s loading the game and the map profile). The built-in pad appears as
"Xbox Series X Controller". All touches came from display 0 (the game
activity's display), so display 0 is the touchscreen.

After the first milestone:

- Screens in physical order (ui_layout): screen 0 = top = the second
  display (view 1), screen 1 = bottom = the activity's display 0 (view 0,
  the touchscreen), so the defaults match ROCKNIX: game on top, menus on
  the bottom.
- L2 / R2 (screen swap, fast-forward) didn't work: the triggers send
  digital KEYCODE_BUTTON_L2 / R2, which SDL turns into raw joystick
  buttons 15 / 16 that its Android controller mapping leaves out (it only
  maps analog trigger axes). ui.c reads them directly on Android; raw
  buttons from 15 up are logged.

### Second milestone (done, tested on the device; merged into `dev` at c1e9070, the first milestone at 68572d1)

- Game list on Android (main.c no longer picks a game; the milestone-1
  `last-rom.txt` is gone). Its Settings screen (SELECT, or the Settings
  button) holds "ROM Folder" on Android instead of the EmulationStation
  switch: Android's folder picker (ACTION_OPEN_DOCUMENT_TREE, opened from
  the game thread through JNI on the activity), the tree URI turned into a
  plain path (All files access), saved as `rom_dir` in
  `<state_dir>/library.cfg` (loaded after prefs.cfg on every platform).
  library.cfg also holds `library_view` (list/grid) on every platform;
  both are written by `save_library_cfg()` in library.c, so neither is lost.
  "ROM Folder" is a Futura label (MENU_TEXTS; brand_data.c regenerated).
- Every game was listed twice on the device: the default rom_dir had
  ROMs/gba and ROMs/GBA, and shared storage ignores case, so they're the
  same folder. Default is now ROMs/gba alone, and the library skips a
  folder it has already read in a scan (by device + inode).
- Tony Hawk dipped into the low 50s (log: emulating ~9 ms, drawing ~7 ms;
  the THPS2 extras measured at ~0 on the desktop). With the game on top,
  the top screen is the second display, redrawn every frame, and each
  frame switched the GPU between the two surfaces twice and redrew the
  static bottom too. Now both screens are retained (recorded, replayed only
  on change; `View.retained`), the second surface stays current after a
  redraw until the bottom needs drawing (android_second_restore), and the
  game's frame is uploaded only to the view that shows it. Emulation:
  a performance hint session (APerformanceHint, Android 13+, via dlsym)
  reports each frame's work against the 16.7 ms deadline so the CPU clock
  is raised in time; libmgba and the app are compiled with
  -mcpu=cortex-a55 like the ROCKNIX build. The 5 s log now has per-screen
  redraw counts and the second display's switch/swap timings.
  Device result (Tony Hawk, 2026-10-14): drawing ~3 ms a frame (was ~7),
  top screen 2.5 ms per redraw with swap 0.8 ms, bottom redrawn 5 of 300
  frames, 5 switches back per 5 s at 0.08 ms; emulation 6-8 ms (was ~9);
  ~60 fps.
- `menu_combo = select+start` (Android default; `none` elsewhere): opens
  the pause menu. The first of the two buttons is held back from the game
  for up to 5 frames; a quick tap is passed on after that.
- Resume point saved on SDL_APP_WILLENTERBACKGROUND (ui.c calls the
  game's handler from ui_events; SDL 2.30 blocks the thread at the next
  pump). Battery saves were already safe: mGBA syncs the mapped .sav
  shortly after each write.

### Third milestone (released in v1.1; frontend launching and pause-on-return experimental)

Tested on the device: the GBA DS data folder, cheats, screenshots, resume
points. Not testable on the owner's RG DS Plus (GammaOS doesn't handle app
switching well enough): starting games from frontends, switching games while
running, the pause menu on returning. Marked experimental in the README and
CHANGELOG until someone tests them on a more capable device.


- Launching from frontends. LauncherActivity (exported) takes the game
  from an extra (`rom`, also `ROM`, `path`, `file`, `romPath`, `GamePath`:
  a path, or a file:// / content:// URI) or from the intent's data. A
  content URI from Android's external storage provider becomes a plain
  path (All files access); anything else is copied into the app's
  `launched/` folder. With a game given, the game list is skipped and
  "Close GBA DS" goes back to the frontend. If GBA DS is already running
  with another game (or the game list), GbaDsActivity.onNewIntent sends
  SDL a quit (the game saves its resume point), and onDestroy starts the
  launcher again with the new game before exiting the process.
  ES-DE (custom systems):
    es_find_rules.xml:
      <emulator name="GBA-DS">
          <!-- Game Boy Advance, dual screen: GBA DS -->
          <rule type="androidpackage">
              <entry>io.github.kyannon.gbads/.LauncherActivity</entry>
          </rule>
      </emulator>
    es_systems.xml, in the gba system:
      <command label="GBA DS (Standalone)">%EMULATOR_GBA-DS% %ACTIVITY_CLEAR_TASK% %ACTIVITY_CLEAR_TOP% %DATA%=%ROMSAF%</command>
    (or %EXTRA_rom%=%ROM% instead of %DATA%=%ROMSAF%)
  Daijisho player: package io.github.kyannon.gbads, activity
  .LauncherActivity, `-a android.intent.action.VIEW -d {file.uri}` (or
  `-e rom {file.path}`).
- Data folder (GBADS_HOME): "GBA DS" on the internal storage when All
  files access is granted (else the app's folder), laid out like the
  ROCKNIX install folder: states/ (with .nomedia), cheats/ (cheat_dir),
  overlays/ and logos/ (asset_dir), screenshots/ (screenshot_dir),
  gba-ds.log. android_config_defaults makes the folders with README.txt
  files. GbaDsActivity.dataFolder() copies states/ over from the app's
  folder once (builds before this used it). RetroArch's own folders
  aren't readable by other apps on newer Android, hence cheats/ there.
- Back from Home / the app switcher mid-game, the pause menu is open
  (SDL_APP_DIDENTERFOREGROUND -> game.c on_foreground).

Not done yet (next): game list and pause menu polish, SELECT+START menu
combo, saving when Android pauses or kills the app, file access settings,
frontend launch intents.

## Launching from GammaOS, and finding slow frames

- GammaOS's Game Systems editor (Launch Type: Custom Package; Package;
  Intent Template; Launch Args) uses Daijisho-style templates, e.g. for
  AetherSX2 `-n <pkg>/.EmulationActivity -a android.intent.action.MAIN -e
  bootPath {file.uri}`. With only the package and the default template, the
  owner's attempt failed. LauncherActivity now has VIEW intent filters
  (file and content schemes, with and without a MIME type), takes more
  extra names (bootPath, uri, filePath...), the ClipData URI, and a
  provider's real path (`_data`, or /storage/... inside the URI) before
  copying. Every launch logs the whole intent ("launched with action=...")
  so a failing frontend shows what it sent.
- Owner's test: the explicit template `-n io.github.kyannon.gbads/.LauncherActivity
  -a android.intent.action.VIEW -d {file.uri}` works; the default one gave
  a black screen and no gba-ds.log (so it never reached the game). Likely
  cause: the launcher read every extra, and a Parcelable extra of a class
  only GammaOS has throws on reading (BadParcelableException), uncaught.
  Now each extra is read on its own inside try/catch, the ROM is resolved
  (and copied if it must be) on a worker thread, and `GBA DS/launch.log`
  records every launch request, the game found, and any Java crash
  (default uncaught-exception handler), so failures are visible without adb.
- Achievement popups: ra_draw fitted its text every frame (tf_width per
  letter cut, GPOS kerning: ~0.3 ms per frame on a desktop CPU, several
  times that on the A55). ui.c now caches text widths (WIDTH_CACHE), which
  also helps labels and wrapped text everywhere.
- Device log after that (Circle of the Moon, 2026-10-15): ~59-60 fps, a
  few frames a second over 16.7 ms (17-25 ms): map work 5-9 ms (the still
  map composed on every capture), emulation 13-20 ms at times, drawing
  10-20 ms on some frames. Changes: the performance hint's target is 65%
  of a frame (11 ms; with the full 16.7 ms ADPF clocked the CPU down until
  frames barely fit); the still map (compose_still: flood fill + sorts) is
  built only when map animations are off (stillStale, built on toggling
  them off); the game thread asks for nice -4 (logged); the slow-frame
  line splits drawing into top / bottom screen. The map capture thread was
  already at nice +10.
- Device log after that (CotM, run 38767740847): 59.5-60 fps in every 5 s
  window; 1-3 frames a second at 17-21 ms (average work ~11 ms = the 65%
  target exactly: the governor clocks down to it), spread over emulation
  (spikes to 12-20 ms against a 6-7 ms average) and drawing (either screen,
  3-13 ms). Achievement popup: one 34 ms frame. Both surfaces had swap
  interval 0, where Android shows only the newest buffer at each refresh:
  a frame posted late meant the previous one shown twice and that one
  dropped, i.e. each slow frame was a visible hitch. Now the view the game
  is on gets swap interval 1 (ui_sync_view: every queued frame shown, in
  order; a late frame is shown a refresh late and the queue absorbs it),
  the other stays at 0; and the ADPF target is 50% of a frame. Watch the
  per-screen present times in the log for a swap that blocks.
- Result: the device crashed a second or two after a game started, right
  after "presenting the top screen in step with its display" (the game on
  the second display, interval 1). Cause not known yet (the logcat sent was
  filtered to GBADS). Now off by default; `vsync = 1` in GBA DS/gba-ds.cfg
  turns it on, for getting `adb logcat -b crash` from the crash.
- That log: not a crash but a hang. `adb logcat -b crash` was empty; the
  log repeated `BufferQueueConsumer: [SurfaceView[]#2(BLAST Consumer)]
  acquireBuffer: max acquired buffer count reached: 6 (max 4)` and
  `BLASTBufferQueue ... Failed to acquire a buffer`. In sync mode the
  second display's SurfaceView doesn't get its buffers back fast enough,
  the producer (eglSwapBuffers on the game thread, holding winLock) blocks
  for good, and the UI thread then waits on winLock. So interval 1 is
  never used on the Presentation surface now (vsync = 1 only affects the
  game when it's on the activity's display). The remaining idea for
  uneven frames: pace the loop to the game display's refresh
  (AChoreographer) at 60.0 Hz instead of the GBA's 59.73, with the audio
  source rate raised to match (0.45%), as RetroArch does; with timer pacing
  at 59.73 the posting time drifts across the 60 Hz refresh every ~3.7 s,
  and near the latch deadline small jitter turns into repeated and dropped
  frames.
- GammaOS's default Intent Template, per launch.log: `action=null data=null
  type=null flags=0x10800000 component=.../.LauncherActivity`, no extras, no
  ClipData. It only starts the package's launcher activity and says nothing
  about the game, so nothing can be done on our side: the explicit template
  is required (README).
- Log with the 50% performance-hint target (CotM, 2026-10-15): per-frame
  averages unchanged (emulating ~6.5 ms, drawing ~3.8 ms), 1-3 frames a
  second still at 17-21 ms: the hint's target doesn't buy faster clocks
  here, so frame speed isn't the lever. One 214 ms "emulating" frame: the
  battery save being flushed (mGBA's GBASavedataClean -> _vfdSync:
  futimens + msync + fsync, on the game thread; fsync on Android's shared
  storage is slow). emu.c now wraps the save's VFile on Android
  (async_save_open): still the mapped file, but sync hands the fsync to a
  worker thread; close waits for it. Desktop: -DGBADS_ASYNC_SAVE turns it
  on (tested: same .sav as without). Also early in that session, a few
  100-600 ms draws of one screen (frames 670-1090), cause unknown.
- Aria of Sorrow log (owner, 2026-10-15), first ~90 s with the map screen
  static (logo / no map yet: bottom redrawn 5 of 300 frames), then the map
  animating (bottom redrawn ~76 of 300): slow frames went from about one
  every 3-5 s to 1-7 every second, mostly with both screens redrawn in the
  same frame (drawing 10-14 ms). Map capture busy was about the same in
  both parts (0-20% vs 2-33%), so the hidden core isn't the cause; drawing
  both displays in one frame is. ui_present now draws the game's view
  first and, if the frame is already past 70% of its time
  (ui_present_budget; GBADS_DRAW_BUDGET=ms overrides, for testing), puts the
  other view's redraw off to the next frame, at most 3 frames in a row
  (logged: "N redraw(s) of the other screen put off a frame"). Desktop
  check with a 0 ms budget: the map still steps (73 of 300 redraws vs 75).
- After the deferral (AoS, 2026-10-15): slow frames with the map 79 in 70 s
  -> 43 in 66 s; without it 33/76 s -> 23/80 s. What's left is mostly one
  frame's emulation taking 12-17 ms against a 5-6 ms average. On the
  desktop (CotM state, tools: a 3600-frame emu_frame loop) only 12 frames
  in 3600 took over 2.5x the average; on the device about one a second, so
  most of it isn't the emulation. Owner chose tuning over a frame of
  buffering (latency). Now: the game thread is pinned to one core (fastest,
  highest-numbered of equals; android_pin_game_thread) and the worker
  threads (map capture, logo extraction, RetroAchievements, save writer,
  software second screen) to the others, since threads start on their
  creator's cores. `pin_cores = 0` in GBA DS/gba-ds.cfg turns it off, for
  comparing. The log lists the cores the app may use (with max MHz), and
  each slow frame and 5 s report says how often the game thread was
  preempted (getrusage RUSAGE_THREAD ru_nivcsw) and which cpu it was on.
- Pinning result (AoS, owner felt no difference): 32 slow frames in 74 s
  pinned, 27 in 69 s not; so pin_cores is now off by default (still there
  for testing). Cores: 4 x 2160 MHz. The game thread is preempted ~800-1500
  times a second either way (ru_nivcsw), with no link to which frames are
  slow, so the count alone says little. The slow-frame line now also has
  the thread's CPU time for the frame ("ran X ms of it (waited Y)",
  CLOCK_THREAD_CPUTIME_ID), the core's clock at that moment
  (scaling_cur_freq), and the sound hand-off's share of "emulating"
  (SDL_QueueAudio takes the lock the audio thread holds while it runs). A
  slow frame that ran its full length points at slow clocks or work; one
  that mostly waited points at another thread or a lock. (The frame
  number in those lines was 0 in the pinning build: initializer order.)
- With those numbers (AoS, 2026-10-15): steady-state slow frames ran
  9-14 ms (an ordinary frame's work) and waited 5-15 ms, mostly at full
  clock (2160 MHz); the sound hand-off was under 1 ms. So it's neither slow
  clocks nor heavier work: the thread is off the CPU for part of the frame.
  Next: the line splits the waiting into emulation (can only be another
  thread taking the core) and drawing (could also be the GPU driver or a
  surface's buffers making us wait), and the game thread asks for nice
  -10 (falls back to -8, -4; logged).
- nice -10 result (AoS, 2026-10-15, granted): slow frames 95 in 110 s
  (0.86/s) at nice -4 in the same log -> 26 in 88 s (0.30/s), about the
  no-map rate before. The rest wait a few ms in emulation and drawing
  alike. Separately, about once a session one frame waits 150-600 ms
  inside drawing the game's screen (second display's swap): a compositor
  or driver stall, not ours to fix so far.
- Frame buffer option (owner's choice after the priority change: smooth
  enough by default, but some players feel stutter more than input lag):
  `frame_buffer` (pause menu, Game page; prefs.cfg). game.c's ahead_*: a
  worker thread runs only emu_frame for frame k+1 while the loop draws and
  shows frame k (from a copy, aheadFb); the loop waits for it at the top of
  every iteration, so sound, RA, map, probes, states, cheats and the menu
  all run with the worker idle. Load/reset discard the frame emulated
  ahead; fast-forward runs inline. Desktop: same picture as without it
  when idle, one frame later when moving. Shared code, so ROCKNIX has the
  option too.
- Opening the pause menu costs one ~300 ms frame (its text images); not
  looked at yet.
- Slow frames: game.c logs, each second there are any, the worst frame's
  breakdown ("slow frames: N in the last second; worst ... input/menu,
  emulating, RetroAchievements, map, drawing (which screens redrawn)").
  Always on Android, with GBADS_DEBUG elsewhere.

- Match Display Rate (main session, v1.1.6-dev; shared code, off by
  default; `match_display`, pause menu → Performance): game.c's
  pacing_update runs the loop at ui_display_hz (on Android SDL's whole-number
  refresh_rate) when within 1.5% of 59.73, resamples the sound from
  rate * hz / 59.73, and calls ui_sync_view(game's view), i.e. swap
  interval 1 on the activity's display (never the Presentation surface, as
  before). The clock then runs 0.5% faster than the display as a limit, and
  the sound queue is kept level by dropping/repeating a sample frame per
  chunk. Not tested on Android yet: the AChoreographer idea above would be
  the alternative if interval 1 misbehaves there.

## For the next release

Player-facing Android changes since the last release, for the main session
to move into CHANGELOG.md and the release notes (see CLAUDE.md, "More than
one session"). Empty after each release.


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

## Philosophy

`wiki/Philosophy.md` is the player-facing statement of what GBA DS is for
and what goes on either screen: (1) a GBA with two 240x160 non-touch
screens, (2) function over form (and nothing on the second screen that
makes the player look twice: no prompts, cursor or controlled character,
nothing split across the screens or on both), (3) keep it simple, (4) only the game's
own graphics, (5) no in-game touch, (6) exceptions allowed when they earn
their place. Read it before changing what a game shows. Keep it written
for players (no rule IDs, file paths or session notes); how sessions apply
it lives here:

- **Check requests against it, and say so.** If a request, or the obvious
  way to do it, conflicts with a principle, say which one and why, and
  suggest something that fits. If the owner still wants it, it's an
  exception: do it, write it as a rule in `docs/games/<game>.md`, and add
  it to the page's list of exceptions. Don't quietly bend a principle, and
  don't quietly refuse because of one.
- **Rule first, then code.** A change to what a game shows starts with its
  rules in `docs/games/<game>.md`, in the player's terms; then the profile,
  then the tests. The owner's notes on their State Saver states are what
  the rules should say. Where the page's principles map to rules: the GBA
  screen is GEN-5, the untouched game GEN-2, nothing half-drawn GEN-3,
  quick to follow GEN-6.
- **Try simple approaches first.** For a game's second screen, in order:
  a screen the game already draws, opened on the hidden copy and captured;
  a part of the live screen moved over (a HUD split, a snapshot); the
  logo. Rebuilding a screen from the game's memory, or drawing anything
  new (GBA DS's font, panels, icons), is out (principles 3 and 4).
- **When unsure, show less.** If a situation can't be detected reliably,
  show the logo or keep the last good picture and list it as a known
  issue. Don't guess.
- **Say what you couldn't do.** Known limitations go in the changelog and
  release notes in plain words. A cut-off status screen that's written
  down is fine; one left for players to find isn't.
- **Keep changes small enough to test.** Every change ends with a test
  build and a short note of what to check on the device; if that note
  would be long, the change is probably too big for one step.

## Conventions

- **Branches:** ROCKNIX and shared work goes to `dev`; Android work to
  `android`; a new game to its own `game/<slug>` branch (see "More than one
  session" below). `main` only moves at
  release time, fast-forwarded to the release commit. The owner creates
  GitHub releases and tags by hand (sessions get HTTP 403 on releases and
  can't push tags).
  Other branches are kept only as `archive/<name>` (work worth keeping that
  isn't in `dev`: `archive/ffta-hud`, the first FFTA second screen;
  `archive/pokedaisy-poc`, the PokeDaisy companion proof of concept); a
  finished topic branch is deleted once merged. Sessions can create branches
  but get HTTP 403 deleting them: the owner deletes them on GitHub (the
  "Block force pushes" ruleset blocks deletion and updates on every branch
  for everyone but the owner and the Claude app).
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
  Cocoon's player entry (`android/cocoon/gba-ds-player.json`, for its
  Daijisho-format Game Boy Advance platform file, inssekt/CocoonFE
  platforms/GameBoyAdvance.json) is no longer a release file: it went to
  Cocoon as pull request inssekt/CocoonFE#561 (from kyannon:gba-ds to
  gamenative, Oct 28, 2026). Keep its launch arguments in step with
  LauncherActivity; if they change, Cocoon needs a new pull request.
  Write release notes (What's new / Fixes / Install / upgrade, for both
  platforms, with the SHA-256 of both files; the APK's is the run's "APK
  SHA-256" annotation, readable from the run page or the public API:
  check-runs/<job id>/annotations) and send them with the zip.
  Then bump `dev` to the next `-dev` version with an empty changelog
  section, but only once the release commit's regression run (the full
  suite, on `dev`) has finished: a push to `dev` cancels the run still
  going there (regress.yml's concurrency), as happened to v1.4.11's and
  v1.4.12's. If it was cancelled, run it on `main` by hand ("Run
  workflow", full).
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

### New games on their own branches

Work on a new game (adding it, or a game the owner hands over for more
compatibility work) happens in its own session on its own branch, merged
into `dev` once it's in good shape, so that `dev` stays releasable.

- **The branch:** `game/<slug>`, the slug lower-case with dashes, the game's
  rules-file name without `.md` (`game/golden-sun-2`). The session creates it
  from the latest `dev` and pushes it (sessions can create branches; only the
  owner can delete them, after the merge). If it already exists, carry on on
  it, merging `dev` in first.
- **Test builds are side builds:** `GBADS_SIDE=<slug>
  GBADS_WEB_WORK=<scratch dir>/wasm scripts/build-rocknix.sh` gives
  `dist/gba-ds-vX.Y.Z-dev-<slug>-linux-aarch64.zip`. It unzips into the Ports
  folder beside the main install: its own folder (`gba-ds-<slug>/`) and its
  own Ports entry, "GBA DS <game> testing" (the game's name from the first
  line of `docs/games/<slug>.md`, a colon made " - " for SD cards' file
  systems), with its own settings, states and logos. Its game list shows
  only the game's releases: the codes in the rules file's "Releases" row
  (`GBADS_SIDE_NAME` / `GBADS_SIDE_CODES="A2NE A2NP"` override either), so
  that row lists every release the profile handles; the build stops if the
  title or the codes are missing. The game's battery saves are the system's,
  shared with the main install. The app's version reads `X.Y.Z-dev+<slug>`,
  and it has no updates (they'd replace it with the release) and no
  EmulationStation / BananaOS integration (the system's GBA games would
  start it). Send that zip after each change, never a plain build from the
  branch.
- **Tests on the branch: only the game's own.** Write its rules
  (`docs/games/<slug>.md`), cases (`tests/regress/<slug>.json`), fixtures and
  goldens as usual, and run `tools/regress.py <slug>`, not the whole suite.
  Fixtures go to `gba-ds-tests`' `main` as usual (only this game's cases use
  them). Don't approve goldens of other games' or shared cases from the
  branch (that broke `dev`'s cases until the merge): if the game's change
  alters a shared picture, give it a new shot name instead. Keep to the game's profile and its own composer: a change to shared
  code (`game.c`, `emu.c`, `mapcap.c`, `logo.c`, other games' profiles) is
  for `dev`. If the game really needs one, keep it small, say so in the
  commit message, and say at the merge that the full suite has to run.
- **Don't edit CHANGELOG.md, README.md or the wiki's game list on the
  branch** (they'd conflict with `dev`). Keep the changelog line and the
  wiki's section for the game ready in the game's rules file under a
  "## For the merge" heading.
- **The merge, when the owner says the game is good:** merge `dev` into the
  branch, run `tools/regress.py <slug>`, then merge the branch into `dev` in
  one step. In that same merge, the changelog line goes under the open
  version, the game into `wiki/Supported-games.md` (table and section) and
  the README's count, and the "For the merge" heading comes out of the rules
  file. Then run the full suite on `dev` (the only full run for the game's
  work), build a normal test zip from `dev` and send it. Tell the owner the
  branch can be deleted.

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
  owner for them), `src/logo_masks.c` (`tools/make_logo_masks.py`) and
  `src/dumps_data.h` with the rules files' Dumps rows (`tools/make_dumps.py
  --dat DAT`: every official dump of each release by CRC32, from No-Intro's
  list in libretro-database (fetched with git, see the tool), the fixtures'
  marked tested; re-run it when a Releases row changes or a game is added,
  including at a game branch's merge; a rules file's hand-written Mods row,
  romhacks known to work, goes into the table too). At start the app logs
  `ROM: CRC32 ...` (the tested dump, another official one, a known mod, or
  modified), and bug reports' info.txt has it as `ROM dump:`
  (`profile_dump`). Romhack support (issue #17) is worked on in the branch
  `patch-support`; a mod's fixture is `roms/CODE+mod.zip`
  (docs/games/README.md, "Releases and dumps"). A mod someone asks for:
  `tools/romhack.py PATCH [--base CODE]` (IPS, UPS, BPS or a patched ROM)
  applies it, says what it changes, runs the release's cases on it
  (regress.py `--any-rom --json`) and writes `build/romhack/<name>/`
  (report.md, the patched ROM, the regress report). Patches (GEN-9): `src/patch.c` (IPS,
  UPS, BPS, bounds-checked; mGBA's own does no BPS). The game's folder is
  `<patch_dir>/<profile_release_name>` (paths.c `patch_folder`, made at
  start); `<state_dir>/<game>.patches` says which is on (one, for now) and
  the "separate battery save / states" switches (`game_paths_patched`:
  "<game> + <patch>"). game.c `patches_setup` sets `emu_patch_plan` (the
  file beside the ROM, then the one on) before `emu_open`, which applies it
  for every core (the game, the hidden copy, logos, twins) from one cached
  patched copy, loaded with `VFileMemChunk` + `core->loadROM`. Extras >
  Patches (`open_patches` / `draw_patches` / `patch_choose`, after Cheats):
  each file tried on the ROM as the list opens; switching asks, then
  `GAME_RELOAD` (main.c starts the ROM again, no auto-load). Patched:
  `ra_game_patched` (no achievements, its banner says so). The game list's
  checksum is of the ROM as it would be played (`emu_rom_read_patched`).
  Cases: `"patch"` (beside the ROM), `"patch_folder"` / `"patch_on"`
  (docs/TESTING.md).

## Testing without the device

- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  GBADS_WINDOW=1024x1536 build/host/gba-ds --state-dir=DIR [--load-state=X.ss]
  --input-script=SCRIPT ROM`. Script lines: `TICK BUTTONS` (A, B+UP, - for
  none), `TICK SHOT:file.bmp` (both screens), `TICK TAP:x,y` (fractions of
  the whole window), `TICK SWIPE:x0,y0,x1,y1` (down, 4 moves, up: the quick
  bar comes up with `SWIPE:0.5,0.99,0.5,0.85`; in a 1024x1536 window its
  buttons are at y 0.923, x 0.117 save, 0.307 load, 0.5 settings, 0.693
  bug, 0.885 close (tapped twice), with or without Controls above
  (profile `controls`), whose tiles are around y 0.836; the pin at
  0.957,0.880 (0.957,0.818 with Controls); a tap elsewhere puts the bar
  away (pinned: opens the menu from outside the bar)), `TICK QUIT`, `TICK LOAD:file.ss` (load a state, the second screen kept). `GBADS_SCREENS=1920x1080,1240x1080` (with `GBADS_WINDOW=1920x2160`) splits the window into two screens of those sizes, stacked, as on devices whose screens differ (AYN Thor; add `--screen_ppi=367,419` for `screen_match = size`). The "Swipe up for quick menu" hint is up for the first 3 s (real time), so pass `--quick_bar_hint=0` for exact pictures (regress.py does). `GBADS_DEBUG=1` logs map captures,
  snapshot and control-scheme switches, taps.
- `build/host/harness ROM OUTDIR SCRIPT` drives the core frame by frame:
  `load/save NAME`, `wait N`, `hold BUTTONS N`, `shot NAME`, `peek ADDR LEN`,
  `poke ADDR LEN VAL`, `io OFFSET N` (I/O registers), `dumpram NAME`, `layershot NAME` (each BG layer and
  OBJ alone), `map NAME` (run a map capture), `logo NAME`. It writes the
  game's battery save beside the ROM (`ROM.sav`): don't run it on the fixtures'
  ROMs in place, or delete those `.sav` files after (never commit them).
- **Players' bug reports** (the quick bar's bug button or Extras > Submit
  a Bug Report, `bug-reports/*.zip`): `GAME-DATE-info.txt` (the player's note, typed on the keyboard that comes up after the button, if any; device, system,
  screens, settings, the second screen's state, the last 48 KB of the log
  without lines mentioning passwords or tokens), `GAME-DATE.state` (that
  moment), `GAME-DATE-rewound.state` (~10 s before) and `GAME-DATE-keys.txt`, the game keys of every frame between,
  readable as `#` lines and runnable as a harness script that plays them
  back from the rewound state exactly (`load ./NAME.state`: harness `load`
  takes a path with a `/` as it is). Unzip, then in that folder
  `harness ROM OUT GAME-DATE-keys.txt` (add `dumpram` / `shot` lines to
  look). Checked: the RAM after the replay equals the `.state`'s, frame
  buffer on or off. Not replayed: cheats (the header says how many were
  on) and the battery save (the harness has none).
- ROMs and save files are not in the repo. The owner attaches them; ask.
  GameShark / Action Replay saves (`.sps`, `.gsv`, as on GameFAQs) convert
  with `tools/sharkport.py IN.sps OUT.sav --magic BYTES` (BYTES: what the
  game's save starts with, so EEPROM saves come out in mGBA's byte order).
  The harness runs without a battery save: start the game in the app with
  `--save_dir`, save a state there (the state carries the save), and go on
  in the harness from it.
- State Saver (`tools/statesaver/`, published as the owner's artifact) has a
  GBA DS tab: it runs a dev zip's `gba-ds/web/gba-ds.wasm` (`scripts/build-web.sh`,
  run by build-rocknix.sh for `-dev` versions; wasm32-wasi, no threads, so map
  captures and logo extraction run inline; asyncify around `web_sleep`) with
  `tools/statesaver/gbads-runtime.js` (in-memory WASI files). Republish it with
  that file beside the page when either changes. `web_*` exports in web/web.c,
  game.c and ui.c (`#ifdef __wasi__`), and web/sdl_audio_web.c (sound, pulled
  by the page: `web_audio_pull`).
- **The web player** (`web/player/`, `scripts/build-web-player.sh`: one .html
  or a folder zip in `build/web-player/`, `--build` to carry a build) runs the
  same web build in a worker with WebGL2, sound, remappable keys and
  controllers, for any known device's screens; map captures run in a second
  worker (a second copy of the build, `mapcap_web_serve`), all inside
  `#ifdef __wasi__`; on-screen controls for phones (`controls.js`), a Home
  Screen app with an offline copy, published to GitHub Pages from `main`
  (`.github/workflows/web-player.yml`). See docs/WEB-PLAYER.md.
- **The owner's states come from State Saver** (artifact
  https://claude.ai/artifact/LGoZRvRDJjagz6ib1h3oxk, the owner's own; started
  empty on Nov 3, 2026, once the earlier one's states were in the suite: that
  one, https://claude.ai/artifact/7uEURvSw3FHtXjPHCzEaT4, is kept as an
  archive). Its
  database collection `states` holds one document per saved moment: name,
  tags, notes (what the owner wants on the second screen: read them as the
  spec), gameCode, romName, frame, `stateAsset` / `shotAsset` (asset ids).
  Newest first: ArtifactData query with `order_by createdAt desc`. Fetch
  each asset with the Artifact tool's read and `path=<asset id>`, one per
  call (a list of paths fails). A state asset is JSON: `stateBase64`
  (gzip when `encoding` is "gzip") is the GBA DS / mGBA state, loadable by
  the app (`--load-state`) and the harness (`load`); `leadIn` (states saved
  since Oct 24, 2026) is the same game about `secondsBefore` earlier. Tags
  often name the rule a state is for (AW-4, SOM-2...). The page itself:
  `tools/statesaver/statesaver.html` (republish with the Artifact tool,
  url above); test it headless with Playwright (`http.server` in
  tools/statesaver; route `**/jszip.min.js` to
  /opt/npm-tools/node_modules/jszip/dist/jszip.min.js, as the CDN may be
  unreachable). It has no `window.claude` there, so it uses browser storage.
- **Finding a game's addresses:** `tools/romxref.py FROM.gba TO.gba ADDR...`
  maps RAM addresses between releases or engine-sharing sequels by matching
  the code that loads them (Advance Wars 2's from Advance Wars'); harness
  `dumpram` before / after an input plus `tools/ramdiff.py` (or a few
  lines of Python over the .ram files) finds the rest (a cursor that moves
  with RIGHT, a counter that runs as a screen opens); `tools/oam.py ROM
  STATE` lists the sprites for SpriteRules; `layershot` shows which BG
  layer holds what. `tools/scout/wwatch.c` (built like the other scout
  tools, docs/PINBALL.md) logs the PCs that write given addresses, frame by
  frame; `llvm-objdump` disassembles the ROM (same page). Per-line register changes (dialogue boxes drawn with
  their own BGxCNT on their lines) don't show in a state's registers: log
  them from `split_draw` in emu.c temporarily.
- **Pokémon Pinball's events** (egg hatch, evolution, every bonus stage):
  `tools/pinball_events.py ROM FIXTURE_STATES_DIR OUTDIR` builds their states
  by steering the game with the harness (docs/PINBALL.md, "Reaching events";
  the pret/pokepinballrs decompilation names its RAM).
- Verify visual changes by rendering screenshots and looking at them before
  sending a build (contact sheets: a few lines of Pillow, then Read the
  PNG).
- **Regression tests:** each game's behaviour is written as numbered rules in
  `docs/games/<game>.md` (FE-2...), checked by the cases in
  `tests/regress/<game>.json` with `tools/regress.py` (see docs/TESTING.md).
  Fixtures (ROMs as `roms/CODE.zip`, states, saves, golden screenshots) are
  in the private repo kyannon/gba-ds-tests, cloned to `../gba-ds-tests`
  (attach it with add_repo). Run the suite before sending a build that
  touches profiles.c, game.c, mapcap.c, emu.c or logo.c. For an
  intended change: rule first, then profile, then `--update CASES` after
  looking at every differing picture in build/regress/index.html; commit the
  goldens in the fixtures repo saying which rule changed. Before a release,
  `--uncovered` lists the rules marked *By hand:* to check on the device
  (and the releases tested only on their logo). `--changed` runs only the
  games a change touches (docs/TESTING.md); GitHub runs that on every push
  to dev / android / game/* (`.github/workflows/regress.yml`; the full
  suite for a "Release vX.Y.Z" commit or "Run workflow" with "full"). `GBADS_SYNC=1`
  makes captures and logo extraction run inline, so runs are exact. By
  default regress.py also runs the app unpaced (`GBADS_NOPACE=1`) and
  drawing only screenshot and touch frames (`GBADS_DRAW_SHOTS_ONLY=1`),
  the CPU count + 1 cases at a time: the full suite takes 10-15 minutes
  on a session's 2 cores (more drawing every frame). `--paced` runs as
  players do. The container's disk can fill up: clear `build/regress` and
  old `dist/` zips. A check that a screen shows nothing there (GEN-1): on
  a live second screen (a HUD over the logo, a card, a podium) it looks
  for 000000, on an idle one (the logo alone, at half strength) 303030.

## Architecture (src/)

- `main.c` start-up; `config.c` settings (`gba-ds.cfg`, `prefs.cfg`, `--key=value`).
  The data folder (issue #34; `paths.c` data_dir_setup): `states/`, `cheats/`,
  `overlays/`, `logos/`, `patches/`, `bug-reports/` live in the app's folder
  unless `data_dir` (Linux: `<app>/data-folder.cfg`, never in the zip, so
  updates keep it; Android: Settings > Folders, `data-folder.cfg` in the
  app's own storage) says otherwise; it's moved there at start, and
  `data_dir_now` in that file records where it is. `asset_dir` and
  `state_dir` default to it. The program, `gba-ds.cfg` and the log stay in
  the app folder on Linux.
- `emu.c` the libmgba wrapper: frames, states, audio, cheats, per-renderer
  sprite hiding (SpriteRule), layer toggles, ROM headers (also inside
  .zip / .7z), and the HUD split (`emu_hud_split`: a second software
  renderer fed the same register / palette / OAM writes draws only some
  layers and sprites into its own buffer; Mario Kart's race HUD; with
  `emu_hud_band`, also the layers whose BGxCNT has given values on a line:
  Advance Wars' conversations). The harness has `split BGMASK PRIO` /
  `split off` and `hudshot NAME`.
- `profiles.c/.h` everything game-specific (MapProfile; Advance Wars 1 and
  2 share their code, addresses in `awRegions`; Final Fantasy Tactics
  Advance's first second screen, with its UI-sprite tagging ROM patch, is
  kept on the branch `archive/ffta-hud`): how to open the
  map, screen and memory probes, crops, captureHide/underHide sprite rules,
  logo extraction, regional variants (`variants[]` + patch functions), and
  for logo-only games (Tony Hawk's Pro Skater 2) the `snap` screen copy and
  `controls` four-face layout (Super Mario Advance 2: `controls.snes`, with the camera pan poke done in game.c's run_frame), (Mario Kart; Super Mario Advance 2 in levels, beside its map capture) the `hud` split and when it's on. Pokémon Pinball has `tall`: the table over both screens as one tall view (game.c update_tall), its top row min(the game's view, rows - 320): the top screen the game as it is while that's its view, else a copy held at rows - 320 with the game's screen-fixed things laid over it (BG0 and the sprites found fixed, drawn by the HUD split: emu_hud_keep_exact); the bottom screen a second copy held 160 rows below the view's top (twins[], run_frame: every frame each copy is taken from the game's state from before the frame before, held at its row (camera stores NOPed, its own pans compensated, twin_jump) and run that frame and this one, on worker threads beside the game: a copy left on its own fell behind the game's logic); sprites alike the game's hidden only when fixed to the screen or wrapped round (twin_hide); paused, the Mart or a message: the game's whole screen on the bottom one (PP-11). See docs/PINBALL.md. Sonic Pinball Party uses the same with `tall.live.carry` (its camera is a row, its table tiles stream through a vblank queue): each copy keeps its own BG0 tile ring, scroll vars and queued ring copies from its last frame (carry_take / carry_put) and walks its camera to its row; the game's own picture is used wherever it has the rows; docs/games/sonic-pinball-party.md has its addresses, tools/scout/sjump.c the experiment. A compilation with its own RAM per game (Game Boy Wars Advance 1+2, `BGWJ`) has `pick`: game.c's ss_switch sets the second screen up again for the variant it names (BGW1, BGW2). Other profile switches: `logoIf` / `logoIfOr` (the logo while they match,
  map and HUD kept for after; Super Mario Advance 4's World-e castle hall;
  the logo stays until a new HUD picture is drawn, at most a second),
  `hud.freeze` (the HUD picture held as it is while they match: SMA4's
  status bar sliding off), `hud.offAfter` (the HUD off from when they match
  until `on[0]` stops: Superstar Saga's results and level-ups, to the
  battle's end). Adding a game mostly means a profile.
- `mapcap.c` map capture on a hidden second core in a background thread.
  A `room` key change starts one once it has held 3 frames (game.c), calling
  off one still running (`mapcap_cancel`: the shadow's frames stop running,
  `emu_cancelled`, composers keep nothing); `aw_run` waits draw only their
  last two frames (`emu_frame_blind`). `GBADS_DEBUG=1` logs each capture's
  time and frame count ("capture: ... ms, ... frames").
- `logo.c` logo extraction. Overlays (game.c `draw_overlay`, Display > Overlays: its own screen,
  `draw_ovl_menu`, with a colour picker and hex keypad): Off, Generated (GBA DS's bezel, drawn by
  `bezel.c` for any screen and picture, in the player's colours `bezel_inner` / `bezel_outer`,
  logo `bezel_logo`; a texture made again when any of it changes) or Custom (the player's PNGs,
  else the bezel in its default colours), on `overlay_screens`. `tools/make_overlays.py` is the
  design's reference (the master images, a set for every device in `docs/devices.csv` to check
  against) and `--embed` makes `src/bezel_logo.c`. regress.py runs with `--overlays=off`.
- `game.c` a play session: input, the second screen (map, logo, THPS2
  snapshot), the quick bar (Controls picker, buttons, bug reports), pause
  menu, cheats screen, screenshots.
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
- `mapper.c` Settings > Map Buttons (Linux): asks for each button, builds an SDL mapping for that controller, kept in `<state_dir>/controllers.txt` and logged (bake device ones into `dist/gamecontrollerdb.txt`). Input scripts drive it with a virtual pad: `JOYBTN:n`, `JOYHAT:v`, `JOYAXIS:n,v`, `JOYBTN:-`.
- `update.c` updates (Linux): GitHub's latest release, the notice and
  Settings > Updates (library.c), download through `http.c` (curl, or
  libcurl), SHA-256 check (`sha256.c`), install over the app's folder via
  `.new` files and rename (gba-ds.cfg kept), then main() execs the new
  binary. Test offline with `tools/update_mock.py` (docs/TESTING.md).
- `library.c` the game list and its Settings screen (and the selected
  game's ROM checksum, read once per file on a worker thread and kept in
  `<state_dir>/roms.cache`: "Romhack: ..." / "Modified ROM" under its logo,
  GEN-8; regress.py's `"kind": "library"` cases); `esinteg.c` the
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
  `/data/screenshots`, `/data/retroarch/cheats`) for settings
  still at ROCKNIX's defaults, and battery saves in the GBA list's own
  `/data/saves/gba/<game>.srm` (save_ext srm, shared with run-retroarch's
  every GBA core: gpSP pads it to 128 KB, mGBA reads the part it needs;
  save_from /data/saves carries GBA DS's older `.sav` over, the newer
  winning, the other kept as `.srm.bak`), plus the RA account from
  `/data/banana/accounts.conf`; ra.c falls back to libcurl (SDL_LoadObject)
  when there's no curl program. `gamecontrollerdb.txt` in the zip maps
  retrogame_joypad (Banana ships no SDL mapping). Cheats: `gba-ds/cheats`
  (assetDir) is searched before cheat_dir everywhere but Android.
  Banana's own GBA list (esinteg.c banana_*; the game list's Settings row on
  Banana): writes /data/banana/systems.conf (a copy of /etc/banana's, which
  banana-ui-session uses instead at the menu's next start), [gba] run line
  -> GBA DS's (below), the original kept as
  `# gba-ds: run = ...`. banana-play.sh runs `gba-ds --supported ROM` (exit
  0 if a profile exists): GBA DS (banana-perf performance), else the
  original line. Off restores it; the copy is deleted if ours and unchanged.
  banana-ui runs the line through a shell (%ROM% quoted), and ours is
  `if [ -f '<dir>/banana-play.sh' ]; then bash ... %ROM%; else ORIG; fi`, so
  the list still starts its games once GBA DS is removed
  (banana_integration_refresh upgrades older lines). banana-ui hands both
  panels to a run line without "run-retroarch" (run-ps1, run-bananafront,
  run-pico8) and keeps one for its own second screen otherwise (main.cpp,
  `bool ra`), deciding from the line before the game starts: so ORIG goes in
  with each launcher's `run-` written `run''-` (the same to the shell), and
  the games GBA DS doesn't support play without Banana's panel (a known
  issue; a per-game hook was asked of Banana's developers).
- **The Stock Linux OS** (Anbernic's own) on the RG DS Plus (Buildroot, weston, two
  windows): launch.sh's "another system" branch. Ports in `/mnt/mmc/Ports`,
  ROMs `/mnt/mmc/Roms/GBA`. Touch: `anbernic_misc/tpctrl` 0 (on) for the run
  (its DS emulator does the same), read straight from the gt9xx device
  (`touch_device`). Sound: SDL's ALSA, whose default (/etc/asound.conf) is PulseAudio; AUDIODEV=pulse too. Two runs were silent with no error, cause unknown (launch.sh logs the sinks 20 s in);
  the volume keys are raw buttons 16/15 and set GBA DS's own volume
  (GBADS_VOLUME file, GBADS_VOLUME_BUTTONS); the menu's volume can't be read.
  No EmulationStation (Settings hides that row when no es_systems.cfg exists).
- EmulationStation: `/usr/bin/runemu.sh` launches games; the GBA command is
  `/usr/bin/runemu.sh %ROM% -P%SYSTEM% --core=%CORE% --emulator=%EMULATOR%
  --controllers="%CONTROLLERSCONFIG%"`.

## Working in these sessions

- **The session's own workspace** (a cloud container) reaches GitHub and the
  package registries only: not the device, not other websites from the
  shell. Test builds go to the owner with SendUserFile; they report back.
- **Build:** `GBADS_WEB_WORK=<scratch dir>/wasm scripts/build-rocknix.sh`
  (the web build's work folder outside the repo), output in `dist/`.
- **Git:** fetch with `git fetch -q origin '+refs/heads/*:refs/remotes/origin/*'`;
  push `git push -q origin dev`, then check `git rev-parse --short dev`
  against `refs/remotes/origin/dev` (the push prints nothing useful).
  Write long commit messages to a file and `git commit -F`. Never force-push.
- **Fixtures repo** (`../gba-ds-tests`, branch `main`): push in small
  commits (a ROM per commit); a big push prints a harmless "push
  negotiation failed"; check with `git ls-remote origin main`. ROM zips are
  named `CODE.zip` holding the ROM under its usual name; another official
  dump of the same release goes in as `CODE+name.zip` (`AX4P+vc.zip`,
  Europe's Rev 1 Virtual Console dump of Super Mario Advance 4, for
  World-e): regress.py checks it against CODE's dumps and make_dumps.py
  marks it tested.
- Shell safety checks block `rm` with a bare variable: use `"${VAR:?}"` or
  a literal path. `pkill -f PATTERN` can kill the shell running it (the
  pattern is in its own command line): keep a PID file instead.
- **The owner's preferences:** player-facing text in plain sentence case;
  changelog lines and release notes written for players (What's new /
  Fixes / Known issues / Install / upgrade, checksums at the end); a test
  zip after every change, with a short note of what changed and what to
  check on the device; known limitations said plainly. When a game's
  behaviour changes, the rule in `docs/games/` changes first.
- **Open by-hand checks** (`tools/regress.py --uncovered`): none open
  beyond the list it prints. Checked on the device for v1.4.2: AW2-4/5,
  LTTP-5, GEN-2 (Super Mario World), GEN-6, PP-6, PP-9 (60 fps), Linux
  updates (BananaOS), Android's update download (its install is still to
  try), Pinball's two-screen view on Android.
- **Won't fix / not issues** (don't list them as outstanding): Advance Wars
  and Game Boy Wars' Field Training keeps the logo (won't fix); Mario
  Kart's course map at 2x is intended; Pokémon Pinball's bonus stages on
  one screen are intended for now. The Stock Linux OS sound is still a
  known issue.

## Ongoing work

- Android version: see `docs/ANDROID.md`.
- Pokémon Pinball's live two-screen view: see `docs/PINBALL.md` (status, facts about the game, the two-copy view of Oct 27 (copies taken from the game every frame), what was learned, what is still to check on the device).
- Experimental since v1.1 (untestable on the owner's RG DS Plus with GammaOS):
  starting games from Android frontends, and the pause menu on returning to
  the app. Needs a tester with a more capable Android device.

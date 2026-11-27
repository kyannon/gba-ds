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

- **Branches: all work is done on a branch of its own and merged into `dev`
  only when it is complete** (the owner's rule, Nov 18, 2026), whether it is
  game-specific or not: a new game or work on one on `game/<slug>` (see "More
  than one session" below); ROCKNIX and shared work, an issue's fix, a docs or
  tooling change, a test build's preparation on a topic branch named for it
  (`issue-61`, `menu-brightness`, `release/v1.5.6-final-dev-build`), cut from
  the latest `dev` and pushed; Android work on `android`. Complete means:
  built and tried, its own cases (and any shared code's) passing, its
  changelog line and docs ready (the main session writes them; a game branch
  keeps them under "For the merge"). Then `dev` is merged into the branch,
  the cases run, and the branch is merged into `dev` in one step. Nothing is
  committed on `dev` itself except those merges: not "just a small fix". The
  two exceptions (the owner's decision, Nov 19, 2026) are the release process's
  own commits, made directly on `dev`: the release commit ("Release vX.Y.Z")
  and the bump to the next `-dev` after it. Never leave `dev` half-done: it has to build and be
  releasable at every commit. A branch is not kept going after its merge (the
  owner deletes it). `main` only moves at
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
- **VERSION** holds `X.Y.Z-dev` (or `X.Y-dev`) on `dev`; a release may also
  be `X.Y.Z.W` (update.c compares up to four numbers from v1.5.2 on; v1.5.1
  and earlier read three, so they'd never be offered a four-number release:
  the v1.5.1 hotfix was named v1.5.2 for that reason). CHANGELOG.md has a
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
  section. (Both are direct commits on `dev`: see Branches.)
  **The full regression suite runs once per release, when the owner asks to
  cut it,** and nowhere else (see "Regression tests" below). Dispatch it on
  `dev` ("Run workflow", full) on the last commit before the release commit:
  after `android` is merged in and the changelog and docs are done. Wait
  for it to finish green before pushing the release commit: a push to `dev`
  cancels the run still going there (regress.yml's concurrency), as
  happened to v1.4.11's and v1.4.12's. The release commit changes only
  VERSION and the changelog's date line (check it with `git show --stat`),
  so it needs no run of its own and CI starts none. If anything else has to
  change after the full run, run the full suite again on the new last
  commit.
- Test builds for the device, sent to the owner after each change: a build
  of `dev` itself (the final dev build, a release's preparation) is the plain
  `dist/gba-ds-vX.Y.Z-dev-linux-aarch64.zip`; **a build of any other branch
  is a side build** (see "Side builds" below): a test build for an issue, a
  topic branch or a game branch never replaces the owner's GBA DS install.
  Android test APKs are the artifacts of the workflow runs on `android` /
  `dev` (link the run).
- **Final dev build** (the last test zip before a release; the owner asks
  for it once the release's games and changes are all merged): a checklist of
  what to confirm on the device, and a zip that carries the test save states.
  1. **Wait for everything to be in `dev`**: every `game/*` branch and topic
     branch the owner means for this release merged, and no session still
     pushing (`git fetch`, then watch `origin/dev` and the recent branches
     for a few quiet minutes; ask the owner when it isn't clear). Check an
     open pull request's follow-up work has landed too. Then `dev` is the
     build.
  2. **The checklist**, in the owner's words "a checklist-based description of
     game rules, major changes and known issues that can or should be checked
     one last time on device", not the release changelog: for each game in the
     release (`CHANGELOG.md`'s New games), a box for each rule's picture per
     release/region, named by the state slot that shows it, with the game's
     known issues listed as "expect" lines; then the interface and shared
     changes, quick checks that older games still work, and what to send
     back. The model: `claude/v1.5.6-checklist.md` in the project. Written
     from `docs/games/<game>.md` and the changelog, never from memory.
  3. **The states**: `tools/device-states.json` lists, per game and release
     (No-Intro name, checked against `src/dumps_data.h`), slots 1 to 9 ->
     fixture state in `gba-ds-tests/states/<game>/` and what to check. Add the
     release's games (and any state the owner asked for during a game's
     branch, from State Saver), a state for every rule and every region the
     fixtures have; say in the checklist where a region has none.
     `tools/make_device_states.py [--games a,b]` makes `build/device-states/`
     (`<name>.slotN.state` and `TEST-STATES.txt`, stopping on a missing
     fixture or a name that isn't a dump); `GBADS_DEVICE_STATES=build/device-states GBADS_LANG_ASK=1
     GBADS_WEB_WORK=<scratch>/wasm scripts/build-rocknix.sh` puts them in the
     zip's `states/` folder (dev versions only); `GBADS_LANG_ASK=1` makes it
     ask the language on a first install, as the release will (GEN-18: the
     owner's rule, Nov 25, 2026, is that only builds a main session makes for
     a release ask; other test builds don't). They replace those slot
     files on the owner's install; say so, and that a state shows only when
     the ROM file is named as the game is.
  4. **Tests**: as for any test build, the cases the changes can affect
     (`tools/regress.py --changed BASE`, BASE the commit of the last build
     the owner tested; when shared code changed it picks every game, so run
     the new games' own cases locally and let GitHub run the rest) and the
     new games' cases; load one baked state in the app headless and look at
     the second screen. The full suite is still the release's.
  5. **Send** the zip, its SHA-256, the checklist (as a file) and a few lines:
     the commit built, what the tests ran, what is not covered (a region
     without states, goldens missing), and the slot files it replaces.

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
- **Test builds are side builds** (the general rule is "Side builds" below):
  `GBADS_SIDE=<slug>
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
- **Tests on the branch: only the work's own.** Write its rules
  (`docs/games/<slug>.md`), cases (`tests/regress/<slug>.json`), fixtures and
  goldens as usual, and run `tools/regress.py <slug>`, not the whole suite.
  Fixtures go to `gba-ds-tests`' `main` as usual (only this game's cases use
  them). Don't approve goldens of other games' or shared cases from the
  branch (that broke `dev`'s cases until the merge): if the game's change
  alters a shared picture, give it a new shot name instead. Keep to the game's profile and its own composer: a change to shared
  code (`game.c`, `second.c`, `hud.c`, `twins.c`, `emu.c`, `mapcap.c`, `logo.c`, other games' profiles) is
  for `dev`. If the game really needs one, keep it small, say so in the
  commit message, and say at the merge which shared-code cases have to run.
- **Don't edit CHANGELOG.md, README.md or the wiki's game list on the
  branch** (they'd conflict with `dev`). Keep the changelog line and the
  wiki's section for the game ready in the game's rules file under a
  "## For the merge" heading.
- **The merge, when the owner says the game is good:** merge `dev` into the
  branch, run `tools/regress.py <slug>`, then merge the branch into `dev` in
  one step. In that same merge, the changelog line goes under the open
  version, the game into `wiki/Supported-games.md` (table and section) and
  the README's count, and the "For the merge" heading comes out of the rules
  file. Then run only the cases the merge needs (the game's own, plus the
  cases of any shared code the branch changed: `tools/regress.py --changed`
  against the merge's first parent), never the full suite (that runs once,
  at release). Build a normal test zip from `dev` and send it. Tell the
  owner the branch can be deleted.

### Side builds: every build of a branch other than `dev`

The owner's rule (Nov 21, 2026): **a test build made to address a specific
issue, or any build based on a branch other than `dev`, is a side build**: its
ZIP installs alongside GBA DS itself, in the same fashion as an individual
game's branch (above), and is never a plain build. Only `dev` (the final dev
build, the release) gives the plain `gba-ds-vX.Y.Z-dev-linux-aarch64.zip` /
release zip, which replaces the main install.

- **Command:** `GBADS_SIDE=<slug> GBADS_WEB_WORK=<scratch dir>/wasm
  scripts/build-rocknix.sh`, output `dist/gba-ds-vX.Y.Z-dev-<slug>-linux-aarch64.zip`.
  `<slug>` is the branch's name in lower-case with slashes made dashes
  (`issue-61`, `menu-brightness`, `release-v1-5-6-final-dev-build`), or
  `game/<slug>`'s own slug. A game branch: as above (the game's name and its
  releases only). **Any other branch** has no rules file, so the build keeps
  every game in the list and names the Ports entry "GBA DS <slug> testing"
  (`GBADS_SIDE_NAME="Menu brightness"` for a nicer name).
- **What that gives the owner:** the folder `gba-ds-<slug>/` and its own Ports
  entry beside the main install, with its own settings, states and logos; the
  system's battery saves are shared with the main install. The version reads
  `X.Y.Z-dev+<slug>`, there are no updates and no EmulationStation / BananaOS
  integration. Say in the note that it is a side build, and that it can be
  deleted (folder and Ports entry) once the issue or branch is merged.
- **It doesn't ask the language on a first install** (GEN-18): only releases
  and builds with `GBADS_LANG_ASK=1` (the final dev build) do, so leave it out.
- **Unzipping it can't touch the main install**, so the test note doesn't need
  "back up your settings" for it. The final dev build, built from `dev`, still
  replaces the install (and its test states replace slot files: say so).
- **Android** test builds are the workflow's APKs and are unchanged by this.

## Building

- Desktop (for development and tests): `scripts/setup-host.sh` once (fetches
  and builds mGBA and SDL2 into `deps/`), then `make` and `make tools`.
- `make module-check`: the second-screen module (`MODULE_SRC`) and the core
  it runs on (`CORE_SRC`, `src/logo.c`) built with no SDL include path, linked
  with no SDL library and run (`tools/module_check.c`), so they stay what a
  libretro core can compile (discussion #75). CI runs it on every push that
  touches `src/` (`.github/workflows/module.yml`, mGBA only, no SDL). A module
  file that includes SDL, calls it, or calls back into the app (`game.c`,
  `ui.c`) fails it. It then runs `tools/module_settings.py`: `docs/LIBRETRO.md`
  lists the `Config` fields and emu.h `extern`s the module reads (a core's
  options, with their defaults), and a read the list lacks, or a row nothing
  reads, fails it.
- `make tools` also builds `build/host/second-driver` (tools/second_driver.c,
  no SDL): the module's reference driver, which replays a trace of the app's
  calls into the module (`GBADS_MODULE_TRACE`) with only the module, for
  regress.py's `"driver": true` cases and `--driver` (docs/TESTING.md);
  `GBADS_DRIVER_SAVE_MEM=1` / `GBADS_DRIVER_SERIALIZE=1` make it use the save and
  states in memory as a core would. `docs/LIBRETRO.md` is the core writer's guide.
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
  write it in normal sentence case. The old pixel font (`ui_text`, capitals
  only) is left for the FPS counter (and the version number where the quick
  bar's pixel font lacks a character); the game list's GitHub link, version
  number and ROM path strip (GEN-21) use the quick bar's Jost-like pixel font
  (`pixfont.h`, library.c `pix_draw`).
- Generated sources: `src/fonts_data.c` (above), `src/brand_data.c` (artwork
  only: the logo, button prompts and flags; `tools/make_brand.py` needs the
  Futura Bold Italic and Gamecuben fonts, which aren't in the repo: ask the
  owner for them), `src/logo_masks.c` (`tools/make_logo_masks.py`), `src/upscale_shaders.c`
  (`tools/embed_shaders.py`: the Scaling filter's upscalers, mGBA's shader
  packs in `third_party/mgba-shaders`, run by `src/upscale.c` on SDL's
  OpenGL ES context; issue #89) and
  `src/dumps_data.h` with the rules files' Dumps rows (`tools/make_dumps.py
  --dat DAT`: every official dump of each release by CRC32, from No-Intro's
  list in libretro-database (fetched with git, see the tool), the fixtures'
  marked tested; re-run it when a Releases row changes or a game is added,
  including at a game branch's merge; a rules file's hand-written Mods row,
  romhacks known to work, goes into the table too). At start the app logs
  `ROM: CRC32 ...` (the tested dump, another official one, a known mod, or
  modified), and bug reports' info.txt has it as `ROM dump:`
  (`profile_dump`). Romhack support (issue #17; branch `patch-support`, merged into
  `dev` Nov 11, 2026; a fallback to the logo when a modified ROM's captures
  keep failing and an estimate for untested mods are left for a later
  release); a mod's fixture is `roms/CODE+mod.zip`
  (docs/games/README.md, "Releases and dumps"). A mod someone asks for:
  `tools/romhack.py PATCH [--base CODE]` (IPS, UPS, BPS or a patched ROM)
  applies it, says what it changes, runs the release's cases on it
  (regress.py `--any-rom --json`) and writes `build/romhack/<name>/`
  (report.md, the patched ROM, the regress report). Patches (GEN-9): `src/patch.c` (IPS,
  UPS, BPS, bounds-checked; mGBA's own does no BPS). The game's folder is
  `<patch_dir>/<profile_release_name>` (paths.c `patch_folder`, made at
  start); `<state_dir>/<game>.patches` says which are on (`on NAME` lines, in the
  order applied, up to `PATCH_ON_MAX`) and the "separate battery save /
  states" switches (`game_paths_patched`: "<game> + <patch> + ...", the
  names sorted). game.c `patches_setup` sets `emu_patch_plan` (the
  file beside the ROM, then the ones on) before `emu_open`, which applies it
  for every core (the game, the hidden copy, logos, twins) from one cached
  patched copy, loaded with `VFileMemChunk` + `core->loadROM`. Extras >
  Patches (`open_patches` / `draw_patches` / `patch_choose`, after Cheats):
  each file tried on the ROM as the list opens (`PatchInfo`: its note and
  the bytes it changes); `patch_rebuild` puts the ones on first, numbered,
  and tries them together (PR_SUMMARY, chain 2 "Fails", chain 1 overlap);
  X / `patch_pick_up` and `patch_shift` reorder; switching asks, then
  `GAME_RELOAD` (main.c starts the ROM again, no auto-load); PR_RESTART
  while the choice isn't `patchStarted`. Some applied, some not:
  `PATCH_PARTIAL` (`emu.patchSkipped`). Patched:
  `ra_game_patched` (no achievements, its banner says so). The game list's
  checksum is of the ROM as it would be played (`emu_rom_read_patched`).
  Cases: `"patch"` (beside the ROM), `"patch_folder"` / `"patch_on"`
  (docs/TESTING.md). Known patches: `docs/patches.md` (any game, by the
  patch file's CRC32 without its last 4 bytes, `tools/patch_id.py`) ->
  make_dumps.py -> `knownPatches[]` + results as mods; the Patches menu
  names them and warns about another version's IPS. A colour patch that
  changes colours a profile probes: profiles.c `colorSwaps[]` (by the
  result's CRC32), applied by `profile_set_rom` (game.c, after emu_open).
  Each tested patch has a `<case>-patch-<label>` case.

## Testing without the device

- The app runs headless: `SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy
  GBADS_WINDOW=1024x1536 build/host/gba-ds --state-dir=DIR [--load-state=X.ss]
  --input-script=SCRIPT ROM`. Script lines: `TICK BUTTONS` (A, B+UP, - for
  none), `TICK SHOT:file.bmp` (both screens), `TICK TAP:x,y` (fractions of
  the whole window), `TICK SWIPE:x0,y0,x1,y1` (down, 4 moves, up: the quick
  bar comes up with `SWIPE:0.5,0.99,0.5,0.85`; in a 1024x1536 window the bar is
  drawn inside the second screen's 960x640 picture (4 px to a GBA pixel;
  `bar_pic` / `bar_u`) and its buttons are at y 0.864, x 0.142 save, 0.320
  load, 0.5 settings, 0.678 bug, 0.857 close (tapped twice), with or without
  Controls above (profile `controls`), whose tiles are around y 0.773; the
  pin at 0.922,0.820 (0.922,0.758 with Controls), its SNES tile at 0.676,0.773; a tap elsewhere puts the bar
  away (pinned: opens the menu from outside the bar)), `TICK QUIT`, `TICK LOAD:file.ss` (load a state, the second screen kept). `GBADS_SCREENS=1920x1080,1240x1080` (with `GBADS_WINDOW=1920x2160`) splits the window into two screens of those sizes, stacked, as on devices whose screens differ (AYN Thor; add `--screen_ppi=367,419` for `screen_match = size`). The "Swipe up for quick menu" hint is up for the first 3 s (real time), so pass `--quick_bar_hint=0` for exact pictures (regress.py does). `GBADS_DEBUG=1` logs map captures,
  snapshot and control-scheme switches, taps.
- `build/host/harness ROM OUTDIR SCRIPT` drives the core frame by frame:
  `load/save NAME`, `wait N`, `hold BUTTONS N`, `shot NAME`, `peek ADDR LEN`,
  `poke ADDR LEN VAL`, `io OFFSET N` (I/O registers), `dumpram NAME`, `layershot NAME` (each BG layer and
  OBJ alone), `map NAME` (run a map capture), `logo NAME`. It writes the
  game's battery save beside the ROM (`ROM.sav`): don't run it on the fixtures'
  ROMs in place, or delete those `.sav` files after (never commit them). `HARNESS_BIOS=FILE` runs it with that GBA BIOS (the fixtures' `bios/gba_bios.bin`, issue #78), `HARNESS_SAVE=PATH` with that battery save instead (written to as the game saves; `tools/pokemon/saves.py state` uses it).
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
  (attach it with add_repo; its `dependencies.json` is described below). **When to run what** (the owner's rule, Nov 17,
  2026): run only the cases the work can affect, and the full suite once
  per release.
  - *Game work* (a new game, or compatibility work on one): the game's own
    cases, including new ones for its new rules, plus the cases of any
    shared code the work changed (`game.c`, `second.c`, `hud.c`, `emu.c`, `mapcap.c`, `logo.c`,
    `ui.c`, shared parts of `profiles.c`...).
  - *GitHub issues and PRs*: only the cases that involve shared code the
    fix changed; a fix that touches none runs none.
  - *Test builds*, the final test zip before a release included: the same,
    only cases for shared code changed or new game rules since the last
    build.
  - *A release*: the full suite, once, when the owner asks to cut it (see
    "Release"). Not for a game's merge, not for a test zip, not for a
    version bump.
  **Which games run a shared file** is measured, not guessed:
  `gba-ds-tests/dependencies.json` (`DEPENDENCIES.md` beside it is the
  readable form: per game, the files and functions it runs) maps each
  function of `src/` to the games whose cases execute it, from a gcov run
  of every game (`tools/regress_deps.py`). `--changed` reads it, so a
  change inside `twins.c`'s `update_tall` picks the pinball games (once the map is measured with twins.c; until then the file is unknown to it and picks every game), not all
  fifty-odd. A change the map can't place (outside any function, a new
  function, a header, a file it doesn't know) picks every game that runs
  the file, or every game: never fewer. `tools/regress_deps.py show
  GAME|FILE` answers "what does this game rely on" / "who runs this
  file"; `check` says how far `src/` has moved on from the measurement.
  The map goes stale when code starts calling something it didn't: after
  a release's full run, or when `check` lists more than a handful of new
  functions, `measure` (a full run on the coverage build, about as long as
  the suite) and `build`, and commit the result to the fixtures repo.
  For an intended change: rule first, then profile, then `--update CASES` after
  looking at every differing picture in build/regress/index.html; commit the
  goldens in the fixtures repo saying which rule changed. Before a release,
  `--uncovered` lists the rules marked *By hand:* to check on the device
  (and the releases tested only on their logo). `--changed` runs only the
  games a change touches (docs/TESTING.md); GitHub runs that on every push
  to dev / android / game/* (`.github/workflows/regress.yml`; the full
  suite only for "Run workflow" with "full", once per release). The
  workflow has time limits (`select` 10 min, a shard 45, the fixtures'
  download two tries of 5 min, the cases 30): a shard that fails on a time
  limit with no failing case is a download, not the code: re-run the failed
  jobs (`gh api -X POST repos/kyannon/gba-ds/actions/runs/RUN_ID/rerun-failed-jobs`),
  and if it fails again, run that shard locally (`python3 tools/regress.py
  GAMES --shard N/K`); docs/TESTING.md, "On GitHub". `GBADS_SYNC=1`
  makes captures and logo extraction run inline, so runs are exact (in the module and
  core it sets emu.c's `emu_sync_work`, which a core sets with `emu_set_sync_work`: the map
  captures, logo extraction, the profiles' own workers and the climb copy's pictures on
  the caller's thread; the app's own threads read `GBADS_SYNC` themselves); the app
  draws with SDL's software renderer (`SDL_RENDER_DRIVER=software`, set by
  regress.py), the same on every machine (through Mesa's OpenGL, blended
  text came out a shade apart from GitHub's runners). A failing case prints
  where its pictures differ and its log's ROM / patch / state lines (all a
  GitHub run keeps). By
  default regress.py also runs the app unpaced (`GBADS_NOPACE=1`) and
  drawing only screenshot and touch frames (`GBADS_DRAW_SHOTS_ONLY=1`),
  the CPU count + 1 cases at a time: the full suite takes about 27 minutes
  on a session's 2 cores (1421 cases, Nov 16, 2026; it grows with the
  cases; more drawing every frame). `--paced` runs as
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
  Widescreen patches (docs/WIDESCREEN.md, rules WS-n; merged Nov 27, 2026, v1.6.5): a
  patch's 32-byte marker `GBADS-WIDESCREEN` in the ROM as played (`emu_wide_find`)
  makes `emu_wide_on` hook the game core's renderer like the HUD split and feed two
  more software renderers looking a margin to each side (text BGs by mGBA's layer
  offsets, sprites by their own moved OAM copy, affine reference points and window
  edges moved); `emu_wide_compose` puts the margins round the frame game.c shows
  (`game_tex_*`; `game_scale` keeps the picture's shape). `GBADS_WIDE=16:9` forces it
  on any game (for writing a patch); the web player's wide view (`pack.py --wide`,
  published at /wide; docs/WEB-PLAYER.md) does it from its bar while the game runs
  (`web_wide` -> game.c `wide_force_apply` -> `emu_wide_set`). `tools/widescreen.py` makes the marker and
  patches; `tools/widetest/build.py` the test ROM (ZWSE) and its patches. GBA DS's
  own patches live in `widescreen/` (never in a release; `index.txt` from
  `tools/widescreen.py index`); `src/widedl.c` fetches that list once a run (raw
  GitHub, branch `GBADS_WIDE_REF`: main, dev for -dev builds, a side build's own;
  `GBADS_WIDE_URL` overrides) and downloads a patch, SHA-256 checked; the Patches
  menu offers the one for each screen's shape (`PR_DOWNLOAD`), and one widescreen
  patch is on at a time (`patch_wide_alone`).
  State files (issue #39): loading (and `emu_state_file_kind` /
  `_rom_crc` / `_get_extra`) takes mGBA's own files and RetroArch's /
  BananaFront's (`RASTATE`, RZIP-compressed (zlib, or zstd since issue #78: the
  vendored decoder `third_party/rzstd`) or not, its mGBA core's bytes in
  the MEM block); `emu_state_save_file_as` writes either, with GBA DS's
  second-screen block (EXTDATA_GBADS) inside the mGBA bytes; `emu_state_save_mem` /
  `_load_mem` / `emu_state_mem_get_extra` are the same in memory (a libretro core's
  serialize), and `emu_save_memory` gives the game its battery save in a buffer
  (`RETRO_MEMORY_SAVE_RAM`; docs/LIBRETRO.md). Save States
  shared with RetroArch (`state_folder = retroarch`, game list Settings >
  Save States, Linux and, since issue #111, Android: docs/ANDROID.md): paths.c `retroarch_state_dir` works out RetroArch's
  folder from its retroarch.cfg as RetroArch does (ROCKNIX: setsettings.sh's
  `/storage/roms/savestates/<system>`), game_paths points slots 0-9 and the
  auto state at `.state`, `.state1`-`.state9`, `.state.auto` there
  (`gp.raDir`), with a `<state>.png` thumbnail and the `.ra` progress in
  `<state_dir>/retroarch/`. The quick state and patched games' own states
  stay GBA DS's. Settings > System has Save States and RetroArch's State
  Folder (library.c ROW_STATES / ROW_STATES_DIR); the folder row opens `folderpick.c`'s `folder_pick` (a
  screen of its own, Linux: folders only, B up, a count of `.state` files,
  Use This Folder / Automatic) and keeps `retroarch_state_dir` in
  library.cfg. Checked: both ways byte-identical 120 frames on against
  mGBA's libretro core, and BananaFront's statefile.c reads and writes the
  same files. Rule GEN-13 (docs/games/README.md); cases `aw-*retroarch*`
  (regress.py's `retroarch` field: a stand-in retroarch.cfg and state
  folder; `file` / `no_file` checks). Every load logs `load state: PATH:
  loaded / not loaded (KIND)`; bug reports' info.txt has a "Save states:"
  line (shared or not, the folder).
- `profiles.c/.h` everything game-specific (MapProfile; Advance Wars 1 and
  2 share their code, addresses in `awRegions`; Final Fantasy Tactics
  Advance's first second screen, with its UI-sprite tagging ROM patch, is
  kept on the branch `archive/ffta-hud`): how to open the
  map, screen and memory probes, crops, captureHide/underHide sprite rules,
  logo extraction, regional variants (`variants[]` + patch functions), and
  for logo-only games (Tony Hawk's Pro Skater 2) the `snap` screen copy and
  `controls` four-face layout (Super Mario Advance 2: `controls.snes`, with the camera pan poke done in game.c's run_frame), (Mario Kart; Super Mario Advance 2 in levels, beside its map capture) the `hud` split and when it's on. Pokémon Pinball has `tall`: the table over both screens as one tall view (twins.c update_tall), its top row min(the game's view, rows - 320): the top screen the game as it is while that's its view, else a copy held at rows - 320 with the game's screen-fixed things laid over it (BG0 and the sprites found fixed, drawn by the HUD split: emu_hud_keep_exact); the bottom screen a second copy held 160 rows below the view's top (twins[] in twins.c, driven from game.c's run_frame by twins_before / twins_second_start / twins_after: every frame each copy is taken from the game's state from before the frame before, held at its row (camera stores NOPed, its own pans compensated, twin_jump) and run that frame and this one, on worker threads beside the game: a copy left on its own fell behind the game's logic); sprites alike the game's hidden only when fixed to the screen or wrapped round (twin_hide); paused, the Mart or a message: the game's whole screen on the bottom one (PP-11). See docs/PINBALL.md. Sonic Pinball Party uses the same with `tall.live.carry` (its camera is a row, its table tiles stream through a vblank queue): each copy keeps its own BG0 tile ring, scroll vars and queued ring copies from its last frame (carry_take / carry_put) and walks its camera to its row; the game's own picture is used wherever it has the rows; docs/games/sonic-pinball-party.md has its addresses, tools/scout/sjump.c the experiment. DK: King of Swing (logo-only) has `climb` (climb.c, second.c `update_climb`, run from `second_step`): in a level the game on the bottom screen and the level above it on the top, drawn by one copy of the game taken from its state every frame with its camera rows lowered 160 and its camera code patched (the profile's `patch`, re-applied with `emu_state_patch32` for IWRAM code), run on its own thread (in step with the game on one core, its pace dropping when the device can't keep up: `climb_pace`); the web player runs it in a worker of its own (docs/WEB-PLAYER.md). `pinball_live = 0` turns it off. A compilation with its own RAM per game (Game Boy Wars Advance 1+2, `BGWJ`) has `pick`: game.c's ss_switch sets the second screen up again for the variant it names (BGW1, BGW2). Other profile switches: `logoIf` / `logoIfOr` (the logo while they match,
  map and HUD kept for after; Super Mario Advance 4's World-e castle hall;
  the logo stays until a new HUD picture is drawn, at most a second),
  `hud.freeze` (the HUD picture held as it is while they match: SMA4's
  status bar sliding off), `hud.offAfter` (the HUD off from when they match
  until `on[0]` stops: Superstar Saga's results and level-ups, to the
  battle's end). A profile whose `composePages` gives pages that turn on a timer (a status screen's three pages, the party's members) sets `pages` (not a marker's blinking frames: they restart with each capture, pages keep their place); it is also what a Toggle row `pages` means (GEN-14), and toggle.c's `second_page()` is the one place that says which page is up. Adding a game mostly means a profile.
- **The second-screen module**, moved out of game.c without SDL so a libretro core can call it as the app does (discussion #75; built on the branch `libretro-core`, merged into `dev` Nov 27, 2026, its full regression run green; every step in the project's `claude/discussion-75-extraction-log.md`; `docs/LIBRETRO.md` is the guide for the core, which Davey-Hughes writes): `second.c` / `second.h` (its state, `SecondState sec`: the capture's pictures, the refresh, the still, the screen copy, the climb view, the toggle's state; and its calls), `hud.c` / `hud.h` (the HUD split and the fights' picture, `HudState hud`), `twins.c` (the Pinball copies and the tall view's composer, `update_tall`, handed the app's state in a `TallEnv`), `toggle.c` (the contextual toggle's state machine and `second_page()`), `workers.c` (the copies' worker threads), `rgba.c` (`rgba_resample`, also ui.c's) and `clamp.h` (SDL's own min / max / clamp macro text, so moved code computes exactly as before: they're type-generic, int stand-ins truncated the HUD's float scaling). What a core calls, in order (tools/second_driver.c is the reference): set `sec.emu / cfg / debug / romPath`, `sec.hooks` (present, presentGba, logoLoad, logoFree) and `hud.hooks` (presentHud, presentCombat, hasCombat, saveBmp); open the game (`emu_open` with its battery save, `profile_set_rom`; `emu_set_sync_work(true)` for exact runs with no threads of the module's or the core's own for that work); `second_game_start()`, `second_open(rom, code, screens)`, a first state if any, `second_loop_start()`; each frame `second_frame_start()`, the game's frame through `second_run_frame(keys)` (the copies run around it), `second_step(screens)`, then `second_shown(&dim)` says which picture the second screen shows; a state load: `second_jump()`, `second_state_loaded(block, n)` (GBA DS's block, put back at the next frame), `second_new_subject()`; a restart: `emu_reset`, `second_jump()`, `second_restarted()`; the logo changed (finished extracting, or switched): `second_logo_changed()`; `second_state_save` with states; `second_idle_picture` / `logo_draw` for the logo; `second_close()` before the game's emulator is closed (a compilation's switch: `second_drop()` then `second_open`). game.c's `ss_setup` / `ss_switch`, `run_frame`, `state_load`, `restart_game`, `logo_refresh`, `draw_game` and the end of a game call the same (game.c writes the module's state itself only to set it up, `sec.emu` / `cfg` / hooks, to count frames, `sec.frame`, the host's, and in `rebuild_textures`, which puts every picture up again after the GPU lost its textures: an app-only event), with the app's own parts around them (controls, button settings, WarioWare's sensor and screens, Android pinning, swallow keys, the SNES layout's camera pan, rewind, rumble). The app's calls can be traced (`GBADS_MODULE_TRACE`: second.c `second_trace_begin`) and replayed by the driver; regress.py's `"driver": true` cases check they give the same pictures (docs/TESTING.md). Keep anything only the module uses `static` in its own file; the headers are the module's interface.
- `mapcap.c` map capture on a hidden second core in a background thread.
  A `room` key change starts one once it has held 3 frames (second.c `map_refresh_step`), calling
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
- `game.c` a play session: input, the second screen's textures and drawing
  (its pictures come from the module above: map, logo, THPS2 snapshot, HUD),
  the quick bar (Controls picker, buttons, bug reports), pause menu, cheats
  screen, screenshots.
  The contextual toggle (GEN-14, issue #37; settings `toggle`, `auto_rotate`,
  `toggle_button`, hotkey action `HK_TOGGLE`, HOME + A; the state machine and `second_page()` are in `toggle.c`, the button in game.c): `toggle_ready()` is the one
  gate, `toggle_press()` / `toggle_step()` the state machine (a turn that waits
  fades the picture: `fadeLevel`), `toggle_new_subject()` starts the pages again
  at the first when a capture was for a new subject (`capSubject`), `second_page()`
  says which page is up. The button: `toggle_btn_screen()` / `toggle_btn_geom()` / `draw_toggle_btn()` (its own 240x176 canvas, so it can sit in the margin under the picture; `cv_put` draws on `cvBuf`), the tap claimed in the pointer loop before "a tap opens the menu". Views (Step 4): a profile with `views` (FF4 first) gives `composePages` pictures the toggle turns to, the first being what shows (no timer); `second_page()` returns `pageOffset` for them. A profile with `onPress` (and `altHere`, whether the other view exists here) has its other view captured on the press: `viewAlt`, `pressCap`, `viewWait`, `fadeUpload` (the old view fades out, the capture is asked for once it's dark, `mapcap_set_alt` before `mapcap_request`, composers read `mapcap_alt_view()`; a refresh still running is called off by the press; `tg_view_mode` says view or pages, the last page of a paged other view bringing the first back; no answer in 600 frames and the old view returns): Minish Cap, Mother 3, Mario & Luigi, Harvest Moon (`hm_farm`: the Farm Map scrolled in stops of 200 x 100 pixels by BG1's scroll, stitched, all sprites hidden, shrunk). In-game settings rows the game can't use (`toggle_supported`, `tallSupported`) are greyed out (`row_disabled`). `tools/regress.py` lints the Toggle rows (`toggle_lint`). Tests: `GBADS_TOGGLE_WAIT=N` (a case's `env`) forces a wait.
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
  --ra-host=http://127.0.0.1:8099` (the mock also has game 777's Rich
  Presence and a leaderboard; `touch /tmp/ra_mock_down` makes unlocks and
  entries fail with HTTP 503). Hardcore (rule GEN-16: `ra_hardcore`
  in prefs.cfg, the Game page's Hardcore item, hidden by `RA_HARDCORE_MENU 0` in ra.h until RetroAchievements approves (the owner's rule, Nov 22, 2026; set 1 then, and rewrite the wiki's Hardcore section and the changelog line); `ra_hardcore()` /
  `ra_start_mode` / `ra_set_hardcore` / `ra_reset_wanted` in ra.c) is built
  but not approved by RetroAchievements, which wants the app public for 6
  months first (from Oct 13, 2026: Apr 13, 2027, unless they take libmgba as
  the parent emulator); the plan and the rules page's checklist are in the
  project's `claude/issue-82-progress.md`. Every state load goes through
  game.c `state_load`, which refuses in hardcore; rewind, fast forward (`canFF`)
  and cheats are gated beside it (`cheats_sync`). The regression suite doesn't cover RA
  (no mock server in regress.py): test with `tools/ra_mock.py` and
  `--ra_hardcore=1` (run it with `--update_check=0` from the game list: a
  host run once updated itself over `build/host`). The compliance document for
  RetroAchievements' team is `docs/RETROACHIEVEMENTS.md`.
  Also in ra.c: the user agent (`GBADS/<numbers> (Linux|Android)
  mgba/<numbers> rcheevos/<ver>`), the events for leaderboards, challenge and
  progress indicators (drawn in the corners of the popup screen by
  `draw_overlays`; leaderboards only run in hardcore, rc_client has no
  softcore switch, so to see them on the host set the hardcore flag for a
  local test build and put it back), and **`states/ra/pending.txt`**: every
  unlock and leaderboard entry rc_client sends is kept (without the token) until the server
  answers, then replayed at the next sign-in before the game loads
  (`pending_track` / `pending_replay`; `replayBusy` holds `begin_load`).
- `drmlease.c` (Linux; issue #86): the second screen on a panel leased from gamescope. ui.c opens it when there's one display and `GBADS_DRM_LEASE` is set (`drmlease_open`: the lease through libwayland-client opened at run time, headers and drm-lease protocol code in `third_party/wayland`, MIT; the panel lit by one atomic commit; two dumb buffers flipped on the leased plane, turned by the panel's orientation; kernel ioctls, no libdrm), makes views[1] a software view (`create_software_view`, retained: drawn only when it changes, `UI_RETAINED`) and `drmlease_present` puts its surface on the panel each frame. The touchscreen's turn comes from the panel's orientation (`touch_rotate`), and `evdev_touch_poll` opens its device with the `openat` system call (Steam's preloaded overlay made `open` fail on it). Test without a panel: docs/TESTING.md, "The leased panel's second screen". Licence: the lease request is a port of Azahar-Enhanced's (GPLv2 or later), noted in the file, `wiki/Licenses.md` and the zip's licenses folder.
- `mapper.c` Settings > Map Buttons (Linux): asks for each button, builds an SDL mapping for that controller, kept in `<state_dir>/controllers.txt` and logged (bake device ones into `dist/gamecontrollerdb.txt`). Input scripts drive it with a virtual pad: `JOYBTN:n`, `JOYHAT:v`, `JOYAXIS:n,v`, `JOYBTN:-`.
- `update.c` updates (Linux): GitHub's latest release, the notice and
  Settings > Updates (library.c), download through `http.c` (curl, or
  libcurl), SHA-256 check (`sha256.c`), install over the app's folder via
  `.new` files and rename (gba-ds.cfg kept), then main() execs the new
  binary. Test offline with `tools/update_mock.py` (docs/TESTING.md).
- `library.c` the first-run language question (GEN-18, issue #92:
  `library_language_ask`, before the game list and from game.c at the first
  pause; `library_language_ask_built` says which builds ask: a version
  without `-dev`, or `GBADS_LANG_ASK` (Makefile `LANG_ASK`, build-rocknix.sh
  and the environment), never `__wasi__`; main.c marks a first install,
  an empty states folder, with `language_asked = 0`; regress.py sets
  `GBADS_LANG_ASK=0` unless a case asks), the game list and its Settings screen (and the selected
  game's ROM checksum, read once per file on a worker thread and kept in
  `<state_dir>/roms.cache`: "Romhack: ..." / "Modified ROM" under its logo,
  GEN-8; regress.py's `"kind": "library"` cases). The scan (issue #49):
  every ROM's game code is kept in `<state_dir>/codes.cache` by path, size
  and date (`archives.cache` before v1.5.3, archives only), patches are
  looked for only for games that are listed, and "Loading games..." shows
  from 0.3 s into a slow scan (not with GBADS_SYNC). Back from a game, the
  list is kept when no ROM folder's time changed (and rom_dir, show_all,
  name_tags are the same); only the game just played is looked at again
  (log: "folders unchanged"). A ROM replaced in place, same name, keeps
  its old entry until the app starts again; `esinteg.c` the
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
- **Armada** (github.com/armada-os/armada, the AYN Thor's own system; issue #86, PR #96 by cerroverb): in **Game Mode** gamescope shows only the top panel and leases the bottom one (DSI-1) to a program that asks through the Wayland protocol `wp_drm_lease_device_v1`; on the desktop both panels are KDE's and GBA DS makes a window on each. `dist/launch.sh` (its "another system" branch) reads `/usr/libexec/armada/device-env` for `ARMADA_SECONDARY_CONNECTOR` / `ARMADA_SECONDARY_TOUCHSCREEN`, passes the touchscreen as `--touch-device` and, when Game Mode's `gamescope-primary` socket exists, sets `GBADS_DRM_LEASE` (the connector) and `GAMESCOPE_WAYLAND_DISPLAY=gamescope-primary`; its first log lines say what it found ("Armada: bottom panel ..."). `src/drmlease.c` does the rest (below). Tried by the contributor on a Thor in Game Mode from a Steam shortcut; the owner's own device is the RG DS Plus.
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
  (the web build's work folder outside the repo), output in `dist/`. Only on
  `dev`: from any other branch, an issue's included, add `GBADS_SIDE=<slug>`
  ("Side builds").
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
  beyond the list it prints; as of v1.5.1 every rule it lists has been
  checked (each rule's note says when). Checked on the device for v1.4.2:
  AW2-4/5, LTTP-5, GEN-2 (Super Mario World), GEN-6, PP-6, PP-9 (60 fps),
  Linux updates (BananaOS), Android's update download (its install is
  still to try), Pinball's two-screen view on Android. For v1.5.1:
  MMBN-6, MPM-6, NFO-7, SM4-7, SMB2-3 on the device, GEN-4 by two full
  runs. A new by-hand rule (or one whose game changed) is open until the
  owner checks it again.
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

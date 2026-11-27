# Regression tests

GBA DS's per-game behaviour is written down as numbered rules in
[`docs/games/`](games/README.md), one file per game. The regression tests
run the real app on saved game states and check both screens against those
rules and against approved screenshots ("goldens"). They are the baseline
for changing a game's profile: a profile change should leave every case
passing, or come with the rule changes and newly approved goldens that say
what changed on purpose.

## What's where

| | |
|---|---|
| `docs/games/*.md` | the rules (e.g. **FE-2. Map windows.**), the releases, the known issues, and the rules not covered yet |
| `tests/regress/*.json` | the cases, one file per game: which ROM and state, the input, when to take screenshots, which rules it covers and the checks |
| `tools/regress.py` | the runner |
| [kyannon/gba-ds-tests](https://github.com/kyannon/gba-ds-tests) (private) | the fixtures: `roms/CODE.zip`, `states/<game>/*.ss`, `saves/*.sav`, `golden/<game>/<case>/<shot>.png` |

ROMs and save states never go in this repository.

## Running

```sh
make && make tools                          # build/host/gba-ds and harness
git clone https://github.com/kyannon/gba-ds-tests ../gba-ds-tests
tools/regress.py                            # every case (once per release; see "When to run what")
tools/regress.py fire-emblem                # one game
tools/regress.py 'fe-us-*' som-us-forest    # case ids or globs
tools/regress.py --list [GAME]              # the cases and the rules they cover
tools/regress.py --changed [BASE]           # only the games a change touches (below)
tools/regress.py --uncovered                # rules no case covers yet, the ones checked by hand,
                                            # and the releases tested only on their logo
tools/regress.py --prune                    # delete goldens no case uses (after removing or renaming shots)
```

The fixtures are looked for in `../gba-ds-tests`, or `$GBADS_TESTS`, or
`--fixtures DIR`. Each run writes `build/regress/index.html`: every case,
failures first, with each check's result, and for a golden that differs the
picture, the golden and a diff side by side.

The exit status is 0 only when every case passes. Run the suite before
sending a build that touches `profiles.c`, `game.c`, `mapcap.c`, `emu.c`,
or `logo.c`.

### When to run what

Run only the cases the work can affect, and the full suite once per
release (CLAUDE.md, "Regression tests" has the rule):

- game work: the game's own cases (and any new ones for its new rules),
  plus the cases of shared code the work changed;
- issues and PRs: only the cases of shared code the fix changed;
- test builds, the final one before a release included: the same, for
  what changed since the last build;
- a release: every case, once, when the owner asks to cut it, on the
  last commit before the release commit.

### Only what changed

`--changed [BASE]` (tools/regress_changed.py) runs only the games a change
touches, the change being BASE (by default where the branch left
`origin/dev`) against the working tree, uncommitted files included. It
prints each changed file and what it picked:

- a game's own files (`docs/games/<game>.md`, `tests/regress/<game>.json`):
  that game;
- `src/profiles.c`: each changed line goes to the definition it's in (a
  comment, to the one below it), then to every game whose profile entry
  uses that definition, directly or through others (`aw_run` reaches a
  dozen games, `fe_compose` the two Fire Emblems). Regional variants count
  for their base profile's games. A line in shared machinery (variants,
  `profile_find`...) runs every game;
- docs, the wiki, the README, the changelog, VERSION, Android-only code,
  State Saver: nothing;
- the app's other sources (`src/*.c`, `*.h`): the games that run the
  functions changed, from the dependency map (below), or every game when
  the map isn't there;
- anything else (the Makefile, build scripts, regress.py): every game.

### Which games run a shared file

`dependencies.json` in the fixtures repo (`DEPENDENCIES.md` beside it is
the same thing for reading: per game, the files and functions it runs)
lists, for every function of `src/`, the games whose cases execute it
(`"*"`: all of them; `[]`: none does). It's measured: the app is built with
gcov (`make OUT=build/cov OPTFLAGS="-O0 -g --coverage" build/cov/gba-ds`)
and `tools/regress_deps.py measure` runs each game's cases on it
(`GCOV_PREFIX` keeps the games' counters apart), then `build` merges the
results into the two files. `--changed` uses it for changes to `src/`:

- a change inside a function: that function's games;
- a change outside any function (a table, a macro), to a function the map
  doesn't know (new, renamed), or to a file it knows no function of that
  runs: every game that runs the file; a header, a file the map doesn't
  know, or no map at all: every game. It only ever widens, never narrows.
- `src/profiles.c` keeps its own, finer tracing (above).

`tools/regress_deps.py show GAME` lists the files a game relies on, `show
FILE` the games that run it, `check` the functions added since the
measurement. The map goes stale when code starts calling something it
didn't (the caller's change picks the caller's games; the callee's map
entry doesn't know them yet): measure again after a release's full run, or
when `check` lists more than a handful of new functions, and commit the new
files to the fixtures repo. The cases that fail on the coverage build are
the ones failing normally (a golden behind the code): the code they ran is
still recorded.

A change to the fixtures alone isn't seen: name the game, or run them all.

### On GitHub

`.github/workflows/regress.yml` runs the suite on every push to `dev`,
`android` and `game/*`: the games `--changed` picks since the push's
previous commit, split over up to six runners (`--shard I/N`), each
fetching only the fixtures its cases use (`--fixture-paths`, a sparse
checkout). Only "Run workflow" with "full"
ticked runs every game: once per release, on the commit before the release
commit (a "Release vX.Y.Z" commit changes only VERSION and the changelog's
date, so it runs nothing). The fixtures come in with the repository secret
`GBA_DS_TESTS_TOKEN` (a fine-grained token, Contents: read-only on
kyannon/gba-ds-tests). The run's summary lists what was picked and why,
each share's failures, and the releases tested only on their logo; a
failing share keeps its report as an artifact for 7 days.

The workflow has time limits, so a hung download ends on its own (Nov 25,
2026, a share sat 47 minutes in its fixtures' fetch): picking the games
10 minutes (the dependency map's fetch 2, after which shared code runs
every game, as when it can't be fetched), a share 45, its fixtures'
download two tries of 5 minutes each (a "Fixtures fetch, attempt N"
warning per try, then "Fixtures not reachable"; 15 for the step), its
cases 30. A share that fails on a time limit with no failing case is a
download, not the code: re-run the failed jobs (`gh api -X POST
repos/kyannon/gba-ds/actions/runs/RUN_ID/rerun-failed-jobs`), and if it
fails again, run that share locally (`python3 tools/regress.py GAMES
--shard N/K`).

## The leased panel's second screen (Armada, issue #86)

`GBADS_DRM_LEASE=DSI-1 GBADS_DRM_LEASE_TEST=1240x1080,270,/tmp/panel.png` runs the app with a panel of that size and orientation in memory instead of a compositor's lease (nothing is leased), and writes what the panel would show to the PNG at exit: the second screen drawn by the software renderer, turned by the rotation, as the panel scans it out. Use it with `GBADS_WINDOW=1920x1080` and `--input-script`; the regression suite doesn't set it, so its cases run the ordinary path.

## How a case runs

The app runs headless (`SDL_VIDEODRIVER=offscreen`) in a 1024x1536 window
(both RG DS Plus screens stacked: the game screen on top), drawn by SDL's
own software renderer (`SDL_RENDER_DRIVER=software`), with
`GBADS_SYNC=1`. The software renderer does the same arithmetic on any
machine: through OpenGL (Mesa's llvmpipe, where a machine has it)
blended pictures (text and its shadow, a dimmed map, a menu's panels)
came out a shade apart from one CPU or Mesa to another, so goldens
approved on a session's container failed on GitHub's runners, which
have no OpenGL (71 cases, Nov 11, 2026). That switch makes map captures and logo extraction run
inline at the frame they're asked for instead of on background threads, so
every run of a case gives exactly the same pictures. The app also runs
as fast as it can (`GBADS_NOPACE=1`) and draws only the frames that need
it (`GBADS_DRAW_SHOTS_ONLY=1`: a screenshot's, and those within 30 frames
of a tap, swipe, state load or joystick line): the game, map and HUD
pictures are still made every frame, only the screens aren't put
together, so the pictures are the same as when it draws every frame, in
under half the time (most of a frame's time went into drawing both
screens). `--paced` runs it as players do
instead, at 60 frames a second and drawing every frame: for a case that
might depend on something timed in real seconds (the quick bar closes
itself after 5 s). Each case gets a
fresh state folder, so it also tests the first-run path (logo extraction).
It runs the app with `--overlays=off`, so goldens are the games' pictures
without GBA DS's bezel (a case can turn it on in its `config`).

A **logo** case (`"kind": "logo"`) extracts one release's logo the way the
app does, and compares it with its golden.

A **library** case (`"kind": "library"`) starts the app on the game list
instead of the game, with the case's ROM the only one in it (`--rom_dir`),
and takes its shots and input as an app case does (GEN-8: what the panel
says about the ROM). `"rom_files": {"NAME": "FIXTURE"}` puts other ROMs
from the fixtures' `roms/` in its folder too, each named NAME (the list
sorts by name), for a list with two versions of a game (GEN-20). The game list shows the app's version at the top
screen's bottom right, so these cases leave it out of the comparison:
`"golden_mask": [[700, 720, 1024, 768]]` (rects painted over in both
pictures before they're compared; any case can use it). A fifth item, a shot's name
(or a list of names), keeps a rect to those shots: `[40, 1440, 800, 1474, "system-language"]`
leaves out the Updates row's note ("This is v<version>...") in that shot only, so a version
change doesn't touch the case's other pictures (`aw-retroarch-settings`).

A case with `"state_files": {"NAME": "FIXTURE"}` puts the fixture `states/FIXTURE` in the app's state
folder as NAME (`"text:..."` as the value writes that text instead): what an earlier play left
there, as the automatic state `CODE.auto.state` and its note `CODE.auto.state.colours` (GB-6's
resume logic). The app saves over a copy on leaving, never the fixture.

A case with `"patch": "FILE"` puts the fixture `patches/FILE` (an IPS, UPS,
BPS or xdelta) beside its ROM, named as the ROM with the patch's extension, so the app
applies it as the game starts (GEN-9). `"patch_folder": [FILE, ...]` puts
fixtures in the game's own patches folder (`<work>/patches/<release name>`,
the release's No-Intro name as `profile_release_name` gives it), and
`"widescreen_list": true` serves the repository's `widescreen/` folder (the widescreen patches' list and files, WS-6) to the case from a local server, as `GBADS_WIDE_URL`; every other case gets an address nothing answers, so none reaches GitHub. `"patch_on": "FILE"` switches one on there (writing `<rom>.patches`; a list switches on several, in the order they're applied; `"patch_separate": false` turns the separate battery save and states switches off). Every
app case runs with its own `--patch_dir`.

A case with `"driver": true` (or every case, with `--driver`) also checks the second-screen
module alone against the app (discussion #75): the app runs with `GBADS_MODULE_TRACE`, which
writes every call it makes into the module (second.c's `second_trace_begin`: open, each frame's
`second_frame_start` / `second_run_frame` / `second_step`, state loads with the file loaded,
`second_new_subject`, `toggle_press`, close) and, at each screenshot, what the second screen
shows (`NAME.second`: `second_shown`'s kind, dim, and the picture) and the game's picture
(`NAME.game`); then `build/host/second-driver` (tools/second_driver.c, `make tools`) replays
that trace with only the module and the emulator core, no SDL and none of the app, and writes
the same. The check ("MODULE") passes when its trace and every picture are the app's, byte for
byte: a difference means the app changes the module's state outside its calls, which a libretro
core couldn't. Left out by design (the host's choices, not the module's): ROM patches (the
driver opens the plain ROM), the player's own or a scraped logo (the driver's is the extracted
one), Game Boy palettes and the restart prompt (it starts the game again in the same run).
Two settings in a case's `env` (or the shell's) make the driver work as a libretro core
would: `GBADS_DRIVER_SAVE_MEM=1` gives the game its battery save in memory
(`emu_save_memory`, a core's `RETRO_MEMORY_SAVE_RAM`) instead of a file, and
`GBADS_DRIVER_SERIALIZE=1` saves the game's state and loads it back in memory after every
frame (`emu_state_save_mem` / `emu_state_load_mem`, `retro_serialize` /
`retro_unserialize`); the app ignores both, so the pictures must still be the app's.
`to-us-resume` (a battery save, no state) runs with both.

## Writing a case

```json
{
 "id": "fe-us-hud",
 "rom": "AE7E",
 "state": "fire-emblem/us-hud.ss",
 "save": "AAME-100-percent.sav",
 "desc": "the unit, goal and terrain windows over the map",
 "rules": ["FE-1", "FE-2", "FE-3"],
 "input": [[150, "START"], [154, "-"]],
 "config": {"layout": "single"},
 "env": {"GBADS_TOGGLE_WAIT": "30"},
 "shots": {"start": 60, "page2": 420, "end": 720},
 "checks": [
  {"rule": "FE-2", "what": "the minimap at the bottom right",
   "type": "not_uniform", "shot": "start", "screen": "second",
   "rect": [800, 560, 990, 740], "color": "182438"}
 ]
}
```

- `env` (optional) sets environment variables for the run: `GBADS_TOGGLE_WAIT=N` makes a page turn by hand wait N frames behind the fade (GEN-14), the way a view that has to be captured will.
- `rom` is the release's game code (`roms/<rom>.zip`); `state` is relative
  to the fixtures' `states/`; `save` (optional) is a battery save from
  `saves/`, put beside the ROM.
- `input` is `[frame, buttons]` pairs as in the app's input scripts (`A`,
  `B+UP`, `-` to release); `config` passes `--key=value` settings.
  `"LOAD:<game>/<state>.ss"` loads another state at that frame, as a quick
  load would, with the second screen kept: the way to test a rule about
  keeping an earlier picture (show it from one state, then load the moment
  where it must stay).
- `bios` (GEN-15): a fixture under `bios/` (the official GBA BIOS, `gba_bios.bin`, from the owner) put as `<asset_dir>/bios/gba_bios.bin`; the case's `config` `gba_bios: 1` switches it on. Its states are made with the harness (`HARNESS_BIOS=FILE harness ROM OUT SCRIPT`, which runs with that BIOS; Sonic Advance 2 waits inside the BIOS every frame, Advance Wars never does) and wrapped with `tools/retroarch_state.py wrap`: `sonic-advance-2/us-retroarch-bios-real.state` (real BIOS, inside it), `us-retroarch-hle-in-bios.state` (built-in BIOS, inside it).
- `cheat_files` (GEN-17): `{name there: fixture under cheats/}`, copied into `<asset_dir>/cheats/`. A `.cht` file is matched to its ROM by name, and a case's ROM is named by its code, so name it `BFZE (USA) (Code Breaker).cht` (the code, then the tags).
- `retroarch` (GEN-13) runs the case with save states shared with a
  stand-in RetroArch (`--state_folder=retroarch`): `cfg` is its
  retroarch.cfg's text, where `{states}` is its state folder in the case's
  own folder (default: `savestate_directory = "{states}"`, so the states go
  in its `mGBA/` folder); `states` maps names in that folder
  (`"mGBA/AWRE.state3"`) to fixtures under `states/`, given times a minute
  apart from noon UTC on Jan 1, 2020 in the order listed (the last the
  newest, which the pause menu opens on; slot labels read "Jan 1, 2020"
  anywhere). The RetroArch-format fixtures (`advance-wars/us-retroarch-*.state`)
  were packed with BananaFront's own `statefile.c`, one around mGBA's
  libretro core's state, one around bytes that aren't mGBA's (another
  core's).
- `shots` names the screenshots and their frames. Every shot is compared
  with its golden unless the case lists `golden` (the shots to compare).
  `golden_tol` (per channel) and `golden_frac` (share of pixels) allow
  slack; the default is an exact match.
- `rules` lists the rules the case's goldens stand for; each check names
  the rule it checks. Coordinates are in a screen's own 1024x768 pixels;
  `screen` is `game` or `second` (the default).

Check types:

| type | passes when |
|---|---|
| `pixels` / `not_pixels` | at least `min` (default all) / none of `probes` (`[x, y, "RRGGBB"]`) match, within `tol` (default 12) |
| `uniform` / `not_uniform` | at least / under `frac` (default 0.99) of `rect` (`[x0, y0, x1, y1]`) is `color` |
| `same` / `differs` | rect `a` of one shot and rect `b` of another differ in at most / more than `frac` (default 1%) of their pixels |
| `log` / `no_log` | the app's log (`GBADS_DEBUG=1`) matches / doesn't match the regex `match` (e.g. `map updated`) |
| `file` / `no_file` | `path` in a `retroarch` case's state folder is there and starts with `starts` (its bytes as Latin-1: `"#RZIPv\u0001#"`, `"RASTATE\u0001"`, `"\u0089PNG"`) / isn't there |
| `image_size` | the picture at `path` (a BMP or PNG, as `file`'s: `state/CODE.game.bmp` is the resume picture a case leaves behind) is `size` `[w, h]` |

Rule checks say *why* a picture is right and keep passing through
harmless changes elsewhere; goldens catch everything else. A case can
have both, either, or only goldens.

## Approving changes

When a change is meant to alter what a game shows:

1. Change the rule in `docs/games/<game>.md` (retire a rule rather than
   renumbering: write *retired* and why).
2. Change the profile.
3. Run the game's cases, open `build/regress/index.html`, and look at every
   differing picture.
4. Approve the new pictures: `tools/regress.py --update <cases>` (it needs
   case ids, a game or a glob; there is no update-everything). Commit the
   goldens in the fixtures repository and the rules/cases here, saying in
   both commit messages which rule changed.

A golden that changes without a rule changing is a regression until shown
otherwise.

## Rumble (issue #83)

`tools/rumble_test.py` builds a tiny test ROM that switches the cartridge's rumble pin on and
off, runs it headless at `rumble_strength` 100, 50 and 0, with Drill Dozer's game code (in
mGBA's database) and with Pokemon Pinball's (not in it), the pins written with `strh` and with
`strb` (the Pinball rumble patch's way), and checks the log's
"rumble: the game asked for rumble (..., motor X)" line: the motor is asked for the game's level
times the strength. It needs llvm-mc and llvm-objcopy and no fixtures. The motor itself (a
controller's, or the device's through SDL's haptics) can only be tried on a device: the log
says what was found at start ("rumble: haptic 0 (...): usable") and the first time a game
asks for rumble ("rumble: the game asked for rumble (..., motor X)").

Cartridge rumble for games mGBA doesn't know (emu.c `gpio_listen`, Nov 23, 2026): mGBA only
listens to the motor pin for the game codes in its database and drops 8-bit stores to the
cartridge, and the Pokemon Pinball rumble patch (`AGB-BPPE-USA_CartRumble_v1.1.ips`) keeps BPPE
and writes the pins with `strb`. The live game's stores to the three GPIO registers switch the
pin on at the first one (not for a cartridge with a sensor on that port: Boktai's light
sensor, a gyro or tilt) and pass a byte on as the 16-bit write a cartridge bus makes of it. The patch sets the
pins up once at boot and only uses them when the game's Game Boy Player flag
(`gGameBoyPlayerEnabled`, 0x02019C0C) is off; the game only rumbles with its own Rumble option
on (`gMain.saveData.rumbleEnabled`, 0x0200B201, in the battery save). Checked with the real
patched ROM (CRC32 4eff7204) from a cold boot with that option forced on (a Code Breaker cheat,
`3200B201 0001`): the log's rumble line at frame 1205; resuming that run's auto state rumbles
too; a state saved before the patch was on doesn't (no pin setup) until Restart Game.

Superstar Saga's rumble patch (`MARIOLUIGIU/P/J_CartRumble.ips`, Nov 24, 2026: "started rumbling during
the story intro but never stopped"). Its helpers (USA at 0x1E19A0) switch the motor on with control 1,
direction 8, data 8, and off with one write: direction 0, the data register left at 8, as a cartridge
whose motor has nothing driving it when the pin is an input. mGBA keeps the data bit as the output
level, so the motor stayed on. emu.c `gpio_settle` turns the motor off while pin 3 is not an output
(after every store the hook sees; the data bit comes back on with the direction). The game's rumble
state is the byte at 0x03000D28 (1 timed, 2 continuous), and it only starts the cartridge when
0x03000BD7 bit 0 is set (the game's own rumble setting, not found by the A-button run from a new game:
forced by `poke 0x3000bd7 1 1` each poll). Checked with the harness (`GBADS_HARNESS_RUMBLE=1`, `rumble`
prints the motor level) on the patched USA ROM (CRC32 e554f7e3), A pressed through the intro: in the
Cackletta scene the state is 2 and the level 1.00; back to state 0 it is 0.00 with the fix and stayed
1.00 without. `tools/rumble_test.py` has the stop-by-direction cases (its test ROM keeps its code clear
of 0xC0-0xCA: a branch at 0xC0 makes mGBA take it for a multiboot image, and the GPIO registers read
back pins).

Game Boy Player rumble (Mario & Luigi, Pokemon Pinball R&S, SMA4) is not supported. Correction
of Nov 22's note: with mGBA's `gba.forceGbp` on the live game, the *unpatched* Pokemon Pinball
does rumble over the link port once its own Rumble option is on (cold boot, the option forced
on, rumble at frame 1205; traced Nov 23, 2026): the earlier 2500-frame trace never reached a
table with the option on. That is how v1.6.1's side build rumbled with the patch on (the
patch leaves the pins alone while the game's Game Boy Player flag is set), and removing
forceGbp for the release is why the patch stopped. Not brought back: the Game Boy Player is
only found at the boot logo, so a resumed state never has it (and a state saved with it holds
the game's flag set, which also turns the patch's cartridge route off), and forcing it on a
copy that extracts logos changed the logos. A future feature.

## Rules checked by hand

A rule the tests can't reach (a link cable, behaviour that never shows on
the screens) is marked in its game's file with *By hand:* and what to
check. `tools/regress.py --uncovered` lists them under "Checked by hand";
go through that list on the device before a release.

## Adding fixtures

New states come from the device or State Saver (named for what they show,
per game: `states/<game>/<region>-<what>.ss`). In State Saver, label a state
with the rule it's for (`AW-4: ...`) and say in the notes what it shows; a
session turns it into a case, checks the pictures, and moves the rule off the
game's *Not covered yet* list. A state needs the matching
ROM in `roms/`: a clean dump, one that's in No-Intro's list (check its CRC32
against the game's Dumps row; a ROM that isn't there, such as one with a
colour or QoL patch, makes its cases fail). A new release's ROM goes in, then
`tools/make_dumps.py --dat DAT` marks it tested (docs/games/README.md,
"Releases and dumps"). Use the app's own save states or harness `save` states;
note that a state loads into a fresh run, so a rule about keeping
something from before (a last picture) needs a case that shows it first:
input that plays there, or a `LOAD:` of the state from an earlier one. For a release whose saves the game won't read (most European releases
won't read USA saves), make a state by playing in from a new game.

## Updates (Linux)

The updater (`src/update.c`) is tested against `tools/update_mock.py`, a
stand-in for GitHub's latest-release API serving a zip you give it:

1. Make a fake release zip laid out like the real one (`gba-ds/gba-ds`,
   `gba-ds/launch.sh`, `gba-ds/gba-ds.cfg`, `GBA DS.sh`, `images/`). A
   `gba-ds/gba-ds` that's a shell script writing a file shows the restart
   worked.
2. Copy `build/host/gba-ds` into an install folder like the device's
   (`ports/gba-ds/gba-ds`, with an old `launch.sh`, a `gba-ds.cfg` of your
   own and `ports/GBA DS.sh`).
3. `python3 tools/update_mock.py --zip release.zip [--slow] [--bad-digest]
   [--status 404]`, then run that copy headless with
   `--update-url=http://127.0.0.1:8098/repos/kyannon/gba-ds/releases/latest`
   (unset `https_proxy`). With no `states/update.cfg` the automatic check
   runs at once and the notice appears; `auto = 0` in it tests the Settings
   path (SELECT, DOWN to Updates, A, A to check).
4. Check: the new files are in place, `gba-ds.cfg` is yours, `GBA DS.sh` is
   the new one (an `images/` folder that wasn't there isn't created), the
   restart ran with the same arguments, and `states/update/` is empty.
   `--apk APP.apk` instead of `--zip` serves an Android release (the APK
   asset); docs/ANDROID.md, "Updates", says how the Android side was tested.
   `PATH=/nonexistent` tests the libcurl fallback (BananaOS); running a copy
   from elsewhere with `GBADS_HOME` set tests launch.sh's copied binary
   (both copies are replaced).

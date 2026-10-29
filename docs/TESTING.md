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
tools/regress.py                            # every case (about 5 minutes with 8 cores)
tools/regress.py fire-emblem                # one game
tools/regress.py 'fe-us-*' som-us-forest    # case ids or globs
tools/regress.py --list [GAME]              # the cases and the rules they cover
tools/regress.py --uncovered                # rules no case covers yet, and the ones checked by hand
tools/regress.py --prune                    # delete goldens no case uses (after removing or renaming shots)
```

The fixtures are looked for in `../gba-ds-tests`, or `$GBADS_TESTS`, or
`--fixtures DIR`. Each run writes `build/regress/index.html`: every case,
failures first, with each check's result, and for a golden that differs the
picture, the golden and a diff side by side.

The exit status is 0 only when every case passes. Run the suite before
sending a build that touches `profiles.c`, `game.c`, `mapcap.c`, `emu.c`,
`logo.c` or `mapframe.c`.

## How a case runs

The app runs headless (`SDL_VIDEODRIVER=offscreen`) in a 1024x1536 window
(both RG DS Plus screens stacked: the game screen on top) with
`GBADS_SYNC=1`. That switch makes map captures and logo extraction run
inline at the frame they're asked for instead of on background threads, so
every run of a case gives exactly the same pictures. Each case gets a
fresh state folder, so it also tests the first-run path (logo and surround
extraction).

A **logo** case (`"kind": "logo"`) extracts one release's logo the way the
app does, and compares it with its golden.

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
 "shots": {"start": 60, "page2": 420, "end": 720},
 "checks": [
  {"rule": "FE-2", "what": "the minimap at the bottom right",
   "type": "not_uniform", "shot": "start", "screen": "second",
   "rect": [800, 560, 990, 740], "color": "182438"}
 ]
}
```

- `rom` is the release's game code (`roms/<rom>.zip`); `state` is relative
  to the fixtures' `states/`; `save` (optional) is a battery save from
  `saves/`, put beside the ROM.
- `input` is `[frame, buttons]` pairs as in the app's input scripts (`A`,
  `B+UP`, `-` to release); `config` passes `--key=value` settings.
  `"LOAD:<game>/<state>.ss"` loads another state at that frame, as a quick
  load would, with the second screen kept: the way to test a rule about
  keeping an earlier picture (show it from one state, then load the moment
  where it must stay).
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
ROM in `roms/`. Use the app's own save states or harness `save` states;
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

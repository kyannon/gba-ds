# Tony Hawk's Pro Skater 3

| | |
|---|---|
| Releases | `AT3E` USA and Europe · `AT3F` France · `AT3D` Germany (each with its own View Goals heading) |
| Profile | `src/profiles.c`: the `AT3E` entry; variants `AT3F`, `AT3D` |
| Tests | `tests/regress/tony-hawks-pro-skater-3.json` (`tools/regress.py tony-hawks-pro-skater-3`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list tony-hawks-pro-skater-3`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**TH3-1. Goal list during a run.** During a run the second screen shows the level's goal list (the pause menu's View Goals) on black, brought up on the hidden copy and kept up to date.

**TH3-2. Logo at the menus.** Back at the menus the second screen shows the logo.

**TH3-3. Controls picker.** The Controls button offers GBA, Four buttons or Auto (four buttons while skating).

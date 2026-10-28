# Tony Hawk's Pro Skater 2

| | |
|---|---|
| Releases | `ATHE` USA and Europe · `ATHF` France · `ATHD` Germany · `ATHJ` Japan (SK8: its own badge and a later title) |
| Profile | `src/profiles.c`: the `ATHE` entry (with `ATHF`, `ATHD`); variant `ATHJ` |
| Tests | `tests/regress/tony-hawks-pro-skater-2.json` (`tools/regress.py tony-hawks-pro-skater-2`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list tony-hawks-pro-skater-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-5).

## Rules

**TH2-1. Goal sheet during a run.** During a run the second screen shows that run's goal sheet (Free Skate, Single Session, the career checklist), copied from the game screen when it's shown, without the sprites the game draws over it.

**TH2-2. Sheet stays.** The sheet stays up through the run, pauses and loading.

**TH2-3. Logo at the menus.** Back at the menus the second screen shows the logo.

**TH2-4. Controls picker.** The second screen's Controls button offers the four-button layout; GBA is the default.

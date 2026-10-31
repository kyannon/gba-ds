# Fire Emblem: The Sacred Stones (Seima no Kouseki in Japan)

| | |
|---|---|
| Releases | `BE8E` USA · `BE8P` Europe (En/Fr/De/Es/It) · `BE8J` Japan. Addresses per release in `feRegions` (shared with Fire Emblem). |
| Profile | `src/profiles.c`: the `BE8E` entry (Fire Emblem's `fe_virt` / `fe_compose`); variants `BE8P`, `BE8J` |
| Tests | `tests/regress/fire-emblem-sacred-stones.json` (`tools/regress.py fire-emblem-sacred-stones`) |
| Status | supported (tested on the device for v1.4.4) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list fire-emblem-sacred-stones`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

The game runs on Fire Emblem's engine, and GBA DS treats it the same way (its rules: [fire-emblem.md](fire-emblem.md), FE-1 to FE-10); each rule here matches one there.

## Rules

**FE8-1. Logo.** Whenever the second screen has no unit's status to show (FE8-2), it shows the logo (the title's: the dragons, the lettering and its ribbon, without Press START or the copyright line): on the title and menus, in story scenes, in fights and on the other armies' turns, and on the map with the cursor on no unit or with a unit selected. (FE-1.)

**FE8-2. Unit status.** With the cursor free on a unit, yours or another army's, the second screen is that unit's status screen (R), as the game draws it, filling the screen, turning through its three pages every 5 seconds; taken again only when the cursor or a unit changes. Once a unit is selected (moving it, its menu, the forecast) the logo comes back until the cursor is free again. (FE-3.)

**FE8-3. Story scenes.** Story scenes before and after the battle stay on the game screen, and the second screen shows the logo. (FE-7.)

**FE8-4. Fights and other turns.** In a fight and on the other armies' turns, the second screen shows the logo. (FE-9.)

**FE8-5. Game screen untouched.** Nothing leaves the game screen: the unit, terrain and goal windows, menus, the battle forecast, conversations, the tutorial's boxes and the phase banners stay where the game draws them. (FE-10.)

## Not covered yet

Tried from the owner's four USA states (the title, a unit's status open, the map, a unit moving) and new games played to the prologue's first fight in each release. Later chapters, the world map between chapters, the Tower of Valni and the creature campaign aren't tried yet: the world map in particular is new in this game, and shows the logo (no unit status there).

## Facts found so far (for sessions)

| What | USA / Europe | Japan |
|---|---|---|
| Chapter data (0 off the map) | `0x0202BCF0` | `0x0202BCEC` |
| Process slots | `0x02024E68` | same |
| Map cursor | `0x0202BCC4` | `0x0202BCC0` |
| Units (0x48 bytes each) | `0x0202BE4C` | `0x0202BE48` |
| Player phase script | `0x0859AAD8` / Europe `0x087ABDB8` | `0x085C2F58` |
| Event engine | `0x08591AC0` / `0x087A2ADC` | `0x085B98A0` |
| Battle scene | `0x085B9358` / `0x087DBBCC` | `0x085E3538` |
| Phase banner | `0x0859AF80` / `0x087AC29C` | `0x085C3400` |
| Status screen | `0x08A009D8` / `0x08A8EE00` | `0x08A72A90` |
| Player phase positions | the minimap / map menu `0x60`, cursor free `0x80`, a unit's menu `0xE8`, a fight `0x108` (Fire Emblem: `0x50`, `0x70`, `0xD0`, `0xF0`) | same |

The RAM was mapped from Fire Emblem's by `tools/romxref.py`; the scripts were found running (process slots dumped with the harness) and matched across releases by their command sequences. Europe and the USA share their RAM; Japan's is 4 bytes lower, but for the process slots. Boot: the USA and Europe show a health and safety screen (A); Europe asks for a language first (A: English); Japan has neither.

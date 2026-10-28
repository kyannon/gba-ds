# Summon Night: Swordcraft Story

| | |
|---|---|
| Releases | `AB4E` USA · `AB4J` Japan (Craft Sword Monogatari; the same addresses). No European release. |
| Profile | `src/profiles.c`: the `AB4E` / `AB4J` entry (`sn_compose`) |
| Tests | `tests/regress/summon-night-swordcraft-story.json` (`tools/regress.py summon-night-swordcraft-story`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list summon-night-swordcraft-story`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SN-1. Status.** While playing, the second screen shows the Status screen (START > Status), the whole screen at the game screen's size, brought up on the hidden copy (also when the menu or the Status is open in the game). Where the menu won't open (a scene, a fight), the last picture stays.

**SN-2. Place.** The START menu's Place bar (where you are) is over the bottom of the Status, always: the box with the place's name across the whole width, its PLACE heading only as far as the Status's lines between ATK, DEF... start, so the row under the stats stays clear.

**SN-3. Title.** On the title screen the second screen shows the logo: the whole sword (down to its pommel), the name and its subtitle, without PRESS START BUTTON or the copyright line.

**SN-4. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

The Japanese release's Status and Place: no Japanese state past the title (it uses the USA release's addresses, found in its code).

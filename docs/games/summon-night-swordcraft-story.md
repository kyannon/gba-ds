# Summon Night: Swordcraft Story

| | |
|---|---|
| Releases | `AB4E` USA · `AB4J` Japan (Craft Sword Monogatari; the same addresses). No European release. |
| Profile | `src/profiles.c`: the `AB4E` / `AB4J` entry (`sn_compose`) |
| Tests | `tests/regress/summon-night-swordcraft-story.json` (`tools/regress.py summon-night-swordcraft-story`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list summon-night-swordcraft-story`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SN-1. Status.** While playing, the second screen shows the Status screen (START > Status), the whole screen at the game screen's size, brought up on the hidden copy (also when the menu or the Status is open in the game). Where the menu won't open (a scene, a fight), the last picture stays.

**SN-2. Place.** The START menu's Place bar (where you are) is over the bottom of the Status, always: the box with the place's name across the whole width, its PLACE heading only as far as the Status's lines between ATK, DEF... start, so the row under the stats stays clear.

**SN-3. Title.** On the title screen the second screen shows the logo: the whole sword (down to its pommel), the name and its subtitle, without PRESS START BUTTON or the copyright line.

**SN-4. Game screen untouched.** Nothing leaves the game screen.

**SN-5. Several characters.** Once the Status has more than one character (its "LR: Switch Characters" prompt along the bottom), the second screen turns through each one's Status in the game's order, 5 seconds each, every one with the Place bar over its bottom (SN-2), which covers the prompt.

**SN-6. Quick to follow.** The Status is taken again about every half second, and as soon as a new area has faded in; a moment where the menu won't open (a scene, a fade) doesn't hold the next one up.

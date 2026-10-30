# Summon Night: Swordcraft Story 2

| | |
|---|---|
| Releases | `BSKE` USA · `BSKJ` Japan (Summon Night: Craft Sword Monogatari 2) |
| Profile | `src/profiles.c`: the `BSKE` entry (with `BSKJ`) |
| Tests | `tests/regress/summon-night-swordcraft-story-2.json` (`tools/regress.py summon-night-swordcraft-story-2`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list summon-night-swordcraft-story-2`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**SN2-1. Logo.** The logo is the title's lettering (SUMMON NIGHT Swordcraft Story 2; in Japan サモンナイト クラフトソード物語2), without its glow, the sky and cliff behind it, PRESS START or the copyright line. The title comes after the intro (skipped with START).

**SN2-2. Status.** While walking about, the second screen shows the Status screen (START > Status: level, HP, experience, the portrait and the stats with each weapon), the whole screen as the game draws it, opened on the hidden copy and kept up to date about once a second.

**SN2-3. Logo elsewhere.** The second screen shows the logo at the title and from there until the first Status picture (the file menu, name entry, the story's opening).

**SN2-4. When the Status can't be opened.** In a conversation, a cutscene or the game's own menus, where START doesn't open the menu, the second screen keeps the last Status picture.

**SN2-5. Several characters.** Once the Status has more than one character (its L and R icons in the bottom corners), the second screen turns through each one's Status in the game's order, 5 seconds each, as the game draws it (the L and R icons too: there's no Place bar to cover them, unlike Swordcraft Story's).

## Not covered yet

- Battles, shops and the forge are untried: they're expected to keep the last Status picture (SN2-4).
- Only the start of the story has been tried (USA and Japan).

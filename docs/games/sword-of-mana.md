# Sword of Mana (Shinyaku Seiken Densetsu in Japan)

| | |
|---|---|
| Releases | `AVSE` USA · `AVSY` Europe (Es/It: ring 16 bytes up, language menu, later title) · `AVSJ` Japan (logo includes the 新約 sprites) |
| Profile | `src/profiles.c`: the `AVSE` entry; variants `AVSY`, `AVSJ` |
| Tests | `tests/regress/sword-of-mana.json` (`tools/regress.py sword-of-mana`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list sword-of-mana`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**SOM-1. Status screen.** During play the second screen shows the character's Status screen (START's ring: Status, then Status), opened on the hidden copy at every map refresh, at the game screen's size.

**SOM-2. Last one stays.** While it can't be opened (a conversation, a scene, the ring missing Status), the last one stays.

**SOM-3. HUD trimmed.** On the game screen the HUD's portrait and HP / MP figures are hidden; the charge bar under them stays.

**SOM-4. Title.** On the title screen the second screen shows the logo, and no captures are tried there.

# Tactics Ogre: The Knight of Lodis (Tactics Ogre Gaiden in Japan)

| | |
|---|---|
| Releases | `ATOE` USA · `ATOJ` Japan (no European release) |
| Profile | `src/profiles.c`: the `ATOE` and `ATOJ` entries (to_compose) |
| Tests | `tests/regress/tactics-ogre.json` (`tools/regress.py tactics-ogre`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list tactics-ogre`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**TO-1. Unit list.** In a battle, with the cursor free on the field (where R opens the menu), the second screen shows the battle's unit list (R, View List), the whole screen at the game screen's size, as the game draws it, without its R SEARCH and START:DATA prompts (the buttons don't work there): what's under them in the list shows instead. It's opened on the hidden copy at every map refresh and as soon as the field cursor has stopped on another square, so it stays up to date without the player opening it. With the list open in the game, the list as the player has it.

**TO-2. On the cursor's unit.** The list's cursor is on the unit the field cursor is on, the player's or the enemy's, wherever it is in the list (the list moves its box about: right or left, bottom or top). On a square with no unit, the list as it opens, on the leader.

**TO-3. Every page in turn.** The list's pages (START: level, HP, MP, element, the bars, side), one every 2.5 seconds, round to the first, turning at that pace through each refresh.

**TO-4. Title.** On the title screen the second screen shows the logo, and no captures are tried there.

**TO-5. Last one stays.** Where R doesn't open the list (a unit moving or acting, the enemy's turn, a scene), the last list pictures stay; before the first, the logo.

**TO-6. Logo.** The title's TACTICS OGRE lettering with The Knight of Lodis under it (and 外伝 beside it in Japan), without the emblem, OGRE BATTLE SAGA or the copyright lines.

**TO-7. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- Nothing known. (The Japanese release's list, its cursor on other units and its pages, and its enemy turn are checked since v1.4.2's last test builds.)

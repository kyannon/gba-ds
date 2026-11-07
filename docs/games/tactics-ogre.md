# Tactics Ogre: The Knight of Lodis (Tactics Ogre Gaiden in Japan)

| | |
|---|---|
| Releases | `ATOE` USA · `ATOJ` Japan (no European release) |
| Dumps | `ATOE` cartridge `6649d2ce` (tested) · `ATOJ` cartridge `0167a878` (tested) |
| Profile | `src/profiles.c`: the `ATOE` and `ATOJ` entries (to_compose) |
| Tests | `tests/regress/tactics-ogre.json` (`tools/regress.py tactics-ogre`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list tactics-ogre`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-6).

## Rules

**TO-1. Unit list.** In a battle (a story battle or Training, whose R menu has no Winning Condition), with the cursor free on the field (where R opens the menu), the second screen shows the battle's unit list (R, View List), the whole screen at the game screen's size, as the game draws it, without its R SEARCH and START:DATA prompts (the buttons don't work there): what's under them in the list shows instead. It's opened on the hidden copy at every map refresh and as soon as the field cursor has stopped on another square, so it stays up to date without the player opening it. With the list open in the game, the list as the player has it.

**TO-2. On the cursor's unit.** The list's cursor is on the unit the field cursor is on, the player's or the enemy's, wherever it is in the list (the list moves its box about: right or left, bottom or top; over 12 units it scrolls). On a square with no unit, no list: TO-9.

**TO-3. Every page in turn.** The list's pages (START: level, HP, MP, element, the bars, side), one every 2.5 seconds, round to the first, turning at that pace through each refresh. With the unit's details first (TO-8), they come after it.

**TO-4. Title.** On the title screen the second screen shows the logo, and no captures are tried there.

**TO-5. Last one stays.** Where R doesn't open the list (a unit moving or acting, the enemy's turn, a scene, the unit's details open in the game), the last list pictures stay; before the first, the logo. So do they while the field fades back in (after the details, the list or a scene closes, before the unit's box under the cursor is back): the next refresh, a second later, takes the list on that unit, not on the leader.

**TO-6. Logo.** The title's TACTICS OGRE lettering with The Knight of Lodis under it (and 外伝 beside it in Japan), without the emblem, OGRE BATTLE SAGA or the copyright lines.

**TO-7. Game screen untouched.** Nothing leaves the game screen.

**TO-8. The unit's details first.** With the field cursor on a unit (the player's, a guest or the enemy's), the first page is that unit's details, the screen B opens on it (name, class, level, HP and MP, STR, INT, AGI, element and alignment, equipment, talents; an enemy's in the game's green), the whole screen at the game screen's size, as the game draws it, without its START:DATA label or the arrows that blink in its bottom corners (the buttons don't work there). Then the list's pages (TO-1 to TO-3), so the whole round is 7 pages, 17.5 seconds. On a square with no unit, TO-9. With Map Animations off, the details stay. With the list open in the game, the list alone, as the player has it (TO-1). The details are taken on the hidden copy only after the list has opened there, so where R doesn't open the list (TO-5), B is never pressed either. Only the details' first half (equipment and talents, POW); its Emblem half (RIGHT) and the talents' MP costs (START) aren't shown.

**TO-9. An empty square: the battle, not a unit.** With the field cursor on a square with no unit, nothing about a unit (the list would sit on the leader): the battle's Winning Condition (R's menu: the aim, the place, the turn, the camera on its unit), its Losing Condition (R there), then the Supply list (Expendables: the party's items), 2.5 seconds each, round to the first. The conditions without their LOSING CONDITION / WINNING CONDITION prompts and R and L buttons, the supply list without its pointing hand (the buttons don't work there): what's under them shows. Training has no conditions: the supply list alone. With Map Animations off, the Winning Condition (in Training, the supply list) stays. Taken on the hidden copy only where R opens its menu on the field (TO-5).

## Not covered yet

- Nothing known. (The Japanese release's list, its cursor on other units and its pages, and its enemy turn are checked since v1.4.2's last test builds.)
- *By hand:* TO-8 on the device, through a battle later in the game (more units, talents, emblems): the details match the unit under the cursor, the round turns at an even pace.
- Lists over 13 units (a fourth full row, or a fifth): the list scrolls a row at a time, which the cursor search follows (the Training cases, 13 units), but no fixture has more. Two units with the same box (name, level, HP) would put the list's cursor on the first of them; the details are always the cursor's unit's.
- Quest Mode battles (the title's QUEST MODE) haven't been tried.
- Out of battle: on the world map (and in its shops, Training's setup) nothing is taken, so the last battle's pictures stay, or the logo; Organize's roster is the battle's list, so its pages turn there as with the list open in a battle (TO-1, to-us-organize); its unit details (A) keep the last pictures. What the world map should show is still to decide. The owner's GameShark save (`saves/ATOE-formido-lv6-gameshark.sav` in the fixtures: Fermin, level 6, at Formido, 13 units, cheat-raised levels and Goth) reaches all of them (states world-map.ss, organize.ss).

## For the merge

- Changelog (What's new): "Tactics Ogre: The Knight of Lodis: the second screen starts with the details of the unit under the cursor (the screen B opens: stats, equipment, talents), then the unit list's pages as before. On an empty square it shows the battle instead of a unit: the Winning and Losing Conditions and the supply list."
- Changelog (Fixes): "Tactics Ogre: The Knight of Lodis: the second screen works in Training battles, and finds the unit under the cursor in lists of over 12 units."
- Wiki, Supported-games.md: the table's row becomes "the details of the unit under your cursor and the battle's unit list; on an empty square, the battle's conditions and supplies"; in Tactics Ogre's section, the "In a battle" line says "...yours or the enemy's. It follows as soon as your cursor stops on another square." (drop "(on an empty square, your leader)"), and after it add:
  - "**With your cursor on a unit,** the first page is that unit's details, the screen B opens on it (stats, element, alignment, equipment, talents; an enemy's in green), then the list's pages: 7 pages, 17.5 seconds round. Its START:DATA label and blinking arrows are left out."
  - "**On an empty square,** nothing about a unit: the battle's Winning Condition, its Losing Condition and the Supply list, 2.5 seconds each (in Training, which has no conditions, the supply list alone), without their button prompts."

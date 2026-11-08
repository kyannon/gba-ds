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

**TO-1. Unit list.** *Retired:* the battle's unit list (R, View List), with its cursor on the field cursor's unit, isn't shown any more: too much, and too slow to turn through, beside the unit's details (TO-8).

**TO-2. On the cursor's unit.** *Retired* with TO-1: the details (TO-8) are always the field cursor's unit's.

**TO-3. Every page in turn.** *Retired* with TO-1 (the details' pages turn: TO-8).

**TO-4. Title.** On the title screen the second screen shows the logo, and no captures are tried there.

**TO-5. Last one stays.** In a battle, where R's menu doesn't open on the field (a unit moving or acting, a scene, the unit list or a unit's details open in the game), the last pictures stay; before the first, the logo. So do they while the field fades back in (after a screen or a scene closes, before the unit's box under the cursor is back): the next refresh, a second later, takes the details of that unit.

**TO-6. Logo.** The title's TACTICS OGRE lettering with The Knight of Lodis under it (and 外伝 beside it in Japan), without the emblem, OGRE BATTLE SAGA or the copyright lines.

**TO-7. Game screen untouched.** Nothing leaves the game screen.

**TO-8. The unit's details.** In a battle, with the field cursor on a unit (the player's, a guest or the enemy's; wherever the game puts the unit's little box on the field, which it moves out of the cursor's way), its details, the screen B opens on it, the whole screen at the game screen's size, as the game draws it (an enemy's in the game's green), its three pages in turn, 2.5 seconds each: as it opens (name, class, level, HP and MP or SP, STR, INT, AGI, element and alignment, equipment, talents and their POW), the talents' MP (or SP) costs (START), the Emblems (RIGHT); without its START:DATA label or the arrows that blink in its bottom corners (the buttons don't work there). With Map Animations off, the first page stays. Taken on the hidden copy only where R's menu opens on the field (TO-5). On a square with no unit, TO-9.

**TO-9. An empty square: the battle, not a unit.** With the field cursor on a square with no unit, nothing about a unit: one page, on black, made of the game's own boxes from R's menu: the Winning Condition's box (its label and aim) at the top, where the game has it; the Losing Condition's box (R there) as far from the bottom; the place's name box (Lutra Islands...) in the middle. Not the field behind them (black round the boxes' corners too), the turn number or their R / L prompts. Training has no conditions: the logo, as long as the cursor stays on that empty square. Taken on the hidden copy only where R opens its menu on the field (TO-5).

**TO-10. World map: the location's HELP.** On the world map (USA), the HELP box SELECT opens on a location (two lines about it), alone, in the middle of a black screen: of the location the cursor is on, or, with the cursor between locations, of the player's own (where the army's flag stands). Taken again as the cursor reaches another location (and at each refresh). In the world map's menus (R, the shops, Training's setup) the last picture stays.

**TO-11. Organize: the unit's details.** In Organize (USA), the details of the unit the roster's cursor is on (A on it), as the game draws them, their three pages in turn, 2.5 seconds each: the first (equipment, talents and their POW), the talents' MP costs (START), the Emblems (RIGHT); without their START:DATA label. Not the roster's pages. Taken again as the roster's cursor moves; with a unit's details open in the game, that unit's, from the first page.

**TO-12. Out of battle, nothing stale.** Once the last picture came from the world map or Organize (TO-10, TO-11), wherever there's nothing to take (the world map's command bar after Organize, its other menus, a town, Training's setup until its units are placed), the logo, not the last picture, until a picture is taken again: on the free world map, in Organize, or in a battle. (In a battle, TO-5: the last pictures stay.) The logo can stay up to a second after the new picture is taken.

**TO-13. The enemy's phase: the logo.** From the moment the enemy's phase begins (End of Turn) until the player's next phase, the logo. Then the details of the unit the player's turn starts on (the leader, where the camera goes), as the Player Phase banner clears, not the last turn's pictures for a moment.

**TO-14. Victory: the logo.** Once the battle is won (as the last blow's EXP box clears, before Congratulations!), the logo, through Congratulations!, the spoils and the scene after, until a picture is taken again (the world map, the next battle). USA; in Japan the last pictures stay (no state of a Japanese victory yet).

## Not covered yet

- Nothing known. (The Japanese release's list, its cursor on other units and its pages, and its enemy turn are checked since v1.4.2's last test builds.)
- *By hand:* TO-8 on the device, through a battle later in the game (more units, talents, emblems): the details match the unit under the cursor, the pages turn at an even pace.
- Quest Mode battles (the title's QUEST MODE) haven't been tried.
- Japan's world map and Organize (TO-10, TO-11): no Japanese save to reach them, so its out-of-battle addresses aren't known; there the world map keeps the last pictures and Organize shows its roster's pages, as before.
- The world map's HELP for the player's own location finds the army's flag (a sprite); a location whose flag stands somewhere else, or a map with no flag (late in the story?), keeps the last picture. Towns and the world map's other screens (shops, the Warren-like menus) haven't been looked at beyond Organize.
- The owner's GameShark save (`saves/ATOE-formido-lv6-gameshark.sav` in the fixtures: Fermin, level 6, at Formido, 13 units, cheat-raised levels and Goth) reaches the world map, Organize and Training.

## For the merge

- Changelog (What's new): "Tactics Ogre: The Knight of Lodis: in a battle, the second screen shows the details of the unit under the cursor (the screen B opens), its three pages in turn: stats and equipment, its talents' costs, its Emblems; the unit list isn't shown any more. On an empty square it shows the battle's Winning and Losing Conditions and its place instead (the logo in Training, during the enemy's phase and once the battle is won). On the world map it shows the HELP text of the location under the cursor (or of where your army is), and in Organize the details of the unit under the cursor; elsewhere out of battle, the logo."
- Changelog (Fixes): "Tactics Ogre: The Knight of Lodis: the second screen works in Training battles, and on every unit, wherever it is in the unit list (an enemy's details could fail to appear)."
- Changelog (Known issues): "Tactics Ogre: The Knight of Lodis: the world map's HELP, Organize's details and the logo after a victory are only in the USA release; in Japan the world map and a won battle keep the last picture and Organize shows its roster's pages."
- Wiki, Supported-games.md: the table's row becomes "in battle, the details of the unit under your cursor (on an empty square, the battle's conditions); on the world map, the location's HELP; in Organize, the unit's details"; Tactics Ogre's section becomes:
  - "**In a battle,** with your cursor on a unit (yours, a guest or the enemy's), the second screen shows its details, the screen B opens on it: three pages, 2.5 seconds each (stats, element, alignment, equipment and talents; the talents' costs; the Emblems). An enemy's are in the game's green. It follows as soon as your cursor stops on another square. Its START:DATA label and blinking arrows are left out."
  - "**On an empty square,** nothing about a unit: the battle's Winning Condition at the top, its Losing Condition at the bottom and the place's name in the middle, on black. Training has no conditions, so there it's the logo."
  - "It's brought up in the background on a silent copy of the game; your game is never paused or touched. While a unit moves or acts and in scenes, the last pictures stay. During the enemy's phase, the logo, until your next turn starts; and once the battle is won (USA), through Congratulations! and the scene after."
  - "**On the world map** (USA), the HELP text SELECT opens on the location under your cursor, or on where your army is when the cursor is between locations, in the middle of a black screen."
  - "**In Organize** (USA), the details of the unit under your cursor, their three pages in turn."
  - "Out of battle, where there's nothing to show (the world map's command bar, its other menus), the logo."

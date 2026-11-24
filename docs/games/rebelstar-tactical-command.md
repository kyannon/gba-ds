# Rebelstar: Tactical Command

| | |
|---|---|
| Releases | `BRLE` USA · `BRLP` Europe (English, French, German, Spanish, Italian) (no Japanese release) |
| Dumps | `BRLE` cartridge `fb6c590f` (tested) · `BRLP` cartridge `e448c5d4` (tested) |
| Profile | `src/profiles.c`: the `BRLE` and `BRLP` entries (rs_compose, rsUsa / rsEur) |
| Tests | `tests/regress/rebelstar-tactical-command.json` (`tools/regress.py rebelstar-tactical-command`) |
| Toggle | `pages`: a soldier's three pages (RS-3) · GEN-14 |
| Status | supported (merged into dev Nov 16, 2026, for v1.5.5) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list rebelstar-tactical-command`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

## What goes on the second screen, and why

The game is a turn-based squad battle on an isometric field, with the soldier under the cursor in a small box over the field (AP, HP and morale bars). Everything else about a soldier (their equipment, attributes and skills) is on their **Status** screen, three menus deep (A on the soldier, Status, then LEFT / RIGHT through its pages), and the mission's aim is on the **Objectives** screen (START, Objectives). Made for a two-screen GBA, the field would stay on one screen and those two screens, which the game otherwise makes you leave the battle to read, would be on the other (Philosophy, rule 1: "ask what the game would have done"; Fire Emblem's and Tactics Ogre's unit details are the model). Both are the game's own screens, opened on the hidden copy (rules 3 and 4). The game has no map screen of its own, so there's no map to show.

## Rules

**RS-1. Logo.** The title's lettering, *rebelstar* with the teal star and TACTICAL COMMAND under it, cut out of the title picture it's painted into (the planet and space behind it left out; not the TM or Press START) by the owner's copy of the logo (tools/logo_stencils/rs.png), scaled and placed on the title's letters. (Until the first test build's second version, the strip of the title picture behind the lettering, rows 8-66.)

**RS-2. Out of battle: the logo.** The title and the opening, the main menu and its save slots, Skirmish's setup, the story's scenes and chapter cards, Mission Loading, the screens before a battle (Deployment, Equip Squad, placing the soldiers) and the end of a battle (VICTORY TO THE REBELS, a defeat): the logo, and nothing from the last battle is kept.

**RS-3. A soldier under the cursor: their Status.** In a battle, on the player's turn, with the field cursor free (no menu open) on one of the player's soldiers, the second screen shows that soldier's Status, the whole screen as the game draws it, its three pages in turn, 2.5 seconds each: EQUIPMENT (what they carry), ATTRIBUTES (Speed, Strength, Courage, Perception, Dexterity, Intelligence, Leadership), SKILLS. Without the D-pad marks in its top corners, which turn its pages in the game (the buttons don't work there). With Map Animations off, the first page stays. Taken again as soon as the cursor reaches another soldier.

**RS-4. Anything else under the cursor: the Objectives.** With the field cursor free on an empty square, an enemy the player can see, or a square where an unseen enemy stands (the game shows no box there, and the second screen shows nothing more about it), the mission's Objectives screen, as the game draws it.

**RS-5. The rest of the player's turn: the last picture stays.** With a soldier's action menu open, moving, aiming or shooting, the START menu and the screens it opens (Objectives, Options, Quit Mission), a Status opened in the game, SELECT's help pages, and conversations on the battle map: the last picture stays; before the first one in a battle, the logo.

**RS-6. The enemy's turn: the logo.** From the moment the turn ends until the player's next turn: the enemy's turn and the turn banners (REBELS Turn: n/m, PRESS START) show the logo. As the field comes back, the second screen shows the soldier (or Objectives) under the cursor at once.

**RS-7. Training missions.** The first six missions (Boot Camp to Team Work) teach the game with green boxes of instructions over the field, and take over the buttons while they do: the game only lets the player do the step it asks for (the action menu opens, but its cursor won't leave Move; START's menu won't open). While a green box is up, and wherever the game won't open the Status or the Objectives, the last picture stays (before the first, the logo). In practice the training missions show the logo for most of their guided turns. (Checked on the device Nov 16, 2026, with the training played on past these moments: the game is never disturbed.)

**RS-8. Game screen untouched.** Nothing leaves the game screen.

**RS-9. Skirmish.** Skirmish battles (any map, either side) are as the story's: RS-3 to RS-6.

**RS-10. Europe.** The European release is the same game in five languages: everything above holds in each (the Status and the Objectives as the game draws them in the language chosen). Its language list, which comes first when there's no battery save, shows the logo (RS-2), and the logo is the same lettering from the same title picture.

## Not covered yet

- *By hand:* RS-3 on the device, later in the story: a soldier with heavy armour, power-ups and a full pack (the Equipment page), with Psionics and other skills (the Skills page); the pages match the soldier under the cursor and turn at an even pace.
- The story's missions after the training ones (from First Patrol on) haven't been played here: the tests use Skirmish battles, which run on the same battle code, and the training missions. Missions with an ally the player doesn't control (a guest) would show the Objectives on the guest (RS-4). Two Player Skirmish (link cable) not tried.
- A battle won or lost from the story (its own end scenes) was seen only in Skirmish (VICTORY TO THE REBELS: the logo, RS-2). Saving in a battle (START, Save Game) wasn't reached: its screen, if not one of the known ones, shows the logo and drops the picture until the field is back (a second).
- Europe's release is tested in English and French (Skirmish, Boot Camp, the title); German, Spanish and Italian use the same code and screens, untried.

## Finding the game's state

For later sessions; addresses USA, with Europe's after a slash where they differ.

- **Europe:** the same game, its code and data moved: the running object at `0x02013CC8`, the cursor object's pointer at `0x02001F8C`, the units from `0x0200FD94`; the classes field `0x02005810`, action menu `0x02005AA0`, START menu `0x02006868`, enemy turn `0x02005540`, deployment `0x02005408`, turn banner `0x02004214`, Status `0x02004358`, Objectives `0x02005EA0`, help `0x02005308`, Options `0x02005EE0`, Quit Mission `0x02004718` (found with `tools/romxref.py` from the USA's and checked in the game, but deployment's, which romxref gave and fits the others' shift). Its menus' labels are 40 bytes each (30 in the USA) and translated, so nothing is found by its text: Objectives is START's fifth item in every language. The battle object's offsets, the menus' and the units' layout are the USA's.
- **The running object:** the word at `0x02013368` points to the game's current state object, whose first word is its class (a table of functions, copied to EWRAM: the same address in every battle). Battles: the battle object keeps one class while the field cursor is free, `0x02004EF0`, and changes it for the action menu (`0x02005180`), choosing where to move (`0x02004B28`), walking (`0x02004E70`), aiming (`0x02004CA0`, `0x02004C60`), Rally (`0x02005140`), the START menu (`0x02005F08`), a conversation on the field (`0x02004B68`), the enemy's turn (`0x02004C20`), placing soldiers before the battle (`0x02004AE8`). It's told from other objects by the halfwords 3, 4 at +0x0C. Screens pushed over the battle are objects of their own: a Status `0x02003A78`, Objectives `0x02005540`, help `0x020049E8`, Options `0x02005580`, Quit Mission `0x02003E38`. Out of battles: title `0x02005EC8`, the opening `0x02005500`, main menu `0x02005368`, save slots `0x02005AA0`, Skirmish's setup `0x02003AF8`, chapter card `0x02003974`, story scene `0x02003F90`, Mission Loading `0x02005440`, the turn banner `0x02003934` (also at a battle's start), the pre-battle menu `0x02003F50`, choosing a soldier to deploy `0x020039B4`, Equip Squad `0x020042D0`, the victory screen `0x02005880`.
- **In a battle or a cutscene:** a battle (or its deployment) has a field cursor object, pointed to by `0x02002008` (0 elsewhere; the cursor's square at +4, +5). A story scene, a chapter card or a menu has neither that nor the battle object. Conversations played on the battle map run on the battle object (class `0x02004B68`).
- **On the battle object:** +0x28 the unit under the cursor (0xFF for none, and for an enemy the player can't see), +0x24 the unit the menu is for, +0x5C the action menu, +0x60 the START menu. A menu: +0x0C its visible items, +0x10 its cursor, its labels from +0x18, 30 bytes each (START's in the order shown, Objectives the fifth (found by its place, not its label); the action menu's are every action, of which the unit's are shown, Status always the last but one, before Cancel). A long action menu (an armed soldier's) scrolls and doesn't wrap; a short one wraps.
- **Units:** 0x48 bytes each from `0x0200F434` (the cheat file's codes write +0x1C on), the square at +0x0D, +0x11, the side at +0x1C (0 the player's), AP +0x1E / +0x1F, HP +0x20 / +0x21.
- **Training missions:** their instructions are green boxes on BG1, palette 8, filled with its colour 4 (the unit and menu boxes on the same layer and palette are filled with colour 1, blue); their tiles move about, so the tiles' pixels are looked at (`rs_green_box`). While they guide, the battle object also lists the squares the game points to with blue arrows (+0x48 on: pointers, and the square, as at +0x54). The game's own lock on the buttons wasn't found in memory; the hidden copy finds it by trying (RS-7).
- **The logo:** the title (BG3, a picture) has the lettering painted in, its greys and dark outlines the same as the planet's and space's behind it, so the outline is the owner's copy of the logo, scaled to 0.21 and placed at 7.5, 10.5 (the best correlation with the title's pixels, 0.90, by search): `tools/make_logo_masks.py RS=<the title frame>` (the frame isn't read: `GBADS_LOGO_DEBUG=1` harness `logo` writes it).
- **The Status screen:** the D-pad marks are BG1's only tiles there, so the pages are taken with BG1 off (the renderer takes a layer change at a state load: the page is saved and loaded again around the picture).

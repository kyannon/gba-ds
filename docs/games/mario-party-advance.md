# Mario Party Advance

| | |
|---|---|
| Releases | `B8ME` USA, `B8MP` Europe, `B8MJ` Japan |
| Dumps | `B8ME` cartridge `f094a4cb` (tested), Virtual Console `1d910a62` · `B8MP` cartridge `c8c889e2` (tested), Virtual Console `46c66f40` · `B8MJ` cartridge `baa4a82b` (tested), Virtual Console `9cd07025` |
| Profile | `src/profiles.c`: the `B8ME` entry; `B8MP` and `B8MJ` are variants of it, with nothing to move (Europe's logo steps: `mpa_eur`) |
| Tests | `tests/regress/mario-party-advance.json` (`tools/regress.py mario-party-advance`) |
| Toggle | `pages`: the Map and Quest Data, in turn (MPA-2) · GEN-14 |
| Status | merged into `dev` for v1.6.4 (pull request #100 by cmyksoda; Europe and Japan added at review) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list mario-party-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules).

The game is one big board, Shroom City, that Mario drives a car round, with a Map of the island and a page of quests, each a screen of its own that the player leaves the board to open. The second screen keeps both open in turn.

## Rules

**MPA-1. Logo.** The logo is the title's lettering: Mario Party with its Advance and ® (BG0 above the copyright line, without the sky, the rainbow or PRESS START), taken from power-on with START pressed through the Toad intro. Europe's has ™ in place of ® and is taken after its language screen (English); Japan's is its own lettering, with its two ®.

**MPA-2. On the board: the Map and Quest Data, in turn.** While Mario is on the board and the player is free (at the start of a turn, or with the car walking), the second screen shows two of the game's own pages, five seconds each, the Map first. The Map (R, then A in the look-around): the island with its areas, the one the car is in bright with its name on the green label, the car, and the quest places. Quest Data (L, then Quest Data in the menu): the quest being asked and who asks it, how many of the 50 are done, and the list of those met. Both are opened on the hidden copy from the board as it is and taken again about once a second (Settings: Map refresh), so the car on the Map trails the live one by a second or two.

**MPA-3. No prompts.** The pages' prompts, cursor and arrows are left out: the button hints along the bottom of both, the Map's cursor ring, its scroll arrows and the name of what the cursor is on (the brown bar stays, empty), and Quest Data's scroll arrows. The pages' own pictures show there instead.

**MPA-4. The game screen as the game draws it.** The HUD (the mushroom counter and the area's name at the top right, the button hints along the bottom) stays where the game draws it.

**MPA-5. Dialogs, scenes and menus: the last pages stay.** Wherever the board isn't free (a dialog, a "stop here?" prompt, the dice, the L menu, a quest's cutscene), the two pages from the last time the board was free stay on the second screen, taking turns as before, so there's one steady second screen. Nothing new is taken until the board is free again.

**MPA-6. The game's own pages.** While the game's own Map or its Quest Data is open on the game screen, the second screen shows the logo at half strength, so the same page is never on both. The logo stays through the blank moment between the page's fade out and its fade in, and for a second after the page has closed (the board fading back in); then the last pages come back.

**MPA-7. Logo elsewhere.** The second screen shows the logo at half strength before the first pages (the opening, the title, the name entry, and a first turn's dialogs), and at the main menu (Shroom City, Play Land, Party Land, Challenge Land), where the old board's pages are dropped and the next board takes new ones.

**MPA-8. First-time dialogs.** On a new file the game explains R and L with a few boxes the first time they're pressed. Those boxes are cleared on the hidden copy, so the pages are there from the first turn, while the game screen keeps its boxes for the player to read.

## Not covered yet

- The minigames (all locked on a new file), Party Land, Challenge Land and the other five areas' boards (Seaside, Jungle, Horror, Snow and Desert): the pages stay or the logo shows, whichever the game's registers give.
- The Map with a flag placed, and the results screens.
- Europe and Japan: checked at a new file's first turn (the pages, with the first-use boxes cleared, and the game's own pages); the rest of the rules were checked on the USA release, whose addresses and screens they share. Europe in English and French; German, Spanish and Italian not seen.

## Known issues

- The car on the Map trails the live one while it moves (the hidden copy takes about 200 frames for both pages).
- The area the Map opens on is the game's own choice: with the car on a border, it may be the neighbouring area.
- The hidden copy takes about 230 frames (550 while a first-use dialog is cleared) for both pages each time it refreshes, once a second by default: a slow device may need a longer Map refresh.

## Notes

| | |
|---|---|
| Mode | 0x03002D74: 4 at a turn's start, 2 with the car walking, 0x80 in a dialog, a menu, a page of the game's own and the look-around, 0 on the title, the main menu and the Map, 0x22 on the minigame picker |
| Scene | 0x030029CD: 0 on the board, 1 in the look-around and on the Map, 2 on Quest Data, 5 on the main menu |
| Board | DISPCNT 0x1D40 (BG0 dialogs and menus, BG2 the path and its places, BG3 the ground, sprites; 0x5D40 with a window on); everything of the HUD is sprites |
| Map | DISPCNT 0x5C40, BG0CNT 0: BG3 the island's parchment and the brown bar, BG2 the name on the bar; sprites the hints (OAM 0-5, palette 3, y 112), the cursor ring (the only 32x32 sprite in palette 1), the arrows (palette 0, 8x16 and 16x8, y 128-152, on the bar), the area label, the car and the quest places. Their tiles move with the area label's text (USA: cursor 336, arrows 372-374; Japan: 314, 350-352), so the hiding rules go by palette, shape and place. R opens the look-around at once; A there fades to black over 33 frames, the Map fades in over 30 more (about 80 frames after A) |
| Quest Data | DISPCNT 0x1F40, BG0CNT 0x1A08: BG0 the list; sprites the request's frame and portrait, the counter, the list's icons, the scroll arrows (tile 312, palette 0) and the hints (palette 2, y 142). L opens the menu in a frame but it takes A only about 20 frames later; A fades out over 35 frames, black until about 60, the page fades in to about 96 after A |
| Fades | BLDY is left at 0x10 with BLDCNT 0 through a dialog: only a blend on (BLDCNT mode 2 or 3 with targets) is a fade |
| First use | the first R, L (and A, to roll) on a file each open boxes of one-time hints (three for R, five for L); the hidden copy presses A through them |
| Releases | Europe and Japan have the USA release's RAM addresses, registers and Quest Data sprites. Europe asks for its language on a file with no battery save (A, then LEFT to its Yes: its cursor starts on No) and saves it; its game then runs as the USA one in the language chosen |
| Title | the lettering and copyright line alone on BG0 (the logo 223x71, above row 100), BG1 clouds, BG2 sky and rainbow, PRESS START and the balloons sprites; from about frame 3300 of a fresh start, 60 frames after START cuts the Toad intro short (the health and safety screen takes a held button, 30 frames, at frame 300) |


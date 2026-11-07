# Super Mario Advance (Super Mario Advance: Super Mario USA + Mario Brothers in Japan)

| | |
|---|---|
| Releases | `AMAE` USA and Europe · `AMAJ` Japan (the same screens and display settings: no variant) |
| Dumps | `AMAE` cartridge `1e4c6d6a` (tested), Virtual Console `5251f2bf` · `AMAJ` cartridge `b993b92f` (tested), Virtual Console `49684d37` |
| Profile | `src/profiles.c`: the `AMAE` entry (with `AMAJ`) |
| Tests | `tests/regress/super-mario-advance.json` (`tools/regress.py super-mario-advance`) |
| Status | testing (branch `game/super-mario-advance`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list super-mario-advance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Modelled on Super Mario Advance 2 and 4 ([SMA rules](super-mario-advance-2.md), [SM4 rules](super-mario-advance-4.md)): in a level the status bar leaves the game screen for the top of the second screen. Super Mario Bros. 2 has no world map and no menu that opens from a level with anything worth a second screen (its pause box holds only the overall Ace Coin percentage and four choices), so there's no capture on the hidden copy. Its nearest thing to a map is the course board the game shows before each level, kept from the game screen as it's shown and put under the status bar (SMB2-8).

## Rules

**SMB2-1. Level: status bar.** In a level of Super Mario Bros. 2 the status bar along the top of the game screen (WORLD and the level's number, the level's Ace Coins, lives, SCORE: rows 0-15) goes to the top of the second screen, where the game draws it, with the course board below it (SMB2-8), on black; with no board kept yet, the logo (GEN-1) at full strength centred in the room below it. The game screen shows the level there instead. The life meter (the hearts down the left side) stays on the game screen, by Mario, as the game draws it. The Yoshi Challenge's levels are the same.

**SMB2-2. What stays in a level.** The pause box (START: PAUSE, the Ace Coin percentage, Continue, Save and Continue, Save and Quit, Try Again), drawn on the status bar's layer below it, stays on the game screen; the status bar stays on the second screen while it's open.

**SMB2-3. Through doors, jars and Subspace.** The status bar stays on the second screen through doors, jars, Subspace, rocket rides and boss rooms (the game keeps it on the same layer everywhere in a level), and through the dark moment between rooms, when the game blanks its screen: the last picture stays then rather than the logo flashing up. *By hand: the tests have no state in a door, jar, Subspace or boss room yet (see "Not covered yet"); check on the device.*

**SMB2-4. Between levels: the logo.** The character select (CHOOSE A PLAYER), the course board (the 20 levels with their Ace Coins, shown before each level), the level card (WORLD 1-1 and its picture), the Bonus Chance slot machine and the game over screen are the game's whole picture on the game screen; the second screen shows the logo at half strength (nothing live to show, GEN-1).

**SMB2-5. Menus and Mario Bros.: the logo.** The title, the game select (Choose a Game), the file select, the Yoshi Challenge's level select and all of Mario Bros. (the arcade game, its own HUD and all) are left as the game draws them, and the second screen shows the logo at half strength.

**SMB2-6. Logo.** The logo (GEN-1) is the title screen's SUPER MARIO ADVANCE lettering with the picture the title draws round it (Mario on top, Birdo at the left: one layer of the title), without the Single Player / Multiplayer choices, the copyright line or the palm trees and hills behind. The Japanese release's title is the same picture with its own (katakana) lettering.

**SMB2-7. Both releases.** The USA and Europe cartridge and the Japanese one behave the same (their levels use the same display settings, so there's no variant).

**SMB2-8. Level: the course board under the status bar.** The course board the game shows before each level (World 1 to 7, each level's box, starred once its five Ace Coins are found, and the Ace Coin percentage) is kept from the game screen, from its first moment in place there, and in the level that follows it's on the second screen under the status bar, one row below it: its panel from the top of its frame down to its grass (rows 8-150 of the 160; the rest of the board's background, above and below, left off so it fits). It's the board as it was before the level: Ace Coins found during the level show on the status bar, in the percentage (SMB2-9) and on the next board's boxes. The Yoshi Challenge's board (orange background, cyan frame) is kept the same way. It stays through doors, pausing and everything in SMB2-3, and is kept with a state saved by GBA DS (GEN-7); a state from before this, or from another emulator, loaded in a level has the logo there until the next board. While the board itself is on the game screen, the second screen shows the logo (SMB2-4). *Philosophy: an exception to be decided by the owner on the device (cropped from the game's whole screen and held from before the level).*

**SMB2-9. The board's Ace Coin percentage, live.** On the board under the status bar (SMB2-8), the Ace Coin percentage at its bottom right is the pause box's (START: the percentage there counts the coins found in this level too), taken on the hidden copy about once a second while Mario is in a level and not paused, in the pause box's own (smaller) digits, right-aligned where the board's own number ends; the board's own number is cleared from under it. Until the first one is taken after a board (the first second of a level), the board's own number shows. Kept with a state saved by GBA DS (GEN-7). (Like Castlevania: Circle of the Moon's completion rate, copied onto its map from its pause screen.)

## Not covered yet

- SMB2-3 needs states in Subspace, in a jar and in boss rooms (Birdo, Mouser, Tryclyde, Fry Guy, Clawgrip, Robirdo, Wart); the tests so far run level starts. *By hand on the device till then.*
- SMB2-4's Bonus Chance and game over screens: checked by hand only.

## Known issues

- Virtual Console dumps (USA/Europe and Japan) not tried: expected to behave as the cartridges.

## For the merge

- Changelog (under the open version, What's new): "Super Mario Advance: in Super Mario Bros. 2's levels the status bar (world, Ace Coins, lives, score) moves to the top of the second screen, with the course board shown before the level (every level's Ace Coins, and the Ace Coin percentage kept up to date) below it; the title, menus, the screens between levels and Mario Bros. show the logo."
- `wiki/Supported-games.md` table row: Super Mario Advance | status bar and course board on the second screen in levels | USA/Europe, Japan.
- `wiki/Supported-games.md` section:

  ### Super Mario Advance

  In Super Mario Bros. 2's levels (the Yoshi Challenge too), the status bar along the top (world and level, the level's Ace Coins, lives and score) moves to the top of the second screen, with the course board the game showed before the level (every level and its Ace Coins) below it; the hearts stay by Mario on the game screen, and so does the pause box. Everywhere else (the title and menus, choosing a player, the course board and level card before each level, the Bonus Chance, and the Mario Bros. arcade game) the second screen shows the logo.
- README: one more supported game in the count.
- `docs/games/README.md` table row: Super Mario Advance | SMB2 | `AMAE` + 1.

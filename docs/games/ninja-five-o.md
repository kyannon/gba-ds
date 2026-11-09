# Ninja Five-O (Ninja Cop in Europe)

| | |
|---|---|
| Releases | `ANXE` USA (Ninja Five-O) · `ANXP` Europe (Ninja Cop; in English) |
| Dumps | `ANXE` cartridge `eb3954c6` (tested) · `ANXP` cartridge `c14295b1` (tested) |
| Profile | `src/profiles.c`: the `ANXE` entry; variant `ANXP` |
| Tests | `tests/regress/ninja-five-o.json` (`tools/regress.py ninja-five-o`) |
| Status | testing (branch `game/ninja-five-o`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list ninja-five-o`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## What the second screen is for

The game has no status screen of its own: START brings up a three-line menu (Quit Mission, Quit Stage, Resume) over the stage, and the mission select only shows a mission's best score. Everything the player needs to know while playing is the HUD the game draws over the stage: the health and ninpo (special) bars, the score (the time in Time Trial), the boss's bar in a boss fight and the Launch Site's countdown along the top, and the shuriken's level, the keys found and the hostages still to rescue (RESCUE) along the bottom. So, as the game would have done on a two-screen GBA, the HUD goes to the second screen, and the game screen shows the stage alone: no bars over Kabuki on his platform at the top of the screen, no RESCUE row over the floor. That's a part of the live screen moved over, the second of the simple approaches; nothing is opened on a hidden copy, so nothing on the second screen is ever behind the game.

## Rules

**NFO-1. Logo.** The logo is the title's lettering: in the USA, NINJA in its black brush stroke with FIVE-O, from the white title screen (the white is left out where it's outside the lettering; inside the brush stroke it stays, as on the title), its lowest drips trimmed at the row where the copyright line starts so that it fits under the HUD (NFO-2); in Europe, NINJA COP from the blue title screen. Without PRESS START, the copyright line or the screen's own white or blue. The ™ is too small to keep apart from the screen behind it: in the USA it's left out, in Europe only its M is left.

**NFO-2. In a stage: the HUD on the second screen.** While a stage is being played (any of the six missions' stages, the boss fights, the Launch Site; Easy, Normal, Hard and Time Trial alike), the HUD leaves the game screen and is drawn on the second screen, on black (a live screen, GEN-1), as the game draws it from frame to frame: along the top where the game has it, the health bar and the ninpo bar (top left), the score, or Time Trial's time (top middle), the boss's bar (top right, in a boss fight) and the Launch Site's countdown (its big red figures, top middle); and the game's bottom row (the shuriken and its level at the left, the keys found, RESCUE with a figure for each hostage still to rescue at the right) moved up to just under the bars, from row 24. The logo (NFO-1) is centred in the room below them, at full strength. On the Launch Site, the countdown's lower half is over the logo's top.

**NFO-3. What stays on the game screen.** Everything that isn't the HUD stays where the game draws it: the stage, the ninja, enemies, hostages and items, the STAGE card as a stage starts, the boss's name and picture before a fight, the dying ninja and the circle that closes on him, and the game's own pause menu (NFO-5). The stage's own signs (a door's arrow, an airport's board) are part of the stage, even at the top of the screen.

**NFO-4. Logo elsewhere.** The second screen shows the logo at half strength on dark grey (GEN-1) whenever no stage is being played: the Konami and Nintendo screens, the opening (the city and EMERGENCY!), the demo (a stage played with no HUD: the HUD's layer is set up but empty, and after a third of a second of that the logo stays), the title, Select Mode, Difficulty, Select Mission, Time Trial's Select Stage, a mission's briefing (its pictures and text), and MISSION FAILED with its score after the ninja dies (from the moment the game takes the stage away, as the closing circle ends).

**NFO-5. The game's pause menu.** START's menu (PAUSE: Quit Mission, Quit Stage, Resume) stays on the game screen, over the stage; the HUD stays on the second screen meanwhile.

**NFO-6. Both releases.** Ninja Cop in Europe draws the same HUD in the same places; the second screen is the same as in the USA but for its logo (NFO-1).

**NFO-7. One screen.** With `map = 0` or on one screen, the game is as it is: the HUD on the game screen. *By hand: no state needed; check on the device that with Map off the bars and RESCUE are back on the game screen.*

## How it was checked

A new game was started on Normal and each stage reached by setting the mission and stage bytes (0x03003D41 and 0x03003D42) as the mission's briefing starts, then played for about a minute with random input, the ninja's health (0x03001F10, as in the CodeBreaker codes on almarsguides.com) held full: in all 20 stages the HUD's sprites are priority 1 in the top 24 rows (the Launch Site's countdown too, at row 12), the stage's own sprites priority 2, the STAGE card and pause menu 0; BG0 holds nothing but the bottom row, with the same settings (screen 31, tiles 3) in every stage (priority 0, 1 in the Launch Site). Those settings with BG0 on are what turns the HUD on: the menus, briefings, MISSION FAILED and the title use BG0 otherwise or not at all. The ninja's health set to 0 gives the death, its closing circle and MISSION FAILED. Time Trial (from a GameShark save with every stage open, `tools/sharkport.py`: the save's EEPROM data as stored) has the same HUD with the time in place of the score.

## Not covered yet

- **The end of a stage and of a mission.** Going through a stage's red door, beating a boss, a mission's results (if it has any screen of its own), the next mission's unlocking and the ending's credits weren't reached: setting the game's state for them didn't work, and playing through a stage blind was out of reach. Whatever the game shows there with the HUD's layer on is treated as part of the stage (NFO-2); anything else gets the logo (NFO-4). A state at each of those moments (from State Saver: a stage's door, a boss's last hit, a mission's end, the ending) would show what to do.
- **The keys and RESCUE DONE.** The bottom row's keys and RESCUE's DONE (once every hostage is saved) only change as the game draws them when one is picked up or saved; no state has them yet. They're on the same row as the shuriken's level, so they move with it.
- **"???" difficulty.** Unlocked by finishing the game; not seen.

## For the merge

- Changelog (under the open version, What's new): "Ninja Five-O (Ninja Cop in Europe) is supported: in a stage its HUD (health and ninpo bars, score or time, the boss's bar, the shuriken's level, keys and hostages) moves to the second screen, leaving the whole stage on the game screen."
- `wiki/Supported-games.md`, table row: Ninja Five-O (Ninja Cop) | USA, Europe | The HUD, with the logo below it.
- `wiki/Supported-games.md`, section:

  ### Ninja Five-O (Ninja Cop in Europe)

  The game has no status screen of its own, so in a stage the HUD moves to the second screen: the health and ninpo bars, the score (the time in Time Trial), the boss's bar and the Launch Site's countdown at the top, with the shuriken's level, the keys you've found and the hostages still to rescue just below them, and the game's logo underneath. The game screen shows the whole stage. The STAGE card and the pause menu stay on the game screen. Everywhere else (the menus, the briefings, MISSION FAILED) the second screen shows the logo.

  Known issues: the end of a stage, a boss's defeat, a mission's end and the ending haven't been tested yet.
- README: the supported-games count goes up by one.

# Advance Wars

| | |
|---|---|
| Releases | `AWRE` USA · `AWRP` Europe (its language menu comes later). No Japanese release. |
| Profile | `src/profiles.c`: the `AWRE` entry; variant `AWRP` |
| Tests | `tests/regress/advance-wars.json` (`tools/regress.py advance-wars`) |
| Status | preliminary |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list advance-wars`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-4).

## Rules

**AW-1. Unit intel or Battle Intel.** In a battle on your turn the second screen shows, at the top in the middle, the intel of the unit under the cursor (the screen R opens: its panel, not the text box beside it) when it's one of yours (still while it moves and picks an action), or else the Battle Intel; both are brought up on the hidden copy. (Until v1.3.2: the Unit list with the unit highlighted.)

**AW-2. Minimap.** The START minimap is at the bottom left of the second screen.

**AW-3. Plate and info move.** The CO and day plate and the terrain and unit info leave the game screen for the bottom of the second screen (plate centred, info on the right on a grey box); the panels are drawn over them where they meet. While the unit intel (R) is open in the game, they stay as they were.

**AW-4. Enemy turn.** On the enemy's turn their plate shows; the panel keeps its last picture.

**AW-5. Fights.** In a fight the second screen is the dark background with the logo small and dimmed in the middle.

**AW-6. Title and menus.** On the title and menus the second screen shows the logo.

**AW-7. Conversations.** A conversation during a battle (the tutorial's, Field Training's) is copied to the top of the second screen, as wide as the screen, over the panels; it stays on the game screen too. *By hand: the only conversation state (Field Training) never gets a second-screen picture to put it on (see Known issues); check a campaign conversation on the device.*

## Known issues

Known issue: in Field Training the tutorial blocks SELECT and START, so the panels can't be brought up; the plate and info still move, and its conversations stay on the game screen only.

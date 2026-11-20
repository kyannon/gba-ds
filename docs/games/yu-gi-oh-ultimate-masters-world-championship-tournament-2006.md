# Yu-Gi-Oh! Ultimate Masters: World Championship Tournament 2006

| | |
|---|---|
| Releases | `BY6E` USA · `BY6P` Europe (Ultimate Masters Edition) |
| Dumps | `BY6E` cartridge `f968a196` (tested) · `BY6P` cartridge `cede4060` (tested) |
| Profile | `src/profiles.c`: the `BY6E` entry (um6_compose, um6_virt); variant `BY6P` (um6_eur): the same screens and addresses, its language menu first |
| Tests | `tests/regress/yu-gi-oh-ultimate-masters-world-championship-tournament-2006.json` (`tools/regress.py yu-gi-oh-ultimate-masters-world-championship-tournament-2006`) |
| Status | tried on the device (USA and Europe); merged into `dev` |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list yu-gi-oh-ultimate-masters-world-championship-tournament-2006`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

This game is a sequel on the same ideas as [Yu-Gi-Oh! The Eternal Duelist Soul](yu-gi-oh-the-eternal-duelist-soul.md), and its second screen follows that game's: on the GBA, the duel field shows a coloured card back for each card in the hand and, for the card under the cursor, a small picture with its level, attribute, ATK and DEF at the left and its name in the bar at the bottom. What the card does, its full picture and its type are on its **Card View**, a full screen the game opens from the card's commands (A on the card, then Card View, the first command). A two-screen GBA would have kept Card View up beside the field, so that's the second screen here.

*Philosophy check (`wiki/Philosophy.md`):* it's a screen the game already draws, opened on the hidden copy and captured (principles 1, 3 and 4); nothing is drawn for it, there's no cursor or prompt on it (2) and nothing needs a tap (5). One thing to know about principle 2 ("nothing shown on both"): the small picture at the left of the field and Card View's picture are the same card. It isn't the same box twice (Card View is the whole screen, with the type and the card's text, which the field doesn't show), but the card's picture is on both screens.

## Rules

**UM6-1. Card View in a duel.** In a duel, while the player moves the cursor freely on the field in their turn, in any phase (the Draw Phase before drawing, the Battle Phase and Main Phase 2 too), with it on a card they can look at (a card in their hand, a monster or a Magic or Trap card on the field: theirs, face-up or face-down, or the opponent's face-up ones; the cards the info bar at the bottom names), the second screen shows that card's Card View, the whole screen at the game screen's size, as the game draws it: the card with its picture, its name, its type and its text. It's opened on the hidden copy (A, then the first command, Card View, which A always starts on), taken as soon as the cursor has stopped on another card, and again about once a second while it stays. With Card View open in the game (from a card's commands), it's taken as it is.

**UM6-2. Deck Edit.** In Deck Edit's card grid and list (SELECT switches them), the second screen shows the Card View of the card under the cursor (A, then Detail, which A starts on). With Card View open in the game, it's taken as it is.

**UM6-3. Last card stays.** In a duel or Deck Edit, where the cursor isn't on a card that can be looked at (an empty zone, a pile on the field (the Decks, Graveyards, Extra Decks and Removed Cards), the opponent's face-down cards or hand, Deck Edit's tabs and menus), or the player isn't moving the cursor in their turn (a phase banner, a summon or an attack playing out, the card's commands or the phase menu open, a question the game asks, choosing a target), the last Card View stays; before the first in a duel or Deck Edit, the logo.

**UM6-4. Logo elsewhere.** Out of the duels and Deck Edit (the title, the main menu, the Campaign's choice of duelist, the coin toss, conversations, the Duel Results, Get Cards, the Forbidden/Limited lists, Options), the second screen shows the logo, idle, and the last Card View is forgotten. So it is from the moment a duel is won or lost (its life points are 0).

**UM6-5. Logo between turns.** From the moment the opponent's turn begins until a card can be shown again (UM6-1), the second screen shows the logo, idle, and the last Card View is forgotten: through the opponent's whole turn (a Card View the player opens in it is shown, then the logo again), and in the player's next turn until the cursor is on a card (the Draw Phase starts on the Deck). While the player's own End Phase plays out, the last Card View stays (UM6-3).

**UM6-6. Logo.** The logo is the title screen's emblem: YU-GI-OH! ULTIMATE MASTERS, World Championship Tournament 2006 (BG0), above New Game / Continue and the copyright line (700 frames from power-on, the title fully faded in).

**UM6-7. Game screen untouched.** Nothing leaves the game screen.

## Facts about the game

- The display registers tell the screens apart: the duel field `BG0CNT 0x1F08`, `BG1CNT 0x1E09` (its banners, attacks, summons, phase menu and questions too), Deck Edit `0x1C0A` `0x1D09` (its grid, list, command bar and Card View alike; DISPCNT `0x1D00` for the grid, `0x3F00` with the command bar, `0x1F00` in Card View), a duel's own Card View `0x0086` `0x4104` (DISPCNT `0x1F40`). The title, the main menu and the Campaign screens have other values. Card View, wherever it's opened, is told by fixed pixels: the dark olive of its text panel (`423918`) at four places and the black round the card.
- The life points: the player's `0x0201C4E0`, the opponent's `0x0201CD48` (from the game's cheat codes), both 8000 at the start, a word each; out of a duel one of them is 0 (the opponent's after a duel won, the player's after one lost), and both are 0 before the first duel since power-on, so both non-zero tells a duel.
- The field cursor: `0x02023340` has the zone in its low 7 bits (0 monsters, 5 Magic & Trap, `0x0B` the hand, `0x0A`, `0x0C`, `0x0E` and `0x0F` the piles round the field) and bit 7 set on the opponent's side; `0x02023341` the column (5 in the hand, mirrored on the opponent's side).
- `0x0201BCC1` has bit 7 set through the opponent's turn (the player's End Phase banner still counts as the player's), 0 in the player's. The game's own state word `0x0201FF34` isn't the turn: it's `0x2000` through both.
- The info bar at the bottom of the field names the card under the cursor in blue (`0000FF`, the player's cards) or red (`FF0000`, the opponent's), a pile's count in cyan; an empty zone, the opponent's face-down cards and hand leave it empty.
- The card's commands on the field: a row of icons above the card (or hand slot), the first the magnifier (Card View, chosen when it opens), the chosen one's name in a bubble above. The magnifier is a 16x16 sprite of tile 448, the name two 32x16 sprites of tiles 520 and 524. A on the cursor while it glides does nothing, so the hidden copy waits 12 frames first.
- B on the free cursor opens the phase menu (Main Phase 1, Battle Phase, End Phase) and A on the End Phase ends the turn after its confirmation.
- In Deck Edit, A on a card opens a command bar (lock, Detail, S), Detail first chosen; A again opens Card View.
- The USA and Europe ROMs differ in their header only.

## Known issues

- Deck Edit's tabs and menus aren't told from its grid, so on them the hidden copy tries A and A and finds no Card View: the last card stays (UM6-3).
- A pile's list in a duel (the Graveyard, Removed Cards, an Extra Deck) isn't a screen the profile knows: the last Card View stays while it's open.
- Choosing the monster to attack, or a target for a card's effect: A brings up no commands there, so the last Card View stays.
- A card that changes under a cursor that hasn't moved (the hand closing up after a summon, a card flipped face-up) shows at the next refresh, about a second later.
- Link Battle hasn't been tried (it needs a second GBA).
- The Japanese release (Yu-Gi-Oh! Duel Monsters Expert 2006, `BY6J`) isn't supported yet.

## Not covered yet

- A duel's card lists (Graveyard and the others), choosing a target, and Link Battle.

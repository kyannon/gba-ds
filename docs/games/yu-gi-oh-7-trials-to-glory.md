# Yu-Gi-Oh! 7 Trials to Glory: World Championship Tournament 2005 (Yu-Gi-Oh! Duel Monsters International 2 in Japan)

| | |
|---|---|
| Releases | `BY7E` USA · `BYIJ` Japan |
| Dumps | `BY7E` cartridge `584db6a6` (tested) · `BYIJ` cartridge `6dcfb879` (tested) |
| Profile | `src/profiles.c`: the `BY7E` entry (yw_compose_for, yw_virt_for, ywUS); the `BYIJ` variant (ywc_jpn, ywJP) |
| Tests | `tests/regress/yu-gi-oh-7-trials-to-glory.json` (`tools/regress.py yu-gi-oh-7-trials-to-glory`) |
| Status | USA and Japan: Card View on the second screen; tried on the device (the owner, Nov 21, 2026) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them. Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused.

Every game also follows the [general rules](README.md#general-rules).

Like Yu-Gi-Oh! The Eternal Duelist Soul, the game is played through its cards, and the duel field shows only a coloured card back for each card in the hand and a one-line bar for the card under the cursor. The card's picture, type and text are on its **Card View** (the game's own full screen for a card). A two-screen GBA would keep that up beside the field, so that's the second screen here (the philosophy's "a GBA with two screens", "only the game's own graphics": the page is the game's, nothing is drawn by GBA DS).

## Rules

**YWC-1. Card View in a duel.** In a duel, while the player moves the cursor freely on the field in their turn, with it on a card they can look at (a card in their hand, a monster or a Magic or Trap card on the field; the cards the info bar at the bottom names), the second screen shows that card's Card View at the game screen's size, as the game draws it. It's opened on the hidden copy (A, then Card View, the first command, which A always starts on; if the commands are already up in the game, A alone) and taken once it has held still; again about once a second while the cursor stays. With Card View already open in the game, it's taken as it is.

**YWC-2. Deck Edit and the banlist.** In Deck Edit's Deck mode and the banlist's list of cards, the second screen shows the Detail page (the same Card View) of the card under the cursor (A for the command bar, whose first command is Detail, then A).

**YWC-3. Last card stays.** Where the cursor isn't on a card that can be looked at (an empty zone, a pile, the opponent's face-down cards, the Deck in the Draw Phase), or the player isn't moving it ("End your turn?", an attack or summon playing out), the last Card View stays; before the first, the logo. Where the hidden copy's A does something other than open Card View, nothing is taken.

**YWC-4. Logo elsewhere.** Everywhere else (the title, the main menu, the town and its conversations, the Deck select list, rock-paper-scissors, the Status and Options screens), the second screen shows the logo, idle, and the last Card View is forgotten.

**YWC-5. Logo.** The logo is the title screen's: YU-GI-OH! 7 TRIALS TO GLORY WORLD CHAMPIONSHIP TOURNAMENT 2005, the flame and crystal on the hex-grid background, above Continue / New Game.

**YWC-6. Game screen untouched.** Nothing leaves the game screen.

**YWC-8. Logo through the opponent's turn.** From the moment the opponent's turn begins (after "End your turn?" is answered YES, as the game's duel state changes) until the player's next turn, the second screen shows the logo, idle, and the last Card View is forgotten: the opponent's cards the info bar names, their attacks and summons included. In the player's next turn the logo stays until the cursor is on a card (YWC-1), also in the Draw Phase before drawing, when the cursor can already be moved to the hand; the turn starts on the Deck.

**YWC-7. Japan.** Yu-Gi-Oh! Duel Monsters International 2 (`BYIJ`) follows YWC-1 to YWC-6 and YWC-8 as the USA release does (the same engine and screens; its title's lettering is the logo, with no Language Selection first). Its Card View label reads 詳細表示 and its page is the same Card View; tried from the owner's states (Deck mode, a hand card before drawing, the commands open, "End your turn?", the opponent's turn).

## Facts about the game

- The display registers tell the screens apart (BG0CNT, BG1CNT; DISPCNT where two share): the duel field `0x4007` `0x4206`, Card View / Detail `0x0087` `0x4104` (in a duel and from Deck Edit and the banlist), Deck select and Deck mode lists `0x0004` `0x0105`.
- `0x03000330` (16 bits): high byte `0x35` while the field is up and free or with a card's commands open; `0x20` with "End your turn?" up, during a fade or a conversation. Low byte `0x8E` free, `0xAE` commands open.
- Weekly banlists: Mokuba refuses a duel with a banned card or fewer than 40 cards in the active deck; resting 7 nights moves to the next week's list.
- A on a hand card shows Set and a highlighted Card View (magnifier tile 548-like sprites at the bottom right); a street duel starts by facing the duelist and pressing R.
- The info bar's name is in white; a pile's count is cyan ("Deck:35"); an empty zone shows a plain bar.
- `0x0201A460` (8 bits; Japan `0x0201A450`), the duel's state: 2 and 3 in the player's Draw Phase (the cursor on the Deck, or moved to a hand card, where Card View works as in the Main Phase; 3 stays while the drawn card comes to the hand), 5 with the cursor free in the player's Main Phase (also with a card's commands open and "End your turn?" up), 6 as the turn is ended, and **8 through the whole of the opponent's turn** (its Draw Phase, summons, attacks, the life points counting down) until the player's Draw Phase. Other values (the player's Battle Phase, an effect playing out) weren't seen; only 8 is taken as the opponent's.
- Deck mode's list cursor row: `0x02026AB0` (Japan `0x02026AA0`). Japan's Card View has BG1CNT `0x0104` (USA `0x4104`); its lists and field have the same registers as the USA ones, the card binder (Volume.1) `0x0005` `0x0106`, a screen of its own (the logo).

## Known issues

- A duel's end, the pile lists (Graveyard...), the cards won the card binder and Japan's own banlist were not tried: the logo is shown where no known screen is up.
- The opponent's turn is told by the duel's state byte reading 8 (YWC-8). If the game also uses 8 in some phase of the player's own turn (the Battle Phase wasn't tried), the logo would show there until the byte changes.
- Deck select's A, A opens Deck mode on the hidden copy, not a card: nothing is taken (YWC-3).

## Not covered yet

- The banlist (both releases), a duel's end, the card lists in a duel, the cards won, Card Trading and Password.

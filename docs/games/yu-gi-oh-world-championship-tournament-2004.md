# Yu-Gi-Oh! World Championship Tournament 2004 (Yu-Gi-Oh! Duel Monsters Expert 3 in Japan)

| | |
|---|---|
| Releases | `BYWE` USA · `BYWP` Europe · `BY3J` Japan |
| Dumps | `BYWE` cartridge `69b6f229` (tested) · `BYWP` cartridge `3c42e84c` (tested) · `BY3J` cartridge `7ab1ec04` (tested) |
| Profile | `src/profiles.c`: the `BYWE` entry (wct_compose, wct_virt); variants `BYWP` (wct_eur) and `BY3J` (wct_jpn): the same screens and addresses, their own titles |
| Tests | `tests/regress/yu-gi-oh-world-championship-tournament-2004.json` (`tools/regress.py yu-gi-oh-world-championship-tournament-2004`) |
| Status | tried on the device (all six languages and all three releases, Nov 19, 2026) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list yu-gi-oh-world-championship-tournament-2004`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why this screen

Asked what the game would have put on a second GBA screen. World Championship Tournament 2004 is the same kind of game as [The Eternal Duelist Soul](yu-gi-oh-the-eternal-duelist-soul.md) (same engine, same screens), and it has the same problem: the duel field shows a card in the hand as a coloured back and the card under the cursor as a one-line bar (name, attribute, level, ATK and DEF). What the card does, its picture and its type are on its **Card View**, a full screen the game opens from the card's commands (or a list's Detail). A two-screen GBA would have kept Card View up beside the field, as the DS Yu-Gi-Oh! games do on their top screen, so that is the second screen: the card the cursor is on, as the game draws it.

- **Nothing to act on (Philosophy, 2).** Card View has no cursor, prompt or arrow: it is a picture to read, and it is not on the game screen at the same time (the field is). It is opened on the hidden copy, so the game being played never opens it, pauses or is sent a button (Philosophy, 1 and 3).
- **Only the game's own screen (Philosophy, 4).** Nothing is drawn for it: the picture is the game's Card View, taken whole, at the game's size (Philosophy, 1).
- **When there's no card, say so plainly (Philosophy, 2).** Out of the duels, on the menus and between turns the second screen shows the logo at half strength, as idle; where the cursor isn't on a card it keeps the last one.
- **No exception.** Nothing here conflicts with the Philosophy page, so the page's list of exceptions doesn't change.

The three releases (USA, Europe, Japan: the Japanese cartridge is the same game, "Yu-Gi-Oh! Duel Monsters Expert 3") share their screens' registers, RAM addresses and sprites; they differ in the title (and its logo) and in the language the game starts with. Each cartridge has six languages, picked on its first start; the second screen takes the game's own Card View whatever the language.

## Rules

**WCT-1. Card View in a duel.** In a duel, while the player moves the cursor freely on the field, in any phase of their turn (the Draw Phase before drawing, the Battle Phase and Main Phase 2 too), with it on a card they can look at (a card in their hand, a monster or a Magic or Trap card on the field: theirs, face-up or face-down, or the opponent's face-up ones; the cards the info bar at the bottom names), the second screen shows that card's Card View, the whole screen at the game screen's size, as the game draws it: the card with its picture and name, its type and its text. It's opened on the hidden copy (A, then the first command, Card View, which A always starts on), taken as soon as the cursor has stopped on another card, and again about once a second while it stays. With Card View open in the game (from a card's commands or a list), it's taken as it is.

**WCT-2. Card lists in a duel.** In a duel's card lists (a Graveyard, Removed Cards or a Fusion Deck, the opponent's too, opened from its pile on the field), the second screen shows the Card View of the card under the list's cursor, the same way (A in the list opens a bar of commands, Detail first; A again).

**WCT-3. Deck Edit.** In Deck Edit's list of cards (and Card Trade's choice of a card, which is the same screen), the second screen shows the Card View of the card under the cursor (A, then Detail, the first command).

**WCT-4. The cards won.** After a duel is won, in the list of the cards from the chosen booster pack, the second screen shows the Card View of the card under the cursor (A in the list opens it).

**WCT-5. Last card stays.** In a duel, Deck Edit or a card list, where the cursor isn't on a card that can be looked at (an empty zone, a pile on the field (the Decks, Graveyards, Fusion Decks and Removed Cards: A opens its list or draws), the opponent's face-down cards or hand), or the player isn't moving the cursor in their turn (a phase change, an attack or a summon playing out, the card's commands open, the end-of-turn question), or the player is choosing a target (the monster to attack, a card for an effect: A there would choose it, so nothing is opened), the last Card View stays; before the first in a duel, the logo.

**WCT-6. Logo elsewhere.** Out of the duels (from the moment one is won or lost: the "YOU WIN" / "YOU LOSE" banner, the booster pack choice) and those card screens (the title, the main menu, the Campaign's choice of duelist, conversations, Options, Card Trade's own screen, Edit Deck's other screens), the second screen shows the logo, idle, and the last Card View is forgotten.

**WCT-7. Logo.** The logo is the title screen's emblem. USA and Europe: YU-GI-OH! (BG2) over WORLD CHAMPIONSHIP TOURNAMENT 2004 (BG0), above "Push Start" and the copyright line; Europe's has the kanji 遊戯王 over its YU-GI-OH!. Japan: 遊戯王 デュエルモンスターズ (BG3) over エキスパート3 and EX (BG2), above "Push Start" and the copyright lines. The title is reached on a fresh start, past the language screen (A takes the language the game offers).

**WCT-8. Game screen untouched.** Nothing leaves the game screen.

**WCT-9. Logo between turns.** From the moment the player ends their turn (the end-of-turn question answered Yes: the game runs on through Main Phase 2's banner and the End Phase) until a card can be shown again (WCT-1 to WCT-4), the second screen shows the logo, idle, and the last Card View is forgotten: through the opponent's whole turn (a Card View the player opens in it is shown, then the logo again), and in the player's next turn until the cursor is on a card (the Draw Phase starts on the Deck).

## Facts about the game

(Found with the USA release; checked the same in Europe and Japan.)

- The display registers tell the screens apart (each screen sets BG0CNT and BG1CNT once): the duel field `0x0007` `0x8106` (DISPCNT `0x1F40`: its banners, attacks and summons too), a card list `0x1F09` `0x1E80` (DISPCNT `0x1F40`: Deck Edit, Card Trade's choice, a duel's Graveyard and other piles), Card View `0x0087` `0x0104` (DISPCNT `0x0F00`, wherever it's opened from), the cards won `0x0004` `0x1E0D` (DISPCNT `0x1B40`). The booster pack choice (`0x0004` `0x0285`, DISPCNT `0x1840`), the title, the main menu (`0x0086` `0x000D`; Japan `0x0106`), the duelists, Options, Card Trade's own screen (`0x0004` `0x0106`) and the language screen are none of these.
- The same engine as The Eternal Duelist Soul's, with its data moved: the field cursor's side `0x03006594` (0 the player's), zone `0x03006598` (0 monsters, 5 Magic & Trap, `0x0B` the hand; `0x0C` Fusion Deck, `0x0D` Deck, `0x0E` Graveyard) and column `0x0300659C` (mirrored on the opponent's side); `0x03006578` bit 3 set while the player moves it freely; `0x030065A0` 6 through a duel.
- The life points: the player's `0x0201BFB4`, the opponent's `0x0201D378` (halfwords, 8000 at a duel's start). Out of a duel one of them is 0 (the opponent's after a duel won, the player's after one lost; both before the first duel), so both together tell a duel apart.
- The duel's step, `0x020154F0`: 1 while a duel is set up, 3 to 5 in the player's turn (Draw Phase 3, Standby Phase 4, then 5), 6 from the end-of-turn question answered Yes (End Phase), 7 handing over, 8 in the opponent's turn.
- The info bar at the bottom of the field names the card under the cursor in white (a monster's with its attribute, level, ATK and DEF at the right); a pile's count is cyan; an empty zone, the opponent's face-down cards and hand leave it empty.
- The card's commands on the field: a row of icons above the card's place (Card View first, the magnifier: a 16x16 sprite of tile 324), the chosen one raised, and under it its name, a 64x32 sprite of tile 376 16 pixels left of the chosen icon. With Card View chosen the pair is 16 pixels apart (with the second command chosen, 8). They're up a few frames after A. A on a card in a list brings up a bar of commands the same way (Detail first); in the cards won, Card View straight away.
- Card View is fully drawn once its fade-in ends: its fixed pixels are the dark blue (`0x0000FF`) name bar along the bottom, the darker (`0x000084`) text panel, and black between the card and the panel and at the card's left.
- Deck Edit's cursor row `0x0203123A` (and `0x02031240`); the cards won's `0x0202463A` (it wraps at the list's 5 rows).
- B in the field asks "End your turn?" (YES / NO). The cursor moves on the field with the D-pad; A on a card opens its commands, A on a pile opens its list.
- The cartridge's save (SRAM) is read once at power-on and checked (its 5,294 bytes, 16-bit words, added up to 0 with the word after them, and "YWCT2004" at `0x14A6`). The owner's save, with the whole Campaign and every card, is the `.sps` in the fixtures (`saves/BYWE.sav`); the other releases load it as it is.

## Known issues

- A duel's card lists were only tried with one card in them; if their cursor isn't Deck Edit's (`0x0203123A`), moving through a longer list changes the second screen at the next refresh (about a second) instead of at once.
- A card that changes under a cursor that hasn't moved (the hand closing up after a summon, a card flipped face-up) shows at the next refresh, about a second later.
- Choosing the monster to attack keeps the last Card View (WCT-5): the target's own Card View isn't taken as it is in The Eternal Duelist Soul.
- Link Duel hasn't been tried (it needs a second GBA).
- All six languages were tried on the device by the owner (Nov 19, 2026): Card View is recognised properly in each (the cases run English).

## Not covered yet

- The Battle Phase (the player's own monster chosen to attack, the target's choice) has no case yet.

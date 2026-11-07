# Yu-Gi-Oh! The Eternal Duelist Soul (Yu-Gi-Oh! Duel Monsters 5 Expert 1 in Japan)

| | |
|---|---|
| Releases | `AY5E` USA · `AY5J` Japan |
| Dumps | `AY5E` cartridge `dfd07a36` (tested) · `AY5J` cartridge `f3041e7e` (tested) |
| Profile | `src/profiles.c`: the `AY5E` entry (eds_compose, eds_virt); variant `AY5J` (eds_jpn: the Japanese release's table, edsJP) |
| Tests | `tests/regress/yu-gi-oh-the-eternal-duelist-soul.json` (`tools/regress.py yu-gi-oh-the-eternal-duelist-soul`) |
| Status | supported (new in v1.4.10) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list yu-gi-oh-the-eternal-duelist-soul`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The game is played through its cards, and on a GBA most of what a card does is a press or two away: the duel field shows only a coloured card back for each card in the hand and a one-line bar (name, level, ATK and DEF) for the card under the cursor; the card's picture, type and text are on its **Card View**, a full screen the game opens from the card's commands. A two-screen GBA would have kept Card View up beside the field, as the DS Yu-Gi-Oh! games later did on their top screen, so that's the second screen here.

## Rules

**EDS-1. Card View in a duel.** In a duel, while the player moves the cursor freely on the field, with it on a card they can look at (a card in their hand, a monster or a Magic or Trap card on the field: theirs, face-up or face-down, or the opponent's face-up ones; the cards the info bar at the bottom names), the second screen shows that card's Card View, the whole screen at the game screen's size, as the game draws it: the card with its picture, its name in the blue bar, its type, and its text or description. It's opened on the hidden copy (A, then the first command, Card View, which A always starts on), taken as soon as the cursor has stopped on another card, and again about once a second while it stays. With Card View open in the game (from a card's commands or a list), it's taken as it is.

**EDS-2. Card lists in a duel.** In a duel's card lists (a Graveyard, Removed Cards or a Fusion Deck, opened from its pile on the field), the second screen shows the Card View of the card under the list's cursor, the same way (A in the list opens it).

**EDS-3. Deck Edit.** In Deck Edit's lists (Trunk, Deck and Side Deck; Card Trading's choice of a card from the Trunk, which is the same screen), the second screen shows the Card View of the card under the cursor (A, then Card View, the first command).

**EDS-4. The cards won.** After a duel is won, in the list of the cards from the chosen booster pack, the second screen shows the Card View of the card under the cursor (A in the list opens it).

**EDS-5. Last card stays.** In a duel, Deck Edit or a card list, where the cursor isn't on a card that can be looked at (an empty zone, a pile on the field (the Decks, Graveyards, Fusion Decks and Removed Cards: A opens its list or draws), the opponent's face-down cards or hand), or the player isn't moving the cursor (the opponent's turn, a phase change, an attack or a summon playing out, the card's commands open, the end-of-phase menu), the last Card View stays; before the first in a duel, the logo.

**EDS-6. Logo elsewhere.** Out of the duels and those card screens (the title, the main menu, the Campaign's choice of duelist, conversations, rock-paper-scissors before a duel, the booster pack choice, Calendar, Record, Card Trading's own screen, Password, Deck Edit's other screens), the second screen shows the logo, idle, and the last Card View is forgotten.

**EDS-7. Logo.** The logo is the title screen's emblem: YU-GI-OH! THE ETERNAL DUELIST SOUL (BG0), the flame above it (BG1) and the silver disc behind it (BG2), above New Game / Continue and the copyright line (900 frames from power-on, the title fully faded in).

**EDS-8. Game screen untouched.** Nothing leaves the game screen.

**EDS-9. Japan's logo.** Duel Monsters 5 Expert 1 (the Japanese original; The Eternal Duelist Soul is its English remake) has its own title: its logo is the title's 遊戯王 デュエルモンスターズ5 エキスパート1 lettering (BG1), above PRESS START and the copyright lines, START having skipped the opening (1440 frames from power-on, START, 120 frames).

**EDS-10. Japan: the same, on its own screens.** Duel Monsters 5 Expert 1 draws its screens differently (the field with the life points at the left, a Card View with a yellow, green or pink card and a parchment panel, a Deck screen instead of Deck Edit), but the second screen follows the same rules: EDS-1 (A, then 詳細表示, the first command), EDS-2 (its pile lists open Card View with A alone), EDS-3 for its Deck screen (Kaban, Deck and Side Deck tabs; A, then 詳細 on the command bar), EDS-4, EDS-5 and EDS-6. Card View is shown at the page it opens on (the card's text); with it already open in the game, at the page the player has it on. Where the field's piles can't be told from their place, the info bar tells them: a pile's starts with 自分の or 相手の in green.

## Facts about the game

- The display registers tell the screens apart (each screen sets BG0CNT and BG1CNT once): the duel field `0x0007` `0x8106` (its banners, attacks and summons too), a duel's card list `0x0004` `0x0104`, Card View `0x0044` `0x0186` (wherever it's opened from), Deck Edit's lists `0x1800` `0x1A01` (DISPCNT `0x7F00`; Card Trading's choice of a card is the same screen), the cards won after a duel `0x0004` `0x0105` with DISPCNT `0x1F40` (Card Trading's own screen and Password have the same two, with DISPCNT `0x1300` and `0x1E00`; the title `0x0084` `0x0105`).
- `0x0201D7E0`: 7 while a duel is set up, 6 while it's under way, 4 after it (the conversation, the booster packs, the cards won, the menus, until the next), 0 before the first duel since power-on.
- The field cursor: `0x0201D7D4` its side (0 the player's), `0x0201D7D8` its zone (0 monsters, 5 Magic & Trap, `0x0B` the hand, `0x0A` and `0x0C` to `0x0F` the piles and zones round the field: Field card, Fusion Deck, Deck, Graveyard, Removed Cards), `0x0201D7DC` its column (mirrored on the opponent's side). `0x0201D7B8` has bit 3 set while the player moves it freely (not in the opponent's turn, a phase change, an animation, the commands or the phase menu).
- The info bar at the bottom of the field names the card under the cursor in white; a pile's count is in cyan; an empty zone, the opponent's face-down cards and hand leave it empty. It changes a few frames after the cursor moves.
- Card lists' cursor `0x02015274` (the cards won; the duel's lists checked with one card so far); Deck Edit's cursor `0x0201E140`, its list `0x0201F73C` (0 Trunk, 1 Deck, 2 Side Deck; L and R switch).
- A on a card: on the field and in Deck Edit, its commands (Card View first and chosen); in a list, Card View straight away (after a fade of about 24 frames). A on the cursor while it glides to a new place does nothing, so the hidden copy waits 12 frames first. Card View is fully drawn the frame its fade-in ends.
- The duel's life points: `0x020192E4` (the player's), `0x0201A048` (the opponent's); the player's hand `0x02019968` (words), the Deck from `0x02018DE0`. Setting the opponent's to 0 and changing phase ends the duel won (how the cards-won states were made).
- The Japanese release (`AY5J`): BG0CNT and BG1CNT are `0x0087` `0x8186` on the field, `0x0005` `0x0187` in Card View, `0x0004` `0x0185` with DISPCNT `0x1F40` in the Deck screen and a pile's list (its conversations have the same two with DISPCNT `0x1F04`), the cards won `0x0004` `0x0105` as in the USA. The opponent's life points `0x02007908` (cleared to 0 when a duel ends: 0 out of a duel), the player's `0x020078F0`; the hand `0x02007920` (a card number and a word of flags per card), the player's monsters from `0x020079A0`. The field cursor's place on the screen `0x02008490` (x) and `0x02008492` (y; the field scrolls), `0x02008482`, and `0x02008483` bit 7 while the player moves it freely (clear while the commands are open, in the opponent's turn, a phase banner). The Deck screen's and a pile list's cursor `0x02005F36` (its row), `0x02005F3A`; the cards won's `0x02009D02`. START ends the turn, R moves on a phase (asking first), L hides the life points. Card View has two pages (the card's text; who owns it): A turns them.
- The Japanese info bar: a monster's on two lines (LV. in green above, the number and name in white below, ATK and DEF at the right), a Magic or Trap card's on the lower line only, a pile's on the lower line, starting in pure green (自分の, 相手の) with its name in pale blue and its count in white.

## Known issues

- The duel's card lists were only tried with one card in them; if their cursor isn't `0x02015274`, moving through a longer list changes the second screen at the next refresh (about a second) instead of at once.
- A card that changes under a cursor that hasn't moved (the hand closing up after a summon, a card flipped face-up) shows at the next refresh, about a second later.
- Link Battle hasn't been tried (it needs a second GBA).
- Japan: the hand's Magic card in the regression cases was made by poking the hand (the starter deck's first draws had none); the duel's other states are from play.

## Not covered yet

- Nothing yet.

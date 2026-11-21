# Duel Masters: Kaijudo Showdown

| | |
|---|---|
| Releases | `BD6E` USA · `BD6P` Europe (English, French, German, Spanish, Italian) |
| Dumps | `BD6E` cartridge `804e5de2` (tested) · `BD6P` cartridge `52e0aa95` (tested) |
| Profile | `src/profiles.c`: the `BD6E` entry (`dmk_compose`, `dmk_virt`), with `BD6P` in its codes |
| Tests | `tests/regress/duel-masters-kaijudo-showdown.json` (`tools/regress.py duel-masters-kaijudo-showdown`) |
| Status | supported (USA and Europe); tried on the device; Europe's duels and lists checked in English and Spanish |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list duel-masters-kaijudo-showdown`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why these screens

Duel Masters is a card game, and the duel screens show cards as little as a GBA can: a hand is a row of card backs with a cost, a power and an icon on each, the card under the cursor is named in a bar, and everything else about it (its picture, race, text, rarity) is on **Card Details**, a full screen the game opens with SELECT (A in the deck list). A two-screen GBA would have kept that page up beside the duel, which is what Yu-Gi-Oh! The Eternal Duelist Soul's second screen does with its Card View, so the same rules are the baseline here (Philosophy, 1 and 3: a screen the game already draws, opened on the hidden copy and captured).

- **Nothing to act on (Philosophy, 2).** Card Details has a "B Exit" prompt at its bottom left; it's left out of the second screen, where B does nothing. Everything else on the page is information.
- **Nothing twice (Philosophy, 2).** With Card Details open on the game screen, the second screen keeps the card it already had (the one the cursor was on when SELECT was pressed) instead of showing the page again.
- **Only the game's own art (Philosophy, 4).** The page is the game's, as it draws it (a civilization's own background colour and all); nothing is drawn for it. No exception to the Philosophy page is needed.
- **When unsure, show less.** Where SELECT shows no card (an empty slot, the opponent's hidden cards, a choice that SELECT doesn't open), the last card stays rather than guessing.

Not done: the duel's other views (the field as a 3D scene, the zones of the opponent) on the second screen; the game screen already has them, and a second view of the same duel would be the same thing twice.

## Rules

**DMK-1. Card Details in a duel.** In a duel, while the player's cursor is on a card (a card in their hand, their mana zone, their battle zone or a graveyard; the creature the cursor is on in the battle zone's 3D view when choosing an attacker or a target; the opponent's face-up cards in the views the game lets the player look at), the second screen shows that card's Card Details, the whole screen at the game screen's size, as the game draws it: the card's picture, its name and race, power, cost, civilization and rarity, and its text; without the "B Exit" prompt. It's opened on the hidden copy (SELECT), taken as soon as the cursor has stopped on another card, and again about once a second while it stays.

**DMK-2. Deck Management.** In Deck Management's card list (the trunk and the deck: the list a deck's Manage deck opens), the second screen shows the Card Details of the card under the cursor (A on the hidden copy, the list's Detail command), the same way.

**DMK-3. The last card stays.** In a duel or a card list, where the cursor isn't on a card (an empty slot of the hand, the opponent's hidden cards, the list's header), or SELECT opens nothing (the opponent's turn, a banner, an attack or a summon playing out, a prompt over the field, a conversation inside the duel), or the game has Card Details open itself (the second screen already shows that card), the last Card Details stays; before the first in a duel, the logo.

**DMK-4. Logo elsewhere.** Out of the duels and the card lists (the title, the menus, conversations, the overworld, the coin flip before a duel, the Gizmo's other pages, the shops), the second screen shows the logo, idle, and the last Card Details is forgotten; so it is from the moment a duel ends (the winner's line, the prize, the way back to the overworld).

**DMK-5. Logo.** The logo is the title screen's DUEL MASTERS lettering with its KAIJUDO SHOWDOWN line (the title's BG0 and its subtitle's sprites, nothing else: the title's other sprites, the orb, the glint, the translucent rays, the language flags and PRESS START, are left out), without the characters, the sun, the menu and the copyright line.

**DMK-6. Game screen untouched.** Nothing leaves the game screen.

**DMK-7. Europe.** The European release (English, French, German, Spanish, Italian: the language is picked on the title screen) follows the same rules; its logo is the same lettering. (Its subtitle's language is the game's own.)

## Facts about the game

- The display registers (BG0CNT to BG3CNT, `0x04000008` to `0x0400000E`) tell the screens apart: a duel (the zone views, the 3D field with its banners, the Summon/Cast screen) `0x1F02 0x1E02 0x1D03 0x1C0B`, Card Details `0x1F03 0x1E02 0x1D01 0x1C02` (the same in a duel and from Deck Management, with a civilization's own colour behind it), Deck Management's deck choice and card list `0x1F08 0x1E81 0x1D0A 0x1C0B`, the overworld and its conversations `0x1F00 0x1E01 0x1D0A 0x1C03`, the coin flip `0x1F03 0x1E01 0x1D0A 0x1C03` (DISPCNT `0x3140`).
- Card Details is drawn almost entirely with sprites (the card's art, every line of text), over four BG panels in black. Its fixed pixels are those black panels: the name box, the description box and the power and cost box.
- SELECT in a duel opens Card Details from the zone views and from the field while a creature is chosen; with nothing under the cursor it opens nothing ("No card to view" is a message in the game's text). In Deck Management's list SELECT is Help and A opens the Detail page (B leaves it). The practice duel's hints are a text box the game puts over the top of the screen; they swallow input until A closes them.
- The hand's cursor (the zone views): `0x0300145A`, the slot's index.
- Every duel screen has the card's name in a bar above the bottom line (white text on a bar tinted by the zone), blank with no card.
- The tutorial duel is the Knight's practice duel at the start of the story (declining to be taught it asks again); Duel Room, the free duel, opens from the title menu once all 300 cards are collected.

## Known issues

- Only the practice duel's turns were played through to write these rules (the game's other duels use the same screens); the Duel Room and the tournaments' duels were not reached.
- A card that changes under a cursor that hasn't moved shows at the next refresh, about a second later.
- The opponent's turn, the field's 3D view and the zone views other than the hand were tried only while writing the rules; a card the cursor is on there shows when SELECT opens it, otherwise the last card stays (DMK-3).
- Link battles and trading haven't been tried (they need a second GBA); Trade's and the shops' card lists aren't followed.

## Not covered yet

- Europe in French, German and Italian: no states (English and Spanish have them).
- The tutorial duel's states have its hint boxes' timing; the Duel Room (locked until an unlock the owner hasn't hit) and the tournaments' duels.

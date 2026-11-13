# Baldur's Gate: Dark Alliance

| | |
|---|---|
| Releases | `BGDE` USA · `BGDP` Europe (English, French, German, Spanish, Italian; a language list on the title; the same addresses) |
| Dumps | `BGDE` cartridge `deedd7d9` (tested) · `BGDP` cartridge `494492dc` (tested) |
| Profile | `src/profiles.c`: the `BGDE` / `BGDP` entry (`bgd_compose`) |
| Tests | `tests/regress/baldurs-gate-dark-alliance.json` (`tools/regress.py baldurs-gate-dark-alliance`) |
| Status | supported (new in v1.5) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list baldurs-gate-dark-alliance`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

The game has no map. What it keeps in its START menu that a player checks often is the character tab (the yellow emblem): the class and level, experience and the experience for the next level, health and energy, armor, base attack and damage, and the six abilities, with the weight carried and the gold in the menu's top bar. That's what the second screen shows.

## Rules

**BGD-1. Walking about: the character tab.** Under the player's control (in a town, a sewer, a dungeon, a camp), the second screen shows START's character tab, the whole screen at the game screen's size, as the game draws it: the tab icons with the weight carried and the gold along the top, the class and level under them, the list of the character's figures, and the hero in the archway on the right. It's opened on the hidden copy at every refresh, so it stays up to date (health and energy as they go down and come back, the gold, the weight) without the player opening it. With START's menu open in the game, on any tab, the same.

**BGD-2. The whole list, in two pages.** The game shows four lines of the list at a time and scrolls through its thirteen. On the second screen they're shown as two pages, turned every 5 seconds: Experience, Next Level, Health, Energy, Armor, Base Attack and Damage; then Strength, Dexterity, Intelligence, Wisdom, Constitution and Charisma. Each page is the list with more lines in it, the game's own lines at the game's own spacing, starting just under the class and level (the game starts them a line lower, to leave room for its scroll arrow). *An exception to "the game's own screen as it is": the list is made taller so that all of it can be read without scrolling, as a player would read it by scrolling.*

**BGD-3. Nothing to press.** The list's cursor (the yellow brackets round a line), its scroll arrows and the small marker under the chosen tab icon are left out: nothing on the second screen is waiting for a button.

**BGD-4. Last one stays.** Where START opens nothing (a conversation, a scene, a door's fade, a shop, the save book), the last picture stays; before the first, the logo.

**BGD-5. Title.** On the title screen (and the European release's language list on it) and while choosing a class, the second screen shows the logo, and no captures are tried. A picture from before (after Quit Game) is dropped.

**BGD-6. Logo.** The logo is the title's lettering, "Forgotten Realms", "Baldur's Gate" and "Dark Alliance" with its red crescent, on the piece of the title's parchment map behind it (a box round the lettering): the lettering is painted into the map, with translucent edges that no outline can cleanly follow.

**BGD-7. Game screen.** Nothing leaves the game screen: the health and energy bars and the three quick slots stay where the game draws them.

**BGD-8. Quick to follow.** When the experience changes (an enemy killed, a quest's reward) or the hero goes up a level, a capture starts at once (GEN-6) instead of waiting for the next refresh.

## Not covered yet

- Only the start of Act I has been played (the Elfsong Tavern); later acts, other classes (the Wizard, the Archer, the Elven Fighter), shops, a level up and the game's own save book haven't been run.
- BGD-8 needs a moment just before the experience changes (a rat about to die in the cellar under the tavern, say): no state for it yet.
- Europe is checked in English; the other four languages share its addresses but haven't been run.

## Facts found so far (for sessions)

The same addresses in both releases.

| What | Where / value |
|---|---|
| In a game | `0x030077D8` (byte): 1 from the start of a new game (the abilities screen) on, through scenes, conversations and the menu; on the title, the European language list and the class choice (and after Quit Game) 0x14 in the USA release, 0x07 in Europe. (`0x0200004C` looked like one in single dumps, but it flips every frame) |
| Under control | `0x03002C02` (byte): 1 walking about (a conversation too: START opens nothing there), 2 a scripted scene in the world (the guards at the start), 0 from START on (the menu loading and open), a black-screen scene, the title |
| START's menu | `0x0300456A` (byte): 1 while its picture is up (~32 frames after START, black between), 0 elsewhere. (`0x03001598` is 1 in the menu only sometimes) |
| Its tab | `0x03000AA0` (byte): 0 weapons, 1 armour, 2 potions, 3 the character (yellow emblem), 4 spells and feats, 5 the book, 6 options. LEFT / RIGHT wrap round; a 2-frame press every 4 frames is taken |
| The character list | `0x03000AA1`: the first line shown (0-9; 13 lines, 4 shown); `0x03000ACB`: the cursor's line within the four shown (0-3). DOWN, a 2-frame press every 8 frames, once the tab has been up ~30 frames |
| Its sprites | everything on the tab but the background (BG2) is a sprite: each letter and digit an 8x8 one (palette 13); the lines at y 68, 83, 98, 113 (15 apart), the class and level at y 38; the cursor's brackets tile 759 (32 tall, x 0 and 152); the scroll arrows tile 784 / 785 (x 137); the tab marker tile 512 (y 20) |
| The character | `0x03000B50` experience (word), `0x03000B54` health, `0x03000B56` energy (halves), `0x03000B5E`-`B63` the six abilities (bytes), `0x03000BA9` points to spend: from the attached CodeBreaker codes (the first, `00008751 000A`+`106A3372 0007`, is the enable code) |
| Title | BG2 a bitmap: the parchment map with the lettering painted in; the lettering in the box x 32-211, y 6-96 (the crescent's tip at y 93) |

## For the merge

- Changelog line (under the open version's What's new): "New game: Baldur's Gate: Dark Alliance (USA, Europe). The game has no map, so the second screen shows the hero's sheet from START's menu, kept up to date as you play: class and level, experience and the next level's, health and energy, armor, attack and damage, and the six abilities, in two pages turned every 5 seconds, with the weight carried and the gold. The logo on the title."
- `wiki/Supported-games.md`, table: `| Baldur's Gate: Dark Alliance | `BGDE` `BGDP` | the hero's sheet (START's character tab, all of it in two pages) | |`
- `wiki/Supported-games.md`, section:

  ### Baldur's Gate: Dark Alliance

  The USA and European releases.

  The game has no map. Walking about, the second screen shows the character tab of START's menu, kept up to date as you play: your class and level, your experience and the experience for the next level, health and energy, armor, base attack and damage, and the six abilities, with the weight you carry and your gold along the top. The game shows four lines of that list at a time; the second screen shows them all, in two pages turned every 5 seconds. The cursor and the scroll arrows are left out. In conversations, scenes and shops the last picture stays; on the title and while choosing a class, the logo.

  Known issues: only the start of Act I and the Fighter have been tried.
- `wiki/Philosophy.md`, its list of exceptions: "Baldur's Gate: Dark Alliance's character list, made taller (two pages) so all of it can be read without scrolling."
- `docs/games/README.md`: its row in the table (prefix BGD, `BGDE` + 1).
- README's count of games, one more; `tools/make_dumps.py` for the Dumps row.

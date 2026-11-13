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

The game has no map. What it keeps in its START menu that a player checks often is the character tab (the yellow emblem): the class and level, experience and the experience for the next level, health and energy, armor, base attack and damage (and the six abilities), with the weight carried and the gold in the menu's top bar. That's what the second screen shows, without the abilities.

## Rules

**BGD-1. Walking about: the character tab.** Under the player's control (in a town, a sewer, a dungeon, a camp), the second screen shows START's character tab, the whole screen at the game screen's size, as the game draws it: the tab icons with the weight carried and the gold along the top, the class and level under them, the list of the character's figures, and the hero in the archway on the right. It's opened on the hidden copy at every refresh and whenever health, energy or experience change (BGD-8), so it stays up to date without the player opening it, in a fight too. With START's menu open in the game, on any tab, the same.

**BGD-2. The first seven lines, in one picture.** The game shows four lines of the list at a time and scrolls through its thirteen. The second screen shows its first seven, all at once: Experience, Next Level, Health, Energy, Armor, Base Attack and Damage, the game's own lines at the game's own spacing, starting just under the class and level (the game starts them a line lower, to leave room for its scroll arrow). The six abilities below them (Strength to Charisma) aren't shown: they change only every few levels, and the owner wants the figures that change in play. *An exception to "the game's own screen as it is": the list is made taller so those seven lines can be read without scrolling, as a player would read them by scrolling. Until Nov 12, 2026's second build: all thirteen lines, in two pages turned every 5 seconds.*

**BGD-3. Nothing to press.** The list's cursor (the yellow brackets round a line), its scroll arrows and the small marker under the chosen tab icon are left out: nothing on the second screen is waiting for a button.

**BGD-4. Last one stays.** Where START opens nothing (a conversation, a scene, a door's fade, a shop, the save book, the hero falling), the last picture stays; before the first, the logo.

**BGD-5. Title.** On the title screen (and the European release's language list on it) and while choosing a class, the second screen shows the logo, and no captures are tried. A picture from before (after Quit Game) is dropped.

**BGD-6. Logo.** The logo is the title's lettering, "Forgotten Realms", "Baldur's Gate" and "Dark Alliance" with its red crescent, on the piece of the title's parchment map behind it (a box round the lettering): the lettering is painted into the map, with translucent edges that no outline can cleanly follow.

**BGD-7. Game screen.** Nothing leaves the game screen: the health and energy bars and the three quick slots stay where the game draws them.

**BGD-8. Health as it happens.** When health, energy or experience change (a hit taken, health coming back, a potion, an enemy killed, a quest's reward, a level), a capture starts at once (GEN-6) instead of waiting for the next refresh, and takes under a second: the hidden copy opens START and goes straight to the character tab, with no button presses after START. In a fight the second screen's Health line follows the bar on the game screen within about a second. *By hand: how closely it follows on the device, where captures run beside the game (the tests run them inline).*

**BGD-9. GAME OVER: the logo.** When the hero dies, as the screen goes black for GAME OVER, the second screen drops the character tab and shows the logo; while the hero falls, the last picture stays (BGD-4).

## Not covered yet

- Only the start of Act I has been played (the Elfsong Tavern); later acts, other classes (the Wizard, the Archer, the Elven Fighter), shops, a level up and the game's own save book haven't been run.
- An experience change (a kill) and a level up haven't been run; BGD-8's case is health going down in a fight with rats.
- Europe is checked in English; the other four languages share its addresses but haven't been run.

## Facts found so far (for sessions)

The same addresses in both releases.

| What | Where / value |
|---|---|
| In a game | `0x030077D8` (byte): 1 from the start of a new game (the abilities screen) on, through scenes, conversations and the menu; on the title, the European language list and the class choice (and after Quit Game) 0x14 in the USA release, 0x07 in Europe. (`0x0200004C` looked like one in single dumps, but it flips every frame) |
| Under control | `0x03002C02` (byte): 1 walking about (a conversation too: START opens nothing there), 2 in a fight (enemies about; START works), a scripted scene in the world (the guards at the start) and while the hero falls dead, 0 from START on (the menu loading and open), a black-screen scene, GAME OVER (from the frame the screen is black, ~20 frames before the text), the title |
| START's menu | `0x0300456A` (byte): 1 while its picture is up (~32 frames after START, black between), 0 elsewhere. (`0x03001598` is 1 in the menu only sometimes) |
| Its tab | `0x03000AA0` (byte): 0 weapons, 1 armour, 2 potions, 3 the character (yellow emblem), 4 spells and feats, 5 the book, 6 options. The menu always opens on 0. LEFT / RIGHT wrap round, but presses are dropped while it settles; written (in the hidden copy) once the menu is up, the tab is drawn 2 frames later |
| The character list | `0x03000AA1`: the first line shown (0-9; 13 lines, 4 shown), written alike: drawn from it 2 frames later; `0x03000ACB`: the cursor's line within the four shown (0-3) |
| Its sprites | everything on the tab but the background (BG2) is a sprite: each letter and digit an 8x8 one (palette 13); the lines at y 68, 83, 98, 113 (15 apart), the class and level at y 38; the cursor's brackets tile 759 (32 tall, x 0 and 152); the scroll arrows tile 784 / 785 (x 137); the tab marker tile 512 (y 20) |
| The character | `0x03000B50` experience (word), `0x03000B54` health (0 from the hero's death on), `0x03000B56` energy (halves), `0x03000B5E`-`B63` the six abilities (bytes), `0x03000BA9` points to spend: from the attached CodeBreaker codes (the first, `00008751 000A`+`106A3372 0007`, is the enable code) |
| Rooms | mode 0; BG0 floors and walls, BG1 furniture and wall pieces in front, BG2 lamps, upper edges, raised floors (256x256 maps each, scrolled together); the hero, enemies, barrels, pots and the HUD are sprites |
| Camera | `0x030017D0` across, `0x030016B0` down (words, pixels). Each frame the scroll routine (0x086A1600) adds the pending deltas `0x03003538` / `0x03002BF0` (halves) to it, draws the 16-px strips newly shown (one per layer a frame: 16 px a step at most) and clears them. The follow routine (0x08690E60) sets them from the hero (stores at 0x08690FD2, 0x0869108A, 0x0869109C); another routine pushes the camera back toward the hero once he's off screen. Blanking the three stores in a copy and writing the deltas every frame pans it anywhere in the level, drawn correctly (`tools/scout/bgdpan.c`, Nov 13, 2026) |
| Level | header at the pointer in `0x030035B4`; per layer a rectangle in 16-px blocks at header + 2 + 4 × the half at header + 0x10 + 2 × layer: the tavern 73 × 77, the cellar 90 × 89. The camera stops a pixel short of width − 240 / height − 160; past the top or left it shows garbage |
| The hero | his position (fixed point, ~1/170 px) is in an entity whose address changes (`0x0201AC38` / `0x0201AC3C` in some states, nothing there in others); a write he can't walk to (through a table or wall) is undone |
| Title | BG2 a bitmap: the parchment map with the lettering painted in; the lettering in the box x 32-211, y 6-96 (the crescent's tip at y 93) |

## For the merge

- Changelog line (under the open version's What's new): "New game: Baldur's Gate: Dark Alliance (USA, Europe). The game has no map, so the second screen shows the hero's sheet from START's menu, kept up to date as you play, within a second of a hit or a potion: class and level, experience and the next level's, health and energy, armor, attack and damage, with the weight carried and the gold. The logo on the title and at GAME OVER."
- `wiki/Supported-games.md`, table: `| Baldur's Gate: Dark Alliance | `BGDE` `BGDP` | the hero's sheet (START's character tab: experience, health, energy, armor, attack, damage) | |`
- `wiki/Supported-games.md`, section:

  ### Baldur's Gate: Dark Alliance

  The USA and European releases.

  The game has no map. Walking about and in fights, the second screen shows the character tab of START's menu, kept up to date as you play (within about a second of a hit, health coming back or a potion): your class and level, your experience and the experience for the next level, health and energy, armor, base attack and damage, with the weight you carry and your gold along the top. The game shows four lines of that list at a time; the second screen shows those seven at once. The cursor and the scroll arrows are left out, and so are the six abilities. In conversations and scenes the last picture stays; on the title, while choosing a class and at GAME OVER, the logo.

  Known issues: only Act I's tavern and cellar and the Fighter have been tried.
- `wiki/Philosophy.md`, its list of exceptions: "Baldur's Gate: Dark Alliance's character list, made taller so its first seven lines can be read without scrolling."
- `docs/games/README.md`: its row in the table (prefix BGD, `BGDE` + 1).
- README's count of games, one more; `tools/make_dumps.py` for the Dumps row.

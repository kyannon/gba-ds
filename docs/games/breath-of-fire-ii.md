# Breath of Fire II (Shimei no Ko in Japan)

| | |
|---|---|
| Releases | `AB2E` USA · `AB2P` Europe · `AB2J` Japan |
| Dumps | `AB2E` cartridge `6f098da3` (tested) · `AB2P` cartridge `ff5e8e39` (tested) · `AB2J` cartridge `8a824a70` (tested) |
| Profile | `src/profiles.c`: the `AB2E` entry (`bof2_compose`), with `AB2P` and `AB2J` in its codes |
| Tests | `tests/regress/breath-of-fire-ii.json` (`tools/regress.py breath-of-fire-ii`) |
| Status | in progress (branch `game/breath-of-fire-ii`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list breath-of-fire-ii`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why these screens

Asked what the game would have put on a second GBA screen: Breath of Fire II has the same answer as its predecessor's (see [Breath of Fire](breath-of-fire.md)). The player otherwise has to open the camp menu, with SELECT, to see how the party is doing: its first page is the **party overview**, each fighter's portrait with level, HP, AP and the experience to the next level, and the zenny. It is the game's own picture, so it is opened on the hidden copy and taken as it is (Philosophy, 3 and 4); nothing is drawn for the game. Unlike the first game, this one has no map of its own that the second screen could keep up (BOF2-3).

- **One screen, everywhere.** In towns, dungeons, the overworld and wherever the camp menu opens, the party overview is what the second screen shows: it is the one screen the game has that is useful at any moment and that the player has to leave the game to see.
- **Nothing to act on (Philosophy, 2).** The camp menu's command list (ITEM, SPEL, EQIP, STAT, FORM, OPTN, SAVE, EXIT), its cursor and the small animated icon above it are only there to be pressed or to be looked at in the menu, so they are left out: the second screen shows the party window alone, as Breath of Fire's does.
- **Nothing twice (Philosophy, 2).** With the camp menu, or one of its pages, open on the game screen, the second screen shows the party overview, not the page again (BOF2-6). In a battle, the game screen already has the four fighters' HP and AP in the battle's own boxes, so the overview there repeats them, and from before the battle, not live. That conflicts with this principle; it is what the owner asked for in Breath of Fire (Nov 19, 2026), to keep the party and its experience in view, and it is kept the same way here as an exception (BOF2-4), listed below. The live alternative, moving the battle's boxes over to the second screen, is not done.
- **A small crop, no exception (Philosophy, 4 and 6).** The party window is cropped from the menu and put in the middle of the screen: "cropping to the part that matters", not a change of the game's art.

Left for later and not done: the battle's HP and AP boxes on the second screen (it needs them found as layers or sprites and tried in every kind of battle), and START's world map, which the game draws and which could be kept on the second screen on the overworld (BOF2-3).

## Rules

**BOF2-1. Logo.** The logo is the title's lettering and nothing else: BREATH OF FIRE II, with its flame in the I of FIRE and its ™ (USA, Europe); in Japan, the katakana lettering with its 使命の子 line, the small BREATH OF FIRE above it and the ®. Without the flames behind it and without the copyright lines or PRESS START BUTTON.

**BOF2-2. The title.** From power-on through the intro (the Capcom and publisher's screens, Japan's story text), the title with its demo and the file menu (GAME START, NEW GAME, COPY, ERASE, EXCHANGE), and the new game's name and settings screens, the second screen shows the logo; any picture from a game played before is dropped when the title comes up. The title is told by the display (its background layers are set up in a way no other screen uses: BG0CNT 0x1C03); the file menu and the set-up screens show no picture because SELECT opens nothing there (BOF2-5).

**BOF2-3. Walking about: the party overview.** In towns, dungeons, castles and on the overworld, whether or not a conversation is up, the second screen shows the first page of SELECT's menu as a party overview: the zenny across the top, and below it each fighter's portrait, name, level, HP, AP and experience with the experience for the next level (up to four fighters, the party), in the game's own window, without the command list at the left (ITEM to EXIT), its cursor, and the icon above it. The window is 192 pixels wide and 160 high, in the middle of the screen with black either side. It is opened on the hidden copy at every refresh (SELECT, then its fade-in), so the numbers follow the game, and shown once the window's frames are where they should be and the picture has stopped changing. The overworld is told by its own display setting (BG0CNT 0x4205) and SELECT opens the same menu there as everywhere (checked on a world map section, in all three releases). START's world map, a screen the game does have, is not shown on the second screen (see "Not covered yet").

**BOF2-4. Battles (exception).** From the battle's first frame to its last (told by the display: BG0CNT 0x020C) the second screen shows the party overview, the last one taken: the overview can't be opened in a battle. The numbers are those from before the battle. With no overview taken yet (a battle loaded from a state straight after the title) it is the logo. When the battle is over, the overview is taken again as soon as SELECT works (the victory messages and level-ups come first).

**BOF2-5. Conversations, shops, scenes, and the file menu.** Where SELECT opens nothing (a conversation that holds the buttons, a shop, an inn, a scene the story plays, the file menu and the new game's set-up screens), the last picture stays; before the first, the logo. (The capture tries SELECT twice, B between them, and gives up until the next refresh.) *By hand: the cases only see the party overview and the logo; a conversation or a shop is checked on the device.*

**BOF2-6. The camp menu open in the game.** With SELECT's menu, or one of its pages (Item, Spell, Equip, Status, Form, Options), open on the game screen, the second screen shows the party overview (BOF2-3), never the page a second time: the hidden copy goes back with B as far as it takes. It follows as soon as the screen changes (GEN-6).

**BOF2-7. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- **START's world map.** The game's world map (the whole world with the party's place marked) opens with START on the overworld, with the display set up as for walking about. It would be a fitting second screen there (a screen the game already draws, Philosophy 3 and 4), but it isn't used: the overview is kept on the overworld as everywhere. Open for the owner to decide. While it's open on the game screen the second screen keeps the overview (BOF2-5).
- **The overworld was tried on states made by editing the USA save's map** (the world map, a grass position by the shore), not reached by playing; the other world map sections and a ship, if the game has one, are untried.
- **Conversations and scenes (BOF2-5)** have no case: they're checked on the device.
- **Europe and Japan** are tested with the USA save (it loads in all three: the games' saves are the same kind), so the party is the same four in all of them; the Japanese party window has the Japanese names and menu words, in the same place.
- **Pages deeper than one B**: a page of a page of the menu (an item's target list) goes back one B at a time, so the picture may come a moment late.

## For the merge

*Nothing here is on `dev` yet; the main session takes these at the merge, then removes this heading.*

- **CHANGELOG, New games:** **Breath of Fire II** (USA, Europe, Japan: Shimei no Ko): the second screen shows the first page of SELECT's menu as a party overview: the zenny and everyone's level, HP, AP and experience, without the command list; it follows the game as you walk, in towns, dungeons and on the overworld. In a battle it keeps the overview from before the fight. On the title and the file menu it shows the logo. Known: the overview in a battle is from before it, so its HP and AP aren't live (the battle's own boxes on the game screen are); conversations and shops keep the last picture.
- **wiki/Supported-games.md, table row** (after Breath of Fire): `| Breath of Fire II (Shimei no Ko in Japan) | `AB2E` `AB2P` `AB2J` | the party overview, also in battles | |`
- **wiki/Supported-games.md, section** (after "Breath of Fire"): "## Breath of Fire II": the USA, European and Japanese (Shimei no Ko) releases. The second screen shows SELECT's menu's first page as a **party overview**: the zenny, and each fighter's level, HP, AP and experience, without the command list; it is taken from a hidden copy of the game about once a second, so your game isn't touched. In a battle it shows the overview from before the fight (the numbers on it are those from before; the battle's own boxes on the game screen are live): one of the exceptions on the Philosophy page. With the menu or one of its pages open on the game screen the second screen shows the overview, not the same thing twice. On the title and file menu: the logo. In conversations and shops the last picture stays.
- **wiki/Philosophy.md, exceptions list:** the line about Breath of Fire's party overview becomes "Breath of Fire's and Breath of Fire II's party overview kept on the second screen through a battle, taken from before it, though the battle's own boxes on the game screen have the live numbers."
- **README.md:** the supported-games count, +1. **docs/games/README.md:** the table row `| [Breath of Fire II (Shimei no Ko in Japan)](breath-of-fire-ii.md) | BOF2 | `AB2E` + 2 |`.
- **Dumps:** `tools/make_dumps.py --dat DAT` writes `src/dumps_data.h` for the new releases (the Dumps row above is already as it writes it).
- **Shared code:** none changed; the profile is `bof2_*` in `src/profiles.c`, beside Breath of Fire's.

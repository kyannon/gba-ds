# Breath of Fire II (Shimei no Ko in Japan)

| | |
|---|---|
| Releases | `AB2E` USA · `AB2P` Europe · `AB2J` Japan |
| Dumps | `AB2E` cartridge `6f098da3` (tested) · `AB2P` cartridge `ff5e8e39` (tested) · `AB2J` cartridge `8a824a70` (tested) |
| Profile | `src/profiles.c`: the `AB2E` entry (`bof2_compose`), with `AB2P` in its codes and `AB2J` as a variant of it (`bof2_jp`) |
| Tests | `tests/regress/breath-of-fire-ii.json` (`tools/regress.py breath-of-fire-ii`) |
| Toggle | `views`, `HELD`: the party overview and the world map (BOF2-3), where both exist; the overview is already kept (BOF2-4) · GEN-14 |
| Status | checked on the device in all three releases (Nov 20, 2026) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list breath-of-fire-ii`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why these screens

Asked what the game would have put on a second GBA screen: Breath of Fire II has the same answer as its predecessor's (see [Breath of Fire](breath-of-fire.md)). The player otherwise has to open the camp menu, with SELECT, to see how the party is doing: its first page is the **party overview**, each fighter's portrait with level, HP, AP and the experience to the next level, and the zenny. It is the game's own picture, so it is opened on the hidden copy and taken as it is (Philosophy, 3 and 4); nothing is drawn for the game. Like the first game, it also has a **world map**, which START opens on the overworld (the whole world, with a dark disc pulsing where the party is); that is a second screen of its own, taken the same way (BOF2-3).

- **One screen at a time, by where you are.** On the overworld the map is what the player wants (where am I, where is that town); in towns, dungeons, castles and wherever START opens no map, the party overview is what the second screen shows, and it is the screen the player has to leave the game to see. Breath of Fire does the same (BOF-3, BOF-4). The map's own marker is where the party is: information, so it stays (Philosophy, 2).
- **Nothing to act on (Philosophy, 2).** The camp menu's command list (ITEM, SPEL, EQIP, STAT, FORM, OPTN, SAVE, EXIT), its cursor and the small animated icon above it are only there to be pressed or to be looked at in the menu, so they are left out: the second screen shows the party window alone, as Breath of Fire's does.
- **Nothing twice (Philosophy, 2).** With the camp menu, or one of its pages, open on the game screen, the second screen shows the party overview, not the page again (BOF2-6). In a battle, the game screen already has the four fighters' HP and AP in the battle's own boxes, so the overview there repeats them, and from before the battle, not live. That conflicts with this principle; it is what the owner asked for in Breath of Fire (Nov 19, 2026), to keep the party and its experience in view, and it is kept the same way here as an exception (BOF2-4), listed below. The live alternative, moving the battle's boxes over to the second screen, is not done.
- **A small crop, no exception (Philosophy, 4 and 6).** The party window is cropped from the menu and put in the middle of the screen: "cropping to the part that matters", not a change of the game's art.

Left for later and not done: the battle's HP and AP boxes on the second screen (it needs them found as layers or sprites and tried in every kind of battle).

## Rules

**BOF2-1. Logo.** The logo is the title's lettering and nothing else: BREATH OF FIRE II, with its flame in the I of FIRE and its ™ (USA, Europe); in Japan, the katakana lettering with its 使命の子 line, the small BREATH OF FIRE above it and the ®. Without the flames behind it and without the copyright lines or PRESS START BUTTON.

**BOF2-2. The title.** From power-on through the intro (the Capcom and publisher's screens, Japan's story text), the title with its demo and the file menu (GAME START, NEW GAME, COPY, ERASE, EXCHANGE), and the new game's name and settings screens, the second screen shows the logo; any picture from a game played before is dropped when the title comes up. The title is told by the display (its background layers are set up in a way no other screen uses: BG0CNT 0x1C03); the file menu and the set-up screens show no picture because SELECT opens nothing there (BOF2-5).

**BOF2-3. Walking about: the world map on the overworld, the party overview elsewhere.** On the overworld (told by the display: BG0CNT 0x4205), the second screen shows the world map START opens there, as the game draws it at the game screen's size: the world with the dark disc that pulses where the party is, and nothing over it. It is opened on the hidden copy at every refresh (START, then its fade-in), once the party has finished its step, and taken once it has faded in; the disc pulses in the game, so the picture is the moment in its cycle when it is largest. Where START opens no map (towns, dungeons, castles, and the overworld too if the game doesn't open it there), the second screen shows the first page of SELECT's menu as a party overview, whether or not a conversation is up: the place's name and the zenny across the top, and below it each fighter's portrait, name, level, HP, AP and experience with the experience for the next level (up to four fighters, the party), in the game's own window, without the command list at the left (ITEM to EXIT), its cursor, and the icon above it. The overview is 192 pixels wide and 160 high, in the middle of the screen with black either side. It is opened on the hidden copy at every refresh (SELECT, then its fade-in), so the numbers follow the game, and shown once the window's frames are where they should be and the picture has stopped changing. Going into a town or out of it, the second screen changes as the game screen's display does (GEN-6).

**BOF2-4. Battles (exception).** From the battle's first frame to its last (told by the display: BG0CNT 0x020C) the second screen shows the party overview, the last one taken, whether the map or the overview was up when it began (on the overworld the overview is taken too, after the map): the overview can't be opened in a battle. The numbers are those from before the battle. With no overview taken yet (a battle loaded from a state straight after the title) it is the logo. When the battle is over, the overview is taken again as soon as SELECT works (the victory messages and level-ups come first).

**BOF2-5. Conversations, shops, scenes, and the file menu.** Where SELECT opens nothing (a conversation that holds the buttons, a shop, an inn, a scene the story plays, the file menu and the new game's set-up screens), the last picture stays; before the first, the logo. (The capture tries SELECT twice, B between them, and gives up until the next refresh.) *By hand: the cases only see the party overview and the logo; a conversation or a shop is checked on the device.*

**BOF2-6. The camp menu or the world map open in the game.** With SELECT's menu, one of its pages (Item, Spell, Equip, Status, Form, Options) or START's world map open on the game screen, the second screen shows the party overview (BOF2-3), never the page or the map a second time: the hidden copy goes back with B as far as it takes. It follows as soon as the screen changes (GEN-6).

**BOF2-7. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- **Whether the world map is always there.** The capture presses START on the overworld and shows the map if the game opens it; if not (a game where the party can't open it yet, if there is one) it shows the overview. Only the game as far as the saves go has been tried: the map opens there.
- **The overworld was tried on states made by editing the USA save's map** (the world map, a grass position by the shore), not reached by playing; the other world map sections and a ship, if the game has one, are untried.
- **Conversations and scenes (BOF2-5)** have no case: they're checked on the device.
- **Europe and Japan** are tested with the USA save (it loads in all three: the games' saves are the same kind), so the party is the same four in all of them; the Japanese party window has the Japanese names and menu words, in the same place.
- **Pages deeper than one B**: a page of a page of the menu (an item's target list) goes back one B at a time, so the picture may come a moment late.

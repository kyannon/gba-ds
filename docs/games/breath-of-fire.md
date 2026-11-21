# Breath of Fire (Ryuu no Senshi in Japan)

| | |
|---|---|
| Releases | `ABFE` USA · `ABFP` Europe · `ABFJ` Japan · `ABFX` Europe (English, French, German) |
| Dumps | `ABFE` cartridge `f06422a8` (tested) · `ABFP` cartridge `a1c3165d` (tested) · `ABFJ` cartridge `66e96b35` (tested) · `ABFX` cartridge `d3660549` (tested) |
| Profile | `src/profiles.c`: the `ABFE` entry (`bof_compose`), with `ABFP`, `ABFJ` and `ABFX` in its codes |
| Tests | `tests/regress/breath-of-fire.json` (`tools/regress.py breath-of-fire`) |
| Status | tried on the device (the owner, Nov 21, 2026) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list breath-of-fire`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-13).

## Why these screens

Asked what the game would have put on a second GBA screen: Breath of Fire has two screens of its own that the player otherwise has to open and leave again. START, once the party has the Map, opens the **world map** (the part of the world around the party, with a marker where it is), and SELECT's menu opens on a **party overview**: the place, the gold, and the level, HP and AP of all eight characters, the four in the party and the four waiting. Both are the game's own pictures, so they are opened on the hidden copy and taken as they are (Philosophy, 3 and 4); nothing is drawn for the game.

- **One screen at a time, by where you are.** On the overworld the map is what the player wants (where am I, where is that town); in towns, caves and everywhere the map doesn't open, the party overview is. Final Fantasy IV Advance does the same (its Sight map on the overworld, the party screen elsewhere).
- **Nothing to act on (Philosophy, 2).** The menu's command list (ITEM to END) and its cursor are only there to be pressed, so they're left out: the second screen shows the party window alone. The map's marker is where you are, which is information, so it stays. The map has no cursor or prompts of its own.
- **Nothing twice (Philosophy, 2).** If the map is open on the game screen, the second screen shows the party overview, not the same map again (BOF-7). In a battle the game screen already has the four fighters' HP and AP in the battle's own boxes, so the party overview there repeats them, and from before the battle, not live. That conflicts with this principle; the owner asked for it (Nov 19, 2026) to keep the party in view, so it is an exception (BOF-5), listed below. The live alternative, moving the battle's boxes over to the second screen, is not done.
- **A small crop, no exception (Philosophy, 4 and 6).** The party window is cropped from the menu and put in the middle of the screen. That is "cropping to the part that matters", not a change of the game's art, and needs no entry in the page of exceptions.

Left for later and not done: the battle's HP and AP boxes moved to the second screen (as Mario & Luigi's battle boxes are), which would free the game screen for the fight. It needs the boxes found as layers or sprites and tried in every kind of battle; a step of its own.

## Rules

**BOF-1. Logo.** The logo is the title's BREATH OF FIRE lettering with its flame in the I of FIRE and its ™ (BG3 of the title screen), without PRESS START and the copyright lines, and without the blue flames behind it. The same in every release (Europe's English, French and German one has one copyright line).

**BOF-2. The title.** From power-on through the intro, the title and the file menu (GAME START, NEW GAME, COPY...), the second screen shows the logo, any picture from a game played before is dropped, and no captures are tried. Told by the display: the intro and the title use only BG1 and the sprites (display register 0x1200); the file menu shows no picture because SELECT opens nothing there (BOF-6).

**BOF-3. The overworld: the world map.** On the overworld, the second screen shows the map START opens there, as the game draws it at the game screen's size: the part of the world around the party, with the marker (a cyan dot) where it is, and nothing over it. It is opened on the hidden copy at every refresh (START, then its fade-in) and taken once it has stopped changing, so the marker follows the party. It only opens on the overworld, and only once the party has the Map (a vital item); anywhere else, and before that, the party overview (BOF-4) is shown instead.

**BOF-4. Elsewhere: the party overview.** In towns, caves, castles and wherever START opens no map, the second screen shows the first page of SELECT's menu as a party overview: the place's name and the party's gold across the top (the place's name is empty on the overworld), and below them each character's portrait, level, HP and AP, eight in all, in the game's own window, without the command list at the left (ITEM, SPEL, EQIP, STAT, SWCH, CHNG, OPTN, SAVE, END) and its cursor. The window is 193 pixels wide and 160 high, in the middle of the screen with black either side. It is opened on the hidden copy at every refresh (SELECT, then its fade-in), so the HP and the gold follow the game, and shown once the window's frames are where they should be and the picture has stopped changing.

**BOF-5. Battles (exception).** From the battle's first frame to its last (told by the display's window 0 being on, display register 0x3F00) the second screen shows the party overview, whether the map or the overview was up when it began: the overview can't be opened in a battle, so it is the last one taken, which the map's capture takes too (it closes the map and opens SELECT's menu once more). The numbers are those from before the battle. With no overview taken yet (a battle loaded from a state straight after the title) it is the logo. When the battle is over, the map or the party overview is taken again as soon as SELECT works (the victory messages and level-ups come first).

**BOF-6. Conversations, shops and scenes.** Where neither START nor SELECT opens anything (a conversation, a shop, an inn, the save prompt, a scene the story plays, the file menu), the last picture stays; before the first, the logo. (The capture tries SELECT twice, B between them, and gives up until the next refresh.) *By hand: the cases only see the party overview and the logo; a conversation or a shop is checked on the device.*

**BOF-7. A menu or the map open in the game.** With SELECT's menu, one of its pages (Item, Spell, Equip, Status, Switch, Change) or the world map open on the game screen, the second screen shows the party overview (BOF-4), never the map a second time: the hidden copy goes back with B as far as it takes. It follows as soon as the screen changes (GEN-6).

**BOF-8. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- **Conversations and scenes (BOF-6)** have no case: they're checked on the device.
- **Cutscenes that share the title's display mode** (0x1200) may show the logo until they end (BOF-2); the intro itself is meant to.
- **Europe's English, French and German release (ABFX)** is tested at its title only (its saves are SRAM, which the flash saves we have don't fit): its field, map and battle use the same code as the other three but have no states yet.
- **Europe and Japan** have a state in a town, on the overworld and in a battle; Europe's overworld state is from before the party has the Map (BOF-3's fall-back), Japan's has it. Their saves have one character in the party, so the overview shows one portrait; the USA's has eight.
- **Japan's logo** has a small green mark under the lettering, which is part of the title's own sprites.
- **Pages deeper than one B**: a sub-page of a sub-page of the menu (an item's target list) goes back one B at a time, so the picture may come a moment late.

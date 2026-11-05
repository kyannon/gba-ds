# Harvest Moon: Friends of Mineral Town (Bokujou Monogatari: Mineral Town no Nakama-tachi in Japan)

| | |
|---|---|
| Releases | `A4NE` USA · `A4NP` Europe (English) · `A4NJ` Japan (each tried walking about, from a new game and from a battery save) |
| Dumps | `A4NE` cartridge `8e923168` (tested) · `A4NP` cartridge `f50f82ad` (tested) · `A4NJ` cartridge `09185657` (tested) |
| Profile | `src/profiles.c`: the `A4NE` entry (hm_compose; `A4NP` the same); variant `A4NJ` (its own logo, hm_jpn) |
| Tests | `tests/regress/harvest-moon-friends-of-mineral-town.json` (`tools/regress.py harvest-moon-friends-of-mineral-town`) |
| Status | in testing (branch `game/harvest-moon-friends-of-mineral-town`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list harvest-moon-friends-of-mineral-town`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

Why the town map: the game has three screens of its own that a second screen could hold, the town map (L+START), the farm map (L+SELECT) and the status pages (SELECT: the farm's money and animals, the Harvest Sprites, the earnings, the tools' levels). The town map is the one that follows the player everywhere: where they are, where the dog is, what the place they're in has, with the time, date and weather beside it. It's the game's map, the thing GBA DS puts on its second screen first. The farm map (only part of the farm at a time, and only on the farm) and the status pages (they change once a day, when the produce is sold) were left for later.

## Rules

**HM-1. Town map.** Walking about (on the farm, in town, in the buildings: wherever L+START opens the town map), the second screen shows the town map, the game's whole screen as it draws it, at the game screen's size: the valley with the player's head where the player is and the dog's head where the dog is, the clock box (season, day, time and today's weather) and the text box under the map. It's opened on the hidden copy at every map refresh (about once a second), so the head, the dog and the clock keep up as the player goes about, without the player opening it. With the town map already open in the game, the same, taken from it.

**HM-2. Where you are.** The text box under the map describes the part of the valley the player is in (AAA Farm: This is your house; North Side of Mineral Town: Lots of shops and homes...): on the hidden copy the map's pointing glove is moved onto the player's head before the picture is taken, and the glove itself is left out of the picture (the map underneath showing).

**HM-3. Last one stays.** Where the town map can't be opened (a conversation, an event, the START menu, the status pages, the farm map, the Diary, a shop, the night's black screen after going to bed), the last town map stays; before the first, the logo.

**HM-4. Title.** On the title screen, the opening before it and the naming screens of a new game (the game's mode 0x78), the second screen shows the logo, any town map is forgotten, and no captures are tried there.

**HM-5. Logo.** The logo is the title screen's wooden sign (sprites): HARVEST MOON with its Friends of Mineral Town board and posts, without the farmer on his horse at its left, the start prompt and the villagers. In Japan, 牧場物語 with its furigana and the ミネラルタウンのなかまたち banner. Frame 1000 from power-on.

**HM-6. Game screen untouched.** Nothing leaves the game screen.

## Not covered yet

- The mine, the Goddess spring, the beach and the other places beyond the farm, the road and the town's north side: the test states don't reach them. If L+START opens the town map there, HM-1 holds; if not, HM-3.
- Festivals (the first, the Spring Thanksgiving Festival, on Spring 14), the horse once owned (its head on the map), rain, snow and the other seasons' clocks.
- The German release (`A4ND`, No-Intro's `c11757d7`): not handled, untried (no ROM to test).
- *Harvest Moon: More Friends of Mineral Town* (`BFGE`, the girl's version) is another game, not handled.

## Facts about the game

For sessions working on the profile (every release's addresses are the same):

- `0x02000012` (byte) the game's mode: 0 at power-on, 0x78 (120) from the opening through the title and a new game's naming screens, 0x37 walking about (the town map, the menus and going to bed too), other values in scenes (0x64 the opening's flashback, 0x3B Thomas's tutorial).
- The game's own data (time, weather, money, the area number) sits at the addresses in Data Crystal's RAM map (money `0x02004080`, the time `0x020025EA`: hour in its low 5 bits, minutes in the next 6) until a battery save is loaded; from then on 0x2834 higher (money `0x020068B4`). The area number is `0x02005CF8` (the farm 2, the road north of it 7, the house 29) in a new game. The profile reads none of them: the town map is taken every refresh anyway, and the player's head moves within an area too.
- The town map: L held, then START. It comes in through a mosaic, fully up about 75 frames later, with the display at 0x7F40 (window 1 on) and BG3CNT 0x1E40 (in play 0x3F40 and 0x1C43). BG3 the valley, BG2 the buildings, BG0 the text box, sprites the heads, the glove, the clock box and the text. The glove is tile 200 in palette 5 (16x16, pointing right, its fingertip 15 across and 5 down); the player's head tile 204 in palette 6, the dog's tile 208 in palette 7. The glove starts at the same place every time (South Side of Mineral Town) and moves 2 pixels a frame while the D-pad is held; the text box describes what's under its fingertip. The heads bob a pixel; the glove sways.
- The farm map (L, then SELECT) is the farm at half size, scrolling with the D-pad. SELECT alone: the status pages (Property: the farm's money, the player, the dog, the animals; Harvest Sprites; Earnings Report; Tool Level List; LEFT / RIGHT turns them). START: the toolbar along the game screen's bottom (Diary, Rucksack...). The Diary saves and has options: Buttons 1 / 2, Clock (the game screen's clock box), Face, Name.
- Time: a game minute every 20 frames; the clock box shows ten-minute steps. It stops in conversations and events.
- The title comes about 900 frames from power-on (Natsume / licence screens, the villagers' opening). Its sign is sprites: HARVEST MOON rows 0-95 from x 42, the farmer on his horse x 21-45 rows 65-88; Japan's sign x 5-234, rows 0-112, the はじめから prompt from row 113.
- A new game with no save: START on the title, then the naming screens (UP from the letters jumps to End): the player, the birthday, the farm, the dog, then a check (A: Yes). Then about 120 A presses through Thomas's welcome and the flashback to the house on Spring 2 at 6:00. Leaving the house starts Thomas's talk (DOWN, A at "Do you want to hear even more instruction?" skips the tutorial) and Zack's.
- A battery save from the USA release loads in the European and Japanese ones too (the Diary, Load, Data 1).
- How the test states were made: a new game played from power-on in the harness (USA); a USA battery save, written by the app from the Diary, loaded in each release by the app (its quick state), then Mary's introduction on the road north of the farm played through in the harness.

## For the merge

Changelog (v1.4.9's New games):

- **Harvest Moon: Friends of Mineral Town** (USA, Europe and Japan): walking about, the second screen is the town map (the one L+START opens), updated about once a second: your head where you are, your dog's, the clock with the date and weather, and the text under the map saying which part of the valley you're in. In conversations, events and the game's menus it keeps its last picture; on the title, the logo, the title's wooden sign. Tried on the farm, the road north of it and in town; the mine and festivals haven't been tried yet.

Wiki (Supported-games.md), the table:

| Harvest Moon: Friends of Mineral Town (Bokujou Monogatari: Mineral Town no Nakama-tachi in Japan) | `A4NE` `A4NP` `A4NJ` | the town map, with where you are |

And its section:

## Harvest Moon: Friends of Mineral Town

The USA, European and Japanese releases, tried on the farm and in town.

- **Walking about,** the second screen is the **town map** (the one L+START opens), as the game draws it: your head where you are, your dog's head where your dog is, the clock with the season, day, time and weather, and the text under the map describing the part of the valley you're in. It's opened in the background in a silent copy of the game about once a second, so it follows you without you opening it; your game is never paused. The map's pointing glove is left out.
- In conversations, events and the game's own menus, and overnight, it keeps its last picture; on the title and the naming screens, the logo.

And docs/games/README.md's table: `| [Harvest Moon: Friends of Mineral Town (Bokujou Monogatari: Mineral Town no Nakama-tachi in Japan)](harvest-moon-friends-of-mineral-town.md) | HM | `A4NE` + 2 |`.

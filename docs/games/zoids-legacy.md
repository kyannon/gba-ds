# Zoids: Legacy (Zoids Saga II in Japan)

| | |
|---|---|
| Releases | `AZ2E` USA · `AZ2J` Japan |
| Profile | `src/profiles.c`: the `AZ2E` entry (zl_virt); variant `AZ2J` (its RAM and routines elsewhere: zlRegions) |
| Tests | `tests/regress/zoids-legacy.json` (`tools/regress.py zoids-legacy`) |
| Status | supported |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list zoids-legacy`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-7).

## Rules

**ZL-1. World map.** Walking about, in a town, out on the world or on a dungeon's floor (where SELECT or R opens the map), the second screen shows the game's map screen (SELECT), the whole screen at the game screen's size, as the game draws it: the world map with the spinning marker where Zeru's carrier is, the list of towns visited beside it (in a dungeon, the dungeon's name under them), and the box under them, empty: its line ("Select town/verify location", a prompt for the game's own map) is left out, the box's light green there. The list's cursor (the triangle by the first town) is left out too. (In the first test builds the line stayed.) The marker spins as it does in the game (a short loop of the map's frames). It's opened on the hidden copy at every map refresh (about every 1.5 seconds) and as soon as Zeru goes into a town or a dungeon or back out onto the world, so it keeps up without the player opening it.

**ZL-2. Last one stays.** Where the map can't be opened on the hidden copy (a conversation or a scene, the pause menu or one of its screens, a shop, the map already open in the game), the last map stays; before the first, the logo.

**ZL-3. Battles playing out.** In a battle (the story's and random ones alike), from its start (the enemies identified, the opening menu: Start a Battle, Form a Team, Create Commands) while it plays out (the Zoids' attacks, the enemies' turns, its messages, the money and experience after it), the second screen shows the logo, idle; the pictures from before go: the map is forgotten as the battle starts, and a Zoid's Status as its turn plays out. After the battle a fresh map is taken. (Until the third test build, the whole battle showed the logo, ZL-8 being new.)

**ZL-4. Title.** On the title screen and its menu (START A GAME, RESUME A GAME, OPTIONS) the second screen shows the logo, any map is forgotten, and no captures are tried. Name entry and the opening scenes don't open the map either, so the logo stays (ZL-2).

**ZL-5. Logo.** The logo is the ZOIDS LEGACY lettering from the title screen (sprites over the title's picture, frame 1300 from power-on; the copyright line and PRESS START below it left out). In Japan it's the ZOIDS SAGA II lettering with ゾイドサーガII under it, taken the same way (frame 1300 there too).

**ZL-6. Game screen untouched.** Nothing leaves the game screen.

**ZL-7. The past.** In the Past Overworld and the Empire Capital as it is there (from leaving Seez Town until Riv Town), where the game has no map, the second screen shows the game's own message for it, as SELECT brings it up there: its text box alone ("World Map cannot be seen under the influence of time & space warp."; in Japan 時空のひずみの影響によりワールドマップを見ることができません), whole and fully written out, without the blinking arrow at its end, in the middle of the second screen from top to bottom (the box's own 64 rows, the game screen's width) on dark grey. The map from before is forgotten, as it shows the present-day world. Back in the present, a fresh map is taken as usual. (In the second test build the past showed the logo.)

**ZL-8. A Zoid choosing.** In a battle, while the player chooses what a Zoid does (its menu: Attack, Waiting, Item; the weapons and targets after Attack; the turn's own menu: Start a Combat, Commands, Retreat), the second screen shows that Zoid's Status, as the game draws it when R is pressed there: its HP, EP and GEP, SP, MM and IV, DF, AM and DCP, SR, the Zoid's and the pilot's names, and the battle beside them with the Zoid and its HP and EP bars. At the turn's own menu, R shows the first Zoid in the party. It's opened on the hidden copy (backing out of the weapons and targets first) as soon as a Zoid's menu comes up, and again at every refresh while it's up; with the game's own Status open, it's taken as it is. Back at a menu from the game's own Status (B), it's the Status again, never a copy of the menu (the third test build showed the menu there). If R doesn't bring a Status up, nothing is taken and the logo stays.

## Facts about the game

- A byte at `0x02030664` (USA; `0x0202FDB0` Japan, by romxref) is the game's mode: 0 walking about (and on the title), 1 in a scene or conversation run by the story, 2 in a battle. The byte 3 after it is the place: `0x0C` Cebu Town, `0x1F` the world, 0 in a battle.
- The word at `0x03005B54` (both releases) is the routine running the screen in front in one of the game's task slots: the map screen `0x080ED8F8` (Japan `0x080BAAE0`), the pause menu `0x08105A20` (`0x080EADD8`), the title `0x08102ED0` (`0x080E8330`), a story conversation `0x08359850`. It isn't used: after RESUME A GAME that slot keeps the title's routine while Zeru walks about. The title is told instead by the mode and the place both 0 (walking about the place is set; name entry and the opening are scenes, mode 1), and the map screen by its pixels.
- SELECT and R both open the map, in towns too (both guides say so for the world; the map is the same screen in a town). The map screen is drawn at once; only the marker (two sprites, tiles 936-945, around 93-104, 112-120 on the first map) and the list's cursor (one sprite at 128, 8, tiles 1012-1022) move: both spin, their tiles stepping every two frames. The marker's spin doesn't repeat within 240 frames; the cursor's every 24.
- The map Zeru is on is a number at `0x0202ECF4` (USA; `0x0202E4C0` Japan, by romxref; a halfword): 0 the world, 4 Cebu Town, 64 the Past Overworld, 58 to 62 the Empire Capital in the past and its buildings (37 to 43 the same town in the present). Each has a 32-byte record in a table in the ROM (`0x087C4434` USA, `0x087B13C8` Japan; the same 196 maps in the same order): its size in tiles, then at +0x1C 1 where Zeru drives the carrier (the worlds, dungeons), +0x1D the town's number, +0x1E the place byte above (0x1F a world, 0x0A to 0x0C a town's streets, 0x0D 0x0E its buildings, 0x14 0x15 a dungeon's floors).
- The game's own save (32 KB) keeps where Zeru is in a block from `0x6B44` ("ZOIDSSAGA2") to `0x6D1B`: the map number at `0x6B4F`, Zeru's position at `0x6B54` and the carrier's at `0x6B5C` (pixels, 32-bit words); its bytes add up to 0xD6, `0x6D1B` making up the sum. Changing the map and the positions there and choosing RESUME A GAME puts Zeru on any map, which is how the states for the past (maps 64 and 58) and the dungeons (174, the E. Sea Ruins' first floor) were made; the story doesn't stop it.
- Zeru's position in a town is a pair of 24.8 fixed-point words at `0x02032758`; the people in it follow in records of 0x48 bytes below it (their spawn points at +8). Moving them next to Zeru and pressing A is how the first states were reached (the town's exit opens once he's talked to enough people).
- Only the story's battles set the mode byte to 2; in a random battle it stays 0. What's true of every battle: the word at `0x02032E8C` (USA; `0x02032584` Japan) holds `0x03005B74` (a list of the battle's task slots, set up as it starts) through the whole battle, the story's and random ones alike, and never walking about, on the map, in the menu or on the title (checked in 93 battle moments and 38 others).
- The menu window up is described at `0x03005E00` (both releases): its x and y in two bytes, then `00 06`, its width and height (16 bits each). A Zoid's menu (Attack, Waiting, Item) is `00 11`, 10 by 8 (Japan 8 by 8); its weapons `42 1A` and `42 18`; Decide/Cancel `16 19`; the turn's menu `40 11` (the story's battles) or `00 11` 16 by 8 (random ones); the Status (R) `1E 13`, 15 by 4, in both releases; the opening menu `00 11` 17 by 8 (the story's), `C0 10` (random ones; with Retreat), in Japan `00 11` 8 by 8 like a Zoid's menu; the byte at `0x0300567E` (both releases) is 1 at every opening menu and 0 at every Zoid's menu and turn menu, which tells them apart. Messages are `00 14`, `00 1C`, `0C 1C`, `E0 03`.
- That window is the last one opened, not the one up: B from the Status back to a Zoid's menu or the turn's menu leaves it `1E 13`, and B from Decide/Cancel back to the weapons leaves it `16 19`. The Status is up when it's `1E 13` and BG0 is scrolled to the Status's rows (BG0VOFS `0x181`, both releases; `0x68` at the menus), which changes the frame the Status opens and closes.
- R on a Zoid's menu or the turn's menu opens a Status (the turn's: the first Zoid's) within 10 frames; R on the opening menu does nothing. B backs out of Decide/Cancel and the weapons to the Zoid's menu; R does nothing there, and B on a Zoid's menu does nothing either.
- Each Zoid chooses and then acts, the next Zoid's menu coming after the first one's attack has played out; so a Status taken as each menu comes up follows them without knowing which Zoid it is.
- The walking counter at `0x020324A8` (frames walked) isn't what starts random battles; the encounter check at `0x0809E730` reads the map's encounter zone. A random battle came up after about 2000 frames of walking north of Cebu Town.
- The past's message (SELECT there) is a text box on BG3, rows 96 to 159, the whole width, in both releases; it's written out over about 120 frames, then a blinking arrow (a sprite, tile 1001) comes up at its end.
- The release's ROMs: USA `220a6f1b` (MD5 prefix), Japan `f7bdd0eb`.

## Known issues

- The Miletos Fortress, in the past, is only seen in a scene and a battle; it hasn't been told apart from the other maps (its battle shows the logo, ZL-3).
- Battles show the logo while they play out (ZL-3).
- From a random battle's turn menu (Start a Combat, Commands, Retreat), the Status has a light strip at its middle, beside the battle: the turn menu's window, left behind on its layer by the game; the game screen shows the same.

## Not covered yet

- Nothing yet.

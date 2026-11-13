# Final Fantasy I & II: Dawn of Souls (Final Fantasy I, II Advance in Japan)

| | |
|---|---|
| Releases | `BFFE` USA · `BFFP` Europe (English, French, German, Spanish, Italian) · `BFFJ` Japan |
| Dumps | `BFFE` cartridge `1b39cdab` (tested) · `BFFP` cartridge `7c9854c6` (tested) · `BFFJ` Rev 1 `0f3bd723` (tested), Rev 1, Virtual Console `2c58c9c1` |
| Profile | `src/profiles.c`: the `BFFE` entry (`ff_compose`) |
| Tests | `tests/regress/final-fantasy-1-2-dawn-of-souls.json` (`tools/regress.py final-fantasy-1-2-dawn-of-souls`) |
| Status | in progress (branch `game/final-fantasy-1-2-dawn-of-souls`) |

What GBA DS does with this game. Each rule is a promise the profile keeps; the regression tests check them (the cases citing a rule are listed by `tools/regress.py --list final-fantasy-1-2-dawn-of-souls`). Change a rule here first when the behaviour is meant to change, then the profile, then the tests and goldens. Rule numbers are never reused: a dropped rule stays, marked *retired*.

Every game also follows the [general rules](README.md#general-rules) (GEN-1 to GEN-11).

The cartridge holds two games, Final Fantasy and Final Fantasy II (with its extra chapter, Soul of Rebirth), chosen on its Title Select screen, and a Bestiary for each. Each game has its own memory layout; the rules say which game they're for.

## Why these screens

Asked what the games would have put on a second GBA screen: the two things a player keeps opening while walking about are the world map (hold B and press SELECT) and START's menu, which shows the whole party at once (level, HP and MP), the gil and play time, the crystals (Final Fantasy) and where the party is. Both are the game's own screens, opened on the hidden copy and taken as they are, with the cursor left out. In a battle the party's HP and MP are on the game screen already, rolling as they change, so the second screen shows the logo there rather than numbers that are out of date.

## Rules

**FF-1. Logo.** The logo is the cartridge's title: its FINAL FANTASY I & II lettering (BG1) and the DAWN OF SOULS bar under it (BG3), without the crystal, the two heroes' pictures behind it, PRESS START or the copyright lines. In the Japanese release, FINAL FANTASY I・II with the ADVANCE bar and ファイナルファンタジー under it (BG1 to BG3). Europe's title comes after its language screen (English, A). The same logo for both games.

**FF-2. Outside the games.** On the cartridge's title, its Title Select screen and the Bestiary, the second screen shows the logo, any pictures from a game played before are dropped, and no captures are tried there. Told by neither game's memory being set up: each release keeps a pointer to each game's memory, set only while that game runs (USA 0x02000584 for Final Fantasy, 0x0200099C for Final Fantasy II; Europe 0x020004D0 and 0x0200080C; Japan 0x020004F8 and 0x02000904).

**FF-3. A game's own title and new game.** On a game's own title, its New Game, Load and naming screens and its opening scenes, nothing can be opened yet: the second screen keeps the logo until the first picture is taken (FF-4, FF-5). (Captures are tried there and fail: neither the map's nor the menu's windows come up.)

**FF-4. Final Fantasy: the world map.** Walking about on the world map (Final Fantasy's map number, the first byte of its memory, is 0), the second screen shows the world map (on the hidden copy: hold B, then SELECT): the parchment with the whole world on it, as the game draws it at the left of its map screen, with the party's marker (a pulsing blue sparkle) shown at its largest, without the hand cursor, the help window ("Select a location to examine."), the command window (Enlarged Map, Current Position) or the button window (A Confirm, B Exit, R + pad Scroll), all of them prompts. The parchment, as much of it as the game shows under its help window (128 by 132 pixels, at the game's own size), is in the middle of the second screen, black around it. It's taken again at every refresh, so the marker follows the party. On foot or aboard (the airship too: its marker where it flies). On the world map the second screen only ever shows the map: if the map doesn't open on the hidden copy (its buttons pressed mid-step can go unseen; they're tried up to three times), the last picture stays, never the menu.

**FF-5. Final Fantasy: the menu elsewhere.** Walking about anywhere else (towns, castles, dungeons; the game opens its map in some dungeons too, at least late in the game, but there the party's HP and MP matter more than where the dungeon is), the second screen shows START's menu as the game draws it: the party (portrait, name, level, class, HP and MP), the command list, the gil and play time, the crystals and where the party is, but without the hand cursor at the command list (a prompt). It's taken again at every refresh, and at once on entering another map.

**FF-6. A menu or the map open in the game.** With START's menu, one of its screens or the world map open in the game, the second screen shows what it would walking about there (FF-4, FF-5): the hidden copy closes them first (B, as many times as it takes for START to bring the menu up, at most three).

**FF-7. Last picture stays.** In a conversation, a shop, an inn, a scene the story plays while the party is on the map, and between maps, where neither the map nor the menu opens, the last picture stays; before the first, the logo. START's menu is told by its windows' frames (the command window's lower edge and the party window's left one, the same in every release), the world map screen by the parchment's and the button window's.

**FF-8. Battles.** In a battle, from its first frame to the end of its results, the second screen shows the logo and the pictures from walking about are dropped (they'd show HP and MP from before the battle); after it, the map or the menu is taken again at once. Told by 0x02000000 being 1 (0 walking about and in the menus), in either game and every release.

**FF-9. Game screen untouched.** Nothing leaves the game screen.

**FF-10. Final Fantasy II: the menu.** Walking about in Final Fantasy II and Soul of Rebirth, anywhere but the world map (FF-11), the second screen shows START's menu as the game draws it: the party (portrait, name, HP and MP), the command list, where the party is, and the time, steps and gil, without the hand cursor. It's taken again at every refresh. FF-6 to FF-9 hold as in Final Fantasy.

**FF-11. Final Fantasy II: the world map.** Walking about on Final Fantasy II's world map (told by the byte 0x11C4 past its memory's base being 4: 0x020030E4 in the USA release), the second screen shows its world map as FF-4 shows Final Fantasy's: the map at the left of the map screen (BG1, its sea's edges on BG3), with the party's marker (a white circle) and the game's own marks for towns and vehicles, without the hand cursor, the help window, the command window (Enlarged Map, Current Position, the vehicles and towns) or the button window; as much of it as the game shows under its help window (128 by 136 pixels), in the middle of the second screen, black around it. Taken again at every refresh, and at once on reaching or leaving the world map; never the menu there, as in FF-4.

## Not covered yet

- **Final Fantasy's ship and canoe**: the cases walk the world map on foot and fly the airship; the map is expected to open aboard the others as well.
- **Soul of Rebirth's world map**: Soul of Rebirth's cases are in Machanon (the menu); FF-11 is expected to hold on its world map as in Final Fantasy II.
- **The European release in French, German, Spanish or Italian**: its windows are the same as in English; the cases play it in English.
- **Final Fantasy II in Europe and Japan**: the cases reach Final Fantasy's world map and Castle Cornelia in both; Final Fantasy II's menu there is expected to match the USA one (its frames are checked the same way), and its world map byte to be the same distance past its base (FF-11). A Final Fantasy II state from either would settle it; if the byte differs, the menu shows on the world map instead.

## How the states were made

All in the harness from power-on, on the USA ROM unless the case says otherwise (Europe and Japan: the same buttons, A on Europe's language screen first): Final Fantasy's new game (each character auto-named, SELECT), the opening on the world map outside Cornelia, Castle Cornelia's 1F, a battle with goblins near Cornelia; Final Fantasy II's new game (the default names), its opening battle with the Black Knights, the Rebel Hideout in Altair and Altair itself. The endgame states come from the owner's GameShark save (`saves/BFFE-endgame.sav`, converted with `tools/sharkport.py`), loaded in the app (the harness has no battery save) and played on in the harness: Final Fantasy's file 1 in the Chaos Shrine's past (Exit, then the airship); Final Fantasy II's file 1 in Pandaemonium 10F (Teleport, to the world map) and file 3, Soul of Rebirth in Machanon.

## For the merge

- Changelog (under the open version, What's new): "Final Fantasy I & II: Dawn of Souls (USA, Europe, Japan): in Final Fantasy, the second screen shows the world map with the party's marker while on the world map, and START's menu (the party, gil, time, crystals, where you are) elsewhere; in Final Fantasy II (and Soul of Rebirth), its world map on the world map and START's menu elsewhere. In battles and outside the games it shows the logo; in conversations the last picture stays."
- `wiki/Supported-games.md`, table: Final Fantasy I & II: Dawn of Souls | USA, Europe, Japan | the world map on the world map, START's menu elsewhere (both games).
- `wiki/Supported-games.md`, section:

  ### Final Fantasy I & II: Dawn of Souls

  Both games on the cartridge. In Final Fantasy, walking about on the world map, the second screen shows the game's world map (the one hold B and SELECT opens) with the party's marker; anywhere else, START's menu with the whole party, the gil, play time, crystals and where you are. Final Fantasy II and Soul of Rebirth do the same with their own world map and menu. Both are taken again every second or so, so they keep up as you play. In battles, on the cartridge's own menus and in the Bestiary, the second screen shows the logo; in conversations, shops and scenes, the last picture stays.

  Late in Final Fantasy the game opens its map in dungeons too; the second screen shows the menu there, where HP and MP matter more.
- README: the supported game count goes up by one; `docs/games/README.md`: its row in the table (prefix FF, `BFFE` + 2).

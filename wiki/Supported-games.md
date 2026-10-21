The USA, European and Japanese releases are all supported. Any other GBA game also runs (set `show_all = 1` to list them); it just plays on one screen, with the second screen dark.

| Game | Codes (USA / Europe / Japan) | Second screen | Map button |
|---|---|---|---|
| Castlevania: Aria of Sorrow | `A2CE` `A2CP` `A2CJ` | map | Select |
| Castlevania: Harmony of Dissonance | `ACHE` `ACHP` `ACHJ` | map | Select |
| Castlevania: Circle of the Moon | `AAME` `AAMP` `AAMJ` | map, with the completion rate | Select |
| Metroid: Zero Mission | `BMXE` `BMXP` `BMXJ` | map | Start |
| Metroid Fusion | `AMTE` `AMTP` `AMTJ` | map | Start |
| The Legend of Zelda: A Link to the Past & Four Swords | `AZLE` `AZLP` `AZLJ` | world and dungeon maps | L |
| Tony Hawk's Pro Skater 2 (SK8 in Japan) | `ATHE` (USA and Europe) `ATHF` `ATHD` `ATHJ` | the goal sheet for the current run | — |
| Mario Kart: Super Circuit (Mario Kart Advance in Japan) | `AMKE` `AMKP` `AMKJ` | the race HUD, results and podium | — |
| Tony Hawk's Pro Skater 3 | `AT3E` (USA and Europe) `AT3F` `AT3D` | the goal list; optional four-button layout | — |
| Super Mario Advance 2: Super Mario World | `AA2E` `AA2P` `AA2J` | the SELECT menu on the world map; the status bar in levels; optional SNES layout | — |
| Final Fantasy Tactics Advance | `AFXE` `AFXP` `AFXJ` | in battles and the scenes on the battle maps: dialogue, menus, unit details | — |
| Advance Wars (preliminary) | `AWRE` (USA only so far) | in battles: the unit list or the battle intel, with the minimap | — |

The codes are what `<CODE>` means in logo and overlay file names (see [Playing](Playing#logos) and [[Map screen surrounds and overlays]]).

## How the maps work

GBA DS runs a second, silent copy of the game next to the one you're playing. About every 1.5 seconds (**Map Refresh** in the pause menu), and right after each room transition, it copies your game's state into the silent copy, presses the map button there, waits for the map screen, and records a short loop of frames so blinking markers still blink. Before showing a map it checks two things: a memory flag that's only set while the map screen is open, and fixed pixels of the map screen itself. Title screens, cutscenes, menus and fades are rejected, and the last good map stays up. Your game is never paused or touched.

You see the game's own map, with everything it shows (explored rooms, save and warp rooms, completion, area names), at sharp 4× scale on the RG DS Plus.

## Castlevania: Aria of Sorrow, Harmony of Dissonance, Circle of the Moon

- The map is shown with a border taken from the game's own menus (see [[Map screen surrounds and overlays]]).
- **Circle of the Moon**'s map screen has no completion rate, so GBA DS copies the percentage shown after "RATE" on the game's pause screen onto the map: in the top-right corner, which stays clear of rooms even on a fully explored castle (the bottom-right corner is the fallback).
- The Japanese Aria of Sorrow and Harmony of Dissonance keep their data at different addresses; GBA DS knows both.

## Metroid: Zero Mission and Metroid Fusion

- **Mini-map:** with the full map always up, the small 3×3 mini-map in the top-right corner of the game screen is hidden. It's left out when the picture is drawn, so the game itself is unchanged; `map = 0` brings it back along with the normal one-screen view.
- **Map screens** show the pause screen's map in its frame, tidied up (`map_crop`, on by default):
  - the button labels and area name are erased, and the scroll arrows and play-time clock left out;
  - Fusion's "A OBJECTIVE" prompt and its row of ticked item icons are left out, and the map grid behind them rebuilt;
  - Zero Mission's corner minimap gives way to the map underneath, with its "SELECT / CHANGE AREAS" prompt covered by a piece of the bezel;
  - the coloured plates are recoloured to the frame's greys so they don't stand out.
- `map_crop = 0` shows the pause screens exactly as the games draw them. The European versions' label erasing was checked in English and German.

## The Legend of Zelda: A Link to the Past

- The cartridge also holds Four Swords, which needs linked GBAs. The game list shows only A Link to the Past, and maps are only captured while it's being played.
- **L** brings up the world map outdoors: GBA DS shows the zoomed-out whole-world view, which doesn't scroll with Link. In a dungeon, once you have its map, it shows the dungeon map.
- Where L does nothing (houses, caves, dungeons without their map), the last map stays up, dimmed.
- After Save and Quit, the second screen goes back to the logo.

## Tony Hawk's Pro Skater 2

- The second screen shows the goal sheet for the current run (Free Skate, Single Session and the career checklist), without the sprites the game draws over it. It stays up through the run, pauses and loading, and goes back to the logo at the menus.
- An optional four-button layout, like the console games, is under **Controls** on the second screen: see [Playing](Playing#buttons).

## Super Mario Advance 2: Super Mario World

- **On the world map,** the second screen shows the SELECT menu: the list of levels on the island with your time and score, kept up to date as you move around, held still (Peach's HELP! and the level you're on are shown, not blinking). It's captured in the background like the other games' maps; the game itself is never paused.
- **In a level,** the status bar (lives, Dragon Coins, time, coins and score) moves to the top of the second screen, with a smaller logo below it. The blue item box stays at the top of the game screen, with your reserve item in it. Message boxes and the pause box stay on the game screen. The status bar stays there through doors and pipes.
- An optional **SNES layout** is under **Controls** on the second screen: see [Playing](Playing#buttons).

## Final Fantasy Tactics Advance

- **In battles and the scenes played on the battle maps** (the opening snowball fight included), everything the game draws over the map moves to the second screen: dialogue with its portraits, the name entry, banners such as GET READY!, the unit's details and the command menus. Behind them is the scene itself (the map and the units, without the menus or the tile height), in grey and dimmed, as it looked when it began: it doesn't follow the camera, and is taken again only when the game fades to black or white and back (a new scene). It fades to black and white along with the game screen, and a new scene's still fades in. The game screen keeps the map, the units, the markers on the map (the move and target ranges, the arrow over the active unit, the target cursor) and the tile height. Spells, weapon swings, explosions, damage numbers and other effects also stay on the game screen.
- A unit's **Status** screen stays whole on the game screen.
- **On the world map,** the notices, an area's info box, your clan funds, the date and the L / R prompts move to the second screen, over the map in grey; the game screen keeps the map, Marche and the towns, and a new area you're placing with its name.
- Everywhere else (the title, menus), the second screen shows the logo.

## Tony Hawk's Pro Skater 3

- **During a run** the second screen shows the level's goal list (the pause menu's View Goals), on black, brought up in the background like the other games' maps and kept up to date as you go; the game is never paused. Back at the menus, it shows the logo again.
- The four-button layout under **Controls**: GBA, Four buttons, or Auto (four buttons while skating), as for Tony Hawk's Pro Skater 2.

## Mario Kart: Super Circuit

<p>
<img src="https://raw.githubusercontent.com/kyannon/gba-ds/main/docs/mario-kart-race.png" width="45%" alt="Mario Kart: Super Circuit racing: the track on the top screen with the item box and place in its corners; the HUD and a large course map on the bottom screen">
<img src="https://raw.githubusercontent.com/kyannon/gba-ds/main/docs/mario-kart-podium.png" width="45%" alt="The cup podium: karts on the podium on the top screen; CONGRATULATIONS!, the portrait, place and cup table on the bottom screen">
</p>

The HUD is moved off the game screen rather than copied: GBA DS draws each frame twice, once without the HUD for the game screen and once with only the HUD for the second screen, so nothing behind the HUD is lost.

- **Racing:**
  - The game screen shows the track and the karts, with the **item box** (and your item) in its top-left corner and your **place** in its top-right corner. The "turn ahead" signs stay where the game puts them.
  - The second screen has the coins, lap and time along the top, the **top-four faces** centred down the left, and the **course map** with every racer's dot, twice as big (1.5× if a course's map wouldn't fit comfortably), in the middle.
  - Behind it all is a dimmed picture of the course's own sky and scenery, captured as each race starts.
- **After the finish**, the lap times and the results table are shown on the second screen.
- **After a cup**, the podium's CONGRATULATIONS!, the cup name, your portrait, place, rank and cup table move to the second screen over the game's dimmed logo; the podium, crowd and trophy stay on the game screen.
- The game's **pause menu** stays on the game screen, with the last HUD below it. In the menus, the second screen shows the logo.
- With one screen (`layout = single`), the HUD stays where the game draws it.

## Advance Wars

Preliminary, the USA release only for now (the European release is listed, with its name on the second screen).

- **In a battle, on your turn,** the second screen is laid out after Advance Wars: Dual Strike's: the game's **Unit** list with the unit under the cursor highlighted when it's one of yours (it stays highlighted while you move it and pick its action), or else the **Battle Intel** (bases and funds). Below it, the **minimap** from the START button. All of it is brought up in the background from the menus of a silent copy of the game, as the other games' maps are; your game is never paused.
- The **CO and day plate** and the **terrain and unit info** move from the game screen to the second screen's bottom left and bottom right, so they no longer cover the map; the panels are drawn over them where they meet.
- On the enemy's turn and in battle animations the last picture stays up. On the title and menus, the second screen shows the logo.

## Coming soon

These games are in the game list, but their second screens are still being built, after the DS versions of each series. For now the game plays on one screen and the other shows the game's name.

- **Tactics Ogre: The Knight of Lodis** (USA; Japan: Tactics Ogre Gaiden)
- **Fire Emblem** (USA, Europe, Japan: Rekka no Ken)
- **Advance Wars** (Europe; the USA release is above)
- **Mega Man Battle Network** (USA, Europe, Japan: Battle Network Rockman EXE)

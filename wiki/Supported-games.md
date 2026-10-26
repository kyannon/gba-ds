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
| Final Fantasy Tactics Advance (preliminary) | `AFXE` `AFXP` `AFXJ` | in battles: a unit's Status, the Mission, the Laws and the turn order | — |
| Advance Wars (preliminary) | `AWRE` `AWRP` (in Japan, only in Game Boy Wars Advance 1+2) | in battles: your unit's intel or the battle intel, with the minimap | — |
| Advance Wars 2: Black Hole Rising (preliminary) | `AW2E` `AW2P` (in Japan, only in Game Boy Wars Advance 1+2) | in battles: your unit's intel or the battle intel, with the minimap | — |
| Game Boy Wars Advance 1+2 (Japan: Advance Wars and Advance Wars 2; preliminary) | `BGWJ` | as Advance Wars and Advance Wars 2, whichever is being played | — |
| Fire Emblem (Rekka no Ken in Japan; preliminary) | `AE7E` `AE7X` (English, French, German) `AE7Y` (English, Spanish, Italian) `AE7J` | on the map: the unit, terrain and goal windows and the minimap | — |
| Sword of Mana (Shinyaku Seiken Densetsu in Japan) | `AVSE` `AVSY` (Spanish, Italian) `AVSJ` | the Status screen | — |
| Summon Night: Swordcraft Story (Craft Sword Monogatari in Japan; preliminary) | `AB4E` `AB4J` (no European release) | the Status screen, with where you are | — |
| Pokémon Pinball: Ruby & Sapphire (preliminary) | `BPPE` | the table twice as tall over both screens, with the score bar | — |

The codes are what `<CODE>` means in logo and overlay file names (see [Playing](Playing#logos) and [[Map screen surrounds and overlays]]).

## How the maps work

GBA DS runs a second, silent copy of the game next to the one you're playing. About every second (`map_interval` in `gba-ds.cfg`), and right after each room transition, it copies your game's state into the silent copy, presses the map button there, waits for the map screen, and records a short loop of frames so blinking markers still blink. Before showing a map it checks two things: a memory flag that's only set while the map screen is open, and fixed pixels of the map screen itself. Title screens, cutscenes, menus and fades are rejected, and the last good map stays up. Your game is never paused or touched.

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
- The map is shown still, with everything that blinks on it (the floor buttons, Link's and the goal's markers) showing at once; the X marking where to go is shown red.
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

Preliminary: a new second screen in v1.3.2, replacing the earlier one (which moved the game's dialogue and menus there).

- **Always,** the second screen is a dark background with the game's logo, small and dimmed, in the middle (the title, menus and the world map too).
- **In a battle,** at the top left, the **Status** screen of the unit under the cursor (or the active unit's, when the cursor isn't on one), turning between its Equip and Abilities pages every 5 seconds; at the top right, the **Mission** (what wins and loses the battle); at the bottom left, the **Laws** (forbidden and recommended); at the bottom right, the **turn order** (the list L shows). All are brought up in the background on a silent copy of the game, as the other games' maps are; your game is never paused or touched, and the game screen stays exactly as the game draws it. They're taken again only with the battle map showing: while a unit picks where to move or what to hit, on the enemy's turn and in scenes, the last ones stay.
- **Known issue:** in the opening snowball fight only the Status and the turn order show.

## Pokémon Pinball: Ruby & Sapphire

Preliminary. The USA release.

- **On a table,** the two screens together show twice as much of it: the top screen the upper part, the bottom screen the part below it. The view follows the game's camera, so the ball stays on the top screen, except at the bottom of the table, where the view stops: the flippers and the ball are then on the bottom screen.
- The game itself only draws the part its camera is on, so the rest is shown as the game last drew it: there, animations stand still until the camera comes back. When a table first comes up the camera sweeps over all of it, so it's all there from the start; after loading a state, the parts the camera hasn't been to yet are black (and the top screen shows the game's own picture until the part above has been seen).
- The **score bar** (score, Pokémon caught, coins, balls) moves from the game screen to the bottom of the second screen.
- **Events** over the table (a Pokémon caught, the Pokémon Mart) show on the game screen as the game draws them, with the table below them on the second screen; the table over both screens comes back right after.
- On the title and menus, the second screen shows the logo.
- **Not tried yet:** the bonus stages.

## Summon Night: Swordcraft Story

Preliminary. The USA release and the Japanese one (Craft Sword Monogatari); there was no European release.

- **While you play,** the second screen shows the **Status** screen from the START menu, at the game screen's size, with the menu's **Place** bar (where you are) over its bottom, its heading short enough to leave the row under the stats in view. Both are brought up in the background on a silent copy of the game; your game is never paused or touched. Where the menu can't be opened (a scene, a fight), the last picture stays.
- On the title screen, the second screen shows the logo, the whole sword included.

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

## Advance Wars and Advance Wars 2: Black Hole Rising

Preliminary. The USA and European releases of both, and in Japan Game Boy Wars Advance 1+2, which has both games on one cartridge. The two work the same way.

- **In a battle, on your turn,** the second screen is laid out after Advance Wars: Dual Strike's: at the top, the **intel of the unit under the cursor** (the screen R opens, with its text box), yours or the enemy's (yours stays while you move it and pick its action), or else the **Battle Intel** (bases and funds). At the bottom left, the **minimap** from the START button. All of it is brought up in the background from the menus of a silent copy of the game, as the other games' maps are; your game is never paused.
- The **CO and day plate** and the **terrain and unit info** move from the game screen to the bottom of the second screen (the plate in the middle, the info on the right, on a grey box), so they no longer cover the map; the panels are drawn over them where they meet.
- The enemy's **CO and day plate** shows on their turn; the panel keeps its last picture.
- **In a fight,** the second screen is the dark background with the game's logo, small and dimmed, in the middle. On the title and menus, the second screen shows the logo.
- **Conversations** in a battle: in Advance Wars they move to the top of the second screen, as wide as the screen, with the battle showing where they were (in the first moments of a battle, before the second screen has a picture, they stay on the game screen); in Advance Wars 2 they stay on the game screen, as the game turns the map off behind them.
- Opening a unit's intel with R in the game leaves the plate and info on the second screen as they were.
- **Known issue:** in Advance Wars' **Field Training** missions the second screen may not work: the tutorial blocks the menus it's built from (SELECT and START). It works fully in Campaign and Versus.
- **Game Boy Wars Advance 1+2** (Japan): GBA DS notices which of the two games you pick from the cartridge's menu and sets the second screen up as for that game (Game Boy Wars Advance as Advance Wars, Game Boy Wars Advance 2 as Advance Wars 2). On the cartridge's own title and menu, and on both games' titles, the second screen shows the cartridge's logo. Game Boy Wars Advance has so far been tried only in its first Field Training mission (with the known issue above); its campaign and versus battles are still to be checked.

## Fire Emblem

Preliminary. The USA release, both European ones (English, French and German; English, Spanish and Italian) and the Japanese one (Rekka no Ken).

- **Always,** the second screen is a dark background with the game's logo, small and dimmed, in the middle (on the title screen and in menus too).
- **On the map,** the windows the game puts in the corners (whichever corners the cursor leaves free) move to fixed places on the second screen: the **terrain** at the bottom left and the **goal** and enemies left at the bottom centre. The **minimap** from the START button is at the bottom right, brought up in the background in a silent copy of the game; when it can't be (in a conversation), the chapter's last one stays.
- **With the cursor on a unit** (yours or the enemy's), that unit's **status screen** (the one R opens) is at the top left, without its moving background, turning through its three pages (personal data, items, weapon and support levels) every 5 seconds; the goal, terrain and minimap stay on top of it. It's opened in the silent copy too, and taken again only when the cursor or a unit changes, so it holds still between pages. Once a unit is selected, its status goes, and its commands take the place. The game's own small unit window is left off both screens.
- **A unit's menu** (Attack, Wait and so on) with its pointing hand, and the **battle forecast** with its weapon-triangle arrows and x2, move to the top centre of the second screen.
- While a unit is selected and in the other menus, the second screen keeps what it showed, and those menus stay on the game screen.
- **Conversations** (the speech bubble, its text and the portraits), on the map and before or after a fight, move to the second screen with the portraits standing on its bottom edge, and the **tutorial's** boxes as they are; the windows step aside until they're done. A unit's small speech bubble with no portrait stays on the map, by the unit. Story scenes before and after the battle (a picture or black behind the talk) stay on the game screen, and the second screen goes back to the logo until the battle map is up again.
- **ENEMY PHASE and PLAYER PHASE**, with their sparkles, go across the second screen instead of the map, while the game screen dims; the windows are cleared, and come back as you take control again.
- **In a fight and on the other armies' turns,** the second screen is the background with the logo alone.

## Sword of Mana

Profile contributed by LucasAlexandrou.

- **During play,** the second screen shows the character's **Status** screen (the one in START's ring: Status, then Status), with level, HP, MP, experience, stats and the weapon and magic levels. It's opened in the background in a silent copy of the game every map refresh, so it stays up to date without you opening it; your game is never paused. While it can't be opened (a conversation, a scene), the last one stays.
- **On the game screen,** the HUD's portrait and HP / MP figures are left off, since the Status screen has them; the charge bar stays.
- **On the title screen,** the second screen shows the logo.

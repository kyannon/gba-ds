The USA, European and Japanese releases are all supported. Any other GBA game also runs (set `show_all = 1` to list them); it just plays on one screen, with the second screen dark.

| Game | Codes (USA / Europe / Japan) | Second screen |
|---|---|---|
| Castlevania: Aria of Sorrow | `A2CE` `A2CP` `A2CJ` | map |
| Castlevania: Harmony of Dissonance | `ACHE` `ACHP` `ACHJ` | map |
| Castlevania: Circle of the Moon | `AAME` `AAMP` `AAMJ` | map, with the completion rate |
| Metroid: Zero Mission | `BMXE` `BMXP` `BMXJ` | map |
| Metroid Fusion | `AMTE` `AMTP` `AMTJ` | map |
| The Legend of Zelda: A Link to the Past & Four Swords | `AZLE` `AZLP` `AZLJ` | world and dungeon maps |
| Tony Hawk's Pro Skater 2 (SK8 in Japan) | `ATHE` (USA and Europe) `ATHF` `ATHD` `ATHJ` | the goal sheet for the current run |
| Mario Kart: Super Circuit (Mario Kart Advance in Japan) | `AMKE` `AMKP` `AMKJ` | the race HUD, results and podium |
| Tony Hawk's Pro Skater 3 | `AT3E` (USA and Europe) `AT3F` `AT3D` | the goal list; optional four-button layout |
| Super Mario Advance 2: Super Mario World | `AA2E` `AA2P` `AA2J` | the SELECT menu on the world map; the status bar in levels; optional SNES layout |
| Final Fantasy Tactics Advance | `AFXE` `AFXP` `AFXJ` | in battles: the Status of the unit under the cursor, or the turn order and Laws; on the world map: today's Laws |
| Advance Wars (Game Boy Wars Advance in Japan) | `AWRE` `AWRP` `BGWJ` (Japan: on Game Boy Wars Advance 1+2, with Advance Wars 2) | in battles: the intel of the unit under the cursor, or the battle intel |
| Advance Wars 2: Black Hole Rising (Game Boy Wars Advance 2 in Japan) | `AW2E` `AW2P` `BGWJ` (Japan: on Game Boy Wars Advance 1+2, with Advance Wars) | in battles: the intel of the unit under the cursor, or the battle intel |
| Fire Emblem (Rekka no Ken in Japan) | `AE7E` `AE7X` (English, French, German) `AE7Y` (English, Spanish, Italian) `AE7J` | on the map: the status of the unit under the cursor |
| Sword of Mana (Shinyaku Seiken Densetsu in Japan) | `AVSE` `AVSY` (Spanish, Italian) `AVSJ` | the Status screen |
| Summon Night: Swordcraft Story (Craft Sword Monogatari in Japan) | `AB4E` `AB4J` (no European release) | the Status screen, with where you are |
| Golden Sun (Ougon no Taiyou: Hirakareshi Fuuin in Japan) | `AGSE` `AGSD` `AGSF` `AGSI` `AGSS` `AGSJ` | the Status screen, each party member in turn; in battle, the one choosing |
| Tactics Ogre: The Knight of Lodis (Tactics Ogre Gaiden in Japan) | `ATOE` `ATOJ` (no European release) | the battle's unit list, on the unit under your cursor |
| Pokémon Pinball: Ruby & Sapphire | `BPPE` | the table twice as tall over both screens, live, with the score bar |
| F-Zero: GP Legend (F-Zero: Falcon Densetsu in Japan) | `BFZE` `BFZP` `BFZJ` | the race HUD, with the course map twice as big |
| River City Ransom EX (Downtown Nekketsu Monogatari EX in Japan) | `BDTE` `BDTJ` (no European release) | the Status screen, with the top bar moved off the game screen |
| Spider-Man: Mysterio's Menace (Spider-Man: Mysterio no Kyoui in Japan) | `ASEE` (USA and Europe) `ASEJ` | the Select menu: the objective, password, suits, upgrades and web containers |

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
- An optional four-button layout, like the console games, is under **Controls** in the quick bar (swipe up from the bottom of the touchscreen): see [Playing](Playing#buttons).

## Super Mario Advance 2: Super Mario World

- **On the world map,** the second screen shows the SELECT menu: the list of levels on the island with your time and score, kept up to date as you move around, held still (Peach's HELP! and the level you're on are shown, not blinking). It's captured in the background like the other games' maps; the game itself is never paused.
- **In a level,** the status bar (lives, Dragon Coins, time, coins and score) moves to the second screen, under the level's name (kept from the world map's list as you went in), with the logo below it. The logo is Super Mario World's own, from its title screen. The blue item box stays at the top of the game screen, with your reserve item in it. Message boxes and the pause box stay on the game screen. The status bar stays there through doors and pipes.
- An optional **SNES layout** is under **Controls** in the quick bar (swipe up from the bottom of the touchscreen): see [Playing](Playing#buttons).

## Final Fantasy Tactics Advance

A new second screen since v1.3.2, replacing the earlier one (which moved the game's dialogue and menus there).

- **In a battle, with the cursor on a unit** (yours, an enemy or a guest), the second screen is that unit's **Status** screen, as the game draws it, turning between its Equip and Abilities pages every 5 seconds.
- **With the cursor on no unit** (or on the judge, who has no Status), it shows the **Laws** (forbidden and recommended) at the top and the **turn order** (the list L shows) below them. The list is too long to fit under the Laws, so its last rows are cut off at the bottom; who acts next always shows.
- All of it is brought up in the background on a silent copy of the game, as the other games' maps are; your game is never paused or touched, and the game screen stays exactly as the game draws it. It's taken again only with the battle map showing: while a unit picks where to move or what to hit, on the enemy's turn and in scenes, the last picture stays.
- **On the world map**, it shows the **Laws** window (what L opens there) on its own, as the game draws it but without the map behind it, the L Law sign or the pointer: today's laws, taken again as soon as your clan arrives somewhere new, even with the place's menu up (a town's, or a mission's). The Laws window shows another day's laws while the map's pointer is on another place (the day your clan would get there), so the second screen only takes it with the pointer on your clan or between places; in a notice or while your clan travels, the last picture stays.
- Once a battle is won (from the last enemy defeated through the closing scene), during story scenes on the battle map, and outside battles and the world map (the title, menus, a town's Pub or Shop), the second screen shows the logo.

## Pokémon Pinball: Ruby & Sapphire

The USA release.

- **On a table,** the two screens together show twice as much of it, **live**, as if they were one tall screen: GBA DS runs two more copies of the game alongside yours, with the same buttons every frame, drawing the parts of the table your game's view doesn't, so the flippers, the Pokémon and everything else move on both screens. Your game itself is never changed.
- The view scrolls smoothly with the ball and never jumps. While the ball is in the table's upper part, your game is on the top screen with the next part of the table below it. Further down, the two screens show the whole lower table, and the ball is on the bottom screen only when it's down by the flippers.
- The **score bar** is always at the bottom of the bottom screen. The catch timer and banners (BALL SAVE) stay on the top screen.
- When the game is **paused**, in the **Pokémon Mart** or showing a **message** (a Pokémon caught), the bottom screen shows its whole screen, with the table on the top screen.
- The copies are checked against your game every frame. When one drifts apart (now and then in catch mode), or after you load a state or rewind, it's taken from your game again; its screen keeps its last picture for that one frame. During each table's opening sweep the game plays on one screen, with the logo on the other.
- `pinball_live = 0` in `gba-ds.cfg` turns the copies off (for slower devices): the game then plays on one screen, with the logo on the other.
- On the title and menus, the second screen shows the logo.
- The **bonus stages** play on one screen, with the logo on the other.
- The frame buffer is always on in this game: it needs it to run at full speed.

## Golden Sun

Every release has been tried walking about (USA / Europe, German, French, Italian, Spanish, Japanese), and battles in the USA / Europe and German ones; if a battle in another release doesn't work, its second screen keeps the logo.

- **While you walk about,** the second screen shows the **Status** screen (the one in SELECT's menu), at the game screen's size and in your chosen window colour, turning to the next party member every 5 seconds. It's brought up in the background on a silent copy of the game, so it stays up to date without you opening it; your game is never paused or touched.
- **In a battle,** while you choose what to do, the second screen shows the battle's Status screen of the character choosing (the first one in the Fight / Flee / Status menu). While the turn plays out, it shows the logo.
- Walking about, where the menu can't be opened (a conversation, a scene), the last pictures stay.
- On the title screen, the second screen shows the logo.

## Tactics Ogre: The Knight of Lodis

The USA and Japanese (Tactics Ogre Gaiden) releases have both been tried.

- **In a battle,** the second screen shows the battle's unit list (R, View List), with its cursor on the unit your cursor is on in the field, yours or the enemy's (on an empty square, your leader). It follows as soon as your cursor stops on another square.
- **Its pages** (the ones START turns through: level, HP, MP, element, the bars, side) change every 2.5 seconds. Its R SEARCH and START:DATA prompts are left out, as those buttons don't work there.
- It's brought up in the background on a silent copy of the game; your game is never paused or touched. While a unit moves or acts, on the enemy's turn and in scenes, the last pictures stay.
- On the title screen, the second screen shows the logo.

## Summon Night: Swordcraft Story

The USA release and the Japanese one (Craft Sword Monogatari); there was no European release.

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
- **After a cup**, the podium's CONGRATULATIONS!, the cup name, your portrait, place, rank and cup table move to the second screen, on dark grey; the podium, crowd and trophy stay on the game screen.
- The game's **pause menu** stays on the game screen, with the last HUD below it. In the menus, the second screen shows the logo.
- With one screen (`layout = single`), the HUD stays where the game draws it.

## F-Zero: GP Legend

As with Mario Kart: Super Circuit, the HUD is moved off the game screen rather than copied, so nothing behind it is lost.

- **Racing** (Grand Prix, Story, Time Trial, Training):
  - The game screen keeps the **speedometer** where the game draws it (bottom right) and shows your **RANK** in the bottom-left corner, where the course map was.
  - The second screen has the **POWER** meter at the top centre, SPARE MACHINE and LAST (laps left) in the top-left corner (in Training, where LAST is alone, in the corner itself), the time, lap times and KO count in the top-right corner, and the **course map** with every racer's dot, twice as big, at the bottom centre. The game draws the map only every other frame, so it blinks on the GBA; here it's held steady, with the dots moving every frame.
  - In Time Trial there's no RANK: the BEST TIME goes to the second screen's top-left corner instead, with LAST under it.
- The game's **pause menu** stays on the game screen, with the HUD on the second screen.
- Everywhere else (the menus, a story's objective screen and scenes, the course name as a race fades in, the RESULTS once a race ends or after GIVE UP) the second screen shows the logo.
- With one screen (`layout = single`), the HUD stays where the game draws it.
- The USA, European and Japanese releases have all been tried.

## River City Ransom EX

The USA release and the Japanese one (Downtown Nekketsu Monogatari EX).

- **On the street,** the second screen shows the **Status screen** (pause, Status): your name, stamina bar and money along the top, and your stats, on a plain blue background. Its message box along the bottom is left black. It's opened in the background in a silent copy of the game about once a second; your game is never paused.
- The **bar along the top of the game screen** (name, stamina, money) isn't drawn, on the street or in the pause menu, so the street shows there instead. The message box along the bottom stays.
- At the Story / Manage Data menu, the title card and through a story's opening conversation, the second screen shows the logo.
- With one screen (`layout = single`), the top bar stays where the game draws it.
- Shops, restaurants and two players haven't been tried yet.

## Spider-Man: Mysterio's Menace

The USA and European release (one cartridge) and the Japanese one (Spider-Man: Mysterio no Kyoui).

- **In a level,** the second screen shows the **Select menu**, as the game draws it: the level's picture, its objective and password, your suits, upgrades and web containers. It's opened in the background in a silent copy of the game about once a second; your game is never paused.
- The game's own **pause menu** (START) and **Select menu** (SELECT) stay on the game screen; the second screen keeps its picture meanwhile.
- At the title, through the intro and the screens between levels (story pictures, the city map with the next mission), the second screen shows the logo.
- Only the first level has been tried so far.

## Advance Wars and Advance Wars 2: Black Hole Rising

The USA and European releases of both, and in Japan Game Boy Wars Advance 1+2, which has both games on one cartridge. The two work the same way.

- **In a battle, on your turn,** the second screen shows the **intel of the unit under the cursor** (the screen R opens, with its text box, where the game puts them), yours or the enemy's (yours stays while you move it and pick its action), or else the **Battle Intel** (bases and funds) in the middle. Both are brought up in the background from the menus of a silent copy of the game, as the other games' maps are; your game is never paused.
- Nothing leaves the game screen: the **CO and day plate**, the **terrain and unit info**, the DAMAGE box and **conversations** stay where the game draws them.
- **On the computer's turn** (from the moment you end yours, through its moves) the second screen shows the logo: the cursor isn't yours, so there's no intel to bring up. Your army is taken to be the first, as in the campaigns; in Versus, if you play a later army or two people share the device, those turns show the logo too.
- **In a fight,** on the title and menus, and on a battle's results pages, the second screen shows the game's logo.
- **Known issue (won't fix):** in Advance Wars' **Field Training** missions the second screen keeps the logo: the tutorial blocks the menus it's built from (SELECT and START). It works fully in Campaign and Versus.
- **Game Boy Wars Advance 1+2** (Japan): GBA DS notices which of the two games you pick from the cartridge's menu and sets the second screen up as for that game (Game Boy Wars Advance as Advance Wars, Game Boy Wars Advance 2 as Advance Wars 2). On the cartridge's own title and menu, and on both games' titles, the second screen shows the cartridge's logo. Game Boy Wars Advance has so far been tried only in its first Field Training mission (with the known issue above); its campaign and versus battles are still to be checked.

## Fire Emblem

The USA release, both European ones (English, French and German; English, Spanish and Italian) and the Japanese one (Rekka no Ken).

- **With the cursor on a unit** (yours or another army's), the second screen is that unit's **status screen** (the one R opens), as the game draws it, turning through its three pages (personal data, items, weapon and support levels) every 5 seconds. It's opened in the background in a silent copy of the game, and taken again only when the cursor or a unit changes, so it holds still between pages.
- **Otherwise** (the cursor on no unit, a unit selected, its menu and the battle forecast, fights, the other armies' turns, story scenes, the title and menus), the second screen shows the game's logo.
- Nothing leaves the game screen: the unit, terrain and goal windows, menus, the battle forecast, conversations, the tutorial's boxes and the ENEMY PHASE / PLAYER PHASE banners stay where the game draws them.

## Sword of Mana

Profile contributed by LucasAlexandrou.

- **During play,** the second screen shows the character's **Status** screen (the one in START's ring: Status, then Status), with level, HP, MP, experience, stats and the weapon and magic levels. It's opened in the background in a silent copy of the game every map refresh, so it stays up to date without you opening it; your game is never paused. While it can't be opened (a conversation, a scene), the last one stays.
- **On the game screen,** the HUD's portrait and HP / MP figures are left off, since the Status screen has them; the charge bar stays.
- **On the title screen,** the second screen shows the logo.

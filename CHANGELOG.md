# Changelog

## v1.4.12 (not yet released)

### New games
- **Crash Nitro Kart** (USA, Europe and Japan): while driving (races, Time Trial and the Adventure hub), the race HUD moves to the second screen: the time, wumpa fruit and lap along the top (on the hub, your trophies and keys), the four leaders' faces down the left, and the course map with every racer's dot, twice as big on every course, over a dimmed picture of the course's scenery. The item you hold, your place, the turn signs and the countdown stay on the game screen. After the finish, the race rankings are on the second screen; pausing keeps the last HUD there; TRY AGAIN, the title and the menus show the logo. Known limitation: Adventure's boss races, CNK challenges, relic races and crystal arenas, Time Trial's results and link play haven't been tried yet; on the hub, the second screen behind the map is mostly dark.
- **Super Mario Advance** (USA and Europe; Super Mario USA + Mario Brothers in Japan): in Super Mario Bros. 2's levels (the Yoshi Challenge too), the status bar (world, the level's Ace Coins, lives and score) moves to the top of the second screen. Below it is the course board the game shows before each level (every level, starred once its five Ace Coins are found), with its Ace Coin percentage kept up to date from the pause screen as you find coins. The hearts and the pause box stay on the game screen. The title, menus, the screens between levels and the Mario Bros. arcade game show the logo. Known limitation: the board's level boxes are as they were before the level (the next board shows a level's new star); doors, jars, Subspace and boss rooms were checked on the device rather than by the tests.

## v1.4.11 (2026-11-07)

### New games
- **Wario Land 4** (USA and Europe; Wario Land Advance in Japan): in a level, the second screen shows the game's own pause screen without its menu, kept up to date: your hearts, the money you carry, the level's coins against its best, and the gem pieces, Keyzer and CD you've found so far, with the hearts and coins taken off the game screen. A find or a lost heart shows at once; the escape's countdown stays on the game screen. In boss fights, once the countdown has settled at the top, the hearts, the countdown and the boss's health bar move to the top of the second screen; TIME UP!! stays on the game screen. Elsewhere (the title, the hub, the level select, the Item Shop, the mini-games, and from the moment Wario loses his last heart), the logo. Tried in every passage, every boss, all of Wario's forms and on Super Hard; Hard hasn't been tried.

### Changes
- **Shader** in the pause menu's Display, in place of LCD Grid: each press picks the next pattern over the pixels, on both screens. **Off**, **LCD Grid** (as before), **Scanlines** (a dark line under each row of pixels), **Soft Cells** (each pixel a little darker toward its edges), and the red, green and blue stripes of a GBA's own screen in three strengths: **Subpixels (Light)** for a dim screen or low brightness, **Subpixels**, and **Subpixels (Strong)** for a bright screen. None of them costs more than the LCD grid did. With the stripes the colours are brightened to make up for them, but the brightest can only go up to white, so they look a little darker. Remembered for next time (`shader` in gba-ds.cfg; an LCD grid you had on stays on). Known limitation: on the RG DS (2× scale) Soft Cells looks like the grid, and the subpixels have no room for their colours, so they show as a grey pattern.
- **LCD Ghosting** in the pause menu's Display, beside Color Correction: off by default; on, each frame of the game is shown half and half with the one before, as a GBA's slow screen blurred them. Moving things leave a short trail, and sprites that a game flickers every other frame to make them see-through look see-through, as they did on a GBA. The game screen only. Remembered for next time (`lcd_ghosting` in gba-ds.cfg).
- **Color Correction** in the pause menu's Display, beside LCD Ghosting, under Shader: off by default; on, the game's colours are shown as a Game Boy Advance's own screen showed them, darker and less saturated, the same as mGBA's GBA Color shader (its sRGB profile and usual darkening). On both screens, and remembered for next time (`color_correction` in gba-ds.cfg).
- **Fast Forward: On / Off** in the pause menu's Extras, beside Rewind (now **Rewind: On / Off**, the two side by side). On by default, as before; off, R2 does nothing, so a trigger pressed by accident can't skip you ahead. Remembered for next time (`fast_forward` in gba-ds.cfg).
- While you hold R2, **Fast forwarding...** shows in the middle of the second screen, after two arrows pointing right, like Rewinding... when you hold L2.
- **Restart Game**, **Close Game** and **Close GBA DS** in the pause menu ask first: the button says **Are You Sure?**, with what will happen under the buttons; choose it again to go ahead. Moving to another button calls it off. (The quick bar's Close GBA DS already asks for a second tap.)
- **A live second screen is black:** wherever the second screen shows something live over the logo (a status bar or other HUD, Mario Kart's podium, Super Mario Advance 4's inventory), it's now on black instead of dark grey, with the logo at full strength. Dark grey, with the logo at half strength, now always means there's nothing live to show, and the game's own fades to black (through a pipe or door) blend into the second screen.
- **Mario & Luigi: Superstar Saga:** in battles, the information boxes (what the block does, the buttons) and the menus that open on the right (the item list and the Bros. Attacks list, with their cursor) move to the same places on the second screen, and Mario's and Luigi's HP and BP meters to its top left corner, over the logo. The command blocks and the A prompt stay over the brother whose turn it is, and so do the damage numbers and the name of the enemy you're aiming at; the tutorials' speech balloons, a boss's own messages and the results after a battle stay on the game screen. Tried through the story's first battles, in late battles with Bros. Attacks and in boss battles (Bowser, Queen Bean, Wiggler, Roy with the Time Bob-omb, Fawful's dome).
- **Yu-Gi-Oh! The Eternal Duelist Soul:** when you end your turn, the second screen goes back to the logo and stays there through the opponent's turn, until your cursor is on a card again in your next turn. While you choose which monster to attack, it shows the Card View of the monster you're aiming at (in the Japanese release, Duel Monsters 5 Expert 1, the last card stays there).

### Fixes
- With no cheat files for a game, the pause menu's Cheats now points you to the Cheats section of the wiki instead of the README, which no longer covers them. The notes in the cheats and overlays folders (and the overlays comment in gba-ds.cfg) point to the wiki too, and on Android the cheats note now says Extras > Cheats, where the menu has them.
- **Sonic Advance 3:** as each act or boss starts, the rings, time and lives no longer sit on the game's card and then jump to the second screen: they're on neither screen until the second screen has the new card, then they're there over it. (The moment before the card's name came in, when they were off the game screen, is gone with it.)
- **Final Fantasy Tactics Advance:** when one of your units gets two turns in a row, the turn order is now taken again at the start of its second turn, so the turn order shown on the next enemy turn is the one from its second turn, not its first.
- **F-Zero: Maximum Velocity:** after a race, some menus (the file screen, RANKING and others) showed a black second screen instead of the logo.
- **Super Mario Advance 4** (Europe): on the Choose a Language menu, the top of its checkered frame was taken for the world map's status bar: it showed on the second screen and was missing from the game screen. The menu is now drawn whole, with the logo on the second screen.
- **Super Mario Advance 4** (World-e): opening the inventory no longer takes the status bar off the second screen: it stays there, still, while the inventory opens, is open and closes. In World-e's castle hall the second screen shows the logo, instead of the inventory without the status bar.
- **Yu-Gi-Oh! The Eternal Duelist Soul:** after an attack that dealt damage (and the question whether to activate a Quick-play Magic or Trap card), the second screen stopped following your cursor for the rest of that turn, so a card could stay up until the turn or the duel ended (issue #26). It now follows the cursor in every phase of your turn, the Draw Phase before you draw included. After a duel you lost, the last card stayed over the conversation and the menus: it's the logo now, as after a win. In card lists, moving to Exit or opening an empty one no longer makes the second screen hesitate.

### Known issues
- **BananaOS:** with GBA DS in BananaOS's GBA list switched on, the GBA games GBA DS doesn't support play in RetroArch without BananaOS's own second screen (its game panel and companions): BananaOS decides how to share the screens before it knows which game it is. The Settings row and the wiki say so; a fix needs BananaOS's help, and has been asked for.

## v1.4.10 (2026-11-06)

### New games
- **Super Mario Advance 4: Super Mario Bros. 3** (USA, Europe and Japan, every revision): on the world map, the status bar (world, lives, coins, score and your cards) moves to the top of the second screen, above your item inventory (what L opens), kept up to date as you walk about, win items and use them; Toad Houses, Spade games, the kings' rooms and World-e's map (the e-Reader levels) do the same. In a level (airships, boss rooms and World 8's tanks too), the status bar moves to the top of the second screen and the P meter to its bottom, with the logo between, so the game screen shows the whole level; in World-e's levels the coin count goes with the status bar. The logo is Super Mario Bros. 3's own title. The game's other colours (which some colour patches pick) work too. Every level the warp zone reaches has been tried, underwater, desert, giant, sky, ice and fortress levels among them, as have airships and their bosses' rooms, Kuribo's Shoe, World 8's tanks, Bowser's castle and World-e.
- **Sonic Advance 3** (USA, Europe, Japan): each act's card on the second screen (the zone's name on its white band, with the team and the act), with the rings, time and lives moved over it where the game draws them on the card, off the game screen; in the hubs, the logo with the hub's keys, lives and zone label. Tried in every zone's first act, Route 99's results and the hubs; special stages, bonus games, water, Time Attack and multiplayer untried.
- **F-Zero: Maximum Velocity** (USA / Europe; F-Zero for Game Boy Advance in Japan; Jisu F-Zero Weilai Saiche in China), with F-Zero: GP Legend's second screen: while racing, the HUD moves to the second screen (POW with BOOST under it, spare machines and laps left, the time and lap times, and the course map twice as big, held steady), leaving the track clear, with your rank and speed on the game screen. Paused, and on YOU LOST (after GIVE UP, RANK OUT or a destroyed machine), the HUD stays on the second screen until the results. The logo is the title's lettering. Grand Prix and Training have been tried in every release; the link-cable modes haven't.
- **Harvest Moon: Friends of Mineral Town** (USA, Europe, Germany and Japan): walking about, the second screen is the town map (the one L+START opens), updated twice a second: your head where you are, your dog's, the clock with the date and weather, and the text under the map saying which part of the valley you're in. In conversations, events and the game's menus it keeps its last picture; on the title, the logo, the title's wooden sign. Tried on the farm, the road north of it and in town; the mine and festivals haven't been tried yet.
- **Yu-Gi-Oh! The Eternal Duelist Soul** (USA; Yu-Gi-Oh! Duel Monsters 5 Expert 1 in Japan, the original): the second screen shows the game's own **Card View** of the card under your cursor (its picture, name, type and text), in duels (your hand, your cards and the opponent's face-up ones), the Graveyard and other card lists, Deck Edit (Japan: the Deck screen), Card Trading's choice of a card and the cards you win after a duel. Where there's nothing to look at (an empty zone, a pile, a face-down card of the opponent's, the opponent's turn), the last card stays; on the title, menus and conversations, the logo. Link Battle hasn't been tried.

### Changes
- **The first run is much quicker to get going:** the game list makes the selected game's logo first, then the others on screen, and starting a game waits only for that game's own logo (and map border), not for every game's. The rest are made while you're in the game list, and wait while you play.
- **BananaOS:** GBA DS's folder now carries its icon (`gba-ds/images/icon.png`, on a see-through background), so BananaOS's Store can show it on GBA DS's tile in Apps.
- **Sonic Advance:** tried in every zone, with every character, in the act's results, pause and the bosses (the European release): no changes needed.

### Fixes
- **BananaOS:** GBA DS now uses the Game Boy Advance list's own battery saves (`saves/gba`, `.srm`), so a game goes on from the same save in GBA DS and in BananaOS's emulators. Saves GBA DS made before are carried over the first time you start each game; if both have one, the newer is used and the other is kept as a backup.
- **BananaOS:** with GBA DS in BananaOS's GBA list switched on, removing GBA DS left the Game Boy Advance list unable to start its games. They now start in BananaOS's own emulator when GBA DS is gone (start this version once to fix the setting from an older one).
- **Lady Sia:** from 10 gems on (and 10 lives or villagers), the count's first digit stayed on the game screen as the HUD left it.
- **Super Mario Advance 4:** in the opening after Super Mario Bros. 3 is picked (the clouds in the checkered frame), the frame's edges were taken for the world map's status bar: its bottom went missing from the game screen and showed on the second. The opening is now drawn whole, with the logo on the second screen.
- **Sonic Advance 2:** with Cream or Tails, the zone card now shows on the second screen (it stayed on the logo). In boss fights, the card and the rings, score, time and lives stay on the second screen (a frozen picture of the fight came up there, and the HUD went back to the game screen); the countdown's START!! stays whole on the game screen. Every zone's first act, XX, the results and Leaf Forest's boss have now been tried, with each character.

### Known issues
- **BananaOS:** with GBA DS in BananaOS's GBA list switched on, the GBA games GBA DS doesn't support play in RetroArch without BananaOS's own second screen (its game panel and companions): BananaOS decides how to share the screens before it knows which game it is. The Settings row and the wiki say so; a fix needs BananaOS's help, and has been asked for.
- **Sonic Advance 3:** for a moment as each act starts (while the screen is still black), the rings, time and lives are off the game screen.
- **F-Zero: Maximum Velocity:** as the game takes its HUD down (after the finish, on pressing A at YOU LOST, as a machine is destroyed), the HUD shows on the game screen for a single frame.
- **Super Mario Advance 4:** with the inventory open on World-e's map, the second screen shows the inventory without the status bar. Going through a pipe or door, the status bar on the second screen fades out and back with the game.

## v1.4.9 (2026-11-05)

### New games
- **Golden Sun: The Lost Age** (USA / Europe, German, French, Italian, Spanish and Japanese releases), with Golden Sun's second screen: while you walk about, the Status screen, turning through your party every 5 seconds (all eight, the ones in the back too); in a battle, the battle's Status of the character choosing what to do, and the logo while the turn plays out. The logo is the title screen's emblem. Every release has been tried walking about and in a battle.
- **Lady Sia** (USA and Europe): in a level, the second screen shows the game's own pause screen as a status screen: Sia's lives, the gems and rescued villagers, the level's time, the ring and the heart (her health), kept up to date about once a second, without the pause menu's lines. The lives, gems, villagers, ring and heart the game puts up during play (as a level starts, as Sia picks something up, the heart while she's hurt) are left off the game screen, since the second screen has them; the game's own pause screen is drawn whole. The logo is the title's lettering, shown at the title, in the menus and story scenes, on the maps between levels, in the jail and in the credits. Every level has been tried.
- **Zoids: Legacy** (USA; Zoids Saga II in Japan): walking about, in towns, out on the world and in dungeons, the second screen shows the game's own map screen (the one SELECT or R opens), without its prompt line, kept up to date without opening it. In battles, it shows the Status of the Zoid choosing its move (the screen R opens there), and the logo while the battle plays out. In the past, where the game has no map, it shows the game's own message saying so. The logo is the title's lettering.

### Changes
- Bug reports now say whether the game's ROM is an unchanged copy of the game or has been patched or modified (from its checksum). Romhacks and patched ROMs are the next step for GBA DS (#17); this helps tell which problems come from them.

### Known issues
- **Golden Sun: The Lost Age:** sailing on Piers's ship hasn't been tried yet.
- **Lady Sia:** the spells and the bonus levels haven't been tried yet.
- **Zoids: Legacy:** from a random battle's turn menu, the Status has a light strip across its middle, left by the game itself (the game screen shows it too).
- **Final Fantasy Tactics Advance:** if one of your units gets two turns in a row, the turn order shown on the next enemy turn may be the one from that unit's first turn (since v1.4.8).

## v1.4.8 (2026-11-04)

### New games
- **Kingdom Hearts: Chain of Memories** (USA, Europe and Japan): walking about in a world, the second screen is the World Map (the one SELECT opens, with the room you're in lit, without its cursor), updated about once a second. In battles, and on the title and in the opening, it shows the logo, taken from the full-size emblem before the title. Tried in Traverse Town.

### Fixes
- **Final Fantasy Tactics Advance:** a place's Info window on the world map (R) could be taken for the Laws and stay on the second screen.
- **Final Fantasy Tactics Advance:** in a town's Pub, Shop or Prison, the second screen kept the world map's Laws; it shows the logo there now.
- **Final Fantasy Tactics Advance:** in a scene after a battle, the second screen could bring back the last unit's Status instead of keeping the logo.
- **Final Fantasy Tactics Advance:** later in the game (from Kingmoon on, in one player's game), the world map's Laws didn't appear on the second screen. The world map is now recognised by its own screen, at any point in the story.
- **Final Fantasy Tactics Advance:** on one of your units' turns after it has moved, with its menu up, the second screen showed nothing new: no Status, and the turn order wasn't taken. It now shows that unit's Status, and the turn order is taken for the enemy's turns. While a unit picks what to hit, the last picture now stays, as it should, instead of an older turn order coming up.
- **Advance Wars and Game Boy Wars Advance (game 1):** in the campaign, with the cursor on an empty tile, the second screen could keep the last unit's intel instead of showing the Battle Intel. The campaign's Intel menu has Terms first, and GBA DS looked for Status in the wrong place.
- **Sonic Advance:** the zone card on the second screen could show the character's name cut off at the top, as with Knuckles. The card now takes the character's name from a moment it's whole, and the zone's name from the moment it's centred (#15).

### Changes
- **Final Fantasy Tactics Advance:** the world map's Laws are shown on the Status screen's checkerboard, as the turn order is in battles (taken from the Party screen when there hasn't been a battle yet; the Laws wait for it, so they never come up on a plain background first).
- **Final Fantasy Tactics Advance:** a unit's Status on the second screen no longer shows the R Info prompt on its Abilities page (that button does nothing there).
- **Final Fantasy Tactics Advance:** with the cursor on no unit, the second screen now shows the whole turn order (01 to 08) in two columns under one LIST bar, its blue box see-through over the Status screen's checkerboard as the game has it over the battle, with the Laws below it. Empty laws (a dash) aren't drawn. Before, the list was below the Laws and its last rows were cut off. If the names are too long for two columns on a GBA-sized screen, only the first column (01 to 04) is shown, so no name is cut off.
- **Final Fantasy Tactics Advance:** while it isn't your turn (an enemy's, the judge's or a guest's), the second screen shows the turn order and Laws instead of the last unit's Status. They're taken again at the start of each of your units' turns, even if you never move the cursor off the unit, so they're as up to date as they can be.
- **Advance Wars, Advance Wars 2 and Game Boy Wars Advance 1+2:** the unit intel's panel is always at the right of the second screen and its text box at the top left. The game moves both away from the cursor; the second screen no longer follows.
- **Advance Wars, Advance Wars 2 and Game Boy Wars Advance 1+2:** with a unit's intel up, the battle's minimap (the one START opens) is back on the second screen, at the bottom left under the text box.
- **Advance Wars, Advance Wars 2 and Game Boy Wars Advance 1+2:** the unit intel, its text and the minimap now share one frame across the whole second screen, made of the game's own window background, with a thin edge in the colour of the panel's outline (red or blue in Advance Wars 2). The panel joins it without its own border or its INTEL label.

### Known issues
- **Final Fantasy Tactics Advance:** if one of your units gets two turns in a row, the turn order shown on the next enemy turn may be the one from that unit's first turn.

## v1.4.7 (2026-11-03)

### Fixes
- **Advance Wars and Advance Wars 2, European releases:** since v1.4.2 the second screen showed only the logo in battles. The unit intel and Battle Intel are back, as in the USA releases (#14).

## v1.4.6 (2026-11-03)

### New games
- **Sonic Advance 2** (USA, Europe and Japan): as each act starts, its zone card (the zone's name, emblem and icons, on blue and black) goes to the second screen and stays there through the act, and the rings, score and time move to its bottom left and the lives to its bottom right, so the game screen is clear, under water too. The pause window stays on the game screen; on the title and menus the second screen shows the logo. Tried in Leaf Forest with Sonic; other zones, characters, bosses, special stages, Time Attack and multiplayer haven't been tried yet. After loading a state from before this version (or another emulator's) mid-act, the logo shows above the HUD until the next act's card.

### Screen shapes (experimental)
- For devices whose two screens are different shapes, such as the AYN Thor and the Retroid Pocket Duo (a widescreen top screen over a narrower bottom one): **Display > Screen shapes** picks **Keep device aspect ratios** (as before), **Match screen aspect ratios** (the wider screen's picture takes the other screen's shape, with black bars at its sides) or **Match screen sizes and aspect ratios** (the same, at the other screen's real size, against the edge nearest it). With Stretch and either match, both screens show the same shape, so Pokémon Pinball's table carries on across them. Greyed out on devices whose screens are the same shape (the RG DS). Also `screen_match` in `gba-ds.cfg`. GBA DS knows the real screen sizes of the AYN Thor, Retroid Pocket Duo and Duo Lite, AYANEO Pocket DS and Anbernic RG DDS; on other devices it goes by what the system reports, and `screen_ppi` sets them if that's wrong.
- Overlays can be made for each: an overlay's name can end in `-aspect` or `-size`, e.g. `primary-stretch-aspect.png` (see the wiki's overlays page).

### Pause menu
- **Swap Screens** has moved to the Extras page, to make room on the Display page.
- **Frame Buffer** is greyed out in the pinball games, where it's always on; pressing it there no longer changes the setting for your other games.

### Fixes
- **Android, AYN Thor and other devices with a larger top screen:** GBA DS took the second display for the top screen, so on the Thor the game and the second screen were the wrong way up, and Pokémon Pinball's table was upside down across the two screens (Swap Screens didn't help). When the two displays differ in size, the larger one is now the top screen; with two the same size (the RG DS), nothing changes. `top_screen` in `gba-ds.cfg` (`app` or `second`) sets it by hand if a device needs it.

### Known issues
- **Screen shapes is experimental:** it hasn't been tried on a device with differently shaped screens yet (it was built from the AYN Thor's specifications and tested with its screens' sizes on a computer). If something looks wrong, a bug report or your `gba-ds.log` helps.

## v1.4.5 (2026-11-02)

### New games
- **Mario & Luigi: Superstar Saga** (USA and Europe; Mario & Luigi RPG in Japan): walking about, the second screen is the Bros. Info screen of the brother you're controlling (START swaps them, and the page follows), updated about once a second. On the title and in battles it shows the logo. Tried at the start of the story; the European and Japanese releases beyond their titles haven't been tried yet.
- **Dragon Ball Z: The Legacy of Goku** (USA and Europe): walking about, the second screen is the pause menu (Level, Experience, HP, Ki and Time), updated about once a second, with its list of options left out; in that empty panel goes whatever the game shows at its top left, moved off the game screen: the HP and Ki bars, or, while SELECT is held or you fly, the counter in their place. On the title and menus it shows the logo; the title screen paints Goku's hair over the Z, so there the Z is drawn again, in the game's own colours, to the official logo's shape. Tried on the first island.

### Save states
- Ten save slots per game in the pause menu: under Save State and Load State, **‹ Slot 0 ›** to **‹ Slot 9 ›** picks one (LEFT / RIGHT, or tap an end of it), and shows when it was saved or that it's empty; the buttons show the slot they use (Save State 3, Load State 3). The menu opens on the slot you saved to last.
- The quick bar's Save State and Load State (and HOME + R1 / L1) have a slot of their own, the quick state, apart from the pause menu's ten.
- **Save on Quit** and **Auto-Load** have moved to the pause menu's Extras page.
- Your existing save state is kept: it's now the quick state, with a copy in slot 0. The files are `<game>.quick.state`, `<game>.slot0.state` to `<game>.slot9.state` and, as before, `<game>.auto.state` for the resume point.

### Bug reports
- The bug button (and Extras > Submit a Bug Report) now pauses the game and brings up a keyboard on the touchscreen for an optional note about what happened, saved at the top of the report's info file. Type by touch, or with the D-pad and A; Skip saves the report without one. The report is still of the moment you pressed the button.

### Rewind
- While you rewind (L2), **Rewinding...** shows in a box in the middle of the second screen, with a rewind sign, in place of the small message it had before; it says **Can't rewind further** once you reach the start of the rewind buffer.

### Fixes
- **Summon Night: Swordcraft Story:** in a battle the second screen shows the logo, instead of the Status from before the battle.
- **Advance Wars, Advance Wars 2:** the map's cursor and targeting pointer no longer leave bits of yellow at the edge of the unit intel's text box on the second screen.
- **F-Zero: GP Legend:** on Mist Flow: Clip Oval (and any course whose map sits as far left), the course map stayed on the game screen; it's on the second screen with the others now.
- **F-Zero: GP Legend:** a rival's place marker (the number over a machine ahead of you) no longer shows up on the second screen; it stays over its machine.

## v1.4.4 (2026-10-31)

### New games
- **Sonic Advance** (USA, Europe and Japan): the second screen keeps each level's zone card, with the rings, score and time at its top right and the lives below them (moved off the game screen). Tried in Neo Green Hill Zone so far.
- **Fire Emblem: The Sacred Stones** (USA, Europe, and Seima no Kouseki in Japan): as in Fire Emblem, the second screen shows the status of the unit under the cursor, turning through its three pages, and the logo everywhere else. Tried in the prologue; later chapters and the world map haven't been tried yet.
- **Sonic Pinball Party** (USA and Europe), work in progress: the table is twice as tall, over both screens, and live on both, as on Pokémon Pinball: the score bar at the top of the top screen, the message line at the bottom of the bottom screen, and the pause menu and banners on the top screen. Tried on the Sonic and NiGHTS tables (Arcade and Story); the Samba de Amigo table, bonus stages and Story mode's conversations haven't been tried yet. On the title and menus, the second screen shows the logo. Glitches may still occur on the tables (see Known issues); `pinball_live = 0` in `gba-ds.cfg` plays it on one screen instead.
- **Spider-Man: Mysterio's Menace** (Spider-Man: Mysterio no Kyoui in Japan): in a level, the second screen shows the Select menu (the objective, password, suits, upgrades and web containers), updated about once a second. At the title, the intro, between levels and on the continue screen after QUIT GAME it shows the logo. The USA / European and Japanese releases have been tried in the first level.
- **Summon Night: Swordcraft Story 2** (Craft Sword Monogatari 2 in Japan): while you walk about, the second screen shows the Status screen (level, HP, experience and your stats with each weapon), updated about once a second, and with more than one character, each one in turn, 5 seconds each (without the L / R icons); in a conversation or the game's own menus the last one stays. At the title it shows the logo. The USA and Japanese releases have been tried at the start of the story.

### Save states
- Save states now keep what the second screen was showing (a map or status screen, a copied screen, Super Mario Advance 2's level name, Sonic Advance's zone card), and loading one brings it back on the next frame, instead of the logo or the last picture until the next capture. States saved before this load as before; the pinball games' two-screen views and the moved HUDs are drawn again from the game within a frame or two, as before.

### Summon Night: Swordcraft Story
- With more than one character on the Status screen, the second screen now shows each one in turn, 5 seconds each, with the place bar covering the L / R prompt.
- The second screen keeps up better: the Status is taken again about every half second and as soon as you arrive in a new area, and a scene or fade no longer holds up the next one for several seconds.

### Supported games
- Final Fantasy Tactics Advance, Advance Wars, Advance Wars 2, Game Boy Wars Advance 1+2, Fire Emblem, Summon Night: Swordcraft Story, Golden Sun, Tactics Ogre, Pokémon Pinball, F-Zero: GP Legend and River City Ransom EX are no longer marked preliminary: they've been tested as much as the other supported games.

### Game list
- For a game without second-screen support, "Single screen only: plays normally" is now at the top right, a little bigger, clear of the GitHub address.
- Bug reports made in the pinball games' two-screen view also hold what both screens showed, the extra copies' own pictures and the view's last 10 seconds, so problems with it can be traced.

### Known issues
- Sonic Pinball Party's two-screen view is a work in progress: graphics glitches may occur, such as sprites misplaced or flickering for a frame and parts of the table drawn out of step for a moment. A banner rising from the bottom of the screen (SHOOT BONUS) shows at the bottom of the bottom screen for its first frame or two. Bug reports (the quick bar's bug button, right after it happens) help.

## v1.4.3 (2026-10-30)

### New games (preliminary)
- **F-Zero: GP Legend:** in a race (Grand Prix, Story, Time Trial, Training), the HUD moves to the second screen: the POWER meter at the top, your spare machines, laps left, time, lap times and KOs in its corners, and the course map twice as big at the bottom, held steady (the game draws it only every other frame) with every racer's dot moving smoothly on it. The game screen keeps the speedometer and your RANK, moved to the bottom left. Everywhere else the second screen shows the logo. The USA, European and Japanese releases have all been tried.
- **River City Ransom EX** (Downtown Nekketsu Monogatari EX in Japan): on the street, the second screen shows the Status screen (your name, stamina, money and stats) on a plain blue background, without its message box's text, updated about once a second, and the bar along the top of the game screen is gone, so more of the street shows. At the menus and the title card the second screen shows the logo. Shops and two players haven't been tried yet.

### Game list
- The GitHub page's address, github.com/kyannon/gba-ds, is shown under the GBA DS logo.

### Bug reports
- The `bug-reports` folder now has a `README.txt`: how to send a report (attached to a new issue on GitHub) and what to write with it.

## v1.4.2 (2026-10-29)

### New games (preliminary)
- **Golden Sun:** while you walk about, the second screen shows the Status screen, turning through your party every 5 seconds. In a battle it shows the battle's Status of the character choosing what to do, and the logo while the turn plays out. Walking about has been tried in every release (USA / Europe, German, French, Italian, Spanish, Japanese); battles in the USA / Europe and German ones.
- **Tactics Ogre: The Knight of Lodis (back in the list):** in a battle, the second screen shows the unit list with its cursor on the unit your cursor is on, turning through its pages (level, HP, MP, element...) every 2.5 seconds, without its R SEARCH and START:DATA prompts. The USA and Japanese (Tactics Ogre Gaiden) releases have both been tried.

### Final Fantasy Tactics Advance
- On the world map, the second screen now shows today's Laws (the window L opens, on its own), taken again as soon as your clan arrives somewhere new, even with the place's own menu up (a town's, or a mission's).
- **Japanese release:** the world map's Laws work there too, and once a battle's won the closing scene now shows the logo rather than the last picture. (In this version's earlier test builds some battles showed only the logo: fixed.) In a battle, the Laws now show above the turn order there too.

### Advance Wars, Advance Wars 2, Game Boy Wars Advance 1+2
- On the computer's turn (from ending yours, through its moves) the second screen shows the logo, as the cursor isn't yours to bring up intel with; the intel comes back on your turn. Your army is taken to be the first, as in the campaigns.
- Once a battle's won or lost, its results pages no longer show the last unit intel: the logo.

### Android
- GBA DS can now update itself: game list > Settings > Updates, or the offer it makes once a day when a new version is out. It downloads the new APK, checks it and hands it to Android's installer, which asks you to confirm. (The download is checked on the device; this release is the first one it can install.)

### Game list
- Games without second-screen support (listed with `show_all = 1`) are tagged **Single screen only** instead of "No map".

### Quick bar
- When a game starts, a hint at the bottom of the touchscreen says "Swipe up for quick menu", then slides away after 3 seconds or as soon as you touch the screen. `quick_bar_hint = 0` in `gba-ds.cfg` turns it off.

### Pokémon Pinball
- The frame buffer is now always on in this game (the pause menu shows it as "Always on here"), as it needs it to run at full speed.

### Second screen: button prompts left out
- **Final Fantasy Tactics Advance:** a unit's Status no longer shows the D-pad and arrow at the right of its page bar.
- **Advance Wars, Advance Wars 2, Game Boy Wars Advance 1+2:** the unit intel no longer has the yellow corners blinking round the unit's picture.
- **Golden Sun:** the Status from walking about no longer has the box at the top right with "L-R: Rearrange / A: Details" (that corner is left black); the battle's Status no longer shows "Use the L & R buttons to switch between characters."
- **Super Mario Advance 2:** the world map's level list no longer has the ◁ ▷ arrows at the screen's edges (shown once there are other worlds' lists to turn to).

### Fixes
- **Golden Sun:** the German release showed only the logo; it now shows the Status screen and battles like the others. Walking through a doorway could drop the Status screen to the logo for a moment: no longer.
- **Advance Wars, Advance Wars 2, Game Boy Wars Advance 1+2:** leaving a battle for the mode menu, or winning one (Field Training's results too), kept the last unit intel on the second screen; it's the logo again. In Advance Wars 2's Versus the intel didn't show at all: it does again.
- **Tactics Ogre (Japanese release):** the unit list's cursor stayed on the leader and its pages didn't turn, and the list wasn't taken again when the cursor moved: it now follows the cursor's unit and turns its pages, as in the USA release.
- **Advance Wars, Game Boy Wars Advance:** the unit or Battle Intel stayed up through the day banner after you ended your turn; the logo now comes up as soon as you choose End, as in Advance Wars 2.
- **Super Mario Advance 2 (Japanese release):** on the world map the level list took up to a second to follow Mario; it now changes as soon as he stops, as in the other releases.
- Starting a game could crash GBA DS within its first seconds, now and then (seen with Fire Emblem after closing a game and starting one again): a game whose second screen turns through pages (Fire Emblem's unit status, Golden Sun's, Tactics Ogre's...) worked out which page to show from the game played before.
- Waking the device from sleep in the middle of a game could crash GBA DS (seen once on BananaOS): after any pause of more than 2 seconds, the pictures on both screens are now made again.
- A touch on the very edge of the touchscreen (its right or bottom edge, or a finger sliding off it) was ignored, so a swipe or tap that ended there didn't count: the quick bar could miss a swipe up from the bottom edge (#9).

### Known issues
- **Advance Wars games:** in Versus, if you play a later army, or two people share the device, those turns show the logo too (the games keep no record GBA DS could find of which armies a person plays).
- **Golden Sun:** GBA DS closed once as a battle began, on one device. It hasn't happened again or been reproduced, and was most likely the crash fixed above (Golden Sun's Status turns through pages too); if you see it, a bug report (quick bar) straight after helps.
- **Advance Wars, Game Boy Wars Advance 1+2:** in Field Training the tutorial blocks SELECT and START, so the second screen keeps the logo (won't fix: it's the tutorial).
- **The RG DS Plus's Stock Linux OS:** now and then a game starts without sound (the next start has had sound again so far), and the system menu's volume doesn't carry over to GBA DS.

## v1.4.1 (2026-10-28)

### What's new
- **Quick bar and bug reports:** swipe up from the bottom of the touchscreen for a bar with the time, the battery and the frames per second, and buttons for Save State, Load State, Settings (the pause menu), Bug Report and Close GBA DS (tap twice), without pausing the game; swipe down, tap off its buttons or leave it 5 seconds to put it away, or tap its pin to keep it up (remembered). Bug Report (also Extras > Submit a Bug Report in the pause menu) saves a report in `bug-reports`: a save state of that moment, one from 10 seconds before, every button pressed in between (so what happened can be played back exactly), and your device, settings and the end of the log. Attach the `.zip` to a GitHub issue.
- **Controls picker** (Tony Hawk's Pro Skater 2 and 3, Super Mario Advance 2): now at the top of the quick bar, so the second screen shows just the game's own picture.
- **Show FPS** has left the pause menu, as the quick bar shows the frames per second. HOME + X still puts the counter above the game.
- **No Second Screen** has left the pause menu's Display page; `show_logo = 0` in `gba-ds.cfg` still keeps the second screen blank where it would show the logo.
- **Pause menu, Game page:** Save State and Load State now sit side by side on one row, and a new row under them has **Save on Quit: On / Off** (whether closing a game saves where you are) and **Auto-Load: On / Off** (whether starting a game picks up from there). Both are remembered; in `gba-ds.cfg` they're `save_on_quit` and `auto_load` (an older `auto_resume = 0` still switches both off).
- **Android:** frontends that pass a game as a plain file path (no `file://`) now start that game instead of the game list.

### Fixes
- **Android:** GBA DS closed as soon as it started on devices with older 64-bit chips (Retroid Pocket Duo Lite and others with Cortex-A53 / A73 cores).
- **Android:** on devices whose two screens are different sizes (Retroid Pocket Duo Lite), the second screen was cut off. Confirmed fixed on a Duo Lite by the player who reported it.

## v1.4 (2026-10-28)

### The second screen
- **A real GBA screen:** whatever the second screen shows is drawn at the game's own size and scaled like the game screen, so the pixels on both screens match. Fire Emblem's, Final Fantasy Tactics Advance's and the Advance Wars games' second screens used to be bigger pictures, so everything on them came out smaller than the game.
- **One thing at a time, the game screen left alone:** in Fire Emblem, Final Fantasy Tactics Advance and the Advance Wars games, the second screen now shows one screen from the game (a unit's status, the turn order, the battle intel), and nothing moves off the game screen any more: windows, menus, conversations and banners stay where the game draws them.
- **Keeps up quicker:** move the cursor onto another unit (Fire Emblem, Final Fantasy Tactics Advance, Advance Wars) or Mario onto another level on the world map (Super Mario Advance 2) and the second screen starts updating as soon as you stop, instead of at the next once-a-second refresh. An update still on its way for where you were is dropped, and the hidden copy of the game does less work while it waits.
- **Logos:** always at their own size, centred on dark grey, so the GBA screen shows against the black around it. When the logo is all there is to show, it's at half strength, so the screen reads as idle. No more small, dimmed logos behind things; Mario Kart's podium is on plain dark grey, as the results couldn't be read over the logo.

### New games (preliminary)
- **Pokémon Pinball: Ruby & Sapphire (USA):** the table is twice as tall, over both screens, and live on both, as if on one tall screen. Two more copies of the game, taken from yours every frame, draw the parts of the table your game's view doesn't, so everything that happens (catch mode's portrait being uncovered, an egg hatching) shows on whichever screen it's on. The view scrolls smoothly with the ball and never jumps. The score bar is at the bottom of the bottom screen; the catch timer and banners such as BALL SAVE stay on the top screen. Paused, in the Pokémon Mart or showing a message, the bottom screen shows the game's whole screen. `pinball_live = 0` in `gba-ds.cfg` turns the copies off (the game on one screen, the logo on the other). The bonus stages play on one screen for now.
- **Game Boy Wars Advance 1+2 (Japan):** the only Japanese release of Advance Wars and Advance Wars 2, both on one cartridge. GBA DS notices which game you pick and sets up the second screen as it does for that game. On the cartridge's own title and menu, it shows the cartridge's logo.
- **Summon Night: Swordcraft Story (USA; Japan: Craft Sword Monogatari):** the second screen shows the Status screen from the START menu, with the menu's Place bar (where you are) at its bottom. The logo is taken from the title screen, the whole sword included.
- **Advance Wars 2: Black Hole Rising (USA, Europe):** set up as Advance Wars is (below).

### Games
- **Fire Emblem:** with the cursor on a unit, yours or another army's, the second screen is that unit's status screen, as the game draws it, turning through its three pages every 5 seconds; otherwise it shows the logo. The terrain, goal and unit windows, menus, the battle forecast, conversations and the phase banners are back on the game screen, and the minimap is gone.
- **Final Fantasy Tactics Advance, a new second screen (preliminary):** in battles, with the cursor on a unit (yours, an enemy or a guest), the second screen is that unit's Status screen, turning between its Equip and Abilities pages every 5 seconds. With the cursor on no unit, or on the judge (who has no Status), it shows the Laws and the turn order below them (the list's last rows are cut off at the bottom; who acts next always shows). It stays in place while a unit moves or attacks. Once the battle is won (from the last enemy defeated through the closing scene), during story scenes on the battle map and outside battles, it shows the logo. The game screen is left exactly as the game draws it: the dialogue, menus and banners no longer move to the second screen.
- **Advance Wars and Advance Wars 2:** with the cursor on a unit, yours or the enemy's, the second screen shows that unit's intel (the screen R opens, with its text box), and otherwise the Battle Intel, in the middle of the screen. The minimap is gone, and the CO and day plate, the terrain and unit info, the DAMAGE box and conversations stay on the game screen.
- **Super Mario Advance 2:** the logo is now Super Mario World's own, from its title screen (SUPER MARIO WORLD alone). On the world map, the level list updates as soon as Mario stops on a level. In a level, the second screen keeps the level's name from the list at the top, with the status bar under it and the logo centred below.

### Everything else
- **Updates from the game list (ROCKNIX, BananaOS, Stock Linux OS):** once a day, when the game list opens, GBA DS looks for a new release and, if there is one, shows what's new and asks: Update now, Later or Skip this version. Update now downloads it, checks it, installs it (your states, settings and `gba-ds.cfg` are kept) and restarts into it. Settings > Updates checks by hand and switches the automatic checks off (`update_check = 0` in `gba-ds.cfg` does too). Android will follow.
- **Android:** before opening Android's settings for All files access, GBA DS now explains what it's for, with **Continue** and **Not now** (which goes on without it and isn't asked again).
- **Can't save files:** the game list says so when GBA DS can't save its files (all systems), instead of logos, states and screenshots quietly not being kept.

### Fixes
- **Super Mario Advance 2:** in some levels (caves, for one) the scenery behind the level was drawn in front of everything, Mario included. It's back where the game puts it.
- **"Preparing games" counting up for ever** (a Reddit report: 4,500 after five hours with three games) when a game's logo couldn't be pulled from its title screen or saved. GBA DS now tries each logo once per start and goes on without it.

### Known issues
- Final Fantasy Tactics Advance (Japan): the second screen doesn't know when a battle is won, so the last picture stays through the closing scene.
- Super Mario Advance 2 (Japan): the world map's level list updates at the regular refresh, not as soon as Mario stops.
- Game Boy Wars Advance 1+2: Game Boy Wars Advance has only been tried in its first Field Training mission, where (as in Advance Wars) the tutorial blocks the menus the second screen is built from.
- Mario Kart: Super Circuit: the race screen's course map is drawn larger than the game's, and the scenery behind it smoothly scaled.

## v1.3.1 (2026-10-24)
- **Pause menu:** three pages instead of four. Frame Buffer and Match Display Rate are now under Display, Rewind and Show FPS under Extras, and the Performance page is gone. Map Animations and Map Refresh are no longer in the menu. They're `map_animations` and `map_interval` in `gba-ds.cfg`: on, and a map refresh every second (it was every 1.5 seconds). Choices made in the old menu items no longer apply.
- **A Link to the Past:** the map on the second screen is always still, with everything that blinks on it (the floor buttons, Link's and the goal's markers) showing at once, and the X marking where to go in red.
- **Castlevania (all three):** the map's border from the game's menus now shows the first time a game is played, not only from the next start.

## v1.3 (2026-10-23)
- **Three more games in the list:** Fire Emblem (USA, Europe, Japan: Rekka no Ken), Advance Wars (USA, Europe) and Sword of Mana (USA, Europe, Japan: Shinyaku Seiken Densetsu), described below for every release.
- **Advance Wars, preliminary:** in battles, the second screen shows the game's unit list, with the unit under the cursor highlighted (it stays while you move it), or the battle intel when the cursor isn't on one of your units, with the minimap, the CO and day plate and the terrain and unit info along the bottom (the plate and the info move off the game screen). The logo is taken from the title screen. Known issue: in the Field Training missions the second screen may not work; it works fully in Campaign and Versus.
- **Fire Emblem, preliminary:** the second screen is always the game's logo, small and dimmed, on a dark background. On the map, the terrain and goal windows move off the game screen to the second screen's bottom left and bottom centre, with the minimap at the bottom right. With the cursor on a unit, its status screen (the one R opens) is at the top left, turning through its three pages every 5 seconds until the unit is selected; a unit's menu (Attack, Wait...) and the battle forecast (with its arrows and x2) go to the top centre. Conversations (with the portraits along the bottom, on the map and before or after a fight), the tutorial's boxes and the ENEMY PHASE / PLAYER PHASE banners (with their sparkles, the game screen dimming meanwhile) move to the second screen; a unit's own small speech bubble stays on the map, and so do story scenes before and after the battle, with the second screen back to the logo.
- **Sword of Mana:** during play, the second screen shows the character's Status screen, kept up to date without opening it, and the HUD's portrait and HP / MP leave the game screen (the charge bar stays); the logo on the title screen. (Contributed by LucasAlexandrou.)
- **No sound on some systems (reported on ROCKNIXDS 1.6.4):** GBA DS finds the sound server's connection itself when the system doesn't pass it on, and tries the other ways of reaching it (PipeWire, PulseAudio, ALSA) when the first fails. `gba-ds.log` now says how the sound goes out, for reports.
- **The RG DS Plus's Stock Linux OS, experimental:** started from its Ports menu, GBA DS finds the GBA ROM folder next to the ports folder (or on either card) and switches the touchscreen on while it runs. The built-in buttons are mapped, HOME and BACK both open the menu, and sound goes through the system's sound service. Its volume keys set GBA DS's own volume (shown on screen and kept between games); the volume set in the system's menu doesn't carry over, and once in a while a game plays without sound (so far, the next start has had sound again). Settings has no EmulationStation option there, since the system doesn't use it. See the new install page in the wiki.
- **GBA DS closing straight back to Ports with no log:** GBA DS now starts even when its files lost their permissions on the way to the device (unzipped on a computer, then copied over a network share or MTP), and on cards that don't let programs run it runs from a copy in memory. Either way the log says so.
- **Map Buttons (in the game list's Settings, on Linux and Android):** GBA DS asks for each button in turn, by where it sits (tap to skip ones a device doesn't have), for a controller GBA DS doesn't know or a button that does the wrong thing, and uses the result straight away, kept for that controller; its last screen closes with a button press or a tap. On Linux the log has the mapping, so it can be built into GBA DS for everyone with that device.
- **Advance Wars and Fire Emblem:** during a fight, the second screen shows the game's logo, small and dimmed, on the dark background.
- **Android: Cocoon.** Each release has a player entry for the Cocoon launcher's Game Boy Advance list (`gba-ds-cocoon-player.json`), for Cocoon to add to its list; see the wiki's page on launching from Android frontends.

## v1.2.1 (2026-10-19)
- **Super Mario Advance 2:** the status bar stays on the second screen in castle rooms behind doors and in the boss fights (it switched to the logo and stayed there), and the SNES layout keeps working in those rooms.

## v1.2 (2026-10-19)
- **Three more games in the list:** Final Fantasy Tactics Advance (USA, Europe, Japan), Super Mario Advance 2: Super Mario World (USA, Europe, Japan) and Tony Hawk's Pro Skater 3 (USA/Europe, France, Germany), each with its own second screen and its logo when there's nothing else to show.
- **Final Fantasy Tactics Advance:** in battles and the scenes on the battle maps, the dialogue, name entry, banners (GET READY!, BATTLE WON!), unit details and command menus move to the second screen, over a still of the scene in grey, leaving the map, the units, the tile height, and every spell, weapon and ability effect on the game screen. On the world map, the notices, clan funds, date and button prompts move there too, over the land and its roads and towns. The still fades to black or white with the game.
- **Super Mario Advance 2: Super Mario World:** on the world map, the second screen shows the island's levels (the SELECT menu, with your time and score) as you move; in a level, the status bar moves to the top of the second screen with a smaller logo below it, and the blue item box stays on the game screen. **Controls** adds the **SNES layout**: in levels, B jumps, Y runs, A and X spin-jump, and L / R pan the camera left / right as on the Super Nintendo, without moving or turning Mario. The pause box, menus and the world map keep the GBA buttons.
- **Tony Hawk's Pro Skater 3:** during a run, the second screen shows the level's goal list (View Goals), on black, kept up to date. **Controls** has the four-button layout, and **Auto**: four buttons while you're skating, the GBA layout in the menus.
- **Rewind:** hold **L2** to go back in time, up to 30 seconds, at double speed; let go to play on from there. On by default; **Rewind** in the pause menu (Performance) switches it off, and `rewind_seconds` in gba-ds.cfg sets how far back it goes.
- **Swap Screens moved to L3** (press the left stick), since L2 now rewinds. HOME + Y still swaps too.
- **Bigger pause menu:** the buttons span the whole screen, with larger text, the same size on every page.
## v1.1.6 (2026-10-17)

- **The Linux download is now `gba-ds-vX.Y.Z-linux-aarch64.zip`** (it was `…-rocknix-aarch64.zip`): the same zip runs on ROCKNIX and BananaOS.
- **BananaOS: play from Banana's own Game Boy Advance list.** Switch on **GBA DS in BananaOS's GBA list** in the game list's Settings (and restart BananaOS): games with second-screen support then start in GBA DS from Banana's menu, and every other game keeps playing in RetroArch.
- **A cheats folder of GBA DS's own:** `.cht` files in `gba-ds/cheats` (subfolders are fine) are found as well as RetroArch's, on ROCKNIX and BananaOS alike: handy where RetroArch's cheat folder is hard to reach.
- **RetroAchievements without the `curl` program:** systems that only have the curl library (BananaOS) now connect too.
- **BananaOS support** (the light Linux for the RG DS and RG DS Plus): the Linux zip runs as a port there, on both screens with the touchscreen, with games from `roms/gba`, battery saves beside RetroArch's in `saves` (a RetroArch save is carried over), and the RetroAchievements account from Banana's setup. See the wiki's *Install on BananaOS*.
- **Match Display Rate** (pause menu → Performance, off by default): runs the game at the screen's refresh rate instead of the GBA's 59.73 frames a second, and shows each frame for exactly one refresh. On a 60 Hz screen that's 0.46% faster (the sound is adjusted to match; you can't hear it) and removes the small stutter of a frame shown twice about every 3.7 seconds. Also `match_display` in gba-ds.cfg.

## v1.1.5 (2026-10-16)

- **GBA DS is now free software under the GNU General Public License, version 3.** The licence is in the repository and, from this release, in `gba-ds/licenses/` (ROCKNIX) and inside the APK.
- **Clock and battery level in the pause menu:** while the game is paused, the time (24-hour, HH:MM) at the top left of the top screen and the battery percentage at the top right.
- **Pause menu: a Performance page**, between Display and Extras, with Map Animations, Map Refresh, Frame Buffer and Show FPS. The tabs are now sized to fit their names.

## v1.1.4 (2026-10-16)

- **Menus on 640×480 screens (RG DS)** are now drawn at the same proportions as on the RG DS Plus: the pause menu, game list, Settings, cheats and achievements were drawn at half size, which looked too small. The RG DS Plus looks the same as before.
- **LCD grid on 640×480 screens (RG DS):** sharp and even. The grid is now drawn at the screen's own resolution when the GBA pixels are only 2 or 3 screen pixels wide, instead of being drawn larger and scaled down, which blurred the picture and made the lines uneven. The RG DS Plus looks the same as before.
- **No Second Screen: Show Logo / Keep Blank** (pause menu → Display): when the game has nothing for the second screen (title screens, menus, before the first map), show the game's logo there or leave it black. Also `show_logo` in gba-ds.cfg.
- **The logo on the second screen now looks like a second GBA screen:** the same size as the game beside it, following the second screen's Scale setting, and with the LCD grid when it's on.

## v1.1.3 (2026-10-15)

- **LCD grid** now covers Mario Kart's second screen too, with the same cell size as the game screen.
- **Mario Kart: Super Circuit** (USA, Europe; Mario Kart Advance in Japan): during a race the whole HUD (coins, lap, item box, time, the top-four faces, your place and the course map) moves to the second screen, leaving the game screen to the track and the karts, with the item box in its top-left corner and your place in its top-right (the "turn ahead" signs stay there too). The top-four faces are centred down the second screen. The course map is drawn twice as big, over a dimmed picture of the course's sky and scenery; the lap times and results after the finish show there too, and after a cup the podium's CONGRATULATIONS!, cup name, your portrait and cup table, over the game's logo (CONGRATULATIONS! and your place centred, your portrait on the left, since the trophy stays on the game screen). In the menus, the second screen shows the game's logo.
- **Documentation moved to the [wiki](https://github.com/kyannon/gba-ds/wiki)**, expanded: a full settings reference, troubleshooting, Android frontend set-up (GammaOS, Daijisho, ES-DE), the ROCKNIX Ports icon, and a guide to adding second-screen support for a game.

## v1.1.2 (2026-10-15)

- **Frame Buffer** (pause menu → Game; both platforms, off by default): the next frame is prepared while the current one is shown, which smooths out the odd stutter at the cost of one frame (about 17 ms) of input delay. Remembered for next time.
- **RetroAchievements on Android** (casual mode), as on ROCKNIX: progress and unlocks on the second screen, and the Achievements list in the pause menu. Sign in from the game list's **Settings** → **RetroAchievements**; GBA DS keeps a sign-in token, not your password.
- **Android: start games from GammaOS's own launcher.** In Game Systems → Game Boy Advance, set Launch Type to **Custom Package**, Package to `io.github.kyannon.gbads`, and the Intent Template given in the README.
- **Smoother play.** Achievement popups no longer slow the game down, and text everywhere costs less to draw. On Android, the game no longer freezes for a moment when it saves, and frame times are steadier.

## v1.1.1 (2026-10-15)

- **RetroAchievements** (casual mode) on ROCKNIX, with the account signed in in EmulationStation. Starting a game shows its achievement progress on the second screen, and unlocks pop up there with the badge and points, out of the game's way. Save states keep the achievements' progress with them. **Achievements** in the pause menu lists the game's achievements with badges, points and progress. `retroachievements = 0` in gba-ds.cfg turns it off. Hardcore mode isn't available yet.
- **Game list as a list again.** The game list now shows one game per row by name, as in earlier versions; the logo grid is still there: tap the button left of Settings, or press X, to switch (GBA DS remembers which you chose). Names leave out tags like "(USA)" and "(En,Fr,De,Es,It)"; the flag tells regional copies apart (`name_tags = 1` in gba-ds.cfg keeps them).
- **Pause menu:** a third page, **Extras**, now holds Cheats, Achievements and Take Screenshot, so the Game page is just playing, saving and closing.
- **Two more scaling options** (pause menu → Display → Scale): **Zoom** fills the screen height and keeps the picture's shape, cutting off its left and right edges; **Stretch** fills the whole screen. Scale now steps through Pixel Perfect, Full Width, Zoom and Stretch.
- **Map Refresh** (pause menu → Display): how often the map updates, every 3, 1.5 (the default), 1 or 0.5 seconds. Faster keeps the map more up to date on devices with power to spare. Also `map_interval` in gba-ds.cfg.
- **New interface font.** All text is now drawn in Jost, an open-source font in the style of the Futura menus, including the parts that used the small blocky font before: cheat names, game names, Settings help and on-screen messages. Long names are cut to fit.

## v1.1 (2026-10-14)

- **Android version** (an APK, first tested on the RG DS Plus with GammaOS): the game on the top screen and the map or logo on the bottom touchscreen, as on ROCKNIX, with the game list, pause menu, cheats and map capture. SELECT+START opens the pause menu (Android keeps HOME), and BACK does too. Save states, cheats, overlays and screenshots live in a `GBA DS` folder on the internal storage. See the README.
  - Experimental, not yet tested on a device: starting games from frontends such as ES-DE and Daijisho, and the pause menu opening when you come back to GBA DS from the home screen or the app switcher.
- **Works on stock ROCKNIX.** The bottom screen stayed dark (taps still reached the hidden menus): stock ROCKNIX keeps the second screen switched off and the touchscreen tied to the top one. GBA DS now switches the bottom screen on and lines the screens and touchscreen up while it runs, and puts everything back the way it was when it closes. ROCKNIXDS keeps working as before.
- Game list: a folder that appears twice in `rom_dir` (or through a link) is only listed once.
- New option `menu_combo = select+start` in gba-ds.cfg: pressing SELECT and START together opens the pause menu, without the game seeing either button. Off by default.

## v1.0.3 (2026-10-14)

- **Tony Hawk's Pro Skater 2** (USA/Europe, France, Germany, and SK8 for Japan) is in the game list. The goal sheet shown before a run (Free Skate or Single Session rules, or the career level's checklist with its scratch-outs and ticks) is copied to the second screen, without the CAREER / CASH counters and button prompts drawn over it, and stays there for the run, through pauses and Retry. At the menus the second screen shows the game's logo, cut out of the title screen.
- Tony Hawk's Pro Skater 2: a **Controls** picker on the second screen (touch) chooses the GBA buttons, a four-face-button layout like the console games (top grind, bottom ollie, left flip, right grab), or **Auto**: four buttons while skating, the GBA layout in menus, pause and results. Remembered per game.
- **Launch from EmulationStation**: a new Settings screen on the game list (SELECT, or the Settings button) adds GBA DS to EmulationStation's emulators for Game Boy Advance, so a game can be started straight from the GBA list with GBA DS, per game or for the whole system. The other emulators keep working as before.
- **Zipped ROMs**: `.zip` and `.7z` files work in the game list and when launched directly (the first GBA ROM inside is used). The game list remembers what's in each archive, so a big `.7z` is only unpacked to look inside once.
- Game list: French and German flags for single-country releases when you have more than one region of a game.

## v1.0.2 (2026-10-13)

- Metroid map screens: the scroll arrows and play-time clock are left out (both games), as is Fusion's row of ticked item icons along the top (the item counters stay), and Zero Mission's corner minimap and its A button give way to the map underneath. Both Metroids are now marked "working".
- **Take Screenshot** on the pause menu's Game page: both screens exactly as displayed (scaling, LCD grid, overlays, screen order), stacked top to bottom in one PNG, saved to `/storage/roms/screenshots`.
- The map no longer dims in the Metroid (and Castlevania) games when a capture misses, e.g. while running through rooms quickly. Only A Link to the Past dims it, where L really has no map to show.
- Zero Mission map: the "SELECT / CHANGE AREAS" prompt (once you've been to more than one area) is covered by a raised piece of the bezel bar, which is now always drawn so the frame looks the same either way.
- Metroid Fusion map: the "A OBJECTIVE" prompt is left out of the capture instead of painted over, so the rooms under it show properly on a well-explored map.
- README: a Roadmap section.

## v1.0.1 (2026-10-13)

- **Cheats**: RetroArch `.cht` files (Code Breaker, GameShark, Action Replay) from RetroArch's cheats folder, switched on and off from a new Cheats screen on the pause menu's Game page.
- Game list: a small USA / Europe / Japan flag in the corner of a game's tile when you have the same game from more than one region.
- **European and Japanese releases** of all six games: map, title logo and Castlevania border. The Japanese Aria of Sorrow, Harmony of Dissonance and Metroid Fusion keep their state at slightly different memory addresses; the European multi-language releases start at a language menu, which the logo and border extraction now gets past.
- Title logos: a fix for games that don't rewrite their display settings every frame (Circle of the Moon JP came out blank).
- New **Map Animations** option (pause menu → Display, or `map_animations` in gba-ds.cfg). Off shows a still map: each blinking or pulsing marker is frozen where it's most visible (Fusion's white Samus square, the Castlevania glows at their brightest, Link and the red X in A Link to the Past), and the still only changes when the map itself does.

## v1.0 (2026-10-13)

First release. GBA DS is a Game Boy Advance emulator for dual-screen handhelds (built for the Anbernic RG DS Plus on ROCKNIX) that keeps the game's own map on the second screen, the way the DS Castlevanias do.

**Games with a live map**
- Castlevania: Aria of Sorrow, Harmony of Dissonance, Circle of the Moon (USA). Circle of the Moon also shows its completion rate, copied from the pause screen.
- Metroid: Zero Mission and Metroid Fusion (USA): the whole pause screen with its frame, lettering and button prompts erased, and the HUD mini-map hidden.
- The Legend of Zelda: A Link to the Past (USA): the zoomed-out world map outdoors, dungeon maps once you have them; dimmed where no map is available. Four Swords is left off the list (it needs linked GBAs).
- Every other GBA game plays normally on one screen.

**App**
- Game list of logo tiles pulled from each ROM; touch or D-pad.
- Pause menu (tap the screen or HOME): save / load state, restart, swap screens, scaling (pixel perfect / full width / stretch), LCD grid, overlays, FPS counter.
- Automatic resume: leaving a game saves where you were.
- Overlays: generated map surrounds, plus your own PNGs for the primary (game) and secondary (map) screens, per game or for every game.
- Everything game-specific (logos, map borders) is extracted from your ROMs; no game art is shipped.

**Install:** unzip `gba-ds-v1.0-rocknix-aarch64.zip` into `/storage/roms/ports/`, keep unzipped `.gba` ROMs in `/storage/roms/gba/`, and launch **GBA DS** from Ports. See the README for details and for upgrading from the old `gbadual` builds.

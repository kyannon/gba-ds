# Changelog

## v1.4.4 (not yet released)

### New games
- **Spider-Man: Mysterio's Menace** (Spider-Man: Mysterio no Kyoui in Japan): in a level, the second screen shows the Select menu (the objective, password, suits, upgrades and web containers), updated about once a second. At the title, the intro and between levels it shows the logo. The USA / European and Japanese releases have been tried in the first level.
- **Summon Night: Swordcraft Story 2** (Craft Sword Monogatari 2 in Japan): while you walk about, the second screen shows the Status screen (level, HP, experience and your stats with each weapon), updated about once a second; in a conversation or the game's own menus the last one stays. At the title it shows the logo. The USA and Japanese releases have been tried at the start of the story.

### Supported games
- Final Fantasy Tactics Advance, Advance Wars, Advance Wars 2, Game Boy Wars Advance 1+2, Fire Emblem, Summon Night: Swordcraft Story, Golden Sun, Tactics Ogre, Pokémon Pinball, F-Zero: GP Legend and River City Ransom EX are no longer marked preliminary: they've been tested as much as the other supported games.

### Game list
- For a game without second-screen support, "Single screen only: plays normally" is now at the top right, a little bigger, clear of the GitHub address.

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

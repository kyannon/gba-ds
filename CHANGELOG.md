# Changelog

## v1.2.2 (not yet released)
- **Four more games in the list, second screens to come:** Tactics Ogre: The Knight of Lodis, Fire Emblem, Advance Wars and Mega Man Battle Network (with their regional releases). Until their second screens are ready, the second screen shows the game's logo (Tactics Ogre, USA: taken from its title screen) or its name.
- **Advance Wars (USA), preliminary:** in battles, the second screen shows the game's unit list, with the unit under the cursor highlighted (it stays while you move it), or the battle intel when the cursor isn't on one of your units, with the minimap, the CO and day plate and the terrain and unit info along the bottom (the plate and the info move off the game screen). The logo is taken from the title screen.
- **Fire Emblem (USA), preliminary:** the second screen is always the game's logo, small and dimmed, on a dark background. On the map, the terrain and goal windows move off the game screen to the second screen's bottom left and bottom centre, with the minimap at the bottom right. With the cursor on a unit, its status screen (the one R opens) is at the top left, turning through its three pages every 5 seconds; a unit's menu (Attack, Wait...) and the battle forecast (with its arrows and x2) go to the top centre. Conversations (with the portraits along the bottom), the tutorial's boxes and the ENEMY PHASE / PLAYER PHASE banners (with their sparkles, the game screen dimming meanwhile) move to the second screen; a unit's own small speech bubble stays on the map.
- **No sound on some systems (reported on ROCKNIXDS 1.6.4):** GBA DS finds the sound server's connection itself when the system doesn't pass it on, and tries the other ways of reaching it (PipeWire, PulseAudio, ALSA) when the first fails. `gba-ds.log` now says how the sound goes out, for reports.
- **The RG DS Plus's Stock Linux OS, experimental:** started from its Ports menu, GBA DS finds the GBA ROM folder next to the ports folder (or on either card) and switches the touchscreen on while it runs. The built-in buttons are mapped, HOME and BACK both open the menu, and sound goes through the system's sound service. Its volume keys set GBA DS's own volume (shown on screen and kept between games); the volume set in the system's menu doesn't carry over, and once in a while a game plays without sound (so far, the next start has had sound again). Settings has no EmulationStation option there, since the system doesn't use it. See the new install page in the wiki.
- **GBA DS closing straight back to Ports with no log:** GBA DS now starts even when its files lost their permissions on the way to the device (unzipped on a computer, then copied over a network share or MTP), and on cards that don't let programs run it runs from a copy in memory. Either way the log says so.
- **Map Buttons (Settings, on Linux):** GBA DS asks for each button in turn (tap to skip ones a device doesn't have) and uses the result straight away, kept for that controller. The log has the mapping, so it can be built into GBA DS for everyone with that device.
- **Mega Man Battle Network (USA), preliminary:** the START menu and every menu it opens (Folder, Library, MegaMan, Email, Items, Network, Save) appear on the second screen, without the paused world behind them, while the game screen keeps the world as it was when you opened them. In battles, the Custom screen's chip selection moves to the second screen (Mega Man's HP stays at the top of the battle), and when a battle ends, the ENEMY DELETED banner and the results screen show there too. Elsewhere, the second screen shows the game's logo, taken from the title screen.
- **Advance Wars and Fire Emblem:** during a fight, the second screen shows the game's logo, small and dimmed, on the dark background.

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

## The game list

GBA DS opens on a game list on the touchscreen (the bottom screen on the RG DS Plus), with the selected game's logo and resume picture on the other screen.

- **Two views:** a list of names (the default) or a two-column grid of the games' logos. Switch with **X** or the button left of **Settings**; GBA DS remembers which you chose.
- **Names** come from the file names, without tags like `(USA)` or `(En,Fr,De)`. When you have more than one regional copy of a game, a flag tells them apart. `name_tags = 1` in `gba-ds.cfg` keeps the tags.
- **Which games:** only games with second-screen support are listed (see [[Supported games]]). `show_all = 1` lists every GBA ROM; the others play normally on one screen.
- **The other screen** shows the selected game's logo, and the picture where it will resume once you've played it.
- The list is rebuilt from the ROM folder each time it opens, so games you add or remove show up with nothing to re-run.
- **First run:** each game's logo (and the Castlevanias' map border art) is pulled out of the ROM in the background, about a minute on the RG DS Plus, once. Tiles say "Loading thumbnail..." until their logo is ready. If you start a game before that's done, a "Preparing games" screen waits for it (B goes back), so the game doesn't start with crackling sound.

| Input | Action |
|---|---|
| D-pad | Move |
| L1 / R1 | Page up / down |
| A or Start, or tap a game | Play |
| Drag | Scroll |
| X, or tap the button left of Settings | Switch between list and grid |
| SELECT, or tap **Settings** | Settings (ROCKNIX: GBA DS in EmulationStation; BananaOS: GBA DS in its GBA list; Linux: Map Buttons; Android: ROM folder, RetroAchievements sign-in, Map Buttons) |
| B or HOME | Back to EmulationStation / the launcher |

## Resume points

Leaving a game saves its state automatically, and launching it again picks up exactly there; the game list shows where. `auto_resume = 0` turns this off. Battery saves (in-game saves) are written as usual too, so they're never at risk.

## The pause menu

Tap the touchscreen, or press **HOME** (Android: **SELECT + START** together, or **BACK**). Both screens dim, the game and its sound pause, and the menu appears on the touchscreen. It has four pages; switch with **L1 / R1** or by tapping the tabs. While the menu is open, the top screen shows the time (24-hour, from the device's clock) at the top left and the battery level at the top right. The menu works by touch or with the D-pad and A; B, Start or HOME closes it.

**Game**
- **Resume**
- **Restart Game**: tap twice; unsaved progress is lost.
- **Save State** / **Load State**: one slot per game (Load is greyed out until you've saved). HOME + R1 / L1 do the same during play.
- **Close Game** (back to the game list) and **Close GBA DS**: both save a resume point first.

**Display** (remembered for next time)
- **Swap Screens**: game and second screen trade places (also **L3**, or HOME + Y).
- **Scale**: Pixel Perfect (sharp, whole-number scale with a small border), Full Width (fills the width, keeps the shape, stays sharp), Zoom (fills the height, cutting off the left and right edges) or Stretch (fills the whole screen).
- **LCD Grid**: a pixel grid like a GBA screen's, on both screens. `lcd_strength` sets how dark.
- **Overlays**: primary / secondary / both / none. See [[Map screen surrounds and overlays]].
- **No Second Screen: Show Logo / Keep Blank**: what the second screen shows when the game has nothing for it (title screens, menus, before the first map): the game's logo, or nothing.
- **Frame Buffer**: off by default. On, the next frame is prepared while the current one is on screen, which smooths out the odd stutter at the cost of one frame (about 17 ms) of input delay.
- **Match Display Rate**: off by default. On, the game runs at the screen's refresh rate (60 Hz: 0.5% faster than a GBA, too little to notice, with the sound adjusted to match) and each frame is shown for exactly one refresh. Off, a 60 Hz screen shows one frame twice about every 3.7 seconds: a small, regular stutter.

**Extras** (Rewind and Show FPS are remembered for next time)
- **Cheats**: see [below](#cheats).
- **Achievements**: when signed in to RetroAchievements; see [below](#retroachievements).
- **Take Screenshot**: both screens as they look right now, with your display settings, stacked top to bottom into one PNG in `/storage/roms/screenshots` (Android: `GBA DS/screenshots`); `screenshot_dir` changes the folder.
- **Rewind**: on by default. Hold **L2** to go back in time, up to 30 seconds (`rewind_seconds`), at double speed and without sound; let go to play on from there. Off saves the little work it takes to keep the buffer.
- **Show FPS**: frames per second and % of full speed, above the game.

## Buttons

| Button | Action |
|---|---|
| D-pad / left stick | D-pad |
| Right face button | GBA A (`ab_swap = 1` flips A and B) |
| Bottom face button | GBA B |
| L1 / R1 | GBA L / R |
| Start / Select | Start / Select |
| HOME, or tap the touchscreen | Pause menu (Android: SELECT + START, or BACK) |
| L2 (hold) | Rewind |
| L3 (press the left stick) | Swap screens |
| R2 (hold) | Fast-forward (`ff_speed`, default 3×) |
| HOME + R1 / L1 | Quick save / load state |
| HOME + Start | Quit |
| HOME + X | FPS counter |
| HOME + Y | Swap screens |

If HOME doesn't open the menu on your device, see `home_button` in [[Settings]]; `menu_combo = select+start` also makes SELECT + START open it.

## Mapping the buttons

If buttons do the wrong thing on your device (or a controller isn't recognised), open **Settings** in the game list (tap it, or SELECT) and choose **Map Buttons** (every system, Android included). GBA DS asks for each button in turn, by where it sits on a GBA-style layout:

- the D-pad's four directions;
- **A** (the right face button), **B** (bottom), **X** (top), **Y** (left);
- **L**, **R**, **L2**, **R2**, **Select**, **Start**, **Home** (the button you want to open GBA DS's menu), **L3** (pressing the left stick in);
- the left stick, pushed up and then left.

Press each one when it's asked for. **Tap the touchscreen to skip** a button your device doesn't have (without a touchscreen, each one is skipped after 15 seconds), or tap **Cancel** to stop and keep the old mapping. A button that's already been given to something else isn't taken twice. Volume keys and buttons that arrive as keyboard keys can't be mapped.

The new mapping works straight away and is kept in `states/controllers.txt` for that controller. The log (`gba-ds.log`) has the same line: send it in an issue and it can be built into GBA DS for everyone with that device and system. Running Map Buttons again replaces it; deleting `controllers.txt` goes back to the built-in mappings.

**Four-button layout (Tony Hawk's Pro Skater 2 and 3).** The GBA version uses L and R for two of the console games' four face buttons. Tap a layout under **Controls** at the bottom of the second screen (when it's the touchscreen).:
- **GBA**: the layout above.
- **Four buttons**: like the console games. Top = GBA A (grind), bottom = GBA B (ollie), left = GBA L (flip), right = GBA R (grab). L1 / R1 still work as L / R.
- **Auto**: four buttons while you're skating, the GBA layout everywhere else (menus, pause, goal sheets, results), so A and B confirm and go back as the game expects. The tile for the layout in use right now is outlined.

**SNES layout (Super Mario Advance 2).** Under **Controls**, **SNES** gives Super Mario World the Super Nintendo's buttons in levels:
- bottom (B) jumps, left (Y) runs and carries, right (A) and top (X) spin-jump;
- **L1 pans the camera left and R1 pans it right**, as on the SNES, without moving Mario. (In the GBA version L pans the way Mario faces.)

Mario keeps facing the way he was while the camera pans; walking ends the pan, as on the SNES. While the game is paused, on the world map, in the SELECT menu and the menus, the buttons are the GBA's (A enters a level and confirms, R switches between Mario and Luigi). **GBA** keeps the GBA layout everywhere.

The choice is remembered per game.

## Cheats

GBA DS reads RetroArch's cheat files (`.cht`, the format of the [libretro cheat database](https://github.com/libretro/libretro-database/tree/master/cht/Nintendo%20-%20Game%20Boy%20Advance)).

- **Where:** GBA DS's own `cheats` folder (`gba-ds/cheats`, on ROCKNIX and BananaOS), then RetroArch's cheats folders: on ROCKNIX `/config/retroarch/cheats` and `/storage/.config/retroarch/cheats`, on BananaOS `/data/retroarch/cheats`. Subfolders such as `Nintendo - Game Boy Advance/` are searched too (`cheat_dir` changes RetroArch's folders, `:`-separated). Android: `GBA DS/cheats` on the internal storage (subfolders are fine).
- **Which file:** picked by name, so `Metroid Fusion (World) (Code Breaker).cht` goes with `Metroid Fusion (USA).gba`. The bracketed parts don't have to match, except the region: a file marked `(USA)` isn't offered for a Japanese ROM. The Japanese Aria of Sorrow, Harmony of Dissonance and Metroid Fusion keep their data at different addresses from the other releases, so for those only files that say `(Japan)` are used, not `(World)` ones.
- **Using them:** pause menu → Extras → **Cheats** lists them with an On/Off switch each: D-pad and A, L1 / R1 a page at a time, B to go back; or tap a cheat to switch it, and drag to scroll. A long name is shown in full at the bottom.
- Code Breaker files usually start with an "Enable Code (Must Be On)" cheat: switch it on along with the others, as on the real device.
- Choices take effect immediately and are remembered per game (`states/<game>.cheats`); a file's own `cheatN_enable` values are used until you change something.
- Cheats can crash or soft-lock a game, and anything they change is kept in your saves. Make a save state first.

## RetroAchievements

GBA DS earns [RetroAchievements](https://retroachievements.org) in **casual (softcore) mode**.

- **Signing in:**
  - ROCKNIX: GBA DS uses the account signed in under EmulationStation's RetroAchievements settings, when RetroAchievements is switched on there. `ra_username` and `ra_password` in `gba-ds.cfg` use another account.
  - Android: the game list's **Settings** → **RetroAchievements: Sign In**, with your retroachievements.org username and password. **Sign Out** forgets the account.
  - Either way, after the first sign-in GBA DS keeps a token (`states/ra/login.cfg`), not your password.
- **While playing:** each game shows your progress on the second screen as it starts ("2 of 30 achievements unlocked"). Unlocks pop up there too, with the badge and points, out of the game's way.
- **Achievements** (pause menu → Extras) lists the game's achievements, grouped as recently unlocked, almost there, locked and unlocked. Each has its badge, description and points, plus a progress bar for those that count something, and your totals are at the top. D-pad or drag to scroll, L1 / R1 a page at a time, B back.
- **Save states** keep the achievements' progress beside them (`<state>.ra`), so loading an old state doesn't unlock anything by mistake. Cheats are allowed in casual mode.
- **Offline**, the game plays on; unlocks earned while the connection is down are sent once it's back.
- **Hardcore mode** isn't available yet (RetroAchievements requires an emulator to have been public for six months first). With hardcore on in EmulationStation, GBA DS still plays in casual mode.
- `retroachievements = 0` in `gba-ds.cfg` turns it off. Badges are cached in `states/ra/badges/`.

## Logos

Until the second screen has something to show (title screen, intro, menus), it shows the game's logo. No logo files are shipped: the first time a game appears in the list, GBA DS boots it silently in the background, runs it to its title screen and renders just the layer that holds the logo, at the game's own resolution with transparency. It's saved as `states/logos/<CODE>.png`. The logo is drawn like a second GBA screen: centred in a GBA-sized picture, with the second screen's scaling and the LCD grid, so it matches the game beside it. **No Second Screen: Keep Blank** (pause menu → Display) leaves the second screen black instead.

To use your own art instead, put `<CODE>.png` (or `.bmp`) in `gba-ds/logos/` (Android: `GBA DS/logos/`). Art up to 240×160 is treated like the extracted logos; bigger images are fitted, smoothly, to where the GBA picture would be (without the grid). `<CODE>` is the game's four-letter code, listed in [[Supported games]].

## Where files go

| What | ROCKNIX | Android |
|---|---|---|
| Battery saves (`.sav`) | next to the ROM (`save_dir` changes it). On first launch an existing RetroArch `Game.srm` is copied to `Game.sav`; the `.srm` is left alone. | next to the ROM |
| Save states, resume points (`.state`, `.auto.state`, with `.ra` beside them) | `/storage/roms/ports/gba-ds/states/` (`state_dir`) | `GBA DS/states/` |
| Menu settings (`prefs.cfg`), game list settings (`library.cfg`) | `gba-ds/states/` | `GBA DS/states/` |
| Cheat choices (`<game>.cheats`) | `gba-ds/states/` | `GBA DS/states/` |
| Extracted logos and map borders | `gba-ds/states/logos/`, `gba-ds/states/frames/` | `GBA DS/states/…` |
| RetroAchievements sign-in and badges | `gba-ds/states/ra/` | `GBA DS/states/ra/` |
| Your logos and overlays | `gba-ds/logos/`, `gba-ds/overlays/` | `GBA DS/logos/`, `GBA DS/overlays/` |
| Screenshots | `/storage/roms/screenshots/` | `GBA DS/screenshots/` |
| Settings | `gba-ds/gba-ds.cfg` | `GBA DS/gba-ds.cfg` (optional) |
| Log | `gba-ds/gba-ds.log` | `GBA DS/gba-ds.log`, `GBA DS/launch.log` |

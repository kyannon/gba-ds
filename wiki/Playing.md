## The game list

GBA DS opens on a game list on the touchscreen (the bottom screen on the RG DS Plus), with the selected game's logo and resume picture on the other screen.

- **Three views:** a list of names (the default), a two-column grid of the games' logos, or a three-column grid (smaller logos, more games on screen). Each press of **X** moves on to the next view, and so does the square button left of the **Supported** / **All** button, whose picture shows what the next press gives; GBA DS remembers which you chose.
- **Names** come from the file names, without tags like `(USA)` or `(En,Fr,De)`. When you have more than one regional copy of a game, a flag tells them apart. `name_tags = 1` in `gba-ds.cfg` keeps the tags.
- **Which games:** the button between the view button and **Settings** says **Supported** (only games with second-screen support, see [[Supported games]]) or **All** (every GBA ROM; the others play normally on one screen). Touch it to switch; GBA DS remembers it (`show_all`).
- **The other screen** shows the selected game's logo, and the picture where it will resume once you've played it.
- The list is rebuilt from the ROM folder each time it opens, so games you add or remove show up with nothing to re-run.
- **First run:** each game's logo is pulled out of the ROM in the background, once. The selected game and the ones on screen go first. Tiles say "Loading thumbnail..." until their logo is ready. You don't have to wait for all of them: if you start a game before its own logo is ready, a "Preparing the game" screen waits for just that one (a few seconds; B goes back). While you play, the rest wait, so the game doesn't start with crackling sound; they carry on when you're back in the game list.

| Input | Action |
|---|---|
| D-pad | Move |
| L1 / R1 | Page up / down |
| A or Start, or tap a game | Play |
| Drag | Scroll |
| X, or tap the button left of Settings | Next view: list, two columns, three columns |
| SELECT, or tap **Settings** | Settings (ROCKNIX: GBA DS in EmulationStation; BananaOS: GBA DS in its GBA list; Controls has Map Buttons and Hotkeys; System has [Save States](#save-states-shared-with-retroarch), RetroArch's State Folder, [GBA BIOS](#a-state-saved-with-a-gba-bios) and Updates (Linux) or Folders, RetroAchievements sign-in, GBA BIOS and Updates (Android)) |
| B or HOME | Back to EmulationStation / the launcher |

## Resume points

Leaving a game saves its state automatically, and launching it again picks up exactly there; the game list shows where. **Save on Quit** and **Auto-Load** on the pause menu's Extras page switch each half off (`save_on_quit = 0`, `auto_load = 0` in `gba-ds.cfg`). With Save on Quit off and Auto-Load on, the game picks up from the last resume point that was saved. Battery saves (in-game saves) are written as usual too, so they're never at risk.

## Save states shared with RetroArch

On ROCKNIX, BananaOS and other Linux systems, GBA DS can share its save states with RetroArch, so you can save in one and go on in the other. In the game list, open **Settings** (SELECT) > **System** and choose **Save States** to switch between **GBA DS's Own** (the default) and **Shared with RetroArch**. **RetroArch's State Folder**, under it, says which folder RetroArch keeps its states in, and lets you choose it yourself, if GBA DS didn't find RetroArch or you keep its states somewhere else: open folders with A (or a tap), go up with B, and choose **Use This Folder**. It says how many RetroArch states are in the folder you're looking at. **Automatic** goes back to the folder RetroArch's settings give.

When they're shared:

- The pause menu's slots 0 to 9 are RetroArch's slots 0 to 9, and the resume point is RetroArch's automatic state (its `<game>.state`, `.state1` to `.state9` and `.state.auto`). There's one copy of each, in RetroArch's folder, so nothing has to be copied across and a folder you sync to other devices just works.
- GBA DS saves them as RetroArch does (compressed, unless RetroArch's "Save State Compression" is off), with a thumbnail beside each for RetroArch's menu.
- A slot's label says **RetroArch** when it was saved there (for example "Slot 3: Nov 14, 5:27 PM, RetroArch").
- RetroArch's Game Boy Advance core has to be **mGBA**, since GBA DS is mGBA inside. A state saved with another core (gpSP, VBA-M...) can't be loaded: its slot says "Another emulator's". On BananaOS, whose own choice is gpSP, set it in Settings > Other systems > Emulators (BananaFront, which BananaOS often plays GBA games in, uses the same core and the same state files). On ROCKNIX, in EmulationStation: the game's Advanced Game Options > Core, or the system's advanced settings.
- The quick state (the quick bar's, HOME + R1 / L1) stays GBA DS's own.
- A game with patches on and its own save states (see [Patches](#patches)) keeps them in GBA DS's folder, as RetroArch has none for it.
- GBA DS's own states stay in its folder while sharing is on, for when you switch back.
- RetroAchievements progress isn't shared: each app keeps its own with its states.

GBA DS works out RetroArch's state folder from RetroArch's own settings, the way RetroArch does: on ROCKNIX `/storage/roms/savestates/gba`, on BananaOS `/data/states/mGBA`. If RetroArch is somewhere GBA DS doesn't look, choose its folder with **RetroArch's State Folder**.

States RetroArch compressed with zstd (RetroArch 1.21 and later can) load like the others.

### A state saved with a GBA BIOS

If RetroArch's mGBA core has a GBA BIOS file set, its states can't go on in GBA DS, which runs games with a built-in BIOS, when the state was saved while the game was inside the BIOS (waiting for the next frame, say, which is much of the time). Loading one says "Saved with a GBA BIOS". To load them, put your own BIOS (the 16 KB file, usually `gba_bios.bin`) in the `bios` folder of GBA DS's data folder, named `gba_bios.bin`, and in the game list's **Settings** > **System** choose **GBA BIOS: Your Own**. GBA DS never looks for a BIOS by itself, and with this left on **Built-in** (the default) nothing is different. The BIOS applies to games you start after switching it. States saved without a BIOS may then not load while it's on: switch it back to **Built-in** for them. GBA DS doesn't come with a BIOS.

## Updates

GBA DS can update itself on ROCKNIX, BananaOS, the Stock Linux OS and Android. It needs the device to be online.

- **Automatic checks:** once a day, when the game list opens, GBA DS asks GitHub in the background whether there's a newer release (pre-releases are never offered). If there is, it shows what's new and asks: **Update now**, **Later** (asked again the next day) or **Skip this version** (not offered again; a newer one still is). Nothing is downloaded unless you choose Update now.
- **By hand:** game list > **Settings** > **Updates** shows your version and when GBA DS last checked, with **Check now** and **Automatic checks: On / Off**.
- **Update now** downloads the release's zip (B or **Cancel** stops it), checks it against the checksum GitHub publishes, installs it over the GBA DS folder and restarts into the new version. Everything in `gba-ds/states/` is kept, and so is your `gba-ds.cfg` (a new version's options keep their defaults until you add them; see [[Settings]]).
- The **GBA DS** entry in Ports (`GBA DS.sh`) and its image are updated too.
- Updating from v1.5.3 or earlier: some of the new version's changes (in how GBA DS is started) only count once you quit GBA DS and start it again from Ports. From v1.5.4 on, the restart after an update takes care of that.
- **On Android**, Update now downloads the release's APK, checks it the same way and hands it to Android's installer, which asks you to confirm (the first time, Android may ask you to allow GBA DS to install apps: allow it, then go back). Android closes GBA DS while it installs; open it again afterwards. Your games, saves, states and settings are kept. If Android says the app is signed differently, your copy is a test build: uninstall it and install the new APK from the [Releases page](https://github.com/kyannon/gba-ds/releases) by hand (the files in the `GBA DS` folder stay).

## The pause menu

Tap the touchscreen, or press **HOME** (Android: **SELECT + START** together, or **BACK**). Both screens dim, the game and its sound pause, and the menu appears on the touchscreen. It has three pages; switch with **L1 / R1** or by tapping the tabs. While the menu is open, the top screen shows the time (24-hour, from the device's clock) at the top left and the battery level at the top right. The menu works by touch or with the D-pad and A; B, Start or HOME closes it.

**Game**
- **Resume**
- **Restart Game**: asks first (**Are You Sure?**; choose it again to restart); unsaved progress is lost.
- **Save State 0** / **Load State 0**, side by side (LEFT / RIGHT moves between them): into and from the slot picked below them, whose number they show (Load is greyed out while that slot is empty).
- **‹ Slot 0 ›** to **‹ Slot 9 ›**: ten save slots per game, each showing when it was saved (or Empty). LEFT / RIGHT on it, or a tap on its left or right end, picks another. The menu opens on the slot you saved to last.
- A state also keeps what the second screen was showing (a map, a status screen, Sonic Advance's zone card), so loading it brings that back at once; states saved before v1.4.4 load as before, and the second screen catches up with its next capture.
- **Close Game** (back to the game list) and **Close GBA DS**: both ask first, like Restart Game, and save a resume point before closing (while Save on Quit is on). Moving to another button calls the question off.

**Display** (remembered for next time)
- **Scale**: Pixel Perfect (sharp, whole-number scale with a small border), Full Width (fills the width, keeps the shape, stays sharp), Zoom (fills the height, cutting off the left and right edges) or Stretch (fills the whole screen).
- **Shader**: a pattern over every pixel, on both screens; each press picks the next. **Off**; **LCD Grid** (lines between the pixels, like a GBA's screen); **Scanlines** (a dark line under each row of pixels); **Soft Cells** (each pixel a little darker toward its edges); and three strengths of **Subpixels**, the red, green and blue stripes of a GBA's screen: **Light** for a dim screen or low brightness, the middle one, and **Strong** for a bright screen. The stripes darken the picture, so with them the colours are brightened to make up for it; the brightest colours can only go up to white, so they look a little darker than without. `lcd_strength` sets how dark the grid, scanlines and soft cells are. On the RG DS (2× scale), Soft Cells looks like the grid, and the subpixels have no room for their colours, so they're shown as a grey pattern.
- **LCD Ghosting**: off by default. On, each frame of the game is shown half and half with the one before, as a GBA's slow screen blurred them: moving things leave a short trail, and sprites that a game flickers every other frame to make them see-through look see-through, as they did on a GBA. The game screen only.
- **Color Correction**: off by default. On, the game's colours are shown as a Game Boy Advance's own screen showed them: darker and less saturated, as mGBA's GBA Color shader draws them (its sRGB profile, Darken Screen at its usual 0.5). On both screens: the game, and the maps, logos and status screens taken from it. Remembered for next time.
- **Overlays**: a screen of its own. Off, Generated (GBA DS's bezel round the picture, in colours you pick, with or without its logo) or Custom (your own PNGs in `overlays/`), and which screens have them. See [[Overlays]].
- **Frame Buffer**: off by default. On, the next frame is prepared while the current one is on screen, which smooths out the odd stutter at the cost of one frame (about 17 ms) of input delay. In the pinball games it's always on, needed for full speed, so it's greyed out there.
- **Match Display Rate**: off by default. On, the game runs at the screen's refresh rate (60 Hz: 0.5% faster than a GBA, too little to notice, with the sound adjusted to match) and each frame is shown for exactly one refresh. Off, a 60 Hz screen shows one frame twice about every 3.7 seconds: a small, regular stutter. Matched, a frame that comes too late for its refresh waits for the next one and the game slows down, so if that keeps happening (three seconds in a row), GBA DS goes back to the GBA's own rate for the rest of the game, and the menu shows **Match Display Rate: Off for now**. Choosing it turns it off; choosing it again tries again.
- **Custom Logos**: only for a game whose logo can be your own or a scraped one ([Logos](#logos)). **Pixelated** (the default) shrinks it to the GBA's pixels; **Full Detail** keeps its detail, at the same size; **Do Not Use** shows the logo GBA DS takes from the game instead.
- **Screen shapes** (experimental; only on devices whose two screens are different shapes, such as the AYN Thor and the Retroid Pocket Duo, greyed out elsewhere): **Keep device aspect ratios** (each screen's picture as its own shape allows), **Match screen aspect ratios** (the wider screen's picture takes the other screen's shape, with black bars at its sides) or **Match screen sizes and aspect ratios** (the same, at the other screen's real size, against the edge nearest it). With Stretch and either match, both screens show the same shape, so Pokémon Pinball's table carries on across them. Overlays can be made for each: see [[Overlays]].
- **Screen Placement**: **Centered** (the default: each screen's picture in the middle of its screen) or **Closest to Gap** (the top screen's picture against its bottom edge and the bottom screen's against its top, so the two are as close together as they go, as on a DS; Pokémon Pinball's table then carries on across the hinge without a jump). It moves the picture only where it doesn't fill the screen's height: Pixel Perfect and Full Width. With Closest to Gap, the Generated bezel has no room for its logo by the hinge, so it's shown once, on the bottom screen below the picture. Greyed out with one screen. Remembered for next time.

**Extras** (Swap Screens, Rewind, Fast Forward, Save on Quit and Auto-Load are remembered for next time)
- **Swap Screens**: game and second screen trade places (also HOME + D-pad up).
- **Cheats**: see [below](#cheats).
- **Patches**: romhacks and other patches for the game; see [below](#patches).
- **Achievements**: when signed in to RetroAchievements; see [below](#retroachievements).
- **Take Screenshot**: both screens as they look right now, with your display settings, stacked top to bottom into one PNG in `/storage/roms/screenshots` (Android: `GBA DS/screenshots`); `screenshot_dir` changes the folder.
- **Submit a Bug Report**: the same as the quick bar's bug button (see [below](#the-quick-bar-and-bug-reports)), for the moment you opened the menu.
- **Rewind: On / Off** and **Fast Forward** (its speed, or Off), side by side.
- **Rewind**: on by default. Hold **L2** to go back in time, up to 30 seconds (`rewind_seconds`), at double speed and without sound, with **Rewinding...** in the middle of the second screen (**Can't rewind further** once you reach the start of the buffer); let go to play on from there. Off saves the little work it takes to keep the buffer.
- **Fast Forward**: 3x by default. Hold **R2** to play that many times as fast, with **Fast forwarding...** in the middle of the second screen; all the other buttons still work meanwhile, so you can press A through text. Each press of the button goes on to the next speed: 2x, 3x, 4x, 5x, Max (as fast as your device can run the game; how fast that is depends on the device and the game, and the sound gets choppy), then Off (R2 does nothing, so a trigger pressed by accident can't skip you ahead), then 2x again. HOME + R2 fast-forwards too, the same way, if you're used to it from RetroArch.
- **Save on Quit: On / Off** and **Auto-Load: On / Off**, side by side: whether closing the game saves a resume point, and whether starting it picks up from one (see Resume points above). Remembered for next time.

## The quick bar and bug reports

Swipe up from the bottom edge of the touchscreen for the quick bar, or press **L3** (or HOME + X); press it again, or swipe down, to put it away. The game keeps running underneath it. The bar is drawn inside the second screen's picture at the GBA's own resolution (240x160, scaled up with the picture, in pixel letters), in your menu **Color** with the same raised buttons as the menus. In games with another button layout (the Tony Hawk's games, Super Mario Advance 2 and 3), **Controls** is at its top: see [Buttons](#buttons). In the Boktai games the top row is **Sun** instead: **Dark**, **-**, **+** and **Full** set the sunlight the cartridge's sun sensor reads, 0 to 10, with the level between the buttons. Under that are the time, the frames per second (about 59.7 is full speed) and the battery level, then five buttons, and under them a line for messages (State saved. and the like):
- **Save State** (floppy disk) and **Load State** (folder): the quick state, a slot of its own, apart from the pause menu's ten (Load is greyed out until you've saved one). HOME + R1 / L1 save and load it too.
- **Settings** (gear): the pause menu.
- **Bug Report** (bug): see below.
- **Close GBA DS** (walking out of a door): tap it twice; your resume point is saved as usual.

When a game starts, a hint at the bottom of the touchscreen says **Swipe up for quick menu**; it slides away after 3 seconds, or as soon as you touch the screen (`quick_bar_hint = 0` in `gba-ds.cfg` turns it off; with the bar pinned it isn't shown).

Swipe down, or tap anywhere off its buttons, to put the bar away (tap again for the pause menu). It also goes when the pause menu opens (HOME or your menu buttons), and after 5 seconds untouched.

**The pin** (top right of the bar) keeps it up: tap it and it turns white. Pinned, the bar doesn't hide by itself, comes back when you leave the pause menu, and is up when you start a game; a tap outside it opens the pause menu. It stays pinned until you tap the pin again (`quick_bar_pinned` in `gba-ds.cfg`).

**Bug Report** (also in the pause menu: Extras > **Submit a Bug Report**) saves a bug report: a `.zip` in the `bug-reports` folder (`gba-ds/bug-reports/`, or `GBA DS/bug-reports/` on Android), named after the game and the date and time. First the game pauses and a keyboard comes up on the touchscreen for an optional note: what happened, and what you expected. Type it by touch, or with the D-pad and A (B deletes, X types a space, SELECT is Shift, START is Done); **Done** saves the report with the note, **Skip** (or B with nothing typed) without one. The report is of the moment you pressed the button, however long the note takes. It holds:
- a save state of the game right then;
- a save state from 10 seconds earlier (`-rewound`);
- every button the game got in between (`-keys.txt`): each press and lift, and how long it was held;
- your note, if you typed one, then your device and system, which ROM you're playing (whether it's an unchanged copy of the game or a patched or modified one, from its checksum), GBA DS's settings and the end of the log (`-info.txt`). Lines of the log that mention passwords or tokens are left out.

From these, what you did can be played back exactly, frame by frame. So when something goes wrong, press the bug button as soon as you can after it, and attach the `.zip` to [an issue on GitHub](https://github.com/kyannon/gba-ds/issues), saying what you expected to happen. The 10 seconds start again after you load a state, rewind or restart the game. The `bug-reports` folder also has a `README.txt` with these steps and what to write in the issue.

## Buttons

| Button | Action |
|---|---|
| D-pad / left stick | D-pad |
| Right face button | GBA A (`ab_swap = 1` flips A and B) |
| Bottom face button | GBA B |
| Top / left face buttons | Nothing (see [Changing the face buttons](#changing-the-face-buttons-for-a-game)) |
| L1 / R1 | GBA L / R |
| Start / Select | Start / Select |
| HOME, or tap the touchscreen | Pause menu (Android: SELECT + START, or BACK) |
| L2 (hold) | Rewind |
| L3 (press the left stick) | Quick bar (as swiping up; again to put it away) |
| R2 (hold) | Fast-forward (default 3×; Fast Forward in Extras sets the speed or turns it off). HOME + R2 too |
| HOME + R1 / L1 | Quick save / load state (the same state as the quick bar's buttons) |
| HOME + Start | Close GBA DS |
| HOME + B | Close the game (back to the game list) |
| HOME + X | Quick bar (it shows the FPS too) |
| HOME + D-pad up | Swap screens |
| HOME + D-pad down | Toggle the selected shader (off, and back on) |
| HOME + A | Turn the second screen's page or view (Settings > Second screen > Contextual toggle, off by default; greyed out as not available in games whose second screen has nothing to turn) |

If HOME doesn't open the menu on your device, see `home_button` in [[Settings]]; `menu_combo = select+start` also makes SELECT + START open it.

### Hotkeys

The HOME + button shortcuts above are hotkeys: hold the hotkey button, then press the other one. To change them, open **Settings** in the game list and open **Controls** and choose **Hotkeys** (under Map buttons):

- **Hotkey button**: HOME (the default), SELECT, START or L3. With SELECT or START, the button still reaches the game when you tap it on its own; with L3, a tap on its own still opens the quick bar. HOME opens the menu whichever is the hotkey.
- **Close emulator** (closes GBA DS), **Close game** (back to the game list), **Quick save state**, **Quick load state**, **Open quick menu** (the quick bar), **Swap screens** and **Toggle selected shader** (off, and back on to the one picked in the pause menu's Shader) and **Turn the second screen** (with the contextual toggle on): the button for each.

Pick a row (tap it, or the D-pad and A), then press the button you want for it. Tapping the touchscreen instead gives a shortcut no button (or keeps the hotkey button as it was). A button already used by another shortcut moves to the new one, and the other is left without. **Reset** goes back to HOME and the buttons above; **Done** (or B) keeps your choices for every game, in `states/hotkeys.cfg`. They can also be set in `gba-ds.cfg` (see the Buttons section of [[Settings]]), but once you've used Hotkeys in Settings, its choices win until you delete `hotkeys.cfg`. On Android, HOME is the BACK button (Android keeps HOME for itself), and Hotkeys calls it Back.

When GBA DS was started straight into a game, by a launcher or a frontend such as EmulationStation, there is no game list to go back to: **Close game** then closes GBA DS too, so both shortcuts do the same. With START as the hotkey, closing GBA DS is START + SELECT. With SELECT or START as the hotkey, the buttons you've given to shortcuts don't reach the game while the hotkey is held; the others do. If `menu_combo = select+start` is on too, SELECT + START opens the menu rather than doing a hotkey.

## Performance

On Linux (ROCKNIX, the Stock Linux OS and others; not BananaOS, which sets its own), game list > **Settings** > **Performance** chooses how fast the device runs while GBA DS is open:

- **Let the System Decide** (the default): the system sets the processor, memory and graphics speed, as it does for its other emulators. It slows them down between frames to save battery.
- **Set to Max**: all three run at full speed while GBA DS is open, and go back to the system's choice when it closes. Try it if games stutter or the sound breaks up on your device; the battery runs down faster and the device gets warmer.

## Mapping the buttons

If buttons do the wrong thing on your device (or a controller isn't recognised), open **Settings** in the game list (tap it, or SELECT) and open **Controls** and choose **Map buttons** (every system, Android included). GBA DS asks for each button in turn, by where it sits on a GBA-style layout:

- the D-pad's four directions;
- **A** (the right face button), **B** (bottom), **X** (top), **Y** (left);
- **L**, **R**, **L2**, **R2**, **Select**, **Start**, **Home** (the button you want to open GBA DS's menu), **L3** (pressing the left stick in);
- the left stick, pushed up and then left.

Press each one when it's asked for. **Tap the touchscreen to skip** a button your device doesn't have (without a touchscreen, each one is skipped after 15 seconds), or tap **Cancel** to stop and keep the old mapping. A button that's already been given to something else isn't taken twice. Volume keys and buttons that arrive as keyboard keys can't be mapped.

The new mapping works straight away and is kept in `states/controllers.txt` for that controller. The log (`gba-ds.log`) has the same line: send it in an issue and it can be built into GBA DS for everyone with that device and system. Running Map Buttons again replaces it; deleting `controllers.txt` goes back to the built-in mappings.

### Changing the buttons for a game

Pause menu → **Controls** tab sets which GBA button each button gives (**A (right) button** and so on; A for this game, Y for all games): **A (Right)**, **B (Bottom)**, **X (Top)**, **Y (Left)**, **L**, **R** and the four **D-pad** directions can each be GBA A, B, L, R, Up, Down, Left, Right, Start, Select, Turbo A, Turbo B or Nothing (LEFT / RIGHT, A or a tap to change one; B to go back). The list scrolls: the D-pad moves down it, or drag it with a finger. For example, B (Bottom) as GBA A and Y (Left) as GBA B puts jump on the bottom button and attack on the left one, as in many SNES games. Two buttons can give the same GBA button.

Changes work straight away and are kept in `states/<game>.cfg` for this game, or in `states/prefs.cfg` for all. **X** on a row (or a touch on its gold Game tag) goes back to the value for all games. GBA DS's own menus keep A to choose and B to go back, L1 / R1 stay GBA L / R, and the hotkeys don't change.

In games with a layout under **Controls** in the quick bar (below), the four-button or SNES layout decides the face buttons wherever it's in use, and your Buttons choices everywhere else.

**Four-button layout (Tony Hawk's Pro Skater 2 and 3).** The GBA version uses L and R for two of the console games' four face buttons. Swipe up from the bottom of the touchscreen for the [quick bar](#the-quick-bar-and-bug-reports) and tap a layout under **Controls**, at its top:
- **GBA**: the layout above.
- **Four buttons**: like the console games. Top = GBA A (grind), bottom = GBA B (ollie), left = GBA L (flip), right = GBA R (grab). L1 / R1 still work as L / R.
- **Auto**: four buttons while you're skating, the GBA layout everywhere else (menus, pause, goal sheets, results), so A and B confirm and go back as the game expects. The tile for the layout in use right now is outlined.

**SNES layout (Super Mario Advance 2).** Under **Controls** in the quick bar, **SNES** gives Super Mario World the Super Nintendo's buttons in levels:
- bottom (B) jumps, left (Y) runs and carries, right (A) and top (X) spin-jump;
- **L1 pans the camera left and R1 pans it right**, as on the SNES, without moving Mario. (In the GBA version L pans the way Mario faces.)

Mario keeps facing the way he was while the camera pans; walking ends the pan, as on the SNES. While the game is paused, on the world map, in the SELECT menu and the menus, the buttons are the GBA's (A enters a level and confirms, R switches between Mario and Luigi). **GBA** keeps the GBA layout everywhere.

**SNES layout (Super Mario Advance 3).** Under **Controls** in the quick bar, **SNES** gives Yoshi's Island the Super Nintendo's buttons in levels:
- bottom (B) jumps and flutters, left (Y) sticks out the tongue, right (A) and top (X) aim and throw an egg;
- L1 and R1 lock the aim.

START pauses and SELECT opens the status screen, as on the GBA. On the island map, a world's panel, in the bonus games and the menus, the buttons are the GBA's (A enters a level and confirms, B goes back). **GBA** keeps the GBA layout everywhere.

The choice is remembered per game.

## Cheats

GBA DS reads RetroArch's cheat files (`.cht`, the format of the [libretro cheat database](https://github.com/libretro/libretro-database/tree/master/cht/Nintendo%20-%20Game%20Boy%20Advance)).

- **Where:** GBA DS's own `cheats` folder (`gba-ds/cheats`, on ROCKNIX and BananaOS), then RetroArch's cheats folders: on ROCKNIX `/config/retroarch/cheats` and `/storage/.config/retroarch/cheats`, on BananaOS `/data/retroarch/cheats`. Subfolders such as `Nintendo - Game Boy Advance/` are searched too (`cheat_dir` changes RetroArch's folders, `:`-separated). Android: `GBA DS/cheats` on the internal storage (subfolders are fine).
- **Which file:** picked by name, so `Metroid Fusion (World) (Code Breaker).cht` goes with `Metroid Fusion (USA).gba`. The bracketed parts don't have to match, except the region: a file marked `(USA)` isn't offered for a Japanese ROM. The Japanese Aria of Sorrow, Harmony of Dissonance and Metroid Fusion keep their data at different addresses from the other releases, so for those only files that say `(Japan)` are used, not `(World)` ones.
- **Using them:** pause menu → Extras → **Cheats** lists them with an On/Off switch each: D-pad and A, L1 / R1 a page at a time, B to go back; or tap a cheat to switch it, and drag to scroll. A long name is shown in full at the bottom.
- Code Breaker files usually start with an "Enable Code (Must Be On)" cheat: switch it on along with the others, as on the real device.
- Choices take effect immediately and are remembered per game (`states/<game>.cheats`); a file's own `cheatN_enable` values are used until you change something.
- Cheats can crash or soft-lock a game, and anything they change is kept in your saves. Make a save state first.

## Patches

GBA DS applies patches (`.ips`, `.ups`, `.bps` and `.xdelta` files: romhacks, colour patches, translations) to a game as it starts, so the ROM itself stays as it is. An `.xdelta` made with compression (xdelta3's `-S` option) can't be read: patch the ROM with it beforehand instead.

- **Where:** each game has its own folder in GBA DS's `patches` folder (`gba-ds/patches`, on ROCKNIX and BananaOS; Android: `GBA DS/patches` on the internal storage; in the folder of your choice if you've moved GBA DS's files, see [[Install on ROCKNIX]] and [[Install on Android]]), named after the game and its region, for example `patches/Mario & Luigi - Superstar Saga (USA)`. GBA DS makes the folder the first time you play the game, so it's there to drop files into, whatever your ROM file is called. Name the patch files as you like: the name is what the list shows. `patch_dir` changes the folder.
- **Using them:** pause menu → Extras → **Patches** lists the game's patches with an On/Off switch each (D-pad and A, L1 / R1 a page at a time, B to go back, or tap one), and at the bottom what GBA DS makes of the selected one: a tested romhack (see [Supported games](Supported-games#romhacks-and-patched-roms)), untested, or made for another version of the game (that can't be switched on). Switching one on or off asks first ("Sure?"): save your game, then A restarts the game with the change, or Y leaves it for now so you can change more first. When the change can't upset your saves, the game doesn't restart: it carries on from where you were, with the change ("carry on from here", the question says). That's when the patched game is the same size as before and keeps the same battery save and save states: the two "separate" switches off, as for a colour patch. Colours already on the screen change as the game draws them again.
- **Patches GBA DS knows:** a list of colour restorations, voice removals and translations (see [Supported games](Supported-games#romhacks-and-patched-roms)) is built in, so the list names them whatever their file is called, says whether they're tested, and warns when one was made for another version of the game than your ROM.
- **Several at once:** you can switch on up to seven. They're applied in order, top to bottom: the ones that are on sit at the top of the list, numbered, and a patch you switch on goes last. To change the order, press X on one (or tap its number), move it with Up / Down (or tap where it should go), then press A. With two or more on, the list says what they make together, and warns you when they won't go together:
  - **Fails:** a `.ups`, `.bps` or `.xdelta` patch is made for the unchanged game, so it only works first in the list (and only one of those can be on). Move it up, or it's left out when the game starts.
  - **A name in gold:** that patch changes some of the same parts of the game as one above it, and wins there, so the two may not work together.
- **Restart:** when the patches aren't the ones the game started with (you pressed Y, or moved one), the top row of the list says so: A on it, then A again, restarts the game with them. Otherwise they're used the next time you start the game.
- **Saves:** a game with a patch on has its own battery save and save states by default, named `<game> + <patch>` (with several, all their names, whatever their order), so a romhack that changes the whole game never loads the original's. The two switches at the top of the list turn either off, for a patch that keeps the game's saves working (a colour patch, say).
- **Save states and patches:** a save state remembers which version of the game it was saved with. Loading one saved with different patches (or none, or another copy of the game) still works, but a note says so ("This state was saved without the patches on: it may not work"), because the game's insides can differ: if things look wrong, load another state or switch the patches back.
- **RetroAchievements** are off while a patch is on (they aren't allowed for patched games); the banner as the game starts says so.
- A patch named as the ROM, beside it (`Mario & Luigi.ips` beside `Mario & Luigi.gba`), the way RetroArch uses them, is always on: it shows in the list as "Always".
- A ROM you've patched yourself works as well, with no patch file.

### Known issues with patches

- An `.ips` patch can't say which version of a game it's made for. GBA DS warns when it knows the patch, but an unknown one made for another version is applied all the same and may break the game.
- A romhack with its own title screen still shows the original game's logo on the second screen.
- Two patches that change the same part of the game are only warned about (the one lower in the list wins there); whether they really clash can't be known until you play.
- Metroid: Zero Mission's colour patches work on their own; stacked with another patch, its map may not come up.
- After switching a colour patch without a restart, colours already on the screen stay until the game draws them again.
- Android: games started from a frontend that passes a link rather than a file get no patches.

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

Until the second screen has something to show (title screen, intro, menus), it shows the game's logo. No logo files are shipped: the first time a game appears in the list, GBA DS boots it silently in the background, runs it to its title screen and renders just the layer that holds the logo, at the game's own resolution with transparency. It's saved as `states/logos/<CODE>.png`. The logo is drawn like a second GBA screen: at its own size, centred on dark grey in a GBA-sized picture (at half strength while it's all there is to show), with the second screen's scaling and the LCD grid, so its pixels match the game beside it. Everything else GBA DS puts on the second screen (maps, status screens, windows) is drawn the same way, at the game's own size.

To use your own art instead, put `<CODE>.png` (or `.bmp`, `.jpg` or `.webp`) in `gba-ds/logos/` (Android: `GBA DS/logos/`). `<CODE>` is the game's four-letter code, listed in [[Supported games]].

**Scraped logos.** If a scraper has downloaded logos for your games (EmulationStation's marquees, Skraper's wheels), set `logo_folder` in `gba-ds.cfg` to that folder, for example `logo_folder = /storage/roms/gba/images`. GBA DS picks the image named after the ROM, whatever region or other details in brackets either name has: `Golden Sun.png`, `Golden Sun (USA).jpg` and `Golden Sun-marquee.png` all go with `Golden Sun (USA, Europe).gba`. If there are several, it prefers the ROM's exact name, then the plain title, then a marquee, wheel or logo, and PNG or WebP over JPG. WebP images work whatever their name ends in (some scrapers save them as `.png`). Point it at a folder of logos rather than one that mixes in box art, or the box art may be picked for a game with no logo. A JPG has no transparency, so its background shows as a box around the logo (the plain border outside it is cut off); a PNG or WebP looks better if your scraper offers one. A `logos/<CODE>.png` of your own still wins. If a logo you've added isn't used, GBA DS's log (`gba-ds.log`) and its bug reports say why on their `logo:` line: no file with a matching name, a folder that isn't there, or a file it can't read (GIF and animated WebP images aren't supported, whatever their name says).

Your own logos and scraped ones are made as tall as the logo GBA DS took from the game, so they sit in the same place and nothing else on the second screen moves. **Custom Logos** in the pause menu's Display page (there only when the game has one of these) switches between **Pixelated**, shrunk to the GBA's own pixels to look like the game drew it, **Full Detail**, at the same size, and **Do Not Use**, which goes back to the logo GBA DS takes from the game, for every game.

## Where files go

`gba-ds/` and `GBA DS/` below are GBA DS's folders as installed. Its own files (everything but the battery saves, screenshots on ROCKNIX and the settings file on ROCKNIX) can be kept somewhere else: `data_dir` on ROCKNIX and BananaOS (see [Settings](Settings#files-and-folders)), **Settings** → **Folders** → **Choose GBA DS folder** on Android.

| What | ROCKNIX | Android |
|---|---|---|
| Battery saves (`.sav`) | next to the ROM, or the folder you name in `save_dir` (see [Settings](Settings#files-and-folders); `save_ext = srm` keeps them as RetroArch's `.srm`). On first launch an existing RetroArch `Game.srm` is copied to `Game.sav`; the `.srm` is left alone. | next to the ROM (**Settings** → **Folders** changes it) |
| Save states (`<game>.quick.state` for the quick bar, `<game>.slot0.state` to `.slot9.state` for the pause menu's slots), resume points (`<game>.auto.state`), with `.ra` beside them | `/storage/roms/ports/gba-ds/states/` (`state_dir`); slots and resume points shared with RetroArch: RetroArch's state folder (see [above](#save-states-shared-with-retroarch)), their `.ra` in `states/retroarch/` | `GBA DS/states/` |
| Menu settings (`prefs.cfg`), game list settings (`library.cfg`) | `gba-ds/states/` | `GBA DS/states/` |
| Cheat choices (`<game>.cheats`), patch choices (`<game>.patches`) | `gba-ds/states/` | `GBA DS/states/` |
| Hotkeys (`hotkeys.cfg`), Map Buttons (`controllers.txt`) | `gba-ds/states/` | `GBA DS/states/` |
| Patches | `gba-ds/patches/<game>/` | `GBA DS/patches/<game>/` |
| Extracted logos | `gba-ds/states/logos/` | `GBA DS/states/…` |
| RetroAchievements sign-in and badges | `gba-ds/states/ra/` | `GBA DS/states/ra/` |
| Your logos and overlays | `gba-ds/logos/`, `gba-ds/overlays/` | `GBA DS/logos/`, `GBA DS/overlays/` |
| Screenshots | `/storage/roms/screenshots/` | `GBA DS/screenshots/` |
| Bug reports | `gba-ds/bug-reports/` | `GBA DS/bug-reports/` |
| Settings | `gba-ds/gba-ds.cfg` | `GBA DS/gba-ds.cfg` (optional) |
| Log | `gba-ds/gba-ds.log` | `GBA DS/gba-ds.log`, `GBA DS/launch.log` |

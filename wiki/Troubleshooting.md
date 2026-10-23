## The log

Every run writes a log, replaced on the next run:
- ROCKNIX: `/storage/roms/ports/gba-ds/gba-ds.log`, including the screen set-up (`screens.sh`) before and after.
- Android: `GBA DS/gba-ds.log`, plus `GBA DS/launch.log` for what a frontend sent when it started GBA DS.

When there's a problem, the log reports it every 5 seconds: audio underruns, slow frames (with what took the time) and how busy the background map capture was.

**More detail:** on ROCKNIX, add a line `export GBADS_DEBUG=1` near the top of `gba-ds/launch.sh` (after the `#!` line). The log then also records every button, key and touch (with raw joystick button numbers), map captures, control-scheme switches and HUD changes. Remove the line afterwards; the log grows quickly.

When reporting a bug, attach `gba-ds.log` from a run where it happened, and say which game and region.

## GBA DS closes straight away, with no log

If starting GBA DS from Ports goes straight back to the menu and there's no `gba-ds.log` in the `gba-ds` folder, the launcher couldn't run at all. Usually the files lost their permissions on the way: the zip was unzipped on a computer and the files copied over a network share or MTP. GBA DS handles that itself since v1.2.2 (the log then says "made the files runnable again"). With an older version, copy the zip itself to the device and unzip it there, or over SSH:

```
chmod +x "/storage/roms/ports/GBA DS.sh" /storage/roms/ports/gba-ds/*.sh /storage/roms/ports/gba-ds/gba-ds
```

If there's a `gba-ds-error.log` next to `GBA DS.sh` instead, the `gba-ds` folder isn't where it should be: unzip the whole zip into the Ports folder.

## Screens and touch (ROCKNIX)

**The second screen stays dark, or both pictures are on one screen.** Stock ROCKNIX keeps the bottom panel off. `screens.sh` (run by `launch.sh`) switches it on and places it beside the top one for the run, then puts everything back on exit. Check the first lines of `gba-ds.log` for what it did. If you launched `gba-ds` some other way than `launch.sh` or the Ports entry, the panel isn't switched on. If GBA DS is ever force-killed, the screens can stay in its layout; starting and closing GBA DS again (or rebooting) puts them back.

**Taps land in the wrong place.** Try `touch_map = screen` in `gba-ds.cfg`. If the menu is on the wrong screen, set `touch_screen` to `first` or `second` (see [Settings](Settings#screens)).

**Buttons do the wrong thing.** Use **Map Buttons** in the game list's Settings (see [Playing](Playing#mapping-the-buttons)).

**HOME doesn't open the menu.** Map it with **Map Buttons**, or turn on the debug log (above), press HOME, and look for "raw joystick ... button N" in the log; put that number in `home_button`. Tapping the touchscreen always opens the menu, and `menu_combo = select+start` adds SELECT + START.

## Sound crackles

- Raise `audio_latency` (default 64 ms) to 96 or 128.
- Set **Map Refresh** (pause menu → Performance) to every 3 seconds. Each map refresh runs a second copy of the game for a moment; on a slower device very short intervals crackle.
- On the first run, wait for the game list's thumbnails to finish ("Loading thumbnail...", about a minute) before playing.
- **Frame Buffer** (pause menu → Performance) smooths out occasional slow frames, at the cost of one frame of input delay.

The log's every-5-seconds report says whether the sound ran out (underruns) and why.

## Small stutters

**A tiny hitch about every 3-4 seconds, like clockwork:** the GBA runs at 59.73 frames a second and the screen at 60 Hz, so now and then a frame has to be shown twice. Turn on **Match Display Rate** (pause menu → Performance). The log's `frame pacing:` line, near the start of each game, shows both rates.

**Hitches at irregular moments:** the log lists frames that took too long ("slow frames"), each with what took the time (emulating, drawing which screen, the map capture). With the debug log on (see above) it lists every one; send that log with a note of the game and roughly when the stutters happened. **Frame Buffer** (pause menu → Performance) smooths out occasional slow frames at the cost of a frame of input delay.

## The map

**The map doesn't appear, or doesn't update.** Maps can't be shown on title screens, cutscenes, menus and during fades; the last good map stays up. In A Link to the Past, the map dims where L does nothing (houses, caves, dungeons without their map). With the debug log on, each capture is logged with the reason it was rejected.

**The map is from an older moment.** Shorten **Map Refresh**, or wait for the next room change, which refreshes it straight away.

**A game isn't in the list.** Only supported games are listed by default; check the game's code against [[Supported games]], or set `show_all = 1`. Hacks and translations with a changed header code aren't recognised.

## Logos and thumbnails

**A tile says "Loading thumbnail..." forever, or a logo is wrong.** Delete `states/logos/<CODE>.png` (and `states/frames/<CODE>*` for map borders); they're made again on the next start. Your own `logos/<CODE>.png` always wins.

## Saves

**My RetroArch progress isn't there.** On the first launch of a game GBA DS copies `Game.srm` to `Game.sav` next to the ROM. After that the two are separate: progress in GBA DS is in the `.sav`. To bring newer RetroArch progress over, copy the `.srm` over the `.sav` while GBA DS isn't running.

**A resume point is broken** (for instance after a cheat): delete `states/<game>.auto.state` and the game starts from its battery save.

## Android

**A frontend starts GBA DS but not the game, or "no ROM".** Look in `GBA DS/launch.log`: it lists the intent's action, data and extras. See [[Launching from Android frontends]] for the arguments each frontend needs.

**GBA DS can't find my ROMs or the `GBA DS` folder.** Check that **All files access** is on for GBA DS (Android Settings → Apps → GBA DS → Permissions). Without it, the files are kept in `Android/data/io.github.kyannon.gbads/files`.

**The second screen is empty.** The device must show its second panel to Android as a second display; GammaOS on the RG DS Plus does.

## RetroAchievements

- **Not signing in:** check the username and password (ROCKNIX: EmulationStation's RetroAchievements settings, or `ra_username` / `ra_password`). Delete `states/ra/login.cfg` to forget the stored token and sign in again.
- **No achievements for a game:** RetroAchievements recognises a game by its ROM's checksum; hacks, translations and bad dumps aren't recognised. The second screen says so as the game starts.
- **Unlocked offline:** unlocks are sent when the connection comes back, while GBA DS is running.

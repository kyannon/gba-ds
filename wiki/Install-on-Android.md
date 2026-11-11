The Android version is made for the **Anbernic RG DS Plus on GammaOS** (Android 14). It needs a device whose second screen Android shows as a second display; on a one-screen device it runs, but there's nowhere to put the map.

## Install

1. Download the latest `gba-ds-v…-android.apk` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Open it on the device and install it. Android asks once to allow installing from your file manager or browser; allow it.
3. Open **GBA DS**. The first time, it explains why it needs **All files access**; tap **Continue**, switch it on for GBA DS on Android's settings page, and go back. It needs this to read ROMs from any folder, and to keep its files in a folder you can reach. (**Not now** goes on without it and doesn't ask again; you can switch it on later in Android's settings.)
4. Put your ROMs in **`ROMs/gba`** on the internal storage (subfolders are fine; `.gba`, `.zip` or `.7z`), or pick another folder in the game list's **Settings** (tap it, top right, or press **SELECT**) → **Folders** → **Choose ROM folder**.

**Updating:** in the game list, **Settings** > **Updates** > **Check now**, then **Update now**: GBA DS downloads the new APK and Android asks you to confirm installing it (GBA DS also offers new versions by itself once a day; see [[Playing#updates]]). Or install the new APK over the old one by hand. Everything is kept either way: release APKs are all signed with the same key.

## Screens and buttons

The game plays on the top screen. The map (or logo, or HUD), the pause menu and the game list are on the bottom touchscreen. The pause menu's **Swap Screens** swaps them. **L3** (press the left stick) brings up the quick bar.

Android keeps HOME for itself, so the pause menu opens with:
- **SELECT + START** pressed together (the game doesn't see the pair), or
- **BACK**.

Everything else works as on ROCKNIX; see [[Playing]].

## The GBA DS folder

Everything except the battery saves lives in a **`GBA DS`** folder on the internal storage, laid out like the ROCKNIX install folder:

| Path | What |
|---|---|
| `GBA DS/states/` | save states, resume points, menu settings (`prefs.cfg`), the game list's settings (`library.cfg`), extracted logos, the RetroAchievements sign-in and badges |
| `GBA DS/cheats/` | your RetroArch `.cht` cheat files (subfolders are fine) |
| `GBA DS/overlays/` | your own overlay PNGs (see [[Overlays]]) |
| `GBA DS/logos/` | your own logo images (`<CODE>.png`), used instead of the extracted ones |
| `GBA DS/screenshots/` | Take Screenshot's PNGs |
| `GBA DS/gba-ds.cfg` | optional: settings from [[Settings]] (create it; only the lines you want to change) |
| `GBA DS/gba-ds.log` | the log of the last run |
| `GBA DS/launch.log` | what frontends sent when they started GBA DS (see [[Launching from Android frontends]]) |

Battery saves (`.sav`) are kept next to the ROMs by default, as on ROCKNIX, so other emulators can use them. **Settings** → **Folders** can keep them in a folder of their own instead (see below).

Without All files access, these are kept in the app's own folder (`Android/data/io.github.kyannon.gbads/files`) instead, which other apps (and you, on newer Android) can't open.

To keep this folder somewhere else (in a folder for all your emulators, say), use **Settings** → **Folders** → **Choose GBA DS folder** (below).

## Game list settings

**Settings** in the game list (tap it, or **SELECT**) has, on Android:
- **Folders**:
  - **Choose ROM folder**: pick the folder GBA DS lists (Android's folder picker).
  - **Choose save folder**: keep battery saves in a folder of their own instead of next to the ROMs (**Keep saves next to the games** goes back). The first time you play a game, its save from next to the ROM is copied there (and left where it was). Saves already in the save folder aren't moved back if you switch back: copy them yourself.
  - **Save files: .sav / .srm (RetroArch's)**: `.srm` is the type RetroArch uses. To share each game's save with RetroArch, choose `.srm` and RetroArch's saves folder (`RetroArch/saves`, or the core's folder in it if RetroArch sorts saves by core). A save in the other type is copied over the first time you play a game.
  
  All three are kept in `states/library.cfg`. A save folder outside the `GBA DS` folder needs All files access.
  - **Choose GBA DS folder**: where the `GBA DS` folder itself goes. Pick an empty folder (make one in the picker) and GBA DS uses it as its folder; pick a folder with other things in it, such as `Emulators`, and GBA DS makes a `GBA DS` folder inside it. Close GBA DS and open it again: everything moves there before the game list opens, and the old folder goes. Picking a folder that already has GBA DS's files in it (from before) uses those as they are. The choice is kept in the app's own storage, so updates keep it; uninstalling forgets it (the folder and your files stay where they are: choose it again after reinstalling). Needs All files access.
- **RetroAchievements: Sign In / Sign Out**: sign in with your retroachievements.org username and password. GBA DS keeps a sign-in token, not the password, and signs in by itself from then on. See [Playing](Playing#retroachievements).
- **Map Buttons**: if a button does the wrong thing, or for a controller GBA DS doesn't know, set each button by where it sits (see [Playing](Playing#mapping-the-buttons)). Android keeps HOME (and usually BACK) for itself, so skip **Home** with a tap: SELECT + START or BACK still open the menu. The mapping is kept in `GBA DS/states/controllers.txt`.
- **Hotkeys** (beside Map Buttons): change the shortcuts, the hotkey button held with another (see [Playing](Playing#hotkeys)). On Android the hotkey button is BACK by default (Android keeps HOME), shown as **Back**; SELECT, START or L3 can be the hotkey instead. Kept in `GBA DS/states/hotkeys.cfg`.

## Starting games from a launcher

GBA DS can be started straight into a game by GammaOS's own launcher, Daijisho, ES-DE and others: see **[[Launching from Android frontends]]**.

## Known limits

- **Coming back from the home screen** (*experimental*): leaving GBA DS saves the game's resume point (Android may close apps in the background). Coming back, the game should be paused with the menu open; GammaOS on the RG DS Plus doesn't switch between apps reliably enough to test this.
- **Performance:** in testing on the RG DS Plus, games run at full speed (about 60 frames a second) with the occasional single slow frame. **Frame Buffer** (pause menu → Display) smooths those out at the cost of one frame of input delay. `gba-ds.log` lists slow frames with what took the time.

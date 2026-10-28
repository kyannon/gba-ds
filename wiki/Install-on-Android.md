The Android version is made for the **Anbernic RG DS Plus on GammaOS** (Android 14). It needs a device whose second screen Android shows as a second display; on a one-screen device it runs, but there's nowhere to put the map.

## Install

1. Download the latest `gba-ds-v…-android.apk` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Open it on the device and install it. Android asks once to allow installing from your file manager or browser; allow it.
3. Open **GBA DS**. The first time, it explains why it needs **All files access**; tap **Continue**, switch it on for GBA DS on Android's settings page, and go back. It needs this to read ROMs from any folder, and to keep its files in a folder you can reach. (**Not now** goes on without it and doesn't ask again; you can switch it on later in Android's settings.)
4. Put your ROMs in **`ROMs/gba`** on the internal storage (subfolders are fine; `.gba`, `.zip` or `.7z`), or pick another folder in the game list's **Settings** (tap it, top right, or press **SELECT**) → **ROM Folder**.

**Updating:** install the new APK over the old one. Everything is kept: release APKs are all signed with the same key.

## Screens and buttons

The game plays on the top screen. The map (or logo, or HUD), the pause menu and the game list are on the bottom touchscreen. **L3** (press the left stick) or the pause menu's **Swap Screens** swaps them.

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
| `GBA DS/overlays/` | your own overlay PNGs (see [[Map screen surrounds and overlays]]) |
| `GBA DS/logos/` | your own logo images (`<CODE>.png`), used instead of the extracted ones |
| `GBA DS/screenshots/` | Take Screenshot's PNGs |
| `GBA DS/gba-ds.cfg` | optional: settings from [[Settings]] (create it; only the lines you want to change) |
| `GBA DS/gba-ds.log` | the log of the last run |
| `GBA DS/launch.log` | what frontends sent when they started GBA DS (see [[Launching from Android frontends]]) |

Battery saves (`.sav`) stay next to the ROMs, as on ROCKNIX, so other emulators can use them.

Without All files access, these are kept in the app's own folder (`Android/data/io.github.kyannon.gbads/files`) instead, which other apps (and you, on newer Android) can't open.

## Game list settings

**Settings** in the game list (tap it, or **SELECT**) has, on Android:
- **ROM Folder**: pick the folder GBA DS lists (Android's folder picker); saved in `states/library.cfg`.
- **RetroAchievements: Sign In / Sign Out**: sign in with your retroachievements.org username and password. GBA DS keeps a sign-in token, not the password, and signs in by itself from then on. See [Playing](Playing#retroachievements).
- **Map Buttons**: if a button does the wrong thing, or for a controller GBA DS doesn't know, set each button by where it sits (see [Playing](Playing#mapping-the-buttons)). Android keeps HOME (and usually BACK) for itself, so skip **Home** with a tap: SELECT + START or BACK still open the menu. The mapping is kept in `GBA DS/states/controllers.txt`.

## Starting games from a launcher

GBA DS can be started straight into a game by GammaOS's own launcher, Daijisho, ES-DE and others: see **[[Launching from Android frontends]]**.

## Known limits

- **Coming back from the home screen** (*experimental*): leaving GBA DS saves the game's resume point (Android may close apps in the background). Coming back, the game should be paused with the menu open; GammaOS on the RG DS Plus doesn't switch between apps reliably enough to test this.
- **Performance:** in testing on the RG DS Plus, games run at full speed (about 60 frames a second) with the occasional single slow frame. **Frame Buffer** (pause menu → Display) smooths those out at the cost of one frame of input delay. `gba-ds.log` lists slow frames with what took the time.

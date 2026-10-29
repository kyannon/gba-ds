GBA DS runs on stock [ROCKNIX](https://rocknix.org) and on ROCKNIXDS, on the Anbernic RG DS Plus (two 1024×768 screens) and the RG DS (two 640×480 screens).

## Install

1. Download the latest `gba-ds-v…-linux-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Unzip it into `/storage/roms/ports/` (over the network share, or with a file manager). You get:
   - `gba-ds/`: the app, its settings (`gba-ds.cfg`) and, later, its saved data (`states/`);
   - `GBA DS.sh`: the **GBA DS** entry in EmulationStation's Ports;
   - `images/GBA DS.png`: the entry's icon (see [the Ports icon](#the-ports-icon) below).
3. Put your ROMs in `/storage/roms/gba/` (subfolders are fine):
   - `.gba` files, or zipped as `.zip` or `.7z` (the first GBA ROM in the archive is used);
   - saves and states are named after the file, so `Game.zip` and an unzipped `Game.gba` share them.
4. In EmulationStation, update the game lists (Start → Game settings → Update gamelists, or restart EmulationStation) and launch **GBA DS** from **Ports**.

The first time the game list opens, GBA DS pulls each game's logo (and, for the Castlevanias, its map border art) out of the ROMs in the background. That takes about a minute on the RG DS Plus, once; tiles say "Loading thumbnail..." until their logo is ready. See [[Playing]] for the game list itself.

## The two screens on stock ROCKNIX

Stock ROCKNIX keeps the RG DS Plus's bottom screen switched off and ties the touchscreen to the top one. While GBA DS runs, `gba-ds/screens.sh` switches the bottom screen on, places it next to the top one, and maps the touchscreen across both; when GBA DS closes (or is killed), it puts everything back as it was. Nothing to set up; `gba-ds.log` records the screen layout before and after. ROCKNIXDS already has both screens on, and works the same way.

## The Ports icon

The zip includes an icon for the Ports entry: **`/storage/roms/ports/images/GBA DS.png`**, a 1024×1024 PNG (its source is [`assets/brand/gba-ds-icon.png`](https://github.com/kyannon/gba-ds/blob/main/assets/brand/gba-ds-icon.png) in the repository). EmulationStation doesn't pick it up by itself, and scrapers won't find GBA DS in their databases, so point the entry at it once:

**From EmulationStation (easiest):**
1. Go to **Ports** and select **GBA DS**.
2. Open the game options (usually **SELECT**) → **Edit this game's metadata**.
3. Set **Image** to `/storage/roms/ports/images/GBA DS.png` (browse to `images/` in the Ports folder).
4. Save. Some themes also show **Thumbnail** or **Marquee**; the same file works for those.

**By editing `gamelist.xml`:** EmulationStation keeps the Ports entries in `/storage/roms/ports/gamelist.xml`. Edit it while EmulationStation isn't running (it rewrites the file when it exits, which would undo your change), e.g. over SSH after stopping it, or from another computer over the network share. Add an entry like this inside `<gameList>` (or add the `<image>` line to the existing GBA DS entry):

```xml
<game>
	<path>./GBA DS.sh</path>
	<name>GBA DS</name>
	<desc>Game Boy Advance with a second screen: maps, HUDs and logos on the other screen.</desc>
	<image>./images/GBA DS.png</image>
	<thumbnail>./images/GBA DS.png</thumbnail>
</game>
```

Paths are relative to the Ports folder. Upgrading GBA DS replaces the image file but not your `gamelist.xml`, so the entry keeps its icon.

## Starting games from EmulationStation's Game Boy Advance list

GBA DS can also be one of the emulators for the **Game Boy Advance** system, so you can start a game from EmulationStation's own GBA list instead of opening GBA DS first.

1. Open GBA DS's game list and go to **Settings** (tap it, top right, or press **SELECT**).
2. Switch **GBA DS in EmulationStation** on.
3. Restart EmulationStation (Start → Quit → Restart EmulationStation).
4. Pick GBA DS:
   - for one game: select it, open its game options (usually **SELECT**) → **Advanced game options** → **Emulator** → **GBADS**;
   - for every GBA game: in the Game Boy Advance system's advanced settings, set the emulator to **GBADS**.

Closing GBA DS goes straight back to EmulationStation. Your other GBA emulators keep working as before.

**How it works:** the setting writes two files into `/storage/.config/emulationstation/` (`es_config_dir` in `gba-ds.cfg` changes the folder):
- `es_systems_gbads.cfg`, which EmulationStation merges into its system list: a copy of the GBA system's emulator list with GBA DS added, and the GBA launch command pointed at `gbads-launch.sh`;
- `gbads-launch.sh`, which starts GBA DS when it's the chosen emulator, run by ROCKNIX's launcher as a Port (so your settings for Ports apply), and hands every other launch to `/usr/bin/runemu.sh` exactly as before.

GBA DS rewrites the copied emulator list each time it starts, so it keeps up with ROCKNIX updates. Switching the setting off deletes both files. If you delete the GBA DS folder without switching it off first, games set to GBA DS fall back to mGBA; delete the two files to tidy up.

## Upgrading

The easiest way: in the game list, **Settings** > **Updates** > **Check now**, then **Update now** (GBA DS also offers new versions by itself once a day; see [[Playing#updates]]). That keeps your `gba-ds.cfg` too.

By hand: unzip the new release over the old one, into `/storage/roms/ports/`.

- **Kept:** everything in `gba-ds/states/` (save states, resume points, cheat choices, menu settings, extracted logos, your RetroAchievements sign-in), your own `logos/` and `overlays/`, and the battery saves next to your ROMs.
- **Replaced** (by hand only): `gba-ds/gba-ds.cfg`. If you've edited it, keep a copy first and copy your changes back (new versions may add options; see [[Settings]]).
- If **GBA DS in EmulationStation** was on, it stays on.

**From gbadual** (this app's old name): unzip as above, then launch **GBA DS** once. On that first run it copies your save states, resume points, menu settings and any custom logos or overlays from the old `gbadual/` folder. Then delete the old `gbadual/` folder and the **GBA Dual Screen** entry (`GBA Dual Screen.sh`) from `/storage/roms/ports/`. Copy any changes you'd made in `gbadual.cfg` into `gba-ds/gba-ds.cfg`.

## Uninstalling

1. If you switched **GBA DS in EmulationStation** on, switch it off first (game list → Settings).
2. Delete `gba-ds/`, `GBA DS.sh` and `images/GBA DS.png` from `/storage/roms/ports/`.

Battery saves (`.sav`, next to your ROMs) stay; other emulators can use them.

[BananaOS](https://github.com/JorreFog/BananaLinux) is a light Linux made only for the RG DS and RG DS Plus. GBA DS runs on it as a port, from the same zip as on ROCKNIX. *Support is new: report anything that doesn't work (see [Troubleshooting](Troubleshooting)).*

## Install

1. Download the latest `gba-ds-v…-linux-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Unzip it into the card's `roms/ports` folder (on the device: `/data/roms/ports`). You get a `gba-ds/` folder, a `GBA DS.sh` entry and `images/`. Banana's file sharing (Settings → Network) lets you copy it over Wi-Fi.
3. Keep your Game Boy Advance games in `roms/gba` (`/data/roms/gba`), where Banana's own GBA list finds them too.
4. In Banana's menu, open **Ports** and start **GBA DS**.

## How it fits in

- **Both screens:** Banana has no compositor, so GBA DS opens a window on each panel and draws them with the GPU, straight onto the panels (SDL's KMS/DRM driver); touches on the bottom panel work as on ROCKNIX. Banana's ports launcher normally switches the bottom panel off; GBA DS switches it back on while it runs.
- **Buttons:** the zip has a mapping for the RG DS's built-in controls (`gba-ds/gamecontrollerdb.txt`, ROCKNIX's), since Banana ships none for SDL apps. The buttons work as on ROCKNIX (HOME, or a tap on the bottom touchscreen, opens the pause menu).
- **Where things go:** these replace ROCKNIX's paths unless you've changed them in `gba-ds.cfg`:

| | |
|---|---|
| Games | `/data/roms/gba` |
| Battery saves (`.srm`) | `/data/saves/gba`: the same file BananaOS's own Game Boy Advance list uses, so a game goes on from where you left it in either. GBA DS's saves from before v1.4.10 (`/data/saves/<game>.sav`) are carried over the first time you start each game; if both have a save, the newer one is used and the other is kept as `.srm.bak`. |
| Screenshots | `/data/screenshots` |
| Cheats | `roms/ports/gba-ds/cheats` (RetroArch `.cht` files; subfolders are fine), and RetroArch's `/data/retroarch/cheats` |
| Save states, settings from the menu, logos | `roms/ports/gba-ds/states/` |
| Log | `roms/ports/gba-ds/gba-ds.log` |

- **RetroAchievements:** the account from Banana's first-time setup (`banana/accounts.conf`) is used, in casual mode. Banana has no `curl` program, so GBA DS talks to the server through Banana's curl library instead (the log says "using libcurl").
- **Battery and clock** in the pause menu come from the system, as on ROCKNIX.
- **Back to Banana:** closing GBA DS from Ports goes back to Banana's menu.

## Starting games from Banana's Game Boy Advance list

GBA DS can take over the second-screen games in Banana's own **Game Boy Advance** list, so you don't have to open GBA DS first:

1. Start GBA DS from Ports, open **Settings** in its game list (tap it, top right, or press SELECT) and switch **GBA DS in BananaOS's GBA list** on.
2. Restart BananaOS (Settings → System), since Banana reads its systems list when it starts.

Then a game picked in Banana's Game Boy Advance list starts in GBA DS when GBA DS has second-screen support for it (see [[Supported games]]), and closing it goes back to Banana's menu. Every other game plays in RetroArch (gpSP) as before.

How it works: Banana reads its systems from `banana/systems.conf` on the card when there is one, instead of its built-in list. Switching this on makes that copy (or edits yours) so the Game Boy Advance entry runs `gba-ds/banana-play.sh`, keeping the original line as a comment (`# gba-ds: run = ...`), which `banana-play.sh` uses for the games GBA DS doesn't support. Switching it off puts the line back, and deletes the copy if GBA DS made it and nothing else in it changed.

While the copy is there, systems added by later BananaOS updates don't appear in Banana's menu until you switch this off (Banana's own list is always in `banana/systems.conf.default`, to compare). If GBA DS's folder moves, it updates the line the next time it starts.

If GBA DS is removed while this is on, the Game Boy Advance list still works: its games start in Banana's own emulator again (since v1.4.10; before, they didn't start at all until the line was put back, and starting GBA DS v1.4.10 once fixes a line from an older version). To tidy up, switch it off before removing GBA DS, or delete `banana/systems.conf` if nothing else in it was changed.

## If something's wrong

Send `roms/ports/gba-ds/gba-ds.log` (and for a stutter or slowness, a log made with `export GBADS_DEBUG=1` added near the top of `gba-ds/launch.sh`). The log's first lines say it found BananaOS, which paths it used, the window size and the frame pacing.

To compare other ways of reaching the panels, add a line near the top of `gba-ds/launch.sh` (before `if [ -d /etc/banana ]`):
- `export GBADS_BANANA_VIDEO=evdev`: one window over both panels drawn by the CPU, copied onto them each frame (24-30 frames a second on an RG DS Plus).
- `export GBADS_BANANA_VIDEO=offscreen`: the same drawn with OpenGL ES and read back (under 5 frames a second).

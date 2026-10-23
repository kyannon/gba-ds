The RG DS Plus ships with Anbernic's own Linux. GBA DS runs on it from its **Ports** menu, from the same zip as on ROCKNIX. *Support is experimental: report anything that doesn't work (see [Troubleshooting](Troubleshooting)).*

## Install

1. Download the latest `gba-ds-v…-linux-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Unzip it into the `Ports` folder on the games card (on the device: `/mnt/mmc/Ports`). You get a `gba-ds/` folder, a `GBA DS.sh` entry and `images/`.
3. Keep your Game Boy Advance games in `Roms/GBA` on the same card, where Anbernic's own GBA list finds them too.
4. Open **Ports** in Anbernic's menu and start **GBA DS**.
5. **Map the buttons** the first time: open **Settings** in GBA DS's game list (tap it, top right) and choose **Map Buttons** (see [Playing](Playing#mapping-the-buttons)). The zip has a mapping for the built-in controls, but parts of it are still guesses.

## How it fits in

- **ROM folder:** GBA DS looks for a `GBA` folder next to the `Ports` folder (or in `Roms` on either card) and lists the games in the first one it finds. `rom_dir` in `gba-ds.cfg` overrides it.
- **Touchscreen:** Anbernic's Linux keeps the touchscreen off outside its own DS emulator. GBA DS switches it on while it runs and off again when it closes.
- **Screenshots** go into `gba-ds/screenshots`.
- **HOME and BACK** both open the pause menu (BACK is `home_button = 8`, set by `launch.sh`); a tap on the touchscreen does too.
- **No EmulationStation:** Anbernic's menu isn't EmulationStation, so the game list's Settings don't offer to add GBA DS to it. Start GBA DS from Ports.

## Known issues

- **Volume:** the volume keys set GBA DS's own volume (shown on screen, kept between games in `gba-ds/states/volume.txt`), not the system's. Anbernic's Linux applies its menu volume somewhere GBA DS can't read or change, so the level you set in the menu doesn't carry over: GBA DS starts at its own last level.

## If something's wrong

Send `Ports/gba-ds/gba-ds.log` from the card. Its first lines say which system GBA DS found, the ROM folder, the volume and whether the touchscreen was switched on. If the buttons are wrong, run **Map Buttons** and send the log too: the mapping it made is in it, ready to build into GBA DS for everyone with the device.

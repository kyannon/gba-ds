The RG DS Plus ships with Anbernic's **Stock Linux OS**. GBA DS runs on it from its **Ports** menu, from the same zip as on ROCKNIX. *Support is experimental: report anything that doesn't work (see [Troubleshooting](Troubleshooting)).*

## Install

1. Download the latest `gba-ds-v…-linux-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases).
2. Unzip it into the `Ports` folder on the games card (on the device: `/mnt/mmc/Ports`). You get a `gba-ds/` folder, a `GBA DS.sh` entry and `images/`.
3. Keep your Game Boy Advance games in `Roms/GBA` on the same card, where the Stock Linux OS's own GBA list finds them too.
4. Open **Ports** in the Stock Linux OS's menu and start **GBA DS**.
5. The built-in buttons are mapped already. If you use another controller, or a button does the wrong thing, use **Map Buttons** in the game list's **Settings** (tap it, top right; see [Playing](Playing#mapping-the-buttons)).

## How it fits in

- **ROM folder:** GBA DS looks for a `GBA` folder next to the `Ports` folder (or in `Roms` on either card) and lists the games in the first one it finds. `rom_dir` in `gba-ds.cfg` overrides it.
- **Touchscreen:** the Stock Linux OS keeps the touchscreen off outside its own DS emulator. GBA DS switches it on while it runs and off again when it closes.
- **Screenshots** go into `gba-ds/screenshots`.
- **Sound** goes through the system's sound service, so it plays wherever the system's does (speaker or headphones).
- **HOME and BACK** both open the pause menu, and so does a tap on the touchscreen.
- **No EmulationStation:** the Stock Linux OS's menu isn't EmulationStation, so the game list's Settings don't offer to add GBA DS to it. Start GBA DS from Ports.

## Known issues

- **Volume:** the volume keys set GBA DS's own volume (shown on screen, kept between games in `gba-ds/states/volume.txt`), not the system's. The Stock Linux OS applies its menu volume somewhere GBA DS can't read or change, so the level you set in the menu doesn't carry over: GBA DS starts at its own last level.
- **Occasionally no sound:** once in a while a game plays silently, with no error, even though the volume keys show a level. The cause isn't known yet. So far, the next start has had sound again. If it happens, please send `gba-ds.log` from that run: 20 seconds into each run, it notes where the sound is going, which should show what went wrong.

## If something's wrong

Send `Ports/gba-ds/gba-ds.log` from the card. Its first lines say which system GBA DS found, the ROM folder, the volume and whether the touchscreen was switched on. If the buttons are wrong, run **Map Buttons** and send the log too: the mapping it made is in it, ready to build into GBA DS for everyone with the device.

**Armada** is the operating system on the AYN Thor ([armada-os/armada](https://github.com/armada-os/armada)). GBA DS runs on it, with the second screen on the Thor's bottom panel and its touchscreen. *Support is experimental: it was tried on a Thor in Armada's Game Mode (issue #86), so report anything that doesn't work (see [Troubleshooting](Troubleshooting)).*

## How the two screens are shown

- **In Game Mode**, Armada's compositor (gamescope) shows only the top panel and lends the bottom one to a program that asks for it. GBA DS asks, draws the second screen with its own software renderer and puts it on that panel, turned the way the panel is mounted. The game is on the top screen, and touches on the bottom panel reach GBA DS (the quick bar, the menus).
- **In the desktop**, both panels are windows of the desktop and GBA DS uses them as on any two-screen system: nothing is lent.

If the bottom panel can't be lent (another system, an older Armada, the request refused), nothing changes: GBA DS carries on as before.

## Start it

1. Download the latest `gba-ds-v…-linux-aarch64.zip` from the [Releases page](https://github.com/kyannon/gba-ds/releases) and unzip it where Armada can run it.
2. In Game Mode, start `launch.sh` from a Steam shortcut (a non-Steam game): that is how it was tried. `launch.sh` reads Armada's device profile (`/usr/libexec/armada/device-env`) for the bottom panel's connector and its touchscreen, and asks for the panel when Game Mode is running.
3. Keep your games in the folder GBA DS looks in (`rom_dir` in `gba-ds.cfg` sets it; see [Settings](Settings)).

## If something's wrong

Send `gba-ds.log` from the app's folder. `launch.sh` writes a line to it, "Armada: bottom panel …, touchscreen … , lease …", naming the connector and touchscreen it found and whether it asked for the panel; GBA DS then says whether the lease was granted and the picture's size. If the bottom panel stays dark or the touches land turned, say what the Armada line and the next lines say. `GBADS_DRM_LEASE=` set to a connector name (`DSI-1`) in the environment asks for that panel yourself, and set to nothing in front of `launch.sh` turns the lease off.

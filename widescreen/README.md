# GBA DS widescreen patches

GBA DS's own widescreen patches (docs/WIDESCREEN.md), one folder per game code. They are never put in a release: the app reads `index.txt` from this folder on GitHub and offers a game's patch for download in its Patches menu (WS-6).

- `CODE/NAME (16-9).ips`: a patch, the game's fixes and the 32-byte marker (`tools/widescreen.py ips`).
- `CODE/dumps.txt`: the CRC32s of the ROM files the patches fit, one a line.
- `index.txt`: the list the app reads. Made by `tools/widescreen.py index`; don't edit it by hand.

`ZWSE` is the test ROM's (`tools/widetest/`), kept so the download can be tried without a real game's patch.

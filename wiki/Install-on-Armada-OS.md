The AYN Thor's **Armada OS** gives each panel to its own compositor, so no single window of GBA DS can reach both. GBA DS can run the game on the top screen and hand the second screen's picture to a small second program that shows it on the bottom panel. *Support is experimental and has not been tried on the device yet: report what works and what doesn't (see [Troubleshooting](Troubleshooting)).*

## How it works

- **GBA DS** runs as usual, on the top screen. With `second_screen = shm` it draws the second screen into a picture in shared memory (a file in `$XDG_RUNTIME_DIR`) instead of into a window.
- **The presenter**, `gba-ds --present-second`, runs on the bottom panel's display and shows that picture full screen, in proportion. It waits until a game is running, follows it from game to game, and shows black when there is none.

## Set up

1. Unzip a Linux build of GBA DS where Armada OS can run it (aarch64).
2. Start GBA DS with `--second-screen=shm` (or `second_screen = shm` in `gba-ds.cfg`). The picture is 1240x1080 unless you give `--second-size=WxH`.
3. Start the presenter on the bottom panel's display, for instance `DISPLAY=:2 SDL_VIDEODRIVER=x11 ./gba-ds --present-second`. Both programs must use the same `XDG_RUNTIME_DIR` (or the same `GBADS_SECOND_SHM=/path/to/file`, which the presenter also takes as `--second-shm=/path/to/file`).
4. Ctrl + Esc closes the presenter.

## Known issues

- **Touch on the bottom panel doesn't reach the game yet**, so the quick bar's swipe isn't available there. HOME + the buttons work as usual.
- **Sound, suspend and resume** haven't been tried.
- The second screen is drawn by the CPU (as on Android), every frame.

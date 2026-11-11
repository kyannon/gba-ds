# Android code paths on the desktop

`build.sh` builds `build/host/gba-ds-android`: the desktop app compiled with
`__ANDROID__`, with the files here standing in for `src/android.c`'s Java
glue. It runs headless like the normal desktop build, so the Android-only
code in the shared files can be tried and looked at before a CI APK:
Settings (Folders, Hotkeys, Updates), the second display drawn with the GPU
(ui.c), the APK update path (update.c, http.c), touch on either screen.

    scripts/setup-host.sh && make     # once
    tools/androidtest/build.sh

Run it like the desktop app, with these on top:

    SDL_VIDEODRIVER=offscreen SDL_AUDIODRIVER=dummy SDL_RENDER_DRIVER=opengles2 \
    GBADS_WINDOW=1024x768 STUB_SECOND=1280x960 GBADS_HOME=DIR SHOTDIR=DIR \
    build/host/gba-ds-android --state-dir=DIR/st --rom-dir=ROMS --input-script=SCRIPT

- `GBADS_WINDOW` is the activity's window (screen 1, the touch screen);
  `STUB_SECOND=WxH` the second display (default 1024x768). Each frame drawn
  on the second display is saved as `SHOTDIR/second-NNNN.bmp`; the script's
  `SHOT:` captures the activity's window only.
- `PICKED=PATH`: what Android's folder picker answers (Settings > Folders).
- `RA_USER` / `RA_PASS`: what the RetroAchievements sign-in box answers.
  HTTP (RetroAchievements, updates) goes through the `curl` command.
- Updates: `--update-url=http://127.0.0.1:8098/repos/kyannon/gba-ds/releases/latest`
  with `tools/update_mock.py --apk FILE` (unset the proxy variables for
  127.0.0.1). The "installer" copies the APK to `$STUB_INSTALLED` and
  answers after 1.5 s with `$STUB_INSTALL`: 1 asking to confirm (default),
  -2 cancelled, -3 another signing key, -4 no room, -5 an older build.
- `GBADS_APP_FILES=DIR`: where data-folder.cfg is looked for (issue #34);
  the move itself (android_data_setup) isn't run here.

What it can't show: Java (the Presentation, the activity's life cycle,
the package installer, Android's pickers), real touch on two displays,
performance. Those need the CI APK on a device.

When android.c gains a function the shared code calls, the link fails
here with "undefined reference": add a stand-in to `stubs_extra.c`.

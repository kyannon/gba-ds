#!/bin/sh
# Boots gba-ds.3dsx in the Azahar 3DS emulator on a virtual display and saves
# screenshots - run inside the linuxserver/azahar image with the repo at /repo,
# the ROMs to offer at /roms and an output directory at /out.
#
#   azahar_run.sh <step>...   each step is an xdotool key name (Azahar's keys:
#                             a = A, s = B, z = X, x = Y, m = START, n = SELECT,
#                             q = L, w = R, 1 = ZL, 2 = ZR, t/g/f/h = D-pad),
#                             "shot:<name>", "wait:<seconds>" or "click:<x>,<y>"
set -eu
SD="$HOME/.local/share/azahar-emu/sdmc"
mkdir -p "$SD/3ds/gba-ds" "$SD/roms/gba" /out/fb
cp /roms/* "$SD/roms/gba/" 2>/dev/null || true
[ -d /sd ] && cp -r /sd/. "$SD/3ds/gba-ds/" || true
trap 'chmod -R a+rwX /out; cp -r "$SD/3ds/gba-ds" /out/sd 2>/dev/null || true; cp "$HOME"/.local/share/azahar-emu/log/*.txt /out/ 2>/dev/null || true; chmod -R a+rwX /out' EXIT

Xvfb :1 -screen 0 1280x960x24 -fbdir /out/fb > /out/xvfb.log 2>&1 &
sleep 2
export DISPLAY=:1
azahar "/repo/3ds/ctr/${APP:-gba-ds.3dsx}" > /out/azahar.log 2>&1 &
sleep "${BOOT_WAIT:-25}"

for step in "$@"; do
    case "$step" in
    shot:*) cp /out/fb/Xvfb_screen0 "/out/${step#shot:}.xwd" ;;
    wait:*) sleep "${step#wait:}" ;;
    click:*)
        xy="${step#click:}"
        xdotool mousemove "${xy%,*}" "${xy#*,}" mousedown 1
        sleep 0.3
        xdotool mouseup 1
        sleep 1
        ;;
    *)
        WIN=$(xdotool search --name "Azahar" | tail -1)
        xdotool windowactivate --sync "$WIN" 2>/dev/null || true
        xdotool keydown "$step"
        sleep 0.3
        xdotool keyup "$step"
        sleep 1
        ;;
    esac
done

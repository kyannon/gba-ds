#!/bin/sh
# volume.sh up|down: the volume keys on systems that leave them to the
# program running (the RG DS Plus's own Linux: PulseAudio's default sink,
# 0-100% in steps of 5, as its own settings do). Run by GBA DS (launch.sh
# sets GBADS_VOLUME_SCRIPT); prints the new level.
command -v pactl >/dev/null 2>&1 || exit 1
v=$(pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -o '[0-9]*%' | head -n 1 | tr -d %)
[ -n "$v" ] || exit 1
case "$1" in
up) v=$((v + 5)) ;;
down) v=$((v - 5)) ;;
esac
[ $v -gt 100 ] && v=100
[ $v -lt 0 ] && v=0
pactl set-sink-volume @DEFAULT_SINK@ "$v%" && echo "$v"

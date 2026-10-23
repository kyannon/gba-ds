#!/bin/sh
# volume.sh up|down: the volume keys on systems that don't change the volume
# themselves while a port runs (the RG DS Plus's own Linux: PulseAudio's
# default sink, 0-100% in steps of 5). Run by GBA DS (launch.sh sets
# GBADS_VOLUME_SCRIPT); prints the new level and who set it ("system" when
# the system had already changed it, so it isn't changed twice).
command -v pactl >/dev/null 2>&1 || exit 1
get() { pactl get-sink-volume @DEFAULT_SINK@ 2>/dev/null | grep -o '[0-9]*%' | head -n 1 | tr -d %; }
v=$(get)
[ -n "$v" ] || exit 1
sleep 0.3
v2=$(get)
if [ -n "$v2" ] && [ "$v2" != "$v" ]; then
	echo "$v2 system"
	exit 0
fi
case "$1" in
up) v=$((v + 5)) ;;
down) v=$((v - 5)) ;;
esac
[ $v -gt 100 ] && v=100
[ $v -lt 0 ] && v=0
pactl set-sink-volume @DEFAULT_SINK@ "$v%" && echo "$v gba-ds"

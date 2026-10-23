#!/bin/bash
# GBA DS launcher for ROCKNIX.
#   launch.sh                      open the game library
#   launch.sh /path/Game.gba       play one game directly
#   (any --option=value from gba-ds.cfg can be appended)
# Output goes to gba-ds.log next to this script.

. /etc/profile >/dev/null 2>&1

DIR="$(cd "$(dirname "$0")" && pwd)"

# Controller mappings shipped with ROCKNIX
for db in /storage/.config/SDL-GameControllerDB/gamecontrollerdb.txt \
          /usr/config/SDL-GameControllerDB/gamecontrollerdb.txt; do
	if [ -f "$db" ]; then
		export SDL_GAMECONTROLLERCONFIG_FILE="$db"
		break
	fi
done

# Find sway's IPC socket so GBA DS can span both screens.
if [ -z "$SWAYSOCK" ]; then
	for s in "$XDG_RUNTIME_DIR"/sway-ipc.*.sock /var/run/0-runtime-dir/sway-ipc.*.sock /run/user/*/sway-ipc.*.sock; do
		if [ -S "$s" ]; then
			export SWAYSOCK="$s"
			break
		fi
	done
fi

# Sound: SDL reaches PipeWire through the runtime directory with its socket;
# when it isn't in the environment (a player on ROCKNIXDS 1.6.4 heard
# nothing), the one that has it.
if [ ! -S "${XDG_RUNTIME_DIR:-/nonexistent}/pipewire-0" ]; then
	for d in /var/run/0-runtime-dir /run/0-runtime-dir /run/user/0; do
		if [ -S "$d/pipewire-0" ]; then
			export XDG_RUNTIME_DIR="$d"
			break
		fi
	done
fi

cd "$DIR"

# First run after upgrading from gbadual (the app's old name): bring over
# save states, resume points, menu settings and any custom logos/overlays.
OLD="$DIR/../gbadual"
if [ -d "$OLD/states" ] && [ ! -d "$DIR/states" ]; then
	cp -a "$OLD/states" "$DIR/states"
	for d in logos overlays; do
		[ -d "$OLD/$d" ] && mkdir -p "$DIR/$d" && cp -an "$OLD/$d/." "$DIR/$d/" 2>/dev/null
	done
fi

# BananaOS (github.com/JorreFog/BananaLinux): no compositor and no sway.
# GBADS_BANANA_VIDEO picks how GBA DS reaches the two panels:
#   kmsdrm (default): SDL's KMS/DRM driver, a window on each panel, drawn by
#     the GPU straight onto it (the top panel is display 0: SDL_KMSDRM_FIRST)
#   evdev: Banana's SDL patch puts one window over both panels
#     (SDL_BANANA_PANELS: the top one left, as sway lays them out), drawn by
#     the CPU and copied onto them each frame: 24-30 frames a second on an
#     RG DS Plus (Banana 0.3.0)
#   offscreen: the same with OpenGL ES, each frame read back: under 5
# Banana's paths replace ROCKNIX's for the settings still at ROCKNIX's
# defaults in gba-ds.cfg (anything you changed there is kept).
if [ -d /etc/banana ]; then
	LOG="$DIR/gba-ds.log"
	VIDEO=${GBADS_BANANA_VIDEO:-kmsdrm}
	echo "BananaOS, video: $VIDEO" > "$LOG"
	B=()
	cfgval() { sed -n "s/^$1 *= *//p" "$DIR/gba-ds.cfg" 2>/dev/null | tail -n 1; }
	if [ "$VIDEO" = kmsdrm ]; then
		export SDL_VIDEODRIVER=kmsdrm SDL_KMSDRM_FIRST=${BANANA_TOP:-DSI-2}
		unset SDL_BANANA_PANELS
		# a window on each panel; touches are the bottom panel's own
		case "$(cfgval layout)" in auto|"") B+=(--layout=dual) ;; esac
		case "$(cfgval touch_map)" in window|"") B+=(--touch-map=screen) ;; esac
	else
		export SDL_VIDEODRIVER=$VIDEO SDL_BANANA_PANELS=side SDL_BANANA_TOP=${BANANA_TOP:-DSI-2}
		unset SDL_KMSDRM_FIRST
	fi
	# the ports launcher switched the bottom panel's backlight off (and
	# bananad keeps it off while /run/banana/panel-off names it): on for us
	if [ -f /run/banana/panel-off ]; then
		BL=$(cat /run/banana/panel-off)
		rm -f /run/banana/panel-off
		[ -w "$BL/bl_power" ] && echo 0 > "$BL/bl_power"
	fi
	[ "$(cfgval rom_dir)" = /storage/roms/gba ] && B+=(--rom-dir=/data/roms/gba)
	[ "$(cfgval screenshot_dir)" = /storage/roms/screenshots ] && B+=(--screenshot-dir=/data/screenshots)
	case "$(cfgval cheat_dir)" in /config/retroarch/cheats*) B+=(--cheat-dir=/data/retroarch/cheats) ;; esac
	# battery saves beside RetroArch's (a gpSP .srm is carried over on a game's first start)
	[ -z "$(cfgval save_dir)" ] && B+=(--save-dir=/data/saves)
	# RetroAchievements: the account from Banana's first-time setup
	A=/data/banana/accounts.conf
	if [ -z "$(cfgval ra_username)" ] && [ -f $A ]; then
		u=$(sed -n 's/^ra_user=//p' $A | head -n 1)
		w=$(sed -n 's/^ra_password=//p' $A | head -n 1)
		[ -n "$u" ] && B+=("--ra-username=$u" "--ra-password=$w")
	fi
	mkdir -p /data/screenshots /data/saves 2>/dev/null
	echo "Banana paths: ${B[*]//--ra-password=*/--ra-password=...}" >> "$LOG"
	exec ./gba-ds "${B[@]}" "$@" >> "$LOG" 2>&1
fi

# Any other system (the RG DS and RG DS Plus's own Linux, ...): neither
# ROCKNIX's /storage nor BananaOS. GBA DS finds the GBA ROM folder beside the
# ports folder it was put in (or on either card), keeps its screenshots in
# its own folder, and reads the touchscreen as the bottom screen's own
# (a window per screen there: SDL gives touches to the first). What the
# system is like goes into gba-ds.log, for reports.
if ! grep -qi rocknix /etc/os-release 2>/dev/null && [ ! -d /storage/.config/system ]; then
	LOG="$DIR/gba-ds.log"
	echo "another system (not ROCKNIX or BananaOS)" > "$LOG"
	O=()
	cfgval() { sed -n "s/^$1 *= *//p" "$DIR/gba-ds.cfg" 2>/dev/null | tail -n 1; }
	if [ "$(cfgval rom_dir)" = /storage/roms/gba ]; then
		ROMS="$(dirname "$(dirname "$DIR")")"
		GBA=
		for r in "$ROMS" /mnt/mmc/Roms /mnt/sdcard/Roms /mnt/mmc/roms /mnt/sdcard/roms /roms /roms2; do
			for g in GBA gba Gba; do
				if [ -d "$r/$g" ]; then
					# (the first with games in it; else the first there is)
					if ls "$r/$g" 2>/dev/null | grep -qiE '\.(gba|zip|7z)$'; then GBA="$r/$g"; break 2; fi
					[ -z "$GBA" ] && GBA="$r/$g"
				fi
			done
		done
		[ -n "$GBA" ] && O+=("--rom-dir=$GBA")
	fi
	[ "$(cfgval screenshot_dir)" = /storage/roms/screenshots ] && mkdir -p "$DIR/screenshots" && O+=("--screenshot-dir=$DIR/screenshots")
	# Ports play straight to the sound card there (SDL's ALSA; through
	# PulseAudio GBA DS was silent), so the volume keys, which the system
	# leaves to the program running, set GBA DS's own volume, kept here.
	mkdir -p "$DIR/states" && export GBADS_VOLUME="$DIR/states/volume.txt"
	# the RG DS Plus's volume keys: raw buttons 16 (up) and 15 (down)
	grep -q 'ANBERNIC-rk3568-keys' /proc/bus/input/devices 2>/dev/null && export GBADS_VOLUME_BUTTONS=16,15
	# the touchscreen read straight from its device (weston gives GBA DS no
	# touches there)
	TS=$(awk '/^N: Name=/{n=tolower($0)} /^H: Handlers=/{ if (n ~ /gt9|goodix|touch|ts[0-9_-]|-ts"/) { if (match($0, /event[0-9]+/)) { print substr($0, RSTART, RLENGTH); exit } } }' /proc/bus/input/devices 2>/dev/null)
	[ -n "$TS" ] && [ -z "$(cfgval touch_device)" ] && O+=("--touch-device=/dev/input/$TS")
	# The RG DS Plus's own Linux keeps its touchscreen off outside its DS
	# emulator (anbernic_misc/tpctrl 1; the emulator's launch.sh writes 0):
	# on for the run, put back afterwards. (And the kernel's inhibited
	# switch, the same way, in case.)
	TP=/sys/class/anbernic_misc/tpctrl
	TP_WAS=$(cat "$TP" 2>/dev/null)
	INH=
	[ -n "$TS" ] && [ -w "/sys/class/input/$TS/device/inhibited" ] && INH="/sys/class/input/$TS/device/inhibited"
	INH_WAS=$(cat "$INH" 2>/dev/null)
	if [ "$TP_WAS" = 1 ] || [ "$INH_WAS" = 1 ]; then
		[ "$TP_WAS" = 1 ] && echo 0 > "$TP" 2>/dev/null
		[ "$INH_WAS" = 1 ] && echo 0 > "$INH" 2>/dev/null
		trap '[ "$TP_WAS" = 1 ] && echo 1 > "$TP" 2>/dev/null; [ "$INH_WAS" = 1 ] && echo 1 > "$INH" 2>/dev/null' EXIT
		trap 'exit 143' TERM INT HUP
	fi
	# the RG DS Plus's HOME and BACK buttons (raw 13 and 8) both open the
	# menu: 13 by gamecontrollerdb.txt, 8 here
	if grep -q 'ANBERNIC-rk3568-keys' /proc/bus/input/devices 2>/dev/null && [ "$(cfgval home_button)" = -1 ]; then
		O+=(--home-button=8)
	fi
	# (and SELECT + START too, in case those aren't where we think)
	[ "$(cfgval menu_combo)" = none ] && O+=(--menu-combo=select+start)
	# (with two screens connected: a window each)
	n=0; for c in /sys/class/drm/card*-*/status; do grep -q '^connected' "$c" 2>/dev/null && n=$((n + 1)); done
	[ $n -ge 2 ] && case "$(cfgval touch_map)" in window|"") O+=(--touch-map=screen) ;; esac
	{
		echo "system: $(sed -n 's/^PRETTY_NAME=//p' /etc/os-release 2>/dev/null | tr -d '"'), $(uname -r)"
		echo "options: ${O[*]}"
		echo "sound: volume $(cat "$GBADS_VOLUME" 2>/dev/null || echo 100)% (buttons ${GBADS_VOLUME_BUTTONS:-none})"
		[ -n "$TS" ] && echo "touchscreen $TS: tpctrl was ${TP_WAS:-none}, now $(cat "$TP" 2>/dev/null)"
	} >> "$LOG" 2>&1
	rm -rf "$DIR/report" # (left by test builds)
	./gba-ds "${O[@]}" "$@" >> "$LOG" 2>&1
	exit $?
fi

# Second screen on and in place for the run (stock ROCKNIX keeps it off),
# put back afterwards; see screens.sh. The trap also covers being killed.
"$DIR/screens.sh" on > "$DIR/gba-ds.log" 2>&1
# what the sound goes through, for reports of no sound
{
	echo "sound: XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-unset} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-unset} PULSE_SERVER=${PULSE_SERVER:-unset}"
	command -v wpctl >/dev/null && timeout 2 wpctl status 2>&1 | sed -n '/^Audio/,/^Video/p' | head -n 30
} >> "$DIR/gba-ds.log" 2>&1
trap '"$DIR/screens.sh" off >> "$DIR/gba-ds.log" 2>&1' EXIT
trap 'exit 143' TERM INT HUP

./gba-ds "$@" >> "$DIR/gba-ds.log" 2>&1

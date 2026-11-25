#!/bin/bash
# GBA DS launcher for ROCKNIX.
#   launch.sh                      open the game library
#   launch.sh /path/Game.gba       play one game directly
#   (any --option=value from gba-ds.cfg can be appended)
# Output goes to gba-ds.log next to this script.
# GBA DS's own output reaches the log through a pipe and a separate cat
# (> >(exec cat >> LOG)), so the game never waits on the card, or ROCKNIX's
# merged /storage/roms folder, to write a log line (issue #50: a write held
# up there froze the game and broke the sound). The pipe holds 64 KB; cat
# writes everything out after GBA DS ends or crashes. GBA DS keeps its own
# exit status and process (exec on BananaOS).
# After an update installed in the app, GBA DS ends with
# $GBADS_RELAUNCH_STATUS and this script starts the new launch.sh (the
# new GBA DS with it), the log carried on: the new launch.sh's changes
# count from then, not from the next start (ROCKNIX and other systems;
# on BananaOS, where GBA DS is exec'd, it restarts itself).
RELAUNCH=75

. /etc/profile >/dev/null 2>&1

DIR="$(cd "$(dirname "$0")" && pwd)"
# the log: started afresh, or carried on after an update's restart
if [ -n "$GBADS_RELAUNCHED" ]; then
	echo "--- launch.sh again after the update" >> "$DIR/gba-ds.log"
else
	: > "$DIR/gba-ds.log"
fi
unset GBADS_RELAUNCHED

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

# Files that lost their permissions on the way (unzipped on a computer, then
# copied over a network share or MTP): runnable again. Where the card won't
# run programs at all (mounted noexec), GBA DS runs from a copy in memory,
# with GBADS_HOME pointing it back here. The note goes into the log below.
GBADS=./gba-ds
EXEC_NOTE=
for f in ./gba-ds ./*.sh; do [ -f "$f" ] && [ ! -x "$f" ] && chmod +x "$f" 2>/dev/null && EXEC_NOTE="made the files runnable again (they'd lost their permissions)"; done
if ! ./gba-ds --version >/dev/null 2>&1; then
	EXEC_NOTE="this folder can't run programs: running a copy of gba-ds from memory"
	for t in "${XDG_RUNTIME_DIR:-/run}" /tmp /dev/shm; do
		if [ -d "$t" ] && cp ./gba-ds "$t/gba-ds.bin" 2>/dev/null && chmod +x "$t/gba-ds.bin" && "$t/gba-ds.bin" --version >/dev/null 2>&1; then
			GBADS="$t/gba-ds.bin"
			export GBADS_HOME="$DIR"
			break
		fi
		rm -f "$t/gba-ds.bin" 2>/dev/null
	done
	[ "$GBADS" = ./gba-ds ] && EXEC_NOTE="gba-ds won't run here, and no copy of it would either: $(./gba-ds --version 2>&1 | head -n 1)"
fi

# GBA DS's own files (states/, cheats/, overlays/, logos/...): this folder,
# or data_dir in data-folder.cfg here (the app moves them there itself)
DATA=$(sed -n 's/^[[:space:]]*data_dir[[:space:]]*=[[:space:]]*//p' "$DIR/data-folder.cfg" 2>/dev/null | tail -n 1 | sed 's:/*[[:space:]]*$::')
case "$DATA" in /*) [ -d "$(dirname "$DATA")" ] || DATA="$DIR" ;; *) DATA="$DIR" ;; esac

# First run after upgrading from gbadual (the app's old name): bring over
# save states, resume points, menu settings and any custom logos/overlays.
OLD="$DIR/../gbadual"
if [ -d "$OLD/states" ] && [ ! -d "$DIR/states" ] && [ ! -f "$DIR/data-folder.cfg" ]; then
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
	echo "BananaOS, video: $VIDEO" >> "$LOG"
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
	# games: /data/roms/gba, and the RG DS Plus's second SD card's gba folder
	# while Banana's library shows that card's games (its sd2 script writes the
	# card's roms folder, /run/sd2/roms, in /run/banana/sd2/roms; issue #42)
	if [ "$(cfgval rom_dir)" = /storage/roms/gba ]; then
		R=/data/roms/gba
		SD2=$(head -n 1 /run/banana/sd2/roms 2>/dev/null)
		[ -n "$SD2" ] && [ -d "$SD2/gba" ] && R="$R:$SD2/gba"
		B+=(--rom-dir="$R")
	fi
	[ "$(cfgval screenshot_dir)" = /storage/roms/screenshots ] && B+=(--screenshot-dir=/data/screenshots)
	case "$(cfgval cheat_dir)" in /config/retroarch/cheats*) B+=(--cheat-dir=/data/retroarch/cheats) ;; esac
	# battery saves: the Game Boy Advance list's own, /data/saves/gba/<game>.srm,
	# the one file every GBA emulator there uses (run-retroarch): a game goes on
	# from the same save in either. GBA DS's saves before v1.4.10
	# (/data/saves/<game>.sav) are carried over on a game's first start.
	[ -z "$(cfgval save_dir)" ] && B+=(--save-dir=/data/saves/gba --save-ext=srm --save-from=/data/saves)
	# RetroAchievements: the account from Banana's first-time setup
	A=/data/banana/accounts.conf
	if [ -z "$(cfgval ra_username)" ] && [ -f $A ]; then
		u=$(sed -n 's/^ra_user=//p' $A | head -n 1)
		w=$(sed -n 's/^ra_password=//p' $A | head -n 1)
		[ -n "$u" ] && B+=("--ra-username=$u" "--ra-password=$w")
	fi
	mkdir -p /data/screenshots /data/saves/gba 2>/dev/null
	echo "Banana paths: ${B[*]//--ra-password=*/--ra-password=...}" >> "$LOG"
	[ -n "$EXEC_NOTE" ] && echo "$EXEC_NOTE" >> "$LOG"
	exec "$GBADS" "${B[@]}" "$@" > >(exec cat >> "$LOG") 2>&1
fi

# Any other system (the RG DS Plus's Stock Linux OS, ...): neither
# ROCKNIX's /storage nor BananaOS. GBA DS finds the GBA ROM folder beside the
# ports folder it was put in (or on either card), keeps its screenshots in
# its own folder, and reads the touchscreen as the bottom screen's own
# (a window per screen there: SDL gives touches to the first). What the
# system is like goes into gba-ds.log, for reports.
if ! grep -qi rocknix /etc/os-release 2>/dev/null && [ ! -d /storage/.config/system ]; then
	LOG="$DIR/gba-ds.log"
	echo "another system (not ROCKNIX or BananaOS)" >> "$LOG"
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
	[ "$(cfgval screenshot_dir)" = /storage/roms/screenshots ] && O+=("--screenshot-dir=$DATA/screenshots")
	# Sound: SDL plays through ALSA there, whose default device is
	# PulseAudio's (/etc/asound.conf); named here too in case that changes.
	# Twice a run was silent with no error, cause unknown: the log's sound
	# snapshot (below) shows the sinks and which card is playing. The volume
	# keys, which the system leaves to the program running, set GBA DS's own
	# volume, kept here.
	if [ -z "$PULSE_SERVER" ]; then
		for s in /tmp/pulse-socket "$XDG_RUNTIME_DIR/pulse/native" /var/run/pulse/native; do
			[ -S "$s" ] && export PULSE_SERVER="unix:$s" && break
		done
	fi
	[ -n "$PULSE_SERVER" ] && [ -z "$AUDIODEV" ] && ls /usr/share/alsa/alsa.conf.d/*pulse* >/dev/null 2>&1 && export AUDIODEV=pulse
	export GBADS_VOLUME="$DATA/states/volume.txt"
	# the RG DS Plus's volume keys: raw buttons 16 (up) and 15 (down)
	grep -q 'ANBERNIC-rk3568-keys' /proc/bus/input/devices 2>/dev/null && export GBADS_VOLUME_BUTTONS=16,15
	# Armada (github.com/armada-os/armada) on a dual-screen device, the AYN
	# Thor: its device profile names the bottom panel's connector and
	# touchscreen. In Game Mode gamescope shows only the top panel and leases
	# the bottom one to a program that asks for it (the bottom panel's own
	# gamescope steps aside meanwhile): GBADS_DRM_LEASE has GBA DS ask the
	# main gamescope (gamescope-primary). On the desktop both panels are
	# KDE's, a window each. Touches come from the bottom touchscreen's
	# device either way.
	ARMADA_TS=
	if [ -x /usr/libexec/armada/device-env ]; then
		eval "$(/usr/libexec/armada/device-env 2>/dev/null | grep -E '^ARMADA_SECONDARY_(CONNECTOR|TOUCHSCREEN)=')"
		for e in /sys/class/input/event*; do
			[ -n "$ARMADA_SECONDARY_TOUCHSCREEN" ] && [ "$(cat "$e/device/name" 2>/dev/null)" = "$ARMADA_SECONDARY_TOUCHSCREEN" ] && ARMADA_TS=${e##*/} && break
		done
		if [ -n "$ARMADA_SECONDARY_CONNECTOR" ] && [ -n "$GAMESCOPE_WAYLAND_DISPLAY" ] && [ -e "${XDG_RUNTIME_DIR:-/run/user/$(id -u)}/gamescope-primary" ]; then
			export GBADS_DRM_LEASE="${GBADS_DRM_LEASE-$ARMADA_SECONDARY_CONNECTOR}" GAMESCOPE_WAYLAND_DISPLAY=gamescope-primary
		fi
		echo "Armada: bottom panel ${ARMADA_SECONDARY_CONNECTOR:-none}, touchscreen ${ARMADA_SECONDARY_TOUCHSCREEN:-none} (${ARMADA_TS:-not found}), lease ${GBADS_DRM_LEASE:-no (not Game Mode)}" >> "$LOG"
	fi
	# the touchscreen read straight from its device (weston gives GBA DS no
	# touches there)
	TS=$ARMADA_TS
	[ -z "$TS" ] && TS=$(awk '/^N: Name=/{n=tolower($0)} /^H: Handlers=/{ if (n ~ /gt9|goodix|touch|ts[0-9_-]|-ts"/) { if (match($0, /event[0-9]+/)) { print substr($0, RSTART, RLENGTH); exit } } }' /proc/bus/input/devices 2>/dev/null)
	[ -n "$TS" ] && [ -z "$(cfgval touch_device)" ] && O+=("--touch-device=/dev/input/$TS")
	# The RG DS Plus's Stock Linux OS keeps its touchscreen off outside its DS
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
	# menu: 8 by gamecontrollerdb.txt, 13 here
	if grep -q 'ANBERNIC-rk3568-keys' /proc/bus/input/devices 2>/dev/null && [ "$(cfgval home_button)" = -1 ]; then
		O+=(--home-button=13)
	fi
	# (with two screens connected: a window each)
	n=0; for c in /sys/class/drm/card*-*/status; do grep -q '^connected' "$c" 2>/dev/null && n=$((n + 1)); done
	[ $n -ge 2 ] && case "$(cfgval touch_map)" in window|"") O+=(--touch-map=screen) ;; esac
	{
		echo "system: $(sed -n 's/^PRETTY_NAME=//p' /etc/os-release 2>/dev/null | tr -d '"'), $(uname -r)"
		echo "options: ${O[*]}"
		echo "sound: ${AUDIODEV:-default device}, volume $(cat "$GBADS_VOLUME" 2>/dev/null || echo 100)% (buttons ${GBADS_VOLUME_BUTTONS:-none})"
		[ -n "$TS" ] && echo "touchscreen $TS: tpctrl was ${TP_WAS:-none}, now $(cat "$TP" 2>/dev/null)"
	} >> "$LOG" 2>&1
	rm -rf "$DIR/report" # (left by test builds)
	# where the sound goes, once a game is likely playing (for reports of no sound)
	(
		sleep 20
		{
			echo "--- sound after 20 s"
			for f in /proc/asound/card*/pcm*p/sub*/status; do echo "$f: $(head -n 2 "$f" 2>/dev/null | tr '\n' ' ')"; done
			command -v pactl >/dev/null && timeout 3 pactl list short sinks && timeout 3 pactl list short sink-inputs
			echo "spk_state $(cat /sys/class/anbernic_misc/spk_state 2>/dev/null)"
			grep -v '^ *#' /etc/asound.conf 2>/dev/null | grep -v '^ *$' | head -n 20
			echo "---"
		} >> "$LOG" 2>&1
	) &
	[ -n "$EXEC_NOTE" ] && echo "$EXEC_NOTE" >> "$LOG"
	GBADS_RELAUNCH_STATUS=$RELAUNCH "$GBADS" "${O[@]}" "$@" > >(exec cat >> "$LOG") 2>&1
	st=$?
	wait $! 2>/dev/null # (the log written out before the traps add to it)
	if [ $st = $RELAUNCH ]; then
		# an update: the touchscreen as it was, then the new launch.sh
		[ "$TP_WAS" = 1 ] && echo 1 > "$TP" 2>/dev/null
		[ "$INH_WAS" = 1 ] && echo 1 > "$INH" 2>/dev/null
		trap - EXIT
		GBADS_RELAUNCHED=1 exec bash "$DIR/launch.sh" "$@"
	fi
	exit $st
fi

# Second screen on and in place for the run (stock ROCKNIX keeps it off),
# put back afterwards; see screens.sh. The trap also covers being killed.
bash "$DIR/screens.sh" on >> "$DIR/gba-ds.log" 2>&1
[ -n "$EXEC_NOTE" ] && echo "$EXEC_NOTE" >> "$DIR/gba-ds.log"
# what the sound goes through, for reports of no sound
{
	echo "sound: XDG_RUNTIME_DIR=${XDG_RUNTIME_DIR:-unset} SDL_AUDIODRIVER=${SDL_AUDIODRIVER:-unset} PULSE_SERVER=${PULSE_SERVER:-unset}"
	command -v wpctl >/dev/null && timeout 2 wpctl status 2>&1 | sed -n '/^Audio/,/^Video/p' | head -n 30
} >> "$DIR/gba-ds.log" 2>&1
# Clock governors (issue #50): GBA DS's Performance setting (the game
# list's Settings; performance = max in states/prefs.cfg) holds the CPU,
# memory and GPU at performance while it runs and puts them back itself.
# What it changed is in GOV_STATE until then: if GBA DS ends without
# putting them back (a crash, being killed), this does. What they are goes
# into the log, for reports.
GOV_STATE=/tmp/gba-ds-governors.state
cfgval() { sed -n "s/^$1 *= *//p" "$DIR/gba-ds.cfg" 2>/dev/null | tail -n 1; }
restore_governors() {
	[ -f "$GOV_STATE" ] || return 0
	while IFS=$'\t' read -r g was; do
		[ -n "$g" ] && [ -n "$was" ] && echo "$was" > "$g" 2>/dev/null
	done < "$GOV_STATE"
	rm -f "$GOV_STATE"
	echo "clock governors put back after GBA DS ended without doing so"
}
{
	restore_governors # (left from a run that ended without putting them back)
	GOVS=
	for g in /sys/devices/system/cpu/cpufreq/policy*/scaling_governor /sys/class/devfreq/*dmc*/governor /sys/class/devfreq/*gpu*/governor; do
		[ -r "$g" ] && GOVS="$GOVS ${g#/sys/}=$(cat "$g" 2>/dev/null)"
	done
	echo "device: ${QUIRK_DEVICE:-unknown}; clock governors:${GOVS:- none found}"
	echo "system: $(sed -n 's/^PRETTY_NAME=//p' /etc/os-release 2>/dev/null | tr -d '"') $(sed -n 's/^BUILD_ID=//p' /etc/os-release 2>/dev/null | tr -d '"'), $(uname -r)"
} >> "$DIR/gba-ds.log" 2>&1

# Services that get in GBA DS's way (issue #50: on one player's RG DS Plus,
# the RetroDualStation add-on, `rds-controlcenter`, crashed and restarted
# every ~11 s, adding and removing a virtual keyboard each time; sway held
# the screen up for half a second at every change). One systemctl call
# lists every loaded service. Stopped for the run, and started again when
# GBA DS closes:
# - a service that keeps restarting (3 restarts or more and still at it)
#   that the user added (its unit file under /storage);
# - the ones known to get in the way (SVC_KNOWN), running or not: they also
#   drive the second screen.
# ROCKNIX's own services are only logged, restarting or not (stopping them
# could take hotkeys or the volume keys away). Stops don't wait (--no-block),
# so start-up isn't held up. What was stopped is in SVC_STATE until it's
# started again: a run that ended without doing so is caught by the next
# start, and an update's restart keeps them stopped through the new
# launch.sh. pause_services = 0 in gba-ds.cfg: logged, nothing stopped.
SVC_STATE=/tmp/gba-ds-paused-services.state
SVC_KNOWN=" rds-controlcenter.service "
services_restore() {
	[ -s "$SVC_STATE" ] || { rm -f "$SVC_STATE"; return 0; }
	local u list=
	while read -r u; do
		[ -n "$u" ] || continue
		systemctl reset-failed "$u" 2>/dev/null
		systemctl start --no-block "$u" 2>/dev/null
		list="$list $u"
	done < "$SVC_STATE"
	rm -f "$SVC_STATE"
	echo "services started again:$list"
}
services_pause() {
	command -v systemctl >/dev/null || return 0
	local t0 t1 off= id nr act sub frag stopped= seen=
	t0=$(date +%s%N 2>/dev/null)
	case "$(cfgval pause_services)" in 0|off|false|no) off=1 ;; esac
	[ -f "$SVC_STATE" ] && echo "services still stopped from the last run: $(tr '\n' ' ' < "$SVC_STATE")"
	while IFS='|' read -r id nr act sub frag; do
		[ -n "$id" ] || continue
		local known= looping=
		case "$SVC_KNOWN" in *" $id "*) known=1 ;; esac
		[ "${nr:-0}" -ge 3 ] 2>/dev/null && [ "$act" != inactive ] && [ "$act" != failed ] && looping=1
		[ -n "$known" ] || [ -n "$looping" ] || continue
		[ "$act" = inactive ] || [ "$act" = failed ] && [ -z "$looping" ] && continue
		local why
		if [ -n "$known" ]; then why="known to get in GBA DS's way"; else why="restarting over and over ($nr restarts, $act/$sub)"; fi
		if [ -n "$off" ]; then
			seen="$seen; $id: $why (not stopped: pause_services = 0)"
		elif [ -n "$known" ] || [ "${frag#/storage/}" != "$frag" ]; then
			if systemctl stop --no-block "$id" 2>/dev/null; then
				grep -qx "$id" "$SVC_STATE" 2>/dev/null || echo "$id" >> "$SVC_STATE"
				stopped="$stopped; $id: $why"
			fi
		else
			seen="$seen; $id: $why (not stopped: part of the system)"
		fi
	done < <(systemctl show '*.service' -p Id,NRestarts,ActiveState,SubState,FragmentPath 2>/dev/null |
		awk -F= '/^$/ { if (id != "") print id "|" nr "|" act "|" st "|" frag; id = nr = act = st = frag = ""; next }
			$1 == "Id" { id = $2 } $1 == "NRestarts" { nr = $2 } $1 == "ActiveState" { act = $2 }
			$1 == "SubState" { st = $2 } $1 == "FragmentPath" { frag = $2 }
			END { if (id != "") print id "|" nr "|" act "|" st "|" frag }')
	t1=$(date +%s%N 2>/dev/null)
	local ms=
	[ -n "$t0" ] && [ -n "$t1" ] && [ "${t0%N}" = "$t0" ] && ms=" ($(( (t1 - t0) / 1000000 )) ms)"
	if [ -n "$stopped" ]; then echo "services stopped for the run$ms:${stopped#;}"; fi
	if [ -n "$seen" ]; then echo "services that may get in the way$ms:${seen#;}"; fi
	[ -z "$stopped$seen" ] && echo "services: none in the way$ms"
}
{ services_pause; } >> "$DIR/gba-ds.log" 2>&1

# What the system does while GBA DS runs (issue #50: on one player's RG DS
# Plus, presenting a frame stalled for half a second, in pairs 2.5 s apart
# about every 10 s, standing still or not): kernel messages, the journal,
# and sway's output, window and input events, each stamped with the system
# clock in seconds (the kernel's and the journal's monotonic clock; sway's
# from /proc/uptime, the same unless the device slept), as GBA DS's
# "stall:" lines are, so a stall can be matched with what happened then.
# At most 200 lines from each. watch_system = 0 in gba-ds.cfg turns it off.
WATCH_PIDS=
# each line as it comes (head would hold them back), 200 at most
watch_lines() {
	local n=0 l max=${3:-200}
	while [ $n -lt $max ] && IFS= read -r l; do
		if [ "$2" = uptime ]; then echo "$1$(cut -d' ' -f1 /proc/uptime) s: ${l:0:300}"; else echo "$1${l:0:300}"; fi
		n=$((n + 1))
	done
}
watch_start() {
	case "$(cfgval watch_system)" in 0|off|false|no) return ;; esac
	local L="$DIR/gba-ds.log"
	if dmesg --help 2>&1 | grep -q -- --follow-new; then
		( dmesg -W 2>/dev/null | watch_lines "kernel: " ) >> "$L" 2>&1 &
		WATCH_PIDS="$WATCH_PIDS $!"
	fi
	if command -v journalctl >/dev/null; then
		( journalctl -f -n 0 -o short-monotonic --no-pager 2>/dev/null | watch_lines "journal: " "" 1000 ) >> "$L" 2>&1 &
		WATCH_PIDS="$WATCH_PIDS $!"
	fi
	if [ -n "$SWAYSOCK" ] && command -v swaymsg >/dev/null; then
		( swaymsg -m -r -t subscribe '["output","window","input","workspace","mode"]' 2>/dev/null | watch_lines "sway event at " uptime ) >> "$L" 2>&1 &
		WATCH_PIDS="$WATCH_PIDS $!"
	fi
	[ -n "$WATCH_PIDS" ] && echo "watching the system during the run (kernel, journal, sway events; watch_system = 0 in gba-ds.cfg: off)" >> "$L"
}
watch_stop() {
	local p
	for p in $WATCH_PIDS; do
		kill $(pgrep -P "$p" 2>/dev/null) "$p" 2>/dev/null
	done
	WATCH_PIDS=
}
trap 'watch_stop; { restore_governors; services_restore; bash "$DIR/screens.sh" off; } >> "$DIR/gba-ds.log" 2>&1' EXIT
trap 'exit 143' TERM INT HUP

watch_start
BEFORE=$!
GBADS_RELAUNCH_STATUS=$RELAUNCH "$GBADS" "$@" > >(exec cat >> "$DIR/gba-ds.log") 2>&1
st=$?
watch_stop
# (the log written out before the trap adds to it: $! is the log's cat
# here, unless this bash doesn't give it, then still a watcher's)
[ "$!" != "$BEFORE" ] && wait $! 2>/dev/null
if [ $st = $RELAUNCH ]; then
	# an update: everything put back as the EXIT trap would (but the
	# services: they stay stopped through the new launch.sh, which starts
	# them when GBA DS closes), then the new launch.sh (which sets the
	# screens up again)
	{ restore_governors; bash "$DIR/screens.sh" off; } >> "$DIR/gba-ds.log" 2>&1
	trap - EXIT
	GBADS_RELAUNCHED=1 exec bash "$DIR/launch.sh" "$@"
fi
exit $st

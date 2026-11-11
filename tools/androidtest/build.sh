#!/bin/sh
# Builds build/host/gba-ds-android: the desktop app compiled as for Android
# (__ANDROID__ on), with these stand-ins for src/android.c's glue, so the
# Android-only code in the shared files (library.c's Settings and Folders,
# ui.c's second display, update.c's APK path...) can be run and looked at
# headless. See README.md beside this file. Run from anywhere after `make`.
set -e
cd "$(dirname "$0")/../.."
here=tools/androidtest
cmd=$(make -n -B build/host/gba-ds 2>/dev/null | grep -- "-o build/host/gba-ds" | head -1)
[ -n "$cmd" ] || { echo "no link command from make (run scripts/setup-host.sh and make first)" >&2; exit 1; }
extra=""
for f in src/http.c src/update.c src/sha256.c; do
	case " $cmd " in *" $f "*) ;; *) extra="$extra $f" ;; esac
done
libs=""
for l in libEGL.so.1 libGLESv2.so.2 libcurl.so.4; do
	p=$(ls /usr/lib/*/"$l" /usr/lib/"$l" 2>/dev/null | head -1)
	[ -n "$p" ] && libs="$libs $p"
done
cmd=$(printf '%s' "$cmd" | sed "s#-o build/host/gba-ds#-D__ANDROID__ -Isrc -I$here/include -o build/host/gba-ds-android $here/stubs_gpu.c $here/stubs_ra.c $here/stubs_extra.c $here/stub_main.c $here/stubs_update.c$extra#")
eval "$cmd $libs"
echo "built build/host/gba-ds-android"

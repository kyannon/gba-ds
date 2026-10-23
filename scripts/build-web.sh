#!/bin/bash
# GBA DS for the web (State Saver's GBA DS tab): the app itself, compiled to
# WebAssembly (wasm32-wasi) with its own SDL2 (offscreen video, software
# renderer, no threads: web/SDL_config_wasi.h) and libmgba, then run through
# binaryen's asyncify so its waits hand control back to the page.
#
#   scripts/build-web.sh [WORKDIR]     -> build/web/gba-ds.wasm
#
# Needs what tools/statesaver/build.sh needs (clang, wasm-ld, llvm-ar,
# cmake, git; deps/mgba and deps/SDL2 from scripts/setup-host.sh), and
# builds binaryen's wasm-opt from GitHub the first time (a few minutes). WORKDIR (default build/wasm) is shared with
# State Saver's build.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
W=$(mkdir -p "${1:-$ROOT/build/wasm}" && cd "${1:-$ROOT/build/wasm}" && pwd)
OUT="$ROOT/build/web"
M="$ROOT/deps/mgba"
S="$ROOT/deps/SDL2"
. "$ROOT/scripts/wasi-toolchain.sh"
. "$ROOT/scripts/mgba-options.sh"
mkdir -p "$OUT"
JOBS=$(nproc)
CFLAGS_W="--target=wasm32-wasip1 --sysroot=$W/sysroot -resource-dir=$W/res -O2 -D_GNU_SOURCE -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS"

# ---- SDL2: its headers with our configuration, and the parts we use
SI="$OUT/sdl-include/SDL2"
if [ ! -f "$OUT/libSDL2.a" ] || [ "$ROOT/web/SDL_config_wasi.h" -nt "$OUT/libSDL2.a" ] || [ "$ROOT/web/sdl_timer_web.c" -nt "$OUT/libSDL2.a" ]; then
	rm -rf "$OUT/sdl-include" "$OUT/sdl-obj"
	mkdir -p "$SI" "$OUT/sdl-obj"
	cp "$S"/include/*.h "$SI/"
	cp "$ROOT/web/SDL_config_wasi.h" "$SI/SDL_config.h"
	SRCS=$(ls "$S"/src/*.c "$S"/src/atomic/*.c "$S"/src/audio/*.c "$S"/src/cpuinfo/*.c "$S"/src/events/*.c \
		"$S"/src/file/SDL_rwops.c "$S"/src/haptic/SDL_haptic.c "$S"/src/joystick/SDL_joystick.c \
		"$S"/src/joystick/SDL_gamecontroller.c "$S"/src/joystick/controller_type.c "$S"/src/joystick/SDL_steam_virtual_gamepad.c \
		"$S"/src/joystick/dummy/*.c "$S"/src/haptic/dummy/*.c "$S"/src/locale/SDL_locale.c "$S"/src/locale/dummy/*.c \
		"$S"/src/misc/SDL_url.c "$S"/src/misc/dummy/*.c "$S"/src/power/SDL_power.c "$S"/src/render/*.c \
		"$S"/src/render/software/*.c "$S"/src/sensor/SDL_sensor.c "$S"/src/sensor/dummy/*.c "$S"/src/stdlib/*.c \
		"$S"/src/libm/*.c "$S"/src/thread/SDL_thread.c "$S"/src/thread/generic/*.c "$S"/src/timer/SDL_timer.c \
		"$S"/src/video/*.c "$S"/src/video/offscreen/*.c "$S"/src/video/yuv2rgb/*.c "$S"/src/filesystem/dummy/*.c \
		"$S"/src/loadso/dummy/*.c "$ROOT/web/sdl_timer_web.c")
	# shellcheck disable=SC2086
	printf '%s\n' $SRCS | xargs -P "$JOBS" -I{} sh -c 'o="$1/sdl-obj/$(echo "$2" | md5sum | cut -c1-12).o"; clang $3 -I"$4" -I"$5/src" -DSDL_config_h_ -include "$4/SDL_config.h" -Wno-everything -c "$2" -o "$o"' _ "$OUT" {} "$CFLAGS_W" "$SI" "$S"
	llvm-ar rcs "$OUT/libSDL2.a" "$OUT"/sdl-obj/*.o
fi
ls -la "$OUT/libSDL2.a"

# ---- libmgba: State Saver's (GBA DS's options, no threads, zlib or LZMA)
if [ ! -f "$W/mgba-build/libmgba.a" ]; then
	echo "building libmgba (tools/statesaver/build.sh)"
	"$ROOT/tools/statesaver/build.sh" "$W" > /dev/null
fi
DEFS=$(sed -n 's/^C_DEFINES = //p' "$W/mgba-build/CMakeFiles/mgba.dir/flags.make")
# (its flags.h says MINIMAL_CORE, which the library itself wasn't built with:
# a struct mCore of another shape; GBA DS includes flags.h)
mkdir -p "$OUT/mgba-flags/mgba"
sed '/^#ifndef MINIMAL_CORE$/,/^#endif$/d' "$W/mgba-build/include/mgba/flags.h" > "$OUT/mgba-flags/mgba/flags.h"

# ---- GBA DS (no RetroAchievements: no network from the page)
APP="src/main.c src/config.c src/font.c src/ui.c src/library.c src/mapper.c src/game.c src/paths.c src/image.c src/brand_data.c
     src/logo.c src/mapframe.c src/cheats.c src/esinteg.c src/textfont.c src/fonts_data.c src/ra.c src/emu.c src/mapcap.c
     src/profiles.c src/logo_masks.c web/web.c"
mkdir -p "$OUT/obj"
VERSION=$(cat "$ROOT/VERSION")
# shellcheck disable=SC2086
printf '%s\n' $APP | xargs -P "$JOBS" -I{} sh -c 'clang $1 -std=gnu11 -DPATH_MAX=4096 -DGBADS_VERSION="\"$2\"" $3 -I"$8/mgba-flags" -I"$4/deps/mgba/include" -I"$5/mgba-build/include" -I"$6" -Wno-everything -c "$4/$7" -o "$8/obj/$(basename "$7" .c).o"' \
	_ "$CFLAGS_W" "$VERSION" "$DEFS" "$ROOT" "$W" "$SI" {} "$OUT"
# shellcheck disable=SC2086
clang $CFLAGS_W -o "$OUT/gba-ds-noasync.wasm" "$OUT"/obj/*.o "$W/mgba-build/libmgba.a" "$OUT/libSDL2.a" \
	"$M/src/util/vfs/vfs-fd.c" "$M/src/util/vfs/vfs-dirent.c" -DENABLE_VFS_FD -DPATH_MAX=4096 $DEFS -I"$M/include" -I"$W/mgba-build/include" \
	-lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-getpid -lwasi-emulated-process-clocks -lm \
	-Wl,--allow-undefined-file=/dev/null -Wl,--import-undefined -Wl,-z,stack-size=1048576 -Wl,--export=malloc 2> >(grep -v warning >&2 || true)

# ---- asyncify: web_sleep unwinds to the page and comes back
[ -n "${GBADS_WEB_NOASYNC:-}" ] && exit 0
WOPT="$W/binaryen-src/build/bin/wasm-opt"
if [ ! -x "$WOPT" ]; then
	[ -d "$W/binaryen-src" ] || git clone -q --depth 1 --branch version_116 https://github.com/WebAssembly/binaryen.git "$W/binaryen-src"
	cmake -S "$W/binaryen-src" -B "$W/binaryen-src/build" -DBUILD_TESTS=OFF -DBUILD_STATIC_LIB=ON -DCMAKE_BUILD_TYPE=Release > /dev/null
	make -C "$W/binaryen-src/build" -j"$JOBS" wasm-opt > /dev/null
fi
"$WOPT" "$OUT/gba-ds-noasync.wasm" -O2 --asyncify \
	--pass-arg=asyncify-imports@env.web_sleep -o "$OUT/gba-ds.wasm"
ls -la "$OUT/gba-ds.wasm"

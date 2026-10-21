#!/bin/bash
# Reproducible cross-build of GBA DS for ROCKNIX (aarch64) on an x86_64 Linux host.
# Produces dist/gba-ds-v<VERSION>-linux-aarch64.zip (ROCKNIX, BananaOS).
#
# Needs: git, cmake, make, zip, an x86_64 host. Everything else is fetched into ./deps.
# Set AARCH64_TOOLCHAIN_BIN to use your own aarch64-linux-gnu-gcc instead.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
DEPS="$ROOT/deps"
mkdir -p "$DEPS"
. scripts/mgba-options.sh   # MGBA_OPTS, MGBA_COMMIT, SDL_BRANCH; shared with setup-host.sh


# --- toolchain -------------------------------------------------------------
if [ -z "${AARCH64_TOOLCHAIN_BIN:-}" ]; then
	TC="$DEPS/aarch64-linux-gnu"
	if [ ! -x "$TC/bin/aarch64-linux-gnu-gcc" ]; then
		git clone --depth 1 https://github.com/radcolor/aarch64-linux-gnu "$TC"
		# The prebuilt toolchain's linker scripts hard-code its CI build path.
		for f in "$TC"/aarch64-linux-gnu/lib/*.so; do
			grep -q /home/runner "$f" 2>/dev/null &&
				sed -i "s|/home/runner/work/build-tools-gcc/build-tools-gcc/aarch64-linux-gnu/aarch64-linux-gnu/lib/|$TC/aarch64-linux-gnu/lib/|g" "$f"
		done
		# ...and its limits.h doesn't chain to glibc's (PATH_MAX etc. missing).
		L=$(echo "$TC"/lib/gcc/aarch64-linux-gnu/*/include-fixed/limits.h)
		if ! grep -qE "gbadual|gba-ds" "$L"; then  # already patched (by either name)
			{ printf '/* gba-ds: chain to glibc limits.h */\n#ifndef _GCC_LIMITS_H_\n#define _GCC_LIMITS_H_\n#include_next <limits.h>\n#endif\n'; cat "$L"; } > "$L.tmp" && mv "$L.tmp" "$L"
		fi
	fi
	export AARCH64_TOOLCHAIN_BIN="$TC/bin"
fi
CROSS="$AARCH64_TOOLCHAIN_BIN/aarch64-linux-gnu-"

# --- libmgba ---------------------------------------------------------------
if [ ! -d "$DEPS/mgba" ]; then
	git clone https://github.com/mgba-emu/mgba.git "$DEPS/mgba"
	git -C "$DEPS/mgba" checkout "$MGBA_COMMIT"
fi
# a build dir configured the old way (LIBMGBA_ONLY) is started over
if grep -q "^LIBMGBA_ONLY:UNINITIALIZED=ON" "$DEPS/mgba/build-aarch64/CMakeCache.txt" 2>/dev/null; then
	rm -rf "$DEPS/mgba/build-aarch64"
fi
cmake -S "$DEPS/mgba" -B "$DEPS/mgba/build-aarch64" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64.cmake" "${MGBA_OPTS[@]}"
make -C "$DEPS/mgba/build-aarch64" -j"$(nproc)" mgba zlibstatic

# --- SDL2 (only to link against; the device's own libSDL2 is used at runtime) ---
if [ ! -d "$DEPS/SDL2" ]; then
	git clone --depth 1 -b "$SDL_BRANCH" https://github.com/libsdl-org/SDL.git "$DEPS/SDL2"
fi
cmake -S "$DEPS/SDL2" -B "$DEPS/SDL2/build-aarch64" -DCMAKE_TOOLCHAIN_FILE="$ROOT/cmake/aarch64.cmake" \
	-DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$DEPS/sdl-aarch64" -DSDL_STATIC=OFF -DSDL_TEST=OFF
make -C "$DEPS/SDL2/build-aarch64" -j"$(nproc)" install

# --- gba-ds ---------------------------------------------------------------
make CROSS="$CROSS" MGBA_SRC="$DEPS/mgba" MGBA_BUILD="$DEPS/mgba/build-aarch64" \
	SDL_PREFIX="$DEPS/sdl-aarch64" OUT=build/aarch64 OPTFLAGS="-O2 -mcpu=cortex-a55"

# --- package ---------------------------------------------------------------
VERSION=$(cat VERSION)
ZIP="gba-ds-v$VERSION-linux-aarch64.zip"
PKG="$ROOT/build/pkg/gba-ds"
rm -rf "$ROOT/build/pkg"
mkdir -p "$PKG"
"${CROSS}strip" -o "$PKG/gba-ds" build/aarch64/gba-ds
cp dist/launch.sh dist/es-play.sh dist/screens.sh dist/gba-ds.cfg dist/gamecontrollerdb.txt dist/banana-play.sh "$PKG/"
mkdir -p "$PKG/logos"
printf "Optional: your own logo images (<CODE>.png or .bmp) override the ones extracted from the games.\n" > "$PKG/logos/README.txt"
mkdir -p "$PKG/cheats"
printf "RetroArch cheat files (.cht) for GBA DS, in this folder or subfolders: picked by name, as RetroArch does (Game (USA).gba -> Game (USA) (Code Breaker).cht).\nRetroArch's own cheat folders are searched too (cheat_dir in gba-ds.cfg).\n" > "$PKG/cheats/README.txt"
mkdir -p "$PKG/overlays"
printf "Overlay PNGs (1024x768, transparent where the picture shows): primary.png (game screen) / secondary.png (map screen) for every game,\n<CODE>-primary.png / <CODE>-secondary.png for one game, optionally with -integer / -fit / -stretch. See README.\n" > "$PKG/overlays/README.txt"
cp README.md "$PKG/README.md"
# mGBA is statically linked (MPL-2.0): ship its licence and where to get its source
mkdir -p "$PKG/licenses"
cp LICENSE "$PKG/licenses/GBA-DS-LICENSE.txt"
cp "$DEPS/mgba/LICENSE" "$PKG/licenses/mGBA-LICENSE.txt"
cp assets/fonts/Jost-OFL.txt "$PKG/licenses/Jost-OFL.txt"
cp third_party/rcheevos/LICENSE "$PKG/licenses/rcheevos-LICENSE.txt"
printf "GBA DS %s is free software under the GNU General Public License, version 3 only (GBA-DS-LICENSE.txt).\nIt statically links libmgba (c) Jeffrey Pfau, MPL-2.0.\nmGBA source: https://github.com/mgba-emu/mgba (commit %s)\nIts text is set in Jost (c) The Jost Project Authors, SIL Open Font License 1.1 (Jost-OFL.txt).\nRetroAchievements support: rcheevos (c) RetroAchievements, MIT (rcheevos-LICENSE.txt), %s.\nGBA DS source: https://github.com/kyannon/gba-ds\n" "$VERSION" "$MGBA_COMMIT" "$(tail -1 third_party/rcheevos/VERSION)" > "$PKG/licenses/README.txt"
cp "dist/GBA DS.sh" "$ROOT/build/pkg/"
# EmulationStation artwork for the Ports entry
mkdir -p "$ROOT/build/pkg/images"
cp assets/brand/gba-ds-icon.png "$ROOT/build/pkg/images/GBA DS.png"
# Dev builds carry the web build for State Saver's GBA DS tab (drop the zip
# there to run this build in a browser). GBADS_WEB_WORK: build-web.sh's WORKDIR.
if [[ $VERSION == *-dev ]]; then
	if "$ROOT/scripts/build-web.sh" ${GBADS_WEB_WORK:+"$GBADS_WEB_WORK"} > "$ROOT/build/web.log" 2>&1; then
		mkdir -p "$PKG/web"
		cp "$ROOT/build/web/gba-ds.wasm" "$PKG/web/"
		echo "$VERSION" > "$PKG/web/VERSION"
	else
		echo "web build failed (build/web.log): the zip has no web/gba-ds.wasm"
	fi
fi
chmod +x "$PKG"/*.sh "$PKG/gba-ds" "$ROOT/build/pkg/GBA DS.sh"
rm -f "$ROOT"/dist/gba-ds-*rocknix-aarch64.zip "$ROOT"/dist/gba-ds-*linux-aarch64.zip
(cd "$ROOT/build/pkg" && zip -qr "$ROOT/dist/$ZIP" gba-ds "GBA DS.sh" images)
echo "built dist/$ZIP"

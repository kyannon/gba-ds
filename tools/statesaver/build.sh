#!/bin/bash
# Builds State Saver's emulator (ss.wasm): GBA DS's own mGBA (deps/mgba, the
# commit and options of scripts/mgba-options.sh) compiled to WebAssembly,
# with ss.c's small API, so the states it saves are GBA DS's byte for byte.
#
#   tools/statesaver/build.sh [WORKDIR]     -> tools/statesaver/ss.wasm
#
# Needs clang, wasm-ld, llvm-ar / llvm-ranlib (LLVM 18 tested), cmake, git,
# and GBA DS's deps/mgba (scripts/setup-host.sh). Fetches wasi-libc and
# compiler-rt's builtins from GitHub into WORKDIR (default build/wasm).
# The page, statesaver.html, is published as an Artifact with ss.wasm beside
# it and the db, assets and downloads capabilities.
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$(mkdir -p "${1:-$ROOT/build/wasm}" && cd "${1:-$ROOT/build/wasm}" && pwd)
M="$ROOT/deps/mgba"
. "$ROOT/scripts/mgba-options.sh"
git -C "$M" rev-parse --short HEAD | grep -q "^${MGBA_COMMIT:0:7}" || echo "warning: deps/mgba is not at $MGBA_COMMIT"

. "$ROOT/scripts/wasi-toolchain.sh"

# libmgba: GBA DS's options, minus threads, zlib and LZMA (no effect on states);
# feature checks can't link a static try-compile, so the false positives are
# turned off by hand
FLAGS="-O2 -D_GNU_SOURCE -DPATH_MAX=4096 -DENABLE_VFS_FD -resource-dir=$W/res -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_GETPID -D_WASI_EMULATED_PROCESS_CLOCKS"
cat > "$W/wasi.cmake" <<TC
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_C_COMPILER clang)
set(CMAKE_AR $(command -v llvm-ar))
set(CMAKE_RANLIB $(command -v llvm-ranlib))
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_SYSROOT $W/sysroot)
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
TC
OPTS=("${MGBA_OPTS[@]}"); OPTS=("${OPTS[@]/-DUSE_LZMA=ON/-DUSE_LZMA=OFF}"); OPTS=("${OPTS[@]/-DUSE_ZLIB=ON/-DUSE_ZLIB=OFF}")
cmake -S "$M" -B "$W/mgba-build" -DCMAKE_TOOLCHAIN_FILE="$W/wasi.cmake" "${OPTS[@]}" -DUSE_PTHREADS=OFF \
	-DHAVE_CRC32=OFF -DHAVE_POPCOUNT32=OFF -DHAVE_SNPRINTF_L=OFF -DHAVE_STRTOF_L=OFF -DCMAKE_C_FLAGS="$FLAGS" > /dev/null
make -C "$W/mgba-build" -j"$(nproc)" mgba > /dev/null

DEFS=$(sed -n 's/^C_DEFINES = //p' "$W/mgba-build/CMakeFiles/mgba.dir/flags.make")
# shellcheck disable=SC2086
clang --target=wasm32-wasip1 --sysroot="$W/sysroot" -mexec-model=reactor $FLAGS $DEFS -I"$M/include" -I"$W/mgba-build/include" \
	"$ROOT/tools/statesaver/ss.c" "$M/src/util/vfs/vfs-fd.c" "$M/src/util/vfs/vfs-dirent.c" "$W/mgba-build/libmgba.a" \
	-lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-getpid -lwasi-emulated-process-clocks \
	-Wl,--export-dynamic -o "$ROOT/tools/statesaver/ss.wasm" 2> >(grep -v warning >&2 || true)
ls -la "$ROOT/tools/statesaver/ss.wasm"

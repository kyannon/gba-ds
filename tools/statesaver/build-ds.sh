#!/bin/bash
# Builds State Saver's DS emulator (mds.wasm): melonDS's core (pinned below)
# compiled to WebAssembly with mds.cpp, its free BIOS and generated firmware,
# so no system files are needed. Run tools/statesaver/build.sh first (same
# WORKDIR): it makes the wasi-libc sysroot and compiler-rt builtins this
# adds libc++ to.
#
#   tools/statesaver/build-ds.sh [WORKDIR]     -> tools/statesaver/mds.wasm
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
W=$(cd "${1:-$ROOT/build/wasm}" && pwd)
MELONDS_COMMIT=906e9eb   # melonDS master, Oct 2026
[ -f "$W/sysroot/lib/wasm32-wasip1/libc.a" ] || { echo "run tools/statesaver/build.sh first"; exit 1; }

# libc++ / libc++abi: static, threads through wasi-libc's single-threaded
# pthread stubs (melonDS includes <thread>; the page never starts one), no exceptions
if [ ! -f "$W/sysroot/lib/libc++.a" ]; then
	git -C "$W/llvm" sparse-checkout set compiler-rt/lib/builtins libcxx libcxxabi libunwind runtimes cmake llvm/cmake llvm/utils/llvm-lit third-party
	cat > "$W/wasi-cxx.cmake" <<TC
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm32)
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)
set(CMAKE_AR $(command -v llvm-ar))
set(CMAKE_RANLIB $(command -v llvm-ranlib))
set(CMAKE_C_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_CXX_COMPILER_TARGET wasm32-wasip1)
set(CMAKE_SYSROOT $W/sysroot)
set(CMAKE_C_FLAGS_INIT "-resource-dir=$W/res")
set(CMAKE_CXX_FLAGS_INIT "-resource-dir=$W/res")
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
set(CMAKE_C_COMPILER_WORKS ON)
set(CMAKE_CXX_COMPILER_WORKS ON)
TC
	cmake -S "$W/llvm/runtimes" -B "$W/cxx-build" -DCMAKE_TOOLCHAIN_FILE="$W/wasi-cxx.cmake" -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$W/sysroot" \
		-DLLVM_ENABLE_RUNTIMES="libcxx;libcxxabi" -DLLVM_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_TESTS=OFF -DLIBCXX_INCLUDE_BENCHMARKS=OFF -DLIBCXXABI_INCLUDE_TESTS=OFF \
		-DLIBCXX_ENABLE_THREADS=ON -DLIBCXX_HAS_PTHREAD_API=ON -DLIBCXX_ENABLE_SHARED=OFF -DLIBCXX_ENABLE_EXCEPTIONS=OFF \
		-DLIBCXX_ENABLE_FILESYSTEM=ON -DLIBCXX_CXX_ABI=libcxxabi -DLIBCXX_HAS_MUSL_LIBC=ON -DLIBCXX_ABI_VERSION=2 -DLIBCXX_ENABLE_MONOTONIC_CLOCK=ON \
		-DLIBCXXABI_ENABLE_EXCEPTIONS=OFF -DLIBCXXABI_ENABLE_SHARED=OFF -DLIBCXXABI_SILENT_TERMINATE=ON -DLIBCXXABI_ENABLE_THREADS=ON -DLIBCXXABI_HAS_PTHREAD_API=ON \
		-DLIBCXXABI_USE_LLVM_UNWINDER=OFF -DLIBCXX_USE_COMPILER_RT=ON -DLIBCXXABI_USE_COMPILER_RT=ON > /dev/null
	make -C "$W/cxx-build" -j"$(nproc)" install > /dev/null
fi

# melonDS
[ -d "$W/melonds" ] || git clone -q https://github.com/melonDS-emu/melonDS.git "$W/melonds"
git -C "$W/melonds" checkout -q "$MELONDS_COMMIT" 2>/dev/null || { git -C "$W/melonds" fetch -q --unshallow 2>/dev/null || true; git -C "$W/melonds" checkout -q "$MELONDS_COMMIT"; }
M="$W/melonds/src"
O="$W/mds-build"
mkdir -p "$O"
sed -e 's|\${melonDS_HOMEPAGE_URL}|https://melonds.kuribo64.net|' -e 's|\${melonDS_VERSION}|1.1|' -e 's|\${MELONDS_VERSION_SUFFIX}|-wasm|' "$M/version.h.in" > "$O/version.h"
# teakra (the DSi DSP) has a few throws on paths that shouldn't run: abort()
# instead, defined after the standard headers so they aren't touched
{ grep -ho "#include <[a-z_/.]*>" "$M"/teakra/src/*.cpp "$M"/teakra/src/*.h "$M"/teakra/include/teakra/*.h | grep -v intrin.h | sort -u
  printf '#include <exception>\n#include <stdexcept>\n#include <cstdlib>\n#define throw ::abort(),\n'; } > "$O/nothrow.h"
BASE="--target=wasm32-wasip1 --sysroot=$W/sysroot -resource-dir=$W/res -O2 -D_GNU_SOURCE -D_WASI_EMULATED_MMAN -D_WASI_EMULATED_SIGNAL -D_WASI_EMULATED_PROCESS_CLOCKS"
CXXF="$BASE -fno-exceptions -std=c++17 -I$O -I$M -I$M/teakra/include -I$M/fatfs"
cd "$M"
SRCS=$(sed -n '/add_library(core STATIC/,/blip_buf.c)/p' CMakeLists.txt | grep -E "\.(cpp|c)\b" | sed 's/)//' | tr -d ' ')
for s in $SRCS; do
	o="$O/$(echo "$s" | tr / _).o"
	case $s in
	*.c) echo "clang $BASE -I$M -I$M/fatfs -c $s -o $o" ;;
	*) echo "clang++ $CXXF -c $s -o $o" ;;
	esac
done > "$O/cmds"
for s in ahbm apbp btdmp disassembler dma timer memory_interface mmio parser processor teakra; do
	echo "clang++ $CXXF -include $O/nothrow.h -c teakra/src/$s.cpp -o $O/teakra_$s.o"
done >> "$O/cmds"
echo "clang++ $CXXF -c $ROOT/tools/statesaver/mds.cpp -o $O/mds.o" >> "$O/cmds"
xargs -P"$(nproc)" -I{} sh -c '{} 2>/dev/null || { echo "failed: {}" >&2; exit 255; }' < "$O/cmds"
# shellcheck disable=SC2086
clang++ $BASE -fno-exceptions -mexec-model=reactor "$O"/*.o -L"$W/sysroot/lib" \
	-lwasi-emulated-mman -lwasi-emulated-signal -lwasi-emulated-process-clocks -Wl,--export-dynamic -o "$ROOT/tools/statesaver/mds.wasm" 2> >(grep -v warning >&2 || true)
ls -la "$ROOT/tools/statesaver/mds.wasm"

#!/bin/bash
# The WebAssembly toolchain State Saver's and GBA DS's web builds share:
# compiler-rt's builtins and wasi-libc for wasm32-wasip1, made with the
# system clang into $W. Sourced, with W set (an absolute directory).
# compiler-rt builtins for wasm32 (the x87 / bfloat16 ones don't build: not needed)
if [ ! -f "$W/res/lib/wasip1/libclang_rt.builtins-wasm32.a" ]; then
	[ -d "$W/llvm" ] || { git clone -q --depth 1 --branch llvmorg-18.1.3 --filter=blob:none --sparse https://github.com/llvm/llvm-project.git "$W/llvm"; git -C "$W/llvm" sparse-checkout set compiler-rt/lib/builtins; }
	mkdir -p "$W/rt"
	for f in "$W"/llvm/compiler-rt/lib/builtins/*.c; do
		b=$(basename "$f" .c)
		case $b in emutls|enable_execute_stack|gcc_personality_v0|clear_cache|os_version_check|atomic|eprintf|trampoline_setup|cpu_model) continue;; esac
		clang --target=wasm32-wasi -O2 -ffreestanding -fno-builtin -c "$f" -o "$W/rt/$b.o" 2>/dev/null || true
	done
	llvm-ar rcs "$W/rt/libclang_rt.builtins-wasm32.a" "$W"/rt/*.o
	rm -rf "$W/res"; cp -r "$(clang -print-resource-dir)" "$W/res"
	mkdir -p "$W/res/lib/wasip1"; cp "$W/rt/libclang_rt.builtins-wasm32.a" "$W/res/lib/wasip1/"
fi

# wasi-libc
if [ ! -f "$W/sysroot/lib/wasm32-wasip1/libc.a" ]; then
	[ -d "$W/wasi-libc" ] || git clone -q --depth 1 https://github.com/WebAssembly/wasi-libc.git "$W/wasi-libc"
	cmake -S "$W/wasi-libc" -B "$W/wasi-libc/build" -DCMAKE_C_COMPILER=clang -DCMAKE_AR="$(command -v llvm-ar)" \
		-DCMAKE_INSTALL_PREFIX="$W/sysroot" -DTARGET_TRIPLE=wasm32-wasip1 -DMALLOC=dlmalloc \
		-DBUILTINS_LIB="$W/rt/libclang_rt.builtins-wasm32.a" -DBUILD_TESTS=OFF -DBUILD_SHARED=OFF -DSETJMP=OFF > /dev/null
	make -C "$W/wasi-libc/build" -j"$(nproc)" install > /dev/null
fi


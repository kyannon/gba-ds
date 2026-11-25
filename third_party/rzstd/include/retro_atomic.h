/* GBA DS's stand-in for libretro-common's retro_atomic.h (2400 lines: every compiler and OS
 * RetroArch builds on). encoding_rzstd.c only needs one thing from it: an int read and
 * written once, safely from any thread, to remember whether the CPU has BMI2. GCC and
 * clang (every compiler GBA DS builds with: the ROCKNIX toolchain, the Android NDK, the
 * web build's clang) have the builtins. Not part of libretro-common. */
#ifndef GBADS_RETRO_ATOMIC_H
#define GBADS_RETRO_ATOMIC_H

typedef int retro_atomic_int_t;
#define retro_atomic_load_acquire_int(p) __atomic_load_n((p), __ATOMIC_ACQUIRE)
#define retro_atomic_store_release_int(p, v) __atomic_store_n((p), (v), __ATOMIC_RELEASE)

#endif

# libmgba build options, sourced by build-rocknix.sh (device) and
# setup-host.sh (desktop), so both builds have the same defines.
#
# Not LIBMGBA_ONLY: that switches every dependency off, and .zip ROMs need
# zlib. Frontends and optional libraries are off one by one instead; zlib
# (with minizip) and LZMA (.7z) come from mGBA's bundled copies, so the
# binary needs nothing new on the device. LTO off: the cross toolchain's
# archiver can't index LTO objects.
MGBA_OPTS=(-DLIBMGBA_ONLY=OFF -DBUILD_STATIC=ON -DBUILD_SHARED=OFF -DCMAKE_BUILD_TYPE=Release -DBUILD_LTO=OFF
	-DBUILD_SDL=OFF -DBUILD_QT=OFF -DBUILD_LIBRETRO=OFF -DBUILD_GL=OFF -DBUILD_GLES2=OFF -DBUILD_GLES3=OFF
	-DBUILD_PERF=OFF -DBUILD_TEST=OFF -DBUILD_SUITE=OFF -DBUILD_CINEMA=OFF -DBUILD_ROM_TEST=OFF
	-DBUILD_UPDATER=OFF -DBUILD_HEADLESS=OFF -DBUILD_PYTHON=OFF -DUSE_EPOXY=OFF
	-DENABLE_DEBUGGERS=OFF -DENABLE_GDB_STUB=OFF -DUSE_EDITLINE=OFF -DUSE_SQLITE3=OFF -DUSE_ELF=OFF
	-DUSE_LZMA=ON -DUSE_ZLIB=ON -DUSE_PNG=OFF -DUSE_LIBZIP=OFF -DUSE_MINIZIP=OFF -DUSE_FFMPEG=OFF
	-DUSE_LUA=OFF -DUSE_JSON_C=OFF -DUSE_DISCORD_RPC=OFF -DUSE_FREETYPE=OFF -DM_CORE_GB=ON
	-DENABLE_SCRIPTING=OFF -DDISABLE_DEPS=OFF
	-DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=ON)
MGBA_COMMIT=143adf5   # mGBA master, Oct 2026 (libmgba API with mAudioBuffer)
SDL_BRANCH=release-2.30.x

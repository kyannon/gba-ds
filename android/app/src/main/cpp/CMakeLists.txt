# Native part of the Android app: libSDL2.so (built from source) and
# libmain.so (GBA DS itself, the same sources as the ROCKNIX build, plus
# src/android.c). libmgba is built beforehand by scripts/build-android.sh.
#
# Set by android/app/build.gradle from Gradle properties:
#   GBADS_SDL_SRC     SDL2 source tree (same branch as the ROCKNIX build)
#   GBADS_MGBA_SRC    mGBA source tree (headers)
#   GBADS_MGBA_BUILD  mGBA build dir for this ABI (libmgba.a, zlib/libz.a)
#   GBADS_VERSION     version string
cmake_minimum_required(VERSION 3.18)
project(gbads C CXX)

get_filename_component(GBADS_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../../.." ABSOLUTE)
foreach(v GBADS_SDL_SRC GBADS_MGBA_SRC GBADS_MGBA_BUILD)
	if(NOT ${v})
		message(FATAL_ERROR "${v} is not set (build with scripts/build-android.sh)")
	endif()
endforeach()

# --- SDL2 (shared: SDLActivity loads libSDL2.so, then libmain.so) ---
set(SDL_SHARED ON CACHE BOOL "" FORCE)
set(SDL_STATIC OFF CACHE BOOL "" FORCE)
set(SDL_TEST OFF CACHE BOOL "" FORCE)
add_subdirectory("${GBADS_SDL_SRC}" SDL)

# --- libmgba (prebuilt, static) ---
add_library(mgba STATIC IMPORTED)
set_target_properties(mgba PROPERTIES IMPORTED_LOCATION "${GBADS_MGBA_BUILD}/libmgba.a")
set(MGBA_LIBS mgba)
if(EXISTS "${GBADS_MGBA_BUILD}/zlib/libz.a")
	add_library(mgba_zlib STATIC IMPORTED)
	set_target_properties(mgba_zlib PROPERTIES IMPORTED_LOCATION "${GBADS_MGBA_BUILD}/zlib/libz.a")
	list(APPEND MGBA_LIBS mgba_zlib)
endif()
# the defines libmgba was built with (its headers depend on them), as the Makefile does
file(STRINGS "${GBADS_MGBA_BUILD}/CMakeFiles/mgba.dir/flags.make" MGBA_DEFINES_LINE REGEX "^C_DEFINES = ")
string(REGEX REPLACE "^C_DEFINES = " "" MGBA_DEFINES "${MGBA_DEFINES_LINE}")
separate_arguments(MGBA_DEFINES)

# --- rcheevos (RetroAchievements, MIT), vendored; the same files as the Makefile's RC_SRC ---
set(RC "${GBADS_ROOT}/third_party/rcheevos")
file(GLOB RC_SRC "${RC}/src/*.c" "${RC}/src/rapi/*.c" "${RC}/src/rcheevos/*.c")
add_library(rcheevos STATIC ${RC_SRC} "${RC}/src/rhash/md5.c")
set_target_properties(rcheevos PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_include_directories(rcheevos PUBLIC "${RC}/include" PRIVATE "${RC}/src")

# --- GBA DS ---
set(S "${GBADS_ROOT}/src")
add_library(main SHARED
	${S}/main.c ${S}/config.c ${S}/font.c ${S}/ui.c ${S}/library.c ${S}/game.c ${S}/paths.c ${S}/image.c ${S}/mapper.c
	${S}/brand_data.c ${S}/logo.c ${S}/mapframe.c ${S}/cheats.c ${S}/esinteg.c ${S}/textfont.c ${S}/fonts_data.c
	${S}/emu.c ${S}/mapcap.c ${S}/profiles.c ${S}/logo_masks.c ${S}/ra.c ${S}/update.c ${S}/http.c ${S}/sha256.c
	${S}/android.c)
set_target_properties(main PROPERTIES C_STANDARD 11 C_EXTENSIONS ON)
target_compile_options(main PRIVATE ${MGBA_DEFINES} -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)
target_compile_definitions(main PRIVATE GBADS_VERSION="${GBADS_VERSION}" GBADS_RA)
target_include_directories(main PRIVATE "${GBADS_MGBA_SRC}/include" "${GBADS_MGBA_BUILD}/include" "${RC}/src")
target_link_libraries(main PRIVATE SDL2::SDL2 ${MGBA_LIBS} rcheevos android log EGL m)

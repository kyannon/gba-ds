#!/bin/sh
# climb copy without its shift against the game's own picture (see src/climb.c CLIMB_TEST):
#   climbcmp.sh ROM STATE FRAMES  (from the repo, after `make tools`; MGBA_BUILD as given to make)
MB=${MGBA_BUILD:-deps/mgba/build-host}
FL=$(make -n tools -B MGBA_BUILD="$MB" 2>/dev/null | grep -o -- '-O2.*-D_REENTRANT' | head -1)
gcc $FL -DCLIMB_TEST -Isrc -o build/host/climb-test tools/scout/climb.c src/emu.c src/mapcap.c src/profiles.c src/logo_masks.c src/logo.c src/patch.c src/climb.c tools/png.c build/host/rzstd/*.o "$MB/libmgba.a" "$MB/zlib/libz.a" -lpng -lpthread -lm 2>&1 | grep error
build/host/climb-test "$1" "$2" ${TMPDIR:-/tmp}/climbcmp "$3"

#!/bin/bash
# Ports entry for GBA DS (ROCKNIX, BananaOS, the RG DS's own Linux). Opens the game library, which
# lists every .gba in the GBA ROM folder (and its subfolders) each time it starts.
# launch.sh is run with bash, not started directly, so it works even when
# the files lost their permissions on the way (unzipped on a computer and
# copied over a network share, say).
D="$(cd "$(dirname "$0")" && pwd)"
if [ ! -f "$D/gba-ds/launch.sh" ]; then
	echo "GBA DS: $D/gba-ds/launch.sh is missing: unzip the whole zip into this folder" > "$D/gba-ds-error.log"
	exit 1
fi
exec bash "$D/gba-ds/launch.sh" "$@"

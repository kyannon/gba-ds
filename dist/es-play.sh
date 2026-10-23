#!/bin/bash
# Started by ROCKNIX's runemu.sh (as a Port) for a GBA game picked in
# EmulationStation with GBA DS as its emulator; see gbads-launch.sh in
# EmulationStation's config folder. The game comes in GBADS_ROM.
exec bash "$(dirname "$0")/launch.sh" "$GBADS_ROM"

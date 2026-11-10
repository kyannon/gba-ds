#!/bin/bash
# GBA DS's web player (web/player): one .html file and a folder zip, in
# build/web-player/ (web/player/pack.py says what's in each).
#
#   scripts/build-web-player.sh                 the player alone (drop builds on it)
#   scripts/build-web-player.sh --build ZIP     with that dev build (or gba-ds.wasm) inside
#   scripts/build-web-player.sh --build         with this tree's build/web/gba-ds.wasm
#                                               (scripts/build-web.sh makes it)
set -euo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
OUT="$ROOT/build/web-player"
ARGS=()
if [ "${1:-}" = "--build" ]; then
	SRC="${2:-$ROOT/build/web/gba-ds.wasm}"
	[ -f "$SRC" ] || { echo "no $SRC (run scripts/build-web.sh first)"; exit 1; }
	ARGS=(--build "$SRC")
fi
python3 "$ROOT/web/player/pack.py" "$OUT" "${ARGS[@]}"

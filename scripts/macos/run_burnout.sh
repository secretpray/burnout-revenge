#!/usr/bin/env bash
# Development launcher: same Burnout-only profile as smoke_game.py, no timer.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
BUILD="$ROOT/build/burnout-mac"
if [ ! -x "$BUILD/runtime/BurnoutRevenge" ]; then
  echo 'Build BurnoutRevenge first; see docs/BUILD.md' >&2
  exit 1
fi
export BURNOUT_GAME_DIR="${BURNOUT_GAME_DIR:-$ROOT/game/burnout/files}"
export BR_DATA_DIR="${BR_DATA_DIR:-$BUILD/data}"
export BR_CACHE_DIR="${BR_CACHE_DIR:-$BUILD/cache}"
export BURNOUT_WHATS_NEW="${BURNOUT_WHATS_NEW:-0}"
# No scripted controller, automated keyboard name, forced mute, or time limit.
cd "$ROOT"
exec "$BUILD/runtime/BurnoutRevenge" "$@"

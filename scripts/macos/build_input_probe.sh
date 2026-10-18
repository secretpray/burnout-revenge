#!/bin/sh
# Developer diagnostic, built against the same local SDL as the port.
set -eu
root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
prefix=${BR_SDL_PREFIX:-"$root/build/deps"}
out=${1:-"$root/build/input-probe"}
mkdir -p "$(dirname -- "$out")"
c++ -std=c++20 -O2 -Wall -Wextra -I"$prefix/include" -I"$root/runtime" \
    "$root/scripts/diagnostics/input_probe.cpp" "$prefix/lib/libSDL3.0.dylib" \
    -Wl,-rpath,"$prefix/lib" -o "$out"
printf 'Built %s\n' "$out"

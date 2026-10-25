#!/usr/bin/env bash
# Burnout Revenge. GPL-3.0-or-later (see COPYING).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
ARCH=$(uname -m)
if [[ $ARCH == x86_64 ]]; then
    if ! /lib64/ld-linux-x86-64.so.2 --help | grep -q 'x86-64-v3 (supported'; then
        echo 'Burnout Revenge needs an x86-64-v3 CPU (including AVX2/FMA and OS AVX support).' >&2
        exit 64
    fi
elif [[ $ARCH != aarch64 ]]; then
    echo "Unsupported CPU architecture: $ARCH" >&2
    exit 64
fi
export LD_LIBRARY_PATH="$ROOT/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
exec "$ROOT/lib/BurnoutRevenge" "$@"

#!/usr/bin/env bash
# Burnout Revenge. GPL-3.0-or-later (see COPYING).
set -euo pipefail
ROOT=$(cd "$(dirname "$(readlink -f "$0")")" && pwd)
ARCH=$(uname -m)
if [[ -f $ROOT/ARCHITECTURE ]]; then
    PACKAGE_ARCH=$(cat "$ROOT/ARCHITECTURE")
    [[ $ARCH == "$PACKAGE_ARCH" ]] || { echo "This package needs $PACKAGE_ARCH; this system is $ARCH." >&2; exit 64; }
fi
if [[ -f $ROOT/MIN_GLIBC ]]; then
    MIN_GLIBC=$(cat "$ROOT/MIN_GLIBC")
    CURRENT_GLIBC=$(getconf GNU_LIBC_VERSION | awk '{print $2}')
    if [[ $(printf '%s\n%s\n' "$MIN_GLIBC" "$CURRENT_GLIBC" | sort -V | head -1) != "$MIN_GLIBC" ]]; then
        echo "This build needs glibc $MIN_GLIBC or newer; this system has $CURRENT_GLIBC." >&2
        exit 65
    fi
fi
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
if [[ ${1:-} == --check-system ]]; then
    LIBRARIES=$(ldd "$ROOT/lib/BurnoutRevenge" 2>&1) || { printf '%s\n' "$LIBRARIES" >&2; exit 65; }
    if [[ $LIBRARIES == *'not found'* ]]; then
        printf 'Missing system libraries:\n%s\nSee README.md for dependencies.\n' "$LIBRARIES" >&2
        exit 65
    fi
    printf 'CPU, glibc and linked libraries checked. Vulkan GPU compatibility needs a game launch.\n'
    exit 0
fi
exec "$ROOT/lib/BurnoutRevenge" "$@"

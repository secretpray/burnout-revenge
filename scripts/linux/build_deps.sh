#!/usr/bin/env bash
# Burnout Revenge. GPL-3.0-or-later (see COPYING).
# Pinned SDL and patched FFmpeg. Vulkan loader/ICD remain system components.
set -euo pipefail
[[ $(uname -s) == Linux ]] || { echo 'Linux only' >&2; exit 1; }
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
PREFIX=${1:-$ROOT/build/linux-deps}
mkdir -p "$PREFIX"
PREFIX=$(realpath "$PREFIX")
mkdir -p "$PREFIX/downloads" "$PREFIX/share/licenses" "$PREFIX/build-logs"
JOBS=${BR_JOBS:-2}
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT
fetch() {
    local name=$1 url=$2 sum=$3
    if [[ ! -f $PREFIX/downloads/$name ]]; then
        curl --fail --location --retry 3 "$url" -o "$PREFIX/downloads/$name.partial"
        mv "$PREFIX/downloads/$name.partial" "$PREFIX/downloads/$name"
    fi
    echo "$sum  $PREFIX/downloads/$name" | sha256sum --check --status
}
fetch SDL3-3.4.16.tar.gz https://github.com/libsdl-org/SDL/releases/download/release-3.4.16/SDL3-3.4.16.tar.gz 7322236cd12090c3eb40b9728be4d49c76f66ad17d04369584d4ecad5cf77c68
fetch ffmpeg-7.1.1.tar.xz https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz 733984395e0dbbe5c046abda2dc49a5544e7e0e1e2366bba849222ae9e3a03b1
tar -xf "$PREFIX/downloads/SDL3-3.4.16.tar.gz" -C "$WORK"
cmake -S "$WORK/SDL3-3.4.16" -B "$WORK/sdl-build" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$PREFIX" -DCMAKE_INSTALL_LIBDIR=lib \
    -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}" \
    "-DCMAKE_C_FLAGS=-ffile-prefix-map=$WORK=. -ffile-prefix-map=$ROOT=." \
    -DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TESTS=OFF -DSDL_TEST_LIBRARY=OFF \
    -DCMAKE_SKIP_RPATH=ON > "$PREFIX/build-logs/sdl-configure.log" 2>&1
cmake --build "$WORK/sdl-build" -j "$JOBS" > "$PREFIX/build-logs/sdl-build.log" 2>&1
cmake --install "$WORK/sdl-build" > "$PREFIX/build-logs/sdl-install.log" 2>&1
mkdir -p "$PREFIX/share/licenses/SDL3" "$PREFIX/share/licenses/FFmpeg"
cp "$WORK/SDL3-3.4.16/LICENSE.txt" "$PREFIX/share/licenses/SDL3/"
BR_FFMPEG_MUSIC=1 BR_JOBS="$JOBS" "$ROOT/scripts/build_ffmpeg_xma.sh" \
    "$PREFIX/downloads/ffmpeg-7.1.1.tar.xz" "$PREFIX/ffmpeg-xma" --cc="${CC:-clang}" \
    > "$PREFIX/build-logs/ffmpeg.log" 2>&1
tar -xf "$PREFIX/downloads/ffmpeg-7.1.1.tar.xz" -C "$WORK" ffmpeg-7.1.1/COPYING.LGPLv2.1 ffmpeg-7.1.1/LICENSE.md
cp "$WORK/ffmpeg-7.1.1/"{COPYING.LGPLv2.1,LICENSE.md} "$PREFIX/share/licenses/FFmpeg/"
{
    echo 'SDL 3.4.16; FFmpeg 7.1.1 + repository patches/ffmpeg; Linux My Music enabled'
    uname -sm
    "${CC:-clang}" --version | head -1
    ldd --version | head -1
    cmake --version | head -1
    if command -v dpkg-query >/dev/null; then
        dpkg-query -W 'glslang*' 'libvulkan*' 'libstdc++*' 2>/dev/null || true
    fi
    sha256sum "$PREFIX/downloads/"* "$ROOT/patches/ffmpeg/"*.patch
} > "$PREFIX/share/linux-dependencies.txt"
echo "Dependencies installed in $PREFIX"

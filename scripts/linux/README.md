# Burnout Revenge Linux candidate

This is an internal release candidate. Native Linux GPU qualification is still
required; a successful VM/container run does not establish hardware support,
Steam Deck compatibility, performance or beta readiness. The Mac release remains
separate. See `bundle-info.json` for this build's revision, architecture and
measured glibc/libstdc++ symbol requirements.

Extract the archive into a folder you own and run `./burnout-revenge.sh`.
The x86-64 build requires x86-64-v3 (AVX2/FMA and OS AVX support). Install your
distribution's Vulkan loader and GPU driver, curl with CA certificates, and the
usual SDL display/audio dependencies. The package never replaces glibc or GPU
drivers. X11 and Wayland must be qualified separately. ARM64 packages are for
infrastructure diagnostics only, outside the first beta's supported scope.

The first run installs from your own supported USA disc image or extracted game
folder. No game files are included. A terminal installation is also available:

```
./burnout-revenge.sh --install /path/to/your-disc.iso --dest /path/to/game
```

Saves/settings use `$XDG_DATA_HOME/burnout-revenge` (default
`~/.local/share/burnout-revenge`); caches use `$XDG_CACHE_HOME/burnout-revenge`
(default `~/.cache/burnout-revenge`). Check the startup log for the actual paths.
For isolated testing, always set `BR_DATA_DIR` and `BR_CACHE_DIR` to scratch
folders. `--portable on` enables the existing portable layout beside the launcher;
that folder must be writable. Replacing an ordinary package must preserve user
data. Never extract an update over portable data without a backup.

For desktop integration, copy the supplied desktop entry and icon to your user
applications/icons folders and change its Exec/Icon fields to the absolute paths
of your installation. Paths containing spaces need quoting in Exec. Adding the
launcher to Steam is optional; Steam Input/Home delivery remains a separate test.

My Music reads local MP3, AAC/M4A, ALAC, FLAC, WAV, AIFF, Ogg Vorbis and Opus
through FFmpeg at 48 kHz stereo. EA Trax retains the patched XMA/WMA path. Device
switching, physical rumble, listening and sleep/resume require hardware checks.

Before calling a candidate a beta, validate install → World Tour → controlled
Race → result → native Save → restart → Reload, Crash, six solo modes,
Rewind/Classic/Replay, two-controller ownership, network/Locker and Mac↔Linux;
run at least 60 minutes on the declared native GPU profile. Record hardware,
driver, kernel, display/session, settings and frame-time/memory evidence.

## Building

Use Clang 18 or newer with a C++20 standard library supporting `std::format`,
CMake 3.21+, Ninja, Python 3, git, curl, make, nasm, pkg-config, Vulkan/glslang
development packages and SDL's X11/Wayland/audio development packages. On Ubuntu:

```
sudo apt-get install clang lld cmake ninja-build python3 git curl xz-utils make nasm pkg-config \
  libvulkan-dev vulkan-tools glslang-dev glslang-tools patchelf \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxfixes-dev libxss-dev \
  libxkbcommon-dev libwayland-dev libasound2-dev libpulse-dev libpipewire-0.3-dev \
  libudev-dev libdrm-dev libgbm-dev libegl-dev libgl-dev libdecor-0-dev
scripts/linux/inventory.sh > environment.txt 2>&1
scripts/linux/build_deps.sh
scripts/setup_tools.sh
# Generate ppc/burnout from your own supported default.xex, as in the root README.
cmake --preset linux-release
cmake --build --preset linux-release
ctest --preset linux-release
# Running directly from the build folder requires LD_LIBRARY_PATH=build/linux-deps/lib.
scripts/linux/make_bundle.py build/linux-release build/linux-package \
  --deps build/linux-deps --allow-unverified
```

Build inside the intended minimum SDK; do not infer SteamOS support from a newer
Ubuntu binary. Dependencies are pinned by source SHA-256 where built here;
`linux-dependencies.txt` records compiler and system Vulkan/glslang packages.
Use `BR_JOBS` to cap dependency build parallelism (default 2). Generated PPC,
XEX/ISO, user saves and private logs must never enter public CI or archives.
Asset-free CI uses `cmake --preset portable-tests`, followed by the matching
build/test presets. Full tests require the private generated headers/toolchain.

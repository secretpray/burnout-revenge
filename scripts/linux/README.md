# Burnout Revenge for Linux (experimental)

Check `bundle-info.json` for this build's qualification, revision, architecture
and measured glibc/libstdc++ symbol requirements. An internal RC is unverified.
An emulator-tested experimental beta includes `VALIDATION.json` with its exact
test environment and completed checks. It has only been tested in UTM on a Mac:
native Linux PC hardware, Steam Deck compatibility and performance are untested.
The stable Mac release remains separate.

## Install and play

You need a graphical Linux session, an **x86-64-v3 CPU (AVX2/FMA)**, **glibc
2.38+**, **GLIBCXX_3.4.32** and a Vulkan 1.2+ driver with dynamic rendering and push descriptors.
This build uses an Ubuntu 24.04 SDK; Ubuntu 22.04's own glibc is too old. Hardware compatibility and minimum
performance requirements have not been established. ARM64 is diagnostic only.

1. Download the Linux `tar.xz` archive and extract it with your file manager.
2. Open a terminal in the extracted `BurnoutRevenge` folder and run:

   ```sh
   ./install.sh
   ```

3. Open **Burnout Revenge** from your applications menu. On first launch, choose
   your own supported **USA Xbox 360 disc image** or extracted game folder.

The installer needs **no sudo**. It checks the package, CPU and linked libraries,
copies the app into your user directory and creates the menu entry. You can
delete the extracted download afterwards. Game files are not included.

Prefer to keep the app in its extracted folder? Run `./burnout-revenge.sh`
directly; installing a menu entry is optional. To check CPU and linked libraries
without starting the game, run `./burnout-revenge.sh --check-system`.

If system dependencies are missing, install your distribution's Vulkan loader
and GPU driver, C++ runtime, curl with CA certificates, and SDL's normal
X11/Wayland/audio dependencies. The package does not replace system libraries or
GPU drivers. Check `bundle-info.json` and the release's tested-system table.

## Update or remove

**Update:** quit the game, extract the new Linux archive, then run its
`./install.sh`. Your installed game, saves and settings stay in place.

**Remove the app:** run `~/.local/bin/burnout-revenge --uninstall`, or run
`./uninstall.sh` from an extracted archive. Your game, saves and settings stay.

## Data and advanced use

A terminal game installation is also available:

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

App files live in `$XDG_DATA_HOME/burnout-revenge/runtime` (normally
`~/.local/share/burnout-revenge/runtime`), with a launcher in `~/.local/bin`.
The installed app's data remains separate from its versioned runtime folders.
The desktop entry follows the freedesktop standard and works with paths
containing spaces. Adding the launcher to Steam is optional; Steam Input/Home
delivery remains a separate test.

My Music reads local MP3, AAC/M4A, ALAC, FLAC, WAV, AIFF, Ogg Vorbis and Opus
through FFmpeg at 48 kHz stereo. EA Trax retains the patched XMA/WMA path. Device
switching, physical rumble, listening and sleep/resume require hardware checks.

## Validation

Before publishing an emulator-tested experimental beta, validate install → World Tour → controlled
Race → result → native Save → restart → Reload, Crash, six solo modes,
Rewind/Classic/Replay, two-controller ownership, network/Locker and Mac↔Linux;
record what passed, failed or remains untested on each architecture. Run at least
60 minutes in the declared VM profile and record driver, kernel, display/session,
settings and memory evidence. VM results do not qualify native Linux GPUs.
Physical controls, listening, real device switching and native GPU endurance
remain separate hardware checks; do not describe scripted or muted runs as those
checks. Known runtime failures must be resolved or explicitly scoped out before
publication.

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

After validation, `--emulator-validation /path/to/report.json` replaces
`--allow-unverified`. The report must use schema `1`, the exact build `revision`
and `architecture`, an `environments` list, `native_hardware_tested: false`, and
`checks` entries `full_tests`, `clean_install`, `gameplay_save_reload`, `display`
and `endurance`, all set to `"pass"`. Include the detailed test matrix and known
limitations in the release notes. The packager records this evidence; it does
not run or establish those checks itself.

Build inside the intended minimum SDK; do not infer SteamOS support from a newer
Ubuntu binary. Dependencies are pinned by source SHA-256 where built here;
`linux-dependencies.txt` records compiler and system Vulkan/glslang packages.
Use `BR_JOBS` to cap dependency build parallelism (default 2). Generated PPC,
XEX/ISO, user saves and private logs must never enter public CI or archives.
Asset-free CI uses `cmake --preset portable-tests`, followed by the matching
build/test presets. Full tests require the private generated headers/toolchain.

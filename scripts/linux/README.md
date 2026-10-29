# Burnout Revenge for Linux and Steam Deck

Check `bundle-info.json` for the build revision, architecture and measured
glibc/libstdc++ requirements. `VALIDATION.json` records checks for this exact
candidate, earlier baseline evidence and limitations. macOS and Linux/Steam
Deck share one release page and version. Game data is not included.

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

## Desktop launcher and Steam

Run `./install.sh` first. It creates **Burnout Revenge** in the applications menu
with the game's icon. In Desktop Steam choose **Games > Add a Non-Steam Game**
and select that installed entry. The name is **Burnout Revenge**, not a test
script's filename.

If the entry is not listed, use **Browse > All Files** and select the installed
`~/.local/bin/burnout-revenge` launcher. Set the shortcut name to **Burnout Revenge**
and choose the package's `burnout-revenge.png` as the shortcut icon in Properties.
The launcher is valid native Linux input for Steam. Leave forced Proton off;
use a **Gamepad** template. Do not add the program in `lib/` on its own.

For a portable extracted setup, add `burnout-revenge.sh` instead and keep its
folder. Version0.1.29 omits the unusable archive `.desktop` file; installation
still generates the correct applications-menu entry.

On **Steam Deck**, return to Gaming Mode after adding the game. Use1280×800 and
60Hz (also on OLED). The native Linux package is the Deck package: no Proton,
VM, developer tools or separate Deck executable is needed. Add your own supported
USA Xbox360 disc image on first launch. For library cover art, use the included
`artwork/burnout-revenge-cover.jpg` via **Manage > Set custom artwork** in Desktop
Steam. Library artwork and the shortcut icon are separate Steam settings; the
installer does not edit Steam account files.

If one controller button appears to produce both a controller action and a
keyboard action, check whether Steam Input or another mapper is also sending a
key. Compare once with Steam fully exited and other mappers stopped. In Steam,
use a **Gamepad** template for the shortcut rather than a keyboard/mouse layout.
This is a diagnostic comparison, not proof that the game's input is correct.

For graphical artifacts or input problems, reproduce the issue and use
**Settings > Advanced > Create bug report**. Include your distro, GPU/driver,
controller/connection and whether Steam was running, plus a short video of the
artifact when possible. For FPS, enable **Settings > Advanced > Performance
Overlay**; Steam is not required. Reports contain the build, settings, GPU and
session log.
Do not attach your disc image or game files.

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

Before publishing an emulator-tested experimental package, validate install → World Tour → controlled
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

For a native candidate, `--native-validation /path/to/report.json` replaces
`--allow-unverified`. Schema2 requires the exact revision/architecture, a native
hardware environment, explicit limitations and passing `full_tests`,
`clean_install` and `candidate_smoke` checks. Earlier gameplay/endurance evidence
belongs in a separate baseline section; do not relabel it as a new candidate run.

For legacy VM reports, `--emulator-validation /path/to/report.json` replaces
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

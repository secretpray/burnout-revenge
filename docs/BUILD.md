# Build from source

[macOS](#macos) · [Linux](#linux)

## macOS

Needs Xcode command-line tools, Homebrew LLVM (Clang 18+), CMake, Ninja, Git,
Python 3 and Pillow for regenerating the icon. Game data is never downloaded.

```sh
brew install llvm cmake ninja
scripts/setup_tools.sh
scripts/macos/build_deps.sh "$PWD/build/deps"
```

Install your own supported ISO with a released app first, then copy its game
folder to `game/burnout/files`, or extract the ISO with the extract-xiso tool
built by setup. The XEX's SHA-256 must be
`5258c75d9ae649ed4fb71d4e2fe8808545db16092cfe764ea1a2504f5aae2e2d`.

```sh
mkdir -p game/burnout
# Example using your own image; extract-xiso creates the output directory.
tools/extract-xiso/build/extract-xiso -x -d game/burnout/files /path/to/Burnout.iso
python3 scripts/macos/recompile_burnout.py
cmake -S . -B build/burnout-mac -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBR_TESTS=ON \
  -DCMAKE_C_COMPILER=/opt/homebrew/opt/llvm/bin/clang \
  -DCMAKE_CXX_COMPILER=/opt/homebrew/opt/llvm/bin/clang++ \
  -DCMAKE_PREFIX_PATH="$PWD/build/deps" \
  -DFFMPEG_XMA="$PWD/build/deps/ffmpeg-xma" \
  -DBR_MOLTENVK=/path/to/MoltenVK/dylib/macOS/libMoltenVK.dylib
cmake --build build/burnout-mac -j4
ctest --test-dir build/burnout-mac --output-on-failure
scripts/macos/run_burnout.sh
```

Use MoltenVK **1.4.2**, the driver pinned by packaging. It is available from
[MoltenVK releases](https://github.com/KhronosGroup/MoltenVK/releases/tag/v1.4.2).
Libraries are built for the minimum supported macOS (15.0), not Homebrew's current
OS. The packager rejects a dependency requiring a newer deployment target.

```sh
MOLTENVK=/path/to/MoltenVK/dylib/macOS/libMoltenVK.dylib \
MOLTENVK_VERSION=1.4.2 python3 scripts/macos/make_app.py \
  build/burnout-mac build/app --game=burnout
python3 scripts/macos/check_app.py "build/app/Burnout Revenge.app"
```

`--dmg` also makes the release's disk image, its Finder window laid out on the
background in `assets/dmg-background*.png` (`scripts/macos/make_dmg_background.py`
draws it). The layout is written by [dmgbuild](https://github.com/dmgbuild/dmgbuild)
(MIT), from a Python that has it; `DMG_PLAIN=1` makes a plain image without it:

```sh
python3 -m venv tools/dmg-python && tools/dmg-python/bin/pip install dmgbuild==1.6.5
DMG_PYTHON=tools/dmg-python/bin/python3 MOLTENVK=... MOLTENVK_VERSION=1.4.2 \
  python3 scripts/macos/make_app.py build/burnout-mac build/app --game=burnout --dmg
```

Generated `ppc/`, `game/`, dependency `tools/` and all build artifacts are ignored.
A fresh build regenerates all game code from your own XEX. The optional
`BR_PREBUILT_RECOMP` cache value can reuse a locally generated library only when
its XEX, recompiler patches, compiler and build options are identical; normal
source builds leave it empty. It does not replace source generation or validation.

## Linux


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

For a release package, replace `--allow-unverified` with
`--native-validation /path/to/report.json`. Schema 2 requires the exact revision
and architecture, a native hardware environment, explicit limitations and
passing `full_tests`, `clean_install` and `candidate_smoke` checks. The packager
records the report; it does not perform those checks. Keep results from older
builds separate. Historical schema 1 VM reports use `--emulator-validation`.

Build against the intended minimum SDK. Dependency versions and hashes are
recorded in `linux-dependencies.txt`. Use `BR_JOBS` to limit build parallelism.
Generated PPC, game files, saves and private logs must not enter public CI or
source archives. Asset-free tests use the `portable-tests` configure/build/test
presets; full tests require locally generated game headers.

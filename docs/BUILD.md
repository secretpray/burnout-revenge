# Build on Apple silicon

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

Generated `ppc/`, `game/`, dependency `tools/` and all build artifacts are ignored.
A fresh build regenerates all game code from your own XEX. The optional
`BR_PREBUILT_RECOMP` cache value can reuse a locally generated library only when
its XEX, recompiler patches, compiler and build options are identical; normal
source builds leave it empty. It does not replace source generation or validation.

# Burnout Revenge for Mac

An experimental, unofficial native port of **Burnout Revenge (Xbox 360, USA)**
for Apple silicon. PowerPC code is recompiled ahead of time; graphics run through
Vulkan/MoltenVK. Bring your own disc image: no ISO, extracted game files, add-on
packs or generated PPC source files are included in this repository or the
downloads. The application contains native code recompiled from the supported
executable and requires the matching user-supplied ISO for its assets.

[Download](https://github.com/secretpray/burnout-revenge/releases) ·
[Build](docs/BUILD.md) · [Progress and limitations](docs/PROGRESS.md) ·
[Rendering research](docs/RENDERING.md) · [Credits](CREDITS.md)

## Install

1. Download and open the macOS Apple silicon DMG from Releases, then drag
   **Burnout Revenge.app** to Applications.
2. Open it. The app is signed ad hoc, not notarized. If macOS blocks it, use
   System Settings → Privacy & Security → Open Anyway.
3. On the first launch, choose your **USA Xbox 360 ISO**. Optionally choose the
   folder with your own add-on car packs, then install. All 802 game files are
   checked against the supported version.
4. Later launches go directly to the game. Settings and saves are kept in
   `~/Library/Application Support/BurnoutRevenge`; caches in
   `~/Library/Caches/BurnoutRevenge`.

Requires an Apple silicon Mac with macOS 15 or later. Development and races were
checked on an M1 Pro running macOS 27.0.1; other Mac models and a complete career
playthrough are not yet verified. PAL/Japanese discs, title updates, GOD packages
and Xbox save imports are not supported in this release.

## Play and configure

Xbox-compatible controllers and DualSense work through SDL. Keyboard defaults
map W/S to right/left trigger, A/D and arrows to steering, Space to A, Backspace
to B, Left Shift to X, Left Control to Y, Q/E to shoulder buttons, and Return to
Start. See the game's own control options for each action. Every keyboard
mapping can be changed in the port's **Controls** page.

Open settings with **F1**, **Cmd+,**, or **Back + Start** on a controller
(Share/Create + Options on DualSense). Changes apply live unless marked restart.

Recommended starting point on M1 Pro: 1.5x internal resolution (1080p), Bicubic,
anti-aliasing enabled, sharpening 0.3 and 16x anisotropic filtering. Use 1x when
GPU time is too high. FSR 1 and experimental MetalFX Spatial are available for
upscaling; neither generates frames. The game's simulation remains capped at 60.

On Mac, DualSense defaults to Apple's controller backend. A restart-only
**DualSense Raw HID Driver** option restores SDL's raw backend. This is a
compatibility mitigation for a reported system-wide Magic Keyboard disconnect;
a causal link and hardware fix have not been established. See
[known issues](docs/PROGRESS.md).

Add-ons can also be imported from Settings. Automatic import searches mounted
`XBOX360/454107DC/00000002` folders. Imported packs connect at the next game
startup. Six user-supplied packs were enumerated locally; every car's unlock and
availability has not been exhaustively verified.

## Origins and license

This project was built on the work of
[SpeedBreaker](https://github.com/SpeedBreakerProject/speedbreaker), updated to
its v0.1.6 runtime, and the Burnout research in
[Xbox360-Native-Ports](https://github.com/CrownParkComputing/Xbox360-Native-Ports/releases/tag/burnoutrevenge-v1).
It also relies on XenonRecomp, Xenia research, SDL, MoltenVK, FFmpeg, AMD FidelityFX
and other projects listed in [CREDITS.md](CREDITS.md) and
[THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

Runtime modifications are **GPL-3.0-or-later**; original authors' notices and
third-party licenses are retained. See [COPYING](COPYING) and [NOTICE](NOTICE).
Cover artwork and the Burnout name belong to their respective owners and are not
covered by the code license. This fan project is not affiliated with or endorsed
by Electronic Arts, Criterion Games, Sony or Apple. Software is provided without
warranty. Do not submit game files, disc images or private profiles to issues.

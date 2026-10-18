# v0.1.0 release validation

Published 2026-10-18 (Europe/Kyiv). The port is experimental. Initially marked
pre-release on GitHub; changed to a normal Latest release on the same date so
it appears on the repository homepage. No binary, tag or checksum changed.

- [Public source](https://github.com/secretpray/burnout-revenge)
- [Release and downloads](https://github.com/secretpray/burnout-revenge/releases/tag/v0.1.0)
- Source tag `v0.1.0`, build revision `23c8e77`.
- Apple silicon, macOS 15.0+. Ad-hoc signed; not notarized.

## Package integrity

| File | Bytes | SHA-256 |
| --- | ---: | --- |
| BurnoutRevenge-0.1.0-macos.dmg | 19599291 | `ded1ecdc623d69f3b03ae8553480d399f490b3c8ddc24a852b7d634fdbba1a06` |
| BurnoutRevenge-0.1.0-third-party-source.tar.xz | 11390044 | `93962fb835829f83ab051bba27bbd540a0f8907da0eedb1c2ea26a4cfc38a9f7` |

GitHub asset digests match these local checksums. Both assets were downloaded
again from the public release and passed `shasum -a 256 -c SHA256SUMS`. `SHA256SUMS` and individual checksum
files are included with the release. Original GPL credits are preserved; exact
FFmpeg/libmspack sources, patches and configuration scripts accompany the binary.

## Evidence and scope

Standalone tests: 60/60. Original dual-game integration: Burnout 62/62,
Most Wanted 61/61 after input changes. Package signature, bundled-only dependency
paths, minimum OS imports and DMG checks passed. All ten ICNS representations have
transparent corners. Direct-binary startup applied the app icon; actual Dock
appearance was not visually confirmed.

Run 82 verified all 802 ISO files and connected six user-supplied add-ons. Run 85
verified repeat startup and functional Bicubic/MetalFX Spatial/FSR 1 switching.
Scenes differed, so it does not support an A/B quality or speed claim. Final
Applications run 86 exited normally after 105.1 seconds, showed a 111 MPH/60 FPS
race and loaded a 2035 KB pipeline cache without cache-serialization or MoltenVK
errors. Crash/replay intervals affect whole-run FPS; stable 60 FPS is not claimed.

No live frame generation is enabled. GPU history and offline interpolation are
research paths. 57 unsupported fatal import stubs remain. No full-career,
all-car/track, online or GOD compatibility claim is made.

Magic Keyboard stopped responding system-wide again during remote UI automation.
Automation was stopped. Causality remains unknown; USB DualSense was reported.
Native controller backend and focus cleanup are not established hardware fixes.
Further keyboard checks should be manual and without remote UI control.

Post-publication clean-install audit found a main-thread SDL audio-startup defect
and intermittent Bluetooth input failures. See [validation and hotfix work](RELEASE_VALIDATION_2026-10-18.md).

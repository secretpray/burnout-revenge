# Progress and known limitations

Updated 2026-10-17. Release candidate preparation is in progress.

## Verified before standalone extraction

- Menus, profile creation/load, first races, engine/collision effects and EA Trax.
- Six user-owned add-on packages enumerated; every unlocked car not verified.
- GPU temporal history/Metal texture export and resize 720p → 1080p → 720p.
- SpeedBreaker v0.1.6 integrated into the dual-game development checkout.
- Dual-game regression suite: Burnout 62/62, Most Wanted 61/61 before final
  keyboard compatibility changes. Final standalone checks are recorded below.
- Run 80: FSR pipelines compile and upscale 1080p to 4K, normal exit. Captured
  profile selection, so it is not an active-race performance benchmark.

## Known issues

This is an experimental port, not a completed compatibility layer. At the last
import audit, 218 imports had 161 handlers and 57 fatal unsupported stubs. Tested
paths do not invoke those stubs; other modes can. Online/multiplayer, full career,
all track/car combinations and every crash/replay path are not verified.

Magic Keyboard was reported to stop responding across macOS applications after
leaving the game with a Bluetooth DualSense connected. Local bluetoothd logs show
actual disconnections for both devices; they do not establish causation. Apple's
native controller backend is now the Mac default, with raw HID opt-in/restart.
Focus loss releases all input grabs and clears keyboard state. Actual Bluetooth
reliability requires user hardware confirmation; automated keyboard events cannot
prove physical accessory health. Raw device logs are private and not published.

Live frame generation is off. GOD, other game revisions and Xbox save import are
not supported. First-time shader creation can hitch. No 120/144 FPS claim.

## Remaining work

- [ ] Final standalone build, unit tests and packaged-app checks.
- [ ] Fresh ISO install with optional add-ons, then direct second startup.
- [ ] Active race with FSR and separate MetalFX regression.
- [ ] Real Magic Keyboard + Bluetooth DualSense focus-switch confirmation.
- [ ] First release source/package/license audit and publication.
- [ ] Full career and broader track/car coverage.
- [ ] Safe GPU-history consumer, live MetalFX FG and pacing on 120 Hz.
- [ ] GOD input support.

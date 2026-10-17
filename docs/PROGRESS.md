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

### Run 81 (dual-game integration, before standalone build)

105.1 s, exit 0, no missing imports. Screenshot confirms active race at 110 MPH
and 60 FPS with FSR 1, internal 1080p → 4K output. Crash/replay intervals also
occurred; GPU timestamps were off, so no GPU execution cost is inferred from
zero log fields. Scripted focus loss/gain releases input and returns grab=0.
Physical Bluetooth keyboard behavior remains unverified.

### Standalone checks and run 82

The first standalone suite exposed donor-specific fixtures: removed SteamOS
launcher and legacy folder-migration tests (those features were removed), updated
control ordering for the new DualSense option and quit text, and pinned an actual
Burnout switch dispatch with deliberately escaping labels/defaults. Final suite:
**60/60 passed**, including the real Metal bridge.

Run 82 started with empty isolated data. The installer verified and copied all
802 files (3.84 GB) from the user's ISO, then the game reached its title screen.
Six user-supplied add-ons were automatically imported and connected. Screenshot
confirms the title screen, not a race. SIGTERM exited normally. This candidate
was marked dirty and is a test package, not the published release.

### Run 83 — first screen and native DualSense discovery

Empty data, no scripted inputs: screenshot confirms two initial choices, ISO and
optional add-on folder. Native Apple backend detects DualSense (rumble supported)
after asynchronous discovery. Immediate inventory before discovery returned zero,
so it cannot establish device absence. Automated clicks did not advance this
run; manual ISO-picker interaction remains inconclusive. Run 82 did complete
installation through the test's controller input. Physical Bluetooth keyboard
reliability needs confirmation. Normal SIGTERM exit.

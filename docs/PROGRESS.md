# Progress and known limitations

Updated 2026-10-18. [Experimental v0.1.0 is published](https://github.com/secretpray/burnout-revenge/releases/tag/v0.1.0).

## Verified before standalone extraction

- Menus, profile creation/load, first races, engine/collision effects and EA Trax.
- Six user-owned add-on packages enumerated; every unlocked car not verified.
- GPU temporal history/Metal texture export and resize 720p → 1080p → 720p.
- SpeedBreaker v0.1.6 integrated into the dual-game development checkout.
- Final dual-game regression suite after input changes: Burnout 62/62 and
  Most Wanted 61/61. Standalone Burnout suite: 60/60, including the Metal bridge.
- Run 80: FSR pipelines compile and upscale 1080p to 4K, normal exit. Captured
  profile selection, so it is not an active-race performance benchmark.

## Known issues

This is an experimental port, not a completed compatibility layer. At the last
import audit, 218 imports had 161 handlers and 57 fatal unsupported stubs. Tested
paths do not invoke those stubs; other modes can. Online/multiplayer, full career,
all track/car combinations and every crash/replay path are not verified.

Magic Keyboard was reported to stop responding across macOS applications after
leaving the game with DualSense connected. The user later confirmed USB for
the controller, so Bluetooth contention is not an established explanation. Local bluetoothd logs show
actual disconnections for both devices; they do not establish causation. Apple's
native controller backend is now the Mac default, with raw HID opt-in/restart.
Focus loss releases all input grabs and clears keyboard state. Actual Bluetooth
reliability requires user hardware confirmation; automated keyboard events cannot
prove physical accessory health. Raw device logs are private and not published.

Live frame generation is off. GOD, other game revisions and Xbox save import are
not supported. First-time shader creation can hitch. No 120/144 FPS claim.

## Remaining work

- [x] Standalone build and unit tests (60/60); signature, dependency, minimum OS
  and DMG checks passed for the final icon package.
- [x] Fresh ISO install with optional add-ons and direct second startup.
- [x] Active race with FSR and live switching of upscale modes (run 85).
- [ ] Real Magic Keyboard focus-switch checks, including the reported USB
  DualSense configuration, without remote UI automation.
- [x] First release source/package/license audit and publication. See
  [release validation](RELEASE_0.1.0.md).
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
installation; its interaction method was not independently traced. Physical Bluetooth keyboard
reliability needs confirmation. Normal SIGTERM exit.

### Run 85 — packaged candidate, installed game and filters

140.1 s, exit 0, no missing imports. Verified installed-game discovery using
the fresh install from run 82 and copied test saves; no reinstall. Screenshots
show active crash/replay sequences in 1080p/Bicubic and 720p/MetalFX, then
active racing at 95 MPH/60 FPS with 720p/FSR 1. Lower-FPS crash/replay intervals
are retained; scenes differ, so this is a functional switch test, not a quality
or performance A/B. GPU timestamps were enabled. MoltenVK logged primitive-restart
warnings and transient pipeline-cache serialization errors during compilation;
no missing imports or process crash occurred. Cache persistence is rechecked
before release.

### Icon correction (2026-10-18)

The packager previously used the square cover for both ICNS and installer art.
Finder applied its own shape, while explicit NSApplication icon assignment
showed the raw square. The canonical transparent rounded icon is now separate
from the original installer cover. Darker, more contrasted artwork is
normalized to a 1024px RGBA canvas with a deterministic rounded alpha mask.
All ten ICNS representations passed alpha checks; direct-binary startup and
process-icon assignment passed in run 86. Actual Dock appearance still needs
manual confirmation.

### Run 86 — installed binary, rounded icon and warm cache

105.1 s, exit 0, no missing imports or measurement warnings. Actual executable
was Applications/Burnout Revenge.app/Contents/MacOS/BurnoutRevenge. Screenshot
confirms an active race at 111 MPH/60 FPS with FSR 1. The log confirms explicit
process-icon assignment and a 2035 KB pipeline cache loaded; this warm run had
no VK_INCOMPLETE or MoltenVK error records. Whole-run FPS includes crash/replays
and is not a 60-FPS stability benchmark. All ten ICNS representations have fully
transparent corners and opaque centers. The DMG's signature/dependency/macOS-floor
check passed. Direct Dock capture through UI automation timed out, so no claim
is made that a Dock screenshot was visually verified.

### Keyboard recurrence during UI automation (2026-10-18)

The user reported another system-wide Magic Keyboard failure while automated
UI control was being used to inspect Dock/Finder. Causality is unproven; UI
automation was stopped immediately. DualSense had been reported connected by
USB, so a two-Bluetooth-device explanation is insufficient. Native DualSense
backend is available, but it is not a confirmed fix. Further physical-keyboard
checks should be manual, without remote UI control, before blaming the port or
controller. Build commands and internal SDL test inputs do not seize OS input.

### Publication — 2026-10-18

Public repository: [secretpray/burnout-revenge](https://github.com/secretpray/burnout-revenge).
Experimental release: [v0.1.0](https://github.com/secretpray/burnout-revenge/releases/tag/v0.1.0).
Tag and binary source revision are `23c8e77`; later main-branch commits only record
validation/documentation. The release includes a DMG, exact LGPL third-party
sources and SHA-256 files. Source audit: 421 tracked files, no game data, generated
PPC, private diagnostic logs or broken local Markdown links.

Publication verification: re-downloaded the DMG, LGPL source archive and
`SHA256SUMS` from GitHub; both files passed `shasum -a 256 -c SHA256SUMS`.
The release is public and not a draft. It was initially marked pre-release;
on 2026-10-18 that GitHub flag was removed and v0.1.0 set as Latest so the
repository homepage can display it. The port remains experimental, with the
same documented limitations; the tag, application and checksums are unchanged.

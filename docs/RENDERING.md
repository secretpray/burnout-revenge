# Rendering and frame generation

FSR 1 and MetalFX Spatial enlarge an existing frame. They do not reconstruct
motion or generate new frames. FSR 1 is the unchanged AMD EASU + RCAS implementation
integrated by SpeedBreaker 0.1.5, now available in this port. Bicubic remains the
default. MetalFX falls back to Bicubic when unavailable; its sharpening is not
followed by a second bicubic sharpening pass.

Earlier fixed-scene testing on M1 Pro at 4K output: internal 1080p/Bicubic showed
cleaner fine detail than 720p/MetalFX; both held 60 FPS. Measured game GPU times
were 9.10 versus 6.60 ms, presenter times 2.81 versus 5.36 ms. Presenter timing
includes interqueue waits and is not additive pure GPU execution time. Active
scene/camera differences must not be treated as controlled image comparisons.

## Experimental temporal work

Seventeen verified Burnout pixel shaders supply exact current and previous
object projection coordinates. Private MRT output captures current-to-previous
motion without the game's blur-strength or extrapolation. A three-slot GPU history
holds scene, composited HUD, depth and pixel motion; gaps, resize and camera cuts
reset history. Metal private texture export was validated. Copies and motion units
were checked against diagnostic captures; offline MetalFX interpolation reduces
car ghosting versus camera-only reprojection in the checked pair.

This producer is disabled during ordinary play (`BR_FG_HISTORY=1` is a developer
switch). It has no live Metal interpolator consumer. At 720p it uses about 88.6 MiB;
at 1080p, 193.7 MiB. Camera-only sky motion, receding traffic, disocclusion, dynamic
HUD and a wider variety of tracks/cameras still need validation.

Next: consumer leases, shared-event ordering, retirement after both producer and
consumer complete, and a presentation schedule that alternates generated and real
frames without delaying input excessively. Intended benefit: smoother presentation
on a 120 Hz display from a 60 Hz simulation. Physics and input do not become 120 Hz.
On a 60 Hz display already receiving 60 FPS, little benefit is expected.

MetalFX Frame Interpolation needs a compatible SDK/OS/device (the developer Mac
reported support on macOS 27). No claim is made that it works on the minimum macOS
15 deployment target. DLSS needs NVIDIA hardware; it is not an M1 implementation.
FSR 3 frame-generation algorithms are research references, not part of this release.

## Where interpolation would help (measured 2026-10-18)

On the measured Traffic Attack route (runs 126–130, M1 Pro, a 60 Hz display) the
game holds 60 FPS everywhere except loading (43–46 s) and the crash camera: 92–97 s
runs at exactly 30 FPS (every frame 33.5 ms, GPU busy 13.5 ms: the game's own 30
FPS presentation, not a shortage of GPU time). Crash mode and replays use the same
camera. On a 60 Hz display 60 FPS play can't gain from generated frames; the crash
camera's ~5 s at 30 FPS per crash is the only place a 30 → 60 interpolation would
show. 60 → 120 needs a 120 Hz display.

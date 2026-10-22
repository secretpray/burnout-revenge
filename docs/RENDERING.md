# Rendering

The runtime translates the game's Xenos command stream and shaders to Vulkan.
On macOS, MoltenVK submits that work through Metal. The presenter scales and
filters the resulting game image, then draws the port's interface over it.

FSR 1 and MetalFX Spatial enlarge an existing frame. FSR 1 uses AMD's EASU
and RCAS implementation; MetalFX Spatial falls back to Bicubic when unavailable.
MetalFX output is not followed by a second Bicubic sharpening pass.

The game's race simulation normally runs at 60 fps. Crash cameras and some
other sequences use the game's own 30 fps presentation. Spatial scaling does
not change simulation speed or remove those limits. Live frame generation is
not enabled in the released application.

## Developer diagnostics

`BR_FG_HISTORY=1` enables an experimental history producer. It is disabled in
ordinary play and has no live interpolator consumer. Verified shader paths
export current/previous projection coordinates; the history stores scene,
HUD, depth and pixel motion in three slots. Gaps, resize and camera cuts
invalidate it. Enabling the producer increases GPU memory use.

Keep timing comparisons on the same scene, camera, resolution and output size.
Presenter wall-clock intervals can include queue waits and are not additive
GPU execution times. Frame captures can stall presentation and should be
excluded from performance measurements.

See [IMAGE_QUALITY.md](IMAGE_QUALITY.md) for the player-facing controls and
[BUILD.md](BUILD.md) for the build requirements.

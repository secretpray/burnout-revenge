# Image quality: anti-aliasing measurements

Date: 2026-10-18. Apple M1 Pro, macOS 27.0.1. Stage 4 of [the roadmap](ROADMAP_RU.md).
Same route as [the shader baseline](SHADER_PREP_BASELINE.md) (profile save copy,
virtual pad to a Traffic Attack race), internal 1.5x (1080p), Bicubic scaling,
`BURNOUT_GPU_TIMING=1`; captures at 85/95 s. Frames differ run to run (traffic),
so edges are compared on the player's car, which sits at the same place on screen.

| Run | Anti-aliasing | Sharpen | AA GPU time (race) | Post-processing total |
| --- | --- | --- | --- | --- |
| 132 | off | 0 | 0.08–0.17 ms (copy only) | 1.8–2.0 ms |
| 131 | FXAA-style (the existing pass) | 0.3 | 0.93–1.13 ms | 2.2–2.5 ms |
| 133 | SMAA 1x High (new) | 0 | 1.23–1.53 ms | 2.5–2.8 ms |

Whole-frame GPU busy stays about 13–15 ms of 16.7 at 60 FPS.

Capture observations (crops of the car): without AA the spoiler edge and mirror
outline step visibly. FXAA smooths them but softens the plate text, the badge and
the body texture. SMAA smooths the same edges and keeps the text and texture close
to the unfiltered picture. (131 also had sharpening 0.3; 133 had none.)

SMAA is added as Settings > Graphics > Anti-Aliasing Type (FXAA / SMAA,
`BURNOUT_AA_METHOD`). The user compared both in play and preferred SMAA: it is the
default from v0.1.5. Under Hor+
and Vert+ FXAA runs instead (its fetches stay within the 16:9 band's spans).

Next: CAS versus the existing sharpening/RCAS on the same crops, then the bloom
taps at 1080p, each with GPU time.

## Sharpening: CAS versus what exists (runs 136–138)

Same route, SMAA on, 1.5x internal, 3840-wide window:

| Run | Scaling | Sharpen | Post-processing GPU (race) |
| --- | --- | --- | --- |
| 136 | Bicubic | 0 | 2.19–2.21 ms |
| 137 | Bicubic | 0.3 | 2.18–2.62 ms |
| 138 | FSR 1 (EASU + RCAS) | 0.3 | 4.73–5.39 ms |

Crops of the plate and badge (2x nearest): Bicubic 0.3 is slightly crisper than 0
with no ringing (its sharpening is clamped to each sample's 2x2 source texels);
FSR 1 is the crispest, also without halos, at ~3 ms more GPU (EASU enlarges to the
full window).

Decision: no separate CAS. RCAS is AMD's successor to CAS (the same contrast-
adaptive limiter) and is already in FSR 1; Bicubic's sharpening costs nothing
measurable and doesn't ring. A CAS option would duplicate both. Recommended:
SMAA + Bicubic + sharpening 0.3; FSR 1 when the window is much larger than the
internal resolution and the GPU has ~3 ms to spare.

## Bloom at higher internal resolutions

The renderer keeps the game's bloom and luminance chain (320x180 and below) at
1x by design, and feeds it from resolves averaged down to 1x
(gpu/renderer.cpp, Internal resolution). The shimmer reported in another port —
720p-sized blur taps stepping over a larger target — has no counterpart here.
No change; revisit only on a visible report, with consecutive-frame captures.

## Bloom off and Motion Blur off (runs 167–183)

Settings > Graphics > **Bloom** and **Motion Blur**, both on by default, both live.

The bloom chain (one race frame's draw log, run 172): a bright pass shrinks the scene
to 320x180, `ps_007687…` blurs it with 13 taps and mixes in last frame's bloom, and the
composite `ps_2072de…` adds that bloom with **one bilinear fetch** (`t2d_4`) over the
motion-blurred scene (`t2d_0`, 8 fetches along the velocity in `t2d_1`), then grades
it (`t3d_2`). The bloom image is a dim, blurred copy of the whole scene: much of the
picture's brightness. boma's Xenia patch (as Xerenge applies it) empties the kernel:
the same passes run (run 173), the bloom target is black, and the frame loses half
its brightness and its blue sky (run 168: sky 88,65,64 against 159,150,161; mean
luma 44–70 against 106–136).

Bloom off here keeps the game's bloom and puts it back where it came from
(`runtime/gpu/burnout_bloom.h`): the composite's bloom fetch is multiplied by the
scene's brightness at the pixel over its brightness around it (rings of 10, 24, 44
guest pixels), capped at 1.6. In even areas the ratio is 1 and the bloom is the
game's; a dark edge beside the sky gets none of the sky's glow, so the haze and
halos go and the sky stays.

| Run | Bloom | Sky (same box) | Mean luma (3 frames) |
| --- | --- | --- | --- |
| 174 | on | 159,150,161 | 107 / 131 / 120 |
| 175 | off (in place) | 159,150,160 | 108 / 137 / 117 |
| 168 | kernel emptied (Xerenge's way) | 88,65,64 | 44 / 70 / 48 |

Split-frame runs 177–180 (`BURNOUT_BLOOM_SPLIT=1`: the left half in place, the right
the game's, in one frame; `BURNOUT_BLOOM_CAP`, `BURNOUT_BLOOM_REACH` for tuning): no
seam in the sky; palms and wires against the sky lose the milky glow. A cap of 1
leaves a darker rim of sky round the leaves; 1.4 to 2 look alike; a reach of 1.6 x
changes nothing visible.

Motion Blur off: `sub_821436D0` (the blur's frame counter) reads as finished, as in
boma's patch. Run 176: the race at speed without the streaks; nothing else changes.

## Bloom and Motion Blur as 0–100% (runs mb-*, bl-*)

Since v0.1.10 both are sliders (steps of 10, default 100%; v0.1.9's `true`/`false`
read as 100%/0%). The composite draw carries both in `pc.flags`, so no recompile:

- **Bloom** (`gpu/burnout_bloom.h`): bits 18–24 hold the share kept in place, 100
  minus the setting; the bloom fetch is scaled by `mix(1, ratio, share)`. 0% is
  exactly v0.1.9's Bloom off.
- **Motion Blur** (`gpu/burnout_blur.h`): the composite's 8 scene taps (`t2d_0`) are
  moved towards the pixel the velocity (`t2d_1`) is read at, by the setting; bits
  25–31 hold the cut. At 0% the game's counter patch also applies, as v0.1.9's off.

Runs with the first race, RT held, 7 captures from 52 s. Road sharpness (mean
neighbour difference, lower-left road): 100% median 7.6, 50% 8.9, 0% 12.5. The 50% and
0% runs took the same path frame for frame, and 50% was blurrier than 0% on every one
of the 7 (6.8–15.1 against 9.9–15.9). Bloom 100/50/0 at the same moment (palms
against the sky under the arch): the haze steps down, the sky stays as bright. No
GLSL compile failures in any run.

## HDR/EDR: reconnaissance (run hdr-1, 18.10)

What the game has to give before its grade:

- Every colour target of a race frame is `k_8_8_8_8` UNORM (`RB_COLOR_INFO` format 0 on
  all 1478 draws of run 172's frame): the scene itself never holds values above 1.
- The composite (`ps_2072de…`) sums the 8 scene taps, scales them by an exposure
  constant, adds the bloom, mixes towards a tint, then looks the colour up in the
  grade LUT (`t3d_2`), which clamps it to [0, 1]. Only that sum can pass 1.
- Measured by replacing the LUT fetch with the incoming colour halved (an
  uncommitted experiment, `BURNOUT_PRELUT_VIEW=1`; display gamma, sharpening and AA
  off): in race frames the colour going into the LUT averages 0.3–0.6; 1–3% of the
  pixels pass 1 (sparks, lamps, sky edges under the bloom), up to 2 (the view's own
  limit). The LUT then expands the rest: the sky at ~0.8 comes out near white.

So an EDR picture would be the SDR grade as it is, with those few highlights
extended past white: an inverse tone map rather than recovered HDR. The way there:
an `R16G16B16A16_SFLOAT` swapchain in `VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT`
(MoltenVK lists it among the 60 formats it offers; the presenter picks sRGB
`B8G8R8A8_UNORM` today), the excess over 1 carried from the composite to the
presenter (the front image is 8-bit), and the screen's headroom from
`NSScreen.maximumExtendedDynamicRangeColorComponentValue`.

The development machine's display (Samsung Smart M70D, 4K, the main display)
reports a potential EDR headroom of 1.0: HDR is off or unavailable there, and
EDR output would change nothing on it. Not started; worth it only with an HDR
display to judge it on (a MacBook Pro's XDR screen, or HDR enabled for the monitor).


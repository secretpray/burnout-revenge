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
`BURNOUT_AA_METHOD`), FXAA still the default until it's checked in play. Under Hor+
and Vert+ FXAA runs instead (its fetches stay within the 16:9 band's spans).

Next: CAS versus the existing sharpening/RCAS on the same crops, then the bloom
taps at 1080p, each with GPU time.

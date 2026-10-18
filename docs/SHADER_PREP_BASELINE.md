# Shader/PSO preparation: baseline measurement

Date: 2026-10-18. Apple M1 Pro, macOS 27.0.1, v0.1.4 (ccf1e7e) from /Applications.
Stage 3 of [the roadmap](ROADMAP_RU.md) starts with this measurement: is first-use
shader or pipeline compilation a visible cost worth an optional preparation step?

## Method

Same scene each run: a copy of the test profile's save and settings in a fresh data
folder, the installed game via `BURNOUT_GAME_DIR`, virtual pad script
`8-30/3:START,10-70/3:A,75~120:RT` — title, profile load, World Tour, Sunshine Keys,
Traffic Attack, car select, a full race with crashes, then the replay. No captures
during measured runs (a capture stalls a frame). Cold: empty `BR_CACHE_DIR`. Warm:
the cold run's cache. Metrics from the session log: `[hitch]` frames (with their
shader/pipeline/texture breakdown), `[perf]` windows and the final `[gpu]` counters.

## Results (log observations)

| Run | Cache | Hitches (race ≥75 s) | Worst | Compiled during hitches | Pipelines in run |
| --- | --- | --- | --- | --- | --- |
| 126 | cold | 7 (2, both ≤33 ms) | 133 ms | 57 shaders + 61 pipelines ≈ 15 ms | 115 |
| 128 | cold | 6 (1, ≤33 ms) | 133 ms | 57 shaders + 60 pipelines ≈ 15 ms | 116 |
| 127 | warm | invalid | — | — | — |

Run 127 is discarded: the window was covered at 6.6 s (another window took focus),
the game throttles while occluded, and the Mac then slept; its numbers measure that,
not the cache. A clean warm run needs the window visible for ~2 minutes.

The longest hitch (133 ms, both cold runs, 47.9 s) is the car select / race load:
98 ms of it the game itself idle (loading), 103 textures in 9.7 ms, pipelines 3.4 ms.
Shader translation and pipeline creation together cost about 15 ms over the whole
2-minute run. The renderer uses about a hundred pipelines for this scene, not
thousands.

## Assessment

On this scene and this Mac, first-use compilation is not a visible problem; an
install-time "Prepare graphics" step would save at most ~15 ms spread over loading
screens. Before building it: one clean warm run for the record, and spot checks on
other tracks/modes (Crash, Road Rage, a night track) in case they compile far more.
If those agree, stage 3 is deprioritised and stage 4 (image quality) comes next.

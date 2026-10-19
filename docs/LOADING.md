# Loading: where the time goes

Measured 2026-10-19 on an M1 Pro, build `cae22a8`: is a faster loader (Fukami's
`fast_unpack`: the game's unpacker run natively) worth doing here? No: a track load
is paced by the game, not held up by our CPU, disk or GPU work.

## Method

Road Rage in Eternal City from the 100% save (copy in `data-matrix`), virtual pad
script as in the other route runs, window 1600x900, Internal Resolution 1x. Run
`ld1`: the session log's file opens, `[perf]` windows and `[hitch]` frames. Run
`ld2`: the same route with macOS `sample` on the process for 4 s from 28.8 s, the
loading screen (`build/load-validation`, private).

## Timeline (ld1)

| Time | What the game does |
| --- | --- |
| 28.7 s | Loading screen (`FE\LoadSc19.bin`), then the event's sound banks (17 `.xwb`, 0.7 s) |
| 29.4 s | `tracks\EU\L2_V1\Gamedata.bgd`, then the rivals' cars (`pveh\MEDI`, `HIGH`: `.bgv`, `.hwb`, `.gin`) |
| 30.6 s | `enviro.dat`, `static.dat` (34 MB) |
| 31.2 s | Traffic: 12 `.btv` files (2.4 MB each), one every 0.10 s |
| 32.3 s | `propinst.dat`, `streamed.dat` (11.7 MB) |
| 32.6 s | First race frame |

About 3.9 s from the loading screen to the race, with the loading screen animating
at 60 fps throughout.

## Where the time goes (ld2, `[perf]`)

- The [perf] window inside the load: 60.0 fps, worst frame 16.9 ms, command
  processor busy 0.3 ms a frame, GPU busy 2.0 ms a frame. The Mac is close to idle.
- Guest main thread: 468 of 486 samples in `sub_82382798` → `sub_8238CD28`, the
  game's D3D waiting for the GPU's ring-buffer read pointer (+10384 against the
  write pointer +10396): it waits for the frame, it does not load.
- Command processor: mostly waiting too (`WAIT_REG_MEM` polls 170 samples, yields
  130, the submit mutex ~170).
- The reader thread (g6): 42 samples in `pread`, 442 waiting for requests.
- The real work: short-lived threads g41–g46 in `sub_82375D60` / `sub_82376540`
  (≈250 samples across six threads, run side by side), the only hot game code.

So the game spreads its loading over vblanks (a file every few frames, the loading
screen drawn in between); its unpacking is a fraction of the window and runs in
parallel already. A native unpacker would shorten nothing visible. Running the
loading frames unpaced would change the game's own timing: not done.

## The one cost that is ours

The first race frame after the load took 116.8 ms (7 vblanks): command processor
busy 42.6 ms, 109 textures (21.8 ms, 36 MB), 108 images, 20 pipelines (3.1 ms),
and write-watch guard stalls 56.4 ms (3 stalls). It is one frame at the switch from
the loading screen to the race. The guard stalls are the part to look at if this
frame ever matters; the shader/pipeline part was measured before and is small
([SHADER_PREP_BASELINE](SHADER_PREP_BASELINE.md)).

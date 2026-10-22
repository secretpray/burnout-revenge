# Native world checkpoint experiment

There is no player-facing rewind setting, key or history buffer yet. Developer
switches expose a boundary probe and a repeated-restore trial. Both are off by
default and operate only with this port's USA executable.

Use a locally built binary with separate `BR_DATA_DIR` and `BR_CACHE_DIR`
directories and a copy of a test profile. Keep online disabled. The trial changes
the current game world and its recording; use the probe alone for observation.

## Boundary and native checkpoint probe

`BURNOUT_REWIND_PROBE=1` logs `[rewind-probe]` diagnostics. It observes the world
update `sub_8211FA28`, called by the loop in `sub_820A3AF0`, and the game's own
checkpoint calls. It never requests a capture or a restore.

The boundary series requires a solo World Tour road event, one local player,
phase 5, no special mode, and the observed live clock flags (`01 00 00 00`). It
excludes online, split screen, the turn-based Crash controllers, instructions,
results, native Replay and unknown clock flag sets. The session phase and event
pointer alone do not distinguish a live event from its Replay.

After five seconds of advancing simulation time it observes 100 updates, logging
changes in stack, TLS and caller. Frozen-clock calls do not count. Leaving the
eligible state, changing the event/controller or rolling the clock back resets
the series. These are observations, not restore tests.

The probe also reports allocation metadata, GPU/audio diagnostics and the native
stacks of the other registered runtime threads. Heap totals count physical
backing once; its C/E aliases are not duplicated. The runtime's 256 MiB virtual
and 64 MiB physical heaps are labelled explicitly. The totals exclude the loaded
image, kernel arena and host state and do not measure a checkpoint's size.
Threads keep running while they are sampled. This does not provide a global
barrier, and diagnostic sampling perturbs timing.

The native observers wrap `sub_82345648` (capture) and `sub_823456A8` (restore).
These original game functions use the world serializer `sub_823424A0` and the
stream helpers at `sub_820BF278`/`sub_820BF2B8`. The observers log successful native
captures (header, payload, size, clock) and native restore calls without changing
those operations. The clock flags' final byte can be 1 during a capture request;
this is separate from the playback byte.

## Repeated-restore trial

`BURNOUT_REWIND_TRIAL=N`, where N is 1 through 100, enables the separate,
experimental orchestration in solo World Tour Road Rage. Other event modes are
excluded from the trial. It does not require the probe switch. It clones the
first successful native checkpoint at or after ten seconds of simulation time.
The clone consists of the 76-byte native header and its serialized payload,
limited to 16 MiB, held in RAM. One guest staging allocation supplies the original
serializer with guest addresses when restoring it.

At the end of the native recorder update (`sub_82346C68`), the trial waits for
two more seconds of game time and invokes the native restore. It repeats from
the same immutable clone until the requested count is reached. It verifies the
owner thread, live event/controller, recorder buffers, return status and clock.
A changed event, buffer, thread or mode discards the clone. A failed contract
after loading stops this isolated experiment with a crash report.

Native restore deliberately sets a separate world playback flag, which inhibits
live event logic. The trial returns that flag to its captured live value. It
also returns the recorder's frame counters, stream cursors and high-water marks,
and native checkpoint list to their captured positions. This truncates the
recorded future before the game records a new branch. The original PPC caller's
registers are preserved across the injected restore.

Completion of the requested attempts is only a diagnostic result. Inspect world
motion, AI/traffic, damage, RNG, score, clocks and eventual event completion
before accepting a restore path. Crash, Aftertouch, loading/unloading, save writes,
audio behavior and further modes need their own validation. The trial supplies
no persistent state format and does not restore host threads or kernel objects.

## Bounded history experiment

`BURNOUT_REWIND_RING=10`, `20` or `30` enables a separate automatic experiment
in solo World Tour Road Rage. `sequence` exercises all three durations in one
event: 40 → 30, 51 → 31 and 62 → 32 seconds. Leave
`BURNOUT_REWIND_TRIAL` unset. Use an isolated profile and mute automated runs.
These switches are developer diagnostics; there is no player-facing rewind
control yet, and both experiments are off by default.

The ring invokes the native capture function at the end of the recorder update
once per simulation second, starting at five seconds. It owns an immutable
header/payload copy for each point, separately from the game's native 15-second
Replay checkpoints. History is limited to 32 seconds, 34 points and 80 MiB of
serialized data; a separate guest staging buffer is limited to 16 MiB plus its
76-byte header. Eviction happens before allocating the new host payload. A
large payload may shorten available history; a request without enough history
is refused. Selection takes the most recent checkpoint at or before the target;
it does not simulate forward to a fractional second.

Restore checks the selected point's world clock, both RNG words and Road Rage
deadline directly against the captured values. It preserves the PPC caller's
registers, returns to live mode, truncates future native input/checkpoints, and
restores the native recorder's last-capture clock. Points after the selected
checkpoint are removed, and subsequent captures belong to the new branch.
Capture and restore are restricted to the observed main guest thread (ID 1).
Event changes, a backwards clock, Replay and recorder buffer changes invalidate
history. Completion or invalidation frees the staging allocation.
This is still an isolated experiment: other events, controlled Aftertouch,
save/lifecycle interactions and audio need further game validation before UI
or release integration.

## Tests

`rewind_probe_test` executes the real observer hooks on inaccessible or read-only
guest pages under AddressSanitizer. It checks disabled behavior, excluded modes,
clock/series resets, native header observation, original arguments/results and
exception behavior, and continuation after a diagnostic failure.

`rewind_trial_test` executes the real orchestration against a synthetic native
serializer. It checks immutable cloning after the original buffer is overwritten,
PPC register preservation, return to live mode, future recording truncation,
disabled/online behavior, and refusal after a buffer or event change. It does not
validate the game's serializer; actual event runs are required for that.

`rewind_ring_test` executes the real ring with a synthetic serializer under
AddressSanitizer. It checks 10/20/30 selection after eviction, recording metadata,
PPC register preservation, new branches, insufficient history under the memory
cap, disabled/invalid/conflicting switches, online/Replay/event exclusions,
buffer changes, capture failure, a foreign thread, and a reused event with a
reset clock.

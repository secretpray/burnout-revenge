# Native world checkpoint experiment

v0.1.22 includes experimental Rewind for solo World Tour Race, Road Rage,
Traffic Attack, Burning Lap, Eliminator and Crash, disabled by default.
Restores prepare the saved streamed track section before loading actors.
The implementation uses the game's USA executable and native world serializer.

## Working controls

Enable Settings > Rewind to collect history in the supported solo World Tour
events listed above. F2, F3 and F4 request 10, 20 and 30 seconds respectively. Each duration has
two editable keyboard bindings and its own editable controller combination:

| Duration | PlayStation | Xbox |
| --- | --- | --- |
| 10 seconds | L1 + R1 | LB + RB |
| 20 seconds | L3 + R3 | LS + RS |
| 30 seconds | L1 + R3 | LB + RS |

Hold a combination for 0.3 seconds. Release all configured shortcut buttons
before another request. A press matching multiple durations is ambiguous and
triggers nothing. Individual buttons retain their normal actions; an initial
button can reach the game before its partner is pressed. Stick clicks may also
nudge steering. Face-button combinations such as Triangle + Square remain
available as custom choices; Circle/B can invoke Crashbreaker if pressed first.
Choose combinations that suit your play style. Analog triggers L2/R2 (LT/RT)
are excluded. Back + Start remains reserved for Settings.

Select a controller row, release all buttons, then hold and release two or three
buttons to record its combination. Esc cancels. Duplicate or subset assignments are
rejected because they make the longer combination ambiguous. Clear Controller Shortcuts disables all editable controller bindings.
Menu Rewind controls only Rewind Now; shortcut durations remain independent.
After closing Settings, resume the native pause menu to apply a queued request.

History clears on saving, loading, event changes and changes to Codes. Online,
Multiplayer, clips and unvalidated event types do not collect player history.
A request without enough history is refused. A brief notice appears immediately
during a request and then shows the actual time restored, or explains why the
request was refused. It takes priority over queued notices without discarding
them and does not take control away from the game.

For developer experiments, use a locally built binary with separate `BR_DATA_DIR` and `BR_CACHE_DIR`
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
These switches are developer diagnostics. Normal use requires only Settings >
Rewind; automatic experiments remain off by default.

The ring invokes the native capture function at the end of the recorder update
once per simulation second, starting at five seconds in road events or one
second in Crash. It owns an immutable
header/payload copy for each point, separately from the game's native 15-second
Replay checkpoints. History is limited to 32 seconds, 66 points and 80 MiB of
serialized data; a separate guest staging buffer is limited to 16 MiB plus its
76-byte header. Expired host storage is reused; staging, a spare payload and a temporary
rollback checkpoint consume additional memory beyond the history cap. A
large payload may shorten available history; a request without enough history
is refused. In road events, selection takes the most recent driving checkpoint
at or before the target, within the 32-second history limit. Native wreck/Aftertouch states and
pre-start/finished participant states are not selectable: the Replay loader
rebuilds an ordinary chase camera rather than the transient cinematic controller.
A request whose target falls inside a crash uses the preceding driving point;
the notice reports the actual elapsed time. If none is available, it refuses the
request. The current physical vehicle is resolved from the local participant ID,
which can change on respawn. In road events a request made during an active wreck waits for
the native camera to finish and for half a second of driving before loading.
The target time remains tied to the original request; simulation continues during
the wait and the existing progress notice stays visible. A 30-second wall-clock
wait limit, expired history, saves, settings changes or leaving the event cancel
the request without loading old actors. Selection does not simulate forward to
a fractional second. Crash events allow pre-impact and active-crash checkpoints
and do not wait for a return to ordinary driving.

Restore checks the selected point's world clock, both RNG words and Road Rage
deadline directly against the captured values. It also compares the exact fields
visited by the native base/race event serializers and the Traffic Attack,
Burning Lap, Eliminator and Crash tails against the selected checkpoint. These are
only fields actually copied by the native stream helper: calls with a zero
field mask count bytes but do not save the pointed-to objects. Those objects,
padding and unsaved presentation caches are excluded. A failed comparison rejects the
restore and recovers the pre-request backup. Traffic Attack also checks the
player's award total in the separate native score block. Eliminator additionally
checks every race participant's identity, physical wrapper, participation flag,
elimination order and mode. A mismatch also recovers the backup. This verifies
those fields, not all actor or external state. It preserves the PPC caller's
registers, returns to live mode, truncates future native input/checkpoints, and
restores the native recorder's last-capture clock. Points after the selected
checkpoint are removed, and subsequent captures belong to the new branch.
Capture and restore are restricted to the observed main guest thread (ID 1).
Event changes, a backwards clock, Replay and recorder buffer changes invalidate
history. Completion or invalidation frees the staging allocation.
This is a native world checkpoint, not a complete emulator savestate. Wider
save/lifecycle interactions, audio continuity and every career event have not
been exhaustively validated.

## Track preparation and capture cost

The native checkpoint's header at offset 72 identifies the streamed track
section. Restoring actors without preparing that section can restore the clock
and RNG correctly while leaving collision unloaded. The ring follows the native
Replay streaming transition across main-loop frames, suspends simulation with
the recorder seek flag, and only loads actors once the streamer reports ready.
A timeout prepares and restores the pre-request backup; failed recovery stops
the run instead of continuing a partially restored world. The event owner,
save epoch and Codes settings are rechecked between preparation frames. Leaving
the event cancels the request without loading actors. A save or settings change
recovers the current world and starts fresh history.

Checkpoint staging uses a scoped host implementation of native memcpy.
`BURNOUT_REWIND_FAST_COPY=0` selects the original implementation for comparison. It preserves the original PPC stack side effect and
falls back for unrelated, overlapping, wrapped or out-of-bounds copies. It is
used only within checkpoint staging. `BURNOUT_REWIND_VERIFY_COPY=1` compares complete native
and accelerated checkpoints at the same capture boundary and discards history
on a mismatch. Verification adds overhead and must be disabled for timing runs.
`BURNOUT_REWIND_FRAME_LOG=<path>` writes optional frame timing CSV diagnostics.
`BURNOUT_REWIND_AUDIT=1` logs native camera poses and per-participant Eliminator
state around capture/load and during subsequent simulation. It adds diagnostic
overhead and is not suitable for capture-cost measurements.

A sequential Race comparison on the development M1 Pro used the same script,
a 960×540 window, no copy verification, and a 40-second measurement window.
At one capture per second, the original path took median 8.18 ms, p95 8.41 ms;
the scoped copy path took median 5.10 ms, p95 5.42 ms. Maximums were 8.66 and
8.95 ms respectively, with no recorded guard faults or stalls during captures.
The earlier isolated 31.12 ms capture did not recur; its original cause remains
unconfirmed. These runs establish lower typical capture cost, not higher overall
frame rate or elimination of every hitch.

The one-second interval preserves 30-second requests within the history budget.
A half-second interval filled the 80 MiB budget at about 17 seconds of history;
a two-second interval reduced memory use at the cost of restore precision.


## Game validation

Bounded scripted runs on an M1 Pro used isolated saves and the game's native
screenshot capture. They do not establish physical controller behavior or audio
quality. Repeated restores, track collision and native finish/Replay were
checked in Race and Road Rage. A Race run used 10/20/30-second requests and
finished in 4:07.15. A request made during Aftertouch waited for the native
camera, restored from clock 47.02 to 30.00 and resumed on the road at 151 mph;
111 sampled frames contained no residual white transition.

Scripted runs also covered Traffic Attack (30/20/10, score 900 to 0 and
finish/Replay), Burning Lap (10/20/30, finish/Replay in 4:00.56), and Eliminator
(revival before an elimination and retention after it, all six participants
compared after loading). A Crash experiment restored 30/20/10 seconds to pre-
impact checkpoints, continued the crash and reached the native Results screen.
The Crash contract has since been expanded to 1197 event bytes, including its
base and nested serializers, plus 112 bytes of separately stored globals. Post-
impact targets at clocks 18 and 24 continued to Crash Total. A physical
DualSense run applied three 10-second restores, including late targets at 39 and
46; the player repeated Crashbreaker and reached Gold Results with $16,140,650
damage. No restore-contract mismatch was recorded. The same physical session
then applied 20 seconds (44.87 to 24) and twice 30 seconds (43.73 to 13 and
53.76 to 23), reaching native Silver Results with $9,339,000 damage. A further
physical Eliminator run restored 77.57 to 57 seconds: the second eliminated AI
returned, active participants increased from four to five, and all six
participant records matched the target. The run ended in native Replay
(1:04.05). Two-survivor coverage is synthetic: a saved three-survivor roster is
restored after the fourth rival is eliminated, and a deliberate mismatch in that
rival triggers recovery to the pre-request backup. These observations do not
prove every event state, complete savestate behavior or deterministic replay.

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
reset clock. It also checks exact saved event fields, Traffic Attack score,
recovery after a rejected load, and save/options/event changes between track
preparation frames. Unsaved event fields may differ without rejecting a load.

The PageHeap reset regression test exercises real Release/Decommit calls with
modeled 4/16/64 KiB host pages, supported real mmap sizes, partial ranges and
remap failure. Shared physical memory is checked through all three mirrors,
including stores through those mirrors after a reset.

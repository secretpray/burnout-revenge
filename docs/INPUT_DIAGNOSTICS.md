# Isolated keyboard and controller test

This developer diagnostic uses the same local SDL library
as the port, but no game, guest CPU, Vulkan, save, ISO or UI automation. Only local
window key-down **counts** are logged, never key values/text, serials or paths.
The window title shows focus/count/controller state. Esc or window close ends it;
all runs also have a 1..600-second bound. No global input grab or event injection.

Build from the repository root after building dependencies:

```sh
sh scripts/macos/build_input_probe.sh
build/input-probe --controllers none --seconds 120
```

Set `BR_SDL_PREFIX=/path/to/SDL-prefix` for a different header/library prefix
(`include/SDL3`, `lib/libSDL3.0.dylib`). This also permits a controlled SDL version
comparison without replacing the application's framework. Record that prefix's
version/hash and rebuild; no downloading or replacement happens automatically.

Run **one** experiment at a time, with the game closed. Keep DualSense paired to
the OS over Bluetooth in the first case; `none` never initializes SDL's gamepad
subsystem. Manually repeat Cmd+Tab out/type/back and Cmd+H/restore, then report
Magic Keyboard health separately from the controller. In controller modes only
an explicit A/Cross press requests a 200 ms vibration pulse.

| Order | Command flags | What it isolates |
| --- | --- | --- |
| A | `--controllers none` | SDL/Cocoa window and keyboard, no controller subsystem or audio |
| B | `--controllers native` | Add native gamepad lifecycle; initially do not press A/Cross |
| C | `--controllers native --audio` | Add main-thread CoreAudio with a silent stream |
| D | Same as C, press A/Cross | Add explicit short haptics requests |
| E, only if useful | `--controllers raw --audio` | Compare raw PS5 driver (MFi/IOKit disabled) |

When a stage fails, stop that comparison and record the last successful stage;
do not label a failed system-wide typing check a pass because the pad stays open.
The probe drains stop output briefly before close. That does not promise delivery
under SIGKILL or a failed Bluetooth link. Record the experiment and its result
after each run.

## Fullscreen / Game Mode variable

Fullscreen and Game Mode can change the environment under test. Compare windowed
and fullscreen behavior separately; a small windowed probe does not reproduce
every fullscreen transition. See [Apple: Use Game Mode](https://support.apple.com/en-gb/105118).

Keep reports private until identifiers in OS logs have been removed. Our probe
itself collects no Bluetooth address, device path, actual keys or OS-wide text.

## Native Crashbreaker charge trace

`BURNOUT_CRASHBREAKER_LOG=/absolute/path/charge.csv` enables a separate,
read-only in-game probe. Run it with isolated `BR_DATA_DIR` and `BR_CACHE_DIR`,
`BURNOUT_MUTE=1`, and a bounded test. It observes the verified USA solo World
Tour Crash event; it does not enable an accessibility option or synthesize input.
With the variable unset, its hooks do not read guest memory.

The CSV records native charge entry/exit and the Crash controls producer:
world time, clock/playback flags, charge window, maximum/current/cumulative
boost, accepted press count, previous button level, and the native input flags
(`-1` when not sampled). Bit 0 of a controls row is the native Crashbreaker
action after the game's input mapping. The minimum-press field is the native
threshold, not a universal recommended tapping rate. Charge parameters vary
with the selected vehicle.

The trace stops at 30,000 rows and flushes every 120 rows. A forced exit may
leave a buffered or incomplete tail; exclude incomplete records. Compare
simulation time and actual accepted edges, not nominal script tap intervals.
This trace and captures perturb timing and do not measure input latency or
qualify physical keyboards, controllers, transports, or remapping behavior.

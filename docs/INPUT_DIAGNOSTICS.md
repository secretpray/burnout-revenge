# Isolated keyboard and controller test

Developer diagnostic, 2026-10-18. This executable uses the same local SDL library
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
under SIGKILL or a failed Bluetooth link. Build has been checked; physical runs
are pending. These probe runs have no automatic game-progress update: record the
experiment and result in PROGRESS after each run.

## Fullscreen / Game Mode variable

The failing game's test profile currently uses fullscreen. Actual Game Mode
status was not observed. Apple documents that eligible fullscreen games can
activate Game Mode and that it changes Bluetooth sampling:
[Apple: Use Game Mode](https://support.apple.com/en-gb/105118).
A system-level typing failure after a fullscreen Space transition warrants a
windowed versus fullscreen comparison, then a manual Game Mode on/off comparison
if needed. This is an untested hypothesis, not evidence that Game Mode is at fault.
The small windowed probe cannot rule out a fullscreen-only failure.

Keep reports private until identifiers in OS logs have been removed. Our probe
itself collects no Bluetooth address, device path, actual keys or OS-wide text.

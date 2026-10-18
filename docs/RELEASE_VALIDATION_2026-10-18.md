# Public v0.1.0 clean-install validation

Date: 2026-10-18, Europe/Kyiv. Hardware: Apple M1 Pro, macOS 27.0.1.
This checks the published artifact, not a locally rebuilt development binary.

## Provenance and install

Downloaded the DMG again from the public GitHub release. SHA-256:
`ded1ecdc623d69f3b03ae8553480d399f490b3c8ddc24a852b7d634fdbba1a06`.
Copied its app to Applications unchanged. Version 0.1.0, commit `23c8e77`;
executable SHA-256 `d9f0ebcebb9f8f09220650e9a979f2af6e9934b23cbe87f687f920ceaaaaba93`.

Original user data/cache were moved aside before first launch, leaving the actual
standard macOS folders empty. No development game override, copied save or old
shader cache was present in runs 87/88. The user manually operated the real ISO
and add-on pickers. All 802 installed files subsequently passed the released
binary's `--verify`; six add-ons connected. Profile creation, active Traffic Attack
and profile re-load are covered below.

Signature, bundled dependencies and macOS 15 import checks passed. Gatekeeper
assessment rejects this non-notarized ad-hoc app; new users may need Open Anyway.
CLI downloads do not set browser quarantine, so the browser-origin security dialog
itself was not verified. This is not a guarantee of all modes or all Macs.

## Runtime observations

| Run | Controller / sound | Evidence and result |
| --- | --- | --- |
| 87 | Bluetooth, native Apple, sound and vibration on | Manual first install; intro/title/Settings captured. Exit 0 at 123.3 s. Four controller removals. No save recorded in this run. |
| 88 | Bluetooth, native Apple, sound on, vibration off | Manual new-profile keyboard accepted; 21,316-byte save plus container/thumbnail. Capture: Traffic Attack at 77 MPH. Exit 0 at 253.4 s; one early controller removal. User reports system-wide Magic Keyboard failure requiring power cycle. |
| 89 | Bluetooth, raw SDL basic reports, sound on, vibration off | Native MFi/IOKit providers disabled for this process. Profile read again. Exit 0 at 183.0 s; early removal at 11.5 s and reconnection at 41.8 s. User reports controller-or-keyboard failure. Basic raw mode is not a confirmed fix. |
| 90 | Verified USB, native Apple, normal sound/vibration | 240.1 s, exit 0, zero removals. Traffic Attack at 160 MPH with collisions. User confirms controller and Magic Keyboard stable during focus-switch/typing checks. |
| 91 | Separate audio-startup candidate, Bluetooth, native Apple, normal sound/vibration | 240.1 s, exit 0, zero removals. Active Traffic Attack at 116 MPH. User confirms stability and supplies real typed text after the game. |

No fatal import was invoked in completed runs. No remote UI automation or injected
OS input was used. Capture schedules contained only internal Shot requests. Some
captures stall rendering, so these runs are functional checks, not FPS benchmarks.

Bluetooth native idle monitor (using the shipped SDL library): 90 s, one connection,
zero removals, reported battery 95%, no rumble. The user confirmed Bluetooth was
the transport for the failing runs. USB was independently confirmed in IOUSB and
SDL (`wired`, Sony VID/PID 054c:0ce6) before run 90.

## Assessment and unresolved work

Clean install, file verification, profile creation and active gameplay passed.
Bluetooth controller/keyboard reliability failed; neither disabling vibration,
stopping UI automation nor switching to raw basic reports is a confirmed fix.
Local bluetoothd records match the controller disconnections in run 87. The run-88
keyboard log also records a disconnection, but its timestamp may reflect the
manual power cycle; it does not prove when the original typing failure began.
No root cause or hardware fix is claimed.

Original settings/save/cache folders were restored; every original settings/save
SHA-256 matched. Further runs use only the archived clean-test profile. The downloaded
release app remains installed. Raw logs, device addresses, saves and captures stay
private and are not part of the repository/release.

A macOS Bluetooth/HID interaction remains a hypothesis. Related first-person reports:
[macOS 27 keyboard disconnects](https://developer.apple.com/forums/thread/845857) and
[audio/HDMI discovery with BLE HID interruptions](https://developer.apple.com/forums/thread/848173).
These involve different hardware and do not establish causation for this port.
Do not treat older, fixed SDL2 disconnect crashes as a patch for this SDL3 setup.

Next checks: complete USB/manual typing comparison; if needed isolate host audio
initialization separately. No speculative runtime or driver change has been shipped.


A concrete runtime defect was identified during this audit: SDL audio-subsystem
initialization happened on a guest thread. The separate candidate moves it to the
main thread before starting guest code, matching the documented SDL contract.
Device-stream creation may remain on a worker thread. This does not yet establish
causation for the Bluetooth failures; tests and candidate comparison are pending.
[SDL initialization threading](https://wiki.libsdl.org/SDL3/SDL_InitSubSystem),
[stream threading](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream).


Candidate result: the corrected backend initialized on the main thread before
guest entry, and the Bluetooth run passed for 240.1 seconds with no removals or
fatal imports. User typing was explicitly confirmed. Full suites passed after the
change: standalone 60/60, dual-game Burnout 62/62 and Most Wanted 61/61. These
results justify a v0.1.1 hotfix; they do not prove every Bluetooth failure shares
this cause. The published v0.1.1 package still needs download/runtime verification.

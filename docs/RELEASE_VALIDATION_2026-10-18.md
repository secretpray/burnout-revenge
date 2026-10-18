# Release validation and v0.1.1 audio-startup hotfix

Date: 2026-10-18, Europe/Kyiv. Hardware: Apple M1 Pro, macOS 27.0.1.
The audit starts with the public v0.1.0 artifact and compares a candidate and the
downloaded v0.1.1 package. Each run below identifies its provenance.

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

## Assessment and correction

Clean install, file verification, profile creation and active gameplay passed.
Bluetooth controller/keyboard reliability failed in v0.1.0; neither disabling vibration,
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

USB comparison passed in run 90. A concrete SDL threading defect was then
corrected and tested separately, as described below.


A concrete runtime defect was identified during this audit: SDL audio-subsystem
initialization happened on a guest thread. The separate candidate moves it to the
main thread before starting guest code, matching the documented SDL contract.
Device-stream creation may remain on a worker thread. The short successful
comparisons below do not establish causation for every Bluetooth failure.
[SDL initialization threading](https://wiki.libsdl.org/SDL3/SDL_InitSubSystem),
[stream threading](https://wiki.libsdl.org/SDL3/SDL_OpenAudioDeviceStream).


Candidate result: the corrected backend initialized on the main thread before
guest entry, and the Bluetooth run passed for 240.1 seconds with no removals or
fatal imports. User typing was explicitly confirmed. Full suites passed after the
change: standalone 60/60, dual-game Burnout 62/62 and Most Wanted 61/61. These
results justified packaging a candidate, but run 93 later reproduced the keyboard
failure. The audio defect is corrected; input reliability remains unresolved. The downloaded v0.1.1 package was subsequently checked in runs 92/93.

## Downloaded v0.1.1 package

Source/tag: `cda797a` / `v0.1.1`. Assets were downloaded from the authenticated
GitHub draft before publication; publication retains those exact bytes.

- DMG SHA-256: `2f44edf493b409367f6aa474cedb976b79cb6a441922b17e287beccfe6d8ab5f`.
- LGPL source archive SHA-256: `561d0e7f323662946af2512d3cdb6b39a301ab6926951e270c95ca86896dab65`.
- Installed executable SHA-256: `4a4b8a6230d009cdcc30599704c5c7b55d02aba102538726b4d24271a8246bb7`.

Both downloaded archives match SHA256SUMS. Signature, bundled dependencies and
macOS 15 API-floor checks passed. Final standalone suite: 60/60 (28.65 s).
Shared-runtime suites: Burnout 62/62 and Most Wanted 61/61.

| Run | Scope | Result |
| --- | --- | --- |
| 92 | Downloaded app, empty isolated data/cache | First screen captured at 5/12 s, exactly ISO and optional add-on choices. Exit 0 at 16.1 s. This does not repeat ISO installation. |
| 93 | Same installed downloaded app, clean-test profile, empty shader cache, Bluetooth, normal sound/vibration | Exit 0 at 240.1 s, zero controller removals/fatal imports. Profile loaded; active Traffic Attack at 153 MPH captured. User reports Magic Keyboard failed system-wide after returning to Burnout, requiring power cycle. Input validation FAILED. |

The full manual ISO/add-on install and new profile were verified in v0.1.0;
installer/profile code did not change in the hotfix. Run 92 verifies the hotfix's
empty first screen; run 93 verifies profile reuse and active gameplay. Original
user data are restored and kept separate from all later test runs. Live frame
generation remains off. These checks do not cover full career or every Mac.

### Final user feedback — release gate failed

The user confirmed the failure happened after returning to **Burnout**, before
the separate Most Wanted smoke. Magic Keyboard required off/on again. The app's
zero gamepad-removal count is insufficient evidence of system-wide keyboard
health. v0.1.1 is kept as a draft; it is NOT a proven Bluetooth fix. No new runtime
claim or release publication follows from the earlier positive candidate reply.

Run 94 (shared Most Wanted development build): 35.0 s, exit 0, sound enabled,
main-thread audio initialization confirmed, zero fatal imports. Capture shows the
career main menu. Only the virtual test pad was enabled; physical joystick
providers were disabled. This validates startup/menu, not a race or real keyboard.

## Input research follow-up

A separate development tranche uses Takedawn's focus/lifecycle approach, not a
replacement runtime. It suppresses every new rumble request while unfocused or
suspended, treats hide/minimize as inactive, and excludes Command shortcuts from
the game's keyboard map. No global OS keyboard hook. See
[the adoption plan](EXTERNAL_PORTS_PLAN.md).

Final suites with this new tranche: standalone 61/61, shared Burnout 63/63,
Most Wanted 62/62. Run 95: virtual pad only, physical SDL providers disabled,
24.1 s, exit 0, two internal focus-loss/gain pairs, grab=0 on return. This cannot
validate real Bluetooth hardware. Run 96 begins the user's physical comparison;
its transport API reports unknown under the native driver, not verified wired.
The cda797a draft DMG and Applications app do not contain these new focus changes.

Run 96 final: 313.5 s, normal targeted SIGTERM, exit 0. No gamepad removals or
fatal imports. Real focus loss/return logged with grab=0. User explicitly confirms
Magic Keyboard stayed stable in game and after returning. This supersedes the
pending state above for this **separate focus candidate**, not the cda797a DMG.
The response did not separately reconfirm real controller/audio. Package validation
is still required; input root cause across all hardware is not claimed. All
caffeinate assertions from this stage ended; permanent lock settings unchanged.

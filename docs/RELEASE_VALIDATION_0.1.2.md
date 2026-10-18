# v0.1.2 validation: first-run screens and the Game Mode finding

Date: 2026-10-18, Europe/Kyiv. Apple M1 Pro, macOS 27.0.1. Tags v0.1.0/v0.1.1
unchanged; v0.1.2 exists only as a GitHub draft (no tag) until it passes.
Raw logs, Bluetooth extracts, device names/addresses and captures stay private.
Log observations, capture observations and the user's manual confirmations are
listed separately; none of them stands in for another.

## Runs

| Run | Build / launch | Data | Result |
| --- | --- | --- | --- |
| 97 | Downloaded 5facc4a DMG, /Applications | empty isolated | First screen captured (ISO + add-on choices). Exit 0 at 18 s (bounded). The user opened and cancelled the ISO dialog meanwhile; not announced beforehand — process error, see below. |
| 98 | Same | clean-test profile | Game started from the installed test profile; the user closed the window at 22 s, having taken it for a second copy. No hardware result. |
| 99 | Dev c3dc8b2-dirty, test hook picks the ISO | empty isolated | Log: Choose → Checking → Confirm → Installing → Done; EA Trax from the disc started on Confirm, faded on START. Captures: new step layout. |
| 100 | Downloaded c3dc8b2 | empty isolated | Real dialogs: ISO and six add-ons chosen, music on Confirm. The user closed the window before installing (87.7 s). The user reported a vertical scroll bar after choosing add-ons. |
| 101–102 | Dev, image + add-on hooks | empty isolated | Compact Confirm page fits; no scroll bar (capture). Presses in 101 that no key script sent were the user's accidental input (user confirmed); every press is now logged with its source. |
| 103 | Downloaded cb135aa, /Applications | empty isolated | See below. Log: full manual first run (mouse), install of 802 files verified in 6.4 s, game 546 s, exit 0 via Quit Game, 0 fatal imports. |

## Run 103: input failure reproduced, cause located

User report: Bluetooth; game, controller and sound worked. The DualSense dropped
right after the user switched the game to fullscreen from the settings menu.
After Quit Game, Magic Keyboard did not work until it was switched off and on.

The system log for the session (bluetoothd, gamepolicyd), in order:

| Time | Source | Event |
| --- | --- | --- |
| 10:35:41.8 | game | Settings: fullscreen Off → On |
| 10:35:42.558 | gamepolicyd | Full screen gaming session active; **Game mode enabled** |
| 10:35:42.759 | bluetoothd | Starts moving a third-party LE HID to a 15 ms interval; the controller answers `Connection Update Completed with error STATUS 759`; retried ~3 times a second (~170/min) |
| 10:35:48.563 | bluetoothd | Classic link dropped; the game logs `gamepad removed` (93.9 s) |
| 10:35–10:43 | bluetoothd | The retry loop runs for the whole Game Mode session |
| 10:43:20.865 | gamepolicyd | **Game mode disabled** (game exited) |
| 10:43:25.882 | bluetoothd | Classic link loss (`Rx: No-Sync`, STATUS 708); a second at 10:43:56 |
| after 10:44 | bluetoothd | No retries, no disconnects |

Earlier runs, same log query: failed run 93 (downloaded v0.1.1 app) shows the same
retry loop for its whole session followed by disconnects. Passed run 96 (the bare
development binary, not an app bundle, so never in Game Mode) shows none.
Fullscreen alone doesn't separate them: 90/91/96 were fullscreen too.

Assessment: the failures follow macOS Game Mode, which the app bundle gets from
its games category. Game Mode retunes Bluetooth HID intervals; one LE HID on this
Mac rejects the new parameters and bluetoothd retries for the whole session, and
the DualSense and Magic Keyboard links drop around its start and end. The focus
changes in 2cf1d87 are kept but were not the cause. This is one Mac and its set of
peripherals: evidence, not proof for every machine.

## Correction under test

`scripts/macos/make_app.py` sets `LSSupportsGameMode = false` (the category stays
racing games); `check_app.py` fails a package without it. Trade-off: no Game Mode
CPU/GPU priority or faster controller polling; the game already holds 60 FPS on
M1 Pro. Next: the same fullscreen + Quit Game sequence with the new package, the
system log checked for Game Mode and the retry loop, and the user's keyboard
confirmation after exit; then a relaunch.

## Run 104: Game Mode opt-out (478cf28)

Downloaded 478cf28 package (`LSSupportsGameMode` false), /Applications, the run-103
profile: also the relaunch check. Log: no installer, game started directly; DualSense
connected at 7.4 s and stayed; fullscreen set at 23.6 s; Quit Game at 217 s, exit 0;
0 fatal imports. No focus loss was logged, so Cmd+Tab/Cmd+H were not exercised.
System log for the session and two minutes after: no gamepolicyd Game Mode entry,
0 LE parameter-update errors, 0 Bluetooth disconnects (run 103: ~170/min, 3 drops).
User confirmation: the game worked and Magic Keyboard kept working after Quit Game,
with no power cycle (the sequence that failed in runs 93 and 103).

## Run 105 and publication

Run 105 (same package and profile): 153.8 s, Quit Game, exit 0, 0 fatal imports;
system log: no Game Mode, no LE retries, no disconnects. No focus loss was logged,
so Cmd+Tab/Cmd+H while fullscreen remain unchecked. The user reported everything
working. v0.1.2 published as Latest at 478cf28; the public download matched
SHA256SUMS and the built DMG byte for byte. v0.1.0 notes link to it; v0.1.1 stays a
draft; tags v0.1.0/v0.1.1 untouched.

Runs 106–112 (development builds, test profile): reference captures of the game's
own menus (the user's screenshots of DRIVER DETAILS [SETTINGS] and the main menu)
and of the restyled settings menu for v0.1.3. Visual checks only.

## Process notes

Run 97 opened a visible window without warning, and run 98 followed: the user saw
two copies. Every visible launch is now announced with its length beforehand.

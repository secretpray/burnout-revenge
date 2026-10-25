# Codes: offline cheats for the solo career

Open Settings (F1, Cmd+, or Back + Start) and go to **Mods**. There are five
modifiers, in the spirit of the Xbox 360 trainers by RETROBYTE (Aurora) and
TeamXPG. All are off by default and apply at once, without a restart. They
work only in solo World Tour driving events. Career progress is saved through
the game's normal path, so a result earned with a Code stays in your career. Our
code never edits your save file.

| Code | What it does |
| --- | --- |
| Infinite Boost | Keeps the player's boost bar full at level 4; turning it off restores the earlier level |
| Infinite Time | Stops the event countdown while preserving the game's time bonuses; turning it off resumes the countdown |
| Quick Money | Raises every Traffic Attack award to at least $30,000 |
| Quick Takedowns | Sets the event count to 99 after the first real Takedown; later Takedowns add normally |
| Easy Boost | Fills the current bar on a Near Miss and lowers the Near Miss and drift thresholds |

![Road Rage results with Quick Takedowns](screenshots/codes-takedowns-gold.jpg)

## Limits

- Online Play must be off **at launch**. If it is on, turn it off and restart:
  even a solo race in that process gets no Code.
- Local Multiplayer, turn-based Crash, Burnout Clips and AI cars are excluded.
  Boost use and the Crashbreaker are unchanged in Crash.
- The guard sits in the game hook itself, so the menu switch and the
  `BURNOUT_INFINITE_BOOST`-style environment variables cannot bypass it. An
  unknown session is rejected.
- With Infinite Time on, an event that ends on timeout will not end. Turn it
  off to finish. (The RETROBYTE trainer carries the same warning.)
- Quick Money and Quick Takedowns change the event result and medal stored in
  your career.
- Not provided: arbitrary memory writes, an external code manager and
  Unlock All.

## How they map to the RETROBYTE trainer

The trainer's addresses (`0x8236BF34` timer, `0x82324E00` money, `0x8276FC2C`
takedowns, `0x82CE34F0` boost) belong to a different build of the executable:
in the USA XEX this port runs they are other instructions. Nothing was copied
from the trainer's binary patches. Each function was found again by what it
does, and checked in the game.

| Trainer | USA XEX in this port | Implementation |
| --- | --- | --- |
| Inf Time: patch at `0x8236BF34` | the event's deadline, read by `sub_8212DE50` | the deadline moves forward by the game time that passed, for the current solo event only |
| Easy Money: `add r3,r3,r10` → `addi r3,r3,30000` | the award function `sub_82324D58` | the amount is raised to at least 30,000 (multiplier included), player 0 only |
| Max TakeDowns: write 99 every tick | per-event statistics, stride 0xC8: `+0x70` is the total shown in the results, `+0x9C` is the count the goal, medal and HUD read | after a real player Takedown (`sub_8212F168`; the mode handlers `sub_82134B80` and `sub_82136498`) both are raised to 99 |
| Inf Boost: write 500.0 every tick | the car's boost object: level `+88`, capacity `+92`, stock `+96` | the game's own infinite-boost branch and level 4 from the game's tables |

## Implementation and checks

USA XEX only. The original functions are always called; a Code changes values
before or after the call, and only for a solo session with one local player,
no networking and a human car for player 0. Temporary changes (the infinite-boost
flag, the Near Miss thresholds) are restored right after the original function,
including when the stack unwinds.

`codes_test` runs the real hooks through the PPC ABI on protected pages under
AddressSanitizer: online, local modes, every Crash controller, AI, unknown
sessions, invalid bar capacities, setting changes, the timer turned off,
restoring the flag and thresholds after an exception, and both Takedown
counters.

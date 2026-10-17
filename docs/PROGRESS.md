# Progress and known limitations

Updated 2026-10-18. Release candidate preparation is in progress.

## Verified before standalone extraction

- Menus, profile creation/load, first races, engine/collision effects and EA Trax.
- Six user-owned add-on packages enumerated; every unlocked car not verified.
- GPU temporal history/Metal texture export and resize 720p → 1080p → 720p.
- SpeedBreaker v0.1.6 integrated into the dual-game development checkout.
- Dual-game regression suite: Burnout 62/62, Most Wanted 61/61 before final
  keyboard compatibility changes. Final standalone checks are recorded below.
- Run 80: FSR pipelines compile and upscale 1080p to 4K, normal exit. Captured
  profile selection, so it is not an active-race performance benchmark.

## Known issues

This is an experimental port, not a completed compatibility layer. At the last
import audit, 218 imports had 161 handlers and 57 fatal unsupported stubs. Tested
paths do not invoke those stubs; other modes can. Online/multiplayer, full career,
all track/car combinations and every crash/replay path are not verified.

Magic Keyboard was reported to stop responding across macOS applications after
leaving the game with DualSense connected. The user later confirmed USB for
the controller, so Bluetooth contention is not an established explanation. Local bluetoothd logs show
actual disconnections for both devices; they do not establish causation. Apple's
native controller backend is now the Mac default, with raw HID opt-in/restart.
Focus loss releases all input grabs and clears keyboard state. Actual Bluetooth
reliability requires user hardware confirmation; automated keyboard events cannot
prove physical accessory health. Raw device logs are private and not published.

Live frame generation is off. GOD, other game revisions and Xbox save import are
not supported. First-time shader creation can hitch. No 120/144 FPS claim.

## Remaining work

- [x] Standalone build and unit tests (60/60); final icon package check pending.
- [x] Fresh ISO install with optional add-ons and direct second startup.
- [x] Active race with FSR and live switching of upscale modes (run 85).
- [ ] Real Magic Keyboard + Bluetooth DualSense focus-switch confirmation.
- [ ] First release source/package/license audit and publication.
- [ ] Full career and broader track/car coverage.
- [ ] Safe GPU-history consumer, live MetalFX FG and pacing on 120 Hz.
- [ ] GOD input support.

### Run 81 (dual-game integration, before standalone build)

105.1 s, exit 0, no missing imports. Screenshot confirms active race at 110 MPH
and 60 FPS with FSR 1, internal 1080p → 4K output. Crash/replay intervals also
occurred; GPU timestamps were off, so no GPU execution cost is inferred from
zero log fields. Scripted focus loss/gain releases input and returns grab=0.
Physical Bluetooth keyboard behavior remains unverified.

### Standalone checks and run 82

The first standalone suite exposed donor-specific fixtures: removed SteamOS
launcher and legacy folder-migration tests (those features were removed), updated
control ordering for the new DualSense option and quit text, and pinned an actual
Burnout switch dispatch with deliberately escaping labels/defaults. Final suite:
**60/60 passed**, including the real Metal bridge.

Run 82 started with empty isolated data. The installer verified and copied all
802 files (3.84 GB) from the user's ISO, then the game reached its title screen.
Six user-supplied add-ons were automatically imported and connected. Screenshot
confirms the title screen, not a race. SIGTERM exited normally. This candidate
was marked dirty and is a test package, not the published release.

### Run 83 — first screen and native DualSense discovery

Empty data, no scripted inputs: screenshot confirms two initial choices, ISO and
optional add-on folder. Native Apple backend detects DualSense (rumble supported)
after asynchronous discovery. Immediate inventory before discovery returned zero,
so it cannot establish device absence. Automated clicks did not advance this
run; manual ISO-picker interaction remains inconclusive. Run 82 did complete
installation; its interaction method was not independently traced. Physical Bluetooth keyboard
reliability needs confirmation. Normal SIGTERM exit.

### 2026-10-17 18:10:12 EEST — burnout, 85-release-race-filters

- Запуск остановлен по лимиту времени; длительность 140.1 с.
- Лог: [`build/burnout-runs/85-release-race-filters/session.log`](../build/burnout-runs/85-release-race-filters/session.log).
- Кадров: 9; папка `build/burnout-runs/85-release-race-filters/build/`.
- Звук: включён.
- Шейдеры: асинхронно; контроллер: виртуальный тестовый.
- Аварийные импорты: в логе не зарегистрированы.
- Стадия меню/гонки требует визуальной проверки; автоматический отчёт её не подтверждает.
- Периодические показатели после 75 с (27 выборок, средние значений логгера): `{"aa_gpu_ms": 1.556, "cp_ms": 6.215, "draws": 2079.556, "fps": 52.726, "game_gpu_ms": 7.048, "gpu_queue_ms": 12.285, "pool_full": 0.0, "post_gpu_ms": 5.383, "stall_ms": 2.236, "uploads": 1.911, "worst_ms": 68.507}`; пул снимков: `default`. Это не распределение времени всех кадров.
- Add-on: зарегистрировано 6; ответы на перечисление игрой: `[6]`. Доступность каждой машины требует проверки в гараже.

### Run 85 — packaged candidate, installed game and filters

140.1 s, exit 0, no missing imports. Verified installed-game discovery using
the fresh install from run 82 and copied test saves; no reinstall. Screenshots
show active crash/replay sequences in 1080p/Bicubic and 720p/MetalFX, then
active racing at 95 MPH/60 FPS with 720p/FSR 1. Lower-FPS crash/replay intervals
are retained; scenes differ, so this is a functional switch test, not a quality
or performance A/B. GPU timestamps were enabled. MoltenVK logged primitive-restart
warnings and transient pipeline-cache serialization errors during compilation;
no missing imports or process crash occurred. Cache persistence is rechecked
before release.

### Icon correction (2026-10-18)

The packager previously used the square cover for both ICNS and installer art.
Finder applied its own shape, while explicit NSApplication icon assignment
showed the raw square. The canonical transparent rounded icon is now separate
from the original installer cover. Darker, more contrasted artwork is
normalized to a 1024px RGBA canvas with a deterministic rounded alpha mask.
The ICNS resolutions and direct-binary startup are checked before publication.

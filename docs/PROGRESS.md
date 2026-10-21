# Progress and known limitations

Updated 2026-10-18. v0.1.0 is public; v0.1.1 remains a draft.
**Current work:** [the plan](PLAN_RU.md), [new-chat prompt](NEW_CHAT_PROMPT_RU.md).

## Verified before standalone extraction

- Menus, profile creation/load, first races, engine/collision effects and EA Trax.
- Six user-owned add-on packages enumerated; every unlocked car not verified.
- GPU temporal history/Metal texture export and resize 720p → 1080p → 720p.
- SpeedBreaker v0.1.6 integrated into the dual-game development checkout.
- Final dual-game regression suite after input changes: Burnout 62/62 and
  Most Wanted 61/61. Standalone Burnout suite: 60/60, including the Metal bridge.
- Run 80: FSR pipelines compile and upscale 1080p to 4K, normal exit. Captured
  profile selection, so it is not an active-race performance benchmark.

## Known issues

This is an experimental port, not a completed compatibility layer. At the last
import audit, 218 imports had 161 handlers and 57 fatal unsupported stubs. Tested
paths do not invoke those stubs; other modes can. Online/multiplayer, full career,
all track/car combinations and every crash/replay path are not verified.

Magic Keyboard can stop responding system-wide after returning to Burnout with
Bluetooth DualSense. Run 93 reproduced this even with the corrected main-thread
SDL audio initialization; zero gamepad removals in the app log did not detect the
keyboard failure. Neither native/raw drivers nor vibration-off proved a fix.
A four-minute USB comparison passed. USB is the locally tested workaround, not a
universal guarantee. Physical typing after **both** focus directions is required.
No UI automation is used. The v0.1.1 audio fix is valid independently, but its
release remains a draft while this input issue is investigated.

Live frame generation is off. GOD, other game revisions and Xbox save import are
not supported. First-time shader creation can hitch. No 120/144 FPS claim.

## Remaining work

- [x] Standalone build and unit tests (60/60); signature, dependency, minimum OS
  and DMG checks passed for the final icon package.
- [x] Fresh ISO install with optional add-ons and direct second startup.
- [x] Active race with FSR and live switching of upscale modes (run 85).
- [ ] Resolve the reproduced Magic Keyboard failure after returning to Burnout
  over Bluetooth (run 93); USB short comparison passed. No UI automation used.
- [x] First release source/package/license audit and publication. See
  [release validation](RELEASE_0.1.0.md).
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
All ten ICNS representations passed alpha checks; direct-binary startup and
process-icon assignment passed in run 86. Actual Dock appearance still needs
manual confirmation.

### Run 86 — installed binary, rounded icon and warm cache

105.1 s, exit 0, no missing imports or measurement warnings. Actual executable
was Applications/Burnout Revenge.app/Contents/MacOS/BurnoutRevenge. Screenshot
confirms an active race at 111 MPH/60 FPS with FSR 1. The log confirms explicit
process-icon assignment and a 2035 KB pipeline cache loaded; this warm run had
no VK_INCOMPLETE or MoltenVK error records. Whole-run FPS includes crash/replays
and is not a 60-FPS stability benchmark. All ten ICNS representations have fully
transparent corners and opaque centers. The DMG's signature/dependency/macOS-floor
check passed. Direct Dock capture through UI automation timed out, so no claim
is made that a Dock screenshot was visually verified.

### Keyboard recurrence during UI automation (2026-10-18)

The user reported another system-wide Magic Keyboard failure while automated
UI control was being used to inspect Dock/Finder. Causality is unproven; UI
automation was stopped immediately. DualSense had been reported connected by
USB, so a two-Bluetooth-device explanation is insufficient. Native DualSense
backend is available, but it is not a confirmed fix. Further physical-keyboard
checks should be manual, without remote UI control, before blaming the port or
controller. Build commands and internal SDL test inputs do not seize OS input.

### Publication — 2026-10-18

Public repository: [secretpray/burnout-revenge](https://github.com/secretpray/burnout-revenge).
Experimental release: [v0.1.0](https://github.com/secretpray/burnout-revenge/releases/tag/v0.1.0).
Tag and binary source revision are `23c8e77`; later main-branch commits only record
validation/documentation. The release includes a DMG, exact LGPL third-party
sources and SHA-256 files. Source audit: 421 tracked files, no game data, generated
PPC, private diagnostic logs or broken local Markdown links.

Publication verification: re-downloaded the DMG, LGPL source archive and
`SHA256SUMS` from GitHub; both files passed `shasum -a 256 -c SHA256SUMS`.
The release is public and not a draft. It was initially marked pre-release;
on 2026-10-18 that GitHub flag was removed and v0.1.0 set as Latest so the
repository homepage can display it. The port remains experimental, with the
same documented limitations; the tag, application and checksums are unchanged.

### Run 87 — public download and real clean first launch

The published v0.1.0 DMG was downloaded again and its SHA-256 matched. It was
installed as a fresh Applications copy; the previous app was already absent.
Original user data/cache were moved to private backups, leaving the actual
standard macOS paths empty. No BR_DATA_DIR, BR_CACHE_DIR, game override, copied
save or old shader cache is used. No executable/library was modified or rebuilt.

The initial screen was captured internally. The user offered to operate the
real ISO/add-on pickers manually. This run has no virtual pad, keyboard response
hook or scripted key presses: BURNOUT_UI_KEYS contains only Shot captures.
The game has now created a verified install marker, started from standard
storage and connected six add-ons. Run exited on window close after 123.3 seconds, exit 0, no unimplemented imports.
The user confirmed successful installation and separately reported profile/race/audio
working, followed by controller dropouts. Captures show first-run UI, intro and
title screen with Settings. No save file or guest keyboard request was recorded
in this run, so new-profile persistence and active racing are not independently
verified by its artifacts; repeat startup is pending. Original profile remains
safely backed up until the checks finish.

Signature/dependencies/macOS floor check PASS outside the sandbox. A sandboxed
availability control could not read SDK metadata; the full check was repeated
successfully with normal SDK access. Gatekeeper assessment rejects the ad-hoc,
non-notarized app as expected: first-time users may need Open Anyway. Downloading
through gh does not apply browser quarantine, so browser security dialogs are
not verified by this run. Runtime checks on this Mac cannot guarantee every
mode or other machines.

Controller regression during run 87: four SDL gamepad-removed events at
57.9, 71.1, 89.9 and 113.8 seconds, with subsequent re-discovery. Native Apple
backend was selected; no remote UI control, virtual pad or scripted input was
used. Treat controller reliability as a failed/unresolved check, not a passed
release gate. Transport and idle-backend diagnostics are being checked.

Run 87 follow-up: the user confirmed the current DualSense transport is Bluetooth
(the earlier USB report belonged to a previous session). Local bluetoothd records
unexpected L2CAP disconnects at the same times as the SDL removals. This confirms
transport-level events, not their cause. Private addresses/names/logs are not
published. Independent `--verify` using the installed release binary confirmed
all 802 installed files match in 4 seconds. Native-backend idle monitoring and
a vibration-disabled second run are planned to separate possible factors.

### Run 88 — repeat startup, vibration disabled

Same downloaded executable, same fresh standard data folders, no re-install and
no copied saves. Native Apple controller backend; BURNOUT_VIBRATION=0 is the only
settings override. No scripted buttons/keyboard reply, only internal captures.
The real profile keyboard has been requested in this run. One gamepad removal
has already recurred with vibration disabled: disabling vibration alone is not
a verified fix. Separately, a 90-second native SDL monitor linked to the shipped
SDL library had one connection and zero removals, battery 95%, with no rumble.
This short idle comparison does not establish the root cause.

Run 88 checkpoint: guest keyboard accepted, new Player Files save created
(21,316 bytes plus container/thumbnail). Internal capture at 70 seconds confirms
Traffic Attack active gameplay, 77 MPH, rendered road/car/HUD. This is a new
profile created manually during this clean test, not a copied development save.
One early gamepad removal is recorded; later observation is ongoing. No fatal
imports so far. Audio was confirmed by the user. Post-exit profile persistence
will be checked before restoring the original user folders.

Run 88 completed by window close at 253.4 seconds, exit 0, no fatal imports.
There was one early native-gamepad removal at 6.6 seconds, then no further
removals through the remaining 246 seconds. Capture confirms the new-profile
Traffic Attack race. The Player Files payload is 21,316 bytes, with container
and thumbnail written. This is encouraging with vibration off, not a causal
fix claim; the repeated startup with the default vibration setting follows.

Run 88 user follow-up: Magic Keyboard again stopped working system-wide and
required power cycling. DualSense disconnected during the title sequence; after
manual reconnection it worked. This occurred without remote UI automation and
with vibration disabled. Therefore neither stopping automation nor vibration-off
is a confirmed fix. The app's native backend did not prevent the failures.

The planned run 89 was NOT started: approval-service usage exhaustion rejected
its execution before launch. On resumption, original settings/save/cache folders
were restored and all original settings/save SHA-256 hashes matched. The new
clean-test data are archived privately. The downloaded release app remains in
Applications. No lost save/settings data, no background game left by the test.
Further controller probes will use the archived test profile, not user data.

### Run 89 — raw SDL basic-mode comparison

On resumption the same released binary starts with archived clean-test data/cache,
raw PS5 HID selected, Apple's MFi and IOKit joystick providers disabled, enhanced
Bluetooth reports disabled and vibration off. Driver logs confirm SDL raw HID and
rumble unavailable. These are process-local diagnostics, not shipped defaults or
a confirmed fix. No system keyboard/mouse events are injected. The user is asked
to load the newly created profile, drive and test physical typing outside the game.
Original user data/settings/cache have already been restored and remain untouched.

Run 89 ended on window close after 183.0 seconds, exit 0, no fatal imports.
The raw driver recorded an early removal at 11.5 seconds and reconnection at
41.8 seconds. The newly created profile was read successfully at 49.3 seconds.
The user reported another controller-or-keyboard failure during the requested
focus-switch check. Raw basic mode is therefore NOT a confirmed workaround.
USB-only comparison follows after verifying actual wired transport.

### Run 90 — public release, real USB control

USB was verified independently in IOUSB and SDL (wired, 054c:0ce6), not inferred
from a charging cable. Unchanged released binary, native Apple backend, normal
sound and vibration, archived clean-test profile. 240.1 seconds, bounded SIGTERM
exit 0; no fatal imports and zero gamepad-removal events. Captured Traffic Attack
at 160 MPH with traffic collisions. The user confirmed controller and physical
Magic Keyboard remained stable through Cmd+Tab/typing checks. This is a short
successful wired validation, not a Bluetooth fix or all-hardware guarantee.

A separate source defect was found: SDL_InitSubSystem(SDL_INIT_AUDIO) ran from
a guest thread, although SDL documents initialization as main-thread-only.
It is moved before guest-thread creation; stream opening remains on the guest
thread (documented thread-safe). Silent test mode still skips host audio, and
failed backend initialization still leaves guest audio callbacks progressing.
Both standalone and dual-game sources are updated; tests/candidate Bluetooth
validation follow. Published v0.1.0 and Applications remain unchanged for now.
Reference: https://wiki.libsdl.org/SDL3/SDL_InitSubSystem

### Run 91 — candidate with main-thread SDL audio initialization

This is a separately built candidate, NOT the published v0.1.0 app. Same archived
clean-test profile, Bluetooth DualSense, native driver, normal sound/vibration.
The log confirms `[audio] SDL audio backend initialized on main thread (coreaudio)`
before entry to guest code; the audio device subsequently opens normally. The
existing profile is found/read. Physical controller/keyboard focus checks pending.

After the shared source correction: standalone 60/60 (30.01 s), dual-game
Burnout 62/62 (30.28 s), Most Wanted 61/61 (30.27 s). Both runtimes built. No
availability/Metal tests skipped in these complete checks. No release artifact
has yet been replaced and no Bluetooth-fix claim is made.

Run 91 completed at 240.1 seconds with bounded SIGTERM, exit 0, no fatal imports
and zero gamepad-removal events. Final capture shows active Traffic Attack at
116 MPH. The user confirmed controller/keyboard stability and typed a free-text
confirmation after switching out of the game. This is one successful Bluetooth
candidate run after the correction, not a universal hardware/root-cause guarantee.
v0.1.1 packaging and download verification follow; v0.1.0 is retained unchanged.

### Run 92 — downloaded v0.1.1, empty first start

Downloaded the draft assets from GitHub (same files retained for final publication);
both DMG and LGPL archive passed the downloaded SHA256SUMS. Installed the downloaded
app in Applications, preserving the v0.1.0 app as a private backup. Source/tag
cda797a/v0.1.1, executable SHA-256 4a4b8a6230d009cdcc30599704c5c7b55d02aba102538726b4d24271a8246bb7.
Empty isolated data/cache, no game override: first-run screen captured at 5/12 s;
16.1 s, normal bounded stop, exit 0. This checks initial UI only, not ISO installation
or a race. Original user data/settings/save hashes remain unchanged. Runtime
validation of the downloaded package with the clean-test profile follows.

### Run 93 — installed downloaded v0.1.1, Bluetooth and cold shaders

The actual Applications app from the GitHub draft DMG (cda797a/v0.1.1) ran for
240.1 seconds, bounded stop, exit 0. Test profile from the manual clean install,
new empty shader cache, native Bluetooth DualSense, normal sound and vibration.
Main-thread CoreAudio initialization precedes guest entry. Profile reload works;
captures show active Traffic Attack (153 MPH at 180 s). Zero controller removals
and zero fatal imports. No OS input injection or virtual controller. Physical
post-game typing was confirmed for run 91, but the user then reported another
system-wide Magic Keyboard failure after returning to Burnout in run 93, requiring
a power cycle. This overrides any provisional success assessment: the keyboard
issue remains OPEN, and v0.1.1 stays a draft.
This is a functional cold-cache test, not a performance benchmark.

### Run 95 — focus regression, virtual controller only (started)

Development candidate after the external-input review. Physical HIDAPI/MFi/IOKit
providers are disabled; only the virtual pad is opened. Isolated clean-test data.
Internal SDL focus events scheduled at 6/10/14/18 seconds, screenshot at 21 s.
This does not switch macOS apps, inject OS keys or prove physical keyboard health.
Final result will be recorded after exit.

Run 95 completed: 24.1 s, bounded stop, exit 0, no fatal imports. Only the
virtual pad opened; two internal focus-loss/gain pairs logged, with grab=0 on
return. New rumble-focus regression and full suites passed: standalone 61/61
(27.69 s), shared Burnout 63/63 (30.02 s), Most Wanted 62/62 (41.52 s).
Initial sandbox runs could not map VM / use Metal; the thread-contention stress
also missed its sampling threshold under parallel build load. Full suites were
rerun sequentially with required VM/Metal access, unchanged tests, all passing.
This is not physical Bluetooth/typing validation.

### Run 96 — focus candidate, native Bluetooth (started)

Separate development binary with the new output/focus/shortcut handling, native
Apple DualSense, normal audio and vibration, archived clean-test profile/cache.
No input script, virtual pad, automated name response or OS UI automation. Only
internal screenshots; the user is asked to test real typing in both directions
of Cmd+Tab and hide/restore. Applications still contains downloaded cda797a;
this candidate is not published. Physical result pending.

Run 96 completed: 313.5 s, exit 0 after targeted normal SIGTERM following user
confirmation. Zero gamepad removals and fatal imports. Actual focus-loss/gain
logged, including return with grab=0. Native backend reports transport unknown;
Bluetooth was the requested physical setup, not inferred from that API. User
explicitly confirmed Magic Keyboard worked after returning and stayed stable in
game. This is a positive short physical keyboard check, not a universal cure;
controller/audio results were not separately reconfirmed in that reply. Caffeinate
was process-bound and ended with the run; no permanent lock settings changed.
Downloaded cda797a in Applications and draft release remain unchanged. Next: package
this candidate, repeat the downloaded-artifact check, then longer input testing.

### Handoff checkpoint

Run 96 is a positive short physical keyboard check after the focus changes;
Applications/draft cda797a still lack those changes. Package validation remains
open. Keep the current name-entry UI unless the actual Burnout APT screen is
proved more faithful and complete; Xerenge's ImGui dialog is not that screen.
Detailed open work and a compact new-chat prompt are now maintained separately.


## 2026-10-21: v0.1.17 — Ranked matchmaking

Published Latest at `44688ff3bfc663fe305cfb847c94edaff90b5e1a`; installed from the
release's downloaded DMG. SHA-256:
`71fcb3d473b94331f284fd50cb7c4dbda986bbe280c903b461f7ec964011d9c9`.
All seven downloaded assets matched the local bytes; check_app PASS on the DMG,
codesign and installed binary hashes checked. Standalone tests: 75/75 on the
v0.1.17 candidate without retries. Earlier investigation under four running games
hit the known swapchain_use_test scheduling threshold; its separate repeat passed.
The packaged clean-commit candidate ran for 45 s with no stubs or hang, showed the
v0.1.17 release note, closed it and reached the main menu (frames and log).

XSessionSearch (`0x000B0016`): `sub_82421818` builds 32 bytes: proc/user/max (BE32),
property/context counts (BE16), their pointers, result capacity and result pointer.
Observed proc 0, max 5, props 0, contexts 2, capacity 6638; contexts `0x800A` is
0 Ranked / 1 Player and `0x800B` is 0. An empty count/pointer header (8 bytes)
completes successfully, including overlapped calls; the game then sends `gqwk`
with empty `MACHINT`. The lobby supplies the host's SESS, so no fabricated Xbox
Live session list or TrueSkill service is needed. Host flags `0x43F`, join `0x43E`.
Reused output buffers, guards, invalid sizes and asynchronous completion are tested.

The lobby's `GS1` (1 Ranked / 0 Player) matches SYSFLAGS bit 18 (observed 262464 /
320). Quick Match now filters both local and LAN-announced games; a regression
test has both modes open, then a ranked game locked/full. No match returns `nfnd`,
which `sub_82368710` maps to -52; `sub_82218098` shows the original offer to create a
match. Unknown `gjnf` had mapped to 0 and shown Connection problem. Frames confirmed
the offer and Yes -> Game Options with Ranked: Yes. The author's server retains
its own selection/error behavior; our wire protocol is unchanged.

Ranked Race reached results with 2 and 4 copies on our server and on LAN, and
2 copies through the author's community server. Logs confirm every participant's
session start, stat writes and end; frames show the four-player LAN result table
(6/5/4/3 points, the last bot's normal race TIMEOUT), and return/restart was checked.
All were virtual pads on one Mac; no confirmation between two physical Macs.
The first four-copy scenario started with three before the last bot entered and
was not counted. The corrected harness starts each next copy after the previous
session, delays the host 45 s per bot, and uses BOTS_SHORT_RACE=1 (one lap, one round).
BOTS_RANKED=1 selects Ranked. Completed runs/screenshots/raw logs were removed;
compact results remain in build/online-validation/ranked-investigation-summary.json.

User decision (21.10): no global ranking, TrueSkill or saved leaderboard, preserve
community-server compatibility. Scoreboards shows NO CHARS before any stat read;
arbitration/stat writes are accepted without an Xbox Live ranking service. The
limitation is explicit in CHANGELOG/README/ONLINE and the in-game release note.
Automated runs now use BURNOUT_MUTE=1 (user request); saved audio preferences and
the user's real saves were not changed. Temporary test lobby stopped.


## 2026-10-21: v0.1.18 — совместимость онлайн-импортов

Пользователь отложил два настоящих Mac и поставил совместимость следующей задачей.
Все игровые запуски: `BURNOUT_MUTE=1`, свои тестовые каталоги, виртуальный пад,
встроенные Shot; настоящие сейвы и настройки звука не менялись.

### Что исправлено

- Список в плане был устаревшим: 218 импортов, 213 уже реализованы, 5 настоящих
  заглушек. Четыре социальных UI, friendship, две tile-функции и две session-функции
  были реализованы до этого этапа, но не имели прямого теста ABI.
- `XamReadTileToTexture`: восемь аргументов, не семь: type, titleId, 64-битный tileId,
  userIndex, pixels, stride, height, overlapped. Вызов sub_825AEBE0 передаёт height в r9,
  completion в r10. Старый hook считал height указателем XXOVERLAPPED и мог писать
  в низкую память. Сигнатура сверена с generated PPC и
  https://github.com/xenia-project/xenia/blob/master/src/xenia/kernel/xam/xam_user.cc
  (ABI, код не копировался). Картинок Xbox Live нет: ERROR_FILE_NOT_FOUND, для async
  completion выставлен также HRESULT 0x80070002; буфер пикселей не трогается.
- `XamUserAreUsersFriends`: нулевой BOOL синхронно либо Length=0 асинхронно, отдельные
  ответы для отсутствующего/неверного user index и отсутствующего получателя результата.
  Ошибка async — ERROR_FUNCTION_FAILED с HRESULT причины, а не выдуманная дружба.
- `XamSessionCreateHandle`: null output/нехватка user или physical heap не дают успешной
  сессии с нулевым guest object. При отказе второго allocation первый освобождается.
  В Heap::AllocPhysical добавлена проверка null перед записью служебного заголовка.
  RefObj очищает output при ошибке, проверяет null output и 0xFFFFFFFF до dynamic_cast.
- Социальные импорты и session objects перенесены в imports_online.cpp, чтобы тест
  линковал реальные функции и PPC wrappers без SDL/UI и всей игры.

### Проверки

- Новый `online_compat_test` с AddressSanitizer: прямые вызовы 9 PPC wrappers;
  синхронный/асинхронный friendship и ошибки; обнуление tile key, отсутствие записи
  в пиксели, completion именно из r10. Первые 64 KiB гостевой памяти защищены,
  поэтому прежняя 7-аргументная форма с height=32 не смогла бы пройти этот тест.
  Две инъекции OOM, неверный тип/invalid handle, 32 create/ref/release с проверкой
  освобождения обоих allocations. Куча выдаёт буферы с 0xA5 — обнуление реально нужно.
- Полный набор: **76/76 PASS**, 30.58 с, без повторов при корректных правах.
  Первый запуск в sandbox не мог слушать loopback (ealobby) и отображать physical
  memory (два write_watch), MetalFX был skipped. Повторён весь набор вне sandbox,
  все 76 прошли, включая MetalFX; это ограничения окружения, не исправление тестов.
- Реальная игра, журнал + кадры: `friends-before` — три Friends из Xbox Live,
  меню остаётся рабочим, exit 0 при штатной остановке. `social-room` — два игрока
  LAN и меню MacB (Kick / Submit Player Review / Show Gamer Card).
- `social-actions`: две копии LAN. Хозяин вызывает XamShowPlayerReviewUI в 79.6 с,
  затем XamShowFriendsUI в 83.6 с; второй игрок — XamShowGamerCardUIForXUID в 43.1 с
  своего процесса. После каждого на кадрах обычная комната, оба игрока на месте.
  Все соответствующие импорты возвращают сразу, без системных экранов Xbox Live.
- XamShowMessageComposeUI и tile completion проверены прямым PPC-тестом. UI-путь
  сообщения в игре и запрос самой картинки в игровом прогоне НЕ подтверждены.

### Что остаётся

`RtlCaptureContext`, `RtlRaiseException`, `RtlUnwind`, `__C_specific_handler`,
`HalReturnToFirmware` — 5 loud stubs. Прямые exception callers найдены в CRT
(включая heap corruption и C++ EH). Настоящий PPC exception dispatch/unwind и
выход в firmware пока unknown; они не заменялись успехом и не считаются готовыми.
Проверка между двумя Mac отложена пользователем; World Tour проверять по находкам.


### v0.1.18: пакет, публикация и установка

- Main и сборочная копия синхронизированы на a0559ba69efd8f578b47b91c191deb7300e93f51:
  все 12 изменённых файлов сверены с origin/main, stash -u сохранён, pull --ff-only.
- Чистый build; app macOS 15+, MoltenVK 1.4.2, dmgbuild; check_app DMG PASS.
  Кандидат 45 с с BURNOUT_MUTE=1, exit 0, главное меню по кадру, UNIMPLEMENTED нет.
  What's new сначала закрылось до кадра; повтор без скрипта пада в первые кадры
  подтвердил две видимые строки v0.1.18 без обрезки, закрытие и exit 0.
- 7 release assets: DMG, полный third-party source tar.xz, канонический ZIP Player Files,
  три .sha256, SHA256SUMS. Все скачаны из draft и побайтно сверены; SHA256SUMS PASS,
  check_app скачанного DMG PASS. SHA256 DMG:
  19fe0d6502c621ffa7fa13032ed28bfe4bc598275649e6ebf80365fd9da814e3.
- Опубликован Latest v0.1.18 на a0559ba; установлен именно скачанный DMG:
  SHA-256, codesign mounted/staging/installed и хеши обоих executable совпали.
  Старый app заменён без backup, временный mount отключён. Сейвы пользователя не трогались.

### Исследование состояний и новых возможностей

По запросу пользователя после совместимости изучены PPSSPP SaveState/Rewind,
архитектура текущего runtime и первичные README/issues 10 близких/крупных портов.
Результат — docs/NEW_FEATURES_RESEARCH_RU.md; очередь остаётся в PLAN_RU.md.
Измерен только известный Player Files (21 316 байт); размеры полного state и delta
не выдуманы — даны формулы и явно условные примеры. RAM snapshot не включает автоматически
нативные continuations, host kernel objects, GPU, audio, файлы и deadlines.
Save states/rewind полезны, но требуют настоящего restore; предложен проверяемый
эксперимент с одной гонкой. Внедрение не начиналось. Спрос по публичным feature issues
отделён от наличия фич, stars и собственной оценки приоритетов. Дополнительные читы
re:Blue обозначены как отдельный fork. Новых сообщений авторам не отправлялось.

Продолжи разработку Burnout Revenge для macOS (Apple silicon) с текущего состояния.
Общайся по-русски. Историю старых чатов не подгружай.

## Где что лежит

- Репозиторий (правки, коммиты, push в main): /Users/secretpray/Documents/Projects/XBOX360/burnout-revenge
  Remote: https://github.com/secretpray/burnout-revenge
- Сборочная копия того же репозитория с зависимостями и generated PPC:
  /Users/secretpray/Documents/Projects/XBOX360/speedbreaker/build/standalone/burnout-revenge
  Правь только основной репозиторий. Сборочную копию синхронизируй так:
  `git -C $B checkout -q -- . && git -C $B pull -q --ff-only`, а для незакоммиченных правок
  ещё `git diff HEAD | git -C $B apply` (новые файлы сначала `git add -N`).
- Сборка и тесты (в сборочной копии):
  `T=…/speedbreaker/tools; export PATH=$T/build-python/cmake/data/bin:$T/bin:$PATH`
  `cmake --build build/burnout-mac -j6` и `ctest --test-dir build/burnout-mac` (сейчас 67/67).
- Пакет: `MOLTENVK=$T/moltenvk/MoltenVK/MoltenVK/dylib/macOS/libMoltenVK.dylib MOLTENVK_VERSION=1.4.2
  python3 scripts/macos/make_app.py build/burnout-mac <out> --game=burnout [--dmg]`, затем
  `python3 scripts/macos/check_app.py <app или dmg>` (должно быть PASS).
- Тестовые запуски: `…/speedbreaker/build/release-0.1.9-validation/run.py`
  `python3 run.py <имя> <сек> "<UI-клавиши и N:Shot>" "<скрипт пада: 8-30/3:START,10-44/3:A,45~140:RT>" "" "" <env.json> <бинарь>`.
  Профиль копируй из `release-0.1.7-validation/data-bk` в новую папку, env — из
  `release-0.1.8-validation/env-base.json` с заменой BR_DATA_DIR. Настоящие сейвы
  пользователя (~/Library/Application Support/BurnoutRevenge) не трогать. 100%-сейв для
  событий с соперниками: `env-full.json` (data-full). Запуски оборачивай во временный
  `caffeinate -d -i -s -u`. Снимки: `<run>/build/ui_shot_N.ppm`, лог: `<run>/session.log`.
- Игра: ~/Library/Application Support/BurnoutRevenge/game; ISO: /Volumes/1Tb NVME/XBOX360.

## Состояние (18.10.2026)

- v0.1.11 опубликован (Latest, 436a13b) и установлен в /Applications из скачанного DMG:
  ползунки Bloom и Motion Blur 0–100%, звуки меню из fe.xwb (XMA2 целыми пакетами,
  подтверждены на слух). v0.1.10: импорт сейвов, 100%-сейв, пропуск трека в Crash.
- Сначала прочитай docs/ACTIVE_WORK.md (раздел с нумерованным планом) и при нужде
  docs/ROADMAP_RU.md; остальные документы — только по ссылкам. PROGRESS.md целиком не читать.

## Оставшиеся пункты плана (по порядку)

1. **3б. Редкое зависание на старте при Skip Intro Movies** (1 из ~100 запусков): что известно
   и что исключено — в ACTIVE_WORK.md. Дальше: лог resume/suspend/событий плеера под env,
   серия запусков под нагрузкой CPU; запасной вариант — пропускать ролик не раньше, чем
   плеер покажет первый кадр.
2. **Камера Takedown и её повтор** (TODO в п. 2 ACTIVE_WORK): проверить в событии с соперниками
   (100%-сейв, `BURNOUT_LOG_CLEARS=1`), что нет пустого scissor и артефактов.
3. **Аддон-машины**: вручную в Car Select шесть машин (Spike TV, Monster, Plantronics,
   360 livery, Yellowcard, Dolby) — попросить пользователя или снять снимками.
4. **Матрица карьеры/режимов** и оставшиеся 57 fatal stubs (в основном сеть) — по находкам.
5. **Руль** (SpeedBreaker PR #29): G29 через SDL joystick, без force feedback на macOS.
6. **HDR/EDR** — отложено: разведка в docs/IMAGE_QUALITY.md; экран пользователя (Samsung
   M70D) отдаёт EDR-запас 1,0. Вернуться только при HDR-экране.
7. Родной APT-экран клавиатуры — только ограниченный эксперимент; пока оставить нашу.

## Правила

- Я единственный автор: никаких `Co-Authored-By`, `Signed-off-by` и строк «Generated with»
  в коммитах, PR и релизах; после коммита проверяй, что `git log --format='%(trailers)'` пуст.
- После успешного этапа собирай пакет и ставь его в /Applications (прежний .app — в
  приватный backup в папке validation); после каждого релиза ставь скачанный релизный DMG
  (проверь SHA-256 и codesign).
- Релиз: поднять версию в CMakeLists.txt, перенести CHANGELOG из Unreleased, сборка, 67+
  тестов, пакет с `--dmg`, короткий запуск кандидата, `package_sources.py` для исходников
  third-party, SHA256SUMS, черновик `gh release create --target <полный SHA> --draft`,
  сверка скачанных ассетов, затем публикация Latest. Существующие теги не передвигать.
- В каждый релиз прикладывать `BurnoutRevenge-100-percent-save.zip` из
  speedbreaker/build/release-assets-next/ (только `Player Files`, sha256 dfcc64b3…6878);
  никаких CON, профилей и наших тестовых сейвов.
- Не публиковать ISO, ассеты игры, generated PPC, сейвы, сырые логи устройств и dSYM.
  Сохранять лицензии и атрибуцию SpeedBreaker/Xbox360-Native-Ports.
- Разделяй наблюдение по логам, по кадрам и ручное подтверждение пользователя. Если нужна
  ручная проверка (геймпад, звук), скажи, что нажать. Не используй системную автоматизацию UI.
- После каждого этапа обновляй ACTIVE_WORK.md. Действуй по плану без повторных согласований.

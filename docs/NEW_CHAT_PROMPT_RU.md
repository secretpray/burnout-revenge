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
  `cmake --build build/burnout-mac -j6` и `ctest --test-dir build/burnout-mac` (сейчас 72/72).
  Если в сборочной копии остались новые файлы от прошлого `apply`, перед ним удали их.
- Пакет: `MOLTENVK=$T/moltenvk/MoltenVK/MoltenVK/dylib/macOS/libMoltenVK.dylib MOLTENVK_VERSION=1.4.2
  python3 scripts/macos/make_app.py build/burnout-mac <out> --game=burnout [--dmg]`, затем
  `python3 scripts/macos/check_app.py <app или dmg>` (должно быть PASS).
- Тестовые запуски: `…/speedbreaker/build/release-0.1.12-validation/run.py` (скопируй в новую
  папку validation): `python3 run.py <имя> <сек> "<UI-клавиши и N:Shot>" "<скрипт пада>" "" "" <env.json> <бинарь>`.
  Готовые env там же: `env-base.json` (профиль data-bk), `env-matrix.json` (копия 100%-сейва
  data-matrix — для всего, где нужны соперники и открытые режимы; data-full не трогать),
  `env-uw.json` (окно 2100x900 → 1920x900, 2.13:1) и `env-uw329.json` (1920x540, 32:9).
  Настоящие сейвы пользователя (~/Library/Application Support/BurnoutRevenge) не трогать.
  Запуски оборачивай во временный `caffeinate -d -i -s -u`. Снимки: `<run>/build/ui_shot_N.ppm`
  (по 17 МБ — удаляй после просмотра), лог: `<run>/session.log`.
- Навигация пада (100%-сейв): `8-14/3:START,10-16/3:A` → главное меню к ~17 с (World Tour,
  Xbox Live, Burnout Clips, Multiplayer, Driver Details; без перехода по кругу). Road Rage в
  Eternal City: `8-17/3:START,10-19/3:A,20.5:RIGHT,21.3:RIGHT,22.1:RIGHT,22.9:RIGHT,23.8:A,26:A,28.5:A,35-41/2:A,41~58:RT`
  (гонка с ~42 с, авария с камерой удара ~47–50 с). Тайминги плавают: проверяй кадрами.
  Если окно теряет фокус в первые секунды, виртуальный пад иногда не работает — перезапусти.
- Отладка через env: `BURNOUT_LOG_2D=<сек>` (пакеты 2D: вызывающие адреса и габариты),
  `BURNOUT_LOG_NET=1`, `BURNOUT_LOG_SUSPEND=1`, `BURNOUT_LOG_CLEARS=1`, `BURNOUT_TRACE=1`
  (все импорты; тяжело), `BURNOUT_STUB_CONTINUE=1` (заглушка вернёт 0 вместо выхода).
- Игра: ~/Library/Application Support/BurnoutRevenge/game; ISO: /Volumes/1Tb NVME/XBOX360.

## Состояние (19.10.2026)

- v0.1.14 опубликован (Latest, 0bdced3), установлен в /Applications из скачанного DMG:
  USB-рули (SpeedBreaker PR #29, проверено на виртуальном G29), HUD гонки у краёв и на 16:10/4:3
  (game/hud_anchor.h). v0.1.13: HUD у краёв ultrawide. v0.1.12: нет зависания на старте
  (cpu/suspend_count.h), Xbox Live офлайн без выхода из игры. Тесты 70/70.
- Проверено: камера Takedown (ручные прогоны пользователя), шесть аддон-машин, матрица меню
  и режимов (кроме Player File; Multiplayer Race на двух виртуальных падах — да). Заглушек осталось 14
  (друзья/gamer card/сессии Live/исключения), офлайн не встречены.
- Сначала прочитай docs/ACTIVE_WORK.md (раздел «Очередь», пункты 11–16) и при нужде
  docs/EXTERNAL_PORTS_PLAN.md (последние разделы: Xerenge online, Fukami, Takedawn); остальные
  документы — только по ссылкам. PROGRESS.md целиком не читать.

## Оставшиеся пункты плана (по порядку)

Идеи из Fukami (Ridge Racer V, PS2, GPL-3.0, `danisandoval/fukami@2ecb281`) — порядок задал
пользователь, **онлайн только после них**. Подробности и критерии — ACTIVE_WORK, пункты 11–15.

1. [x] **Дальность LOD машин** — сделано (Settings > Graphics > Vehicle Detail, ACTIVE_WORK п. 11).
2. [x] **Геймпады по слотам игроков** — сделано (ACTIVE_WORK п. 12; `BURNOUT_VIRTUAL_PAD2`, шаг `DETACH`).
3. **Сохранения, устойчивые к сбою**: проверить путь записи сейвов; если на месте — копия,
   F_FULLFSYNC и замена при закрытии; тест обрыва посреди записи.
4. **Частота внешнего экрана в fullscreen** (Fukami `refresh_rate = auto`): 144/165 Гц → 120
   на время fullscreen и возврат; ProMotion не трогать.
5. **Быстрее загрузки** (Fukami `fast_unpack`): сначала профиль; нативная распаковка только если
   основное время — она, с проверкой байт в байт.
6. **Онлайн** — по фазам [ONLINE_PLAN_RU](ONLINE_PLAN_RU.md) (Xerenge 0.4.7: Live-вход + сервер
   лобби EA, Aries). У Xerenge нет лицензии — только идеи и адреса; SDK — BSD-3 с атрибуцией.
7. **Полная карьера** и оставшиеся 14 заглушек — по находкам.
8. **HDR/EDR** — отложено до HDR-экрана; **апскейл 2D** — отложен по замеру (вернуться на 1440p/4K).
9. Родной APT-экран клавиатуры — только ограниченный эксперимент; пока оставить нашу.

## Правила

- Я единственный автор: никаких `Co-Authored-By`, `Signed-off-by` и строк «Generated with»
  в коммитах, PR и релизах; после коммита проверяй, что `git log --format='%(trailers)'` пуст.
- После успешного этапа собирай пакет и ставь его в /Applications (прежний .app — в
  приватный backup в папке validation); после каждого релиза ставь скачанный релизный DMG
  (проверь SHA-256 и codesign).
- Релиз: поднять версию в CMakeLists.txt, перенести CHANGELOG из Unreleased, обновить README,
  сборка, 72+ тестов, пакет с `--dmg`, короткий запуск кандидата, `package_sources.py` для исходников
  third-party, SHA256SUMS, черновик `gh release create --target <полный SHA> --draft`,
  сверка скачанных ассетов, затем публикация Latest. Существующие теги не передвигать.
- В каждый релиз прикладывать `BurnoutRevenge-100-percent-save.zip` из
  speedbreaker/build/release-assets-next/ (только `Player Files`, sha256 dfcc64b3…6878);
  никаких CON, профилей и наших тестовых сейвов.
- Код без лицензии (Xerenge) не копировать: только идеи и адреса, с атрибуцией в комментарии.
- Не публиковать ISO, ассеты игры, generated PPC, сейвы, сырые логи устройств и dSYM.
  Сохранять лицензии и атрибуцию SpeedBreaker/Xbox360-Native-Ports.
- Разделяй наблюдение по логам, по кадрам и ручное подтверждение пользователя. Если нужна
  ручная проверка (геймпад, звук), скажи, что нажать. Не используй системную автоматизацию UI.
- После каждого этапа обновляй ACTIVE_WORK.md. Действуй по плану без повторных согласований.

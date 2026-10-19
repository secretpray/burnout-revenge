Продолжи разработку Burnout Revenge для macOS (Apple silicon) с текущего состояния.
Общайся по-русски. Историю старых чатов не подгружай.

## Где что лежит

- Репозиторий (правки, коммиты, push в main): /Users/secretpray/Documents/Projects/XBOX360/burnout-revenge
  Remote: https://github.com/secretpray/burnout-revenge
- Сборочная копия того же репозитория с зависимостями и generated PPC:
  /Users/secretpray/Documents/Projects/XBOX360/speedbreaker/build/standalone/burnout-revenge
  Правь только основной репозиторий. Незакоммиченные правки переноси в сборочную копию
  копированием файлов (`cp`) или `git diff HEAD > patch && git -C $B apply patch` (новые файлы
  сначала `git add -N`). После push: сверь, что изменённые файлы копии совпадают с origin/main
  (`git -C $B show origin/main:<файл> | cmp - $B/<файл>`), затем `git -C $B stash push -u` и
  `git -C $B pull --ff-only` (stash оставляй; `checkout -- .` в копии не используй).
- Сборка и тесты (в сборочной копии):
  `T=…/speedbreaker/tools; export PATH=$T/build-python/cmake/data/bin:$T/bin:$PATH`
  `cmake --build build/burnout-mac -j6` и `ctest --test-dir build/burnout-mac` (сейчас 73/73).
  Если в сборочной копии остались новые файлы от прошлого `apply`, перед ним удали их.
- Пакет: `MOLTENVK=$T/moltenvk/MoltenVK/MoltenVK/dylib/macOS/libMoltenVK.dylib MOLTENVK_VERSION=1.4.2
  python3 scripts/macos/make_app.py build/burnout-mac <out> --game=burnout [--dmg]`, для `--dmg` ещё
  `DMG_PYTHON=$T/dmg-python/bin/python3` (dmgbuild 1.6.5: оформленное окно образа), затем
  `python3 scripts/macos/check_app.py <app или dmg>` (должно быть PASS).
- Тестовые запуски: `run.py` из любой папки `…/speedbreaker/build/*-validation` (скопируй в новую):
  `python3 run.py <имя> <сек> "<UI-клавиши и N:Shot>" "<скрипт пада>" "" "" <env.json> <бинарь>`
  (с непустым скриптом пада он же ставит `BURNOUT_VIRTUAL_PAD=1`). Готовые env:
  `release-0.1.12-validation/env-base.json` (профиль data-bk), `wheel-validation/env-matrix.json`
  (копия 100%-сейва data-matrix — соперники и открытые режимы; data-full не трогать),
  `env-uw.json`/`env-uw329.json` (ultrawide), `wheel-validation/env-wheel.json` (виртуальный G29),
  `pads-validation/env-mp3.json` (два виртуальных пада → Multiplayer Race вдвоём),
  `pads-validation/env-swap.json` (отключение пада игрока 1), `save-validation/env-pf5.json`
  (своя копия data-save: Driver Details > Player File > Save Player File > «перезаписать? Yes»).
  Установка с настоящего ISO без окна: `cleanup-validation/cli.py <имя> TERM|KILL|none <сек> --install <iso> --dest <папка>`
  (свои BR_DATA_DIR/BR_CACHE_DIR; копию игры после проверки удаляй).
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
  (все импорты; ~300 МБ за 20 с — пиши во временную папку и удаляй), `BURNOUT_STUB_CONTINUE=1`
  (заглушка вернёт 0 вместо выхода), `BURNOUT_LOG_VEHICLE_DETAIL=1` (уровни LOD машин за 5 с),
  `BURNOUT_VIRTUAL_PAD2=<шаги>` (второй виртуальный пад; шаг `N:DETACH` отключает пад),
  `BURNOUT_TEST_SAVE_CRASH=1` (выход 77 сразу после первой записи сейва).
- Поиск в коде игры: generated PPC — `…/speedbreaker/ppc/burnout/ppc_recomp.*.cpp` (в комментариях
  исходные инструкции); константы .rdata — образ default.xex через XenonUtils
  (`…/speedbreaker/tools/XenonRecomp`, `Image::ParseImage`). Профиль процесса: `sample <pid> <сек>`.
- Игра: ~/Library/Application Support/BurnoutRevenge/game; ISO: /Volumes/1Tb NVME/XBOX360.

## Состояние (19.10.2026)

- v0.1.15 опубликован (Latest, 22d31bd), установлен в /Applications из скачанного DMG: Vehicle Detail
  (game/vehicle_detail.h, Far по умолчанию), игроки по падам и разделённый экран (hid/pad_slots.h),
  сейвы через копию (user/save_commit.h), установка без мусора (журнал install-unfinished),
  оформленный DMG (scripts/macos/styled_dmg.py, фон make_dmg_background.py). README: скриншоты
  разделённого экрана, Graphics с Vehicle Detail и окна DMG (снят пользователем); в CREDITS/README
  — Xerenge, Fukami, Takedawn. v0.1.14: рули, HUD у краёв 16:10/4:3. Тесты 73/73.
- Проверено: камера Takedown (ручные прогоны пользователя), шесть аддон-машин, матрица меню
  и режимов (кроме Player File; Multiplayer Race на двух виртуальных падах — да). Заглушек осталось 14
  (друзья/gamer card/сессии Live/исключения), офлайн не встречены.
- Сначала прочитай docs/ACTIVE_WORK.md (раздел «Очередь», пункты 11–16) и при нужде
  docs/EXTERNAL_PORTS_PLAN.md (последние разделы: Xerenge online, Fukami, Takedawn); остальные
  документы — только по ссылкам. PROGRESS.md целиком не читать.

## Оставшиеся пункты плана (по порядку)

Идеи из Fukami закрыты (ACTIVE_WORK п. 11–15а): Vehicle Detail, слоты падов, сейвы через копию,
установка без мусора — сделаны; загрузка — закрыта по замеру (docs/LOADING.md); частота внешнего
экрана (п. 14) — отложена, нет монитора 144/165 Гц.

1. **Сохранение Burnout Clip через копию** (быстрая регрессия): клип 4 МБ, GENERIC_ALL — теперь
   пишется через user/save_commit.h; проверить сохранение и просмотр в My Clips (как прогоны 188–197).
2. **Первый кадр гонки 117 мс** (docs/LOADING.md): 56 мс — write-watch guard stalls, 22 мс —
   109 текстур; разобрать, если окупится.
3. **Полная карьера** и оставшиеся 14 заглушек — по находкам.
4. **Онлайн** — автор Xerenge ответил (https://github.com/shipa-2/Xerenge/issues/28): сервер
   `94639.snk.wtf` разрешён, Xerenge теперь GPL-3.0-or-later, `ealobby` и заметки хуков можно
   адаптировать. План с фазами: [ONLINE_PLAN_RU](ONLINE_PLAN_RU.md); начинать с фазы 0.
5. **HDR/EDR** — отложено до HDR-экрана; **апскейл 2D** — отложен по замеру (вернуться на 1440p/4K).
6. Родной APT-экран клавиатуры — только ограниченный эксперимент; пока оставить нашу.

Ждут ручной проверки пользователем: два настоящих пада и пад по BT+USB одновременно, настоящий
руль, кнопка CANCEL INSTALL и выход во время установки в окне первого запуска.

## Правила

- Я единственный автор: никаких `Co-Authored-By`, `Signed-off-by` и строк «Generated with»
  в коммитах, PR и релизах; после коммита проверяй, что `git log --format='%(trailers)'` пуст.
- После успешного этапа собирай пакет и ставь его в /Applications (прежний .app — в
  приватный backup в папке validation); после каждого релиза ставь скачанный релизный DMG
  (проверь SHA-256 и codesign).
- Релиз: поднять версию в CMakeLists.txt, перенести CHANGELOG из Unreleased (его раздел — окно
  What's new в игре), обновить README, сборка, 73+ тестов, пакет с `--dmg` (DMG_PYTHON), короткий
  запуск кандидата, `package_sources.py <assets>/BurnoutRevenge-<v>-third-party-source.tar.xz
  --ffmpeg-archive …/speedbreaker/tools/ffmpeg-src/ffmpeg-7.1.1.tar.xz`, .sha256 и SHA256SUMS,
  заметки `release-<v>/notes.md`, черновик `gh release create --target <полный SHA> --draft`,
  сверка скачанных ассетов и `check_app.py` скачанного DMG, затем публикация Latest. Существующие
  теги не передвигать.
- Скриншоты окна Finder (DMG) снимает пользователь: у терминала нет записи экрана, не включать.
  Язык Finder временно: `defaults write com.apple.finder AppleLanguages '(en)' && killall Finder`,
  вернуть — `defaults delete com.apple.finder AppleLanguages && killall Finder`.
- В каждый релиз прикладывать `BurnoutRevenge-100-percent-save.zip` из
  speedbreaker/build/release-assets-next/ (только `Player Files`, sha256 dfcc64b3…6878);
  никаких CON, профилей и наших тестовых сейвов.
- Xerenge — GPL-3.0-or-later с `c023338`: код брать только из ревизий с лицензией (закреплённый
  SHA), с копирайтом в файлах и строкой в CREDITS. Код без лицензии не копировать.
- Не публиковать ISO, ассеты игры, generated PPC, сейвы, сырые логи устройств и dSYM.
  Сохранять лицензии и атрибуцию SpeedBreaker/Xbox360-Native-Ports.
- Разделяй наблюдение по логам, по кадрам и ручное подтверждение пользователя. Если нужна
  ручная проверка (геймпад, звук), скажи, что нажать. Не используй системную автоматизацию UI.
- После каждого этапа обновляй ACTIVE_WORK.md. Действуй по плану без повторных согласований.

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
  `cmake --build build/burnout-mac -j6` и `ctest --test-dir build/burnout-mac` (сейчас 75/75;
  `swapchain_use_test` и `guest_time_test` иногда падают под нагрузкой — повтори отдельно).
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
- **Онлайн-проверки** — папка `…/speedbreaker/build/online-validation` (вне репозитория):
  - Свой сервер лобби: `$B/build/burnout-mac/runtime/burnout-lobby --host 127.0.0.1 --locker-dir <tmp>`
    (запускать в фоне, журнал — во временную папку; после работы остановить). Сервер автора
    Xerenge: `BURNOUT_LOBBY_SERVER=94639.snk.wtf` (разрешён, issue #28).
  - Боты: `python3 bots.py <имя> [копий 2–4] [сек] [сервер=127.0.0.1]` — хозяин + боты на
    127.0.0.2–5, данные `data-onA…D`, сами входят в Live, создают/находят Player Match, стартуют
    и едут (газ); в конце отчёт по журналам. Гонка ~5–7 мин; 4 копии × 2 гонки ≈ 17 мин — запускать
    в фоне. Адреса lo0 127.0.0.2–5 живут до перезагрузки; добавляет пользователь:
    `sudo ifconfig lo0 alias 127.0.0.N up` (пароль — только в его Терминале).
  - `two.sh` — две копии через сервер автора для ручной игры пользователем.
  - Онлайн-env: `BURNOUT_ONLINE=1`, `BURNOUT_GAMERTAG`, `BURNOUT_LOBBY_SERVER`,
    `BURNOUT_ONLINE_ADDRESS`, `BURNOUT_LOG_NET=1` (журнал большой — после прогонов удалять
    папки прогонов и `data-on*/logs/*`: диск у пользователя почти полон).

## Состояние (20.10.2026)

- v0.1.15 опубликован (Latest) и установлен. После него в main — онлайн (всё под
  `BURNOUT_ONLINE`, по умолчанию выключен; в релиз не выходил).
- Онлайн по плану [ONLINE_PLAN_RU](ONLINE_PLAN_RU.md) (читать раздел нужной фазы и «Ход фазы»):
  фазы 0–1 (консоль «в Live»), 3 (хуки входа), 2 (свой сервер: `runtime/thirdparty/ealobby` —
  Xerenge `cfe047e` без изменений + наши правки безопасности, утилита `burnout-lobby`, тест
  `ealobby_test`; вход и 30 мин простоя), 4 (гонка на двоих) — сделаны; 5 — боты и гонка
  вчетвером на одном Mac работают (общий UDP-порт 8192 с сокетом на игрока, SO_NOSIGPIPE).
- Заглушек офлайн не встречено; Ranked не работает (нужен XSessionSearch).
- Сначала прочитай docs/ACTIVE_WORK.md (п. 16 — онлайн, «Очередь»); PROGRESS.md целиком не читать.

## Оставшиеся пункты плана (по порядку)

1. **Онлайн, фаза 5 дальше**: LAN без сервера (сервер внутри игры: `ealobby` с `self_address`,
   анонсы UDP 31859; у федерации ещё нет проверки соседей — см. README ealobby), Crash Party/
   Crash Battle ботами; затем один-два ручных прогона пользователя на двух Mac (LAN и интернет).
2. **Фаза 6**: Burnout Clips через Locker (`burnout-lobby` уже отдаёт `dir`), проверить сохранение
   клипа через копию (user/save_commit.h, 4 МБ).
3. **Фаза 7**: Настройки > Онлайн — выбор сервера (автора / свой / LAN), повторное подключение с
   паузами; затем релиз с онлайном.
4. Отправить автору Xerenge issue с журналами (текст готов: burnout-revenge/private/xerenge-report)
   и, по желанию пользователя, наши правки безопасности ealobby.
5. Первый кадр гонки 117 мс (docs/LOADING.md), полная карьера — по находкам.
6. HDR/EDR и апскейл 2D — отложены; APT-клавиатура — ограниченный эксперимент.

Ждут ручной проверки пользователем: два настоящих пада и пад по BT+USB одновременно, настоящий
руль, кнопка CANCEL INSTALL и выход во время установки в окне первого запуска.

## Правила

- Я единственный автор: никаких `Co-Authored-By`, `Signed-off-by` и строк «Generated with»
  в коммитах, PR и релизах; после коммита проверяй, что `git log --format='%(trailers)'` пуст.
- После успешного этапа собирай пакет и ставь его в /Applications (резервных копий прежних
  сборок не держать); после каждого релиза ставь скачанный релизный DMG (проверь SHA-256 и
  codesign).
- Диск у пользователя небольшой: в `…/speedbreaker/build` держать только нужное для текущего и
  следующего релиза (standalone, deps, release-assets-next, release-<текущий>, тестовые данные
  `data-*`, env и скрипты). Папки прогонов, кандидаты, пакеты прошлых версий и бэкапы удалять,
  как только проверка закончена.
- Релиз: поднять версию в CMakeLists.txt, перенести CHANGELOG из Unreleased (его раздел — окно
  What's new в игре), обновить README, сборка, 75+ тестов, пакет с `--dmg` (DMG_PYTHON), короткий
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

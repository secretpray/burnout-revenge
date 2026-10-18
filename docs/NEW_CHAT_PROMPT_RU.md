Продолжи разработку Burnout Revenge для macOS из текущего состояния. Общайся по-русски.

Основной репозиторий: /Users/secretpray/Documents/Projects/XBOX360/burnout-revenge
Remote: https://github.com/secretpray/burnout-revenge
Существующая сборочная копия с зависимостями/generated PPC:
/Users/secretpray/Documents/Projects/XBOX360/speedbreaker/build/standalone/burnout-revenge
Не веди две расходящиеся версии исходников: сначала сверь git status, remote и commit.
Исходную сборку Most Wanted сохрани работоспособной.

Сначала прочитай docs/ACTIVE_WORK.md и docs/ROADMAP_RU.md. Остальные документы
читай только по нужным ссылкам; PROGRESS.md и историю старого чата целиком не загружай.

Контрольная точка на 18.10.2026:
— Runtime focus-правки в 2cf1d87, изолированный SDL probe в 99fcf50; новые docs-коммиты
  могут быть выше. Полные suites: standalone 61/61, shared Burnout 63/63, NFSMW 62/62.
— В /Applications/Burnout Revenge.app установлен скачанный v0.1.1/cda797a БЕЗ новых
  focus-правок. Он воспроизводил отказ Magic Keyboard. v0.1.0 опубликован, v0.1.1 draft;
  существующие теги не передвигать. Сверь фактическое состояние перед изменениями.
— Отдельный новый кандидат, запуск 96: 313,5 с, exit 0, без pad removals/fatal imports.
  Пользователь подтвердил стабильную Magic Keyboard в игре и после возврата. Это
  короткая положительная keyboard-проверка, не гарантия для всех Bluetooth-устройств.
— Live FG выключена. FSR 1/MetalFX Spatial уже реализованы. Остаётся 57 fatal stubs.
— Экранная клавиатура: пользователь предпочитает родной экран САМОЙ Burnout, только
  если он реально точнее; иначе оставить нашу. Xerenge тоже использует ImGui.
  В APT есть KeyboardShow/Move/Type/Back, но рабочий родной путь ещё не доказан.

Ближайшая задача: собрать чистый новый пакет с focus-правками, проверить его как
скачанный артефакт, установить в Applications и отдельно подтвердить реальный
контроллер, звук, печать после обоих направлений Cmd+Tab, Cmd+H, exit/relaunch.
Не публикуй старый cda797a как исправленный. Для нового кода нужен новый свободный
patch-tag после проверки тегов, а не перенос v0.1.1. Затем следуй этапам плана:
shader/PSO preparation → image quality → безопасная FG → остальные функции.

ISO: /Volumes/1Tb NVME/XBOX360/Burnout Revenge (USA).iso
Add-on: /Volumes/1Tb NVME/XBOX360/454107DC
Пользовательские settings/save: ~/Library/Application Support/BurnoutRevenge
Кэши: ~/Library/Caches/BurnoutRevenge
Приватная история тестов: старый workspace, build/release-validation-20261018;
изолированный тестовый профиль — tested-data. Настоящие saves не сбрасывать.

Разрешаю необходимые сборки, локальные проверки/запуски, обновление приложения и
подготовку исправленного релиза. Если нужна реальная печать/геймпад — скажи, что
нажать: я участвую в ручных проверках. Не применяй CUA/OS UI automation и не внедряй
системные нажатия; отключение клавиатуры было системным. Для бодрствования только
временный caffeinate, лучше привязанный к тестовому процессу; постоянную блокировку
macOS не менять. После каждого запуска обновляй результаты и открытые задачи.
Разделяй наблюдение логов, кадров и ручное подтверждение. Не выдавай prototype,
счётчик FPS или стилизованный UI за завершённую/родную реализацию. Не публикуй ISO,
assets, generated PPC, saves, raw device logs и dSYM. Сохраняй лицензии и атрибуцию
SpeedBreaker/Xbox360-Native-Ports. Действуй по плану без повторных общих согласований.

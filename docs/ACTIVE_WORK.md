# Открытые задачи

Обновлено 18.10.2026. Только незавершённая/плановая работа; история — отдельно.

- [ ] v0.1.2 (draft, тега нет). Прогон 103 (cb135aa, установленный пакет) повторил отказ
  Magic Keyboard после Quit Game и отвал DualSense при переходе в полноэкранный режим.
  Системный журнал: отказы следуют за macOS Game Mode (bluetoothd ~170 повторов/мин
  LE-параметров всю сессию; в прогоне 93 то же, в 96 без Game Mode — нет). Кандидат с
  `LSSupportsGameMode=false` — проверить: fullscreen + Quit Game, журнал без Game Mode
  и без повторов, подтверждение клавиатуры, relaunch. Подробно:
  [RELEASE_VALIDATION_0.1.2](RELEASE_VALIDATION_0.1.2.md).
- [ ] Новые экраны первого запуска (линейные 01/02/03, Barlow Condensed, EA Trax с диска)
  прошли в 103 по логу; звуки навигации меню из `sound/fe.xwb` (XACT2) не сделаны.
- [ ] Родной APT-экран Burnout исследовать ограниченным экспериментом. Сейчас
  оставить нашу клавиатуру: перенос ImGui-диалога Xerenge оригинал не восстанавливает.
  Заменять только после доказанной большей точности и проверки полного пути.
- [ ] Optional shader/PSO preparation из пользовательского ISO; coverage manifest,
  cancellation/resume и cold/prepared сравнение.
- [ ] SMAA/CAS и bloom/LOD audit с одинаковой сценой и GPU замерами.
- [ ] GPU history consumer/leases/events и MetalFX FI; сравнить draw-state interpolation.
  Сначала Crash 30→60, затем 60→120. Live FG пока выключена.
- [ ] Save backup/restore, controller glyphs, custom music, полное Add-on покрытие.
- [ ] 57 fatal imports, career/Crash/replay матрица; регионы/GOD/online отдельно позже.

Начать с [подробного плана](ROADMAP_RU.md), затем нужных разделов
[исследования внешних портов](EXTERNAL_PORTS_PLAN.md),
[экранной клавиатуры](NATIVE_KEYBOARD_RU.md),
[SDL диагностики](INPUT_DIAGNOSTICS.md) и
[аудита релиза](RELEASE_VALIDATION_2026-10-18.md),
[проверки 0.1.2](RELEASE_VALIDATION_0.1.2.md).
Не читать PROGRESS.md целиком ради восстановления контекста.

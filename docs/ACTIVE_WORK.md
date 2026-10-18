# Открытые задачи

Обновлено 18.10.2026. Только незавершённая/плановая работа; история — отдельно.

- [x] v0.1.2 опубликован (Latest, 478cf28): отказ от macOS Game Mode; прогоны 104/105 чистые
  по системному журналу, клавиатура после Quit Game подтверждена пользователем.
- [ ] Cmd+Tab/Cmd+H в полноэкранном режиме: в 104/105 потерь фокуса в логе нет — отдельно
  проверить, что игра получает focus-lost в fullscreen Space.
- [ ] v0.1.3 (draft): меню настроек в стиле экрана Settings игры, латинские названия
  клавиш, About на Advanced, анимированные шевроны. Нужна ручная проверка пользователем.
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

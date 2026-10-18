# Открытые задачи

Обновлено 18.10.2026. Только незавершённая/плановая работа; история — отдельно.

- [ ] Собрать и проверить конечный пакет с focus-правками. В Applications всё ещё
  скачанный v0.1.1/cda797a, без них; этот пакет воспроизводил отказ Magic Keyboard.
  Тег v0.1.1 не менять. Новый source-кандидат успешно прошёл короткую ручную
  keyboard-проверку 96; окончательный package/hardware gate ещё открыт.
- [ ] В пакете отдельно подтвердить реальное управление контроллером и звук,
  Cmd+Tab в обе стороны, Cmd+H/restore, exit/relaunch, USB/Bluetooth. Нужен пользователь;
  не применять OS UI automation/CUA. До прохождения не публиковать hotfix как исправленный.
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
[аудита релиза](RELEASE_VALIDATION_2026-10-18.md).
Не читать PROGRESS.md целиком ради восстановления контекста.

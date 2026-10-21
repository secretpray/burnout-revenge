# План: Burnout Revenge для Mac

Единственный план проекта: что сделано (коротко), что дальше и в каком порядке, что ждёт
условий или ручной проверки. Обновлять после каждого этапа. История запусков и подробности
сделанного — в git (коммиты) и [PROGRESS](PROGRESS.md); для игроков — README и
[ONLINE](ONLINE.md).

## Состояние (21.10.2026)

- **v0.1.16 — Latest** (`bf4a64b`), установлен в /Applications (скачанный DMG, sha256
  `7427ebe1…d730`, codesign, check_app PASS). Тесты 75/75.
- В игре: полный офлайн (World Tour, Multiplayer на разделённом экране, Burnout Clips),
  настройки в стиле игры, сейвы через копию и резервные копии, установщик с диска, моды
  машин, руль (виртуально), ultrawide/Vert+ HUD.
- **Онлайн (v0.1.16, по умолчанию выкл.)**: Settings > Online — сервер автора Xerenge
  (`94639.snk.wtf`), свой (`burnout-lobby` в пакете) или LAN без сервера; гонки, Road Rage,
  Crash Party/Tour/Battle до четырёх игроков, Burnout Clips через Locker. Проверено ботами на
  одном Mac и двумя копиями через сервер автора; **между двумя Mac не проверено**; Ranked нет.
- Автору Xerenge отправлены (21.10): отчёт о macOS-сборке 0.4.7 с логами, сообщение об уязвимостях
  `ealobby`, находки и предложения (Ranked, боты). Ждём ответа.

## Очередь (по порядку)

### 1. Ranked
Сейчас Xbox Live > Quick Match > Ranked → «Connection problem»: игра шлёт XGI `0x000B0016`
(XSessionSearch — поиск сессий по правилу matchmaking, TrueSkill), мы отвечаем X_E_FAIL
(`runtime/kernel/xmsg.cpp`, `XgiOnline`). Create Match с Ranked: Yes и Scoreboards не
проверены. Известно: при входе игра сама создаёт в лобби комнату Ranked (`gcre`, экран «Ranked
game», «Not enough players»), шлёт `cate VIEW=XBOX.en` (`R=RANKED,1,ALL,1,DEFAULT,0,0`);
XSessionArbitrationRegister (`0x000B001A`) и XSessionWriteStats (`0x000B0025`) мы принимаем
«успехом» без данных. У Xerenge Ranked тоже не работает.
1. Разведка: Quick Match → Ranked и Create Match → Ranked: Yes с `BURNOUT_LOG_NET=1`; разобрать
   аргументы `0x000B0016` по generated PPC вызывающего кода (индекс процедуры, свойства и
   контексты поиска, буфер `XSESSION_SEARCHRESULT_HEADER`, overlapped); что игра делает с
   пустым результатом и с найденной сессией (ждёт XSESSION_INFO хоста → XGISessionCreate join).
2. Ответ на поиск: сначала пустой успешный результат (игра становится хостом, как консоль без
   найденного матча) и сведение игроков через лобби (`gsea`/`gqwk` с флагами Ranked; `SESS`
   хоста уже ходит в записи игры). Если игре нужны сессии — заполнять
   `XSESSION_SEARCHRESULT` из записей лобби (XNADDR, XNKID/XNKEY из `SESS`).
3. Боты: `bots.py` — хозяин с Ranked: Yes, боты Quick Match → Ranked (нужна правка
   сценария); 2 и 4 копии, свой сервер и LAN.
4. Scoreboards (XUserReadStats/CreateStatsEnumerator — что просит игра). Хранить ли рейтинг на
   нашем сервере — решение пользователя (сервер автора так не умеет, совместимость с ним —
   решение 19.10).
5. Готово: ranked-гонка на двоих и вчетвером доезжает до результатов на своём сервере и по LAN;
   на сервере автора — не хуже, чем там умеет Xerenge. Затем релиз v0.1.17.

### 2. Онлайн между двумя Mac
Ручной прогон пользователя: LAN (`Server: LAN`) и сервер автора; разные геймертеги; брандмауэр
macOS — разрешить входящие. По находкам править. Совместимость с игроками Xerenge (порты
совпадают: ниже 1024 → +30000) — при случае.

### 3. Ответы автора Xerenge
Ждём: исправит ли уязвимости `ealobby` (наши правки — коммиты c80a3cd, 276f9f0, 36210f1),
ответ по macOS-сборке, по Ranked. Брать его исправления сервера (копированием файлов поверх
`runtime/thirdparty/ealobby`, README там же) и держать протокол совместимым.

### 4. Совместимость и карьера
- 14 заглушек, офлайн не встречавшихся: друзья, gamer card, сообщения
  (`XamShow{Friends,GamerCardForXUID,MessageCompose,PlayerReview}UI`, `XamUserAreUsersFriends`,
  `XamParseGamerTileKey`/`ReadTileToTexture`), `XamSession*`, исключения
  (`RtlCaptureContext`/`RaiseException`/`Unwind`, `__C_specific_handler`),
  `HalReturnToFirmware`. **Онлайн они достижимы** (меню друзей и карточек в Xbox Live) —
  проверить, что их вызов не роняет игру, и дать безопасные ответы.
- Полная карьера World Tour, все режимы и Player File — по находкам. Непроверенное отмечать
  как unknown.
- Позже: регионы EU/PAL (свой XEX/generated code), GOD-образы (отдельный импорт).

### 5. Первый кадр гонки
117 мс на первом кадре гонки ([LOADING](LOADING.md)) — разобрать, если найдётся дешёвый выигрыш.

### 6. Интерполяция кадров (исследование)
Live FG выключена. Порядок: id кадров и сбросы (cut/resize/меню/загрузка) → безопасная
передача истории Vulkan/MoltenVK → Metal (leases/events) → MetalFX FI в режиме разработчика с
откатом на настоящий кадр → сравнить с собственной draw-state interpolation (подход Xerenge,
не их код). Сначала Crash 30→60, затем гонка 60→120 на 120 Гц. В настройки — только после
замеров плавности, задержки и артефактов.

### 7. Родной экран клавиатуры (APT)
Ограниченный эксперимент по [NATIVE_KEYBOARD_RU](NATIVE_KEYBOARD_RU.md): заменять наш диалог
только если найден полный игровой путь и он точнее. Иначе — записать решение «не заменять».

## С условиями (не начинать без них)

- **HDR/EDR** — нужен HDR-экран (у Samsung M70D пользователя запас EDR 1,0). Разведка: цели
  8 бит, блики до ~2; путь — RGBA16F + EXTENDED_SRGB_LINEAR ([IMAGE_QUALITY](IMAGE_QUALITY.md)).
- **Апскейл 2D (Super-xBR)** — замер на 1080p: выигрыш мал; вернуться при 1440p/4K или
  жалобах на мыло.
- **Смена частоты внешнего экрана в fullscreen** (144/165 → 120 Гц) — нужен такой монитор
  для проверки; без проверки не делать.
- **Обновления SpeedBreaker** — каждый релиз upstream разбирать на пользу для Mac.

## Ждут ручной проверки пользователем

Два настоящих пада; один пад по BT и USB одновременно; настоящий руль (оси, кнопки, force
feedback); CANCEL INSTALL и выход во время установки в окне первого запуска; онлайн между
двумя Mac.

## Справка: онлайн для разработки

- **Фазы 0–7 сделаны** (19–20.10): консоль «в Live» (`user/online.*`, XNADDR, XUID на
  геймертег), хуки входа (`game/online_hooks.cpp`, `game/online_log.cpp` — адреса Xerenge,
  сверены с нашей USA-сборкой), свой сервер (`runtime/thirdparty/ealobby` — Xerenge `cfe047e`
  без изменений + наши правки, README там же; `burnout-lobby`, тест `ealobby_test`), гонка
  на двоих, 3–4 игрока и LAN (лобби в игре, `BURNOUT_LOBBY_SERVER=lan`), Burnout Clips через
  Locker, Settings > Online.
- **Сеть** (`kernel/burnout_imports.cpp`): порты ниже 1024 → +30000 (как `MapWirePort` у
  Xerenge); общий UDP-порт 8192 — сокет на игрока (`SharePort`/`ExpectPeer`/`Route`, правило
  Xerenge); перекрытый приём через свой поток; `WSAGetOverlappedResult` ставит код через
  `SocketFailure` (иначе поток приёма DirtySock `0x824163B8` бросает сокет); пауза повторов
  входа 5→60 с (`online::LobbyConnectAllowed`).
- **Протокол лобби**: Aries — заголовок 12 байт (команда, код, длина BE) + `KEY=VALUE`, NUL;
  `@dir` на 31860, лобби 31861, Locker HTTP 31862 (`locker.jsp?cmd=dir|get|put|del&lkey=…`),
  LAN-анонсы UDP 31859 и связь лобби `xhlo/xjoi/xfwd/xlea`. ID игрока — FNV-1a геймертега.
- **Протокол между игроками**: порт 1000 — байт 2: 1 запрос / 2 подтверждение / 0 без; байт 3 —
  тип (`0x15` приглашение хоста, `0x05` соединение, `0x08` список игроков, `0x0D/0x0E` пинг);
  далее номер и ID «от → кому» (младшие 32 бита XUID без старшего бита). Порт 8192 — CommUDP:
  новичок ждёт каждого (`connect ":8192:8192#$свой-$чужой"`, состояние 3), сидящие зовут его
  (`listen "<адрес>:8192:8192#…"`, состояние 2); привет `00 00 00 01` + хеш ключа, ответ
  `00 00 00 02`; связь 4 → обе стороны шлют `0x05` на порт 1000. Журнал состояний — в
  `game/online_log.cpp` (`BURNOUT_LOG_NET=1`).
- **Боты** (`…/speedbreaker/build/online-validation/bots.py`): 2–4 копии на 127.0.0.2–5, сценарий
  пада по событиям игры (`@menu`, `@online`, `@session`, `@race`, `@raceend`), `BOTS_MODE` 1–4
  (Road Rage, Crash Party, Crash Tour, Crash Battle), сервер `127.0.0.1` или `lan`.

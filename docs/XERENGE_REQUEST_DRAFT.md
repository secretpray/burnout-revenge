# Request to the Xerenge author (draft, not sent)

Where: a new issue in https://github.com/shipa-2/Xerenge/issues (no e-mail on the
profile, Discussions are off). Post it from your own account; English is the
language of that tracker. Before posting: check the links, the version, and that
the "what we did" part is still true.

---

**Title:** Mac port of Burnout Revenge (GPL-3.0): may it use your lobby server, and under what licence can we use your online work?

Hi Shipa_2,

I maintain [Burnout Revenge for Mac](https://github.com/secretpray/burnout-revenge), a
native Apple-silicon port of the USA release (static recompilation, its own Metal/MoltenVK
renderer, no game files distributed; GPL-3.0, derived from the SpeedBreaker runtime). Xerenge
has been the best research reference for this game: we already credit you for the music-skip
finding and other ideas in our CREDITS.

We'd like to add online play, and we want it to work *with* Xerenge rather than beside it, so
that Mac players can join the same games as Xerenge players on Linux, Windows, Android and your
macOS build. Before we write anything, three questions:

1. **Your server.** May our client log in to `94639.snk.wtf`? We would list it in our settings
   as "Xerenge community server (by shipa-2)", next to "your own server" and "LAN". We'll keep
   to any limits you set (connections, reconnect back-off, Locker uploads), identify our client
   in a field you choose so you can tell it apart or turn it off, and remove the entry the day
   you ask. If you'd rather not, we'll ship only self-hosted and LAN.

2. **Licence.** Xerenge has no licence file, so today we can only re-implement from protocol
   facts (Aries framing, commands, ports, the Locker URLs) with attribution, without copying a
   line. Would you add a licence (MIT or BSD would be ideal; anything GPL-3.0-compatible works)
   — or, if you prefer, give written permission for us to adapt `rexglue/src/ealobby/` and the
   online hook notes in `online_stub.cpp` in a GPL-3.0 project, with your copyright kept in
   each file and credit in our README/CREDITS?

3. **Compatibility.** Is the protocol on your server the one in 0.4.7 (`22dad21`)? Do you plan
   changes we should follow (a version field, the `NUMPART`/port-8192 fixes from #19)? Is
   there a way you'd like us to report server-side problems — for example the 2-minute connect
   in #24, if we see it too?

In return: we'll report what we find against your server, send fixes upstream as PRs where they
fit Xerenge, and link back to your project from ours.

Thanks for Xerenge — it made this game playable again for a lot of people.

— secretpray (Burnout Revenge for Mac)

---

## Перевод для себя (не публиковать)

**Заголовок:** Порт Burnout Revenge на Mac (GPL-3.0): можно ли подключаться к вашему серверу
лобби и на каких условиях использовать ваши онлайн-наработки?

Привет! Я веду Burnout Revenge for Mac — нативный порт USA-версии для Apple silicon
(статическая рекомпиляция, свой рендер, без файлов игры; GPL-3.0, на основе SpeedBreaker).
Xerenge — лучший источник исследований по игре; мы уже благодарим вас в CREDITS.

Хотим сделать онлайн совместимым с Xerenge, чтобы игроки Mac попадали в те же игры, что и
игроки Xerenge. Три вопроса:

1. **Сервер.** Можно ли нашему клиенту входить на `94639.snk.wtf`? В настройках он будет
   «Xerenge community server (by shipa-2)» рядом со «своим сервером» и «LAN». Соблюдаем ваши
   лимиты, помечаем клиента полем на ваш выбор, убираем из списка по первой просьбе. Если
   нет — только свой сервер и LAN.
2. **Лицензия.** Лицензии нет, поэтому сейчас можем только переписать по фактам протокола с
   атрибуцией. Добавите лицензию (лучше MIT/BSD; подойдёт любая, совместимая с GPL-3.0) или
   письменно разрешите адаптировать `ealobby` и заметки о хуках онлайна в GPL-3.0-проекте с
   вашим копирайтом в файлах и упоминанием в README/CREDITS?
3. **Совместимость.** Протокол на сервере — как в 0.4.7? Будут ли изменения (поле версии,
   исправления из #19)? Как сообщать о проблемах сервера (например, 2-минутное подключение
   из #24)?

Взамен: сообщаем о найденном, присылаем исправления PR, ссылаемся на проект.

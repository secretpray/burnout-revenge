# ealobby — Xerenge's lobby server for Burnout Revenge

A stand-in for EA's long-gone Burnout Revenge lobby servers (the Aries
directory and lobby on ports 31860/31861, EA's Locker for Burnout Clips on
31862), written by Shipa Snake for Xerenge.

- Upstream: <https://github.com/shipa-2/Xerenge>, `rexglue/src/ealobby/` and
  `rexglue/tools/ealobby/main.cpp`.
- Imported from commit `cfe047e3d758736624392469c0a54b4c64a650cc` (2026-10-20);
  the files were last changed upstream in `0ac02b5a427c0cf9aa9f2c439878b3610dbe34cb`.
- Copyright (C) 2026 Shipa Snake (shipa-2) and the Xerenge contributors.
- Licence: GPL-3.0-or-later (Xerenge since `c023338`; see `COPYING` at this
  repository's root). Used here with its author's agreement
  (shipa-2/Xerenge#28).

`src/` and `cli/main.cpp` (upstream's `tools/ealobby/main.cpp`: `tools/` is
ignored here) are upstream's files as they were at that commit, unmodified by
the import, so upstream's fixes can be taken over by copying them again.
Changes of ours go in separate commits on top, each listed here:

- `server.cpp`: a peer link (`xhlo`) only on a server with seamless
  multiplayer on (`self_address`), and `xfwd` answers only to a client joined
  through that link; login keys (`LKEY`) from the system's random source;
  `gset` changes the game and kicks only from its host (any player still sets
  its own `USERPARAMS`).
- `server.cpp`: seamless multiplayer is for a LAN - a peer link (`xhlo`)
  only from a loopback, private or link-local address that the link names
  as its `NODE`, and a game beacon only for a LAN server and from it (a
  beacon naming another address would send this server's players there);
  copies on one machine may use any loopback address.
- `server.cpp`: while a player of another server joins a game hosted here,
  what that tells this server's own clients is held back 250 ms - the
  joiner's answer goes the longer way, through its own server, and a host
  told first called the joiner before it knew the game (the fourth player of
  a Crash Party never connected). EA's servers answered the joiner first too.

What it cannot do: tell who a player is. EA's servers trusted Xbox Live for
the gamertag; here a client's `auth` names its persona, and anyone can log in
as anyone (and so reach that persona's Burnout Clips in the Locker). Run a
server for people you know.

Built by `runtime/CMakeLists.txt`: the `ealobby-core` library, the
standalone server `burnout-lobby` (docs/ONLINE_PLAN_RU.md, phase 2), and the
game's own lobby for a LAN with no server (`BURNOUT_LOBBY_SERVER=lan`,
`runtime/user/online.cpp`, phase 5).

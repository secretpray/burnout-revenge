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

- (none yet)

Built by `runtime/CMakeLists.txt`: the `ealobby-core` library and the
standalone server `burnout-lobby` (docs/ONLINE_PLAN_RU.md, phase 2).

// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// Online play (docs/ONLINE_PLAN_RU.md), phase 0: the console says it is
// signed in to Xbox Live, and game/online_log.cpp logs the login, to see
// where it stops. Nothing is contacted yet: the network stays the offline
// console's (kernel/burnout_imports.cpp).
//
//   BURNOUT_ONLINE=1          signed in to Live as user 0 (off by default)
//   BURNOUT_GAMERTAG=<name>   the gamertag (user/online_identity.h), "Player"
//
// Read once, at first use; any thread.
#pragma once

#include <cstdint>
#include <string>

namespace online
{
    bool Enabled();
    // The gamertag and the XUID user 0 has online (offline: "Player" and the
    // offline XUID, as before).
    const std::string& Gamertag();
    uint64_t Xuid();
}

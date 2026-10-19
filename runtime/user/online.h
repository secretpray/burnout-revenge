// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// Online play (docs/ONLINE_PLAN_RU.md), phase 0: the console says it is
// signed in to Xbox Live, and game/online_log.cpp logs the login, to see
// where it stops. Nothing is contacted yet: the network stays the offline
// console's (kernel/burnout_imports.cpp).
//
//   BURNOUT_ONLINE=1               signed in to Live as user 0 (off by default)
//   BURNOUT_GAMERTAG=<name>        the gamertag (user/online_identity.h), "Player"
//   BURNOUT_LOBBY_SERVER=<host>    the lobby server (EA's, stood in for: Xerenge's
//                                  ealobby or ours), by name or IPv4; none: 127.0.0.1
//   BURNOUT_ONLINE_ADDRESS=<ipv4>  this console's address for its peers, and the
//                                  one its sockets bind to (127.0.0.2 and 127.0.0.3
//                                  for two copies on one Mac); none: the LAN's
//
// Phase 1: with BURNOUT_ONLINE the network is a console's with a working
// connection (kernel/burnout_imports.cpp: link up, an XNADDR, real sockets) and
// XOnlineGetServiceInfo names the lobby server (kernel/xmsg.cpp).
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
    // IPv4 addresses in network byte order. The lobby server's is looked up
    // once, here and not by the game, which can't resolve a name (the
    // address it is given must be one: Xerenge's issue #24). 127.0.0.1 when
    // none is set or the name doesn't resolve.
    uint32_t LobbyServerAddress();
    // This console's address as its peers see it (its XNADDR).
    uint32_t LocalAddress();
    // What a socket bound to "any address" is bound to: BURNOUT_ONLINE_ADDRESS
    // when set (two copies on one machine each keep the game's fixed ports),
    // otherwise 0 (any).
    uint32_t BindAddress();
}

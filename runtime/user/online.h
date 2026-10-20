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
//                                  ealobby or ours), by name or IPv4; none: 127.0.0.1;
//                                  "lan": the game's own (below)
//   BURNOUT_ONLINE_ADDRESS=<ipv4>  this console's address for its peers, and the
//                                  one its sockets bind to (127.0.0.2 and 127.0.0.3
//                                  for two copies on one Mac); none: the LAN's
//
// Phase 1: with BURNOUT_ONLINE the network is a console's with a working
// connection (kernel/burnout_imports.cpp: link up, an XNADDR, real sockets) and
// XOnlineGetServiceInfo names the lobby server (kernel/xmsg.cpp).
//
// Phase 5, a LAN with no server (BURNOUT_LOBBY_SERVER=lan): each console runs
// a lobby of its own (Xerenge's ealobby, its "seamless multiplayer") at its
// address, announces the games it hosts on UDP 31859 and joins its player to
// the games the others announce, through a link to their lobby. No Locker:
// Burnout Clips need a server. (Xerenge's design, its rexglue/src/ealobby.)
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
    // BURNOUT_LOBBY_SERVER is set (and found): there is a server, and with it
    // EA's Locker (Burnout Clips) on its port 31862. Not for the game's own lobby.
    bool HasLobbyServer();
    // BURNOUT_LOBBY_SERVER=lan: the lobby is the game's own, at LocalAddress()
    // (started by the first LobbyServerAddress()).
    bool OwnLobby();
    // This console's address as its peers see it (its XNADDR).
    uint32_t LocalAddress();
    // What a socket bound to "any address" is bound to: BURNOUT_ONLINE_ADDRESS
    // when set (two copies on one machine each keep the game's fixed ports),
    // otherwise 0 (any).
    uint32_t BindAddress();

    // An XNADDR (36 bytes: ina, inaOnline, wPortOnline, abEnet[6],
    // abOnline[20]) for `address`: both addresses that one, and the two
    // identities made from it - so two copies on one machine (127.0.0.2,
    // 127.0.0.3) don't look like one, and no real MAC leaves the Mac. (As the
    // ReXGlue SDK in Xerenge's fork fills it, shipa-2/rexglue-xerenge, BSD.)
    void FillXnAddr(uint8_t* xnaddr, uint32_t address);
    // XSESSION_INFO for a session this console hosts (60 bytes at `info`: a
    // fresh XNKID, this console's XNADDR, a fresh XNKEY) and a nonce (8 bytes
    // at `nonce`); either may be null.
    void FillHostSessionInfo(uint8_t* info, uint8_t* nonce);
}

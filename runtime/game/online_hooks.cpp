// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// Online, phase 3 (docs/ONLINE_PLAN_RU.md): the places after the lobby login
// where the game waits on EA's other servers, long gone. With BURNOUT_ONLINE
// the lobby itself is real (Xerenge's ealobby or ours, kernel/xmsg.cpp gives
// its address), and these are answered here:
//   - Revenge Rivals: the rivals table lives in EA's Locker. Not downloaded;
//     the post-login manager is told it finished.
//   - The news ticker and the terms of service: fetched over HTTPS from URLs
//     the server's login reply would carry. Not fetched.
//   - EA's Locker (Burnout Clips): the lobby server's own, on its port 31862,
//     given to the game as an address (it can't resolve a name - Xerenge's
//     issue #24). Without a server the replay manager's login is skipped.
//   - Voice: DirtySock's Voip calls given no voice module do nothing (on a
//     console the module always starts and the game never checks). Always,
//     online or not: a null module is never valid there.
// The functions, their offsets and what they mean are Xerenge's findings
// (shipa-2/Xerenge, rexglue/src/online_stub.cpp at c023338,
// GPL-3.0-or-later; adapted with its author's agreement, shipa-2/Xerenge#28),
// checked against our USA build (docs/ONLINE_PLAN_RU.md).
#include <stdafx.h>
#include <user/online.h>

#include <cstdio>
#include <cstring>
#include <string>

namespace
{
    // "a.b.c.d:31862" for the server's Locker, "" without a server.
    std::string LockerHost()
    {
        if (!online::Enabled() || !online::HasLobbyServer())
            return {};
        const uint32_t address = online::LobbyServerAddress();
        const uint8_t* ip = reinterpret_cast<const uint8_t*>(&address);
        char text[32];
        snprintf(text, sizeof(text), "%u.%u.%u.%u:31862", ip[0], ip[1], ip[2], ip[3]);
        return text;
    }

    void LogOnce(bool& logged, const char* text)
    {
        if (!logged)
        {
            logged = true;
            fprintf(stderr, "[online] %s\n", text);
        }
    }
}

// CB4NetworkRevengeRivals::DownloadRivalsTable: nothing is fetched; the
// table stays empty.
extern "C" PPC_FUNC(__imp__sub_8222FB00);
PPC_FUNC(sub_8222FB00)
{
    if (online::Enabled())
    {
        fprintf(stderr, "[online] Revenge Rivals table not downloaded (no EA Locker); left empty\n");
        return;
    }
    __imp__sub_8222FB00(ctx, base);
}

// CB4PostLoginManager::Update. State 3 waits for the rivals download above to
// call back, which it never will: answered for here, as if it had finished
// (CB4PostLoginManager::RivalsDownloadedCallback(ok, self)). Not from
// DownloadRivalsTable itself: its caller sets state 3 after it returns.
extern "C" PPC_FUNC(__imp__sub_82230B70);
PPC_EXTERN_FUNC(sub_8222BFD8);
PPC_FUNC(sub_82230B70)
{
    const uint32_t self = ctx.r3.u32;
    if (online::Enabled() && PPC_LOAD_U32(self) == 3)
    {
        fprintf(stderr, "[online] rivals download reported finished\n");
        ctx.r3.u64 = 1;
        ctx.r4.u64 = self;
        sub_8222BFD8(ctx, base);
        ctx.r3.u64 = self;
    }
    __imp__sub_82230B70(ctx, base);
}

// CB4NetworkingService::PrepareDownloadNews: no news ticker; UpdateNewsTos
// stays idle.
extern "C" PPC_FUNC(__imp__sub_821E9638);
PPC_FUNC(sub_821E9638)
{
    if (online::Enabled())
    {
        static bool logged = false;
        LogOnce(logged, "no news (nothing downloaded)");
        return;
    }
    __imp__sub_821E9638(ctx, base);
}

// CB4NetworkingService::PrepareDownloadAgreement: no terms of service. (Left
// half-done, the download state would point at an HTTP transfer never made.)
extern "C" PPC_FUNC(__imp__sub_821E95A8);
PPC_FUNC(sub_821E95A8)
{
    if (online::Enabled())
    {
        static bool logged = false;
        LogOnce(logged, "no terms of service (nothing downloaded)");
        return;
    }
    __imp__sub_821E95A8(ctx, base);
}

// LockerApiCreate(host, callback, ref): without a host the Locker is EA's
// (its name copied to ref+4, 256 bytes, and put in every request's URL). The
// server's is written there instead.
extern "C" PPC_FUNC(__imp__sub_8240BE58);
PPC_FUNC(sub_8240BE58)
{
    __imp__sub_8240BE58(ctx, base);
    const std::string host = LockerHost();
    const uint32_t ref = ctx.r3.u32;
    if (!host.empty() && ref && host.size() < 255)
    {
        memcpy(base + ref + 4, host.c_str(), host.size() + 1);
        fprintf(stderr, "[online] Locker (Burnout Clips) at %s\n", host.c_str());
    }
}

// The replay manager's login step (Burnout Clips sharing, on the Locker),
// which only sets the post-login manager (r6) to state 5 when done. With a
// server's Locker it runs; without one it is skipped and state 5 is set.
extern "C" PPC_FUNC(__imp__sub_8211F448);
PPC_FUNC(sub_8211F448)
{
    if (online::Enabled() && LockerHost().empty())
    {
        fprintf(stderr, "[online] replay manager login skipped (no Locker server)\n");
        PPC_STORE_U32(ctx.r6.u32, 5);
        return;
    }
    __imp__sub_8211F448(ctx, base);
}

// DirtySock's Voip calls with no voice module (VoipGetRef null): nothing, 0.
// CGtVoIPManagerXenon::Prepare calls VoipControl(null, 'port', ...) when the
// module didn't start, which would read 0x68 off null.
namespace
{
    bool VoipWithoutModule(PPCContext& ctx, const char* name)
    {
        if (ctx.r3.u32 != 0)
            return false;
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            fprintf(stderr, "[online] %s called without a voice module; voice chat is off\n", name);
        }
        ctx.r3.u64 = 0;
        return true;
    }
}

#define VOIP_GUARD(address, name)                                  \
    extern "C" PPC_FUNC(__imp__sub_##address);                     \
    PPC_FUNC(sub_##address)                                        \
    {                                                              \
        if (!VoipWithoutModule(ctx, name))                         \
            __imp__sub_##address(ctx, base);                       \
    }

VOIP_GUARD(825893B0, "VoipSetLocalUser")
VOIP_GUARD(82589448, "VoipConnect2")
VOIP_GUARD(82589578, "VoipDisconnect")
VOIP_GUARD(825895B8, "VoipRemote")
VOIP_GUARD(82589630, "VoipLocal")
VOIP_GUARD(82589658, "VoipStatus2")
VOIP_GUARD(82589708, "VoipControl")

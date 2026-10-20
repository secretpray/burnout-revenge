// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// Online, phase 0 (docs/ONLINE_PLAN_RU.md): with BURNOUT_ONLINE the Xbox Live
// login is logged, to see where it stops - nothing here changes what the game
// does. The login is CB4XenonLoginMenuState, a state machine in +0x140 that
// sub_82217590 (its Action) runs each frame:
//   0 waiting for the menu movie      5 connecting to Live (NetConnStatus 'onln')
//   1 preparing the network manager   6 connecting to the lobby server
//   2 preparing the player manager    7 logging in to the lobby
//   3 preparing the network lobby     8 downloading the terms of service
//   4 gamertag selection (sign-in)    10 fetching configuration
//   11 done, or failed
// The states, the functions and what they mean are Xerenge's findings
// (shipa-2/Xerenge, rexglue/src/online_stub.cpp at c023338,
// GPL-3.0-or-later), checked against our USA build: sub_82217590 reads and
// writes +320 (0x140) there.
#include <stdafx.h>
#include <user/online.h>
#include <hid/script_event.h>

#include <algorithm>
#include <cstdio>
#include <iterator>
#include <string>

namespace
{
    const char* LoginStateName(uint32_t state)
    {
        switch (state)
        {
        case 0: return "waiting for the menu movie";
        case 1: return "preparing the network manager";
        case 2: return "preparing the player manager";
        case 3: return "preparing the network lobby";
        case 4: return "gamertag selection";
        case 5: return "connecting to Live";
        case 6: return "connecting to the lobby server";
        case 7: return "logging in to the lobby";
        case 8: return "downloading the terms of service";
        case 10: return "fetching configuration";
        case 11: return "done / failed";
        default: return "?";
        }
    }

    std::string FourCC(uint32_t v)
    {
        std::string s;
        for (int shift = 24; shift >= 0; shift -= 8)
        {
            const char c = char((v >> shift) & 0xFF);
            s += (c >= 0x20 && c < 0x7F) ? c : '.';
        }
        return s;
    }
}

// CB4XenonLoginMenuState::Action: each change of the login's state.
extern "C" PPC_FUNC(__imp__sub_82217590);
PPC_FUNC(sub_82217590)
{
    if (!online::Enabled())
    {
        __imp__sub_82217590(ctx, base);
        return;
    }
    const uint32_t self = ctx.r3.u32;
    const uint32_t before = PPC_LOAD_U32(self + 0x140);
    __imp__sub_82217590(ctx, base);
    const uint32_t after = PPC_LOAD_U32(self + 0x140);
    if (after != before)
    {
        fprintf(stderr, "[online] login state %u (%s) -> %u (%s)\n", before, LoginStateName(before), after, LoginStateName(after));
        if (after == 11)
            hid::ScriptEvent("online");  // at Xbox Live's main menu: an online bot's script goes on
    }
}

// NetConnStatus(selector, ...): DirtySock's view of the connection ('conn',
// 'onln', ...), logged when what it answers for a selector changes.
extern "C" PPC_FUNC(__imp__sub_8240A8F0);
PPC_FUNC(sub_8240A8F0)
{
    const uint32_t selector = ctx.r3.u32;
    __imp__sub_8240A8F0(ctx, base);
    if (!online::Enabled())
        return;
    static uint32_t selectors[16] = {};
    static uint32_t answers[16] = {};
    for (int i = 0; i < 16; i++)
    {
        if (selectors[i] != selector && selectors[i] != 0)
            continue;
        if (selectors[i] == 0 || answers[i] != ctx.r3.u32)
        {
            selectors[i] = selector;
            answers[i] = ctx.r3.u32;
            fprintf(stderr, "[online] NetConnStatus('%s') = %08X ('%s')\n", FourCC(selector).c_str(), ctx.r3.u32,
                FourCC(ctx.r3.u32).c_str());
        }
        break;
    }
}

// CB4NetworkManager::ShowNetworkError(this, error, ...): which check gave up
// on the connection, with the code that called it.
extern "C" PPC_FUNC(__imp__sub_82224DD0);
PPC_FUNC(sub_82224DD0)
{
    if (online::Enabled())
        fprintf(stderr, "[online] network error %d shown, from %08X\n", int32_t(ctx.r4.u32), uint32_t(ctx.lr));
    __imp__sub_82224DD0(ctx, base);
}

// CommUDP, DirtySock's peer link on port 8192 (one per other player):
// its connect ("address:port:port") and listen, with the object, the result
// and who called. Which player's link a console never starts shows here (the fourth player of a Crash Party,
// 20.10). The functions and offsets are Xerenge's (online_stub.cpp at
// c023338), checked against our build.
namespace
{
    std::string GuestText(uint8_t* base, uint32_t address)
    {
        std::string s;
        for (uint32_t i = 0; address && i < 64; i++)
        {
            const char c = char(base[address + i]);
            if (!c)
                break;
            s += (c >= 0x20 && c < 0x7F) ? c : '?';
        }
        return s;
    }
}

extern "C" PPC_FUNC(__imp__sub_825BF2B8);
PPC_FUNC(sub_825BF2B8)
{
    const uint32_t self = ctx.r3.u32, lr = uint32_t(ctx.lr);
    const std::string text = GuestText(base, ctx.r4.u32);
    __imp__sub_825BF2B8(ctx, base);
    if (online::Enabled())
        fprintf(stderr, "[online] CommUDP %08X connect \"%s\" -> %d (from %08X)\n", self, text.c_str(), int32_t(ctx.r3.u32), lr);
}

extern "C" PPC_FUNC(__imp__sub_825BF3E8);
PPC_FUNC(sub_825BF3E8)
{
    const uint32_t self = ctx.r3.u32, lr = uint32_t(ctx.lr);
    const std::string text = GuestText(base, ctx.r4.u32);
    __imp__sub_825BF3E8(ctx, base);
    if (online::Enabled())
        fprintf(stderr, "[online] CommUDP %08X listen \"%s\" -> %d (from %08X)\n", self, text.c_str(), int32_t(ctx.r3.u32), lr);
}

// CommUDP's update, all links each frame (the list at 0x82D3E6E8, next at
// +0x7C): each link's state (+0x94; 3 waits to be called), DirtySock socket
// (+0x80) and connection id (+0x98), logged when they change. A waiting link
// takes a call only from its own socket, with its id.
extern "C" PPC_FUNC(__imp__sub_825BE2F0);
PPC_FUNC(sub_825BE2F0)
{
    __imp__sub_825BE2F0(ctx, base);
    if (!online::Enabled())
        return;
    struct Seen
    {
        uint32_t link, state, socket, id;
    };
    static Seen seen[16] = {};
    uint32_t link = PPC_LOAD_U32(0x82D3E6E8);
    for (int n = 0; link && n < 16; n++, link = PPC_LOAD_U32(link + 0x7C))
    {
        const uint32_t state = PPC_LOAD_U32(link + 0x94), socket = PPC_LOAD_U32(link + 0x80), id = PPC_LOAD_U32(link + 0x98);
        Seen* slot = std::find_if(std::begin(seen), std::end(seen), [&](const Seen& s) { return s.link == link; });
        if (slot == std::end(seen))
            slot = std::find_if(std::begin(seen), std::end(seen), [](const Seen& s) { return s.link == 0; });
        if (slot == std::end(seen))
            break;
        if (slot->link != link || slot->state != state || slot->socket != socket || slot->id != id)
        {
            *slot = { link, state, socket, id };
            fprintf(stderr, "[online] CommUDP %08X state %d, socket %08X, id %08X\n", link, int32_t(state), socket, id);
        }
    }
}

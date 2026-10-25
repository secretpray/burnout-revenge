// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include "codes.h"

namespace game::crash_launch
{
    inline constexpr uint32_t kSoloEvent = 0x828465E0 + 0x36D20;
    inline constexpr uint32_t kNetworkSession = 0x8287C718;
    constexpr bool KnownNetworkEvent(uint32_t event)
    {
        return event == 0x82888190 || event == 0x82889780 || event == 0x8288AC70;
    }
    inline bool NetworkEvent(uint8_t* base, uint32_t event)
    {
        return KnownNetworkEvent(event) && PPC_LOAD_U32(codes::kSession) == kNetworkSession &&
            PPC_LOAD_U32(kNetworkSession + 4) == event && PPC_LOAD_U32(codes::kLocalPlayers) == 1;
    }

    // USA World Tour Crash owns this embedded event through the solo wrapper.
    // Signing in to Live does not make a World Tour event multiplayer. Reject
    // all other wrappers before reading an event pointer from any of them.
    inline bool SoloEvent(uint8_t* base)
    {
        return PPC_LOAD_U32(codes::kSession) == codes::kSoloSession &&
            PPC_LOAD_U32(codes::kSoloSession + 4) == kSoloEvent &&
            PPC_LOAD_U32(codes::kLocalPlayers) == 1;
    }
}

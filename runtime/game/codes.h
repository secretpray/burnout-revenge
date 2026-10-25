// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace game::codes
{
    // USA executable only. Embedded session wrappers from sub_8211CAC0;
    // World Tour builders return +0x36208, local split-screen +0x3621C.
    constexpr uint32_t kFrontendSession = 0x8287C698;
    constexpr uint32_t kSoloSession = 0x8287C6E8;
    constexpr uint32_t kSplitSession = 0x8287C6FC;
    constexpr uint32_t kSession = 0x828BA538;
    constexpr uint32_t kLocalPlayers = 0x82CE000C;
    constexpr uint32_t kSpecialMode = 0x828BA56F;
    // sub_82104148 (also used by the game's combined multiplayer predicate
    // sub_821041D8): the turn-based Crash controllers and their variant.
    constexpr uint32_t kPartyController = 0x828A3470;
    constexpr uint32_t kCrashController = 0x828AE948;
    constexpr uint32_t kCrashVariant = 0x828B9E14;

    struct Context
    {
        bool enabled{}, online{};
        uint32_t session{}, controller{}, localPlayers{}, specialMode{}, crashVariant{}, driver{}, player{};
    };
    constexpr bool Allowed(const Context& c)
    {
        return c.enabled && !c.online && c.session == kSoloSession &&
            c.controller != 0 && c.controller != kPartyController && c.controller != kCrashController &&
            c.localPlayers == 1 && c.specialMode == 0 && c.driver == 0 && c.player == 0;
    }
}

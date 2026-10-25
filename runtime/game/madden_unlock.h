// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
struct PPCContext;
namespace game::madden
{
    enum class Status : uint8_t { Unavailable, Ready, Pending, Unlocked };
    Status GetStatus();
    void Request();
    // Runs on the guest main thread; the UI never touches career memory.
    void AtBoundary(PPCContext& ctx, uint8_t* base);
}

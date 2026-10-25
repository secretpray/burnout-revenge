// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace game::crash_pacing
{
    // Developer diagnostics only. No guest writes, clock changes or input injection.
    // BURNOUT_CRASH_PACING_LOG=<csv>; the caller owns a live guest boundary.
    void Sample(const char* kind, uint8_t* base, uint32_t object = 0, uint32_t caller = 0);
}

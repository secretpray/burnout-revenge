// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
struct PPCContext;
namespace game::rewind::ring
{
    // Developer experiment, after the original native recorder update.
    void AtBoundary(PPCContext& ctx, uint8_t* base, uint32_t recorder);
}

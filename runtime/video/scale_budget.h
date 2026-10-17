// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Internal Resolution's Auto on iPhones and iPads: how many host pixels per
// guest pixel (sx x sy, video::AutoRenderScale's budget) the GPU gets, chip
// by chip. A pure function of the GPU's name, for tests/scale_budget_test.cpp.
//
// Only chips measured to hold a race at 60 fps with headroom, and to stay out
// of serious thermal throttling over a long session, get more than 1x1. Any
// other chip, a newer one included, keeps 1x1 until it is measured: a phone
// that renders too much heats up and throttles to 30 fps mid-session, which
// is worse than a softer picture. None has qualified yet: the A20 Pro (iPhone
// 18 Pro Max) reaches the serious thermal state at 1x1 already (on a
// charger), so it keeps 1x1 too, and Settings > Graphics > Internal
// Resolution > 2x stays the player's choice.
#pragma once
#include <cstdint>
#include <string_view>

namespace video
{
    struct ScaleBudget
    {
        // At most this many host pixels per guest pixel (1: 1x1, 4: 2x2).
        uint32_t pixels = 1;
        // Why, for the log ("[renderer] Auto internal resolution: ...").
        const char* reason = "";
    };

    // The budget for the GPU that MoltenVK names `gpuName` (Metal's device
    // name, "Apple A20 Pro GPU", "Apple M2 GPU").
    ScaleBudget MobileScaleBudget(std::string_view gpuName);
}

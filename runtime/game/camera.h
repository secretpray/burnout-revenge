// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace game::camera
{
    // USA primary driving camera. Other views retain their native FOV.
    inline constexpr uint32_t kPrimary = 0x82923220;
    inline constexpr uint32_t kFrame = 0x828743F0;
    inline constexpr uint32_t kParticipant = 0x82CE0030;
    inline constexpr uint32_t kManager = kFrame - 2736;
    inline constexpr uint32_t kChase = kManager + 5184;

    // Multiplier on both native view-window axes, before Hor+/Vert+.
    // Leaves the native FOV field and saved camera controller untouched.
    float ViewWindowScale(uint8_t* base, uint32_t camera, uint32_t caller, double nativeFov);
}

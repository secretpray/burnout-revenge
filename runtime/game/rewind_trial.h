// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace game::rewind::trial
{
    // Developer-only, isolated-profile experiment. A successful native capture
    // calls this while its header and payload still belong to the game.
    void Captured(uint8_t* base, uint32_t header, uint32_t payload, uint32_t capacity);
}

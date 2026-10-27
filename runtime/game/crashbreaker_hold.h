// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cmath>

namespace game::crashbreaker
{
    // Charge input only. Native world time freezes with pause. There is no
    // wall-clock timer, queued input or catch-up work after a slow frame.
    struct Hold
    {
        bool active = false;
        float origin = 0, lastWorld = 0, chargeStart = 0;

        void Reset() { *this = {}; }
        bool Down(bool held, float world, float start)
        {
            if (!held || !std::isfinite(world) || !std::isfinite(start) || world < start)
            { Reset(); return false; }
            if (!active || world < lastWorld || start != chargeStart)
                origin = world; // first press, a rewind, or a new charge
            active = true;
            lastWorld = world;
            chargeStart = start;
            // Eight presses per world second, with equal down/up intervals.
            // Re-reading one world tick returns the same button level.
            return std::fmod(double(world) - double(origin), .125) < .0625;
        }
    };
}

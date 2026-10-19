// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Settings > Graphics > Vehicle Detail: the distances at which the game
// swaps a car for a simpler model, times a factor. The graphics manager
// (0x82923170) holds two sets of three, copied from a table in .rdata
// (0x8201BF88: 30, 60, 90 m) by sub_82142C88 at its set-up:
//   +0x68..+0x70  race cars: sub_820BD008 compares the squared distance from
//                 the camera with (k * d)^2 (k the camera's factor, +144 of
//                 the camera at manager+3204): within 30 m level 3 (the full
//                 model), to 60 m level 2, to 90 m level 1, beyond level 0.
//                 It also fades between the 30 and 60 m models.
//   +0x74..+0x7C  traffic: sub_82339020 squares them once per call and
//                 compares each vehicle the same way.
// Only the levels change: which cars are drawn, the traffic's draw distance
// and its lists (32 and 64 entries, bounded by their counts) stay the
// game's. The idea is Takedawn's (Burnout 3's CB3GraphicsManagerBase::
// SetDefaultLODSwitchDistances, its graphics.vehicle_detail) and Fukami's
// (Ridge Racer V's car_lod); the Revenge sites and this code are ours.
#pragma once

#include <array>
#include <cstdint>

namespace game::vehicle_detail
{
    constexpr uint32_t kManager = 0x82923170;
    constexpr uint32_t kFields = kManager + 0x68;  // 6 floats: race cars, then traffic
    constexpr uint32_t kStockTable = 0x8201BF88;   // 3 floats, read-only
    constexpr int kFieldCount = 6;

    // The setting's values: the factor itself.
    constexpr int kOriginal = 1;
    constexpr int kFar = 2;
    constexpr int kFarthest = 4;

    constexpr int Factor(int setting)
    {
        return setting == kFar || setting == kFarthest ? setting : kOriginal;
    }

    using Distances = std::array<float, kFieldCount>;

    constexpr Distances Wanted(const std::array<float, 3>& stock, int factor)
    {
        Distances d{};
        for (int i = 0; i < kFieldCount; i++)
            d[i] = stock[i % 3] * float(factor);
        return d;
    }

    // What a call finds in the manager against what it last wrote there:
    // Keep (already the wanted values), Write (ours or the game's own
    // stock values, to be set to the wanted ones) or Foreign (a value
    // neither we nor the set-up wrote: someone else uses the fields; still
    // written, and logged once).
    enum class Found : uint8_t { Keep, Write, Foreign };

    constexpr Found Check(const Distances& current, const Distances& wanted, const Distances& written,
                          const std::array<float, 3>& stock)
    {
        Found result = Found::Keep;
        for (int i = 0; i < kFieldCount; i++)
        {
            if (current[i] == wanted[i])
                continue;
            if (current[i] != written[i] && current[i] != stock[i % 3])
                return Found::Foreign;
            result = Found::Write;
        }
        return result;
    }
}

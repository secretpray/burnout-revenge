// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Settings > Graphics > Vehicle Detail (game/vehicle_detail.h): the
// manager's six switch distances, rewritten when they aren't what the
// setting asks. Checked where the game reads them, so a change applies from
// the next frame.
#include <stdafx.h>
#include <user/settings.h>

#include "vehicle_detail.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cstdlib>

namespace
{
    using namespace game::vehicle_detail;

    float LoadF32(uint8_t* base, uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); }
    void StoreF32(uint8_t* base, uint32_t address, float v) { PPC_STORE_U32(address, std::bit_cast<uint32_t>(v)); }

    std::array<float, 3> Stock(uint8_t* base)
    {
        std::array<float, 3> s{};
        for (int i = 0; i < 3; i++)
            s[i] = LoadF32(base, kStockTable + 4 * i);
        return s;
    }

    // Called on the guest's draw thread (the race cars' level and the
    // set-up); the traffic reads the fields on the same frame's draw.
    void Apply(uint8_t* base)
    {
        static Distances written{};
        static int loggedFactor = 0;
        static bool loggedForeign = false;

        const std::array<float, 3> stock = Stock(base);
        const int factor = Factor(settings::GetInt(settings::Id::VehicleDetail));
        const Distances wanted = Wanted(stock, factor);
        Distances current{};
        for (int i = 0; i < kFieldCount; i++)
            current[i] = LoadF32(base, kFields + 4 * i);

        const Found found = Check(current, wanted, written, stock);
        if (found == Found::Keep)
            return;
        if (found == Found::Foreign && !loggedForeign)
        {
            loggedForeign = true;
            fprintf(stderr, "[vehicle detail] the switch distances were changed by someone else: %g %g %g / %g %g %g\n",
                current[0], current[1], current[2], current[3], current[4], current[5]);
        }
        for (int i = 0; i < kFieldCount; i++)
            StoreF32(base, kFields + 4 * i, wanted[i]);
        written = wanted;
        if (factor != loggedFactor)
        {
            loggedFactor = factor;
            fprintf(stderr, "[vehicle detail] x%d: cars and traffic at full detail within %g m, simplest beyond %g m\n",
                factor, wanted[0], wanted[2]);
        }
    }
}

// The manager's set-up: copies the stock table into both sets.
extern "C" PPC_FUNC(__imp__sub_82142C88);
PPC_FUNC(sub_82142C88)
{
    const uint32_t manager = ctx.r3.u32;
    __imp__sub_82142C88(ctx, base);
    if (manager == kManager)
        Apply(base);
}

// A race car's level of detail (r3: 0 simplest .. 3 full).
// BURNOUT_LOG_VEHICLE_DETAIL=1: how many times each level was chosen, every
// 5 s (one count per car and draw).
extern "C" PPC_FUNC(__imp__sub_820BD008);
PPC_FUNC(sub_820BD008)
{
    Apply(base);
    __imp__sub_820BD008(ctx, base);

    static const bool log = [] {
        const char* v = std::getenv("BURNOUT_LOG_VEHICLE_DETAIL");
        return v && *v && *v != '0';
    }();
    if (!log)
        return;
    static uint32_t counts[4] = {};
    static auto since = std::chrono::steady_clock::now();
    counts[std::min<uint32_t>(ctx.r3.u32, 3)]++;
    const auto now = std::chrono::steady_clock::now();
    if (now - since >= std::chrono::seconds(5))
    {
        fprintf(stderr, "[vehicle detail] levels 3/2/1/0: %u %u %u %u\n", counts[3], counts[2], counts[1], counts[0]);
        counts[0] = counts[1] = counts[2] = counts[3] = 0;
        since = now;
    }
}

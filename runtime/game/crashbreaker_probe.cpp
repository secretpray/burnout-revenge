// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Read-only developer evidence for the native Crashbreaker charge mechanic.
#include <stdafx.h>
#include "crash_launch_scope.h"
#include "rewind_probe.h"
#include <report/report.h>

namespace
{
    FILE* Output()
    {
        static FILE* file = [] {
            const char* path = std::getenv("BURNOUT_CRASHBREAKER_LOG");
            FILE* f = path && *path ? std::fopen(path, "w") : nullptr;
            if (f) std::fputs("kind,session_s,world_s,event_state,driver_state,clock_flags,playback,charging,charge_start,maximum,charge,total,presses,previous_down,input_flags,minimum_presses,caller\n", f);
            return f;
        }();
        return file;
    }

    bool Scope(uint8_t* base)
    {
        return game::crash_launch::SoloEvent(base) &&
            PPC_LOAD_U32(game::rewind::kPhase) == 5;
    }

    void Sample(FILE* file, const char* kind, uint8_t* base, int flags, uint32_t caller)
    {
        // Fixed, verified USA embedded records only. Do not walk car pointers.
        constexpr uint32_t event = game::crash_launch::kSoloEvent;
        constexpr uint32_t burn = 0x82CE0030 + 13280;
        static std::mutex mutex;
        std::lock_guard lock(mutex);
        static uint32_t rows = 0;
        if (rows >= 30000) return;
        ++rows;
        auto f = [base](uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); };
        std::fprintf(file, "%s,%.9f,%.9g,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%u,%u,%d,%.9g,%u\n",
            kind, report::SessionSeconds(), f(game::rewind::kWorldClock),
            PPC_LOAD_U32(event + 2384), PPC_LOAD_U32(event + 2396),
            PPC_LOAD_U32(game::rewind::kClockFlags), PPC_LOAD_U8(game::rewind::kWorldPlayback),
            PPC_LOAD_U8(burn + 195), f(burn + 124), f(burn + 92), f(burn + 96), f(burn + 100),
            PPC_LOAD_U32(event + 2820), PPC_LOAD_U8(event + 2894), flags,
            f(0x820176B0), caller);
        // Bounded native runs may use _Exit: keep at most one second of tail.
        if (rows % 120 == 0) std::fflush(file);
    }
}

extern "C" PPC_FUNC(__imp__sub_821325E8);
PPC_FUNC(sub_821325E8)
{
    FILE* file = Output();
    const uint32_t caller = uint32_t(ctx.lr);
    const bool observe = file && ctx.r3.u32 == game::crash_launch::kSoloEvent &&
        ctx.r4.u32 == 0 && Scope(base);
    if (observe) Sample(file, "charge_pre", base, -1, caller);
    __imp__sub_821325E8(ctx, base);
    if (observe) Sample(file, "charge_post", base, -1, caller);
}

// This native controls producer writes bit 0 of car+6536 from action 17.
// Inspect the receiver only after its original has used it successfully.
extern "C" PPC_FUNC(__imp__sub_82277A90);
PPC_FUNC(sub_82277A90)
{
    FILE* file = Output();
    const uint32_t self = ctx.r3.u32, caller = uint32_t(ctx.lr);
    __imp__sub_82277A90(ctx, base);
    if (file && Scope(base) && self && self == PPC_LOAD_U32(0x82CE0030 + 18240))
        Sample(file, "controls", base, PPC_LOAD_U8(self + 6536), caller);
}

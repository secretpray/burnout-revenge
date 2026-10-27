// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Native Crashbreaker charge input assistance and optional developer evidence.
#include <stdafx.h>
#include "crash_launch_scope.h"
#include "rewind_probe.h"
#include "crashbreaker_hold.h"
#include <report/report.h>
#include <user/settings.h>

namespace
{
    constexpr uint32_t kEvent = game::crash_launch::kSoloEvent;
    constexpr uint32_t kParticipant = 0x82CE0030;
    // Embedded player-zero physical record: world + (10 << 16) - 7344.
    // Do not follow an unrecognized streamed/foreign car pointer.
    constexpr uint32_t kCar = 0x82B552D0;
    thread_local game::crashbreaker::Hold s_hold;

    thread_local bool s_assistControls = false;
    struct ControlsScope
    {
        bool previous = s_assistControls;
        explicit ControlsScope(bool active) { s_assistControls = active; }
        ~ControlsScope() { s_assistControls = previous; }
    };

    bool CanAssist(uint8_t* base)
    {
        if (!game::crash_launch::SoloEvent(base)) return false;
        const uint32_t controller = PPC_LOAD_U32(game::codes::kSoloSession + 8);
        return controller && controller != game::codes::kPartyController &&
            controller != game::codes::kCrashController &&
            PPC_LOAD_U32(game::rewind::kPhase) == 5 &&
            game::rewind::IsLiveClock(PPC_LOAD_U32(game::rewind::kClockFlags)) &&
            !PPC_LOAD_U8(game::rewind::kWorldPlayback) &&
            PPC_LOAD_U32(kEvent + 2384) == 18 && PPC_LOAD_U32(kEvent + 2396) == 7 &&
            PPC_LOAD_U32(kParticipant + 18240) == kCar &&
            PPC_LOAD_U8(kParticipant + 13280 + 195) == 1;
    }

    FILE* Output()
    {
        static FILE* file = [] {
            const char* path = std::getenv("BURNOUT_CRASHBREAKER_LOG");
            FILE* f = path && *path ? std::fopen(path, "w") : nullptr;
            if (f) std::fputs("kind,session_s,world_s,event_state,driver_state,clock_flags,playback,charging,charge_start,maximum,charge,total,presses,previous_down,input_flags,minimum_presses,caller,strength\n", f);
            return f;
        }();
        return file;
    }

    bool Scope(uint8_t* base)
    {
        return game::crash_launch::SoloEvent(base) &&
            PPC_LOAD_U32(game::rewind::kPhase) == 5;
    }

    void Sample(FILE* file, const char* kind, uint8_t* base, int flags, uint32_t caller, double strength = -1)
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
        std::fprintf(file, "%s,%.9f,%.9g,%u,%u,%u,%u,%u,%.9g,%.9g,%.9g,%.9g,%u,%u,%d,%.9g,%u,%.9g\n",
            kind, report::SessionSeconds(), f(game::rewind::kWorldClock),
            PPC_LOAD_U32(event + 2384), PPC_LOAD_U32(event + 2396),
            PPC_LOAD_U32(game::rewind::kClockFlags), PPC_LOAD_U8(game::rewind::kWorldPlayback),
            PPC_LOAD_U8(burn + 195), f(burn + 124), f(burn + 92), f(burn + 96), f(burn + 100),
            PPC_LOAD_U32(event + 2820), PPC_LOAD_U8(event + 2894), flags,
            f(0x820176B0), caller, strength);
        // Bounded native runs may use _Exit: keep at most one second of tail.
        if (rows % 120 == 0) std::fflush(file);
    }
}

extern "C" PPC_FUNC(__imp__sub_8225E778);
PPC_FUNC(sub_8225E778)
{
    FILE* file = Output();
    if (file && ctx.r3.u32 == kCar && Scope(base))
        Sample(file, "burst", base, -1, uint32_t(ctx.lr), ctx.f1.f64);
    __imp__sub_8225E778(ctx, base);
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
    const bool assist = settings::GetBool(settings::Id::HoldCrashbreaker) &&
        self == kCar && CanAssist(base);
    if (!assist) s_hold.Reset();
    {
        ControlsScope scope(assist);
        __imp__sub_82277A90(ctx, base);
    }
    if (file && Scope(base) && self && self == PPC_LOAD_U32(0x82CE0030 + 18240))
        Sample(file, "controls", base, PPC_LOAD_U8(self + 6536), caller);
}

// Native action 17, queried by the scoped Crash controls producer above.
// Filter before the producer packs/records its input. Replay then consumes the
// same ordinary button levels; XInput and menu controls are never rewritten.
extern "C" PPC_FUNC(__imp__sub_8210BB70);
PPC_FUNC(sub_8210BB70)
{
    __imp__sub_8210BB70(ctx, base);
    if (!s_assistControls) return;
    const float world = std::bit_cast<float>(PPC_LOAD_U32(game::rewind::kWorldClock));
    const float start = std::bit_cast<float>(PPC_LOAD_U32(kParticipant + 13280 + 124));
    if (!std::isfinite(world) || !std::isfinite(start) || world < start)
    { s_hold.Reset(); return; }
    // Use the producer's own analog threshold, including remapped controls.
    const float threshold = std::bit_cast<float>(PPC_LOAD_U32(0x8200ED80));
    const bool held = ctx.f1.f64 > threshold;
    if (!held) { s_hold.Reset(); return; }
    if (!s_hold.Down(true, world, start)) ctx.f1.f64 = 0;
}

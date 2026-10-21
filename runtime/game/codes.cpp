// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Built-in solo Codes, guarded at every original game update or award.
#include <stdafx.h>
#include <user/online.h>
#include <bit>
#include <chrono>
#include <cstdlib>
#include <cmath>

#include <user/settings.h>
#include "codes.h"

namespace
{
    using namespace game::codes;
    bool LogCodes()
    {
        static const bool log = [] { const char* v = std::getenv("BURNOUT_LOG_CODES"); return v && *v && *v != '0'; }();
        return log;
    }
    Context ReadContext(uint8_t* base, bool enabled, uint32_t car = 0)
    {
        const uint32_t session = PPC_LOAD_U32(kSession);
        return {enabled, online::Enabled(), session,
            session == kSoloSession ? PPC_LOAD_U32(session + 8) : 0, PPC_LOAD_U32(kLocalPlayers),
            PPC_LOAD_U8(kSpecialMode), PPC_LOAD_U32(kCrashVariant),
            car ? PPC_LOAD_U32(car + 15328) : 0, car ? PPC_LOAD_U32(car + 19168) : 0};
    }
    struct LevelOverride { uint32_t self{}, level{}; };
    thread_local LevelOverride levelOverride;
    float Float(uint8_t* base, uint32_t address) { return std::bit_cast<float>(PPC_LOAD_U32(address)); }
    void SetFloat(uint8_t* base, uint32_t address, float value) { PPC_STORE_U32(address, std::bit_cast<uint32_t>(value)); }
    bool SetLevel(uint8_t* base, uint32_t self, uint32_t level)
    {
        if (level > 3) return false;
        const float cap = Float(base, 0x825E1F3C + level * 4);
        const float cost = Float(base, 0x825E1F5C + level * 4);
        const float earn = Float(base, 0x825E1F4C + level * 4);
        const float recovery = Float(base, 0x825E1F30) * cost;
        if (!std::isfinite(cap) || cap <= 0 || cap > 10000 || !std::isfinite(cost) || cost < 0 ||
            !std::isfinite(earn) || earn <= 0 || !std::isfinite(recovery)) return false;
        // Fields written by the native level-up and level-down routines.
        PPC_STORE_U32(self + 88, level);
        SetFloat(base, self + 92, cap); SetFloat(base, self + 104, cost);
        SetFloat(base, self + 108, recovery); SetFloat(base, self + 112, earn);
        if (Float(base, self + 96) > cap) SetFloat(base, self + 96, cap);
        return true;
    }
    // Restore tunables even if the original call throws. Changes are scoped
    // to one guarded player's original update, never a whole frame of AI.
    struct FloatOverride
    {
        uint8_t* base; uint32_t address, saved;
        FloatOverride(uint8_t* b, uint32_t a, float value) : base(b), address(a), saved(PPC_LOAD_U32(a)) { SetFloat(base, a, value); }
        ~FloatOverride() { PPC_STORE_U32(address, saved); }
    };
}

extern "C" PPC_FUNC(__imp__sub_820C56F0);
PPC_FUNC(sub_820C56F0)
{
    const uint32_t self = ctx.r3.u32;
    if (levelOverride.self == self) levelOverride = {};
    __imp__sub_820C56F0(ctx, base);
}

extern "C" PPC_FUNC(__imp__sub_820DA9D8);
PPC_FUNC(sub_820DA9D8)
{
    using namespace game::codes;
    const uint32_t self = ctx.r3.u32;
    const bool enabled = settings::GetBool(settings::Id::InfiniteBoost);
    static const bool log = [] {
        const char* v = std::getenv("BURNOUT_LOG_CODES");
        return v && *v && *v != '0';
    }();
    if (!enabled && !log && levelOverride.self != self)
    {
        __imp__sub_820DA9D8(ctx, base);
        return;
    }
    const uint32_t car = PPC_LOAD_U32(self + 180);
    const Context context = ReadContext(base, enabled, car);
    const float originalCapacity = Float(base, self + 92);
    bool apply = Allowed(context) && std::isfinite(originalCapacity) && originalCapacity > 0 && originalCapacity <= 10000;
    if (apply)
    {
        const uint32_t level = PPC_LOAD_U32(self + 88);
        apply = level <= 3 && SetLevel(base, self, 3);
        if (apply && levelOverride.self != self) levelOverride = {self, level};
    }
    else if (levelOverride.self == self)
    {
        SetLevel(base, self, levelOverride.level);
        levelOverride = {};
    }
    const float capacity = Float(base, self + 92);
    const uint8_t stock = PPC_LOAD_U8(self + 184);
    struct RestoreFlag
    {
        uint8_t* base; uint32_t address; uint8_t value; bool active;
        ~RestoreFlag() { if (active) PPC_STORE_U8(address, value); }
    } restore{base, self + 184, stock, apply};
    if (apply)
    {
        // The game's own infinite-boost branch. Never leave its flag set:
        // the next mode or a live setting change must see the stock flag.
        PPC_STORE_U8(self + 184, 1);
        PPC_STORE_U32(self + 96, std::bit_cast<uint32_t>(capacity));
    }
    __imp__sub_820DA9D8(ctx, base);
    if (apply) { PPC_STORE_U8(self + 184, stock); restore.active = false; }
    if (log)
    {
        struct Seen { uint32_t self{}, boostingFrames{}; std::chrono::steady_clock::time_point time{}; };
        static thread_local Seen seen[16];
        for (auto& entry : seen)
        {
            if (entry.self && entry.self != self) continue;
            entry.boostingFrames += apply && PPC_LOAD_U8(self + 186) != 0;
            const auto now = std::chrono::steady_clock::now();
            if (entry.self == 0 || now - entry.time >= std::chrono::seconds(5))
            {
                const uint32_t boostingFrames = entry.boostingFrames;
                entry = {self, 0, now};
                fprintf(stderr, "[codes] boost=%08X car=%08X apply=%u driver=%u player=%u locals=%u special=%u online=%u session=%08X controller=%08X variant=%u level=%u stock=%u active=%u boosting_frames=%u boost=%g/%g levels=%g,%g,%g,%g\n",
                    self, car, apply, context.driver, context.player, context.localPlayers,
                    context.specialMode, context.online, context.session, context.controller, context.crashVariant, PPC_LOAD_U32(self + 88) + 1,
                    PPC_LOAD_U8(self + 184), PPC_LOAD_U8(self + 186), boostingFrames,
                    std::bit_cast<float>(PPC_LOAD_U32(self + 96)), capacity,
                    std::bit_cast<float>(PPC_LOAD_U32(0x825E1F3C)), std::bit_cast<float>(PPC_LOAD_U32(0x825E1F40)),
                    std::bit_cast<float>(PPC_LOAD_U32(0x825E1F44)), std::bit_cast<float>(PPC_LOAD_U32(0x825E1F48)));
            }
            break;
        }
    }
}




// The event's own deadline, not the shared simulation clock. Querying it
// shifts the deadline by elapsed game time; live Off resumes from that time.
extern "C" PPC_FUNC(__imp__sub_8212DE50);
PPC_FUNC(sub_8212DE50)
{
    const uint32_t self = ctx.r3.u32;
    const bool apply = game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::InfiniteTime))) &&
        PPC_LOAD_U32(game::codes::kSoloSession + 4) == self && PPC_LOAD_U8(self + 3132) != 0;
    struct Clock { uint32_t self{}; float now{}; };
    static thread_local Clock clock;
    const float now = Float(base, 0x82AB6FA4);
    const float deadline = Float(base, self + 3056);
    if (apply && std::isfinite(now) && std::isfinite(deadline) && deadline > now)
    {
        if (clock.self == self && now >= clock.now && now - clock.now < 1.f)
            SetFloat(base, self + 3056, deadline + (now - clock.now));
        clock = {self, now};
    }
    else clock = {};
    __imp__sub_8212DE50(ctx, base);
    if (LogCodes())
    {
        static thread_local std::chrono::steady_clock::time_point last;
        const auto stamp = std::chrono::steady_clock::now();
        if (stamp - last >= std::chrono::seconds(5))
        {
            last = stamp;
            fprintf(stderr, "[codes] time apply=%u event=%08X active=%u now=%g remaining=%g\n", apply, self, PPC_LOAD_U8(self + 3132), now, ctx.f1.f64);
        }
    }
}
extern "C" PPC_FUNC(__imp__sub_82324D58);
PPC_FUNC(sub_82324D58)
{
    const uint32_t self = ctx.r3.u32, player = ctx.r4.u32, amount = ctx.r5.u32, multiplier = ctx.r6.u32;
    const bool apply = player == 0 && amount > 0 && amount < 1000000 && multiplier > 0 && multiplier <= 1000 &&
        game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::QuickMoney)));
    if (apply) ctx.r5.u64 = std::max(amount, (30000u + multiplier - 1) / multiplier);
    __imp__sub_82324D58(ctx, base);
    if (LogCodes()) fprintf(stderr, "[codes] money apply=%u self=%08X player=%u amount=%u multiplier=%u total=%u\n", apply, self, player, amount, multiplier,
        player < 4 ? PPC_LOAD_U32(self + 16 + player * 512 + 452) : 0);
}
// Per-player event statistics (stride 0xC8): +0x70 is the takedown total on
// the results screen, +0x9C the count the event goal, medal and HUD read.
static uint32_t EventStats(uint8_t* base, uint32_t car) { return 0x8276FB10 + PPC_LOAD_U32(car + 19168) * 0xC8; }
extern "C" PPC_FUNC(__imp__sub_8212F168);
PPC_FUNC(sub_8212F168)
{
    const uint32_t car = ctx.r5.u32;
    const bool apply = game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::QuickTakedowns), car));
    const uint32_t before = apply ? PPC_LOAD_U32(0x8276FB80) : 0;
    __imp__sub_8212F168(ctx, base);
    if (apply)
    {
        const uint32_t after = PPC_LOAD_U32(0x8276FB80);
        if (after > before && after < 99)
        {
            PPC_STORE_U32(0x8276FB80, 99);
            if (LogCodes()) fprintf(stderr, "[codes] takedowns apply=1 before=%u native=%u total=99\n", before, after);
        }
    }
}
// The mode's own takedown handlers (vtable; the two event types counting
// takedowns toward a goal). Only a takedown the game itself counted is raised.
static void EventTakedown(PPCContext& ctx, uint8_t* base, void (*original)(PPCContext&, uint8_t*))
{
    const uint32_t car = ctx.r5.u32;
    const bool apply = game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::QuickTakedowns), car));
    const uint32_t counter = apply ? EventStats(base, car) + 0x9C : 0;
    const uint32_t before = apply ? PPC_LOAD_U32(counter) : 0;
    original(ctx, base);
    if (apply)
    {
        const uint32_t after = PPC_LOAD_U32(counter);
        if (after > before && after < 99) PPC_STORE_U32(counter, 99);
        if (LogCodes()) fprintf(stderr, "[codes] event_takedowns apply=1 before=%u native=%u total=%u\n", before, after, PPC_LOAD_U32(counter));
    }
}
extern "C" PPC_FUNC(__imp__sub_82134B80);
PPC_FUNC(sub_82134B80) { EventTakedown(ctx, base, __imp__sub_82134B80); }
extern "C" PPC_FUNC(__imp__sub_82136498);
PPC_FUNC(sub_82136498) { EventTakedown(ctx, base, __imp__sub_82136498); }
extern "C" PPC_FUNC(__imp__sub_820BAD28);
PPC_FUNC(sub_820BAD28)
{
    const uint32_t car = PPC_LOAD_U32(ctx.r3.u32);
    if (game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::EasyBoost), car)))
    {
        FloatOverride speed(base, 0x825E21CC, 5.f);
        FloatOverride chainSpeed(base, 0x825E21D0, 5.f);
        __imp__sub_820BAD28(ctx, base);
    }
    else __imp__sub_820BAD28(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_820D18D8);
PPC_FUNC(sub_820D18D8)
{
    const uint32_t car = PPC_LOAD_U32(ctx.r3.u32);
    if (game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::EasyBoost), car)))
    {
        FloatOverride speed(base, 0x825E21BC, 5.f);
        FloatOverride distance(base, 0x825E21C0, 1.f);
        __imp__sub_820D18D8(ctx, base);
    }
    else __imp__sub_820D18D8(ctx, base);
}
extern "C" PPC_FUNC(__imp__sub_820AC718);
PPC_FUNC(sub_820AC718)
{
    const uint32_t self = ctx.r3.u32;
    if (uint32_t(ctx.lr) == 0x820BB144 && game::codes::Allowed(ReadContext(base, settings::GetBool(settings::Id::EasyBoost), PPC_LOAD_U32(self + 180))))
    {
        const float capacity = Float(base, self + 92), gain = Float(base, self + 112) + Float(base, self + 116);
        if (std::isfinite(capacity) && capacity > 0 && capacity <= 10000 && std::isfinite(gain) && gain > 0)
        {
            ctx.f1.f64 = capacity / gain;
            if (LogCodes()) fprintf(stderr, "[codes] easy_boost apply=1 near_miss=%g capacity=%g\n", ctx.f1.f64, capacity);
        }
    }
    __imp__sub_820AC718(ctx, base);
}

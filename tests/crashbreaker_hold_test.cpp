// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Real native wrapper: read-only guest pages, recorded input and world-time cadence.
#include <stdafx.h>
#include <game/crashbreaker_hold.h>
#include <game/crash_launch_scope.h>
#include <game/rewind_probe.h>
#include <user/settings.h>
#include <stdexcept>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_82277A90);
PPC_EXTERN_FUNC(sub_8210BB70);
namespace settings::detail {
    std::atomic<double> g_effective[kCount]{};
    std::atomic<uint8_t> g_state{2};
    void InitBeforeLoad(Id) { assert(false); }
}
namespace report { double SessionSeconds() { return 0; } }
namespace
{
    constexpr uint32_t event = game::crash_launch::kSoloEvent, car = 0x82B552D0, participant = 0x82CE0030;
    PPCContext expected, queryExpected;
    bool fail = false;
    double raw = .75, recorded = -1, want = .75;
    unsigned originals = 0;
}
extern "C" PPC_FUNC(__imp__sub_821325E8) {}
extern "C" PPC_FUNC(__imp__sub_8225E778) {}
extern "C" PPC_FUNC(__imp__sub_8210BB70)
{
    assert(std::memcmp(&ctx, &queryExpected, sizeof(ctx)) == 0);
    ctx.f1.f64 = raw;
}
extern "C" PPC_FUNC(__imp__sub_82277A90)
{
    assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    ++originals;
    PPCContext query = ctx;
    queryExpected = query;
    sub_8210BB70(query, base);
    recorded = query.f1.f64; // what the native producer/recorder will receive
    assert(recorded == want);
    query.f1 = queryExpected.f1;
    assert(std::memcmp(&query, &queryExpected, sizeof(query)) == 0);
    if (fail) throw std::runtime_error("native failure");
    ctx.r3.u64 = 0x123456789ABCDEF0;
    ctx.f1.f64 = 42;
}

int main()
{
    using game::crashbreaker::Hold;
    for (unsigned hz : {30u, 60u, 120u})
    {
        Hold h; unsigned edges = 0; bool previous = false;
        for (unsigned i = 0; i < 5 * hz; ++i)
        {
            float time = float(i) / hz;
            bool down = h.Down(true, time, 0);
            edges += down && !previous; previous = down;
            // Extra polls and paused world time cannot create extra edges.
            for (int n = 0; n < 5; ++n) assert(h.Down(true, time, 0) == down);
        }
        assert(edges == 40);
        assert(!h.Down(false, 5, 0));
        assert(h.Down(true, 5, 0)); // a fresh tap passes immediately
        assert(!h.Down(true, 5.08f, 0));
        assert(h.Down(true, 2, 0)); // rewind resets the cadence
        assert(h.Down(true, 2.08f, 2)); // new charge
        assert(!h.Down(true, NAN, 2) && !h.active);
        assert(h.Down(true, 2, 2));
        assert(h.Down(true, 10002, 2)); // no catch-up loop or queued presses
    }
    unsetenv("BURNOUT_CRASHBREAKER_LOG");
    auto* base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(base != MAP_FAILED);
    auto call = [&](uint32_t receiver = car) {
        PPCContext ctx{}; ctx.r3.u32 = receiver;
        ctx.r1.u32 = 0x70001000; ctx.lr = 0x82104584; expected = ctx;
        unsigned before = originals;
        sub_82277A90(ctx, base);
        expected.r3.u64 = 0x123456789ABCDEF0; expected.f1.f64 = 42;
        assert(originals == before + 1 && std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    };
    call(); // Off: all guest memory is inaccessible
    struct Range { uint32_t address, size; };
    for (auto r : {Range{0x82000000, 0x10000}, Range{0x82840000, 0x80000}, Range{0x82AB0000, 0x270000}, Range{0x82CE0000, 0x10000}})
        assert(mprotect(base + r.address, r.size, PROT_READ) == 0);
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    auto word = [&](uint32_t a, uint32_t v) {
        auto* start = base + (a & ~(page - 1));
        assert(mprotect(start, page, PROT_READ | PROT_WRITE) == 0);
        PPC_STORE_U32(a, v);
        assert(mprotect(start, page, PROT_READ) == 0);
    };
    auto byte = [&](uint32_t a, uint8_t v) {
        auto* start = base + (a & ~(page - 1));
        assert(mprotect(start, page, PROT_READ | PROT_WRITE) == 0);
        PPC_STORE_U8(a, v);
        assert(mprotect(start, page, PROT_READ) == 0);
    };
    auto real = [&](uint32_t a, float v) { word(a, std::bit_cast<uint32_t>(v)); };
    real(0x8200ED80, .5f);
    word(game::codes::kSession, game::codes::kSoloSession);
    word(game::codes::kSoloSession + 4, event); word(game::codes::kLocalPlayers, 1);
    word(game::codes::kSoloSession + 8, 0x82897FA8);
    word(game::rewind::kPhase, 5); word(game::rewind::kClockFlags, 0x01000000);
    word(event + 2384, 18); word(event + 2396, 7); word(participant + 18240, car);
    byte(participant + 13280 + 195, 1);
    real(participant + 13280 + 124, 10);
    settings::detail::g_effective[size_t(settings::Id::HoldCrashbreaker)] = 1;
    real(game::rewind::kWorldClock, 10); want = raw; call();
    real(game::rewind::kWorldClock, 10.08f); want = 0; call();
    real(game::rewind::kWorldClock, 10.13f); want = raw; call();
    real(game::rewind::kWorldClock, 10.20f); want = 0; fail = true;
    try { call(); assert(false); } catch (const std::runtime_error&) {}
    fail = false;
    // Scope unwinds on failure: a query outside the producer stays native.
    PPCContext outside{}; queryExpected = outside;
    sub_8210BB70(outside, base); assert(outside.f1.f64 == raw);
    // All excluded scopes retain the original input, including a held button.
    auto excluded = [&](uint32_t address, uint32_t value, uint32_t original) {
        word(address, value); want = raw; call(); word(address, original);
    };
    excluded(game::codes::kSession, game::codes::kSplitSession, game::codes::kSoloSession);
    excluded(game::codes::kSession, game::crash_launch::kNetworkSession, game::codes::kSoloSession);
    excluded(game::codes::kSession, game::codes::kFrontendSession, game::codes::kSoloSession);
    excluded(game::codes::kSession, 0xDEADBEEF, game::codes::kSoloSession);
    excluded(game::codes::kSoloSession + 4, event + 4, event);
    excluded(game::codes::kSoloSession + 8, 0, 0x82897FA8);
    excluded(game::codes::kSoloSession + 8, game::codes::kPartyController, 0x82897FA8);
    excluded(game::codes::kSoloSession + 8, game::codes::kCrashController, 0x82897FA8);
    excluded(game::codes::kLocalPlayers, 2, 1);
    excluded(game::rewind::kPhase, 4, 5);
    excluded(game::rewind::kClockFlags, 0x01010000, 0x01000000);
    excluded(event + 2384, 16, 18); // launch, including Classic
    excluded(event + 2396, 6, 7);
    excluded(participant + 18240, 0xDEADBEEF, car);
    byte(game::rewind::kWorldPlayback, 1); want = raw; call();
    byte(game::rewind::kWorldPlayback, 0);
    byte(participant + 13280 + 195, 0); call();
    byte(participant + 13280 + 195, 1);
    call(car + 4);
    // Release, invalid clocks and an Off transition preserve native values.
    raw = 0; want = 0; call();
    raw = .25; want = .25; call(); // below the native threshold stays untouched
    raw = .75; want = raw;
    real(game::rewind::kWorldClock, NAN); call();
    settings::detail::g_effective[size_t(settings::Id::HoldCrashbreaker)] = 0;
    call();
    assert(munmap(base, PPC_MEMORY_SIZE) == 0);
    puts("Crashbreaker hold: native ABI preserved; no guest writes; input filtered before recording");
}

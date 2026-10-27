// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the real hooks on read-only guest pages; disabled uses PROT_NONE.
#include <stdafx.h>
#include <game/crash_launch_scope.h>
#include <game/rewind_probe.h>
#include <user/settings.h>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_821325E8);
PPC_EXTERN_FUNC(sub_82277A90);
PPC_EXTERN_FUNC(sub_8225E778);
namespace settings::detail {
    std::atomic<double> g_effective[kCount]{};
    std::atomic<uint8_t> g_state{2};
    void InitBeforeLoad(Id) { assert(false); }
}
namespace report { double SessionSeconds() { return 2.; } }
static PPCContext expected;
static unsigned originals;
static void Original(PPCContext& ctx)
{
    assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    ++originals;
    ctx.r3.u64 = 0x123456789ABCDEF0;
    ctx.f1.f64 = 42.;
}
extern "C" PPC_FUNC(__imp__sub_821325E8) { Original(ctx); }
extern "C" PPC_FUNC(__imp__sub_82277A90) { Original(ctx); }
extern "C" PPC_FUNC(__imp__sub_8225E778) { Original(ctx); }
extern "C" PPC_FUNC(__imp__sub_8210BB70) { Original(ctx); }

int main(int argc, char**)
{
    const bool enabled = argc > 1;
    char path[] = "/tmp/crashbreaker-probe-XXXXXX";
    int fd = mkstemp(path); assert(fd >= 0); close(fd);
    if (enabled) setenv("BURNOUT_CRASHBREAKER_LOG", path, 1);
    else unsetenv("BURNOUT_CRASHBREAKER_LOG");
    auto* base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(base != MAP_FAILED);
    constexpr uint32_t car = 0x82B552D0;
    struct Range { uint32_t address, size; };
    constexpr Range ranges[] = {{0x82010000, 0x10000}, {0x82840000, 0x80000},
        {0x82AB0000, 0x270000}, {0x82CE0000, 0x10000}};
    if (enabled)
    {
        for (auto r : ranges) assert(mprotect(base + r.address, r.size, PROT_READ | PROT_WRITE) == 0);
        PPC_STORE_U32(game::codes::kSession, game::codes::kSoloSession);
        PPC_STORE_U32(game::codes::kSoloSession + 4, game::crash_launch::kSoloEvent);
        PPC_STORE_U32(game::codes::kLocalPlayers, 1);
        PPC_STORE_U32(game::rewind::kPhase, 5);
        PPC_STORE_U32(0x82CE0030 + 18240, car);
        PPC_STORE_U8(car + 6536, 1);
        for (auto r : ranges) assert(mprotect(base + r.address, r.size, PROT_READ) == 0);
    }
    for (unsigned i = 0; i < 160; ++i)
    {
        PPCContext ctx{};
        ctx.r3.u32 = i % 3 ? car : game::crash_launch::kSoloEvent;
        ctx.lr = 0x82104584; expected = ctx;
        if (i % 3 == 2) sub_8225E778(ctx, base);
        else if (i % 3 == 1) sub_82277A90(ctx, base);
        else sub_821325E8(ctx, base);
        expected.r3.u64 = 0x123456789ABCDEF0; expected.f1.f64 = 42.;
        assert(originals == i + 1 && std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    }
    FILE* read = fopen(path, "r"); assert(read);
    std::string log; char block[4096];
    while (size_t n = fread(block, 1, sizeof(block), read)) log.append(block, n);
    fclose(read);
    if (enabled)
    {
        for (const char* kind : {"charge_pre,", "charge_post,", "controls,", "burst,"})
            assert(log.find(kind) != std::string::npos);
        // Even in a valid solo session, a foreign receiver stays unmapped.
        PPCContext foreign{}; foreign.r3.u32 = 0xDEADBEEF; expected = foreign;
        sub_82277A90(foreign, base);
        // Reject foreign sessions before following their event pointer.
        assert(mprotect(base + 0x82840000, 0x80000, PROT_READ | PROT_WRITE) == 0);
        PPC_STORE_U32(game::codes::kSession, 0xDEADBEEF);
        assert(mprotect(base + 0x82840000, 0x80000, PROT_READ) == 0);
        // A hostile car receiver is never dereferenced by the diagnostic.
        PPCContext ctx{}; ctx.r3.u32 = 0xDEADBEEF; expected = ctx;
        sub_82277A90(ctx, base);
        ctx = {}; ctx.r3.u32 = game::crash_launch::kSoloEvent; expected = ctx;
        sub_821325E8(ctx, base);
    }
    else assert(log.empty());
    assert(munmap(base, PPC_MEMORY_SIZE) == 0);
    unlink(path);
    puts("Crashbreaker probe: read-only; native ABI preserved; disabled reads no guest memory");
}

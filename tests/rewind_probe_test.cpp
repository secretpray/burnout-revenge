// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Real PPC hook on read-only/protected guest pages. No game assets required.
#include <stdafx.h>
#include <game/rewind_probe.h>
#include <kernel/vmem.h>
#include <cpu/guest_thread.h>
#include <cstdlib>
#include <stdexcept>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_8211FA28);
PPC_EXTERN_FUNC(sub_82345648);
PPC_EXTERN_FUNC(sub_823456A8);
Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static bool live{}, throws{}, diagnosticThrows{};
static int originals{}, inventories{};
static PPCContext expected;
namespace game::madden { void AtBoundary(PPCContext&,uint8_t*) {} }
namespace game::crash_pacing { void Sample(const char*,uint8_t*,uint32_t,uint32_t) {} }
namespace online { bool Enabled() { return live; } }
namespace game::rewind::trial { void Captured(uint8_t*, uint32_t, uint32_t, uint32_t) {} }
namespace game::rewind::ring { void AtWorldBoundary(PPCContext&,uint8_t*) {} }
namespace gpu { std::string DescribeState() { return "test GPU"; } }
namespace apu { std::string DescribeState() { return "test audio"; } }
namespace report
{
    std::string DescribeThreadStacks()
    {
        ++inventories;
        if (diagnosticThrows) throw std::runtime_error("diagnostics");
        return "test thread\n";
    }
}
uint32_t GuestThread::GetCurrentThreadId() { return 1; }
namespace vmem
{
    PageHeap::PageHeap(uint32_t base, uint32_t size, uint32_t page) : m_base(base), m_size(size), m_pageSize(page) {}
    PageHeap& Virtual4K() { static PageHeap h(0x10000,0x1000,0x1000); return h; }
    PageHeap& Virtual64K() { static PageHeap h(0x40000000,0x10000,0x10000); return h; }
    PageHeap& Physical() { static PageHeap h(0xA0000000,0x1000,0x1000); return h; }
    bool PageHeap::Query(uint32_t addr, RegionInfo* info)
    {
        *info = {addr, 0, 0, m_size, X_MEM_FREE, 0};
        return true;
    }
}
extern "C" PPC_FUNC(__imp__sub_8211FA28)
{
    ++originals;
    assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    if (throws) throw std::runtime_error("original");
    ctx.r3.u64 = 0x123456789ABCDEF0;
    ctx.f1.f64 = 42;
}
extern "C" PPC_FUNC(__imp__sub_82345648)
{
    assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    ctx.r3.u64 = 1;
}
extern "C" PPC_FUNC(__imp__sub_823456A8)
{
    assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    ctx.r3.u64 = 0xABCDEF;
}
namespace
{
    // Keep everything except these pages inaccessible, including the event
    // and controller pointers. Observation must never follow either pointer.
    struct Range { uint32_t address, size; };
    const Range ranges[] = {{0x82840000,0x80000}, {0x82AB0000,0x10000}, {0x82AF0000,0x10000}, {0x82CE0000,0x10000}};
    void Protect(int protection)
    {
        for (auto r : ranges) assert(mprotect(g_memory.base + r.address, r.size, protection) == 0);
    }
    void Word(uint32_t address, uint32_t value)
    {
        auto* base = g_memory.base;
        PPC_STORE_U32(address, value);
    }
    void Setup()
    {
        using namespace game::codes;
        Protect(PROT_READ | PROT_WRITE);
        Word(kSession, kSoloSession); Word(kSoloSession + 4, 0xDEADBEEF); Word(kSoloSession + 8, 0xF00DF00D);
        Word(kLocalPlayers, 1); Word(kSpecialMode, 0); Word(kCrashVariant, 0);
        Word(game::rewind::kPhase, 5); Word(game::rewind::kWorldClock, std::bit_cast<uint32_t>(42.f));
        Word(game::rewind::kClockFlags, 0x01000000);
        Protect(PROT_READ);
        live = false;
    }
    void Call(uint32_t game = game::rewind::kGame)
    {
        PPCContext ctx{};
        ctx.r3.u32 = game; ctx.r4.u64 = 0xABCDEF0123456789;
        ctx.r1.u32 = 0x70001000; ctx.r13.u32 = 0x70000000; ctx.lr = 0x820A3B9C;
        expected = ctx;
        int before = originals;
        sub_8211FA28(ctx, g_memory.base);
        assert(originals == before + 1);
        expected.r3.u64 = 0x123456789ABCDEF0; expected.f1.f64 = 42;
        assert(std::memcmp(&ctx, &expected, sizeof(ctx)) == 0);
    }
    void Change(uint32_t address, uint32_t value)
    {
        Protect(PROT_READ | PROT_WRITE); Word(address, value); Protect(PROT_READ);
    }
}
int main(int argc, char** argv)
{
    using namespace game::codes;
    bool enabled = argc > 1 && std::strcmp(argv[1], "enabled") == 0;
    setenv("BURNOUT_REWIND_PROBE", enabled ? "1" : "0", 1);
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    g_userHeap.heap = reinterpret_cast<O1HeapInstance*>(g_memory.base + 0x70000000);
    g_userHeap.physicalHeap = reinterpret_cast<O1HeapInstance*>(g_memory.base + 0xBC000000);
    if (!enabled)
    {
        // Even the receiver/global pages are inaccessible with the probe off.
        Call(); Call(1); assert(inventories == 0);
    }
    else
    {
        Call(1); assert(inventories == 0); // foreign receiver: no global reads
        Setup();
        live = true; Change(kSession,0x8287C718); Call(); assert(inventories == 0);
        Setup(); Change(kSession, 0xDEADBEEF); Call(); assert(inventories == 0);
        Setup(); Change(kLocalPlayers, 2); Call(); assert(inventories == 0);
        Setup(); Change(kSpecialMode, 0x01000000); Call(); assert(inventories == 0);
        Setup(); Change(kSoloSession + 8, kCrashController); Call(); assert(inventories == 0);
        Setup(); Change(kSoloSession + 8, kPartyController); Call(); assert(inventories == 0);
        Setup(); Change(kSoloSession + 4, 0); Call(); assert(inventories == 0);
        Setup(); Change(game::rewind::kPhase, 4); Call(); assert(inventories == 0);
        for (uint32_t flags : {0u, 0x00010000u, 0x01010000u, 0x01010100u, 0x01000001u})
        {
            Setup(); Change(game::rewind::kClockFlags, flags); Call();
            Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(60.f)); Call();
            assert(inventories == 0); // instructions/results, Replay, unknown
        }
        Setup(); for (int i=0; i<150; ++i) Call(); assert(inventories == 0); // stationary instructions
        for (int i=1; i<155; ++i)
        {
            Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(42.f + i * 0.1f));
            Call();
        }
        assert(inventories == 1);
        Change(kSoloSession + 4, 0xDEADBEE0); Call(); assert(inventories == 1);
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(70.f)); Call(); assert(inventories == 2);
        Change(game::rewind::kPhase, 4); Call(); assert(inventories == 2);
        Change(game::rewind::kPhase, 5); Call(); assert(inventories == 2);
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(80.f)); Call(); assert(inventories == 3);
        Change(game::rewind::kWorldClock, 0x7FC00000); Call(); assert(inventories == 3); // NaN resets
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(0.f)); Call(); assert(inventories == 3);
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(6.f)); Call(); assert(inventories == 4);
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(1.f)); Call(); assert(inventories == 4); // clock rollback resets
        diagnosticThrows = true;
        Change(game::rewind::kWorldClock, std::bit_cast<uint32_t>(8.f)); Call(); assert(inventories == 5);
        // A diagnostic failure must leave the original game running, with no
        // later probe access even if the guest pages become inaccessible.
        Protect(PROT_NONE); Call(); assert(inventories == 5);
    }
    // Native hooks are observers of operations the original game initiated.
    // Their payload remains inaccessible: only the small original header is read.
    if (enabled) Setup();
    if (enabled)
    {
        Change(0x82AF1004, 0x1800);
        Change(0x82AF1038, std::bit_cast<uint32_t>(42.f));
    }
    PPCContext native{};
    native.r3.u32 = 0x82AF1000; native.r4.u32 = 0x42000000; native.r5.u32 = 0x100000;
    expected = native;
    sub_82345648(native, g_memory.base);
    expected.r3.u64 = 1;
    assert(std::memcmp(&native, &expected, sizeof(native)) == 0);
    native.r3.u32 = 0x82AF1000; expected = native;
    sub_823456A8(native, g_memory.base);
    expected.r3.u64 = 0xABCDEF;
    assert(std::memcmp(&native, &expected, sizeof(native)) == 0);
    throws = true;
    try { Call(); assert(false); } catch (const std::runtime_error&) {}
    assert(munmap(g_memory.base, PPC_MEMORY_SIZE) == 0);
    puts("rewind probe: original ABI preserved; guest memory never written; guards pass");
}

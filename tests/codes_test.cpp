// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the real code hook through PPC registers against protected guest
// pages. The original is a spy: no game data is required or shipped.
#include <stdafx.h>
#include <game/codes.h>
#include <user/settings.h>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_820DA9D8);
PPC_EXTERN_FUNC(sub_820C56F0);
PPC_EXTERN_FUNC(sub_82324D58);
PPC_EXTERN_FUNC(sub_8212F168);
PPC_EXTERN_FUNC(sub_820BAD28);
PPC_EXTERN_FUNC(sub_820D18D8);
PPC_EXTERN_FUNC(sub_820AC718);
PPC_EXTERN_FUNC(sub_8212DE50);
PPC_EXTERN_FUNC(sub_82134B80);
PPC_EXTERN_FUNC(sub_82136498);
Memory::Memory() {}
Memory g_memory;
static bool live = false, throwOriginal = false;
namespace online { bool Enabled() { return live; } }
static constexpr uint32_t kBoost = 0x20000, kCar = 0x30000;
static uint8_t sawFlag;
static float sawBoost;
static int calls;
static uint32_t moneyAmount, moneyMultiplier;
static bool realTakedown = true, probeThrows;
static float nearSpeed, chainSpeed, driftSpeed, driftDistance, earned;
static float Get(uint32_t address) { auto* base = g_memory.base; return std::bit_cast<float>(PPC_LOAD_U32(address)); }
extern "C" PPC_FUNC(__imp__sub_820DA9D8)
{
    assert(ctx.r3.u32 == kBoost && ctx.r4.u64 == 0xDEADBEEF01234567);
    ++calls;
    sawFlag = PPC_LOAD_U8(kBoost + 184);
    sawBoost = std::bit_cast<float>(PPC_LOAD_U32(kBoost + 96));
    if (throwOriginal) throw std::runtime_error("unwind");
    ctx.r3.u64 = 0x12345678;
}
extern "C" PPC_FUNC(__imp__sub_8212DE50) { ctx.f1.f64=Get(ctx.r3.u32+3056)-Get(0x82AB6FA4); }
extern "C" PPC_FUNC(__imp__sub_820AC718) { earned=float(ctx.f1.f64); ctx.r3.u32=95; }
extern "C" PPC_FUNC(__imp__sub_820D18D8) { driftSpeed=Get(0x825E21BC); driftDistance=Get(0x825E21C0); if (probeThrows) throw std::runtime_error("drift"); ctx.r3.u32=94; }
extern "C" PPC_FUNC(__imp__sub_820BAD28) { nearSpeed=Get(0x825E21CC); chainSpeed=Get(0x825E21D0); if (probeThrows) throw std::runtime_error("near"); ctx.r3.u32=93; }
extern "C" PPC_FUNC(__imp__sub_8212F168) { if (realTakedown) { PPC_STORE_U32(0x8276FB80, PPC_LOAD_U32(0x8276FB80) + 1); } ctx.r3.u32 = 92; }
extern "C" PPC_FUNC(__imp__sub_82134B80) { if (realTakedown) PPC_STORE_U32(0x8276FBAC, PPC_LOAD_U32(0x8276FBAC) + 1); ctx.r3.u32 = 96; }
extern "C" PPC_FUNC(__imp__sub_82136498) { if (realTakedown) PPC_STORE_U32(0x8276FBAC, PPC_LOAD_U32(0x8276FBAC) + 1); ctx.r3.u32 = 97; }
extern "C" PPC_FUNC(__imp__sub_820C56F0) {}
extern "C" PPC_FUNC(__imp__sub_82324D58) { moneyAmount = ctx.r5.u32; moneyMultiplier = ctx.r6.u32; ctx.r3.u32 = 91; }
static void Map(uint32_t address, size_t size)
{
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    auto start = address & ~(page - 1);
    assert(mprotect(g_memory.base + start, ((address + size - start + page - 1) & ~(page - 1)), PROT_READ | PROT_WRITE) == 0);
}
static void Word(uint32_t address, uint32_t value)
{
    auto* base = g_memory.base;
    PPC_STORE_U32(address, value);
}
static void Reset()
{
    using namespace game::codes;
    PPCContext constructor{}; constructor.r3.u32 = kBoost; sub_820C56F0(constructor,g_memory.base);
    for (auto id : {settings::Id::InfiniteBoost, settings::Id::InfiniteTime, settings::Id::QuickMoney, settings::Id::QuickTakedowns, settings::Id::EasyBoost}) settings::Set(id,1);
    realTakedown = true; probeThrows = false;
    live = throwOriginal = false;
    Word(kBoost + 88,0);
    Word(0x825E1F30, std::bit_cast<uint32_t>(2.f));
    for (uint32_t level=0;level<4;++level) {
        Word(0x825E1F3C+level*4,std::bit_cast<uint32_t>(float(240+160*level)));
        Word(0x825E1F4C+level*4,std::bit_cast<uint32_t>(float(level+1)));
        Word(0x825E1F5C+level*4,std::bit_cast<uint32_t>(10.f));
    }
    Word(0x825E21CC,std::bit_cast<uint32_t>(30.f));Word(0x825E21D0,std::bit_cast<uint32_t>(30.f));
    Word(0x825E21BC,std::bit_cast<uint32_t>(30.f));Word(0x825E21C0,std::bit_cast<uint32_t>(10.f));
    Word(kBoost,kCar); Word(0x8276FB80,0); Word(0x8276FBAC,0);
    Word(kBoost + 112,std::bit_cast<uint32_t>(1.f));Word(kBoost + 116,0);
    Word(kBoost + 180, kCar); Word(kBoost + 92, std::bit_cast<uint32_t>(240.f));
    Word(kBoost + 96, std::bit_cast<uint32_t>(12.f));
    Word(kCar + 15328, 0); Word(kCar + 19168, 0);
    Word(kSession, kSoloSession); Word(kSoloSession + 8, 0x82897FA8);
    Word(kLocalPlayers, 1); Word(kCrashVariant, 0);
    g_memory.base[kSpecialMode] = 0; g_memory.base[kBoost + 184] = 0;
}
static void Call(bool allowed, uint8_t originalFlag = 0)
{
    PPCContext ctx{};
    ctx.r3.u64 = kBoost; ctx.r4.u64 = 0xDEADBEEF01234567;
    g_memory.base[kBoost + 184] = originalFlag;
    int before = calls;
    sub_820DA9D8(ctx, g_memory.base);
    assert(calls == before + 1 && ctx.r3.u32 == 0x12345678);
    assert(sawFlag == (allowed ? 1 : originalFlag));
    assert(sawBoost == (allowed ? 720.f : 12.f));
    assert(g_memory.base[kBoost + 184] == originalFlag);
}
int main()
{
    using namespace game::codes;
    unsetenv("BURNOUT_INFINITE_BOOST"); unsetenv("BURNOUT_LOG_CODES");
    char temp[] = "/tmp/br-codes-test-XXXXXX";
    assert(mkdtemp(temp));
    auto file = std::filesystem::path(temp) / "settings.toml";
    assert(settings::Load(file));
    assert(!settings::GetBool(settings::Id::InfiniteBoost));
    assert(settings::Info(settings::Id::InfiniteBoost).category == settings::Category::Codes);
    settings::Set(settings::Id::InfiniteBoost, 1); assert(settings::Save());
    assert(settings::Load(file) && settings::GetBool(settings::Id::InfiniteBoost));
    g_memory.base = static_cast<uint8_t*>(mmap(nullptr, PPC_MEMORY_SIZE, PROT_NONE, MAP_PRIVATE | MAP_ANON, -1, 0));
    assert(g_memory.base != MAP_FAILED);
    Map(kBoost, 256); Map(kCar, 20000); Map(kSoloSession, 32); Map(kSession, 64);
    Map(kLocalPlayers, 4); Map(kCrashVariant, 4); Map(0x825E1F30,1024); Map(0x8276FB80,4); Map(0x82AB6FA4,4);
    Reset(); Call(true); Reset(); Call(true, 2);
    Reset(); settings::Set(settings::Id::InfiniteBoost, 0); Call(false);
    Reset(); live = true; Call(false); // Online overrides every solo-looking field.
    Reset(); g_memory.base[kSpecialMode] = 1; Call(false);
    for (uint32_t count : {0u, 2u, 3u, 4u, 0xFFFFFFFFu}) { Reset(); Word(kLocalPlayers, count); Call(false); }
    for (uint32_t session : {0u, 0x8287C6FCu, 0x8287C718u, 0xDEADBEEFu})
    {
        Reset(); Word(kSession, session); Call(false);
    }
    for (uint32_t controller : {0u, kPartyController, kCrashController})
        for (uint32_t variant : {0u, 1u, 2u, 4u})
        { Reset(); Word(kSoloSession + 8, controller); Word(kCrashVariant, variant); Call(false); }
    for (uint32_t driver : {1u, 2u, 0xFFFFFFFFu}) { Reset(); Word(kCar + 15328, driver); Call(false); }
    for (uint32_t player : {1u, 2u, 3u, 0xFFFFFFFFu}) { Reset(); Word(kCar + 19168, player); Call(false); }
    for (float capacity : {0.f, -1.f, 10001.f, std::numeric_limits<float>::infinity(), std::numeric_limits<float>::quiet_NaN()})
    { Reset(); Word(kBoost + 92, std::bit_cast<uint32_t>(capacity)); Call(false); }
    // A mode transition in the same process must never carry our stock flag.
    Reset(); Call(true); live = true; Word(kBoost + 96, std::bit_cast<uint32_t>(12.f)); Call(false, 2);
    Reset(); throwOriginal = true;
    PPCContext ctx{}; ctx.r3.u32 = kBoost; ctx.r4.u64 = 0xDEADBEEF01234567;
    try { sub_820DA9D8(ctx, g_memory.base); assert(false); } catch (const std::runtime_error&) {}
    assert(g_memory.base[kBoost + 184] == 0);
    // Exercise every modifier with all multiplayer rejection paths, through its real ABI.
    for (int mode=0;mode<6;++mode)
    {
        Reset();
        if (mode==1) live=true;
        if (mode==2) Word(kLocalPlayers,2);
        if (mode==3) Word(kSession,0x8287C6FC);
        if (mode==4) Word(kSoloSession+8,kCrashController);
        if (mode==5) Word(kSession,0xDEADBEEF);
        const bool allowed=mode==0;
        PPCContext c{};c.r3.u32=kCar;c.r4.u32=0;c.r5.u32=100;c.r6.u32=2;
        sub_82324D58(c,g_memory.base);assert(c.r3.u32==91 && moneyAmount==(allowed?15000u:100u) && moneyMultiplier==2);
        c={};c.r5.u32=kCar;sub_8212F168(c,g_memory.base);auto* base=g_memory.base;
        assert(c.r3.u32==92 && PPC_LOAD_U32(0x8276FB80)==(allowed?99u:1u));
        c={};c.r5.u32=kCar;sub_82134B80(c,g_memory.base);assert(c.r3.u32==96 && PPC_LOAD_U32(0x8276FBAC)==(allowed?99u:1u));
        c={};c.r5.u32=kCar;sub_82136498(c,g_memory.base);assert(c.r3.u32==97 && PPC_LOAD_U32(0x8276FBAC)==(allowed?100u:2u));
        c={};c.r3.u32=kBoost;sub_820BAD28(c,g_memory.base);assert(c.r3.u32==93 && nearSpeed==(allowed?5.f:30.f) && chainSpeed==(allowed?5.f:30.f));
        assert(Get(0x825E21CC)==30 && Get(0x825E21D0)==30);
        c={};c.r3.u32=kBoost;sub_820D18D8(c,g_memory.base);assert(c.r3.u32==94 && driftSpeed==(allowed?5.f:30.f) && driftDistance==(allowed?1.f:10.f));
        assert(Get(0x825E21BC)==30 && Get(0x825E21C0)==10);
        c={};c.r3.u32=kBoost;c.lr=0x820BB144;c.f1.f64=60;sub_820AC718(c,g_memory.base);assert(c.r3.u32==95 && earned==(allowed?240.f:60.f));
    }
    Reset();realTakedown=false;PPCContext c{};c.r5.u32=kCar;sub_8212F168(c,g_memory.base);
    c={};c.r5.u32=kCar;sub_82134B80(c,g_memory.base);
    {auto* base=g_memory.base;assert(PPC_LOAD_U32(0x8276FB80)==0 && PPC_LOAD_U32(0x8276FBAC)==0);}
    // An AI attacker's event takedown is never raised.
    Reset();Word(kCar+15328,1);c={};c.r5.u32=kCar;sub_82136498(c,g_memory.base);
    {auto* base=g_memory.base;assert(PPC_LOAD_U32(0x8276FBAC)==1);}
    // Event time freezes by advancing only the deadline; Off resumes normally.
    constexpr uint32_t kEvent=kCar;
    Reset();Word(kSoloSession+4,kEvent);g_memory.base[kEvent+3132]=1;
    Word(kEvent+3056,std::bit_cast<uint32_t>(160.f));Word(0x82AB6FA4,std::bit_cast<uint32_t>(100.f));
    c={};c.r3.u32=kEvent;sub_8212DE50(c,g_memory.base);assert(c.f1.f64==60);
    Word(0x82AB6FA4,std::bit_cast<uint32_t>(100.5f));c.r3.u32=kEvent;sub_8212DE50(c,g_memory.base);assert(c.f1.f64==60 && Get(kEvent+3056)==160.5f);
    settings::Set(settings::Id::InfiniteTime,0);Word(0x82AB6FA4,std::bit_cast<uint32_t>(101.f));c.r3.u32=kEvent;sub_8212DE50(c,g_memory.base);assert(c.f1.f64==59.5);
    for (int mode=1;mode<6;++mode) {
        Reset();Word(kSoloSession+4,kEvent);g_memory.base[kEvent+3132]=1;
        if(mode==1)live=true;if(mode==2)Word(kLocalPlayers,2);if(mode==3)Word(kSession,0x8287C6FC);
        if(mode==4)Word(kSoloSession+8,kCrashController);if(mode==5)Word(kSession,0xDEADBEEF);
        Word(kEvent+3056,std::bit_cast<uint32_t>(160.f));Word(0x82AB6FA4,std::bit_cast<uint32_t>(100.f));
        c.r3.u32=kEvent;sub_8212DE50(c,g_memory.base);
        Word(0x82AB6FA4,std::bit_cast<uint32_t>(100.5f));c.r3.u32=kEvent;sub_8212DE50(c,g_memory.base);assert(c.f1.f64==59.5 && Get(kEvent+3056)==160);
    }
    // No amplification for AI, a different award call site, invalid earning factors or another player.
    Reset();Word(kCar+15328,1);c={};c.r3.u32=kBoost;c.lr=0x820BB144;c.f1.f64=60;sub_820AC718(c,g_memory.base);assert(earned==60);
    Reset();c={};c.r3.u32=kBoost;c.lr=0x820D19A0;c.f1.f64=60;sub_820AC718(c,g_memory.base);assert(earned==60);
    Reset();Word(kBoost+112,0);c={};c.r3.u32=kBoost;c.lr=0x820BB144;c.f1.f64=60;sub_820AC718(c,g_memory.base);assert(earned==60);
    Reset();c={};c.r3.u32=kCar;c.r4.u32=1;c.r5.u32=100;c.r6.u32=2;sub_82324D58(c,g_memory.base);assert(moneyAmount==100);
    Reset();probeThrows=true;c={};c.r3.u32=kBoost;
    try{sub_820BAD28(c,g_memory.base);assert(false);}catch(const std::runtime_error&){}
    assert(Get(0x825E21CC)==30 && Get(0x825E21D0)==30);
    try{sub_820D18D8(c,g_memory.base);assert(false);}catch(const std::runtime_error&){}
    assert(Get(0x825E21BC)==30 && Get(0x825E21C0)==10);
    Reset();Call(true);settings::Set(settings::Id::InfiniteBoost,0);Word(kBoost+96,std::bit_cast<uint32_t>(12.f));Call(false);
    {auto* base=g_memory.base;assert(PPC_LOAD_U32(kBoost+88)==0 && Get(kBoost+92)==240 && Get(kBoost+104)==10 && Get(kBoost+112)==1);}
    munmap(g_memory.base, PPC_MEMORY_SIZE); std::filesystem::remove_all(temp);
    puts("Codes: real PPC hook, settings roundtrip, online/local/Crash/AI rejection, transitions and flag restore PASS");
}

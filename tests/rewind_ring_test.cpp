// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the real bounded ring against a synthetic native world serializer.
#include <stdafx.h>
#include <game/rewind_ring.h>
#include <game/rewind_probe.h>
#include <cpu/guest_thread.h>
#include <cstdlib>
#include <unistd.h>

Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static constexpr uint32_t kRecorder=0x82AFE440,kScratch=0x60000000,kInput=0x50000000,kSnapshots=0x52000000;
static constexpr uint32_t kEvent=0x8287EC90,kRng=game::rewind::kGame+0x33088;
static unsigned allocations{},frees{},captures{},restores{};
static bool onlineMode{},large{},failedCapture{};
static uint32_t liveToken{},snapshotToken{};
static float loadedClock{};
static uint32_t guestThread=1;
namespace online { bool Enabled() { return onlineMode; } }
uint32_t GuestThread::GetCurrentThreadId() { return guestThread; }
void* Heap::Alloc(size_t size) { assert(size==16*1024*1024+76); ++allocations; return g_memory.base+kScratch; }
void Heap::Free(void* p) { assert(p==g_memory.base+kScratch); ++frees; }
static void Word(uint32_t address,uint32_t value) { auto* base=g_memory.base; PPC_STORE_U32(address,value); }
static uint32_t Word(uint32_t address) { auto* base=g_memory.base; return PPC_LOAD_U32(address); }
static void Float(uint32_t address,float value) { Word(address,std::bit_cast<uint32_t>(value)); }
static float Clock() { return std::bit_cast<float>(Word(game::rewind::kWorldClock)); }
extern "C" PPC_FUNC(__imp__sub_82345648)
{
    assert(ctx.r3.u32==kScratch && ctx.r4.u32==kScratch+76 && ctx.r5.u32==16*1024*1024);
    ++captures;
    if (failedCapture) { ctx.r3.u64=0; return; }
    const uint32_t size=large ? 4*1024*1024:128;
    Word(kScratch,kScratch+76);Word(kScratch+4,size);
    Word(kScratch+56,Word(game::rewind::kWorldClock));
    for (unsigned i=0;i<2;++i) Word(kScratch+76+i*4,Word(kRng+i*4));
    Word(kScratch+84,Word(kEvent+3056));Word(kScratch+88,liveToken);
    ctx.r3.u64=1;ctx.r8.u64=0xFFFF;ctx.f1.f64=100;
}
extern "C" PPC_FUNC(__imp__sub_823456A8)
{
    assert(ctx.r3.u32==kScratch && Word(kScratch)==kScratch+76);
    ++restores;
    loadedClock=std::bit_cast<float>(Word(kScratch+56));
    assert(loadedClock==29.f+float(restores));
    for (unsigned i=0;i<2;++i) Word(kRng+i*4,Word(kScratch+76+i*4));
    Word(kEvent+3056,Word(kScratch+84));snapshotToken=Word(kScratch+88);
    Float(game::rewind::kWorldClock,loadedClock);
    base[game::rewind::kWorldPlayback]=1;
    ctx.r3.u64=1;ctx.r8.u64=0xABAB;ctx.f1.f64=-10;
}
static void Map(uint32_t address,size_t size)
{ assert(mprotect(g_memory.base+address,size,PROT_READ|PROT_WRITE)==0); }
static void Call()
{
    PPCContext ctx{};ctx.r1.u32=0x70000000;ctx.r3.u64=0x12345;ctx.lr=0x8210EF58;
    const auto before=ctx;
    game::rewind::ring::AtBoundary(ctx,g_memory.base,kRecorder);
    assert(memcmp(&ctx,&before,sizeof(ctx))==0);
}
int main(int argc,char** argv)
{
    const std::string mode=argc>1 ? argv[1]:"sequence";
    setenv("BURNOUT_REWIND_RING",mode=="disabled" ? "0":mode=="invalid" ? "30junk":mode=="budget" ? "30":"sequence",1);
    unsetenv("BURNOUT_REWIND_TRIAL");
    if (mode=="trial") setenv("BURNOUT_REWIND_TRIAL","2",1);
    if (mode=="thread") guestThread=2;
    g_memory.base=static_cast<uint8_t*>(mmap(nullptr,PPC_MEMORY_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(g_memory.base!=MAP_FAILED);
    if (mode=="disabled" || mode=="invalid" || mode=="trial" || mode=="thread")
    { Call();assert(!allocations && !captures);assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0; }
    Map(0x82840000,0x80000);Map(0x82AB0000,0x10000);Map(0x82AF0000,0x10000);
    Map(0x82CE0000,0x10000);Map(0x82D10000,0x10000);Map(kScratch,17*1024*1024);
    using namespace game::rewind;
    namespace codes=game::codes;
    Word(codes::kSession,codes::kSoloSession);Word(codes::kSoloSession+4,kEvent);
    Word(codes::kSoloSession+8,0x82897FA8);Word(codes::kLocalPlayers,1);Word(kPhase,5);Word(kClockFlags,0x01000000);
    for (auto [offset,buffer]:{std::pair{116u,kInput},std::pair{140u,kSnapshots}})
    {
        Word(kRecorder+offset+4,buffer);Word(kRecorder+offset+16,0x1000000);
        Word(kRecorder+offset+8,buffer);Word(kRecorder+offset+12,buffer);
    }
    onlineMode=mode=="online";large=mode=="budget";
    failedCapture=mode=="failed";
    if (mode=="replay") Word(kClockFlags,0x01010000);
    if (mode=="event") Word(codes::kSoloSession+4,kGame+0x36170);
    if (mode=="lifecycle")
    {
        Float(kWorldClock,20.f);Call();assert(captures==1 && allocations==1);
        // Same event object can be reused by a restart: a backwards clock
        // must discard the prior timeline before taking another snapshot.
        Float(kWorldClock,5.f);Call();assert(captures==2 && allocations==2 && frees==1);
        Word(kClockFlags,0x01010000);Call();assert(frees==2);
        Word(kClockFlags,0x01000000);Call();assert(allocations==3);
        Word(kPhase,0);Call();assert(frees==3 && !restores);
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    float now=5;
    uint32_t previousTokens[3]{};
    for (unsigned tick=0;tick<500;++tick)
    {
        Float(kWorldClock,now);Float(kEvent+3056,90.f+float(tick));
        Word(kRng,1000+tick);Word(kRng+4,2000+tick);liveToken=tick+100;
        const auto frame=uint32_t(now*60);
        Word(kRecorder+532,frame);Word(kRecorder+528,frame);
        Word(kRecorder+540,unsigned(now)/15);Float(kRecorder+572,float(unsigned(now)/15*15));
        for (auto [offset,buffer]:{std::pair{116u,kInput},std::pair{140u,kSnapshots}})
        {
            Word(kRecorder+offset+8,buffer+frame);Word(kRecorder+offset+12,buffer+frame);
        }
        if (mode=="buffer" && tick==20) Word(kRecorder+116+4,kInput+0x1000);
        const auto before=restores;Call();
        if (restores!=before)
        {
            const auto loadedFrame=uint32_t(loadedClock*60);
            assert(Word(kRecorder+532)==loadedFrame && Word(kRecorder+528)==loadedFrame);
            assert(Word(kRecorder+116+8)==kInput+loadedFrame && Word(kRecorder+116+12)==kInput+loadedFrame);
            assert(Word(kRecorder+140+8)==kSnapshots+loadedFrame && Word(kRecorder+140+12)==kSnapshots+loadedFrame);
            assert(Word(kRecorder+540)==2 && Word(kRecorder+572)==std::bit_cast<uint32_t>(30.f));
            assert(g_memory.base[kWorldPlayback]==0);
            previousTokens[restores-1]=snapshotToken;
            // Each new branch must serialize fresh state instead of retaining
            // points from the discarded future (RNG/token changes every tick).
            if (restores>1) assert(previousTokens[restores-1]!=previousTokens[restores-2]);
            now=Clock();
        }
        now+=.25f;
    }
    if (mode=="sequence") assert(restores==3 && captures>60 && allocations==1 && frees==1);
    else if (mode=="budget" || mode=="buffer" || mode=="failed") assert(!restores && allocations==1 && frees==1);
    else assert(!restores && !allocations && !captures);
    assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);
    puts("rewind ring: native capture, 10/20/30 selection, new branches, bounds and exclusions passed");
}

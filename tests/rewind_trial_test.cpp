// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Real native-checkpoint orchestration; synthetic serialized world and original
// function spies. No game assets, real profile, host stack copies or GPU required.
#include <stdafx.h>
#include <game/rewind_trial.h>
#include <game/rewind_probe.h>
#include <cpu/guest_thread.h>
#include <cstdlib>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_82346C68);
Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static bool live{};
static unsigned allocations{}, frees{}, restores{}, originals{};
static constexpr uint32_t kRecorder=0x82AFE440, kHeader=0x40000000, kPayload=kHeader+76, kScratch=0x70000000;
static constexpr uint32_t kEvent=0x8287EC90, kInput=0x50000000, kSnapshots=0x51000000;
static PPCContext expected;
namespace online { bool Enabled() { return live; } }
namespace game::rewind::ring { void AtBoundary(PPCContext&, uint8_t*, uint32_t) {} }
uint32_t GuestThread::GetCurrentThreadId() { return 1; }
void* Heap::Alloc(size_t size) { assert(size <= 0x10000); ++allocations; return g_memory.base+kScratch; }
void Heap::Free(void* p) { assert(p == g_memory.base+kScratch); ++frees; }
static void Word(uint32_t address, uint32_t value) { auto* base=g_memory.base; PPC_STORE_U32(address,value); }
static uint32_t Word(uint32_t address) { auto* base=g_memory.base; return PPC_LOAD_U32(address); }
static void Float(uint32_t address,float value) { Word(address,std::bit_cast<uint32_t>(value)); }
extern "C" PPC_FUNC(__imp__sub_82346C68)
{
    assert(std::memcmp(&ctx,&expected,sizeof(ctx))==0);
    ++originals; ctx.r3.u64=0xFEEDBEEFDEADBEEF;
}
extern "C" PPC_FUNC(__imp__sub_823456A8)
{
    assert(ctx.r3.u32==kScratch && Word(kScratch)==kScratch+76 && Word(kScratch+4)==128);
    for (unsigned i=0;i<128;++i) assert(base[kScratch+76+i]==uint8_t(i));
    ++restores;
    Float(game::rewind::kWorldClock,15.f); Float(kEvent+3056,90.f);
    base[game::rewind::kWorldPlayback]=1;
    ctx.r3.u64=1; ctx.f1.f64=123; ctx.r6.u64=0x777;
}
static void Map(uint32_t address, size_t size)
{
    assert(mprotect(g_memory.base+address,size,PROT_READ|PROT_WRITE)==0);
}
static void Call()
{
    PPCContext ctx{}; ctx.r3.u32=kRecorder; ctx.r4.u64=0xAABBCCDD12345678;
    ctx.r1.u32=0x70100000; ctx.lr=0x8210EF58; expected=ctx;
    sub_82346C68(ctx,g_memory.base);
    expected.r3.u64=0xFEEDBEEFDEADBEEF;
    assert(std::memcmp(&ctx,&expected,sizeof(ctx))==0);
}
int main(int argc,char** argv)
{
    std::string mode=argc>1 ? argv[1] : "valid";
    setenv("BURNOUT_REWIND_TRIAL",mode=="disabled" ? "0" : "2",1);
    g_memory.base=static_cast<uint8_t*>(mmap(nullptr,PPC_MEMORY_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(g_memory.base!=MAP_FAILED);
    if(mode=="disabled")
    {
        game::rewind::trial::Captured(g_memory.base,1,2,3); Call();
        assert(allocations==0 && restores==0 && originals==1);
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0); return 0;
    }
    Map(0x82840000,0x80000);Map(0x82AB0000,0x10000);Map(0x82AF0000,0x10000);Map(0x82CE0000,0x10000);Map(0x82D10000,0x10000);
    Map(kHeader,0x10000);Map(kScratch,0x10000);
    using namespace game::rewind;
    namespace codes = game::codes;
    Word(codes::kSession,codes::kSoloSession);Word(codes::kSoloSession+4,kEvent);Word(codes::kSoloSession+8,0x82897FA8);
    Word(codes::kLocalPlayers,1);Word(kPhase,5);Word(kClockFlags,0x01000001);Float(kWorldClock,15.f);Float(kEvent+3056,90.f);
    Word(kHeader,kPayload);Word(kHeader+4,128);Float(kHeader+56,15.f);
    for(unsigned i=0;i<128;++i)g_memory.base[kPayload+i]=uint8_t(i);
    for(auto [offset,buffer]:{std::pair{116u,kInput},std::pair{140u,kSnapshots}})
    {
        Word(kRecorder+offset+4,buffer);Word(kRecorder+offset+16,0x1000);
        Word(kRecorder+offset+8,buffer+100);Word(kRecorder+offset+12,buffer+100);
    }
    Word(kRecorder+532,100);Word(kRecorder+528,100);Word(kRecorder+540,2);
    Word(kRecorder+164,kSnapshots);Word(kRecorder+168,kSnapshots+80);
    live=mode=="online";
    if(mode=="mode") Word(codes::kSoloSession+4,kGame+0x36170); // native Race, not the measured trial
    game::rewind::trial::Captured(g_memory.base,kHeader,kPayload,128);
    Word(kClockFlags,0x01000000);Call();
    if(live || mode=="mode") { assert(allocations==0 && restores==0); }
    else
    {
        assert(allocations==1 && restores==0);
        // The game-owned source may be overwritten once the clone is armed.
        memset(g_memory.base+kHeader,0,0x10000);
        if(mode=="buffer") Word(kRecorder+116+4,kInput+0x2000);
        if(mode=="event") Word(codes::kSoloSession+4,kEvent+0x1000);
        for(unsigned n=0;n<2;++n)
        {
            Float(kWorldClock,17.f);Float(kEvent+3056,777.f);
            Word(kRecorder+532,220);Word(kRecorder+528,220);Word(kRecorder+540,3);
            Word(kRecorder+116+8,kInput+300);Word(kRecorder+116+12,kInput+400);
            Word(kRecorder+140+8,kSnapshots+300);Word(kRecorder+140+12,kSnapshots+400);
            Word(kRecorder+164,kSnapshots+800);
            Call();
            if(mode=="valid")
            {
                assert(restores==n+1 && Word(kWorldClock)==std::bit_cast<uint32_t>(15.f));
                assert(Word(kEvent+3056)==std::bit_cast<uint32_t>(90.f));
                assert(g_memory.base[kWorldPlayback]==0);
                assert(Word(kRecorder+532)==100 && Word(kRecorder+528)==100 && Word(kRecorder+540)==2);
                assert(Word(kRecorder+116+8)==kInput+100 && Word(kRecorder+116+12)==kInput+100);
                assert(Word(kRecorder+140+8)==kSnapshots+100 && Word(kRecorder+140+12)==kSnapshots+100);
                assert(Word(kRecorder+164)==kSnapshots);
            }
        }
        assert(restores==(mode=="valid" ? 2u : 0u) && frees==1);
    }
    assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);
    puts("rewind trial: clone, ABI, event/buffer guards and future truncation pass (synthetic world)");
}

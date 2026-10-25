// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include <game/madden_unlock.h>
#include <game/codes.h>
#include <game/rewind_probe.h>
#include <unistd.h>

Memory::Memory() {}
Memory g_memory;
static constexpr uint32_t kCareer = 0x8281FA78;
static int calls;
PPC_FUNC(sub_821015A8)
{
    assert(ctx.r3.u64 == kCareer);
    assert(PPC_LOAD_U8(kCareer+1242)==0);
    PPC_STORE_U8(kCareer+1242,1);
    ++calls;
    ctx.r3.u64=0xDEADBEEF; ctx.f1.f64=123.; ctx.lr=0xFEED;
}
static void Map(uint32_t address, size_t size)
{
    const size_t page = size_t(sysconf(_SC_PAGESIZE));
    const size_t start = address & ~(page-1);
    assert(mprotect(g_memory.base+start,(address+size-start+page-1)&~(page-1),PROT_READ|PROT_WRITE)==0);
}
int main()
{
    using namespace game;
    using madden::Status;
    g_memory.base=static_cast<uint8_t*>(mmap(nullptr,PPC_MEMORY_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(g_memory.base!=MAP_FAILED);
    Map(rewind::kPhase,4); Map(codes::kSession,4); Map(kCareer,1260);
    auto* base=g_memory.base;
    PPCContext ctx{}; ctx.r3.u64=0x123456789; ctx.f1.f64=42.; ctx.lr=0xACDC;
    auto poll=[&] { madden::AtBoundary(ctx,base); assert(ctx.r3.u64==0x123456789 && ctx.f1.f64==42. && ctx.lr==0xACDC); };
    madden::Request(); poll(); assert(calls==0 && madden::GetStatus()==Status::Unavailable);
    PPC_STORE_U32(rewind::kPhase,5); PPC_STORE_U32(codes::kSession,codes::kFrontendSession);
    poll(); assert(madden::GetStatus()==Status::Unavailable);
    PPC_STORE_U32(kCareer+1248,0x10000);
    poll(); assert(madden::GetStatus()==Status::Ready);
    madden::Request(); assert(madden::GetStatus()==Status::Pending && calls==0);
    // A request is discarded when the career menu is left before consumption.
    PPC_STORE_U32(codes::kSession,codes::kSoloSession);
    poll(); assert(calls==0 && madden::GetStatus()==Status::Unavailable);
    PPC_STORE_U32(codes::kSession,codes::kFrontendSession);
    poll(); assert(madden::GetStatus()==Status::Ready);
    madden::Request(); poll(); assert(calls==1 && madden::GetStatus()==Status::Unlocked);
    madden::Request(); poll(); assert(calls==1);
    // No other career bytes, catalogue pointer or adjacent bonus flag changed.
    for (uint32_t i=0;i<1248;++i) assert(PPC_LOAD_U8(kCareer+i)==(i==1242 ? 1:0));
    assert(PPC_LOAD_U32(kCareer+1248)==0x10000);
    munmap(base,PPC_MEMORY_SIZE); g_memory.base=nullptr;
}

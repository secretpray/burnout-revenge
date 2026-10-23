// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the actual PPC hook, including native fallback and its scoped ABI.
#include <stdafx.h>
#include <game/rewind_copy.h>
#include <array>
PPC_EXTERN_FUNC(sub_8259D3A0);
static unsigned nativeCalls{};
extern "C" PPC_FUNC(__imp__sub_8259D3A0)
{ ++nativeCalls;ctx.r3.u64=0xDEADBEEF; }
static void Call(uint8_t* base,uint32_t destination,uint32_t source,uint32_t size,bool fast)
{
    PPCContext ctx{};ctx.r3.u64=0xABCD00000000ull|destination;ctx.r4.u32=source;ctx.r5.u32=size;
    ctx.r1.u32=0xF000;ctx.r31.u64=0x12345678;ctx.f31.f64=42.;ctx.lr=0x820BF354;
    const auto saved=ctx;const auto before=nativeCalls;
    sub_8259D3A0(ctx,base);
    if(fast)
    {
        assert(nativeCalls==before && !memcmp(&ctx,&saved,sizeof(ctx)));
        assert(PPC_LOAD_U64(saved.r1.u32-8)==saved.r3.u64);
        assert(!memcmp(base+destination,base+source,size));
    }
    else
    {
        assert(nativeCalls==before+1 && ctx.r3.u64==0xDEADBEEF);
        ctx.r3=saved.r3;assert(!memcmp(&ctx,&saved,sizeof(ctx)));
    }
}
int main()
{
    std::array<uint8_t,0x10000> memory{};
    for(unsigned i=0;i<0x1000;++i) memory[0x100+i]=uint8_t(i*31);
    using namespace game::rewind::copy;
    Call(memory.data(),0x2001,0x103,513,false);
    {
        Scope capture(0x2000,0x1000,true);
        Call(memory.data(),0x2001,0x103,513,true); // unaligned capture
        Call(memory.data(),0x6003,0x2001,513,true); // restore direction
        assert(region.calls==2 && region.bytes==1026);
        Call(memory.data(),0x2200,0x2001,513,false); // overlapping
        Call(memory.data(),0x2FF0,0x100,32,false); // staging end exceeded
        Call(memory.data(),0x1FF0,0x2001,32,false); // staging beginning crossed
        Call(memory.data(),0xFFFFFFF0,0x2001,32,false); // address wrapping
        Call(memory.data(),0x6000,0x100,32,false); // unrelated game memcpy
        Call(memory.data(),0x2001,0x100,0,false);
        {
            Scope original(0,0,false);
            Call(memory.data(),0x2001,0x103,513,false);
        }
        assert(region.calls==2 && region.bytes==1026);
        Call(memory.data(),0x2800,0x103,64,true);
    }
    assert(!region.begin && !region.calls);
    Call(memory.data(),0x2001,0x103,513,false);
    puts("rewind copy: staged bytes, ABI, overlap/bounds, nested scopes and native fallback passed");
}

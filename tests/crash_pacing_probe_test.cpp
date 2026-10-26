// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Real diagnostic hooks on read-only guest pages, including the native ABI.
#include <stdafx.h>
#include <game/crash_pacing.h>
#include <game/crash_launch_scope.h>
#include <game/rewind_probe.h>
#include <game/camera.h>
#include <unistd.h>

PPC_EXTERN_FUNC(sub_82101E18);
PPC_EXTERN_FUNC(sub_8235A080);
PPC_EXTERN_FUNC(sub_8210ECF0);
namespace report { double SessionSeconds() { return 1.; } }
static PPCContext expected;
static unsigned originals;
extern "C" PPC_FUNC(__imp__sub_82101E18)
{
    assert(std::memcmp(&ctx,&expected,sizeof(ctx))==0);
    ++originals;
    ctx.r3.u64=0x123456789ABCDEF0;
    ctx.f1.f64=42.;
}
extern "C" PPC_FUNC(__imp__sub_8235A080)
{
    assert(std::memcmp(&ctx,&expected,sizeof(ctx))==0);
    ++originals;
    ctx.r3.u64=0x123456789ABCDEF0;
    ctx.f1.f64=42.;
}
extern "C" PPC_FUNC(__imp__sub_8210ECF0)
{
    __imp__sub_82101E18(ctx,base);
}

int main(int argc,char** argv)
{
    const bool enabled=argc>1;
    char name[]="/tmp/crash-pacing-test-XXXXXX";
    int fd=mkstemp(name);assert(fd>=0);close(fd);
    if(enabled) setenv("BURNOUT_CRASH_PACING_LOG",name,1);
    else unsetenv("BURNOUT_CRASH_PACING_LOG");
    auto* base=static_cast<uint8_t*>(mmap(nullptr,PPC_MEMORY_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(base!=MAP_FAILED);
    struct Range { uint32_t address,size; };
    constexpr Range ranges[]={{0x82840000,0x80000},{0x82920000,0x10000},
        {0x82AB0000,0x60000},{0x82CE0000,0x10000},{0x82D10000,0x10000}};
    if(enabled)
    {
        for(auto r:ranges) assert(mprotect(base+r.address,r.size,PROT_READ|PROT_WRITE)==0);
        PPC_STORE_U32(game::codes::kSession,game::codes::kSoloSession);
        PPC_STORE_U32(game::codes::kSoloSession+4,game::crash_launch::kSoloEvent);
        PPC_STORE_U32(game::rewind::kPhase,5);
        PPC_STORE_U32(game::camera::kPrimary+104,game::camera::kFrame);
        PPC_STORE_U32(game::rewind::kClockFlags,0x01000000);
        for(auto r:ranges) assert(mprotect(base+r.address,r.size,PROT_READ)==0);
    }
    for(unsigned i=0;i<140;++i)
    {
        PPCContext ctx{};ctx.r3.u32=game::camera::kPrimary;ctx.lr=0x12345678;expected=ctx;
        const unsigned before=originals;
        if(i%3==0) sub_82101E18(ctx,base);
        else if(i%3==1) sub_8235A080(ctx,base);
        else sub_8210ECF0(ctx,base);
        expected.r3.u64=0x123456789ABCDEF0;expected.f1.f64=42.;
        assert(originals==before+1 && std::memcmp(&ctx,&expected,sizeof(ctx))==0);
    }
    // The enabled path must actually emit all observation boundaries; merely
    // returning early would also pass the read-only-page checks above.
    FILE* read=fopen(name,"r");assert(read);
    std::string log;
    char block[4096];
    while(size_t n=fread(block,1,sizeof(block),read)) log.append(block,n);
    fclose(read);
    if(enabled)
    {
        assert(log.find("dispatch,")!=std::string::npos);
        assert(log.find("render_camera,")!=std::string::npos);
        assert(log.find("native_update_end,")!=std::string::npos);
    }
    else assert(log.empty());
    // An unknown frame pointer is recorded but never dereferenced.
    if(enabled)
    {
        assert(mprotect(base+0x82920000,0x10000,PROT_READ|PROT_WRITE)==0);
        PPC_STORE_U32(game::camera::kPrimary+104,0xDEADBEEF);
        assert(mprotect(base+0x82920000,0x10000,PROT_READ)==0);
        game::crash_pacing::Sample("foreign_frame",base);
        // Unknown session short-circuits before reading its event pointer.
        assert(mprotect(base+0x82840000,0x80000,PROT_READ|PROT_WRITE)==0);
        PPC_STORE_U32(game::codes::kSession,0xDEADBEEF);
        assert(mprotect(base+0x82840000,0x80000,PROT_READ)==0);
        game::crash_pacing::Sample("foreign_session",base);
    }
    assert(munmap(base,PPC_MEMORY_SIZE)==0);
    unlink(name);
    puts("crash pacing: no guest writes; native calls/ABI preserved; disabled probe reads no guest memory");
}

// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Execute the real bounded ring against a synthetic native world serializer.
#include <stdafx.h>
#include <game/rewind_ring.h>
#include <game/rewind_probe.h>
#include <game/rewind_gate.h>
#include <game/crash_launch_status.h>
#include <kernel/write_watch.h>
#include <cpu/guest_thread.h>
#include <cstdlib>
#include <unistd.h>

Memory::Memory() {}
Memory g_memory;
Heap g_userHeap;
static constexpr uint32_t kRecorder=0x82AFE440,kScratch=0x60000000,kInput=0x50000000,kSnapshots=0x52000000;
static uint32_t kEvent=0x8287EC90;
static constexpr uint32_t kTrafficScore=0x82CDFD04;
static constexpr uint32_t kRng=game::rewind::kGame+0x33088;
static unsigned allocations{},frees{},captures{},restores{};
static bool onlineMode{},large{},failedCapture{};
static uint32_t liveToken{},snapshotToken{};
static float loadedClock{};
static uint32_t guestThread=1;
static bool userMode{},rejectTarget{},trackMode{},timeoutMode{},corruptEvent{},corruptScore{},presentationCache{};
static uint32_t corruptOffset{};
static bool corruptCrashGlobals{},lateRoster{};
static bool trackSaveMode{},trackOptionsMode{},trackCancelMode{},drivingSelection{},corruptActor{};
static float expectedSelectionClock=25.f;
static constexpr uint32_t kRacer=0x82CE9630;
static void ActorPayload(uint8_t* base,bool load)
{
    uint32_t cursor=kScratch+3600;
    for(unsigned actor=0;actor<(lateRoster ? 5u:1u);++actor)
        for(uint32_t offset:{13492u,13520u,14780u})
        {
            const unsigned size=offset==13492 ? 2:4;
            const uint32_t address=kRacer+actor*0x5000+offset;
            if(load) memcpy(base+address,base+cursor,size);
            else memcpy(base+cursor,base+address,size);
            cursor+=size;
        }
}
static double sessionSeconds{};
static uint32_t trackSection=0,trackPreparations{},trackPolls{};
static constexpr uint32_t kStreamer=0x82AB6F80+0x227DA0,kSectionOverride=0x82AB6F80+0x228420,kTrackTransition=0x82AB6F80+0x25BFF9;
namespace online { bool Enabled() { return onlineMode; } }
namespace writewatch { GuardStats GetGuardStats() { return {}; } }
namespace report { double SessionSeconds() { return sessionSeconds; } }
uint32_t GuestThread::GetCurrentThreadId() { return guestThread; }
void* Heap::Alloc(size_t size) { assert(size==16*1024*1024+76); ++allocations; return g_memory.base+kScratch; }
void Heap::Free(void* p) { assert(p==g_memory.base+kScratch); ++frees; }
static void Word(uint32_t address,uint32_t value) { auto* base=g_memory.base; PPC_STORE_U32(address,value); }
static uint32_t Word(uint32_t address) { auto* base=g_memory.base; return PPC_LOAD_U32(address); }
static void Float(uint32_t address,float value) { Word(address,std::bit_cast<uint32_t>(value)); }
static float Clock() { return std::bit_cast<float>(Word(game::rewind::kWorldClock)); }
// Model native section preparation independently of actor loading.
extern "C" PPC_FUNC(__imp__sub_82326ED0)
{
    assert(ctx.r3.u32==kStreamer && ctx.r4.u32==1 && ctx.r5.u32==9);
    ctx.r3.u64=trackMode ? trackSection:0;
}
extern "C" PPC_FUNC(__imp__sub_82370CC0)
{
    assert(trackMode && ctx.r3.u32==0x82AB6F80);
    assert(base[kTrackTransition]==1 && base[kRecorder+560]==1 && (Word(kSectionOverride)==0 || Word(kSectionOverride)==3));
}
extern "C" PPC_FUNC(__imp__sub_82326A48)
{
    assert(trackMode && ctx.r3.u32==kStreamer);
    ++trackPreparations;trackPolls=0;
}
extern "C" PPC_FUNC(__imp__sub_82336E28)
{
    assert(trackMode && ctx.r3.u32==kStreamer && !ctx.r4.u32 && !ctx.r5.u32);
    if(timeoutMode && trackPreparations==1) { sessionSeconds+=16;ctx.r3.u64=0;return; }
    ++trackPolls;base[kStreamer+95]=trackPolls==1;
    ctx.r3.u64=trackPolls>1;
}
extern "C" PPC_FUNC(__imp__sub_82120BB0)
{
    assert(trackMode && ctx.r3.u32==kStreamer && trackPolls>=2);
    trackSection=Word(kSectionOverride);ctx.r3.u64=1;
}
extern "C" PPC_FUNC(__imp__sub_82345648)
{
    assert(ctx.r3.u32==kScratch && ctx.r4.u32==kScratch+76 && ctx.r5.u32==16*1024*1024);
    ++captures;
    if (failedCapture) { ctx.r3.u64=0; return; }
    const uint32_t size=large ? 4*1024*1024:4096;
    Word(kScratch,kScratch+76);Word(kScratch+4,size);
    Word(kScratch+56,Word(game::rewind::kWorldClock));
    Word(kScratch+72,trackMode ? trackSection:0);
    for (unsigned i=0;i<2;++i) Word(kScratch+76+i*4,Word(kRng+i*4));
    Word(kScratch+84,Word(kEvent+3056));Word(kScratch+88,liveToken);
    ActorPayload(base,false);
    memcpy(base+kScratch+92,base+kEvent,3488);
    Word(kScratch+3800,Word(kTrafficScore));
    if(kEvent==game::rewind::kGame+0x36D20) memcpy(base+kScratch+3804,base+0x828E4748,112);
    ctx.r3.u64=1;ctx.r8.u64=0xFFFF;ctx.f1.f64=100;
}
extern "C" PPC_FUNC(__imp__sub_823456A8)
{
    assert(ctx.r3.u32==kScratch && Word(kScratch)==kScratch+76);
    ++restores;
    if(trackMode)
    {
        assert(trackSection==((timeoutMode || trackSaveMode || trackOptionsMode) ? 3u:0u) && trackPreparations==((timeoutMode || trackSaveMode || trackOptionsMode) ? 2u:1u) && trackPolls==2);
        assert(Word(kSectionOverride)==7 && base[kTrackTransition]==0 && base[kRecorder+560]==0);
    }
    loadedClock=std::bit_cast<float>(Word(kScratch+56));
    if(timeoutMode || trackSaveMode || trackOptionsMode) assert(loadedClock==40.f);
    else if(drivingSelection) assert(loadedClock==expectedSelectionClock);
    else if (!rejectTarget && !corruptEvent && !corruptScore && !corruptActor && !corruptCrashGlobals) assert(loadedClock==29.f+float(restores));
    for (unsigned i=0;i<2;++i) Word(kRng+i*4,Word(kScratch+76+i*4));
    memcpy(base+kEvent,base+kScratch+92,3488);
    Word(kTrafficScore,Word(kScratch+3800));
    if(kEvent==game::rewind::kGame+0x36D20) memcpy(base+0x828E4748,base+kScratch+3804,112);
    if(corruptCrashGlobals && restores==1) base[0x828E4780]^=1;
    ActorPayload(base,true);
    if(corruptActor && restores==1) Word(kRacer+(lateRoster ? 3*0x5000:0)+14780,0);
    if(lateRoster && !corruptActor && restores==1)
    {
        // The fourth eliminated rival is active again at 30 seconds. The
        // earlier three eliminations remain, leaving three active cars.
        assert(Word(kRacer+3*0x5000+14780)==UINT32_MAX && Word(kRacer+3*0x5000+13520)==2);
        for(unsigned i=0;i<3;++i) assert(Word(kRacer+i*0x5000+14780)==0);
        assert(Word(kRacer+4*0x5000+14780)==UINT32_MAX);
    }
    if(corruptScore && restores==1) Word(kTrafficScore,Word(kTrafficScore)+1);
    if(corruptEvent && restores==1) base[kEvent+corruptOffset]^=1;
    if(presentationCache)
    {
        Word(kEvent+2992,0x13579BDF); // r6=0: counted but not saved by the native helper.
        Word(kEvent+3028,0x12345678);Word(kEvent+3032,0x87654321);
    }
    Word(kEvent+3056,Word(kScratch+84));snapshotToken=Word(kScratch+88);
    Float(game::rewind::kWorldClock,loadedClock);
    base[game::rewind::kWorldPlayback]=1;
    ctx.r3.u64=rejectTarget && restores==1 ? 0:1;ctx.r8.u64=0xABAB;ctx.f1.f64=-10;
}
static void Map(uint32_t address,size_t size)
{ assert(mprotect(g_memory.base+address,size,PROT_READ|PROT_WRITE)==0); }
static void Call()
{
    PPCContext ctx{};ctx.r1.u32=0x70000000;ctx.r3.u64=0x12345;ctx.lr=0x8210EF58;
    const auto before=ctx;
    game::rewind::ring::AtBoundary(ctx,g_memory.base,kRecorder);
    if(trackMode && game::rewind::ring::GetStatus().state==game::rewind::ring::State::Restoring)
    {
        const unsigned beforeRestore=restores;
        for(unsigned frame=0;frame<8 && game::rewind::ring::Preparing(kRecorder);++frame)
        {
            if(!frame)
            {
                if(trackSaveMode) { game::rewind::gate::BeginWrite();game::rewind::gate::EndWrite(); }
                if(trackOptionsMode) game::rewind::ring::SetWorldOptions(1);
                if(trackCancelMode) Word(game::rewind::kPhase,0);
            }
            game::rewind::ring::AtWorldBoundary(ctx,g_memory.base);
            if(!frame) assert(restores==beforeRestore);
        }
        assert(!game::rewind::ring::Preparing(kRecorder) && restores==beforeRestore+(trackCancelMode ? 0:1));
    }
    assert(memcmp(&ctx,&before,sizeof(ctx))==0);
}
int main(int argc,char** argv)
{
    const std::string mode=argc>1 ? argv[1]:"sequence";
    timeoutMode=mode=="track_timeout";
    trackSaveMode=mode=="track_save";trackOptionsMode=mode=="track_options";trackCancelMode=mode=="track_event";
    const bool eventCase=mode.starts_with("contract_");
    presentationCache=mode=="contract_eliminator_cache";
    corruptScore=mode=="contract_score_bad";
    corruptCrashGlobals=mode=="contract_crash_globals_bad";
    lateRoster=mode=="contract_eliminator_late" || mode=="contract_eliminator_late_bad";
    corruptActor=mode=="contract_eliminator_actor_bad" || mode=="contract_eliminator_late_bad";
    corruptEvent=eventCase && mode.ends_with("_bad") && !corruptScore && !corruptActor && !corruptCrashGlobals;
    if(mode.starts_with("contract_crash")) { kEvent=game::rewind::kGame+0x36D20;corruptOffset=mode=="contract_crash_parent_bad" ? 156:mode=="contract_crash_nested_bad" ? 2528:mode=="contract_crash_pointer_bad" ? 2836:2764; }
    else if(mode.starts_with("contract_traffic") || corruptScore) { kEvent=game::rewind::kGame+0x37AC0;corruptOffset=3000; }
    else if(mode.starts_with("contract_lap")) { kEvent=game::rewind::kGame+0x39F10;corruptOffset=3016; }
    else if(mode.starts_with("contract_eliminator")) { kEvent=game::rewind::kGame+0x3AAE0;corruptOffset=3020; }
    else if(eventCase) corruptOffset=2392;
    if(mode=="crash_meter" || mode=="crash_special" || mode=="crash_party" || mode=="contract_crash_early") kEvent=game::rewind::kGame+0x36D20;
    unsetenv("BURNOUT_REWIND_MODES"); // Player settings need no developer mode switch.
    drivingSelection=mode=="driving_target" || mode=="camera_wait";
    if(mode=="camera_wait") expectedSelectionClock=30.f;
    userMode=mode=="crash_meter" || mode=="finished" || mode=="crash_special" || mode=="crash_party" || drivingSelection || mode=="camera_wait_save" || mode=="camera_wait_timeout" || mode=="no_driving_target" || trackSaveMode || trackOptionsMode || trackCancelMode || eventCase || mode=="user" || mode=="rollback" || mode=="save" || timeoutMode;
    rejectTarget=mode=="rollback";
    trackMode=mode=="track" || timeoutMode || trackSaveMode || trackOptionsMode || trackCancelMode;
    setenv("BURNOUT_REWIND_RING",mode=="disabled" || userMode ? "0":mode=="invalid" ? "30junk":mode=="budget" ? "30":"sequence",1);
    unsetenv("BURNOUT_REWIND_TRIAL");
    if (mode=="trial") setenv("BURNOUT_REWIND_TRIAL","2",1);
    if (mode=="thread") guestThread=2;
    g_memory.base=static_cast<uint8_t*>(mmap(nullptr,PPC_MEMORY_SIZE,PROT_NONE,MAP_PRIVATE|MAP_ANON,-1,0));
    assert(g_memory.base!=MAP_FAILED);
    if (mode=="disabled" || mode=="invalid" || mode=="trial" || mode=="thread")
    { Call();assert(!allocations && !captures);assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0; }
    Map(0x82840000,0x80000);Map(0x82AB0000,0x10000);Map(0x82AF0000,0x10000);
    if(kEvent==game::rewind::kGame+0x36D20) Map(0x828E0000,0x10000);
    Map(0x82B50000,0x10000);Map(0x82CC0000,0x10000);
    Map(0x82CD0000,0x40000);Map(0x82D10000,0x10000);Map(kScratch,17*1024*1024);
    if(trackMode)
    {
        Map(kStreamer&~0xFFFFu,0x10000);
        Word(kSectionOverride,7);
    }
    Word(0x82CC7FA0+6*4,0x82B552D0);Word(0x82CE0030+13520,2);
    Word(0x82CDFFE8,2);Word(0x82CDFFF4,0x82CE0030);Word(0x82CDFFF8,kRacer);
    Word(0x82CC7FA0+1*4,0x82B55300);g_memory.base[kRacer+15480]=1;
    Word(kRacer+14780,UINT32_MAX);Word(kRacer+13520,2);
    g_memory.base[0x82CE0030+15480]=6; // respawn changed the physical ID, not the local participant.
    if(lateRoster)
    {
        Word(0x82CDFFE8,6);
        for(unsigned i=0;i<5;++i)
        {
            const uint32_t racer=kRacer+i*0x5000,wrapper=0x82B56000+i*0x1000;
            Word(0x82CDFFF8+i*4,racer);g_memory.base[racer+15480]=uint8_t(i+1);
            Word(0x82CC7FA0+(i+1)*4,wrapper);
        }
    }
    using namespace game::rewind;
    if (userMode) ring::SetEnabled(true);
    namespace codes=game::codes;
    Word(codes::kSession,codes::kSoloSession);Word(codes::kSoloSession+4,kEvent);
    Word(codes::kSoloSession+8,0x82897FA8);Word(codes::kLocalPlayers,1);Word(kPhase,5);Word(kClockFlags,0x01000000);
    if(kEvent==kGame+0x36D20) g_memory.base[codes::kSpecialMode]=1;
    if(mode=="crash_party") Word(codes::kSoloSession+8,codes::kPartyController);
    if(mode=="crash_special") g_memory.base[codes::kSpecialMode]=2;
    for (auto [offset,buffer]:{std::pair{116u,kInput},std::pair{140u,kSnapshots}})
    {
        Word(kRecorder+offset+4,buffer);Word(kRecorder+offset+16,0x1000000);
        Word(kRecorder+offset+8,buffer);Word(kRecorder+offset+12,buffer);
    }
    onlineMode=mode=="online" || mode=="signedin";
    if(mode=="online") Word(codes::kSession,0x8287C718);
    large=mode=="budget";
    failedCapture=mode=="failed";
    if (mode=="replay") Word(kClockFlags,0x01010000);
    if (mode=="event") Word(codes::kSoloSession+4,kGame+0x37000);
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
    if(mode=="crash_meter")
    {
        game::crash_launch::rewindBlocked.store(true);
        for (unsigned t=1;t<=15;++t) { Float(kWorldClock,float(t));Call(); }
        assert(!allocations && !captures && !ring::Request(10));
        assert(ring::GetStatus().state==ring::State::Collecting);
        game::crash_launch::rewindBlocked.store(false);
        Float(kWorldClock,16);Call();assert(captures==1 && allocations==1);
        assert(!ring::Request(10)); // No snapshot from the launch mini-game.
        ring::SetEnabled(false);Call();assert(frees==1);
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if(mode=="crash_special" || mode=="crash_party")
    {
        Float(kWorldClock,20.f);Call();
        assert(!allocations && !captures && !restores);
        assert(ring::GetStatus().state==ring::State::Unsupported && !ring::Request(10));
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if(mode=="contract_crash_early")
    {
        Float(kWorldClock,.5f);Call();assert(!captures);
        Float(kWorldClock,1.f);Call();assert(captures==1 && allocations==1);
        ring::SetEnabled(false);Call();assert(frees==1);
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if(mode=="finished")
    {
        for(unsigned t=5;t<=20;++t) { Float(kWorldClock,float(t));Call(); }
        assert(ring::Request(10));
        Word(0x82CE0030+13520,3);Call();
        assert(!restores && frees==1 && ring::TakeOutcome()==-1);
        assert(ring::GetStatus().state==ring::State::Unsupported && !ring::Request(10));
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if (mode=="save")
    {
        Float(kWorldClock,20.f);Call();assert(captures==1);
        const auto old=ring::GetStatus().epoch;
        gate::BeginWrite();Call();assert(frees==1 && ring::GetStatus().state==ring::State::Saving);
        gate::EndWrite();Call();assert(allocations==2 && ring::GetStatus().epoch!=old);
        ring::SetWorldOptions(1);Call();assert(allocations==3 && frees==2);
        ring::SetEnabled(false);Call();assert(frees==3 && ring::GetStatus().state==ring::State::Disabled);
        assert(!ring::Request(10));
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if(mode=="camera_wait" || mode=="camera_wait_save" || mode=="camera_wait_timeout")
    {
        for(unsigned t=5;t<=40;++t)
        {
            Float(kWorldClock,float(t));
            Word(kRecorder+532,t*60);Word(kRecorder+528,t*60);Call();
        }
        g_memory.base[0x82B552D0+599]=1;
        assert(ring::Request(10));Call();
        assert(!restores && ring::GetStatus().state==ring::State::Restoring && !ring::Preparing(kRecorder));
        if(mode=="camera_wait_save")
        {
            gate::BeginWrite();gate::EndWrite();Call();
            assert(!restores && ring::TakeOutcome()==-1 && frees==1);
        }
        else if(mode=="camera_wait_timeout")
        {
            sessionSeconds=31;Call();
            assert(!restores && Clock()==40.f && ring::TakeOutcome()==-1);
            assert(ring::GetStatus().state==ring::State::Ready);
        }
        else
        {
            // Simulation continues, the native camera ends, then settles for
            // half a second. The target stays 40-10, not 41.5-10.
            Float(kWorldClock,40.5f);sessionSeconds=.5;Call();assert(!restores);
            g_memory.base[0x82B552D0+599]=0;
            for(float t:{41.f,41.25f})
            { Float(kWorldClock,t);sessionSeconds=t-40;Call();assert(!restores); }
            Float(kWorldClock,41.5f);sessionSeconds=1.5;Call();
            assert(restores==1 && Clock()==30.f && Word(kRecorder+532)==1800 && ring::TakeOutcome()==11500);
        }
        ring::SetEnabled(false);Call();
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    if(drivingSelection || mode=="no_driving_target")
    {
        // The requested 30s point is in Aftertouch. A ten-second request at
        // 40s must choose 25s (and report 15s), never the unsafe 26..32s poses.
        for(unsigned t=5;t<=40;++t)
        {
            Float(kWorldClock,float(t));
            g_memory.base[0x82B552D0+599]=mode=="no_driving_target" || (t>=26 && t<=32);
            Word(kRecorder+532,t*60);Word(kRecorder+528,t*60);
            Call();
        }
        if(drivingSelection)
        {
            assert(ring::Request(10));Call();
            assert(restores==1 && Clock()==25.f && Word(kRecorder+532)==1500 && ring::TakeOutcome()==15000);
        }
        else
        {
            assert(ring::GetStatus().state==ring::State::Collecting);
            assert(!ring::Request(10) && !restores && Clock()==40.f);
        }
        ring::SetEnabled(false);Call();assert(frees==1);
        assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
    }
    float now=5;
    uint32_t previousTokens[3]{};
    for (unsigned tick=0;tick<500;++tick)
    {
        Float(kWorldClock,now);
        for(unsigned i=0;i<3488;++i) g_memory.base[kEvent+i]=uint8_t(tick*7+i);
        if(kEvent==kGame+0x36D20) for(unsigned i=0;i<112;++i) g_memory.base[0x828E4748+i]=uint8_t(tick+i*3);
        Float(kEvent+3056,90.f+float(tick));Word(kTrafficScore,100*tick);
        Word(kRng,1000+tick);Word(kRng+4,2000+tick);liveToken=tick+100;
        Word(kRacer+14780,UINT32_MAX-tick);Word(kRacer+13520,2+tick%2);
        if(lateRoster)
            for(unsigned i=0;i<5;++i)
            {
                const uint32_t racer=kRacer+i*0x5000;
                const bool eliminated=now>=float(i==4 ? 70:10*(i+1));
                Word(racer+14780,eliminated ? 0:UINT32_MAX);
                Word(racer+13520,eliminated ? 3:2);
                auto* base=g_memory.base;PPC_STORE_U16(racer+13492,eliminated ? i+1:65535);
            }
        const auto frame=uint32_t(now*60);
        Word(kRecorder+532,frame);Word(kRecorder+528,frame);
        Word(kRecorder+540,unsigned(now)/15);Float(kRecorder+572,float(unsigned(now)/15*15));
        for (auto [offset,buffer]:{std::pair{116u,kInput},std::pair{140u,kSnapshots}})
        {
            Word(kRecorder+offset+8,buffer+frame);Word(kRecorder+offset+12,buffer+frame);
        }
        if (mode=="buffer" && tick==20) Word(kRecorder+116+4,kInput+0x1000);
        if(trackMode && now>=40 && !trackPreparations) trackSection=3;
        const auto before=restores;
        if (userMode && before<3 && now>=float(40+11*before) && (!(rejectTarget || corruptEvent || corruptScore || corruptActor || corruptCrashGlobals) || before==0))
        {
            assert(!ring::Request(15));
            assert(ring::Request(10*(before+1)));
            assert(!ring::Request(10)); // single pending request
        }
        Call();
        if(trackCancelMode && frees)
        {
            assert(!restores && frees==1 && Word(kSectionOverride)==7 && g_memory.base[kRecorder+560]==0 && g_memory.base[kTrackTransition]==0);
            assert(ring::GetStatus().state==ring::State::Unsupported && ring::TakeOutcome()==-1);
            assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);return 0;
        }
        if((timeoutMode || trackSaveMode || trackOptionsMode) && restores)
        {
            assert(restores==1 && trackPreparations==2 && Clock()==now && ring::TakeOutcome()==-1);
            assert(ring::GetStatus().state==(timeoutMode ? ring::State::Failed:ring::State::Collecting) && frees==1);
            if(trackSaveMode || trackOptionsMode)
            {
                Float(kWorldClock,now+.25f);Call();assert(allocations==2 && frees==1);
                ring::SetEnabled(false);Call();assert(frees==2);
            }
            break;
        }
        if ((rejectTarget || corruptEvent || corruptScore || corruptActor || corruptCrashGlobals) && restores)
        {
            assert(restores==2 && Clock()==now && ring::TakeOutcome()==-1);
            assert(ring::GetStatus().state==ring::State::Failed && frees==1);
            assert(!ring::Request(10));break;
        }
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
            if (userMode) assert(ring::TakeOutcome()==int(10000*restores));
        }
        now+=.25f;
    }
    if (userMode && !rejectTarget && !corruptEvent && !corruptScore && !corruptActor && !corruptCrashGlobals && !timeoutMode && !trackSaveMode && !trackOptionsMode) { ring::SetEnabled(false);Call(); }
    if(timeoutMode || trackSaveMode || trackOptionsMode) assert(restores==1 && frees==(timeoutMode ? 1u:2u));
    else if (mode=="signedin" || mode=="sequence" || mode=="user" || (eventCase && !corruptEvent && !corruptScore && !corruptActor && !corruptCrashGlobals) || trackMode) assert(restores==3 && captures>60 && allocations==1 && frees==1);
    else if (rejectTarget || corruptEvent || corruptScore || corruptActor || corruptCrashGlobals) assert(restores==2 && frees==1);
    else if (mode=="budget" || mode=="buffer" || mode=="failed") assert(!restores && allocations==1 && frees==1);
    else assert(!restores && !allocations && !captures);
    assert(munmap(g_memory.base,PPC_MEMORY_SIZE)==0);
    puts("rewind ring: native capture, 10/20/30 selection, new branches, bounds and exclusions passed");
}

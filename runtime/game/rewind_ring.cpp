// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "rewind_ring.h"
#include "rewind_probe.h"
#include "rewind_gate.h"
#include "rewind_copy.h"
#include <cpu/guest_thread.h>
#include <user/online.h>
#include <kernel/write_watch.h>
#include <report/report.h>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <ctime>

extern "C" PPC_FUNC(__imp__sub_82345648);
extern "C" PPC_FUNC(__imp__sub_823456A8);
extern "C" PPC_FUNC(__imp__sub_82326ED0);
extern "C" PPC_FUNC(__imp__sub_82326A48);
extern "C" PPC_FUNC(__imp__sub_82336E28);
extern "C" PPC_FUNC(__imp__sub_82370CC0);
extern "C" PPC_FUNC(__imp__sub_82120BB0);

namespace game::rewind::ring
{
    namespace
    {
        constexpr uint32_t kRecorder=0x82AFE440, kRoadRage=kGame+0x386B0;
        constexpr uint32_t kHeader=76, kPayloadLimit=16*1024*1024, kRng=kGame+0x33088;
        constexpr size_t kHistoryLimit=80*1024*1024;
        constexpr uint32_t kWorld=0x82AB6F80,kStreamer=kWorld+0x227DA0;
        // sub_82342AF0 -> sub_8233EAE0 -> sub_8232EAB8 saves player 0's
        // Traffic Attack award total, also updated by sub_82324D58.
        constexpr uint32_t kTrafficScore=kWorld+0x228D84;
        constexpr uint32_t kSectionOverride=kWorld+0x228420,kTrackTransition=kWorld+0x25BFF9;
        struct ContextGuard
        {
            PPCContext& ctx; PPCContext saved;
            explicit ContextGuard(PPCContext& context) : ctx(context), saved(context) {}
            ~ContextGuard() { ctx=saved; }
        };
        struct Stream
        {
            uint32_t buffer{}, capacity{}, cursor{}, end{};
            static Stream Read(uint8_t* base,uint32_t address)
            {
                return {PPC_LOAD_U32(address+4),PPC_LOAD_U32(address+16),
                    PPC_LOAD_U32(address+8),PPC_LOAD_U32(address+12)};
            }
            bool Valid() const
            {
                return buffer && capacity && uint64_t(buffer)+capacity<=UINT32_MAX &&
                    buffer<=cursor && cursor<=end && uint64_t(end)<=uint64_t(buffer)+capacity;
            }
            bool SameBuffer(const Stream& other) const
            {
                return Valid() && other.Valid() && buffer==other.buffer && capacity==other.capacity;
            }
            void Restore(uint8_t* base,uint32_t address) const
            {
                PPC_STORE_U32(address+8,cursor); PPC_STORE_U32(address+12,end);
            }
        };
        // USA stream helper sub_820BF2D8 copies a field only when r6 intersects
        // stream+56. Event serializers pass 2 for saved fields; their r6=0
        // calls only count bytes and do not copy the pointed-to objects.
        template<class Visit> void EventRanges(uint32_t event,Visit visit)
        {
            if(event==kGame+0x36D20)
            {
                // r6=2 fields in sub_8213F4B0, its base (82137410),
                // nested 8212FF08/8212EAB8, six 82131A50 entries,
                // two 821319B8 entries, vector 822EF370 and pointer helpers.
                // Unsaved padding, vtables and r6=0 fields are excluded.
                for(auto [offset,size]:std::array<std::pair<uint32_t,uint32_t>,11>{{
                    {16,32},{48,4},{56,13},{188,12},{204,32},{316,1},
                    {320,4},{108,74},{183,4},{236,38},{2608,52}}}) visit(offset,size);
                for(uint32_t i=0;i<6;++i)
                { visit(2388+i*36,22);visit(2412+i*36,12); }
                for(uint32_t i=0;i<2;++i) visit(2672+i*32,19);
                visit(2836,12);
                for(auto [offset,size]:std::array<std::pair<uint32_t,uint32_t>,8>{{
                    {2604,1},{2764,72},{2848,47},{2896,21},{2920,12},
                    {3480,8},{2964,512},{2384,4}}}) visit(offset,size);
                return;
            }
            for(auto [offset,size]:std::array<std::pair<uint32_t,uint32_t>,9>{{
                {16,32},{48,4},{56,13},{188,4},{204,32},{316,1},
                {2384,4},{2392,52},{2984,8}}}) visit(offset,size);
            if(event==kGame+0x37AC0) // sub_8213C108: Traffic Attack.
            {
                for(auto [offset,size]:std::array<std::pair<uint32_t,uint32_t>,5>{{
                    {2992,4},{2996,1},{3000,4},{3032,1},{3036,8}}}) visit(offset,size);
            }
            else if(event==kGame+0x39F10) visit(3016,1); // sub_8213C2C8: Burning Lap.
            else if(event==kGame+0x3AAE0) // sub_8213C400: Eliminator.
            { visit(3020,8);visit(3036,2); }
        }
        struct EventState
        {
            std::array<uint8_t,2048> bytes{};
            uint32_t size{};
            void Read(uint8_t* base,uint32_t event)
            {
                size=0;
                EventRanges(event,[&](uint32_t offset,uint32_t count) {
                    assert(size+count<=bytes.size());
                    memcpy(bytes.data()+size,base+event+offset,count);size+=count;
                });
            }
            bool Matches(uint8_t* base,uint32_t event) const
            {
                uint32_t cursor=0;bool same=true;
                EventRanges(event,[&](uint32_t offset,uint32_t count) {
                    if(cursor+count>size || memcmp(bytes.data()+cursor,base+event+offset,count))
                    {
                        if(same)
                        {
                            uint32_t first=0;
                            while(cursor+first<size && first<count && bytes[cursor+first]==base[event+offset+first]) ++first;
                            fprintf(stderr,"[rewind-ring] event mismatch event=%08X offset=%u bytes=%u\n",event,offset+first,count);
                        }
                        same=false;
                    }
                    cursor+=count;
                });
                return same && cursor==size;
            }
        };
        std::atomic<uint32_t> auditCamera{0};
        bool AuditEnabled()
        {
            static const bool enabled=[] { const char* v=std::getenv("BURNOUT_REWIND_AUDIT");return v && std::string_view(v)=="1"; }();
            return enabled;
        }
        constexpr uint32_t kLocalParticipant=0x82CE0030;
        // Physical wrapper table ends at sub_8223D0C8's adjacent table (+9830).
        constexpr int32_t kPhysicalSlots=9830-9776;
        struct ActorAudit
        {
            uint32_t racer{},wrapper{},body{},disabled{},running{},rank{},position{},flags{},mode{};
            bool operator==(const ActorAudit&) const = default;
        };
        struct RosterAudit
        {
            uint32_t count{};
            std::array<ActorAudit,8> actors{};
            static RosterAudit Read(uint8_t* base)
            {
                RosterAudit r;
                r.count=PPC_LOAD_U32(kWorld+0x229068);
                if(r.count>r.actors.size()) return r;
                // sub_8232DA90 returns the race participant. Its signed ID is
                // passed to sub_8223D0B0 by Eliminator sub_8213C4B8.
                for(uint32_t i=0;i<r.count;++i)
                {
                    auto& a=r.actors[i];
                    a.racer=PPC_LOAD_U32(kWorld+((9*65536-23523+i)*4));
                    if(!a.racer) continue;
                    const int32_t id=int8_t(PPC_LOAD_U8(a.racer+15480));
                    if(id<0 || id>=kPhysicalSlots) continue;
                    a.wrapper=PPC_LOAD_U32(kWorld+0x47760+((7*65536+9776+id)*4));
                    a.running=PPC_LOAD_U32(a.racer+14780);
                    a.rank=PPC_LOAD_U16(a.racer+13492);
                    a.position=PPC_LOAD_U16(a.racer+13680);
                    a.mode=PPC_LOAD_U32(a.racer+13520);
                    if(a.wrapper)
                    {
                        a.body=PPC_LOAD_U32(a.wrapper+2664);
                        a.disabled=PPC_LOAD_U8(a.wrapper+599);
                        if(a.body) a.flags=PPC_LOAD_U8(a.body+12894);
                    }
                }
                return r;
            }
            void Log(const char* label,float clock) const
            {
                fprintf(stderr,"[rewind-audit] roster label=%s clock=%.6f count=%u\n",label,clock,count);
                for(uint32_t i=0;i<std::min<uint32_t>(count,actors.size());++i)
                {
                    const auto& a=actors[i];
                    fprintf(stderr,"[rewind-audit] actor label=%s clock=%.6f index=%u racer=%08X wrapper=%08X body=%08X disabled=%u running=%u rank=%u position=%u flags=%02X mode=%u\n",
                        label,clock,i,a.racer,a.wrapper,a.body,a.disabled,a.running,a.rank,a.position,a.flags,a.mode);
                }
            }
            bool Matches(const RosterAudit& other) const
            {
                if(!count || count>actors.size() || count!=other.count) return false;
                for(uint32_t i=0;i<count;++i)
                {
                    const auto& a=actors[i];const auto& b=other.actors[i];
                    if(!a.racer || !a.wrapper || a.racer!=b.racer || a.wrapper!=b.wrapper ||
                        a.running!=b.running || a.rank!=b.rank || a.mode!=b.mode) return false;
                }
                return true;
            }
            bool operator==(const RosterAudit&) const = default;
        };
        uint32_t PlayerWrapper(uint8_t* base)
        {
            // Local participant 0 can switch physical wrappers on respawn.
            const int32_t id=int8_t(PPC_LOAD_U8(kLocalParticipant+15480));
            if(id<0 || id>=kPhysicalSlots) return 0;
            return PPC_LOAD_U32(kWorld+0x47760+((7*65536+9776+id)*4));
        }
        // Replay loading rebuilds the ordinary chase camera, not the transient
        // Aftertouch/cinematic controller. Select an earlier driving point.
        bool DrivablePoint(uint8_t* base)
        {
            const uint32_t wrapper=PlayerWrapper(base);
            return wrapper && !PPC_LOAD_U8(wrapper+599) && PPC_LOAD_U32(kLocalParticipant+13520)==2;
        }
        struct CameraAudit
        {
            uint32_t camera{},frame{};
            std::array<uint32_t,8> words{};
            static CameraAudit Read(uint8_t* base)
            {
                CameraAudit c;c.camera=auditCamera.load(std::memory_order_relaxed);
                if(c.camera) c.frame=PPC_LOAD_U32(c.camera+104);
                if(c.frame)
                {
                    unsigned i=0;
                    for(uint32_t offset:{16u,32u,36u,40u,44u,48u,52u,56u}) c.words[i++]=PPC_LOAD_U32(c.frame+offset);
                }
                return c;
            }
            void Log(const char* label,float clock) const
            {
                fprintf(stderr,"[rewind-audit] camera_point label=%s clock=%.6f camera=%08X frame=%08X",label,clock,camera,frame);
                unsigned i=0;
                for(uint32_t offset:{16u,32u,36u,40u,44u,48u,52u,56u})
                    fprintf(stderr," f%u=%.8g",offset,std::bit_cast<float>(words[i++]));
                fprintf(stderr,"\n");
            }
        };
        void AuditWorld(uint8_t* base)
        {
            if(!AuditEnabled() || PPC_LOAD_U32(kPhase)!=5) return;
            static thread_local double next{};
            const double now=report::SessionSeconds();if(now<next) return;next=now+.1;
            const float clock=std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
            const uint32_t camera=auditCamera.load(std::memory_order_relaxed);
            const uint32_t frame=camera ? PPC_LOAD_U32(camera+104):0;
            fprintf(stderr,"[rewind-audit] world session=%.6f clock=%.6f camera=%08X frame=%08X playback=%u seeking=%u",
                now,clock,camera,frame,PPC_LOAD_U8(kWorldPlayback),PPC_LOAD_U8(kRecorder+560));
            if(frame)
                for(uint32_t offset:{16u,32u,36u,40u,44u,48u,52u,56u})
                    fprintf(stderr," f%u=%.8g",offset,std::bit_cast<float>(PPC_LOAD_U32(frame+offset)));
            const uint32_t wrapper=PlayerWrapper(base);
            if(wrapper)
                fprintf(stderr," player_disabled=%u player_state=%u player_mode=%u dt=%.8g",
                    PPC_LOAD_U8(wrapper+599),PPC_LOAD_U8(wrapper+624),PPC_LOAD_U32(kLocalParticipant+13520),
                    std::bit_cast<float>(PPC_LOAD_U32(kWorld+32)));
            fprintf(stderr,"\n");
            const auto session=PPC_LOAD_U32(codes::kSession);
            if(session==codes::kSoloSession && PPC_LOAD_U32(session+4)==kGame+0x3AAE0)
                RosterAudit::Read(base).Log("world",clock);
            if(session==codes::kSoloSession && PPC_LOAD_U32(session+4)==kGame+0x36D20)
            {
                fprintf(stderr,"[rewind-audit] crash session=%.6f clock=%.6f",now,clock);
                for(uint32_t offset:{2384u,2604u,2764u,2768u,2772u,2780u,2800u,2880u,2896u,2912u,2920u})
                    fprintf(stderr," e%u=%08X",offset,PPC_LOAD_U32(kGame+0x36D20+offset));
                fprintf(stderr,"\n");
            }
            static const bool route=std::getenv("BURNOUT_REWIND_ROUTE_AUDIT")!=nullptr;
            if(route)
            {
                const auto roster=RosterAudit::Read(base);
                for(uint32_t i=0;i<std::min<uint32_t>(roster.count,roster.actors.size());++i)
                {
                    const auto& a=roster.actors[i];if(!a.racer) continue;
                    fprintf(stderr,"[rewind-route] session=%.6f clock=%.6f index=%u mode=%u",now,clock,i,a.mode);
                    for(uint32_t o=16;o<=76;o+=4)
                        fprintf(stderr," f%u=%.8g",o,std::bit_cast<float>(PPC_LOAD_U32(a.racer+o)));
                    fprintf(stderr,"\n");
                }
            }
        }
        struct Point
        {
            std::vector<uint8_t> bytes;
            Stream input,snapshots;
            EventState eventState;
            RosterAudit roster;
            CameraAudit camera;
            // The base and Crash serializers also copy two adjacent globals.
            std::array<uint8_t,112> crashGlobals{};
            uint32_t frame{},replayFrame{},count{},entries[18]{},lastNativeClock{},rng[2]{},deadline{},trafficScore{};
            float clock{};
            bool drivable{};
        };
        struct History
        {
            std::deque<Point> points;
            std::vector<uint8_t> spare;
            size_t bytes{};
            uint32_t guest{},event{},controller{},thread{};
            float lastClock{},nextCapture{},drivableSince{-1.f},requestedClock{};
            bool waitingForCamera{};
            double cameraWaitStarted{};
            unsigned completed{};
            bool finished{},preparing{},recovering{},invalidated{};
            size_t restoreIndex{};
            unsigned restoreSeconds{};
            float restoreFrom{};
            Point backup;
            uint32_t previousSection{},fromSection{},targetSection{};
            uint8_t previousTransition{},previousSeeking{};
            std::chrono::steady_clock::time_point restoreStarted{};
            double trackStarted{};
            uint64_t saveEpoch{};
            uint32_t options{};
        };
        thread_local History s;
        std::atomic<bool> enabled{false};
        std::atomic<uint32_t> worldOptions{0},timeline{1};
        std::atomic<uint64_t> published{0},request{0};
        std::atomic<int> outcome{0};
        void Publish(State state,float now=0)
        {
            unsigned mask=0,seconds=0;
            if (!s.points.empty() && state==State::Ready)
            {
                const auto first=std::find_if(s.points.begin(),s.points.end(),[](const Point& p) { return p.drivable; });
                if(first!=s.points.end()) seconds=unsigned(std::max(0.f,now-first->clock));
                for (unsigned i=0;i<3;++i) if (seconds>=10*(i+1)) mask|=1u<<i;
                if (!mask) state=State::Collecting;
            }
            published.store((uint64_t(timeline.load(std::memory_order_relaxed))<<32)|
                (uint64_t(std::min(seconds,255u))<<16)|(mask<<8)|unsigned(state),std::memory_order_release);
        }
        bool FastCopy()
        {
            static const bool fast=[] {const char* v=std::getenv("BURNOUT_REWIND_FAST_COPY");return !v || std::string_view(v)!="0";}();
            return fast;
        }
        bool VerifyCopy()
        {
            static const bool verify=[] {const char* v=std::getenv("BURNOUT_REWIND_VERIFY_COPY");return v && std::string_view(v)=="1";}();
            return verify;
        }
        double ThreadMilliseconds()
        {
#if defined(__APPLE__) || defined(__linux__)
            timespec t{};
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID,&t)==0) return double(t.tv_sec)*1000.+double(t.tv_nsec)/1e6;
#endif
            return 0.;
        }
        bool SupportedEvent(uint32_t event)
        {
            // USA native world serializer dispatch, sub_823424A0. These six
            // solo World Tour event types have separate checked load contracts.
            for(uint32_t offset:{0x36170u,0x36D20u,0x37AC0u,0x386B0u,0x39F10u,0x3AAE0u})
                if(event==kGame+offset) return true;
            return false;
        }
        unsigned Duration()
        {
            static const unsigned duration=[] {
                const char* value=std::getenv("BURNOUT_REWIND_RING");
                const char* trial=std::getenv("BURNOUT_REWIND_TRIAL");
                if (!value || (trial && *trial && std::string_view(trial)!="0")) return 0u;
                const std::string_view v(value);
                return v=="10" ? 10u : v=="20" ? 20u : v=="30" ? 30u : v=="sequence" ? 1u : v=="capture" ? 2u : v=="reverse" ? 4u : 0u;
            }();
            return duration;
        }
        float Interval()
        {
            if (!Duration()) return 1.f;
            static const float interval=[] {
                const char* v=std::getenv("BURNOUT_REWIND_INTERVAL");
                return v && std::string_view(v)=="0.5" ? .5f:v && std::string_view(v)=="2" ? 2.f:1.f;
            }();
            return interval;
        }
        void Clear(uint8_t* base,const char* reason,bool finished=false)
        {
            if (s.guest)
            {
                g_userHeap.Free(base+s.guest);
                fprintf(stderr,"[rewind-ring] clear: %s points=%zu bytes=%zu\n",reason,s.points.size(),s.bytes);
            }
            const unsigned completed=s.completed;
            s=History{}; s.completed=completed; s.finished=finished;
            timeline.fetch_add(1,std::memory_order_relaxed);
            if (request.exchange(0,std::memory_order_acq_rel)) outcome.store(-1,std::memory_order_release);
        }
        bool Metadata(uint8_t* base,Point& p)
        {
            p.input=Stream::Read(base,kRecorder+116); p.snapshots=Stream::Read(base,kRecorder+140);
            p.frame=PPC_LOAD_U32(kRecorder+532); p.replayFrame=PPC_LOAD_U32(kRecorder+528);
            p.count=PPC_LOAD_U32(kRecorder+540); p.lastNativeClock=PPC_LOAD_U32(kRecorder+572);
            if (!p.input.Valid() || !p.snapshots.Valid() || p.count>18) return false;
            for (unsigned i=0;i<18;++i) p.entries[i]=PPC_LOAD_U32(kRecorder+164+i*4);
            p.trafficScore=s.event==kGame+0x37AC0 ? PPC_LOAD_U32(kTrafficScore):0;
            p.eventState.Read(base,s.event);
            if(s.event==kGame+0x36D20) memcpy(p.crashGlobals.data(),base+0x828E4748,p.crashGlobals.size());
            p.drivable=s.event==kGame+0x36D20 || DrivablePoint(base);
            if(AuditEnabled()) p.camera=CameraAudit::Read(base);
            if(s.event==kGame+0x3AAE0) p.roster=RosterAudit::Read(base);
            p.rng[0]=PPC_LOAD_U32(kRng); p.rng[1]=PPC_LOAD_U32(kRng+4);
            p.deadline=s.event==kRoadRage ? PPC_LOAD_U32(kRoadRage+3056):0;
            return true;
        }
        bool MakePoint(PPCContext& ctx,uint8_t* base,float now,Point& p)
        {
            if (!Metadata(base,p)) return false;
            memset(base+s.guest,0,kHeader);
            PPC_STORE_U32(s.guest+8,p.frame); PPC_STORE_U32(s.guest+12,p.replayFrame);
            PPC_STORE_U32(s.guest+16,p.input.cursor-p.input.buffer);
            const auto started=std::chrono::steady_clock::now();
            const double cpuStart=ThreadMilliseconds();
            const auto guardStart=writewatch::GetGuardStats();
            bool ok;uint64_t copiedCalls=0,copiedBytes=0;
            {
                ContextGuard guard(ctx);
                copy::Scope scope(s.guest,kHeader+kPayloadLimit,FastCopy());
                ctx.r3.u64=s.guest; ctx.r4.u64=s.guest+kHeader; ctx.r5.u64=kPayloadLimit;
                __imp__sub_82345648(ctx,base); ok=ctx.r3.u32!=0;
                copiedCalls=copy::region.calls;copiedBytes=copy::region.bytes;
            }
            const auto nativeEnd=std::chrono::steady_clock::now();
            const uint32_t size=PPC_LOAD_U32(s.guest+4);
            p.clock=std::bit_cast<float>(PPC_LOAD_U32(s.guest+56));
            if(AuditEnabled()) p.camera.Log("capture",p.clock);
            if(AuditEnabled() && s.event==kGame+0x3AAE0) p.roster.Log("capture",p.clock);
            if (!ok || !size || size>kPayloadLimit || !std::isfinite(p.clock) || std::abs(p.clock-now)>.05f) return false;
            p.bytes.assign(base+s.guest,base+s.guest+kHeader+size);
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            const double cpuMs=ThreadMilliseconds()-cpuStart;
            const auto guardEnd=writewatch::GetGuardStats();
            if(FastCopy() && VerifyCopy())
            {
                ContextGuard guard(ctx);copy::Scope native(s.guest,kHeader+kPayloadLimit,false);
                ctx.r3.u64=s.guest;ctx.r4.u64=s.guest+kHeader;ctx.r5.u64=kPayloadLimit;
                __imp__sub_82345648(ctx,base);
                const bool identical=ctx.r3.u32 && PPC_LOAD_U32(s.guest+4)==size &&
                    memcmp(base+s.guest,p.bytes.data(),p.bytes.size())==0;
                fprintf(stderr,"[rewind-copy] verify clock=%g identical=%u calls=%llu bytes=%llu fast_ms=%.3f\n",now,identical,
                    (unsigned long long)copiedCalls,(unsigned long long)copiedBytes,ms);
                if(!identical)
                {
                    size_t first=0,differences=0;
                    for(size_t i=0;i<p.bytes.size();++i) if(base[s.guest+i]!=p.bytes[i])
                    {if(!differences) first=i;++differences;}
                    fprintf(stderr,"[rewind-copy] mismatch first=%zu differences=%zu\n",first,differences);
                    return false;
                }
            }
            const double nativeMs=std::chrono::duration<double,std::milli>(nativeEnd-started).count();
            fprintf(stderr,"[rewind-ring] capture clock=%g payload=%u points=%zu bytes=%zu ms=%.3f native_ms=%.3f host_ms=%.3f cpu_ms=%.3f stalls_us=%llu faults=%llu event=%08X session=%.3f\n",
                now,size,s.points.size(),s.bytes,ms,nativeMs,ms-nativeMs,cpuMs,
                (unsigned long long)(guardEnd.stallUs-guardStart.stallUs),(unsigned long long)(guardEnd.faults-guardStart.faults),s.event,report::SessionSeconds());
            return true;
        }
        bool Capture(PPCContext& ctx,uint8_t* base,float now)
        {
            Point p;
            // Reuse an expired allocation rather than churning one multi-MiB
            // malloc/free pair on every capture after history fills.
            while (!s.points.empty() && s.points.front().clock<now-32.f)
            {
                s.bytes-=s.points.front().bytes.size();
                s.spare=std::move(s.points.front().bytes);s.points.pop_front();
            }
            p.bytes=std::move(s.spare);
            if (!MakePoint(ctx,base,now,p)) return false;
            while (!s.points.empty() && (s.points.front().clock<now-32.f || s.points.size()>=66 ||
                s.bytes+p.bytes.size()>kHistoryLimit))
            { s.bytes-=s.points.front().bytes.size(); s.points.pop_front(); }
            s.bytes+=p.bytes.size(); s.points.push_back(std::move(p));
            return true;
        }
        uint32_t TrackSection(PPCContext& ctx,uint8_t* base)
        {
            ContextGuard guard(ctx);
            ctx.r3.u64=kStreamer;ctx.r4.u64=1;ctx.r5.u64=9;
            __imp__sub_82326ED0(ctx,base);return ctx.r3.u32;
        }
        uint32_t PointSection(const Point& p)
        {
            uint32_t section;memcpy(&section,p.bytes.data()+72,4);
            return __builtin_bswap32(section);
        }
        void BeginTrack(PPCContext& ctx,uint8_t* base,const Point& p,bool force=false)
        {
            // Replay prepares streamed track before loading actors. Header +72
            // identifies a track section. Streaming needs complete main-loop
            // frames, so never wait for it inside the recorder callback.
            s.fromSection=TrackSection(ctx,base);s.targetSection=PointSection(p);
            s.preparing=force || s.fromSection!=s.targetSection;
            if(!s.preparing) return;
            s.trackStarted=report::SessionSeconds();
            s.previousSection=PPC_LOAD_U32(kSectionOverride);
            s.previousTransition=PPC_LOAD_U8(kTrackTransition);
            s.previousSeeking=PPC_LOAD_U8(kRecorder+560);
            // This is the native seek-in-progress guard checked by world
            // simulation before reading collision pointers being replaced.
            PPC_STORE_U8(kRecorder+560,1);
            PPC_STORE_U32(kSectionOverride,s.targetSection);PPC_STORE_U8(kTrackTransition,1);
            ContextGuard guard(ctx);
            ctx.r3.u64=kWorld;__imp__sub_82370CC0(ctx,base);
            ctx.r3.u64=kStreamer;__imp__sub_82326A48(ctx,base);
            fprintf(stderr,"[rewind-ring] track begin section=%u->%u\n",s.fromSection,s.targetSection);
        }
        bool Load(PPCContext& ctx,uint8_t* base,const Point& p)
        {
            const uint32_t beforeScore=s.event==kGame+0x37AC0 ? PPC_LOAD_U32(kTrafficScore):0;
            std::array<std::pair<uint32_t,uint32_t>,4> fields{};
            if(s.event==kGame+0x37AC0) fields={{{2992,4},{3000,4},{3036,4},{3040,4}}};
            else if(s.event==kGame+0x39F10) fields={{{3016,1},{0,0},{0,0},{0,0}}};
            else if(s.event==kGame+0x3AAE0) fields={{{3020,4},{3024,4},{3036,1},{3037,1}}};
            auto readField=[&](size_t i) { return fields[i].second==1 ? uint32_t(PPC_LOAD_U8(s.event+fields[i].first)) : PPC_LOAD_U32(s.event+fields[i].first); };
            std::array<uint32_t,4> before{};
            for(size_t i=0;i<fields.size();++i) if(fields[i].second) before[i]=readField(i);
            memcpy(base+s.guest,p.bytes.data(),p.bytes.size()); PPC_STORE_U32(s.guest,s.guest+kHeader);
            bool ok;
            {
                ContextGuard guard(ctx);copy::Scope scope(s.guest,kHeader+kPayloadLimit,FastCopy());ctx.r3.u64=s.guest;
                __imp__sub_823456A8(ctx,base); ok=ctx.r3.u32!=0;
            }
            const float after=std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
            const auto roster=s.event==kGame+0x3AAE0 ? RosterAudit::Read(base):RosterAudit{};
            const bool actors=s.event!=kGame+0x3AAE0 || p.roster.Matches(roster);
            if(AuditEnabled() && s.event==kGame+0x3AAE0)
            {
                p.roster.Log("target",p.clock);roster.Log("loaded",after);
                fprintf(stderr,"[rewind-audit] roster_compare clock=%.6f identical=%u\n",after,roster==p.roster);
            }
            if(AuditEnabled())
            {
                p.camera.Log("target",p.clock);CameraAudit::Read(base).Log("loaded",after);
            }
            const bool rng=PPC_LOAD_U32(kRng)==p.rng[0] && PPC_LOAD_U32(kRng+4)==p.rng[1];
            const bool deadline=s.event!=kRoadRage || PPC_LOAD_U32(kRoadRage+3056)==p.deadline;
            const bool eventState=p.eventState.Matches(base,s.event);
            const bool crashGlobals=s.event!=kGame+0x36D20 ||
                memcmp(p.crashGlobals.data(),base+0x828E4748,p.crashGlobals.size())==0;
            if(!crashGlobals) fprintf(stderr,"[rewind-ring] Crash global state mismatch\n");
            const bool score=s.event!=kGame+0x37AC0 || PPC_LOAD_U32(kTrafficScore)==p.trafficScore;
            if (!ok || !eventState || !score || !actors || !crashGlobals || !std::isfinite(after) || std::abs(after-p.clock)>.05f || !rng || !deadline ||
                PPC_LOAD_U32(codes::kSession)!=codes::kSoloSession || PPC_LOAD_U32(codes::kSoloSession+4)!=s.event ||
                PPC_LOAD_U32(codes::kSoloSession+8)!=s.controller || !IsLiveClock(PPC_LOAD_U32(kClockFlags)))
            {
                fprintf(stderr,"[rewind-ring] failed native restore contract ok=%u clock=%g target=%g rng=%u deadline=%u event_state=%u score=%u actors=%u\n",ok,after,p.clock,rng,deadline,eventState,score,actors);
                return false;
            }
            if(s.event==kGame+0x37AC0)
                fprintf(stderr,"[rewind-ring] traffic score=%u->%u matched=1\n",beforeScore,p.trafficScore);
            for(size_t i=0;i<fields.size();++i) if(fields[i].second)
                fprintf(stderr,"[rewind-ring] event field event=%08X offset=%u value=%08X->%08X matched=1\n",
                    s.event,fields[i].first,before[i],readField(i));
            PPC_STORE_U8(kWorldPlayback,0);
            p.input.Restore(base,kRecorder+116); p.snapshots.Restore(base,kRecorder+140);
            PPC_STORE_U32(kRecorder+532,p.frame); PPC_STORE_U32(kRecorder+528,p.replayFrame);
            PPC_STORE_U32(kRecorder+540,p.count); PPC_STORE_U32(kRecorder+572,p.lastNativeClock);
            for (unsigned i=0;i<18;++i) PPC_STORE_U32(kRecorder+164+i*4,p.entries[i]);
            return true;
        }
        void FinishRestore(PPCContext& ctx,uint8_t* base)
        {
            const Point& p=s.recovering ? s.backup:s.points[s.restoreIndex];
            if (!Load(ctx,base,p))
            {
                if (Duration() || s.recovering) std::abort();
                s.recovering=true;
                BeginTrack(ctx,base,s.backup);
                if(!s.preparing) FinishRestore(ctx,base);
                return;
            }
            if(s.recovering)
            {
                const bool invalidated=s.invalidated;
                Clear(base,invalidated ? "restore cancelled; current world recovered" : "restore rejected; current world recovered",!invalidated);
                Publish(invalidated ? State::Collecting:State::Failed);outcome.store(-1,std::memory_order_release);return;
            }
            const float after=p.clock,now=s.restoreFrom;
            const unsigned seconds=s.restoreSeconds;
            s.lastClock=after;s.nextCapture=after+Interval();
            while(s.points.size()>s.restoreIndex+1)
            {s.bytes-=s.points.back().bytes.size();s.spare=std::move(s.points.back().bytes);s.points.pop_back();}
            ++s.completed;
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-s.restoreStarted).count();
            s.backup=Point{};
            fprintf(stderr,"[rewind-ring] restore=%u seconds=%u clock=%g->%g rng=1 deadline=1 event_state=%u points=%zu bytes=%zu ms=%.3f\n",
                s.completed,seconds,now,after,p.eventState.size,s.points.size(),s.bytes,ms);
            timeline.fetch_add(1,std::memory_order_relaxed);
            if(!Duration()) {Publish(State::Ready,after);outcome.store(int(std::lround((now-after)*1000.f)),std::memory_order_release);}
            else if((Duration()!=1 && Duration()!=4) || s.completed==3) Clear(base,"automatic experiment completed",true);
        }
        void RestoreTrackFlags(uint8_t* base)
        {
            PPC_STORE_U32(kSectionOverride,s.previousSection);
            PPC_STORE_U8(kTrackTransition,s.previousTransition);
            PPC_STORE_U8(kRecorder+560,s.previousSeeking);
        }
        void PumpTrack(PPCContext& ctx,uint8_t* base)
        {
            if(!s.preparing) return;
            // Preserve the native transition through complete main-loop frames.
            // No actor state is loaded while the required collision is absent.
            ContextGuard guard(ctx);
            ctx.r3.u64=kStreamer;ctx.r4.u64=0;ctx.r5.u64=0;
            __imp__sub_82336E28(ctx,base);
            const bool available=ctx.r3.u32 && !PPC_LOAD_U8(kStreamer+95);
            if(available)
            {
                ctx.r3.u64=kStreamer;__imp__sub_82120BB0(ctx,base);
                if(ctx.r3.u32)
                {
                    RestoreTrackFlags(base);
                    s.preparing=false;
                    fprintf(stderr,"[rewind-ring] track ready section=%u->%u\n",s.fromSection,s.targetSection);
                    FinishRestore(ctx,base);return;
                }
            }
            if(report::SessionSeconds()-s.trackStarted>15.)
            {
                fprintf(stderr,"[rewind-ring] track preparation timed out state=%u recovery=%u\n",PPC_LOAD_U32(kStreamer+16),s.recovering);
                if(Duration() || s.recovering) std::abort();
                // Actors have not been loaded yet. Rebuild the original track
                // under the same simulation guard before returning the backup.
                RestoreTrackFlags(base);
                s.preparing=false;s.recovering=true;
                BeginTrack(ctx,base,s.backup,true);
            }
        }
        void Restore(PPCContext& ctx,uint8_t* base,float now,unsigned seconds,float requestedAt=-1.f)
        {
            const float target=(requestedAt>=0.f ? requestedAt:now)-float(seconds);
            size_t index=s.points.size();
            while(index && (s.points[index-1].clock>target+.001f || !s.points[index-1].drivable)) --index;
            if(!index)
            {
                if(Duration()) {Clear(base,"requested history unavailable",true);Publish(State::Failed);}
                outcome.store(-1,std::memory_order_release);return;
            }
            --index;
            const Point& p=s.points[index];
            if(now-p.clock>32.001f) { outcome.store(-1,std::memory_order_release);return; }
            fprintf(stderr,"[rewind-ring] driving target requested=%u actual=%.3f clock=%.6f\n",seconds,now-p.clock,p.clock);
            if(!p.input.SameBuffer(Stream::Read(base,kRecorder+116)) || !p.snapshots.SameBuffer(Stream::Read(base,kRecorder+140)))
            {Clear(base,"native recorder buffer changed",true);Publish(State::Failed);return;}
            s.restoreStarted=std::chrono::steady_clock::now();
            s.restoreIndex=index;s.restoreFrom=now;s.restoreSeconds=seconds;s.recovering=false;
            if(!Duration() && !MakePoint(ctx,base,now,s.backup))
            {outcome.store(-1,std::memory_order_release);return;}
            BeginTrack(ctx,base,p);
            if(!s.preparing) FinishRestore(ctx,base);
            else Publish(State::Restoring);
        }

    }
    void AtBoundary(PPCContext& ctx,uint8_t* base,uint32_t recorder)
    {
        if (recorder!=kRecorder || s.preparing) return;
        if (!Duration() && !enabled.load(std::memory_order_acquire))
        {
            if (s.guest || s.finished) Clear(base,"disabled");
            Publish(State::Disabled); return;
        }
        if (s.finished || s.preparing) return;
        // The observed solo world boundary belongs to the main guest thread.
        // Never run the shared native serializer from a second host thread.
        const auto thread=GuestThread::GetCurrentThreadId();
        if (thread!=1) return;
        gate::Boundary saveBoundary;
        if (!saveBoundary)
        { if (s.guest) Clear(base,"save transaction active"); Publish(State::Saving); return; }
        if (s.guest && (s.saveEpoch!=gate::epoch.load(std::memory_order_acquire) || s.options!=worldOptions.load(std::memory_order_acquire)))
            Clear(base,"save or world options changed");
        const auto session=PPC_LOAD_U32(codes::kSession);
        const Boundary b{kGame,PPC_LOAD_U32(kPhase),session,
            session==codes::kSoloSession ? PPC_LOAD_U32(session+8):0,
            session==codes::kSoloSession ? PPC_LOAD_U32(session+4):0,
            PPC_LOAD_U32(codes::kLocalPlayers),PPC_LOAD_U8(codes::kSpecialMode),PPC_LOAD_U32(codes::kCrashVariant),online::Enabled()};
        auto permitted=b;
        // Crash world capture also runs outside the ordinary recorder callback.
        // Only its solo event may use the native Crash special-mode flag.
        if (SupportedEvent(b.event) && b.event==kGame+0x36D20 && b.specialMode==1) permitted.specialMode=0;
        if (!CanObserve(permitted) || !SupportedEvent(b.event) || !IsLiveClock(PPC_LOAD_U32(kClockFlags)) || PPC_LOAD_U8(kWorldPlayback)!=0)
        {
            if (Duration())
            {
                static thread_local uint64_t last=UINT64_MAX;
                const auto flags=PPC_LOAD_U32(kClockFlags);
                const uint64_t key=(uint64_t(b.event)<<32)|flags;
                if (last!=key)
                { last=key;fprintf(stderr,"[rewind-ring] excluded event=%08X phase=%u flags=%08X playback=%u special=%u players=%u controller=%08X\n",
                    b.event,b.phase,flags,PPC_LOAD_U8(kWorldPlayback),b.specialMode,b.localPlayers,b.controller); }
            }
            if (s.guest) Clear(base,"left a supported live solo event"); Publish(State::Unsupported); return;
        }
        // Native road participant mode 3 is finished/eliminated, including
        // the brief interval before the session switches to Replay/results.
        if(b.event!=kGame+0x36D20 && PPC_LOAD_U32(kLocalParticipant+13520)==3)
        {
            if(s.guest) Clear(base,"local participant finished or eliminated");
            Publish(State::Unsupported);return;
        }
        const float now=std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
        if (!std::isfinite(now) || now<(Duration()==4 || b.event==kGame+0x36D20 ? 1.f:5.f)) { if (s.guest) Clear(base,"clock reset"); Publish(State::Collecting); return; }
        if (s.guest && (s.event!=b.event || s.controller!=b.controller || s.thread!=thread || now+.001f<s.lastClock))
            Clear(base,"event/thread/timeline changed");
        if (!s.guest)
        {
            void* staging=g_userHeap.Alloc(kHeader+kPayloadLimit);
            if (!staging) { s.finished=true; Publish(State::Failed); return; }
            s.guest=g_memory.MapVirtual(staging); s.event=b.event; s.controller=b.controller; s.thread=thread;
            s.nextCapture=now;
            s.saveEpoch=gate::epoch.load(std::memory_order_acquire);s.options=worldOptions.load(std::memory_order_acquire);
        }
        s.lastClock=now;
        if (!s.points.empty() && (!s.points.back().input.SameBuffer(Stream::Read(base,kRecorder+116)) ||
            !s.points.back().snapshots.SameBuffer(Stream::Read(base,kRecorder+140))))
        { Clear(base,"native buffers changed",true); Publish(State::Failed); return; }
        if (now+.001f>=s.nextCapture)
        {
            if (!Capture(ctx,base,now)) { Clear(base,"native capture failed",true); Publish(State::Failed); return; }
            s.nextCapture=now+Interval();
        }
        const unsigned seconds=Duration()==1 ? 10*(s.completed+1):Duration()==4 ? 30-10*s.completed:Duration();
        Publish(State::Ready,now);
        if (Duration())
        { if (Duration()!=2 && now>=float(seconds+(Duration()==4 ? 1:30)+s.completed)) Restore(ctx,base,now,seconds); }
        else if (const uint64_t pending=request.load(std::memory_order_acquire))
        {
            if(uint32_t(pending>>32)!=timeline.load(std::memory_order_relaxed))
            { request.store(0,std::memory_order_release);s.waitingForCamera=false;outcome.store(-1,std::memory_order_release);return; }
            const bool driving=s.event==kGame+0x36D20 || DrivablePoint(base);
            if(!driving || s.waitingForCamera)
            {
                if(!s.waitingForCamera)
                {
                    s.waitingForCamera=true;s.requestedClock=now;s.cameraWaitStarted=report::SessionSeconds();
                    s.drivableSince=-1.f;
                    fprintf(stderr,"[rewind-ring] waiting for native camera clock=%.6f seconds=%u\n",now,unsigned(pending&255));
                }
                if(!driving) s.drivableSince=-1.f;
                else if(s.drivableSince<0.f) s.drivableSince=now;
                if(report::SessionSeconds()-s.cameraWaitStarted>30. || now-s.requestedClock+float(pending&255)>32.f)
                {
                    request.store(0,std::memory_order_release);s.waitingForCamera=false;
                    outcome.store(-1,std::memory_order_release);return;
                }
                if(s.drivableSince<0.f || now-s.drivableSince<.5f)
                { Publish(State::Restoring);return; }
            }
            const float requestedAt=s.waitingForCamera ? s.requestedClock:now;
            request.store(0,std::memory_order_release);s.waitingForCamera=false;
            Restore(ctx,base,now,unsigned(pending&255),requestedAt);
        }
    }
    void AuditCamera(uint32_t camera) { auditCamera.store(camera,std::memory_order_relaxed); }
    bool Preparing(uint32_t recorder) { return recorder==kRecorder && s.preparing; }
    void SetEnabled(bool value) { enabled.store(value,std::memory_order_release); }
    void AtWorldBoundary(PPCContext& ctx,uint8_t* base)
    {
        if (GuestThread::GetCurrentThreadId()!=1) return;
        AuditWorld(base);
        if(s.preparing)
        {
            gate::Boundary boundary;
            if(!boundary) return;
            const auto session=PPC_LOAD_U32(codes::kSession);
            const bool owner=session==codes::kSoloSession && PPC_LOAD_U32(kPhase)==5 &&
                PPC_LOAD_U32(session+4)==s.event && PPC_LOAD_U32(session+8)==s.controller &&
                IsLiveClock(PPC_LOAD_U32(kClockFlags)) && !PPC_LOAD_U8(kWorldPlayback);
            if(!owner)
            {
                // The event can be abandoned while streaming spans frames.
                // Never load an old event's actors into a new owner or menu.
                if(PPC_LOAD_U32(kSectionOverride)==s.targetSection &&
                    PPC_LOAD_U8(kTrackTransition)==1 && PPC_LOAD_U8(kRecorder+560)==1)
                    RestoreTrackFlags(base);
                Clear(base,"left world during track preparation");
                Publish(State::Unsupported);outcome.store(-1,std::memory_order_release);return;
            }
            if(!s.recovering && (s.saveEpoch!=gate::epoch.load(std::memory_order_acquire) ||
                s.options!=worldOptions.load(std::memory_order_acquire) ||
                (!Duration() && !enabled.load(std::memory_order_acquire))))
            {
                // Registration may change between streaming frames. The world
                // has stayed suspended; recover its pre-request state instead
                // of applying a checkpoint from before the save/options change.
                fprintf(stderr,"[rewind-ring] track restore invalidated; recovering current world\n");
                if(Duration()) std::abort();
                RestoreTrackFlags(base);s.preparing=false;s.recovering=true;s.invalidated=true;
                BeginTrack(ctx,base,s.backup,true);return;
            }
            PumpTrack(ctx,base);return;
        }
        if (!Duration() && !enabled.load(std::memory_order_acquire))
        { if(s.guest || s.finished) AtBoundary(ctx,base,kRecorder);return; }
        const auto session=PPC_LOAD_U32(codes::kSession);
        const auto event=session==codes::kSoloSession ? PPC_LOAD_U32(session+4):0;
        if (s.guest && (s.saveEpoch!=gate::epoch.load(std::memory_order_acquire) || s.options!=worldOptions.load(std::memory_order_acquire)))
        { Clear(base,"save or world options changed");Publish(State::Collecting); }
        if (PPC_LOAD_U32(kPhase)==5 && event==kGame+0x36D20)
        { AtBoundary(ctx,base,kRecorder);return; }
        if (PPC_LOAD_U32(kPhase)!=5 || session!=codes::kSoloSession ||
            (s.guest && event!=s.event) || !IsLiveClock(PPC_LOAD_U32(kClockFlags)) || PPC_LOAD_U8(kWorldPlayback)!=0)
        { if(s.guest) Clear(base,"left the live world");Publish(State::Unsupported);return; }
        // Ordinary driving captures at its native recorder boundary. Crash
        // above additionally observes the completed world update.
    }
    void SetWorldOptions(uint32_t value) { worldOptions.store(value,std::memory_order_release); }
    Status GetStatus()
    {
        const uint64_t v=published.load(std::memory_order_acquire);
        return {State(v&255),unsigned((v>>16)&255),unsigned((v>>8)&7),uint32_t(v>>32)};
    }
    bool Request(unsigned seconds)
    {
        const unsigned bit=seconds==10 ? 1:seconds==20 ? 2:seconds==30 ? 4:0;
        const auto status=GetStatus();
        if (!enabled.load(std::memory_order_acquire) || !bit || !(status.readyMask&bit)) return false;
        uint64_t empty=0;
        return request.compare_exchange_strong(empty,(uint64_t(status.epoch)<<32)|seconds,std::memory_order_acq_rel);
    }
    int TakeOutcome() { return outcome.exchange(0,std::memory_order_acq_rel); }
}

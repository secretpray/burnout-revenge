// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <stdafx.h>
#include "rewind_ring.h"
#include "rewind_probe.h"
#include <cpu/guest_thread.h>
#include <user/online.h>
#include <cmath>
#include <cstdlib>
#include <deque>

extern "C" PPC_FUNC(__imp__sub_82345648);
extern "C" PPC_FUNC(__imp__sub_823456A8);

namespace game::rewind::ring
{
    namespace
    {
        constexpr uint32_t kRecorder=0x82AFE440, kRoadRage=kGame+0x386B0;
        constexpr uint32_t kHeader=76, kPayloadLimit=16*1024*1024, kRng=kGame+0x33088;
        constexpr size_t kHistoryLimit=80*1024*1024;
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
        struct Point
        {
            std::vector<uint8_t> bytes;
            Stream input,snapshots;
            uint32_t frame{},replayFrame{},count{},entries[18]{},lastNativeClock{},rng[2]{},deadline{};
            float clock{};
        };
        struct State
        {
            std::deque<Point> points;
            size_t bytes{};
            uint32_t guest{},event{},controller{},thread{};
            float lastClock{},nextCapture{};
            unsigned completed{};
            bool finished{};
        };
        thread_local State s;
        unsigned Duration()
        {
            static const unsigned duration=[] {
                const char* value=std::getenv("BURNOUT_REWIND_RING");
                const char* trial=std::getenv("BURNOUT_REWIND_TRIAL");
                if (!value || (trial && *trial && std::string_view(trial)!="0")) return 0u;
                const std::string_view v(value);
                return v=="10" ? 10u : v=="20" ? 20u : v=="30" ? 30u : v=="sequence" ? 1u : 0u;
            }();
            return duration;
        }
        void Clear(uint8_t* base,const char* reason,bool finished=false)
        {
            if (s.guest)
            {
                g_userHeap.Free(base+s.guest);
                fprintf(stderr,"[rewind-ring] clear: %s points=%zu bytes=%zu\n",reason,s.points.size(),s.bytes);
            }
            const unsigned completed=s.completed;
            s=State{}; s.completed=completed; s.finished=finished;
        }
        bool Metadata(uint8_t* base,Point& p)
        {
            p.input=Stream::Read(base,kRecorder+116); p.snapshots=Stream::Read(base,kRecorder+140);
            p.frame=PPC_LOAD_U32(kRecorder+532); p.replayFrame=PPC_LOAD_U32(kRecorder+528);
            p.count=PPC_LOAD_U32(kRecorder+540); p.lastNativeClock=PPC_LOAD_U32(kRecorder+572);
            if (!p.input.Valid() || !p.snapshots.Valid() || p.count>18) return false;
            for (unsigned i=0;i<18;++i) p.entries[i]=PPC_LOAD_U32(kRecorder+164+i*4);
            p.rng[0]=PPC_LOAD_U32(kRng); p.rng[1]=PPC_LOAD_U32(kRng+4);
            p.deadline=PPC_LOAD_U32(kRoadRage+3056);
            return true;
        }
        bool Capture(PPCContext& ctx,uint8_t* base,float now)
        {
            Point p;
            if (!Metadata(base,p)) return false;
            memset(base+s.guest,0,kHeader);
            PPC_STORE_U32(s.guest+8,p.frame); PPC_STORE_U32(s.guest+12,p.replayFrame);
            PPC_STORE_U32(s.guest+16,p.input.cursor-p.input.buffer);
            const auto started=std::chrono::steady_clock::now();
            bool ok;
            {
                ContextGuard guard(ctx);
                ctx.r3.u64=s.guest; ctx.r4.u64=s.guest+kHeader; ctx.r5.u64=kPayloadLimit;
                __imp__sub_82345648(ctx,base); ok=ctx.r3.u32!=0;
            }
            const uint32_t size=PPC_LOAD_U32(s.guest+4);
            p.clock=std::bit_cast<float>(PPC_LOAD_U32(s.guest+56));
            if (!ok || !size || size>kPayloadLimit || !std::isfinite(p.clock) || std::abs(p.clock-now)>.05f) return false;
            while (!s.points.empty() && (s.points.front().clock<now-32.f || s.points.size()>=34 ||
                s.bytes+kHeader+size>kHistoryLimit))
            { s.bytes-=s.points.front().bytes.size(); s.points.pop_front(); }
            p.bytes.assign(base+s.guest,base+s.guest+kHeader+size);
            s.bytes+=p.bytes.size(); s.points.push_back(std::move(p));
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            fprintf(stderr,"[rewind-ring] capture clock=%g payload=%u points=%zu bytes=%zu ms=%.3f\n",now,size,s.points.size(),s.bytes,ms);
            return true;
        }
        void Restore(PPCContext& ctx,uint8_t* base,float now,unsigned seconds)
        {
            size_t index=s.points.size();
            while (index && s.points[index-1].clock>now-float(seconds)+.001f) --index;
            if (!index) { Clear(base,"requested history unavailable",true); return; }
            --index;
            const Point& p=s.points[index];
            if (!p.input.SameBuffer(Stream::Read(base,kRecorder+116)) || !p.snapshots.SameBuffer(Stream::Read(base,kRecorder+140)))
            { Clear(base,"native recorder buffer changed",true); return; }
            const auto started=std::chrono::steady_clock::now();
            memcpy(base+s.guest,p.bytes.data(),p.bytes.size()); PPC_STORE_U32(s.guest,s.guest+kHeader);
            bool ok;
            {
                ContextGuard guard(ctx); ctx.r3.u64=s.guest;
                __imp__sub_823456A8(ctx,base); ok=ctx.r3.u32!=0;
            }
            const float after=std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
            const bool rng=PPC_LOAD_U32(kRng)==p.rng[0] && PPC_LOAD_U32(kRng+4)==p.rng[1];
            const bool deadline=PPC_LOAD_U32(kRoadRage+3056)==p.deadline;
            if (!ok || !std::isfinite(after) || std::abs(after-p.clock)>.05f || !rng || !deadline ||
                PPC_LOAD_U32(codes::kSession)!=codes::kSoloSession || PPC_LOAD_U32(codes::kSoloSession+4)!=s.event ||
                PPC_LOAD_U32(codes::kSoloSession+8)!=s.controller || !IsLiveClock(PPC_LOAD_U32(kClockFlags)))
            {
                fprintf(stderr,"[rewind-ring] failed native restore contract ok=%u clock=%g target=%g rng=%u deadline=%u\n",ok,after,p.clock,rng,deadline);
                std::abort();
            }
            PPC_STORE_U8(kWorldPlayback,0);
            p.input.Restore(base,kRecorder+116); p.snapshots.Restore(base,kRecorder+140);
            PPC_STORE_U32(kRecorder+532,p.frame); PPC_STORE_U32(kRecorder+528,p.replayFrame);
            PPC_STORE_U32(kRecorder+540,p.count); PPC_STORE_U32(kRecorder+572,p.lastNativeClock);
            for (unsigned i=0;i<18;++i) PPC_STORE_U32(kRecorder+164+i*4,p.entries[i]);
            s.lastClock=after; s.nextCapture=after+1.f;
            while (s.points.size()>index+1) { s.bytes-=s.points.back().bytes.size(); s.points.pop_back(); }
            ++s.completed;
            const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
            fprintf(stderr,"[rewind-ring] restore=%u seconds=%u clock=%g->%g rng=%u deadline=%u points=%zu bytes=%zu ms=%.3f\n",
                s.completed,seconds,now,after,rng,deadline,s.points.size(),s.bytes,ms);
            if (Duration()!=1 || s.completed==3) Clear(base,"automatic experiment completed",true);
        }
    }
    void AtBoundary(PPCContext& ctx,uint8_t* base,uint32_t recorder)
    {
        if (!Duration() || s.finished || recorder!=kRecorder) return;
        // The observed solo world boundary belongs to the main guest thread.
        // Never run the shared native serializer from a second host thread.
        const auto thread=GuestThread::GetCurrentThreadId();
        if (thread!=1) return;
        const auto session=PPC_LOAD_U32(codes::kSession);
        const Boundary b{kGame,PPC_LOAD_U32(kPhase),session,
            session==codes::kSoloSession ? PPC_LOAD_U32(session+8):0,
            session==codes::kSoloSession ? PPC_LOAD_U32(session+4):0,
            PPC_LOAD_U32(codes::kLocalPlayers),PPC_LOAD_U8(codes::kSpecialMode),PPC_LOAD_U32(codes::kCrashVariant),online::Enabled()};
        if (!CanObserve(b) || b.event!=kRoadRage || !IsLiveClock(PPC_LOAD_U32(kClockFlags)) || PPC_LOAD_U8(kWorldPlayback)!=0)
        { if (s.guest) Clear(base,"left live solo Road Rage"); return; }
        const float now=std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
        if (!std::isfinite(now) || now<5.f) { if (s.guest) Clear(base,"clock reset"); return; }
        if (s.guest && (s.event!=b.event || s.controller!=b.controller || s.thread!=thread || now+.001f<s.lastClock))
            Clear(base,"event/thread/timeline changed");
        if (!s.guest)
        {
            void* staging=g_userHeap.Alloc(kHeader+kPayloadLimit);
            if (!staging) { s.finished=true; return; }
            s.guest=g_memory.MapVirtual(staging); s.event=b.event; s.controller=b.controller; s.thread=thread;
            s.nextCapture=now;
        }
        s.lastClock=now;
        if (!s.points.empty() && (!s.points.back().input.SameBuffer(Stream::Read(base,kRecorder+116)) ||
            !s.points.back().snapshots.SameBuffer(Stream::Read(base,kRecorder+140))))
        { Clear(base,"native buffers changed",true); return; }
        if (now+.001f>=s.nextCapture)
        {
            if (!Capture(ctx,base,now)) { Clear(base,"native capture failed",true); return; }
            s.nextCapture=now+1.f;
        }
        const unsigned seconds=Duration()==1 ? 10*(s.completed+1):Duration();
        if (now>=float(seconds+30+s.completed)) Restore(ctx,base,now,seconds);
    }
}

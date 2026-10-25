// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// Read-only checkpoint feasibility experiment, NOT a rewind implementation.
#include <stdafx.h>
#include "rewind_probe.h"
#include "rewind_trial.h"
#include "rewind_ring.h"
#include "madden_unlock.h"
#include <apu/audio.h>
#include <cpu/guest_thread.h>
#include <gpu/command_processor.h>
#include <kernel/vmem.h>
#include <report/report.h>
#include <cmath>
#include <cstdlib>

namespace
{
    bool Enabled()
    {
        static const bool enabled = [] {
            const char* v = std::getenv("BURNOUT_REWIND_PROBE");
            return v && std::strcmp(v, "1") == 0;
        }();
        return enabled;
    }

    template<class F> void Diagnose(F&& operation)
    {
        try { operation(); }
        catch (const std::exception& error)
        {
            fprintf(stderr, "[rewind-probe] native diagnostics failed: %s\n", error.what());
        }
    }

    void Inventory()
    {
        // Query holds each heap's lock, not all heaps or their writers. These
        // are allocation metadata observations, not a consistent memory image.
        uint64_t committed = 0, reserved = 0;
        for (auto* heap : {&vmem::Virtual4K(), &vmem::Virtual64K(), &vmem::Physical()})
        {
            for (uint32_t address = heap->Base(); address < heap->End();)
            {
                vmem::RegionInfo r{};
                if (!heap->Query(address, &r) || r.regionSize == 0) break;
                if (r.state != vmem::X_MEM_FREE)
                {
                    fprintf(stderr, "[rewind-probe] region=%08X+%08X allocation=%08X state=%08X protect=%08X\n",
                        r.baseAddress, r.regionSize, r.allocationBase, r.state, r.protect);
                    if (r.state == vmem::X_MEM_COMMIT) committed += r.regionSize;
                    else reserved += r.regionSize;
                }
                address += r.regionSize;
            }
        }
        fprintf(stderr, "[rewind-probe] heap_committed=%llu heap_reserved=%llu runtime_user=%08X+10000000 runtime_physical=%08X+04000000 image=%08llX+%08llX\n",
            (unsigned long long)committed, (unsigned long long)reserved,
            g_memory.MapVirtual(g_userHeap.heap), g_memory.MapVirtual(g_userHeap.physicalHeap),
            (unsigned long long)PPC_IMAGE_BASE, (unsigned long long)PPC_IMAGE_SIZE);
        fprintf(stderr, "[rewind-probe] physical mirrors C0000000/E0000000 alias A0000000; e_shift=%u; heap totals exclude image/arena/host state\n", g_memory.eWindowShift);
        fprintf(stderr, "[rewind-probe] GPU observation:\n%s\n[rewind-probe] audio observation:\n%s\n[rewind-probe] other thread stacks (running, NOT parked):\n%s",
            gpu::DescribeState().c_str(), apu::DescribeState().c_str(), report::DescribeThreadStacks().c_str());
    }

    void Observe(PPCContext& ctx, uint8_t* base)
    {
        using namespace game::rewind;
        // A replacement XEX is not supported. Do not dereference a foreign
        // receiver, a session's event, or a controller during loading.
        const uint32_t game = ctx.r3.u32;
        if (game != kGame) return;
        const uint32_t session = PPC_LOAD_U32(game::codes::kSession);
        const Boundary b{game, PPC_LOAD_U32(kPhase), session,
            session == game::codes::kSoloSession ? PPC_LOAD_U32(session + 8) : 0,
            session == game::codes::kSoloSession ? PPC_LOAD_U32(session + 4) : 0,
            PPC_LOAD_U32(game::codes::kLocalPlayers), PPC_LOAD_U8(game::codes::kSpecialMode),
            PPC_LOAD_U32(game::codes::kCrashVariant)};
        struct Series
        {
            Boundary boundary{};
            uint32_t frames{}, sp{}, tls{}, lr{}, changedStack{}, changedTls{}, changedCaller{};
            float firstClock{}, lastClock{}, originClock{};
        };
        static thread_local Series s;
        static thread_local uint32_t lastEvent=0,lastController=0;
        if (b.phase==5 && (lastEvent!=b.event || lastController!=b.controller))
        {
            lastEvent=b.event;lastController=b.controller;
            fprintf(stderr,"[rewind-probe] mode event=%08X controller=%08X session=%08X flags=%08X special=%u players=%u variant=%u\n",
                b.event,b.controller,b.session,PPC_LOAD_U32(kClockFlags),b.specialMode,b.localPlayers,b.crashVariant);
        }
        if (!CanObserve(b)) { s = {}; return; }
        const uint32_t flags = PPC_LOAD_U32(kClockFlags);
        static thread_local uint32_t lastFlags = 0xFFFFFFFF;
        if (lastFlags != flags)
        {
            lastFlags = flags;
            fprintf(stderr, "[rewind-probe] clock_flags=%08X world_clock=%g event=%08X\n", flags,
                std::bit_cast<float>(PPC_LOAD_U32(kWorldClock)), b.event);
        }
        if (!IsLiveClock(flags)) { s = {}; return; }
        if (b.event != s.boundary.event || b.controller != s.boundary.controller) s = {};
        const float clock = std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
        if (!std::isfinite(clock)) { s = {}; return; }
        if (clock < s.lastClock) s = {};
        if (s.boundary.event == 0)
        {
            s.boundary = b;
            s.originClock = clock;
        }
        const bool advanced = clock > s.lastClock;
        s.lastClock = clock;
        // Session phase 5 is also used by the stationary instructions/countdown.
        // Observe a moving simulation, not 100 calls while its clock is frozen.
        if (!advanced || clock - s.originClock < 5.f || s.frames >= 100) return;
        if (s.frames == 0)
        {
            s.boundary = b;
            s.sp = ctx.r1.u32; s.tls = ctx.r13.u32; s.lr = uint32_t(ctx.lr); s.firstClock = clock;
            fprintf(stderr, "[rewind-probe] begin thread=%u game=%08X phase=%u session=%08X controller=%08X event=%08X sp=%08X tls=%08X lr=%08X clock=%g\n",
                GuestThread::GetCurrentThreadId(), b.game, b.phase, b.session, b.controller, b.event, s.sp, s.tls, s.lr, clock);
            fprintf(stderr, "[rewind-probe] capture=unavailable restore=unavailable: no cooperative barrier or world serializer; no guest bytes copied or written\n");
            Inventory();
        }
        s.changedStack += ctx.r1.u32 != s.sp;
        s.changedTls += ctx.r13.u32 != s.tls;
        s.changedCaller += uint32_t(ctx.lr) != s.lr;
        if (++s.frames == 100)
            fprintf(stderr, "[rewind-probe] boundary_samples=100 changed_sp=%u changed_tls=%u changed_lr=%u game_seconds=%g; stable call boundary does NOT establish restore safety\n",
                s.changedStack, s.changedTls, s.changedCaller, clock - s.firstClock);
    }
}

extern "C" PPC_FUNC(__imp__sub_8211FA28);
PPC_FUNC(sub_8211FA28)
{
    const uint32_t gameObject=ctx.r3.u32;
    static thread_local bool failed = false;
    if (Enabled() && !failed)
    {
        try { Observe(ctx, base); }
        catch (const std::exception& error)
        {
            failed = true;
            fprintf(stderr, "[rewind-probe] diagnostics disabled: %s\n", error.what());
        }
    }
    __imp__sub_8211FA28(ctx, base);
    if (gameObject==game::rewind::kGame)
    {
        game::rewind::ring::AtWorldBoundary(ctx,base);
        game::madden::AtBoundary(ctx,base);
    }
}

// Native Replay uses a game-owned serializer, rather than raw RAM copies.
// Observe the original capture/restore calls; do not request either operation.
extern "C" PPC_FUNC(__imp__sub_82345648);
PPC_FUNC(sub_82345648)
{
    const uint32_t header = ctx.r3.u32, payload = ctx.r4.u32, capacity = ctx.r5.u32;
    __imp__sub_82345648(ctx, base);
    if (ctx.r3.u32 == 0) return;
    game::rewind::trial::Captured(base, header, payload, capacity);
    if (!Enabled() || PPC_LOAD_U32(game::codes::kSession) != game::codes::kSoloSession) return;
    static thread_local unsigned count = 0;
    if (count >= 24) return;
    Diagnose([&] {
        fprintf(stderr, "[rewind-probe] native_capture=%u header=%08X payload=%08X capacity=%u bytes=%u snapshot_clock=%g world_clock=%g flags=%08X\n",
            ++count, header, payload, capacity, PPC_LOAD_U32(header + 4),
            std::bit_cast<float>(PPC_LOAD_U32(header + 56)),
            std::bit_cast<float>(PPC_LOAD_U32(game::rewind::kWorldClock)), PPC_LOAD_U32(game::rewind::kClockFlags));
    });
}

extern "C" PPC_FUNC(__imp__sub_823456A8);
PPC_FUNC(sub_823456A8)
{
    const uint32_t header = ctx.r3.u32;
    __imp__sub_823456A8(ctx, base);
    if (!Enabled() || PPC_LOAD_U32(game::codes::kSession) != game::codes::kSoloSession) return;
    static thread_local unsigned count = 0;
    if (count >= 100) return;
    Diagnose([&] {
        fprintf(stderr, "[rewind-probe] native_restore=%u header=%08X bytes=%u snapshot_clock=%g world_clock=%g flags=%08X\n",
            ++count, header, PPC_LOAD_U32(header + 4), std::bit_cast<float>(PPC_LOAD_U32(header + 56)),
            std::bit_cast<float>(PPC_LOAD_U32(game::rewind::kWorldClock)), PPC_LOAD_U32(game::rewind::kClockFlags));
    });
}

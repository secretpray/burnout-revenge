// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// One native world checkpoint, deliberately restored only in an opt-in trial.
#include <stdafx.h>
#include "rewind_trial.h"
#include "rewind_ring.h"
#include "rewind_probe.h"
#include <cpu/guest_thread.h>
#include <user/online.h>
#include <cmath>
#include <cstdlib>

extern "C" PPC_FUNC(__imp__sub_823456A8);

namespace game::rewind::trial
{
    namespace
    {
        constexpr uint32_t kRecorder = 0x82AB6F80 + 0x474C0;
        constexpr uint32_t kRoadRageEvent = kGame + 0x386B0;
        constexpr uint32_t kHeaderBytes = 76, kMaxPayload = 16 * 1024 * 1024;

        unsigned Repeats()
        {
            static const unsigned repeats = [] {
                const char* v = std::getenv("BURNOUT_REWIND_TRIAL");
                if (!v || !*v) return 0u;
                char* end = nullptr;
                const auto n = std::strtoul(v, &end, 10);
                return end && !*end && n > 0 && n <= 100 ? unsigned(n) : 0u;
            }();
            return repeats;
        }
        Boundary ReadBoundary(uint8_t* base)
        {
            const uint32_t session = PPC_LOAD_U32(codes::kSession);
            return {kGame, PPC_LOAD_U32(kPhase), session,
                session == codes::kSoloSession ? PPC_LOAD_U32(session + 8) : 0,
                session == codes::kSoloSession ? PPC_LOAD_U32(session + 4) : 0,
                PPC_LOAD_U32(codes::kLocalPlayers), PPC_LOAD_U8(codes::kSpecialMode),
                PPC_LOAD_U32(codes::kCrashVariant), online::Enabled()};
        }
        struct Stream
        {
            uint32_t base{}, capacity{}, cursor{}, end{};
            static Stream Read(uint8_t* base, uint32_t address)
            {
                return {PPC_LOAD_U32(address + 4), PPC_LOAD_U32(address + 16),
                    PPC_LOAD_U32(address + 8), PPC_LOAD_U32(address + 12)};
            }
            bool Valid() const
            {
                return base != 0 && capacity != 0 && uint64_t(base) + capacity <= UINT32_MAX &&
                    cursor >= base && end >= cursor && uint64_t(end) <= uint64_t(base) + capacity;
            }
            bool SameBuffer(const Stream& current) const
            {
                return Valid() && current.Valid() && base == current.base && capacity == current.capacity;
            }
            void Restore(uint8_t* base, uint32_t address) const
            {
                // Native seek grows the high-water mark; branching needs to
                // truncate it too, so stale future input cannot be replayed.
                PPC_STORE_U32(address + 8, cursor);
                PPC_STORE_U32(address + 12, end);
            }
        };
        struct Checkpoint
        {
            bool attempted{}, ready{}, finished{};
            Boundary boundary{};
            std::vector<uint8_t> bytes;
            uint32_t thread{}, guest{}, liveFrame{}, replayFrame{}, count{}, entries[18]{};
            uint8_t worldPlayback{};
            Stream input{}, snapshots{};
            float clock{}, deadline{};
            unsigned restores{};
        };
        thread_local Checkpoint s;

        void Discard(const char* reason)
        {
            fprintf(stderr, "[rewind-trial] discarded: %s\n", reason);
            if (s.guest) g_userHeap.Free(g_memory.Translate(s.guest));
            s.guest = 0;
            s.bytes.clear();
            s.finished = true;
        }

        bool SameEvent(const Boundary& b)
        {
            return CanObserve(b) && b.event == s.boundary.event && b.controller == s.boundary.controller;
        }

        void AtBoundary(PPCContext& ctx, uint8_t* base, uint32_t recorder)
        {
            if (!Repeats() || !s.attempted || s.finished || recorder != kRecorder) return;
            const auto b = ReadBoundary(base);
            if (!SameEvent(b) || s.thread != GuestThread::GetCurrentThreadId() ||
                !IsLiveClock(PPC_LOAD_U32(kClockFlags)) || PPC_LOAD_U8(kWorldPlayback) != s.worldPlayback)
            {
                Discard("left the original live event or owner thread");
                return;
            }
            if (!s.ready)
            {
                s.input = Stream::Read(base, recorder + 116);
                s.snapshots = Stream::Read(base, recorder + 140);
                s.count = PPC_LOAD_U32(recorder + 540);
                if (!s.input.Valid() || !s.snapshots.Valid() || s.count > 18)
                {
                    Discard("invalid native recorder bounds"); return;
                }
                for (unsigned i=0; i<18; ++i) s.entries[i] = PPC_LOAD_U32(recorder + 164 + i * 4);
                s.liveFrame = PPC_LOAD_U32(recorder + 532);
                s.replayFrame = PPC_LOAD_U32(recorder + 528);
                void* guest = g_userHeap.Alloc(s.bytes.size());
                if (!guest) { Discard("no guest staging buffer"); return; }
                s.guest = g_memory.MapVirtual(guest);
                s.ready = true;
                fprintf(stderr, "[rewind-trial] ready bytes=%zu clock=%g event=%08X guest=%08X frame=%u/%u checkpoints=%u input=%u snapshots=%u repeats=%u\n",
                    s.bytes.size(), s.clock, b.event, s.guest, s.liveFrame, s.replayFrame, s.count,
                    s.input.cursor - s.input.base, s.snapshots.cursor - s.snapshots.base, Repeats());
            }
            const float now = std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
            if (!std::isfinite(now) || now < s.clock) { Discard("clock left the captured timeline"); return; }
            if (now < s.clock + 2.f) return;
            if (!s.input.SameBuffer(Stream::Read(base, recorder + 116)) ||
                !s.snapshots.SameBuffer(Stream::Read(base, recorder + 140)))
            {
                Discard("native recorder buffer changed"); return;
            }

            memcpy(base + s.guest, s.bytes.data(), s.bytes.size());
            PPC_STORE_U32(s.guest, s.guest + kHeaderBytes);
            const auto started = std::chrono::steady_clock::now();
            bool restored = false;
            {
                struct RestoreContext
                {
                    PPCContext& context; PPCContext saved;
                    ~RestoreContext() { context = saved; }
                } restore{ctx, ctx};
                ctx.r3.u64 = s.guest;
                __imp__sub_823456A8(ctx, base);
                restored = ctx.r3.u32 != 0;
            }
            const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
            const float after = std::bit_cast<float>(PPC_LOAD_U32(kWorldClock));
            const auto afterBoundary = ReadBoundary(base);
            const uint32_t flags = PPC_LOAD_U32(kClockFlags);
            const uint32_t loadedPlayback = PPC_LOAD_U8(kWorldPlayback);
            fprintf(stderr, "[rewind-trial] restore=%u ok=%u clock=%g->%g target=%g flags=%08X playback=%u->%u event=%08X deadline=%g->%g ms=%.3f\n",
                ++s.restores, restored, now, after, s.clock, flags, loadedPlayback,
                s.worldPlayback, afterBoundary.event, s.deadline, std::bit_cast<float>(PPC_LOAD_U32(b.event + 3056)), ms);
            // A partially restored world cannot safely keep running. Preserve a
            // crash report for this isolated experiment instead of claiming success.
            if (!restored || !SameEvent(afterBoundary) || !IsLiveClock(flags) || !std::isfinite(after) || std::abs(after - s.clock) > 0.05f)
            {
                fprintf(stderr, "[rewind-trial] native restore contract failed\n");
                std::abort();
            }
            // Loading a native Replay checkpoint deliberately inhibits event
            // logic. The trial resumes live play, so undo that forced mode.
            PPC_STORE_U8(kWorldPlayback, s.worldPlayback);
            s.input.Restore(base, recorder + 116);
            s.snapshots.Restore(base, recorder + 140);
            PPC_STORE_U32(recorder + 532, s.liveFrame);
            PPC_STORE_U32(recorder + 528, s.replayFrame);
            PPC_STORE_U32(recorder + 540, s.count);
            for (unsigned i=0; i<18; ++i) PPC_STORE_U32(recorder + 164 + i * 4, s.entries[i]);
            if (s.restores >= Repeats()) Discard("requested restore attempts completed; inspect world/finish before accepting");
        }
    }

    void Captured(uint8_t* base, uint32_t header, uint32_t payload, uint32_t capacity)
    {
        if (!Repeats() || s.attempted || s.finished) return;
        const auto b = ReadBoundary(base);
        // This first trial has been exercised only in World Tour Road Rage.
        if (!CanObserve(b) || b.event != kRoadRageEvent || PPC_LOAD_U8(kWorldPlayback) != 0 ||
            (PPC_LOAD_U32(kClockFlags) & 0xFFFFFF00) != 0x01000000) return;
        const float clock = std::bit_cast<float>(PPC_LOAD_U32(header + 56));
        if (!std::isfinite(clock) || clock < 10.f) return;
        const uint32_t bytes = PPC_LOAD_U32(header + 4);
        if (!bytes || bytes > capacity || bytes > kMaxPayload || uint64_t(payload) + bytes > UINT32_MAX) return;
        s.attempted = true;
        s.boundary = b;
        s.thread = GuestThread::GetCurrentThreadId();
        s.clock = clock;
        s.worldPlayback = PPC_LOAD_U8(kWorldPlayback);
        s.deadline = std::bit_cast<float>(PPC_LOAD_U32(b.event + 3056));
        s.bytes.resize(kHeaderBytes + bytes);
        memcpy(s.bytes.data(), base + header, kHeaderBytes);
        memcpy(s.bytes.data() + kHeaderBytes, base + payload, bytes);
    }
}

extern "C" PPC_FUNC(__imp__sub_82346C68);
PPC_FUNC(sub_82346C68)
{
    const uint32_t recorder = ctx.r3.u32;
    __imp__sub_82346C68(ctx, base);
    game::rewind::trial::AtBoundary(ctx, base, recorder);
    game::rewind::ring::AtBoundary(ctx, base, recorder);
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See timeline.h.
#include <stdafx.h>
#include "timeline.h"
#include <cpu/guest_time.h>
#include <kernel/function.h>

namespace timeline
{
    namespace
    {
        struct Event { int64_t ns; uint32_t arg; uint8_t kind; uint8_t thread; };
        constexpr uint32_t kMax = 1 << 20;
        constexpr int64_t kWindowNs = 200'000'000;
        const char* kNames[Count] = { "wait+", "wait-", "present+", "present-", "batch+", "batch-",
            "submit", "complete", "write/ev", "write/done", "swap", "regmem+", "regmem-", "interrupt", "submit+", "waittarget" };

        std::chrono::steady_clock::time_point s_launch = std::chrono::steady_clock::now();
        int64_t s_fromNs = [] {
            const char* v = std::getenv("BURNOUT_TIMELINE");
            return v ? int64_t(std::atof(v) * 1e9) : int64_t(-1);
        }();
        std::unique_ptr<Event[]> s_events;
        std::atomic<uint32_t> s_next{ 0 };
        std::atomic<bool> s_dumped{ false };
        std::atomic<uint8_t> s_threadIds{ 0 };

        void Dump()
        {
            uint32_t n = std::min(s_next.load(), kMax);
            std::vector<Event> ev(s_events.get(), s_events.get() + n);
            std::sort(ev.begin(), ev.end(), [](auto& a, auto& b) { return a.ns < b.ns; });
            FILE* f = fopen("build/timeline.txt", "w");
            if (!f)
                return;
            // Summary: game wait share, GPU queue occupancy, per-kind counts.
            int64_t t0 = ev.empty() ? 0 : ev.front().ns;
            int64_t waitNs = 0, waitStart = -1, busyNs = 0, busyStart = -1;
            int inFlight = 0;
            uint32_t counts[Count] = {};
            for (auto& e : ev)
            {
                counts[e.kind]++;
                if (e.kind == GameWaitBegin && waitStart < 0) waitStart = e.ns;
                if (e.kind == GameWaitEnd && waitStart >= 0) { waitNs += e.ns - waitStart; waitStart = -1; }
                if (e.kind == Submit && inFlight++ == 0) busyStart = e.ns;
                if (e.kind == Complete && inFlight > 0 && --inFlight == 0) busyNs += e.ns - busyStart;
            }
            double span = ev.empty() ? 1 : double(ev.back().ns - t0);
            fprintf(f, "# %.1f ms: game waiting %.0f%%, GPU queue non-empty %.0f%%\n", span / 1e6, 100.0 * waitNs / span, 100.0 * busyNs / span);
            for (int k = 0; k < Count; k++)
                fprintf(f, "# %-10s %u\n", kNames[k], counts[k]);
            for (auto& e : ev)
                fprintf(f, "%10.3f t%u %-10s %08X\n", (e.ns - t0) / 1e6, e.thread, kNames[e.kind], e.arg);
            fclose(f);
            fprintf(stderr, "[timeline] wrote build/timeline.txt (%u events)\n", n);
        }
    }

    bool g_enabled = s_fromNs >= 0;

    void Record(Kind kind, uint32_t arg)
    {
        int64_t ns = std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now() - s_launch).count();
        if (ns < s_fromNs)
            return;
        if (ns > s_fromNs + kWindowNs)
        {
            if (!s_dumped.exchange(true))
            {
                g_enabled = false;
                Dump();
            }
            return;
        }
        static std::once_flag once;
        std::call_once(once, [] { s_events.reset(new Event[kMax]); });
        thread_local uint8_t thread = s_threadIds.fetch_add(1);
        uint32_t i = s_next.fetch_add(1);
        if (i < kMax)
            s_events[i] = { ns, arg, uint8_t(kind), thread };
    }
}


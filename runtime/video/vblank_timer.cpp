// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See vblank_timer.h.
#include "vblank_timer.h"

#include <algorithm>
#include <cstdlib>
#include <string_view>

#ifdef __APPLE__
#include <TargetConditionals.h>
#endif
// macOS only: iOS keeps the ordinary thread (its vblank timing hasn't been measured).
#if defined(__APPLE__) && !TARGET_OS_IOS
#define BR_REALTIME_VBLANK 1
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#endif

namespace video
{
    namespace
    {
#ifdef BR_REALTIME_VBLANK
        uint32_t ToMach(int64_t ns)
        {
            static const mach_timebase_info_data_t timebase = [] {
                mach_timebase_info_data_t tb{};
                mach_timebase_info(&tb);
                return tb;
            }();
            return uint32_t(double(std::max<int64_t>(ns, 0)) * double(timebase.denom) / double(timebase.numer));
        }
#endif

        int64_t Percentile(std::vector<int64_t> v, size_t permille)
        {
            if (v.empty())
                return 0;
            size_t k = std::min(v.size() - 1, v.size() * permille / 1000);
            std::nth_element(v.begin(), v.begin() + ptrdiff_t(k), v.end());
            return v[k];
        }
    }

    VblankTimer ChosenVblankTimer()
    {
        const char* v = std::getenv("BURNOUT_VBLANK_TIMER");
        return v && std::string_view(v) == "sleep" ? VblankTimer::Ordinary : VblankTimer::RealTime;
    }

    bool PrepareVblankThread(VblankTimer timer, int64_t periodNs)
    {
#ifdef BR_REALTIME_VBLANK
        if (timer != VblankTimer::RealTime)
            return false;
        // Woken at its time, ahead of the game's own threads: it runs the
        // guest's vblank handler (microseconds) and sleeps again.
        thread_time_constraint_policy_data_t policy{};
        policy.period = ToMach(periodNs);
        policy.computation = ToMach(1'000'000);
        policy.constraint = ToMach(2'000'000);
        policy.preemptible = 1;
        return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
                   reinterpret_cast<thread_policy_t>(&policy), THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
#else
        (void)timer;
        (void)periodNs;
        return false;
#endif
    }

    const char* VblankTimerName(VblankTimer timer, bool realtime)
    {
#ifdef BR_REALTIME_VBLANK
        if (timer == VblankTimer::RealTime)
            return realtime ? "real-time" : "ordinary (real-time refused)";
        return "ordinary (BURNOUT_VBLANK_TIMER=sleep)";
#else
        (void)timer;
        (void)realtime;
        return "ordinary";
#endif
    }

    void VblankTiming::Add(int64_t lateNs, int64_t handlerNs)
    {
        if (m_late.size() < kCount)
        {
            m_late.push_back(lateNs);
            m_handler.push_back(handlerNs);
            return;
        }
        m_late[m_next] = lateNs;
        m_handler[m_next] = handlerNs;
        m_next = (m_next + 1) % kCount;
    }

    VblankTiming::Summary VblankTiming::Summarize() const
    {
        Summary s;
        s.count = uint32_t(m_late.size());
        if (m_late.empty())
            return s;
        s.lateMedianNs = double(Percentile(m_late, 500));
        s.late99Ns = double(Percentile(m_late, 990));
        s.lateMaxNs = double(*std::max_element(m_late.begin(), m_late.end()));
        s.handler99Ns = double(Percentile(m_handler, 990));
        return s;
    }
}

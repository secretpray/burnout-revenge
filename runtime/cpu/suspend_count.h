// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// A guest thread's suspend count, which one resume may take below 0.
//
// Burnout's movie player suspends its workers after their work and the main
// thread resumes them for more. A resume that lands before the worker's own
// suspend (often, on our cores) would be lost, as a plain count loses it, and
// once the last one before the main thread waits for their output (E_PENDING)
// was, the game hung at launch (1 in ~100 with Skip Intro Movies; ACTIVE_WORK
// 3b). So that resume is kept: the next self-suspend returns at once and the
// worker looks for work again, as its loop does after any wake. One is kept
// at most, so resumes don't pile up into suspends skipped later.
#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>

class SuspendCount
{
public:
    explicit SuspendCount(bool suspended) : m_count(suspended ? 1 : 0) {}

    void WaitWhileSuspended()
    {
        for (int32_t count = m_count.load(); count > 0; count = m_count.load())
            m_count.wait(count);
    }

    // The calling thread suspends itself; returns the previous count.
    uint32_t SuspendSelf()
    {
        const int32_t previous = m_count.fetch_add(1);
        WaitWhileSuspended();
        return uint32_t(std::max(previous, 0));
    }

    // Returns the previous count (0: it was running).
    uint32_t Resume()
    {
        int32_t count = m_count.load();
        while (count >= 0 && !m_count.compare_exchange_weak(count, count - 1)) {}
        if (count < 0)
            return 0;  // a resume is kept already
        m_count.notify_all();
        return uint32_t(count);
    }

    int32_t Value() const { return m_count.load(); }

private:
    std::atomic<int32_t> m_count;
};

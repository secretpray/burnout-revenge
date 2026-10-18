// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// A guest thread's suspend count (cpu/suspend_count.h): a resume that comes
// before the thread suspends itself is kept, one at most.
#include <cpu/suspend_count.h>

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstdio>
#include <thread>

int main()
{
    {
        // Created suspended: it waits for the first resume.
        SuspendCount count(true);
        std::atomic<bool> started = false;
        std::thread t([&] { count.WaitWhileSuspended(); started = true; });
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(!started);
        assert(count.Resume() == 1);
        t.join();
        assert(started && count.Value() == 0);
    }
    {
        // A resume of a running thread is kept for its next suspend, one only.
        SuspendCount count(false);
        assert(count.Resume() == 0 && count.Value() == -1);
        assert(count.Resume() == 0 && count.Value() == -1);
        assert(count.SuspendSelf() == 0 && count.Value() == 0);  // returns at once
    }
    {
        // A worker that suspends itself after each piece of work, resumed for
        // each new piece, often before its suspend: no piece is left waiting.
        SuspendCount count(false);
        std::atomic<int> posted = 0, done = 0;
        constexpr int kWork = 20000;
        std::thread worker([&] {
            while (done < kWork)
            {
                while (done < posted)
                    ++done;
                if (done < kWork)
                    count.SuspendSelf();
            }
        });
        for (int i = 0; i < kWork; ++i)
        {
            ++posted;
            count.Resume();
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (done < posted)
                assert(std::chrono::steady_clock::now() < deadline);
        }
        worker.join();
    }
    puts("suspend_count_test: early resumes kept, one at most; no lost wake-up");
}

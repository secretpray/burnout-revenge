// The guest vblank's timer (runtime/video/vblank_timer.cpp) on this machine:
// how late a thread sleeping to vblanks 16.667 ms apart wakes, as an ordinary
// thread and as the Vsync thread prepares itself, alone and with every core
// kept busy by threads of the class the game's own busy threads run at (the
// command processor, the renderer, audio: user-interactive on macOS).
// From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/vblank_timer_test.cpp runtime/video/vblank_timer.cpp -o build/vblank_timer_test
//   build/vblank_timer_test [seconds per run, default 3]
// Checked on macOS only, where the thread changed (2026-10-12): prepared, it
// wakes within 1.5 ms in 99% of vblanks and 0.5 ms at the median, loaded or
// not, real-time scheduling granted. The ordinary thread's numbers are
// printed beside it (the vblank's until then: 3.2 ms late at the median in
// the game, up to 9).
#include <video/vblank_timer.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
#ifdef __APPLE__
#include <TargetConditionals.h>
#include <pthread.h>
#endif

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    constexpr int64_t kPeriodNs = 16'666'667;

    int64_t NowNs()
    {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    }

    struct Result
    {
        video::VblankTiming::Summary late;
        bool realtime = false;
    };

    // A thread as the Vsync thread runs: prepared, then sleeping to each
    // vblank in turn (the time it should have fired, not the time it woke).
    Result Run(video::VblankTimer timer, double seconds)
    {
        Result r;
        std::thread t([&] {
            r.realtime = video::PrepareVblankThread(timer, kPeriodNs);
            video::VblankTiming timing;
            int64_t next = NowNs() + kPeriodNs;
            const int count = int(seconds * 60.0);
            for (int i = 0; i < count; i++)
            {
                std::this_thread::sleep_until(std::chrono::steady_clock::time_point(std::chrono::nanoseconds(next)));
                timing.Add(NowNs() - next, 0);
                next += kPeriodNs;
            }
            r.late = timing.Summarize();
        });
        t.join();
        return r;
    }

    // Every core busy (each thread spins, yielding now and then, as the
    // command processor's idle poll does).
    struct Load
    {
        std::atomic<bool> stop{ false };
        std::vector<std::thread> threads;
        explicit Load(unsigned count)
        {
            for (unsigned i = 0; i < count; i++)
                threads.emplace_back([this] {
#ifdef __APPLE__
                    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
                    volatile uint64_t x = 0;
                    while (!stop.load(std::memory_order_relaxed))
                    {
                        for (int k = 0; k < 20000; k++)
                            x = x + uint64_t(k);
                        std::this_thread::yield();
                    }
                });
        }
        ~Load()
        {
            stop = true;
            for (std::thread& t : threads)
                t.join();
        }
    };

    void Print(const char* name, const Result& r)
    {
        printf("  %-44s late %.3f ms (median), %.3f ms (99%%), %.3f ms at most (%u vblanks)%s\n", name, r.late.lateMedianNs / 1e6,
            r.late.late99Ns / 1e6, r.late.lateMaxNs / 1e6, r.late.count, r.realtime ? "; real-time" : "");
    }
}

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::max(1.0, std::atof(argv[1])) : 3.0;
    const unsigned cores = std::max(2u, std::thread::hardware_concurrency());
    printf("vblanks every 16.667 ms, %.0f s each; %u cores\n", seconds, cores);
    Result ordinaryIdle = Run(video::VblankTimer::Ordinary, seconds);
    Print("an ordinary thread, idle machine", ordinaryIdle);
    Result vsyncIdle = Run(video::VblankTimer::RealTime, seconds);
    Print("the Vsync thread's, idle machine", vsyncIdle);
    Result ordinaryLoaded, vsyncLoaded;
    {
        Load load(cores);
        ordinaryLoaded = Run(video::VblankTimer::Ordinary, seconds);
        Print("an ordinary thread, every core busy", ordinaryLoaded);
        vsyncLoaded = Run(video::VblankTimer::RealTime, seconds);
        Print("the Vsync thread's, every core busy", vsyncLoaded);
    }
#if defined(__APPLE__) && !TARGET_OS_IOS
    for (const Result* r : { &vsyncIdle, &vsyncLoaded })
    {
        CHECK(r->late.late99Ns <= 1.5e6, "the Vsync thread within 1.5 ms in 99%% of vblanks (%.3f ms)", r->late.late99Ns / 1e6);
        CHECK(r->late.lateMedianNs <= 0.5e6, "and 0.5 ms at the median (%.3f ms)", r->late.lateMedianNs / 1e6);
        CHECK(r->realtime, "real-time scheduling granted");
    }
#else
    printf("  (not checked here: the Vsync thread is an ordinary one on this platform)\n");
#endif
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}

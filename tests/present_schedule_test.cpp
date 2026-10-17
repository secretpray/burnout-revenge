// Tests for video::PresentSchedule (runtime/video/present_schedule.h): when the
// Mac shows a new frame at a set time after its vblank, and the watchdog that
// turns it off. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/present_schedule_test.cpp -o build/present_schedule_test
//   build/present_schedule_test
#include <video/present_schedule.h>

#include <cstdint>
#include <cstdio>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

using video::PresentSchedule;

namespace
{
    constexpr int64_t kMs = 1'000'000;

    void Gate()
    {
        printf("when frames are shown at a time:\n");
        struct Row
        {
            const char* name;
            PresentSchedule::Mode mode;
            double hz;
            bool locked, want;
        };
        const Row rows[] = {
            { "ProMotion 120 Hz, not locked (measured: 99.9% at 16.67 ms)", PresentSchedule::Mode::Auto, 120.0, false, true },
            { "ProMotion 120 Hz, the lock took it", PresentSchedule::Mode::Auto, 120.0, true, false },
            { "a 60 Hz display, not locked yet (the macOS 15 VM: black)", PresentSchedule::Mode::Auto, 60.0, false, false },
            { "a 60 Hz display, locked", PresentSchedule::Mode::Auto, 60.0, true, false },
            { "a 59.94 Hz TV", PresentSchedule::Mode::Auto, 59.94, false, false },
            { "a 144 Hz monitor (not measured)", PresentSchedule::Mode::Auto, 144.0, false, false },
            { "a 90 Hz display (not measured)", PresentSchedule::Mode::Auto, 90.0, false, false },
            { "119.88 Hz", PresentSchedule::Mode::Auto, 119.88, false, true },
            { "the display's rate unknown", PresentSchedule::Mode::Auto, 0.0, false, false },
            { "BURNOUT_PRESENT_AT=<ms>, 60 Hz, locked", PresentSchedule::Mode::On, 60.0, true, true },
            { "BURNOUT_PRESENT_AT=0, ProMotion", PresentSchedule::Mode::Off, 120.0, false, false },
        };
        for (const Row& r : rows)
        {
            PresentSchedule s(r.mode);
            s.SetDisplayHz(r.hz);
            CHECK(s.Wanted(r.locked) == r.want, "%s: %s", r.name, r.want ? "at a time" : "when ready");
        }
        // The display changes under the game (the window moves to another one).
        PresentSchedule s;
        s.SetDisplayHz(120.0);
        CHECK(s.Wanted(false), "on the MacBook's panel: at a time");
        s.SetDisplayHz(60.0);
        CHECK(!s.Wanted(false), "moved to a 60 Hz monitor: when ready");
        s.SetDisplayHz(120.0);
        CHECK(s.Wanted(false), "and back: at a time");
    }

    void Watchdog()
    {
        printf("the watchdog:\n");
        // ProMotion as measured: shown 0-8 ms after the time asked for, now
        // and then a frame a step or two late (its GPU work not done): never trips.
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            bool tripped = false;
            for (int i = 0; i < 20000; i++)
            {
                tripped |= s.Presented();
                const int64_t late = i % 50 == 0 ? 25 * kMs : (i % 3) * 4 * kMs;
                tripped |= s.Reported(true, late);
            }
            CHECK(!tripped && !s.Tripped() && s.Wanted(false), "frames shown at their time, some late: never trips");
        }
        // Reports a present behind (the driver reports a frame after its
        // wait returned): fine.
        {
            PresentSchedule s(PresentSchedule::Mode::On);
            bool tripped = s.Presented();
            for (int i = 0; i < 1000; i++)
            {
                tripped |= s.Presented();
                tripped |= s.Reported(true, 2 * kMs);
            }
            CHECK(!tripped, "reports one present behind: fine");
        }
        // The macOS 15 VM's black screen: every frame reported shown ~9 ms
        // before its time (3-14 ms; the time not kept). Trips at the 60th.
        {
            PresentSchedule s(PresentSchedule::Mode::On);
            int at = -1;
            for (int i = 0; i < 1000 && at < 0; i++)
            {
                s.Presented();
                if (s.Reported(true, -(3 + i % 12) * kMs))
                    at = i;
            }
            CHECK(at == int(PresentSchedule::kBadOfWindow) - 1 && s.GetReason() == PresentSchedule::Reason::NotKept && s.Early() == 60,
                "shown 3-14 ms early: trips at the %uth (%d)", PresentSchedule::kBadOfWindow, at + 1);
            CHECK(!s.Wanted(false) && !s.Wanted(true), "then when ready, forced on or not");
        }
        // The VM's first seconds with frames shown at a time: each reported
        // ~100 ms after its present (88 ms after the time asked for), until
        // the display froze. Trips at the 60th.
        {
            PresentSchedule s(PresentSchedule::Mode::On);
            int at = -1;
            for (int i = 0; i < 1000 && at < 0; i++)
                if (s.Reported(true, (80 + i % 20) * kMs))
                    at = i;
            CHECK(at == int(PresentSchedule::kBadOfWindow) - 1, "80-99 ms late: trips at the %uth (%d)", PresentSchedule::kBadOfWindow, at + 1);
        }
        // ProMotion as measured: up to 21 ms late (a frame's GPU work not
        // done in time), never more than 1.2 ms early (0 of 46,000).
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            bool tripped = false;
            for (int i = 0; i < 46000; i++)
                tripped |= s.Reported(true, (i % 5 == 0 ? 21 : i % 9) * kMs);
            CHECK(!tripped, "up to 21 ms late: no trip");
        }
        // ProMotion as measured: never more than 1.2 ms early (0 of 46,000).
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            bool tripped = false;
            for (int i = 0; i < 46000; i++)
                tripped |= s.Reported(true, (i % 7 == 0 ? -1'200'000 : (i % 9) * kMs));
            CHECK(!tripped, "1.2 ms early now and then: no trip");
        }
        // Never shown (Metal's presentedTime 0): trips within kWindow reports.
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            int at = -1;
            for (int i = 0; i < 1000 && at < 0; i++)
            {
                s.Presented();
                if (s.Reported(false, 0))
                    at = i;
            }
            CHECK(at == int(PresentSchedule::kBadOfWindow) - 1, "never shown: trips at the %uth (%d)", PresentSchedule::kBadOfWindow, at + 1);
            CHECK(s.Tripped() && !s.Wanted(false) && s.GetReason() == PresentSchedule::Reason::NotKept && s.NotShown() == 60,
                "then when ready, for good");
            CHECK(!s.Reported(true, 0) && !s.Presented(), "a trip is reported once");
            s.SetDisplayHz(60.0);
            s.SetDisplayHz(120.0);
            CHECK(!s.Wanted(false), "still off after a display change (the session)");
        }
        // Shown, but far from the time asked for (a clock taken for another).
        {
            PresentSchedule s(PresentSchedule::Mode::On);
            bool tripped = false;
            for (int i = 0; i < 200 && !tripped; i++)
                tripped = s.Reported(true, 2'000 * kMs);
            CHECK(tripped && !s.Wanted(true), "shown 2 s late: trips (even when forced on)");
        }
        // Half of them never shown: trips; fewer: doesn't.
        {
            PresentSchedule a, b;
            a.SetDisplayHz(120.0);
            b.SetDisplayHz(120.0);
            bool ta = false, tb = false;
            for (int i = 0; i < 2000; i++)
            {
                ta |= a.Reported(i % 2 == 0, 0);
                tb |= b.Reported(i % 3 != 0, 0);
            }
            CHECK(ta, "every other frame never shown: trips");
            CHECK(!tb, "one in three never shown: doesn't (%u of %u bad)", b.BadInWindow(), b.ReportedInWindow());
        }
        // Presented, never reported: trips after kUnreported.
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            int at = -1;
            for (int i = 0; i < 1000 && at < 0; i++)
                if (s.Presented())
                    at = i;
            CHECK(at == int(PresentSchedule::kUnreported) - 1 && s.GetReason() == PresentSchedule::Reason::Unreported,
                "no reports: trips at the %uth (%d)", PresentSchedule::kUnreported, at + 1);
        }
        // A burst of bad reports after a long good run (a load's hitch, a
        // fullscreen switch): kBadOfWindow is needed within kWindow.
        {
            PresentSchedule s;
            s.SetDisplayHz(120.0);
            bool tripped = false;
            for (int i = 0; i < 5000; i++)
                tripped |= s.Reported(true, 3 * kMs);
            for (int i = 0; i < int(PresentSchedule::kBadOfWindow) - 1; i++)
                tripped |= s.Reported(false, 0);
            for (int i = 0; i < 5000; i++)
                tripped |= s.Reported(true, 3 * kMs);
            CHECK(!tripped, "%u bad in a row among good ones: no trip", PresentSchedule::kBadOfWindow - 1);
        }
    }
}

int main()
{
    Gate();
    Watchdog();
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}

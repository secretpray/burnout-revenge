// Tests for game::WholeVblanks (runtime/game/whole_vblanks.h): the guest
// vblanks GameFrameTime gives each frame as its game time. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/whole_vblanks_test.cpp -o build/whole_vblanks_test
//   build/whole_vblanks_test [frames.csv[@30] ...]
// Without arguments: synthetic frames against vblanks. With BURNOUT_GAME_TIME_LOG
// files (measured_ms,vblanks,...; "@30" after a path: Frame Rate 30): also the
// game time each rule gives their frames (all but loads: those of 0 to
// WholeVblanks::kLoadVblanks), against the measured time and the vblanks that
// fired.
#include <game/whole_vblanks.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    constexpr double kRefreshMs = 1000.0 / 60.0;

    // The rule until 2026-10-07: the count when the measured time is within
    // half a vblank of it, else the measured time.
    uint64_t HalfVblankRule(double measuredMs, uint64_t vblanks, double vblankMs)
    {
        if (vblanks < 1 || vblanks > 6)
            return 0;
        return std::abs(measuredMs / vblankMs - double(vblanks)) > 0.5 ? 0 : vblanks;
    }

    // The rule from 2026-10-07 to 2026-10-11: each frame its own count,
    // game time level with the vblanks after every frame (a frame that saw
    // none kept its measured time and was given back by the next ones).
    struct CountRule
    {
        double ahead = 0.0;
        uint64_t Give(double measuredMs, uint64_t vblanks, double vblankMs)
        {
            if (vblanks > 6)
            {
                ahead = 0.0;
                return 0;
            }
            if (vblanks == 0)
            {
                ahead = std::min(ahead + std::max(measuredMs, 0.0) / vblankMs, 6.0);
                return 0;
            }
            double owed = double(vblanks) - ahead;
            int64_t give = std::clamp<int64_t>(std::llround(owed), 1, 6);
            ahead = double(give) - owed;
            return uint64_t(give);
        }
    };

    // v0.1.3's rule (2026-10-11 to 2026-10-12): as WholeVblanks, but a
    // frame of more than 6 vblanks kept its measured time and started over
    // (ahead 0), and nothing was owed beyond kSlack. Below 15 fps the frame
    // after each such one, kSlack behind again, got kSlack fewer than fired.
    struct V013Rule
    {
        double ahead = 0.0;
        static constexpr double kSlack = game::WholeVblanks::kSlack;
        uint64_t Give(double measuredMs, uint64_t vblanks, double vblankMs)
        {
            if (vblanks > 6 || !(vblankMs > 0.0))
            {
                ahead = 0.0;
                return 0;
            }
            ahead -= double(vblanks);
            if (ahead + 1.0 > kSlack)
            {
                ahead = std::min(ahead + std::max(measuredMs, 0.0) / vblankMs, kSlack);
                return 0;
            }
            int64_t give = 1;
            if (ahead + 1.0 < -kSlack)
                give = std::clamp<int64_t>(std::llround(-kSlack - ahead), 1, 6);
            ahead = std::max(ahead + double(give), -kSlack);
            return uint64_t(give);
        }
    };

    struct Frame
    {
        double ms;
        uint64_t vblanks;
    };

    // The world steps a frame runs: what it was given, or its measured time
    // in steps, and at most 6 (the stepper's cap; the steps over it are dropped).
    double WorldSteps(uint64_t given, double ms, double vblankMs)
    {
        return given ? double(std::min<uint64_t>(given, 6)) : std::min(ms / vblankMs, 6.0);
    }

    // GitHub issue #9's mechanism: the main loop runs up to `queue` frames
    // ahead of a display that shows one frame a vblank (the render thread
    // and the GPU ring hold them). A frame's work is 4-10 ms, and with
    // probability `hiccupP` 15 ms or more longer (streaming, a page fault):
    // up to `hiccupMs`. The loop starts a frame when the last one is done
    // and the queue has room (a vblank took a frame from it). Returns the
    // frames' measured times and counts, and how many vblanks found the
    // queue empty (the display showed a frame again).
    std::vector<Frame> QueuedLoop(int queue, double hiccupP, double hiccupMs, int count, uint32_t seed, int& dry)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::vector<double> starts, done;
        std::vector<int> shownAt;  // the vblank each frame was first shown at
        double t = 0.0;
        int nextShown = 0;          // frames shown so far
        int vblank = 1;             // the next vblank to fire, at vblank * kRefreshMs
        dry = 0;
        auto fireUntil = [&](double until) {
            for (; double(vblank) * kRefreshMs <= until; vblank++)
            {
                if (nextShown < int(done.size()) && done[size_t(nextShown)] <= double(vblank) * kRefreshMs)
                    shownAt.push_back(vblank), nextShown++;
                else if (nextShown > 0)
                    dry++;
            }
        };
        for (int k = 0; k <= count; k++)
        {
            // Room in the queue: frames done but not shown fewer than `queue`.
            while (int(done.size()) - nextShown >= queue)
            {
                double next = double(vblank) * kRefreshMs;
                fireUntil(next);
                t = std::max(t, next);
            }
            fireUntil(t);
            starts.push_back(t);
            double work = 4.0 + 6.0 * unit(rng);
            if (unit(rng) < hiccupP)
                work += 15.0 + (hiccupMs - 15.0) * unit(rng);
            t += work;
            done.push_back(t);
            fireUntil(t);
        }
        std::vector<Frame> out;
        for (int k = 0; k < count; k++)
        {
            double a = starts[size_t(k)], b = starts[size_t(k) + 1];
            uint64_t n = uint64_t(std::floor(b / kRefreshMs) - std::floor(a / kRefreshMs));
            out.push_back({ b - a, n });
        }
        return out;
    }

    struct Totals
    {
        double measuredMs = 0, vblankMs = 0, newMs = 0, oldMs = 0, v013Ms = 0;
        double newSteps = 0, v013Steps = 0;  // world steps (6 a frame at most)
        uint64_t frames = 0, given = 0, paced = 0, pacedGivenOne = 0, notAsV013 = 0;
        int maxSteps = 0;
    };

    // Each rule's game time for a run of frames.
    Totals Play(const std::vector<Frame>& frames, double vblankMs)
    {
        game::WholeVblanks rule;
        V013Rule v013;
        Totals t;
        for (const Frame& f : frames)
        {
            uint64_t k = rule.Give(f.ms, f.vblanks, vblankMs), old = HalfVblankRule(f.ms, f.vblanks, vblankMs),
                     prev = v013.Give(f.ms, f.vblanks, vblankMs);
            t.measuredMs += f.ms;
            t.vblankMs += double(f.vblanks) * vblankMs;
            t.newMs += k ? double(k) * vblankMs : f.ms;
            t.oldMs += old ? double(old) * vblankMs : f.ms;
            t.v013Ms += prev ? double(prev) * vblankMs : f.ms;
            t.newSteps += WorldSteps(k, f.ms, vblankMs);
            t.v013Steps += WorldSteps(prev, f.ms, vblankMs);
            t.notAsV013 += k != prev;
            t.frames++;
            t.given += k != 0;
            t.maxSteps = std::max(t.maxSteps, int(k));
            if (f.vblanks == 1 && std::abs(f.ms / vblankMs - 1.0) <= 0.5)
            {
                t.paced++;
                t.pacedGivenOne += k == 1;
            }
        }
        return t;
    }

    // `count` frames against vblanks due every `vblankMs`, each firing 0 to
    // `lateMs` late (a vblank thread's wake-up; it catches up: one fires
    // before the next is due). Paced: frame i starts 0 to `jitterMs` after
    // vblank i fired, which released it (60 fps held). Else frames last
    // `meanMs` +- `jitterMs` (GPU-bound), so their starts wander between
    // vblanks. A frame's count: the vblanks that fired after its start, up
    // to the next one's (GameFrameTime reads the count at each frame's start).
    std::vector<Frame> Frames(double meanMs, double jitterMs, double vblankMs, double lateMs, bool paced, int count, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::vector<double> fired;
        double last = paced ? double(count + 2) * vblankMs : double(count + 2) * (meanMs + jitterMs);
        for (double due = 0.0; due <= last + vblankMs; due += vblankMs)
            fired.push_back(due + unit(rng) * std::min(lateMs, vblankMs * 0.95));
        std::vector<double> starts;
        double t = unit(rng) * vblankMs;
        for (int i = 0; i <= count; i++)
        {
            starts.push_back(paced ? fired[size_t(i)] + 0.05 + unit(rng) * jitterMs : t);
            t += meanMs + (2 * unit(rng) - 1) * jitterMs;
        }
        std::vector<Frame> out;
        size_t v = 0;
        for (int i = 0; i < count; i++)
        {
            uint64_t n = 0;
            while (v < fired.size() && fired[v] <= starts[i])
                v++;
            while (v < fired.size() && fired[v] <= starts[i + 1])
                v++, n++;
            out.push_back({ starts[i + 1] - starts[i], n });
        }
        return out;
    }

    // Issue #9: the queued loop at 60 fps held. With no vblank finding the
    // queue empty, every frame is shown for one vblank and must get one.
    void QueuedLoops()
    {
        struct Case
        {
            const char* name;
            int queue;
            double hiccupP, hiccupMs;
        };
        const Case cases[] = {
            { "queue 2, a 15-30 ms hiccup every ~3 s", 2, 1.0 / 180, 30.0 },
            { "queue 3, a 15-45 ms hiccup every ~3 s", 3, 1.0 / 180, 45.0 },
            { "queue 3, a 15-45 ms hiccup every ~0.5 s (a Deck-like CPU)", 3, 1.0 / 30, 45.0 },
            { "queue 2, a 15-80 ms hiccup every ~1 s (some run the queue dry)", 2, 1.0 / 60, 80.0 },
        };
        for (const Case& c : cases)
        {
            uint64_t frames = 0, newOff = 0, oldOff = 0, dryTotal = 0;
            double worst = 0;
            for (int p = 0; p < 8; p++)
            {
                int dry = 0;
                std::vector<Frame> loop = QueuedLoop(c.queue, c.hiccupP, c.hiccupMs, 6000, 77u + uint32_t(p), dry);
                game::WholeVblanks rule;
                CountRule old;
                double given = 0, fired = 0;
                for (const Frame& f : loop)
                {
                    uint64_t k = rule.Give(f.ms, f.vblanks, kRefreshMs), o = old.Give(f.ms, f.vblanks, kRefreshMs);
                    newOff += k != 1;
                    oldOff += o != 1;
                    given += k ? double(k) : f.ms / kRefreshMs;
                    fired += double(f.vblanks);
                }
                frames += loop.size();
                dryTotal += uint64_t(dry);
                worst = std::max(worst, std::abs(given - fired));
            }
            printf("  %-62s frames not given 1: %llu (the rule until 2026-10-11: %llu) of %llu; vblanks with the queue empty %llu; "
                "game time within %.2f vblanks\n", c.name, (unsigned long long)newOff, (unsigned long long)oldOff,
                (unsigned long long)frames, (unsigned long long)dryTotal, worst);
            CHECK(oldOff > 0, "%s: the old rule's hitches are there to remove (%llu)", c.name, (unsigned long long)oldOff);
            if (dryTotal == 0)
                CHECK(newOff == 0, "%s: issue #9: every frame given 1 while the display shows one a vblank (%llu not)", c.name,
                    (unsigned long long)newOff);
            else
                CHECK(newOff <= dryTotal && newOff < oldOff, "%s: only frames after an empty queue given more (%llu, %llu empty, old %llu)",
                    c.name, (unsigned long long)newOff, (unsigned long long)dryTotal, (unsigned long long)oldOff);
            CHECK(worst <= game::WholeVblanks::kSlack + 1.0, "%s: game time within kSlack + 1 vblanks (%.2f)", c.name, worst);
        }
    }

    void Synthetic()
    {
        // Frame by frame, from a fresh start.
        {
            game::WholeVblanks r;
            CHECK(r.Give(16.6, 1, kRefreshMs) == 1, "a 60 fps frame: 1");
            CHECK(r.ahead == 0.0, "nothing ahead");
            CHECK(HalfVblankRule(44.0, 2, kRefreshMs) == 0, "the old rule's miss: 44 ms, 2 fired, kept 44 ms");
            // GPU-bound 44 ms frames (2.64 refreshes, 2 or 3 fired): game time
            // falls kSlack behind first, then each gets its count.
            CHECK(r.Give(44.0, 3, kRefreshMs) == 1 && r.ahead == -2.0, "first slow frame: 1 (%.2f behind)", -r.ahead);
            CHECK(r.Give(44.0, 2, kRefreshMs) == 1 && r.ahead == -3.0, "then 1, kSlack behind (%.2f)", -r.ahead);
            CHECK(r.Give(44.0, 3, kRefreshMs) == 3, "3 fired, kSlack behind: 3");
            CHECK(r.Give(44.0, 2, kRefreshMs) == 2, "2 fired: 2");
            CHECK(r.ahead == -game::WholeVblanks::kSlack, "and stays kSlack behind (%.2f)", r.ahead);
            CHECK(r.Give(44.0, 1, 2 * kRefreshMs) == 1 && r.Give(44.0, 2, 2 * kRefreshMs) == 2,
                "Frame Rate 30: 44 ms is 1.32 guest vblanks of 33.3 ms; 1 or 2 fired, given");
            r.Reset();
            CHECK(r.ahead == 0.0, "reset");
            // GitHub issue #9: at 60 fps held, the main loop's frames drift
            // against the vblanks while the display shows one frame a vblank
            // from its queue: a slow frame sees 2 or 3, the next ones none.
            // Each is shown for one vblank and gets one.
            const uint64_t drift[] = { 1, 2, 0, 1, 1, 3, 0, 0, 1, 2, 1, 0, 1 };
            bool allOne = true;
            for (uint64_t k : drift)
                allOne &= r.Give(k == 0 ? 4.0 : 16.6 * double(k), k, kRefreshMs) == 1;
            CHECK(allOne && r.ahead == 0.0, "issue #9: every drifting frame given 1, nothing left over (%.2f)", r.ahead);
            // A slow frame of 6 (100 ms: the queue ran dry, the display waited
            // 3 vblanks): it gets the 3 beyond kSlack, the 3 catching up one each.
            CHECK(r.Give(100.0, 6, kRefreshMs) == 3, "6 fired, 3 beyond kSlack: 3");
            CHECK(r.Give(3.0, 0, kRefreshMs) == 1 && r.Give(3.0, 0, kRefreshMs) == 1 && r.Give(3.0, 0, kRefreshMs) == 1 && r.ahead == 0.0,
                "the frames catching up: 1 each, back level (%.2f)", r.ahead);
            // More frames than vblanks (the queue filling): one each up to
            // kSlack ahead, then their measured time.
            CHECK(r.Give(4.0, 0, kRefreshMs) == 1 && r.Give(4.0, 0, kRefreshMs) == 1 && r.Give(4.0, 0, kRefreshMs) == 1, "kSlack frames ahead: 1 each");
            CHECK(r.Give(4.0, 0, kRefreshMs) == 0 && r.ahead == game::WholeVblanks::kSlack, "then measured time, never more than kSlack ahead");
            CHECK(r.Give(16.6, 1, kRefreshMs) == 1 && r.ahead == game::WholeVblanks::kSlack, "kSlack ahead, a frame of 1: 1 (it fits)");
            CHECK(r.Give(33.3, 2, kRefreshMs) == 1 && r.ahead == game::WholeVblanks::kSlack - 1.0, "a frame of 2: 1, giving one back");
            CHECK(r.Give(120.0, 7, kRefreshMs) == 0 && r.ahead == 0.0, "7 vblanks (loading): measured time, and a fresh start");
            for (int i = 0; i < 20; i++)
                r.Give(30.0, 0, kRefreshMs);
            CHECK(r.ahead == game::WholeVblanks::kSlack, "ahead at most kSlack (%.2f)", r.ahead);
            r.Reset();
            CHECK(r.ahead == 0.0 && r.Give(16.6, 1, 0.0) == 0, "no vblank period: measured time");
        }

        struct Case
        {
            const char* name;
            double meanMs, jitterMs, vblankMs, lateMs;
            bool paced;
            double oldMin, oldMax;  // the half-vblank rule's game time over the vblanks', expected range
        };
        const Case cases[] = {
            { "60 fps held, starts 0-3 ms late", kRefreshMs, 3.0, kRefreshMs, 0.5, true, 0.9999, 1.0001 },
            { "30 fps held, Frame Rate 30", 2 * kRefreshMs, 3.0, 2 * kRefreshMs, 0.5, true, 0.9999, 1.0001 },
            { "60 fps held, vblanks up to 6 ms late", kRefreshMs, 1.0, kRefreshMs, 6.0, true, 0.98, 1.02 },
            { "GPU-bound 44 ms +-2, Frame Rate 60", 44.0, 2.0, kRefreshMs, 0.5, false, 1.03, 1.2 },
            { "GPU-bound 49 ms +-2, Frame Rate 60", 49.0, 2.0, kRefreshMs, 0.5, false, 1.005, 1.1 },
            { "GPU-bound 44 ms +-2, Frame Rate 30", 44.0, 2.0, 2 * kRefreshMs, 0.5, false, 0.7, 0.95 },
            { "GPU-bound 49 ms +-2, Frame Rate 30", 49.0, 2.0, 2 * kRefreshMs, 0.5, false, 0.7, 0.95 },
            { "GPU-bound 18.7 ms +-1, vblanks up to 8 ms late", 18.7, 1.0, kRefreshMs, 8.0, false, 0.8, 0.97 },
            { "GPU-bound 26 ms +-7, Frame Rate 60", 26.0, 7.0, kRefreshMs, 0.5, false, 0.9, 1.1 },
        };
        for (const Case& c : cases)
        {
            double worst = 0, oldMean = 0;
            uint64_t paced = 0, pacedOne = 0, frames = 0, given = 0;
            int maxSteps = 0;
            const int runs = 12;
            for (int p = 0; p < runs; p++)
            {
                Totals t = Play(Frames(c.meanMs, c.jitterMs, c.vblankMs, c.lateMs, c.paced, 4000, 1234u + uint32_t(p)), c.vblankMs);
                worst = std::max(worst, std::abs(t.newMs - t.vblankMs) / c.vblankMs);
                oldMean += t.oldMs / t.vblankMs / runs;
                paced += t.paced;
                pacedOne += t.pacedGivenOne;
                frames += t.frames;
                given += t.given;
                maxSteps = std::max(maxSteps, t.maxSteps);
            }
            printf("  %-48s new rule within %.2f vblanks of the vblanks' time; half-vblank rule x%.4f; %llu of %llu frames given whole vblanks\n",
                c.name, worst, oldMean, (unsigned long long)given, (unsigned long long)frames);
            // Game time against the vblanks that fired, over 4000 frames:
            // within kSlack (and a last frame that saw none), whatever the
            // frames did.
            CHECK(worst <= game::WholeVblanks::kSlack + 1.0, "%s: the new rule within kSlack + 1 vblanks of the vblanks (%.2f)", c.name, worst);
            CHECK(oldMean >= c.oldMin && oldMean <= c.oldMax, "%s: the half-vblank rule's known bias (x%.4f)", c.name, oldMean);
            if (c.paced)
                CHECK(pacedOne == paced && given == frames, "%s: every paced frame given one vblank (%llu of %llu)", c.name,
                    (unsigned long long)pacedOne, (unsigned long long)paced);
            CHECK(maxSteps <= 6, "%s: at most 6 vblanks a frame", c.name);
        }
    }

    // GPU-bound frames below 15 fps: `meanMs` +- `jitterMs`, and with
    // probability `spikeP` one `spikeMs` to 2 x `spikeMs` longer (a texture
    // upload, streaming). Vblanks every kRefreshMs, each 0-0.5 ms late; a
    // frame's count is those after its start up to the next one's.
    std::vector<Frame> SlowFrames(double meanMs, double jitterMs, double spikeP, double spikeMs, int count, uint32_t seed)
    {
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> unit(0.0, 1.0);
        std::vector<double> starts;
        double t = unit(rng) * kRefreshMs;
        for (int i = 0; i <= count; i++)
        {
            starts.push_back(t);
            double ms = meanMs + (2 * unit(rng) - 1) * jitterMs;
            if (unit(rng) < spikeP)
                ms += spikeMs * (1.0 + unit(rng));
            t += std::max(ms, 1.0);
        }
        std::vector<double> fired;
        for (double due = 0.0; due <= t + kRefreshMs; due += kRefreshMs)
            fired.push_back(due + unit(rng) * 0.5);
        std::vector<Frame> out;
        size_t v = 0;
        for (int i = 0; i < count; i++)
        {
            uint64_t n = 0;
            while (v < fired.size() && fired[v] <= starts[size_t(i)])
                v++;
            while (v < fired.size() && fired[v] <= starts[size_t(i) + 1])
                v++, n++;
            out.push_back({ starts[size_t(i) + 1] - starts[size_t(i)], n });
        }
        return out;
    }

    // Below 15 fps (an Intel UHD laptop GPU at 1.5x, ~12 fps,
    // a frame of 7 or more vblanks every second or so): game time follows the
    // vblanks that fired, frames over 6 included; loads start over.
    void LowFps()
    {
        using game::WholeVblanks;
        const double owedFloor = -WholeVblanks::kSlack - WholeVblanks::kMaxOwed;
        // Frame by frame.
        {
            WholeVblanks r;
            // GPU-bound at 12 fps: kSlack behind after the first, then each
            // its count. The frames before judge the rate: from the fourth
            // one it is below 20 fps.
            CHECK(r.Give(83.0, 5, kRefreshMs) == 2 && r.ahead == -WholeVblanks::kSlack, "12 fps, the first frame: 2 (%.2f)", r.ahead);
            for (int i = 0; i < 3; i++)
                CHECK(r.Give(83.0, 5, kRefreshMs) == 5 && r.ahead == -WholeVblanks::kSlack, "then its count: 5");
            CHECK(r.frameMs >= WholeVblanks::kLowRateMs, "below 20 fps after 4 frames of 83 ms (%.1f ms)", r.frameMs);
            // A frame of 7: 6 (the stepper's cap), 1 owed; the next of 5 gets 6.
            CHECK(r.Give(117.0, 7, kRefreshMs) == 6 && r.ahead == -WholeVblanks::kSlack - 1.0, "7 fired: 6, one owed (%.2f)", r.ahead);
            CHECK(r.Give(83.0, 5, kRefreshMs) == 6 && r.ahead == -WholeVblanks::kSlack, "5 fired, one owed: 6, level again (%.2f)", r.ahead);
            // A frame of 12: 6, and 6 owed, given back one a frame by frames of 5.
            CHECK(r.Give(200.0, 12, kRefreshMs) == 6 && r.ahead == -WholeVblanks::kSlack - 6.0, "12 fired: 6, 6 owed (%.2f)", r.ahead);
            int extra = 0;
            for (int i = 0; i < 6; i++)
                extra += int(r.Give(83.0, 5, kRefreshMs)) - 5;
            CHECK(extra == 6 && r.ahead == -WholeVblanks::kSlack, "frames of 5 give the 6 back, one each (%d, %.2f)", extra, r.ahead);
            // The short frame after a long one (the loop catching up) gives
            // back at most its own count again: a frame of 1 gets 2.
            r.Give(200.0, 12, kRefreshMs);
            CHECK(r.Give(16.6, 1, kRefreshMs) == 2 && r.ahead == -WholeVblanks::kSlack - 5.0, "a frame of 1, 6 owed: 2 (%.2f)", r.ahead);
            for (int i = 0; i < 5; i++)
                r.Give(83.0, 5, kRefreshMs);
            CHECK(r.ahead == -WholeVblanks::kSlack, "the rest by the frames of 5 (%.2f)", r.ahead);
            // At 8 fps (frames of 7-8: more than 6 on average) more is owed
            // than comes back, up to kMaxOwed beyond kSlack.
            for (int i = 0; i < 40; i++)
                CHECK(r.Give(125.0, 7 + uint64_t(i % 2), kRefreshMs) == 6, "8 fps: 6 a frame");
            CHECK(r.ahead == owedFloor, "owed kMaxOwed beyond kSlack, no more (%.2f)", r.ahead);
            // Back at 60 fps: 2 a frame while the frames before still average
            // 50 ms or more, then the rest is forgotten (no catch-up at 60 fps).
            std::string got;
            int back = 0;
            for (int i = 0; i < 10; i++)
            {
                uint64_t k = r.Give(16.6, 1, kRefreshMs);
                back += int(k) - 1;
                got += std::to_string(k) + " ";
            }
            CHECK(back >= 2 && back <= 8 && r.ahead == -WholeVblanks::kSlack, "60 fps again: 2 a frame for a few, then 1 (%s; %.2f)",
                got.c_str(), r.ahead);
            // At 60 fps a long frame is a hitch, as in v0.1.3: its measured
            // time and a fresh start, nothing owed, so no frame after it runs
            // more than one step (GitHub issue #9's frames).
            for (int i = 0; i < 20; i++)
                r.Give(16.6, 1, kRefreshMs);
            CHECK(r.Give(300.0, 18, kRefreshMs) == 0 && r.ahead == 0.0, "a 300 ms hitch at 60 fps: measured time, a fresh start (%.2f)",
                r.ahead);
            CHECK(r.Give(4.0, 0, kRefreshMs) == 1 && r.Give(16.6, 1, kRefreshMs) == 1 && r.Give(16.6, 1, kRefreshMs) == 1,
                "after it: one a frame, the queue catching up");
            // Half a second is still a frame; more is a load: measured time,
            // nothing owed after it, the rate judged afresh.
            r.Reset();
            for (int i = 0; i < 4; i++)
                r.Give(83.0, 5, kRefreshMs);
            r.Give(200.0, 12, kRefreshMs);
            CHECK(r.ahead < -WholeVblanks::kSlack, "owing again (%.2f)", r.ahead);
            CHECK(r.Give(500.0, WholeVblanks::kLoadVblanks, kRefreshMs) == 6, "30 vblanks: not a load, 6");
            CHECK(r.Give(2000.0, 120, kRefreshMs) == 0 && r.ahead == 0.0 && r.frameMs == WholeVblanks::kHighRateMs,
                "a load (2 s): measured time, nothing owed (%.2f)", r.ahead);
            CHECK(r.Give(83.0, 5, kRefreshMs) == 2 && r.ahead == -WholeVblanks::kSlack, "after it: 2, as from a fresh start (%.2f)", r.ahead);
            r.Give(200.0, 12, kRefreshMs);
            CHECK(r.Give(510.0, WholeVblanks::kLoadVblanks + 1, kRefreshMs) == 0 && r.ahead == 0.0, "31 vblanks: a load");
        }

        // Long runs: game time over the vblanks that fired (frames over 6
        // included), and world steps (6 a frame at most).
        struct Case
        {
            const char* name;
            double meanMs, jitterMs, spikeP, spikeMs;
            // Game time within kSlack + kMaxOwed (+1) of the vblanks all
            // run long: the frames have room to give back what longer ones
            // owe before kMaxOwed is reached. (With frames of 11-18 vblanks
            // now and then, two close together can reach it: a little is
            // forgotten.)
            bool keepsUp;
            double minRatio;  // game time over the vblanks, at least
        };
        const Case cases[] = {
            { "Intel UHD at 1.5x: 12 fps (80 ms +-35), a 140-235 ms frame every ~1.5 s", 80.0, 35.0, 1.0 / 20, 60.0, true, 0.999 },
            { "15 fps (67 ms +-20), a 190-310 ms frame every ~2 s", 67.0, 20.0, 1.0 / 30, 120.0, false, 0.999 },
            { "10.5 fps (95 ms +-15)", 95.0, 15.0, 0.0, 0.0, true, 0.999 },
            { "11 fps (83 ms +-25), a 170-250 ms frame every ~1 s", 83.0, 25.0, 1.0 / 12, 85.0, false, 0.995 },
            { "8 fps (125 ms +-25): more than 6 vblanks a frame", 125.0, 25.0, 0.0, 0.0, false, 0.0 },
        };
        for (const Case& c : cases)
        {
            double fired = 0, given = 0, v013 = 0, steps = 0, v013Steps = 0, worst = 0, lowest = 0;
            uint64_t frames = 0, over6 = 0;
            int maxGive = 0;
            for (int p = 0; p < 20; p++)
            {
                std::vector<Frame> run = SlowFrames(c.meanMs, c.jitterMs, c.spikeP, c.spikeMs, 3000, 4321u + uint32_t(p));
                WholeVblanks rule;
                V013Rule prev;
                double runFired = 0, runGiven = 0;
                for (const Frame& f : run)
                {
                    uint64_t k = rule.Give(f.ms, f.vblanks, kRefreshMs), o = prev.Give(f.ms, f.vblanks, kRefreshMs);
                    runFired += double(f.vblanks);
                    runGiven += k ? double(k) : f.ms / kRefreshMs;
                    v013 += o ? double(o) : f.ms / kRefreshMs;
                    steps += WorldSteps(k, f.ms, kRefreshMs);
                    v013Steps += WorldSteps(o, f.ms, kRefreshMs);
                    over6 += f.vblanks > 6;
                    maxGive = std::max(maxGive, int(k));
                    lowest = std::min(lowest, rule.ahead);
                    if (c.keepsUp)
                        worst = std::max(worst, std::abs(runGiven - runFired));
                }
                fired += runFired;
                given += runGiven;
                frames += run.size();
            }
            printf("  %-56s %.1f%% of frames over 6; game time over vblanks fired %.4f (v0.1.3 %.4f), world steps %.4f (v0.1.3 %.4f)",
                c.name, 100.0 * double(over6) / double(frames), given / fired, v013 / fired, steps / fired, v013Steps / fired);
            if (c.keepsUp)
                printf("; within %.2f vblanks\n", worst);
            else
                printf("; owed %.2f at most\n", -lowest);
            CHECK(maxGive <= 6, "%s: at most 6 a frame (%d)", c.name, maxGive);
            CHECK(lowest >= owedFloor, "%s: never owed more than kSlack + kMaxOwed (%.2f)", c.name, lowest);
            if (c.keepsUp)
                CHECK(worst <= WholeVblanks::kSlack + WholeVblanks::kMaxOwed + 1.0,
                    "%s: game time within kSlack + kMaxOwed + 1 of the vblanks over 20 x 3000 frames (%.2f)", c.name, worst);
            CHECK(given / fired >= c.minRatio && steps / fired >= c.minRatio, "%s: game time and world steps follow the vblanks (x%.4f, x%.4f)",
                c.name, given / fired, steps / fired);
            if (c.minRatio > 0.0)
                CHECK(v013 / fired < 0.985 && v013Steps / fired < 0.985, "%s: v0.1.3's rule ran slow here (x%.4f, world x%.4f)", c.name,
                    v013 / fired, v013Steps / fired);
        }
    }

    // BURNOUT_GAME_TIME_LOG rows: measured_ms,vblanks,... (the frame's own; the
    // rule used then doesn't change them).
    void Replay(const std::string& arg)
    {
        std::string path = arg;
        double vblankMs = kRefreshMs;
        if (arg.size() > 3 && arg.substr(arg.size() - 3) == "@30")
        {
            path = arg.substr(0, arg.size() - 3);
            vblankMs = 2 * kRefreshMs;
        }
        std::ifstream in(path);
        if (!in)
        {
            printf("  %s: can't read\n", path.c_str());
            g_failures++;
            return;
        }
        std::string line;
        std::getline(in, line);
        std::vector<Frame> frames;
        while (std::getline(in, line))
        {
            double ms;
            unsigned long long k;
            if (sscanf(line.c_str(), "%lf,%llu", &ms, &k) == 2 && k <= game::WholeVblanks::kLoadVblanks)
                frames.push_back({ ms, k });
        }
        if (frames.empty())
        {
            printf("  %s: no frames of 0-%llu vblanks\n", path.c_str(), (unsigned long long)game::WholeVblanks::kLoadVblanks);
            return;
        }
        Totals t = Play(frames, vblankMs);
        printf("  %s: %llu frames of 0-%llu vblanks, %.1f s measured; game time over measured: new rule %.4f, v0.1.3 rule %.4f, "
            "half-vblank rule %.4f, vblanks %.4f; world steps over vblanks: new %.4f, v0.1.3 %.4f; frames given otherwise than by v0.1.3's "
            "rule %llu\n", path.c_str(), (unsigned long long)t.frames, (unsigned long long)game::WholeVblanks::kLoadVblanks,
            t.measuredMs / 1000, t.newMs / t.measuredMs, t.v013Ms / t.measuredMs, t.oldMs / t.measuredMs, t.vblankMs / t.measuredMs,
            t.newSteps * vblankMs / t.vblankMs, t.v013Steps * vblankMs / t.vblankMs, (unsigned long long)t.notAsV013);
    }
}

int main(int argc, char** argv)
{
    printf("synthetic frames:\n");
    Synthetic();
    printf("a main loop queued ahead of the display (GitHub issue #9):\n");
    QueuedLoops();
    printf("below 15 fps: frames over 6 vblanks, and loads:\n");
    LowFps();
    if (argc > 1)
        printf("logs:\n");
    for (int i = 1; i < argc; i++)
        Replay(argv[i]);
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}

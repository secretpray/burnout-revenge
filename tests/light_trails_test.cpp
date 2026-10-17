// Tests for game::trails (runtime/game/light_trails.h): what the nitrous light
// trails' ring holds when RenderFlares draws them, with the hook and without,
// at 60 fps, 30 fps and frames between (GitHub issue #10). From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/light_trails_test.cpp -o build/light_trails_test
//   build/light_trails_test
// The game's side is simulated as sub_824E6220 does it: the index moves
// (-1 -> 0 first), the hook runs at the matrix copy, then the newest entry is
// written. The trail is drawn between the three entries, so its length is the
// distance from the newest to the oldest.
#include <game/light_trails.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using namespace game::trails;

    // A car on a straight line at `speed` units per ms, its matrix tagged
    // with the time it was written (so a test can tell which frame's it is).
    Entry CarAt(double ms, double speed)
    {
        Entry e;
        e.pos[0] = float(1000.0 + speed * ms);
        e.pos[1] = 5.0f;
        e.pos[2] = float(-200.0 + 0.5 * speed * ms);
        const uint64_t tag = uint64_t(std::llround(ms * 1000.0));
        memcpy(e.matrix.data(), &tag, sizeof(tag));
        return e;
    }

    uint64_t TagOf(const Entry& e)
    {
        uint64_t tag;
        memcpy(&tag, e.matrix.data(), sizeof(tag));
        return tag;
    }

    struct Ring
    {
        Entry e[3];
        int32_t index = -1;
        History history;
        bool hooked = true;

        // One RenderFlares call in a player view at game time `ms`.
        void Draw(double ms, const Entry& now)
        {
            if (index < 0)
                index = 0;
            if (++index > 2)
                index = 0;
            const uint32_t slot = uint32_t(index);
            if (hooked)
            {
                history.Add(ms, now);
                const Fill f = FillFor(history, ms);
                if (f.hasMiddle)
                    e[MiddleSlot(slot)] = f.middle;
                if (f.hasOldest)
                    e[OldestSlot(slot)] = f.oldest;
            }
            e[slot] = now;
        }
        const Entry& Newest() const { return e[index]; }
        const Entry& Middle() const { return e[MiddleSlot(uint32_t(index))]; }
        const Entry& Oldest() const { return e[OldestSlot(uint32_t(index))]; }
    };

    double Dist(const Entry& a, const Entry& b)
    {
        double s = 0;
        for (int k = 0; k < 3; k++)
            s += (double(a.pos[k]) - double(b.pos[k])) * (double(a.pos[k]) - double(b.pos[k]));
        return std::sqrt(s);
    }

    bool SameEntry(const Entry& a, const Entry& b)
    {
        return memcmp(a.pos, b.pos, sizeof(a.pos)) == 0 && a.matrix == b.matrix;
    }

    // Game time as GameFrameTime sums it: each frame's ms in 16.16, rounded.
    struct Clock
    {
        uint64_t units = 0;
        double ms = 0;
        void Advance(double frameMs)
        {
            units += uint64_t(std::llround(frameMs * 65536.0));
            ms = double(units) / 65536.0;
        }
    };

    constexpr double kSpeed = 0.06;  // units per ms: ~60 m/s if a unit is a metre
    constexpr double kV = kVblankMs;

    void SlotMath()
    {
        printf("slots\n");
        // Newest 1 (the first write, from -1): middle 0, oldest 2; and around.
        CHECK(MiddleSlot(1) == 0 && OldestSlot(1) == 2, "newest 1");
        CHECK(MiddleSlot(2) == 1 && OldestSlot(2) == 0, "newest 2");
        CHECK(MiddleSlot(0) == 2 && OldestSlot(0) == 1, "newest 0");
    }

    // 60 fps: the game alone spans 2 vblanks of driving; hooked, 4 (the 360's
    // two 30 fps frames), the middle exactly halfway, each entry the frame's
    // own (not interpolated).
    void SixtyFps()
    {
        printf("60 fps\n");
        Ring game, hooked;
        game.hooked = false;
        Clock c;
        for (int f = 0; f < 120; f++)
        {
            c.Advance(kV);
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
        }
        const double gameSpan = Dist(game.Newest(), game.Oldest()), span = Dist(hooked.Newest(), hooked.Oldest());
        const double want = Dist(CarAt(0, kSpeed), CarAt(4 * kV, kSpeed));
        CHECK(std::abs(gameSpan - want / 2) < 1e-3, "the game's own span %.4f, want half the 360's %.4f", gameSpan, want / 2);
        CHECK(std::abs(span - want) < 1e-3, "hooked span %.4f, want %.4f (4/60 s at the car's speed)", span, want);
        CHECK(std::abs(Dist(hooked.Newest(), hooked.Middle()) - want / 2) < 1e-3, "middle not halfway");
        // Entries are the samples the game wrote 2 and 4 frames ago, as written.
        const uint64_t now = TagOf(hooked.Newest());
        CHECK(now - TagOf(hooked.Middle()) == uint64_t(std::llround(2 * kV * 1000)) ||
                  now - TagOf(hooked.Middle()) == uint64_t(std::llround(2 * kV * 1000)) + 1,
            "middle is the frame 2 vblanks back (tags %llu %llu)", (unsigned long long)now, (unsigned long long)TagOf(hooked.Middle()));
        CHECK(now - TagOf(hooked.Oldest()) >= uint64_t(std::llround(4 * kV * 1000)) - 1 &&
                  now - TagOf(hooked.Oldest()) <= uint64_t(std::llround(4 * kV * 1000)) + 1,
            "oldest is the frame 4 vblanks back");
    }

    // 30 fps (the 360's own rate, and Frame Rate 30): the hook writes exactly
    // what the game had in those entries.
    void ThirtyFpsUnchanged()
    {
        printf("30 fps: no change\n");
        Ring game, hooked;
        game.hooked = false;
        Clock c;
        int differ = 0;
        for (int f = 0; f < 90; f++)
        {
            c.Advance(2 * kV);
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            for (int s = 0; s < 3; s++)
                differ += !SameEntry(game.e[s], hooked.e[s]);
        }
        CHECK(differ == 0, "%d ring entries differ from the game's at 30 fps", differ);
    }

    // Frames between: Auto switching 60 -> 30 -> 60, a GPU-bound 2-vblank
    // frame now and then, measured (unsnapped) frame times. The span stays
    // 4/60 s of driving (to interpolation), never the game's 2 frames.
    void MixedFrames()
    {
        printf("mixed frames\n");
        Ring hooked;
        Clock c;
        std::vector<double> frames;
        for (int i = 0; i < 60; i++)
            frames.push_back(kV);
        for (int i = 0; i < 30; i++)
            frames.push_back(2 * kV);
        for (int i = 0; i < 60; i++)
            frames.push_back(i % 7 == 3 ? 2 * kV : kV);
        for (int i = 0; i < 60; i++)
            frames.push_back(kV * (0.9 + 0.2 * double((i * 37) % 11) / 10.0));  // 15.0-18.3 ms, as measured
        double worst = 0;
        for (size_t i = 0; i < frames.size(); i++)
        {
            c.Advance(frames[i]);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            if (i < 8)
                continue;
            const double span = Dist(hooked.Newest(), hooked.Oldest()), want = Dist(CarAt(0, kSpeed), CarAt(4 * kV, kSpeed));
            worst = std::max(worst, std::abs(span - want) / want);
        }
        CHECK(worst < 1e-3, "span off by %.4f%% of 4/60 s", worst * 100);
    }

    // Paused (the world stands still, frames go on): the trail collapses onto
    // the car 4 vblanks after the stop, as a 360's does at its second paused
    // frame (its ring then holds the stop three times).
    void PausedCollapses()
    {
        printf("paused\n");
        Ring hooked;
        Clock c;
        for (int f = 0; f < 60; f++)
        {
            c.Advance(kV);
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
        }
        const double stopMs = c.ms;
        for (int f = 1; f <= 3; f++)
        {
            c.Advance(kV);
            hooked.Draw(c.ms, CarAt(stopMs, kSpeed));
            CHECK(Dist(hooked.Newest(), hooked.Oldest()) > 0, "collapsed %d vblanks after the stop, before 4", f);
        }
        c.Advance(kV);
        hooked.Draw(c.ms, CarAt(stopMs, kSpeed));
        CHECK(Dist(hooked.Newest(), hooked.Oldest()) == 0 && Dist(hooked.Newest(), hooked.Middle()) == 0, "not collapsed 4 vblanks after the stop");
    }

    // A car drawn for the first time, or again after a gap: where the history
    // doesn't reach, the game's entries stay (never one from before the gap).
    void ShortHistory()
    {
        printf("short history and gaps\n");
        Ring game, hooked;
        game.hooked = false;
        Clock c;
        for (int f = 0; f < 4; f++)
        {
            c.Advance(kV);
            game.Draw(c.ms, CarAt(c.ms, kSpeed));
            hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            const Fill fill = FillFor(hooked.history, c.ms);
            CHECK(fill.hasMiddle == (f >= 2) && fill.hasOldest == false, "frame %d: middle %d oldest %d", f, fill.hasMiddle, fill.hasOldest);
            for (int s = 0; s < 3; s++)
                CHECK(f >= 2 || SameEntry(game.e[s], hooked.e[s]), "frame %d entry %d changed with no history", f, s);
        }
        c.Advance(kV);
        hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
        CHECK(FillFor(hooked.history, c.ms).hasOldest, "4 vblanks of history: oldest filled");

        // Off screen for half a second, then back somewhere else.
        c.Advance(500.0);
        const double back = c.ms;
        hooked.Draw(c.ms, CarAt(c.ms + 1e5, kSpeed));
        CHECK(hooked.history.Size() == 1, "history kept across a %.0f ms gap (%zu samples)", 500.0, hooked.history.Size());
        const Fill fill = FillFor(hooked.history, back);
        CHECK(!fill.hasMiddle && !fill.hasOldest, "filled from before the gap");
    }

    // Two player-view calls in one frame (the same game time): one sample.
    void SameFrameTwice()
    {
        printf("same frame twice\n");
        History h;
        h.Add(100.0, CarAt(100.0, kSpeed));
        h.Add(100.0, CarAt(100.0, kSpeed));
        CHECK(h.Size() == 1, "%zu samples", h.Size());
        h.Add(90.0, CarAt(90.0, kSpeed));
        CHECK(h.Size() == 1 && h.NewestMs() == 90.0, "time going back starts over");
    }

    // Up to 360 fps (well past any display the vblank locks to) the history
    // still reaches 4/60 s back.
    void HighFrameRates()
    {
        printf("high frame rates\n");
        for (double fps : { 90.0, 120.0, 144.0, 240.0, 360.0 })
        {
            Ring hooked;
            Clock c;
            for (int f = 0; f < 200; f++)
            {
                c.Advance(1000.0 / fps);
                hooked.Draw(c.ms, CarAt(c.ms, kSpeed));
            }
            const double span = Dist(hooked.Newest(), hooked.Oldest()), want = Dist(CarAt(0, kSpeed), CarAt(4 * kV, kSpeed));
            CHECK(std::abs(span - want) / want < 1e-3, "%.0f fps: span %.4f, want %.4f", fps, span, want);
        }
    }
}

int main()
{
    SlotMath();
    SixtyFps();
    ThirtyFpsUnchanged();
    MixedFrames();
    PausedCollapses();
    ShortHistory();
    SameFrameTwice();
    HighFrameRates();
    if (g_failures)
    {
        printf("%d FAILED\n", g_failures);
        return 1;
    }
    printf("all passed\n");
    return 0;
}

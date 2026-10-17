// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The nitrous light trails at the length the Xbox 360 draws them, at any
// frame rate (game/light_trails.cpp hooks the game; tests/light_trails_test.cpp
// checks this header alone). GitHub issue #10.
//
// The trail is not an effect on the picture: VehicleRenderConn::RenderFlares
// (sub_824E6220, called per view) keeps, for every car it draws in a player
// view (the view's id at +4 is 1 or 2) and not in a reflection, the car's
// render matrix and position in a ring of three in its CarRenderInfo (the
// connection's +68): the matrices at +4512 (64 bytes each), the positions at
// +4704 (16 bytes each, x y z used), the newest one's index at +4752. It
// advances the index and writes the newest entry once a call, so once a
// frame. Then, when the car's nitrous is on (CarRenderInfo +4492) and it
// moves (faster than 0.2 in the decompilation), it draws the car's light
// flares 8 times along each of the three segments between the entries
// (oldest to middle, newest to oldest, middle to newest; sub_824DF658 with
// flags 8 and 2), each with the matrix of the segment's start. So the trail
// is where the car was over the last two frames, 24 flares.
//
// A real Xbox 360 runs the race at 30 fps (a 60 fps capture of one shows every
// race frame twice, now and then three or four times), so its trail covers
// 4/60 s of driving. At BurnoutRevenge's 60 fps it covered 2/60 s: half as long,
// with the same 24 flares packed into half the distance, so brighter.
//
// The fix keeps the game's code and its ring, and changes what is in the two
// entries the game doesn't write: at the ring write (the matrix copy
// sub_82134608 called from 0x824E63D0, after the index has moved and before
// any flare is drawn), the middle entry gets the car's position and matrix of
// 2/60 s of game time ago and the oldest those of 4/60 s ago, from a history
// of what the game wrote; the game then writes the newest as before. Game
// time is the time the main loop hands its frames (game::FrameClockMs: whole
// vblanks, so host timing jitter doesn't move it), and a paused game
// collapses the trail onto the car as a 360 does. At 30 fps those times are
// the frames the game wrote last and the one before (nothing changes); at 60
// fps the frames 2 and 4 before this one. When the history doesn't reach back
// that far (a car just drawn for the first time, or again after a gap), the
// game's own entry stays.
#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace game::trails
{
    // ctx.lr when RenderFlares calls the matrix copy that writes the newest
    // entry (bl 0x82134608 at 0x824E63D0; r3 = the entry, r4 = the matrix,
    // r30 = the CarRenderInfo). The position is stored right after it, from
    // the caller's r1 + 96.
    constexpr uint32_t kRingWriteReturn = 0x824E63D4;
    constexpr uint32_t kNewPositionOnStack = 96;
    // CarRenderInfo's ring.
    constexpr uint32_t kMatrices = 4512, kMatrixBytes = 64;
    constexpr uint32_t kPositions = 4704, kPositionBytes = 16;
    constexpr uint32_t kNewestIndex = 4752;

    // One vblank of game time, a world step; the 360's race frame is two.
    constexpr double kVblankMs = 1000.0 / 60.0;
    constexpr double kConsoleFrameMs = 2.0 * kVblankMs;
    // A sample this close to the wanted time is that time: game time is
    // summed from 16.16 ms values, each rounded by at most 1/131072 ms.
    constexpr double kSnapMs = 0.01;
    // A car not drawn for this long starts its history over (a new car in a
    // reused CarRenderInfo, or one back after a while off screen).
    constexpr double kGapMs = 250.0;

    // The ring entries around the newest (the index the game just moved to).
    constexpr uint32_t MiddleSlot(uint32_t newest) { return (newest + 2) % 3; }  // the console's last frame
    constexpr uint32_t OldestSlot(uint32_t newest) { return (newest + 1) % 3; }  // the one before

    // One ring entry, the matrix as the game's bytes (copied, never read).
    struct Entry
    {
        float pos[3] = {};
        std::array<uint8_t, kMatrixBytes> matrix{};
    };

    // What the game wrote for one car, at the game times it wrote it.
    class History
    {
    public:
        static constexpr size_t kCapacity = 32;  // reaches 4/60 s back at up to 465 fps

        bool Empty() const { return count_ == 0; }
        size_t Size() const { return count_; }
        double NewestMs() const { return count_ ? At(count_ - 1).ms : 0.0; }

        void Clear() { count_ = 0; }

        // The entry the game wrote at `ms`. A second write at the same time
        // (the same frame) replaces the first; time going back, or a gap of
        // kGapMs, starts over.
        void Add(double ms, const Entry& e)
        {
            if (count_)
            {
                const double newest = NewestMs();
                if (std::abs(ms - newest) <= 1e-6)
                {
                    Mutable(count_ - 1) = { ms, e };
                    return;
                }
                if (ms < newest || ms - newest > kGapMs)
                    count_ = 0;
            }
            if (count_ == kCapacity)
            {
                first_ = (first_ + 1) % kCapacity;
                count_--;
            }
            Mutable(count_++) = { ms, e };
        }

        // The car at game time `ms`: a sample within kSnapMs of it as it was
        // written, else the position between the two samples around it and
        // the nearer one's matrix. False when the history doesn't reach back
        // to `ms` (or `ms` is past the newest sample).
        bool Get(double ms, Entry& out) const
        {
            if (!count_ || ms < At(0).ms - kSnapMs || ms > NewestMs() + kSnapMs)
                return false;
            for (size_t i = count_; i-- > 0;)
            {
                const Sample& s = At(i);
                if (std::abs(s.ms - ms) <= kSnapMs)
                {
                    out = s.e;
                    return true;
                }
                if (s.ms < ms && i + 1 < count_)
                {
                    const Sample& n = At(i + 1);
                    const double f = (ms - s.ms) / (n.ms - s.ms);
                    for (int k = 0; k < 3; k++)
                        out.pos[k] = float(double(s.e.pos[k]) + (double(n.e.pos[k]) - double(s.e.pos[k])) * f);
                    out.matrix = f < 0.5 ? s.e.matrix : n.e.matrix;
                    return true;
                }
            }
            return false;
        }

    private:
        struct Sample
        {
            double ms = 0.0;
            Entry e;
        };
        const Sample& At(size_t i) const { return samples_[(first_ + i) % kCapacity]; }
        Sample& Mutable(size_t i) { return samples_[(first_ + i) % kCapacity]; }

        std::array<Sample, kCapacity> samples_{};
        size_t first_ = 0, count_ = 0;
    };

    // What to put in the two entries the game doesn't write, given the car's
    // history (with the newest entry already added at `nowMs`): the middle
    // one 2/60 s back, the oldest 4/60 s back. Each `has` is false where the
    // history doesn't reach (keep the game's entry).
    struct Fill
    {
        bool hasMiddle = false, hasOldest = false;
        Entry middle, oldest;
    };
    inline Fill FillFor(const History& h, double nowMs)
    {
        Fill f;
        f.hasMiddle = h.Get(nowMs - kConsoleFrameMs, f.middle);
        f.hasOldest = h.Get(nowMs - 2.0 * kConsoleFrameMs, f.oldest);
        return f;
    }
}

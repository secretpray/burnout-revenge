// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// How GameFrameTime (frame_time.cpp) gives game frames whole guest vblanks
// of time, apart so tests/whole_vblanks_test.cpp can check it.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace game
{
    // Game time follows the guest vblanks that fire: over any run of frames
    // of up to 6 vblanks it adds up to them, to within kSlack vblanks (below
    // 20 fps, frames of up to kLoadVblanks and kSlack + kMaxOwed), and each
    // frame that saw any gets whole ones (1 to 6), so the fixed 1/60 s world
    // step runs a whole number of times a frame.
    //   - A frame the vblank paces (60 fps held, or 30 at Frame Rate 30) saw
    //     one and gets one.
    //   - The main loop runs up to a few frames ahead of the display (its
    //     render thread and the GPU ring queue them), so its own frames
    //     don't line up with the vblanks: a frame the loop is slow with
    //     (streaming, a file read, a page fault) spans 2 vblanks and the next
    //     one, catching up, none, while the display still shows one frame a
    //     vblank from the queue. Each of those frames gets one: game time may
    //     run up to kSlack vblanks behind or ahead of the vblanks that fired.
    //     Until 2026-10-11 they got their own counts (2, then the measured
    //     few ms): the world ran 2 steps in a frame the display showed for
    //     one vblank and none in the next, a hitch every few seconds while
    //     driving that no frame-time graph shows (GitHub issue #9).
    //   - Behind by more than that, the queue ran dry and the display waited
    //     (a frame shown twice): the frame gets the vblanks beyond kSlack,
    //     at most 6, so game time keeps up with the display.
    //   - A frame it doesn't pace (GPU-bound) starts anywhere between two,
    //     so its count is its length rounded down or up: a 44 ms frame (2.64
    //     refreshes) sees 2 or 3, and once game time is kSlack behind (the
    //     first such frame), gets them.
    //   - Ahead by kSlack (more frames than vblanks for a while: the queue
    //     filling after a load), a frame no vblank fired in keeps its
    //     measured time.
    //   - A frame of more than 6 (a hitch, or a load's) keeps its measured
    //     time, and game time starts over (the game's stepper runs at most 6
    //     steps a frame and drops the rest).
    //   - Below 20 fps (the frames before averaging 50 ms or more) long
    //     frames are the frame rate's own: at 12 fps (5 vblanks a frame) one
    //     of 7-8 comes every second or so. Starting over after each, kSlack
    //     behind again, the frame after it got kSlack fewer than fired, and
    //     game time ran 4% slow (until 2026-10-12; an Intel laptop GPU at
    //     1.5x). There a frame of up to kLoadVblanks (half a second; longer:
    //     a load, as above) gets 6, the cap leaves the rest owed, up to
    //     kMaxOwed beyond kSlack, and later frames give it back, each at most
    //     doubling its own count (a frame of 5 gets 6, one of 1 gets 2).
    //     Frames over 6 on average (below 10 fps) owe more than comes back:
    //     the game runs slow there, as its stepper made it on the console.
    // The rule until 2026-10-07 gave a frame its count only when its
    // measured time was within half a vblank of it, else the measured time:
    // GPU-bound 44 ms frames got 3 vblanks or their own 2.64, so the game ran
    // fast (3-6% on an Intel laptop GPU), and at Frame Rate 30 (44-49 ms
    // frames, 1 or 2 guest vblanks of 33.3 ms) 9-15% slow; a late vblank
    // thread's 0 and 2 counts around 18 ms frames (the Mac at 3x3) ran it
    // 6-9% slow.
    struct WholeVblanks
    {
        // Game time given beyond the vblanks that fired, in vblanks (below 0:
        // behind), within +-kSlack: what can't be given back is forgotten (a
        // held vblank, as a GPU-only suspension's, isn't replayed). Below
        // 20 fps it may be down to -(kSlack + kMaxOwed).
        double ahead = 0.0;
        // The recent frames' length, ms on average (a frame counted as its
        // vblanks, up to kMaxGive of them): 16.7 at 60 fps, 83 at 12.
        double frameMs = kHighRateMs;
        // How far the main loop's frames may drift from the vblanks without
        // the display showing it: the frames queued between the loop and the
        // display (2-3, measured on the Steam Machine).
        static constexpr double kSlack = 3.0;
        // The most a frame gets: the game's stepper runs at most 6 world
        // steps a frame (sub_823A20C0) and drops the rest. (At Frame Rate 30
        // a guest vblank is two steps, so 3 of them would be the cap there;
        // a frame still gets up to 6, as before.)
        static constexpr int64_t kMaxGive = 6;
        // Frames this long on average and longer: below 20 fps, where a
        // frame of up to kLoadVblanks gets kMaxGive and what the cap leaves
        // stays owed, up to kMaxOwed beyond kSlack (half a second). Longer
        // frames are loads (half a second: nobody drives at 2 fps).
        static constexpr double kLowRateMs = 50.0;
        static constexpr double kMaxOwed = 30.0;
        static constexpr uint64_t kLoadVblanks = 30;
        static constexpr double kHighRateMs = 1000.0 / 60.0;

        // A frame the main loop measured at `measuredMs`, during which
        // `vblanks` guest vblanks fired (from its start to the next frame's),
        // each `vblankMs` long: the vblanks to give it, or 0 to keep its
        // measured time. More than 6 (a hitch, loading) keeps it too, and
        // starts over; below 20 fps only more than kLoadVblanks does.
        uint64_t Give(double measuredMs, uint64_t vblanks, double vblankMs)
        {
            if (vblanks > kLoadVblanks || !(vblankMs > 0.0))
            {
                Reset();
                return 0;
            }
            // Judged by the frames before this one: a long frame at 60 fps
            // is a hitch, wherever it takes the average.
            const bool low = frameMs >= kLowRateMs;
            frameMs += (double(std::min<uint64_t>(vblanks, kMaxGive)) * vblankMs - frameMs) / 4.0;
            if (!low && vblanks > uint64_t(kMaxGive))
            {
                ahead = 0.0;
                return 0;
            }
            if (!low)
                ahead = std::max(ahead, -kSlack);  // back above 20 fps: owed no more
            ahead -= double(vblanks);
            if (ahead + 1.0 > kSlack)
            {
                ahead = std::min(ahead + std::max(measuredMs, 0.0) / vblankMs, kSlack);
                return 0;
            }
            int64_t give = 1;
            if (ahead + 1.0 < -kSlack)
            {
                // Owed beyond its own count (only after a frame the cap held
                // back): at most as much again.
                give = std::min<int64_t>(std::llround(-kSlack - ahead), std::max<int64_t>(2 * int64_t(vblanks), 1));
                give = std::clamp<int64_t>(give, 1, kMaxGive);
            }
            ahead = std::max(ahead + double(give), -kSlack - (low ? kMaxOwed : 0.0));
            return uint64_t(give);
        }

        // Game time measured again (BURNOUT_GAME_TIME=0, a rate far from 60).
        void Reset()
        {
            ahead = 0.0;
            frameMs = kHighRateMs;
        }
    };
}

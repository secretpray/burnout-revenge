// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The render callback's cadence against the output device (apu/audio.cpp),
// header only so that tests/audio_cadence_test.cpp runs it.
//
// All of the game's sound runs on the render callback. XAudio processes its
// voices once per call, and NFSMW's own mixer thread (sub_825E3C30) mixes its
// next 256 samples only when XAudio's pass over its voice wakes it; that
// thread also carries out the game's sound commands (play, stop, unload:
// sub_825CF588). The console's hardware calls the callback every 5.33 ms,
// whatever the TV or speakers do. Here the cadence follows the output
// device's clock: a frame more or less when SDL's queue strays more than
// kSlack frames from its target. Up to v0.1.5 that was all, so a device that
// stopped taking audio (one that stalls, another app holding it) kept the
// queue over its target and the callback skipped for good: the game's sound
// stopped, its sound commands with it, and the game hung the next time it
// waited for a sound to stop (sub_821BD920 polls until the sound system no
// longer lists a stream it stopped, as when it unloads an event's sounds
// after the race): GitHub issue #20.
//
// Now a device that has taken nothing for StallTicks ticks (half a second,
// or four queue targets: far longer than any device takes between its
// pulls) is stalled: the callback runs at real time again, every frame is
// dropped (audio.cpp), and the stall is over the moment the device takes
// some: what was queued before it is stale and cleared (Resumed), and the
// queue fills up again as at the start. A device that does take audio, at
// whatever pace, is followed exactly as before.
#pragma once
#include <algorithm>
#include <cstdint>

namespace apu
{
    class AudioCadence
    {
    public:
        static constexpr int kSlack = 2;         // frames the queue may stray from its target
        static constexpr int kStallTicks = 94;   // ~0.5 s

        // Ticks without the device taking anything before it counts as
        // stalled, for a queue target of `target` frames.
        static constexpr int StallTicks(int target) { return std::max(kStallTicks, 4 * target); }

        // One tick (every 5.33 ms): how many frames to render now (0, 1 or
        // 2), with `queued` frames waiting for the device and a queue target
        // of `target` frames.
        int Frames(int queued, int target)
        {
            // What the device took since the last tick: the queue then, plus
            // what was queued since, less the queue now.
            m_idle = m_primed && m_expected - queued <= 0 ? m_idle + 1 : 0;
            m_primed = true;
            if (m_stalled)
            {
                if (m_idle > 0)
                {
                    m_expected = queued;  // the frame is dropped
                    return 1;
                }
                // It took some: over. The caller clears what is queued.
                m_stalled = false;
                m_resumed = true;
                queued = 0;
            }
            else if (m_idle >= StallTicks(target))
            {
                m_stalled = true;
                m_stalls++;
                m_expected = queued;
                return 1;
            }
            const int frames = queued > target + kSlack ? 0 : queued < target - kSlack ? 2 : 1;
            m_expected = queued + frames;
            return frames;
        }

        // The device isn't taking audio: every frame rendered is dropped,
        // not queued.
        bool Stalled() const { return m_stalled; }
        // True once after a stall ends: the audio still queued from before
        // it is stale (clear it).
        bool Resumed()
        {
            const bool r = m_resumed;
            m_resumed = false;
            return r;
        }
        uint64_t Stalls() const { return m_stalls; }

    private:
        bool m_primed = false, m_stalled = false, m_resumed = false;
        int m_expected = 0;   // the queue expected now if the device took nothing
        int m_idle = 0;       // ticks in a row the device took nothing
        uint64_t m_stalls = 0;
    };
}

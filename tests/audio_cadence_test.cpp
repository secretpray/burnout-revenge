// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// audio_cadence_test: the render callback's cadence against the output
// device (apu/audio_cadence.h), run against a simulated device that takes
// `pull` frames every `pull` ticks of its own clock (as a 1024-sample buffer
// takes 4 frames of 256), a little or a lot fast or slow, or that stops
// taking audio.
//   1. GitHub issue #20: a device that stops taking audio for good. The game's
//      sound (its render callback) carries on at real time once the device
//      has taken nothing for StallTicks; every frame is dropped from then on,
//      so the queue is held. The cadence as it was up to v0.1.5 rendered
//      nothing from the moment the queue filled up: that is checked too, as
//      the bug's model.
//   2. A device that takes audio, from 5% slow to 5% fast (SDL's dummy driver
//      runs ~3% slow), with pulls of 1 to 16 frames: never a stall, nothing
//      dropped, and frame for frame what the v0.1.5 cadence rendered, for 10
//      minutes.
//   3. A device that stalls for 2 s and takes audio again: the stall ends at
//      its first pull, the stale queue is cleared, and the queue is back at
//      its target within a second, nothing dropped after.
//   4. A route change to a larger buffer (Bluetooth headphones: target 8 ->
//      32 frames, pulls of 16): as before, frame for frame.
//   5. The stall threshold: StallTicks idle ticks, not one fewer.
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/audio_cadence_test.cpp -o /tmp/audio_cadence_test
#include <apu/audio_cadence.h>

#include <cstdio>
#include <cstdlib>
#include <string>

namespace
{
    int s_failed = 0;

    void Check(bool ok, const std::string& what)
    {
        if (!ok)
        {
            s_failed++;
            fprintf(stderr, "FAIL: %s\n", what.c_str());
        }
    }

    // The cadence up to v0.1.5 (apu/audio.cpp): one frame more or less when
    // the queue strays more than 2 frames from its target, nothing else.
    int OldFrames(int queued, int target)
    {
        return queued > target + 2 ? 0 : queued < target - 2 ? 2 : 1;
    }

    struct Device
    {
        int pull = 4;          // frames taken at once
        double rate = 1.0;     // its clock against ours
        bool taking = true;
        double due = 0;        // ticks of its clock until its next pull

        // One tick: what it takes from a queue of `queued` frames.
        int Take(int queued)
        {
            if (!taking)
                return 0;
            int took = 0;
            for (due -= rate; due <= 0; due += pull)
                took += std::min(queued - took, pull);
            return took;
        }
    };

    struct Sim
    {
        apu::AudioCadence cadence;
        bool oldRule = false;
        int target = 8;
        int queued = 0;
        int lastFrames = 0;
        long long rendered = 0, dropped = 0, taken = 0;
        int maxQueued = 0;

        // One tick (5.33 ms): the device takes what it takes, then the
        // render thread renders.
        void Tick(Device& d)
        {
            const int took = d.Take(queued);
            queued -= took;
            taken += took;
            const int frames = oldRule ? OldFrames(queued, target) : cadence.Frames(queued, target);
            if (!oldRule && cadence.Resumed())
                queued = 0;  // audio.cpp clears the stale audio
            for (int i = 0; i < frames; i++)
            {
                rendered++;
                if (!oldRule && cadence.Stalled())
                    dropped++;
                else
                    queued++;
            }
            lastFrames = frames;
            maxQueued = std::max(maxQueued, queued);
        }
    };

    constexpr long long kTicksPerSecond = 48000 / 256;  // 187.5 -> 187

    void DeviceStopsForGood(bool oldRule)
    {
        Sim s;
        s.oldRule = oldRule;
        Device d;
        for (int i = 0; i < 1000; i++)  // ~5 s of normal play
            s.Tick(d);
        d.taking = false;
        const long long before = s.rendered;
        const long long ticks = 60 * kTicksPerSecond;  // a minute of a device that takes nothing
        for (long long i = 0; i < ticks; i++)
            s.Tick(d);
        const long long rendered = s.rendered - before;
        if (oldRule)
        {
            // The bug's model: once the queue is over its target, no render
            // at all, so the game's mixer never runs again (#20).
            Check(rendered <= 8, "old rule: a stalled device stops the render callback (rendered " + std::to_string(rendered) + ")");
            return;
        }
        // Before the stall shows, the queue sits over its target and frames
        // are skipped (as before); from then on, one a tick.
        const long long allowed = apu::AudioCadence::StallTicks(s.target) + 1;
        Check(rendered >= ticks - allowed, "a stalled device: the game's sound carries on at real time (rendered " +
            std::to_string(rendered) + " of " + std::to_string(ticks) + " ticks)");
        Check(s.cadence.Stalled() && s.cadence.Stalls() == 1, "a stalled device is one stall, still on");
        Check(s.maxQueued <= s.target + apu::AudioCadence::kSlack + 2,
            "a stalled device: the queue is held (max " + std::to_string(s.maxQueued) + ")");
        Check(s.dropped >= rendered - (s.target + apu::AudioCadence::kSlack + 2),
            "a stalled device: what it can't take is dropped (dropped " + std::to_string(s.dropped) + ")");
    }

    // A device that takes audio: the cadence renders frame for frame what
    // the v0.1.5 one did, and never sees a stall.
    void TakesAudio(double rate, int pull, int target, long long ticks)
    {
        Sim s, old;
        old.oldRule = true;
        s.target = old.target = target;
        Device d, od;
        d.rate = od.rate = rate;
        d.pull = od.pull = pull;
        long long differ = 0;
        for (long long i = 0; i < ticks; i++)
        {
            s.Tick(d);
            old.Tick(od);
            differ += s.lastFrames != old.lastFrames;
        }
        const std::string what = "a device at " + std::to_string(rate) + "x, pulls of " + std::to_string(pull) + ", target " +
            std::to_string(target) + ": ";
        Check(s.cadence.Stalls() == 0 && s.dropped == 0, what + "no stall, nothing dropped (" + std::to_string(s.cadence.Stalls()) +
            " stalls, " + std::to_string(s.dropped) + " dropped)");
        Check(differ == 0, what + "rendered as v0.1.5 did, frame for frame (" + std::to_string(differ) + " ticks differ)");
        Check(s.rendered == s.taken + s.queued, what + "rendered = taken + queued");
    }

    void StallsAndResumes()
    {
        Sim s;
        Device d;
        for (int i = 0; i < 1000; i++)
            s.Tick(d);
        d.taking = false;
        int stalledAt = -1;
        for (long long i = 0; i < 2 * kTicksPerSecond; i++)
        {
            s.Tick(d);
            if (stalledAt < 0 && s.cadence.Stalled())
                stalledAt = int(i);
        }
        Check(stalledAt == apu::AudioCadence::StallTicks(s.target) - 1, "the stall shows after StallTicks idle ticks (" +
            std::to_string(stalledAt + 1) + ")");
        Check(s.dropped > 0, "a 2 s stall drops frames");
        d.taking = true;
        long long droppedAtResume = -1;
        int over = -1, back = -1;
        for (long long i = 0; i < 60 * kTicksPerSecond; i++)
        {
            s.Tick(d);
            if (over < 0 && !s.cadence.Stalled())
            {
                over = int(i);
                droppedAtResume = s.dropped;
                s.maxQueued = 0;
            }
            if (over >= 0 && back < 0 && s.queued >= s.target - apu::AudioCadence::kSlack - d.pull &&
                s.queued <= s.target + apu::AudioCadence::kSlack)
                back = int(i);
        }
        Check(over >= 0 && over <= d.pull, "the stall ends at the device's first pull (" + std::to_string(over) + " ticks)");
        Check(!s.cadence.Stalled() && s.cadence.Stalls() == 1, "one stall, over");
        Check(back >= 0 && back <= kTicksPerSecond, "the queue is back at its target within a second (" + std::to_string(back) + " ticks)");
        Check(s.dropped == droppedAtResume, "nothing dropped once the device takes audio again");
        Check(s.maxQueued <= s.target + apu::AudioCadence::kSlack + 2, "no stale audio left after the stall (max queue " +
            std::to_string(s.maxQueued) + ")");
    }

    void RouteToLargerBuffer()
    {
        Sim s, old;
        old.oldRule = true;
        Device d, od;
        for (int i = 0; i < 1000; i++)
        {
            s.Tick(d);
            old.Tick(od);
        }
        // Bluetooth headphones: a 4096-sample buffer (16 frames), target 32.
        s.target = old.target = 32;
        d.pull = od.pull = 16;
        long long differ = 0;
        for (long long i = 0; i < 60 * kTicksPerSecond; i++)
        {
            s.Tick(d);
            old.Tick(od);
            differ += s.lastFrames != old.lastFrames;
        }
        Check(differ == 0 && s.cadence.Stalls() == 0 && s.dropped == 0,
            "a route change to a larger buffer: as before, frame for frame (" + std::to_string(differ) + " ticks differ)");
    }

    void StallThreshold()
    {
        using C = apu::AudioCadence;
        for (int target : { 4, 8, 32 })
        {
            // A queue over its target that stays put: nothing rendered, and
            // the device takes nothing.
            const int queued = target + C::kSlack + 1;
            C c;
            c.Frames(queued, target);
            for (int idle = 1; idle < C::StallTicks(target); idle++)
                c.Frames(queued, target);
            Check(!c.Stalled(), "target " + std::to_string(target) + ": one idle tick short of StallTicks isn't a stall");
            c.Frames(queued, target);
            Check(c.Stalled(), "target " + std::to_string(target) + ": StallTicks idle ticks are a stall");
            c.Frames(queued, target);
            Check(c.Stalled(), "a stalled queue that stays put is still a stall");
            c.Frames(queued - 1, target);
            Check(!c.Stalled() && c.Resumed() && !c.Resumed(), "a stall ends when the device takes a frame, reported once");
        }
        Check(C::StallTicks(8) == C::kStallTicks && C::StallTicks(64) == 256, "StallTicks: half a second, or four targets");
    }
}

int main()
{
    DeviceStopsForGood(true);
    DeviceStopsForGood(false);
    for (double rate : { 0.95, 0.966, 0.998, 1.0, 1.002, 1.05 })
        for (int pull : { 1, 2, 4, 16 })
            TakesAudio(rate, pull, pull == 16 ? 32 : 8, 600 * kTicksPerSecond);
    StallsAndResumes();
    RouteToLargerBuffer();
    StallThreshold();
    if (s_failed)
    {
        fprintf(stderr, "%d checks failed\n", s_failed);
        return 1;
    }
    printf("audio_cadence_test: all checks passed\n");
    return 0;
}

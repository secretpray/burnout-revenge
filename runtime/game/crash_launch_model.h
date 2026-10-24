// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <algorithm>
#include <array>
#include <cmath>

namespace game::crash_launch
{
    // Independently expressed launch rules; platform addresses stay in the probe.
    struct Parameters
    {
        std::array<float, 6> limits{.007f, .12f, .18f, .30f, .86f, .9965f};
        float multiplier{.75f}, minimumSpeed{.5f};
        bool Valid() const
        {
            float previous = 0;
            for (float limit : limits)
            {
                if (!std::isfinite(limit) || limit <= previous || limit > 1) return false;
                previous = limit;
            }
            return std::isfinite(multiplier) && multiplier >= 0 && multiplier <= 10 &&
                std::isfinite(minimumSpeed) && minimumSpeed > 0 && minimumSpeed <= 10;
        }
        unsigned Zone(float level) const
        {
            for (unsigned i = 0; i < limits.size(); ++i)
                if (level <= limits[i]) return i;
            return level <= 1 ? 6 : 7;
        }
    };

    enum class Phase { Ascending, Descending, Done };
    enum class Result { Pending, Slow, Fast, Power, Stall, Blown, Missed };
    struct Model
    {
        Parameters parameters;
        Phase phase{Phase::Ascending};
        Result result{Result::Pending};
        float level{}, upper{}, fraction{}, steering{};
        float engineAcceleration{1.f};
        float engineLevel{}, wheelLevel{};
        bool upperStopped{}, previousEligiblePress{};
        unsigned oscillations{};

        float Maximum(float previous) const
        {
            // Keep the retail single-precision operation boundaries; folding
            // this into a fused multiply-add changes some launch results.
            const float scaled = fraction * (previous - 10.f);
            return 10.f + scaled;
        }
        float GearScale(float previous) const { return Maximum(previous) / previous + .1f; }

        float EngineAngularVelocity(float maximumRPM) const
        {
            // The pending retail update overwrites its earlier descent curve
            // with this linear value before returning. Keep both roundings.
            const float maximum = maximumRPM * .10471975803375244f;
            return maximum * engineLevel;
        }
        float WheelAngularVelocity() const { return wheelLevel * 1000.f; }

        void Finish(bool secondPress)
        {
            phase = Phase::Done;
            const auto first = parameters.Zone(upper), second = parameters.Zone(level);
            if (first == 6) result = Result::Blown;
            else if (!secondPress) result = Result::Missed;
            else if (second == 0) result = Result::Stall;
            else if (first == 5) result = second == 2 ? Result::Power : Result::Fast;
            else result = Result::Slow;
            fraction = result == Result::Stall ? 0.f :
                result == Result::Power || result == Result::Fast ? 1.f :
                result == Result::Slow ? std::max(.52f, upper) : .52f;
            const auto& p = parameters.limits;
            steering = level < p[1] ? (p[1] - level) / std::max(p[1] - p[0], .1f) :
                level > p[2] ? -(level - p[2]) / std::max(p[3] - p[2], .1f) : 0.f;
            steering = result == Result::Power ? 0.f : std::clamp(steering, -.8f, .8f);
        }

        // The initial launch press starts ascent outside this method. Subsequent
        // samples use the eligible-zone press history of the original mechanic.
        void Step(float dt, bool pressed)
        {
            if (phase == Phase::Done || !std::isfinite(dt) || dt <= 0) return;
            const auto& p = parameters.limits;
            float q;
            if (upperStopped) q = level < p[3] ? 1.f - p[3] : (1.f - level) * parameters.multiplier;
            else q = level > p[4] - .1f ? p[4] - .1f : level * parameters.multiplier;
            const float delta = (q + parameters.minimumSpeed) * dt;
            // A turning-point frame keeps the acceleration of the phase that
            // ran, including an upper stop or the automatic bounce at zero.
            engineAcceleration = phase == Phase::Ascending ? 1.f : 0.f;
            if (phase == Phase::Ascending)
            {
                level += delta;
                engineLevel = level;
                if (level >= 1) { level = 1; phase = Phase::Descending; }
            }
            else
            {
                const float square = level * level;
                const float quadratic = square * .4f;
                const float linear = level * .6f;
                engineLevel = quadratic + linear;
                level -= delta;
                if (level <= 0)
                {
                    if (upperStopped) { Finish(false); return; }
                    level = 0;
                    phase = Phase::Ascending;
                    ++oscillations;
                }
            }
            const bool eligible = pressed && (upperStopped ? level <= p[3] : level > p[3]);
            if (oscillations >= 5) { Finish(false); return; }
            if (eligible && !previousEligiblePress)
            {
                if (upperStopped) { Finish(true); return; }
                upper = level;
                upperStopped = true;
                phase = Phase::Descending;
                if (parameters.Zone(upper) == 6) { Finish(true); return; }
            }
            previousEligiblePress = eligible;
            engineLevel = wheelLevel = level;
        }
    };
}

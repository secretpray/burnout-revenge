// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <game/crash_launch_model.h>
#include <bit>
#include <cassert>
#include <limits>

int main()
{
    using namespace game::crash_launch;
    Parameters p;
    assert(p.Valid());
    for (unsigned i = 0; i < p.limits.size(); ++i)
    {
        assert(p.Zone(p.limits[i]) == i);
        assert(p.Zone(std::nextafter(p.limits[i], 2.f)) == i + 1);
    }
    assert(p.Zone(1) == 6 && p.Zone(1.1f) == 7);
    auto invalid = p;
    invalid.limits[2] = invalid.limits[1]; assert(!invalid.Valid());
    invalid = p; invalid.minimumSpeed = 0; assert(!invalid.Valid());
    invalid = p; invalid.multiplier = std::numeric_limits<float>::quiet_NaN(); assert(!invalid.Valid());

    // The saturated branches deliberately omit the multiplier.
    Model m; m.level = .8f; m.Step(.02f, false);
    assert(std::abs(m.level - .8252f) < 1e-6f);
    m = {}; m.upperStopped = true; m.phase = Phase::Descending; m.level = .2f;
    m.Step(.02f, false); assert(std::abs(m.level - .176f) < 1e-6f);
    // A press below the eligible upper zone has no effect.
    m = {}; m.Step(1.f / 60, true); assert(!m.upperStopped);

    for (auto expected : {Result::Slow, Result::Fast, Result::Power, Result::Stall, Result::Blown, Result::Missed})
    {
        m = {};
        const float upper = expected == Result::Blown ? .98f : expected == Result::Slow ? .6f : .92f;
        for (unsigned tick = 0; tick < 2000 && m.phase != Phase::Done; ++tick)
        {
            const bool press = !m.upperStopped ? m.level >= upper :
                expected == Result::Power ? m.level <= .16f :
                expected == Result::Stall ? m.level <= .026f :
                expected != Result::Missed && m.level <= .28f;
            m.Step(1.f / 60, press);
        }
        assert(m.result == expected);
        assert(m.phase == Phase::Done);
        if (expected == Result::Power) assert(m.fraction == 1 && m.steering == 0);
        if (expected == Result::Slow) assert(m.fraction > .59f && m.fraction < .64f);
        if (expected == Result::Stall) assert(m.fraction == 0);
    }
    m = {};
    for (unsigned i = 0; i < 3000 && m.phase != Phase::Done; ++i) m.Step(1.f / 60, false);
    assert(m.oscillations == 5 && m.result == Result::Missed);
    assert(m.fraction == .52f && m.Maximum(140.f) == 77.6f);
    // Captured retail DoBoostStart results. In particular a missed lower press
    // must still finalize minimum launch speed, independently of the upper stop.
    m = {}; m.upper = .92f; m.level = -.001f; m.Finish(false);
    assert(m.result == Result::Missed && m.fraction == .52f && m.steering == .8f);
    m = {}; m.upper = .92f; m.level = 0; m.Finish(true);
    assert(m.result == Result::Stall && m.Maximum(140.f) == 10.f);
    m = {}; m.upper = 1; m.level = 1; m.Finish(true);
    assert(m.result == Result::Blown && m.Maximum(140.f) == 77.6f && m.steering == -.8f);
    // Retail bus parameters: a frame-accurate lower press can reach Stall,
    // while waiting just one more tick takes the missed-press slow fallback.
    for (unsigned lowerTick : {133u, 134u})
    {
        m = {};
        m.parameters.limits = {.007f, .08f, .22f, .30f, .8f, .9965f};
        for (unsigned tick = 1; tick <= 134 && m.phase != Phase::Done; ++tick)
            m.Step(1.f / 60, tick == 69 || tick == lowerTick);
        if (lowerTick == 133)
        {
            assert(m.result == Result::Stall && m.level > 0 && m.level <= .007f);
            assert(m.Maximum(140) == 10);
        }
        else assert(m.result == Result::Missed && m.fraction == .52f);
    }
    // A physical Cross/A held from the initial launch is not lost input.
    // Both retail PS2 and Xbox instruction traces stop it on tick 30 at
    // 0.3010755479, then finish without a second press on tick 46. The
    // eligible-zone history must not be replaced with a raw-button edge.
    m = {};
    m.parameters.limits = {.007f, .08f, .22f, .30f, .8f, .9965f};
    for (unsigned tick = 1; tick <= 46; ++tick)
    {
        m.Step(1.f / 60, true);
        if (tick < 30) assert(!m.upperStopped);
        if (tick == 30)
            assert(m.upperStopped && std::bit_cast<unsigned>(m.upper) == 1050289811u);
        if (tick < 46) assert(m.phase != Phase::Done);
    }
    assert(m.result == Result::Missed && m.fraction == .52f);
    // Release the initial tap, then make separate upper/lower presses:
    // retail traces stop on tick 69 and finish with Power on tick 127.
    m = {};
    m.parameters.limits = {.007f, .08f, .22f, .30f, .8f, .9965f};
    for (unsigned tick = 1; tick <= 127; ++tick)
    {
        m.Step(1.f / 60, tick < 5 || tick == 69 || tick == 127);
        if (tick < 69) assert(!m.upperStopped);
        if (tick < 127) assert(m.phase != Phase::Done);
    }
    assert(m.result == Result::Power && m.fraction == 1 && m.steering == 0);
    m = {}; m.Step(.2f, false);
    assert(m.level == .1f); // Original accepts simulation hitches above 100 ms.
    assert(m.engineAcceleration == 1.f);
    assert(std::abs(m.EngineAngularVelocity(8000) - 83.77581f) < .00002f);
    assert(m.WheelAngularVelocity() == 100.f);
    // The retail engine keeps the phase's throttle on each turning frame.
    // The earlier nonlinear descent write does not survive the pending step.
    m = {}; m.level = .999f; m.Step(1.f / 60, false);
    assert(m.phase == Phase::Descending && m.engineAcceleration == 1.f);
    m.Step(1.f / 60, false);
    assert(m.engineAcceleration == 0.f);
    assert(m.EngineAngularVelocity(8000) > 800.f);
    m = {}; m.phase = Phase::Descending; m.level = .001f;
    m.Step(1.f / 60, false);
    assert(m.phase == Phase::Ascending && m.level == 0 && m.engineAcceleration == 0.f);
    m.Step(1.f / 60, false); assert(m.engineAcceleration == 1.f);
    // A finishing descent returns before the common linear/wheel overwrite.
    m = {}; m.upperStopped = true; m.upper = .92f;
    m.phase = Phase::Descending; m.level = .02f; m.wheelLevel = .02f;
    m.Step(1.f / 60, true);
    assert(m.result == Result::Missed);
    assert(std::bit_cast<unsigned>(m.EngineAngularVelocity(8000)) == 0x4122FE85u);
    assert(m.WheelAngularVelocity() == 20.f);
    m.Finish(false);
    const auto frozen = m;
    m.Step(.02f, true); assert(m.level == frozen.level && m.result == frozen.result);
}

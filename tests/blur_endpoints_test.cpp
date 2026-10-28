// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
// Endpoint regression for the helper emitted by WithBlurLength. A minimal
// vector shim lets us execute that source without a GPU. This is not a GLSL
// compiler or a test of intermediate rounding on a particular graphics driver.
#include <gpu/burnout_blur.h>

#include <array>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

using uint = uint32_t;
struct vec2 { float x, y; };
vec2 operator+(vec2 a, vec2 b) { return {a.x + b.x, a.y + b.y}; }
vec2 operator-(vec2 a, vec2 b) { return {a.x - b.x, a.y - b.y}; }
vec2 operator*(vec2 a, double scale) { return {a.x * float(scale), a.y * float(scale)}; }
struct { uint flags; } pc;
#include "blur_tap_generated.h"

bool Same(float actual, float expected)
{
    return (std::isnan(actual) && std::isnan(expected)) ||
        std::bit_cast<uint32_t>(actual) == std::bit_cast<uint32_t>(expected);
}

void Expect(vec2 actual, vec2 expected)
{
    assert(Same(actual.x, expected.x) && Same(actual.y, expected.y));
}

int main()
{
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    struct Sample { vec2 pixel, tap; };
    const std::array samples = {
        Sample{{.5f, .5f}, {.125f, .25f}},
        Sample{{.5f, .5f}, {nan, .75f}},
        Sample{{.5f, .5f}, {inf, -inf}},
        Sample{{-3e38f, 3e38f}, {3e38f, -3e38f}},
        Sample{{.867f, .999999f}, {1e-8f, 1.f}},
        Sample{{.2f, .3f}, {.8f, .9f}},
        Sample{{-0.f, 0.f}, {0.f, -0.f}},
        Sample{{nan, inf}, {.125f, .25f}},
    };
    for (const auto& sample : samples)
    {
        pc.flags = gpu::burnout::BlurFlags(100);
        Expect(blurTap(sample.pixel, sample.tap), sample.tap);
        pc.flags = gpu::burnout::BlurFlags(0);
        Expect(blurTap(sample.pixel, sample.tap), sample.pixel);
    }
    // The ordinary half-length path still interpolates, and the other flags
    // (vertex format and bloom) do not change the blur setting.
    pc.flags = gpu::burnout::BlurFlags(50) | gpu::burnout::BloomFlags(70, true) | 0xFu;
    Expect(blurTap({.5f, .5f}, {.125f, .25f}), {.3125f, .375f});
}

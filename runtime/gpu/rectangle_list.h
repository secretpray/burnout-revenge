// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once

// Mirror the vertex opposite the longest edge (Xenia's geometry shader rule).
// Share the selection expression with GLSL so permutation fixtures exercise
// the same decision as the native shader.
#define BR_RECT_PIVOT(d12, d20, d01) (((d12) > (d20) && (d12) > (d01)) ? 0u : ((d20) > (d01) ? 1u : 2u))
#define BR_RECT_STRING_INNER(x) #x
#define BR_RECT_STRING(x) BR_RECT_STRING_INNER(x)
namespace gpu
{
    constexpr unsigned RectanglePivot(float d12, float d20, float d01)
    {
        return BR_RECT_PIVOT(d12, d20, d01);
    }
    inline constexpr const char kRectanglePivotGlsl[] =
        "uint rectanglePivot(float d12, float d20, float d01) { return "
        BR_RECT_STRING(BR_RECT_PIVOT(d12, d20, d01)) "; }\n";
}
#undef BR_RECT_PIVOT
#undef BR_RECT_STRING
#undef BR_RECT_STRING_INNER

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Draws in depth-only EDRAM mode (RB_MODECONTROL edram_mode kDepthOnly) run
// no pixel shader on Xenos: only the rasterised depth and stencil reach EDRAM
// (xenos/xenos.h, EdramMode::kDepthOnly; Xenia draws them without one). The
// pixel shader still bound then is whatever an earlier draw left, and running
// it gives the draw that shader's kills, alpha test and depth export.
//
// NFSMW's sun shadow pass draws its alpha-tested casters (trees, poles,
// signs) in colour+depth mode with a pixel shader that kills texels whose
// alpha is 0.2 or less. Its other casters are depth-only, drawn with that
// pixel shader still bound and vertex shaders that export no texture
// coordinates: the solid occluders (walls, concrete barriers, tunnel roofs
// and the hills over them) and, after the world's map is resolved to
// 03A8A000, the cars (the map with them is 0444E000). Run, the bound shader
// read texel (0,0)'s alpha for all of them and killed every pixel, so up to
// v0.1.2 no car or wall cast a sun shadow and tunnels let the sun through
// (GitHub issue #3).
#pragma once
#include "xenos/xenos.h"

namespace gpu
{
    // Whether a draw in this EDRAM mode runs the guest's bound pixel shader.
    // guestOverride (BURNOUT_DEPTH_ONLY_PS=1, for A/B only): always, as up to
    // v0.1.2.
    constexpr bool RunsGuestPixelShader(xe::gpu::xenos::EdramMode mode, bool guestOverride = false)
    {
        return mode != xe::gpu::xenos::EdramMode::kDepthOnly || guestOverride;
    }

    // What a depth-only draw runs instead: no inputs, outputs, kill or
    // resources, so it fits every pipeline layout and attachment set.
    inline constexpr char kEmptyPixelShaderGlsl[] = "#version 460\nvoid main()\n{\n}\n";
}

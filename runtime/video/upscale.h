// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Which pass draws the game's picture into the window (video/presenter.cpp),
// and Settings > Graphics > Sharpening as FSR 1's RCAS strength. Pure
// functions, for tests/upscale_test.cpp.
//
// Settings > Graphics > Upscaling: Bicubic (Catmull-Rom, sharpened and
// clamped to the 2x2 texels around each sample), Bilinear, or FSR 1 (AMD
// FidelityFX Super Resolution 1: EASU, an edge-adaptive Lanczos-like
// upscale, then RCAS, a contrast-adaptive sharpening pass; video/fsr1.h).
// FSR 1 is made for enlarging (AMD: 1x to 4x the pixels): it is used only
// when the picture on the screen is larger than the frame image, along one
// axis or both, and smaller along neither (1x Internal Resolution on a
// 1080p, 1440p or MacBook screen; the Steam Deck's 720 rows stretched to
// 800). A frame shown at its own size, or shrunk (2x on a 1080p screen),
// takes the bicubic pass exactly as before FSR 1 existed.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace video::upscale
{
    // Settings > Graphics > Upscaling's values (settings::ScalingMode).
    enum class Mode : int32_t { Bicubic = 0, Linear = 1, Fsr1 = 2 };

    enum class Pass : uint8_t { Linear, Bicubic, Fsr1 };

    inline const char* PassName(Pass pass)
    {
        return pass == Pass::Fsr1 ? "FSR 1" : pass == Pass::Bicubic ? "bicubic" : "bilinear";
    }

    // The picture enlarged: dstW x dstH (its place on the screen) is larger
    // than the srcW x srcH image along one axis and smaller along neither.
    inline bool Enlarged(uint32_t srcW, uint32_t srcH, int32_t dstW, int32_t dstH)
    {
        if (!srcW || !srcH || dstW <= 0 || dstH <= 0)
            return false;
        const bool notSmaller = uint32_t(dstW) >= srcW && uint32_t(dstH) >= srcH;
        return notSmaller && (uint32_t(dstW) > srcW || uint32_t(dstH) > srcH);
    }

    // The pass for this frame. An unknown mode is bicubic, the default.
    inline Pass Choose(int32_t mode, uint32_t srcW, uint32_t srcH, int32_t dstW, int32_t dstH)
    {
        switch (Mode(mode))
        {
        case Mode::Linear:
            return Pass::Linear;
        case Mode::Fsr1:
            return Enlarged(srcW, srcH, dstW, dstH) ? Pass::Fsr1 : Pass::Bicubic;
        default:
            return Pass::Bicubic;
        }
    }

    // Sharpening (0..1, default 0.3) for RCAS: the strength AMD's FsrRcasCon
    // takes as stops below its maximum (0: the most it sharpens, each stop
    // half as much), here as the linear factor it becomes (2^-stops), which
    // multiplies RCAS's sharpening lobe: Sharpening 1 is RCAS's maximum,
    // 0.5 half of it, and 0 turns RCAS off (EASU's picture as it is), as 0
    // turns bicubic's sharpening off.
    inline float RcasAmount(float sharpening)
    {
        if (!(sharpening > 0.0f))  // 0, negative or NaN
            return 0.0f;
        return std::min(sharpening, 1.0f);
    }

    // The same as FsrRcasCon's stops (infinity: off).
    inline float RcasStops(float sharpening)
    {
        const float amount = RcasAmount(sharpening);
        return amount > 0.0f ? -std::log2(amount) : INFINITY;
    }
}

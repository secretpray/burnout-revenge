// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Motion Blur's length (Settings > Graphics > Motion Blur, 0-100%). The
// game blurs in the frame's composite (gpu/burnout_bloom.h's
// ps_2072de4532ec246b): 8 fetches of the scene, t2d_0, along the pixel's
// velocity (read from t2d_1 at the pixel), each at the pixel's position plus
// a multiple of that velocity, averaged. Here each fetch's offset from the
// pixel is scaled by the setting: 100% the game's streaks, 50% half as long,
// 0% all 8 at the pixel (and the game's blur stopped at its source as well,
// game/render_patches.cpp, v0.1.9's Motion Blur off). The cut (100 minus the
// setting) is in the draw's pc.flags, so the setting applies live without a
// recompile.
#pragma once
#include <algorithm>
#include <cstdint>
#include <regex>
#include <string>

#include "burnout_bloom.h"

namespace gpu::burnout
{
    // pc.flags bits 25-31: how much shorter the blur is, 0 (the game's) to 100.
    inline constexpr uint32_t kBlurCutShift = 25;
    inline constexpr int kBlurTaps = 8;

    inline uint32_t BlurFlags(int32_t blurPercent)
    {
        return uint32_t(100 - std::clamp(blurPercent, 0, 100)) << kBlurCutShift;
    }

    // `original` with the composite's scene fetches brought towards the pixel
    // by the draw's cut; unchanged for any other shader, or if the fetches are
    // not the 8 expected.
    inline std::string WithBlurLength(uint64_t ps, const std::string& original)
    {
        if (!IsBloomComposite(ps))
            return original;
        const size_t entry = original.find("void main()");
        if (entry == std::string::npos)
            return original;
        // The pixel's position: what the velocity (t2d_1) is fetched at.
        static const std::regex pixel(R"(texture\(t2d_1, (vec2\(r\[\d+\]\.\w, r\[\d+\]\.\w\)), fetchLodBias\(1u\)\))");
        static const std::regex tap(R"(texture\(t2d_0, (vec2\(r\[\d+\]\.\w, r\[\d+\]\.\w\)), fetchLodBias\(0u\)\))");
        std::smatch at;
        if (!std::regex_search(original.begin() + entry, original.end(), at, pixel))
            return original;
        const std::string uv = at[1].str();
        std::string body = original.substr(entry);
        size_t taps = std::distance(std::sregex_iterator(body.begin(), body.end(), tap), std::sregex_iterator());
        if (taps != kBlurTaps)
            return original;
        body = std::regex_replace(body, tap, "texture(t2d_0, blurTap(" + uv + ", $1), fetchLodBias(0u))");
        return original.substr(0, entry) +
            "vec2 blurTap(vec2 pixel, vec2 at) { return pixel + (at - pixel) * (1.0 - float((pc.flags >> " +
            std::to_string(kBlurCutShift) + "u) & 127u) * 0.01); }\n\n" + body;
    }
}

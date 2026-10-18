// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Bloom off without a grey sky (Settings > Graphics > Bloom). The game adds
// its bloom in the frame's composite (Burnout USA ps_2072de4532ec246b): one
// bilinear fetch, t2d_4, of a 320x180 copy of the scene that a blur and a
// bright pass made (with last frame's bloom mixed in), over the motion-blurred
// scene (t2d_0, 8 fetches), then the colour grade (t3d_2). That bloom is much
// of the picture's brightness: the sky is drawn pale and the bloom brightens
// it. Emptying the bloom kernel (boma's Xenia patch, as Xerenge applies it)
// leaves the sky grey and the frame dull; a higher exposure in its place
// brightens dark areas too.
//
// Here the bloom stays the game's, and is put back where it came from: its
// fetch is scaled by how bright the scene is at the pixel against the scene
// around it, over about the bloom's own reach (rings of 10, 24 and 44 guest
// pixels). In an even area (the sky, a wall) that ratio is 1 and the
// bloom is exactly the game's; a dark pixel beside a bright one gets little of
// the bright one's glow (no halo), and a bright one keeps what the blur spread
// from it. The ratio is capped at 1.6: at 1 the sky round leaves came out
// darker (a rim), 1.4 to 2 looked alike (split-frame runs). The draw flag
// kBloomInPlace (pc.flags) chooses this per draw, so the setting applies live
// without a recompile.
#pragma once
#include <cstdint>
#include <cstdlib>
#include <regex>
#include <string>

namespace gpu::burnout
{
    inline constexpr uint32_t kBloomInPlace = 1u << 16;  // pc.flags (bits 0-3: the vertex formats)
    // BURNOUT_BLOOM_SPLIT=1 (tuning): only the left half of the frame in place,
    // the right half the game's, in one and the same frame.
    inline constexpr uint32_t kBloomSplit = 1u << 17;

    // The composites verified to sample the bloom as above: the scene in t2d_0,
    // the bloom in t2d_4.
    inline bool IsBloomComposite(uint64_t ps)
    {
        return ps == 0x2072de4532ec246bull;
    }

    inline constexpr const char* kBloomInPlaceGlsl = R"(
float bloomLuma(vec3 c) { return dot(c, vec3(0.299, 0.587, 0.114)); }

// The scene's brightness at uv against the scene around it (gpu/burnout_bloom.h).
float bloomInPlaceRatio(vec2 uv)
{
    const vec2 guestPixel = vec2(1.0 / 1280.0, 1.0 / 720.0);
    const float radii[3] = float[3](10.0 * BLOOM_REACH, 24.0 * BLOOM_REACH, 44.0 * BLOOM_REACH);
    const float weights[3] = float[3](0.8, 0.5, 0.25);
    float centre = bloomLuma(texture(t2d_0, uv).rgb);
    float sum = centre, total = 1.0;
    for (int ring = 0; ring < 3; ring++)
        for (int i = 0; i < 8; i++)
        {
            float angle = (float(i) + 0.5 * float(ring)) * 0.78539816;
            vec2 at = uv + vec2(cos(angle), sin(angle)) * radii[ring] * guestPixel;
            sum += weights[ring] * bloomLuma(texture(t2d_0, at).rgb);
            total += weights[ring];
        }
    return clamp(centre / max(sum / total, 1.0 / 1024.0), 0.0, BLOOM_CAP);
}

)";

    // `original` with the bloom fetch scaled when the draw sets kBloomInPlace;
    // unchanged for any other shader, or if the fetch is not where expected.
    inline std::string WithBloomInPlace(uint64_t ps, const std::string& original)
    {
        if (!IsBloomComposite(ps))
            return original;
        static const std::regex fetch(R"((vec4 tdata = texture\(t2d_4, (vec2\(r\[\d+\]\.\w, r\[\d+\]\.\w\)), fetchLodBias\(4u\)\);))");
        std::smatch m;
        const size_t entry = original.find("void main()");
        if (entry == std::string::npos || !std::regex_search(original, m, fetch) ||
            original.find("uniform sampler2D t2d_0;") == std::string::npos)
            return original;
        std::string glsl = original;
        const std::string scaled = m[1].str() + "\n        if ((pc.flags & " + std::to_string(kBloomInPlace) +
            "u) != 0u && ((pc.flags & " + std::to_string(kBloomSplit) + "u) == 0u || " + m[2].str() + ".x < 0.5)) "
            "tdata.xyz *= bloomInPlaceRatio(" + m[2].str() + ");";
        glsl.replace(size_t(m.position(0)), size_t(m.length(0)), scaled);
        // BURNOUT_BLOOM_CAP, BURNOUT_BLOOM_REACH: tuning only (the ratio's cap,
        // the rings' radii as a multiple).
        const char* cap = std::getenv("BURNOUT_BLOOM_CAP");
        const char* reach = std::getenv("BURNOUT_BLOOM_REACH");
        glsl.insert(entry, std::string("#define BLOOM_CAP ") + (cap ? cap : "1.6") + "\n#define BLOOM_REACH " +
            (reach ? reach : "1.0") + "\n" + kBloomInPlaceGlsl);
        return glsl;
    }
}

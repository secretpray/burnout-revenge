// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Settings > Graphics > Bloom and Motion Blur as percentages: the draw flags
// the composite gets (gpu/burnout_bloom.h, gpu/burnout_blur.h) and the
// composite's GLSL as they rewrite it, on the fetches the translator writes
// for ps_2072de4532ec246b (its velocity, 8 scene taps, the bloom).
#include <gpu/burnout_blur.h>

#include <cassert>
#include <string>

namespace
{
    constexpr uint64_t kComposite = 0x2072de4532ec246bull;

    std::string Fetch(const char* texture, const char* at, int slot)
    {
        return std::string("    {\n        vec4 tdata = texture(") + texture + ", vec2(" + at + "), fetchLodBias(" +
            std::to_string(slot) + "u));\n    }\n";
    }

    std::string Composite(int taps)
    {
        std::string glsl = "uniform sampler2D t2d_0;\nvoid main()\n{\n" + Fetch("t2d_1", "r[0].x, r[0].y", 1);
        const char* at[] = { "r[1].x, r[1].y", "r[1].z, r[1].w", "r[4].x, r[4].y", "r[4].z, r[4].w",
            "r[6].x, r[6].y", "r[6].z, r[6].w", "r[7].x, r[7].y", "r[7].z, r[7].w", "r[2].x, r[2].y" };
        for (int i = 0; i < taps; i++)
            glsl += Fetch("t2d_0", at[i], 0);
        return glsl + Fetch("t2d_4", "r[0].x, r[0].y", 4) + "}\n";
    }

    size_t Count(const std::string& s, const std::string& what)
    {
        size_t n = 0;
        for (size_t at = s.find(what); at != std::string::npos; at = s.find(what, at + 1))
            n++;
        return n;
    }
}

int main()
{
    using namespace gpu::burnout;
    // Flags: 100% is the game's (nothing set); the share in place and the
    // blur's cut each in their own bits, clear of the vertex formats (0-3).
    assert(BloomFlags(100, false) == 0 && BloomFlags(100, true) == 0);
    assert(BloomFlags(0, false) == 100u << kBloomShareShift);
    assert(BloomFlags(70, true) == ((30u << kBloomShareShift) | kBloomSplit));
    assert(BloomFlags(-5, false) == BloomFlags(0, false) && BloomFlags(250, false) == 0);
    assert(BlurFlags(100) == 0 && BlurFlags(0) == 100u << kBlurCutShift && BlurFlags(40) == 60u << kBlurCutShift);
    const uint32_t both = BloomFlags(0, true) | BlurFlags(0);
    assert((both & 0xFu) == 0 && ((both >> kBloomShareShift) & 127u) == 100 && ((both >> kBlurCutShift) & 127u) == 100);
    assert((both & kBloomSplit) != 0);

    // The blur: all 8 scene taps, and only they, brought towards the pixel
    // the velocity is fetched at.
    const std::string original = Composite(8);
    const std::string blur = WithBlurLength(kComposite, original);
    assert(Count(blur, "texture(t2d_0, blurTap(vec2(r[0].x, r[0].y), vec2(") == 8);
    assert(Count(blur, "texture(t2d_1, vec2(r[0].x, r[0].y)") == 1 && Count(blur, "texture(t2d_4, vec2(r[0].x, r[0].y)") == 1);
    assert(blur.find("vec2 blurTap(") < blur.find("void main()"));
    assert(blur.find("(pc.flags >> 25u) & 127u") != std::string::npos);
    // Not the composite, or not its 8 taps: unchanged.
    assert(WithBlurLength(0, original) == original);
    assert(WithBlurLength(kComposite, Composite(7)) == Composite(7));
    assert(WithBlurLength(kComposite, Composite(9)) == Composite(9));

    // Then the bloom, as the renderer applies them: scaled by a mix of 1 and
    // the ratio by the share in place, after the blur's helper.
    const std::string both_glsl = WithBloomInPlace(kComposite, blur);
    assert(both_glsl.find("tdata.xyz *= mix(1.0, bloomInPlaceRatio(vec2(r[0].x, r[0].y)), float((pc.flags >> 18u) & 127u) * 0.01);") != std::string::npos);
    assert(Count(both_glsl, "blurTap(vec2(r[0].x, r[0].y)") == 8);
    assert(both_glsl.find("float bloomInPlaceRatio(") < both_glsl.find("void main()"));
    assert(both_glsl.find("vec2 blurTap(") < both_glsl.find("void main()"));
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See smaa.h.
#include "smaa.h"

#include <cstring>

// SMAA.hlsl's text (thirdparty/smaa, MIT): k_smaa_hlsl (runtime/CMakeLists.txt).
#include <smaa_hlsl.inc>

// The lookup textures, as SMAA ships them.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include <thirdparty/smaa/AreaTex.h>
#include <thirdparty/smaa/SearchTex.h>
#pragma GCC diagnostic pop

static_assert(AREATEX_WIDTH == video::smaa::kAreaWidth && AREATEX_HEIGHT == video::smaa::kAreaHeight);
static_assert(SEARCHTEX_WIDTH == video::smaa::kSearchWidth && SEARCHTEX_HEIGHT == video::smaa::kSearchHeight);

namespace video::smaa
{
    namespace
    {
        // SMAA's hooks for a shading language of our own (SMAA_CUSTOM_SL):
        // GLSL 4.6 compute, where texture() has no derivatives to choose a
        // mip by, so every fetch is textureLod at 0 (the images have one
        // mip). Point fetches use the sampler bound for them (SearchTex's
        // is nearest).
        constexpr const char* kPrelude = R"(#version 460
layout(local_size_x = 8, local_size_y = 8) in;
layout(push_constant) uniform P { vec4 metrics; } p;
#define SMAA_RT_METRICS p.metrics
#define SMAA_CUSTOM_SL 1
#define SMAA_PRESET_HIGH 1
#define SMAATexture2D(tex) sampler2D tex
#define SMAATexturePass2D(tex) tex
#define SMAASampleLevelZero(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroPoint(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleLevelZeroOffset(tex, coord, offset) textureLodOffset(tex, coord, 0.0, offset)
#define SMAASample(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASamplePoint(tex, coord) textureLod(tex, coord, 0.0)
#define SMAASampleOffset(tex, coord, offset) textureLodOffset(tex, coord, 0.0, offset)
#define SMAA_FLATTEN
#define SMAA_BRANCH
#define lerp(a, b, t) mix(a, b, t)
#define saturate(a) clamp(a, 0.0, 1.0)
#define mad(a, b, c) fma(a, b, c)
#define SMAAGather(tex, coord) textureGather(tex, coord)
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define int2 ivec2
#define int3 ivec3
#define int4 ivec4
#define bool2 bvec2
#define bool3 bvec3
#define bool4 bvec4
)";

        // SMAA.hlsl, its edge detection's `discard` (a pixel with no edge)
        // turned into returning no edge: a compute pass stores every pixel.
        std::string Smaa()
        {
            std::string text(reinterpret_cast<const char*>(k_smaa_hlsl));
            for (size_t at; (at = text.find("discard;")) != std::string::npos;)
                text.replace(at, 8, "return float2(0.0, 0.0);");
            return text;
        }

        // Each pass: this pixel's texcoord at its centre, as SMAA's vertex
        // shaders take it.
        constexpr const char* kPixel = R"(
bool Pixel(out ivec2 pix, out vec2 uv)
{
    pix = ivec2(gl_GlobalInvocationID.xy);
    uv = (vec2(pix) + 0.5) * p.metrics.xy;
    return pix.x < int(p.metrics.z) && pix.y < int(p.metrics.w);
}
)";
    }

    std::string EdgesGlsl()
    {
        return std::string(kPrelude) + Smaa() + kPixel + R"(
layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 3, rgba8) uniform writeonly image2D edgesOut;
void main()
{
    ivec2 pix;
    vec2 uv;
    if (!Pixel(pix, uv))
        return;
    vec4 offset[3];
    SMAAEdgeDetectionVS(uv, offset);
    imageStore(edgesOut, pix, vec4(SMAALumaEdgeDetectionPS(uv, offset, colorTex), 0.0, 0.0));
}
)";
    }

    std::string WeightsGlsl()
    {
        return std::string(kPrelude) + Smaa() + kPixel + R"(
layout(set = 0, binding = 0) uniform sampler2D edgesTex;
layout(set = 0, binding = 1) uniform sampler2D areaTex;
layout(set = 0, binding = 2) uniform sampler2D searchTex;
layout(set = 0, binding = 3, rgba8) uniform writeonly image2D weightsOut;
void main()
{
    ivec2 pix;
    vec2 uv;
    if (!Pixel(pix, uv))
        return;
    vec2 pixcoord;
    vec4 offset[3];
    SMAABlendingWeightCalculationVS(uv, pixcoord, offset);
    imageStore(weightsOut, pix, SMAABlendingWeightCalculationPS(uv, pixcoord, offset, edgesTex, areaTex, searchTex, vec4(0.0)));
}
)";
    }

    std::string BlendGlsl()
    {
        return std::string(kPrelude) + Smaa() + kPixel + R"(
layout(set = 0, binding = 0) uniform sampler2D colorTex;
layout(set = 0, binding = 1) uniform sampler2D blendTex;
layout(set = 0, binding = 3, rgba8) uniform writeonly image2D colorOut;
void main()
{
    ivec2 pix;
    vec2 uv;
    if (!Pixel(pix, uv))
        return;
    vec4 offset;
    SMAANeighborhoodBlendingVS(uv, offset);
    imageStore(colorOut, pix, vec4(SMAANeighborhoodBlendingPS(uv, offset, colorTex, blendTex).rgb, 1.0));
}
)";
    }

    const uint8_t* AreaTexBytes() { return areaTexBytes; }
    const uint8_t* SearchTexBytes() { return searchTexBytes; }
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Settings > Graphics > Anti-Aliasing's SMAA: SMAA 1x (runtime/thirdparty/smaa,
// MIT, unmodified) at the game's resolution, as three compute passes in
// place of the FXAA-style one (video/presenter.cpp), all at High quality:
//   1. luma edge detection, into an edges image;
//   2. blending weight calculation, from the edges and SMAA's two lookup
//      textures (AreaTex, SearchTex), into a weights image;
//   3. neighborhood blending of the frame by those weights, into the image
//      the scaling reads (the FXAA pass's).
// SMAA's own functions do the work; this file only wraps them for compute
// (every fetch at mip 0, an edge-less pixel stored as zero, not discarded).
#pragma once
#include <cstdint>
#include <string>

namespace video::smaa
{
    // The passes' GLSL. Push constants: vec4 (1/width, 1/height, width,
    // height). Bindings: 0..2 combined image samplers, 3 the storage image
    // written (rgba8):
    //   edges:    0 the frame;
    //   weights:  0 the edges (linear), 1 AreaTex (linear), 2 SearchTex (nearest);
    //   blending: 0 the frame, 1 the weights (both linear).
    std::string EdgesGlsl();
    std::string WeightsGlsl();
    std::string BlendGlsl();

    // SMAA's lookup textures: AreaTex 160x560 RG8, SearchTex 64x16 R8, rows
    // tightly packed.
    constexpr uint32_t kAreaWidth = 160, kAreaHeight = 560;
    constexpr uint32_t kSearchWidth = 64, kSearchHeight = 16;
    const uint8_t* AreaTexBytes();   // kAreaWidth * kAreaHeight * 2
    const uint8_t* SearchTexBytes(); // kSearchWidth * kSearchHeight
}

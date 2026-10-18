// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's own button prompts ("Select (A)", "Exit Replay (A)", "Camera
// (LB)") drawn for the device in use, as our menus' hints are
// (hid::LastGlyphs), as Settings > Input > Button Prompts chooses.
//
// Where they live: one 128x256 k_8_8_8_8 tiled texture (8-in-32 endian), a
// sheet of the Xbox 360 pad's buttons the front end and the in-game HUD
// (Replay, pause) both sample: A B X Y discs, LB RB LT RT, Start, a stick
// (the game prints LS or RS over it) and the d-pad (the cells in
// pad_glyphs.cpp). The race HUD's "PRESS (A) TO BOOST!" has its own A disc,
// a 128x128 DXT3 texture. The front end and each race load their own copy
// of the sheet, byte for byte the same, at an address that changes; each
// texture is known by a hash of its bytes on the NTSC-U disc. (The idea of
// drawing over a known prompt texture comes from Takedawn's Burnout 3
// glyphs, MIT; no code or artwork of it is used.)
//
// How: before the renderer decides whether such a texture is current, Use()
// writes the picture for the wanted family over the guest copy (the
// original for Xbox), which marks its pages written, so the usual reload
// untiles the new picture. A change of device rewrites the ones already
// loaded the same way, from Refresh() once a frame. The pictures are drawn
// here when first needed (shapes, and the setup screens' Barlow for labels;
// keyboard caps name the keys bound in Settings > Controls); the sticks, the
// d-pad and anything not redrawn stay the game's own.
#pragma once
#include <cstdint>

namespace gpu::padglyphs
{
    // False only with BURNOUT_PAD_GLYPHS=off (or 0): the game's textures
    // are then never looked at. Otherwise the setting (Auto: the device in
    // use; Xbox, PlayStation, Keyboard: always that set), which
    // BURNOUT_PAD_GLYPHS=auto|xbox|playstation|keyboard overrides.
    bool Enabled();

    // Command processor thread, for each use of a 2D texture, before its
    // validity check: `guest` is its base level's guest memory (`mipBase`,
    // `mipSize`: its mips', for the log).
    void Use(uint8_t* guest, uint32_t base, uint32_t size, uint32_t width, uint32_t height, uint32_t format,
        bool tiled, uint32_t endian, uint32_t pitchBlocks, uint32_t mipBase, uint32_t mipSize);

    // Command processor thread, once a frame: the textures known redrawn
    // for a change of device (`memory`: guest physical memory).
    void Refresh(uint8_t* memory);
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace gpu
{
    struct VolumeDimensions
    {
        uint32_t width, height, depth;
    };
    // The size word uses different bit widths for true 3D and stacked 2D.
    constexpr VolumeDimensions DecodeVolumeSize(uint32_t size, bool stacked)
    {
        return stacked ? VolumeDimensions{(size & 0x1FFF) + 1, ((size >> 13) & 0x1FFF) + 1, (size >> 26) + 1}
            : VolumeDimensions{(size & 0x7FF) + 1, ((size >> 11) & 0x7FF) + 1, (size >> 22) + 1};
    }
    constexpr uint32_t VolumeSliceBase(uint32_t base, uint32_t slice, uint32_t stride, bool tiled, bool stacked)
    {
        // True tiled 3D interleaves Z into its address. Stacked textures
        // consist of independent, 4 KB-aligned 2D slices even when tiled.
        return base + (tiled && !stacked ? 0 : slice * stride);
    }
}

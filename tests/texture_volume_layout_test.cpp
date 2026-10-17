// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <gpu/texture_volume_layout.h>
#include <cassert>

int main()
{
    auto stacked = gpu::DecodeVolumeSize(15u | (15u << 13) | (15u << 26), true);
    assert(stacked.width == 16 && stacked.height == 16 && stacked.depth == 16);
    // Catch accidentally reading the 11/11/10-bit true-3D size fields.
    auto largeStack = gpu::DecodeVolumeSize(4095u | (63u << 13) | (31u << 26), true);
    assert(largeStack.width == 4096 && largeStack.height == 64 && largeStack.depth == 32);
    auto volume = gpu::DecodeVolumeSize(31u | (63u << 11) | (127u << 22), false);
    assert(volume.width == 32 && volume.height == 64 && volume.depth == 128);
    assert(gpu::VolumeSliceBase(0x1000, 3, 0x2000, true, true) == 0x7000);
    assert(gpu::VolumeSliceBase(0x1000, 3, 0x2000, false, true) == 0x7000);
    assert(gpu::VolumeSliceBase(0x1000, 3, 0x2000, false, false) == 0x7000);
    assert(gpu::VolumeSliceBase(0x1000, 3, 0x2000, true, false) == 0x1000);
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
#include <vulkan/vulkan.h>

namespace platform::metalfx
{
    // macOS Burnout only. All calls on the presenter thread. Reuse a slot
    // only after its Vulkan presentation fence; Shutdown follows device idle.
    struct Pass
    {
        VkImage input = VK_NULL_HANDLE, output = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkSemaphore inputReady = VK_NULL_HANDLE, outputReady = VK_NULL_HANDLE;
        uint64_t value = 0;
    };
    bool Initialize(VkPhysicalDevice physical, VkDevice device, bool enabled);
    bool Available();
    Pass Prepare(uint32_t slot, uint32_t width, uint32_t height, uint32_t outputWidth, uint32_t outputHeight);
    // Encodes work on an independent Metal queue. It waits for inputReady
    // and signals outputReady at pass.value, without blocking the CPU.
    bool Encode(uint32_t slot, const Pass& pass);
    struct TemporalInputs
    {
        VkImage scene, ui, depth, motion;
        uint32_t width, height;
    };
    // Read-only export/format/usage inspection, also safe on the renderer
    // command thread after Initialize. Does not encode or consume a frame.
    bool ValidateTemporalInputs(const TemporalInputs& inputs);
    void Shutdown();
}

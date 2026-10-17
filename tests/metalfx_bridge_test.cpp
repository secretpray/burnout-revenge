// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Real GPU integration, no game data: Vulkan -> MetalFX -> Vulkan readback.
// Constant colours catch channel swaps and accidental gamma conversion;
// alternating slots/resizing exercise timeline values and texture lifetime.
#include <platform/metalfx.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>
#include <cassert>

#define VK_OK(expr) do { VkResult r = (expr); if (r != VK_SUCCESS) { fprintf(stderr, "%s: %d\n", #expr, int(r)); return 1; } } while (0)

int main()
{
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.apiVersion = VK_API_VERSION_1_2;
    const char* instanceExts[]{"VK_KHR_portability_enumeration"};
    VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    ici.pApplicationInfo = &app; ici.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
    ici.enabledExtensionCount = 1; ici.ppEnabledExtensionNames = instanceExts;
    VkInstance instance;
    const VkResult created = vkCreateInstance(&ici, nullptr, &instance);
    if (created == VK_ERROR_INCOMPATIBLE_DRIVER) { fputs("no Vulkan driver: skipped\n", stderr); return 77; }
    VK_OK(created);
    uint32_t count = 0; VK_OK(vkEnumeratePhysicalDevices(instance, &count, nullptr));
    if (!count) return 77;
    std::vector<VkPhysicalDevice> devices(count); VK_OK(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
    VkPhysicalDevice physical = devices[0];
    VK_OK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, nullptr));
    std::vector<VkExtensionProperties> extensions(count); VK_OK(vkEnumerateDeviceExtensionProperties(physical, nullptr, &count, extensions.data()));
    auto has = [&](const char* name) { return std::any_of(extensions.begin(), extensions.end(), [&](auto& e) { return !strcmp(e.extensionName, name); }); };
    if (!has("VK_EXT_metal_objects")) return 77;
    std::vector<const char*> enabled{"VK_EXT_metal_objects"};
    if (has("VK_KHR_portability_subset")) enabled.push_back("VK_KHR_portability_subset");
    VkPhysicalDeviceTimelineSemaphoreFeatures timelineFeature{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2}; features.pNext = &timelineFeature;
    vkGetPhysicalDeviceFeatures2(physical, &features);
    if (!timelineFeature.timelineSemaphore) return 77;
    vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, nullptr);
    std::vector<VkQueueFamilyProperties> families(count); vkGetPhysicalDeviceQueueFamilyProperties(physical, &count, families.data());
    uint32_t family = 0; while (family < count && !(families[family].queueFlags & VK_QUEUE_GRAPHICS_BIT)) ++family;
    assert(family < count);
    float priority = 1;
    VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = family; qci.queueCount = 1; qci.pQueuePriorities = &priority;
    VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.pNext = &timelineFeature;
    dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.enabledExtensionCount = uint32_t(enabled.size()); dci.ppEnabledExtensionNames = enabled.data();
    VkDevice device; VK_OK(vkCreateDevice(physical, &dci, nullptr, &device));
    VkQueue queue; vkGetDeviceQueue(device, family, 0, &queue);
    namespace fx = platform::metalfx;
    assert(!fx::Initialize(physical, device, false));
    if (!fx::Initialize(physical, device, true)) return 77;
    assert(!fx::Prepare(0, 128, 72, 128, 72).view); // 1:1 bypass
    assert(!fx::Prepare(0, 128, 72, 64, 36).view); // downscale bypass
    VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.queueFamilyIndex = family; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool; VK_OK(vkCreateCommandPool(device, &pci, nullptr, &pool));
    VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; cai.commandPool = pool; cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; cai.commandBufferCount = 2;
    VkCommandBuffer cmds[2]; VK_OK(vkAllocateCommandBuffers(device, &cai, cmds));
    VkPhysicalDeviceMemoryProperties memory; vkGetPhysicalDeviceMemoryProperties(physical, &memory);
    for (uint32_t frame = 0; frame < 8; ++frame)
    {
        uint32_t w = frame < 4 ? 128 : 160, h = frame < 4 ? 72 : 90, ow = w * 2, oh = h * 2;
        auto pass = fx::Prepare(frame % 2, w, h, ow, oh); assert(pass.view);
        VkBufferCreateInfo bci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; bci.size = ow * oh * 4; bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VkBuffer buffer; VK_OK(vkCreateBuffer(device, &bci, nullptr, &buffer));
        VkMemoryRequirements req; vkGetBufferMemoryRequirements(device, buffer, &req);
        uint32_t type = 0; const VkMemoryPropertyFlags props = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        while (type < memory.memoryTypeCount && (!(req.memoryTypeBits & (1u << type)) || (memory.memoryTypes[type].propertyFlags & props) != props)) ++type;
        assert(type < memory.memoryTypeCount);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = type;
        VkDeviceMemory staging; VK_OK(vkAllocateMemory(device, &ai, nullptr, &staging)); VK_OK(vkBindBufferMemory(device, buffer, staging, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        for (auto cmd : cmds) VK_OK(vkResetCommandBuffer(cmd, 0));
        VK_OK(vkBeginCommandBuffer(cmds[0], &bi));
        auto barrier = [&](VkCommandBuffer cmd, VkImage image, VkImageLayout old, VkAccessFlags src, VkAccessFlags dst) {
            VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.oldLayout = old; b.newLayout = VK_IMAGE_LAYOUT_GENERAL;
            b.srcAccessMask = src; b.dstAccessMask = dst; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image; b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
        };
        barrier(cmds[0], pass.input, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_ACCESS_TRANSFER_WRITE_BIT);
        barrier(cmds[0], pass.output, VK_IMAGE_LAYOUT_UNDEFINED, 0, VK_ACCESS_MEMORY_WRITE_BIT);
        const std::array<std::array<float, 4>, 4> colours{{{.1f,.4f,.8f,1}, {.8f,.2f,.1f,1}, {.03f,.03f,.03f,1}, {.9f,.7f,.4f,1}}};
        VkClearColorValue colour{}; std::copy(colours[frame % 4].begin(), colours[frame % 4].end(), colour.float32);
        VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        vkCmdClearColorImage(cmds[0], pass.input, VK_IMAGE_LAYOUT_GENERAL, &colour, 1, &range);
        VK_OK(vkEndCommandBuffer(cmds[0]));
        assert(fx::Encode(frame % 2, pass));
        VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO}; timeline.signalSemaphoreValueCount = 1; timeline.pSignalSemaphoreValues = &pass.value;
        VkSubmitInfo pre{VK_STRUCTURE_TYPE_SUBMIT_INFO}; pre.pNext = &timeline; pre.commandBufferCount = 1; pre.pCommandBuffers = &cmds[0]; pre.signalSemaphoreCount = 1; pre.pSignalSemaphores = &pass.inputReady;
        VK_OK(vkQueueSubmit(queue, 1, &pre, VK_NULL_HANDLE));
        VK_OK(vkBeginCommandBuffer(cmds[1], &bi));
        barrier(cmds[1], pass.output, VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_MEMORY_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{}; copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; copy.imageExtent = {ow, oh, 1};
        vkCmdCopyImageToBuffer(cmds[1], pass.output, VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &copy);
        VK_OK(vkEndCommandBuffer(cmds[1]));
        timeline = {VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO}; timeline.waitSemaphoreValueCount = 1; timeline.pWaitSemaphoreValues = &pass.value;
        VkPipelineStageFlags stage = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        VkSubmitInfo post{VK_STRUCTURE_TYPE_SUBMIT_INFO}; post.pNext = &timeline; post.waitSemaphoreCount = 1; post.pWaitSemaphores = &pass.outputReady; post.pWaitDstStageMask = &stage; post.commandBufferCount = 1; post.pCommandBuffers = &cmds[1];
        VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; VkFence fence; VK_OK(vkCreateFence(device, &fci, nullptr, &fence));
        VK_OK(vkQueueSubmit(queue, 1, &post, fence));
        VK_OK(vkWaitForFences(device, 1, &fence, VK_TRUE, 10'000'000'000));
        void* mapped; VK_OK(vkMapMemory(device, staging, 0, VK_WHOLE_SIZE, 0, &mapped));
        auto pixels = static_cast<const unsigned char*>(mapped);
        int worst = 0;
        for (uint32_t pixel = 0; pixel < ow * oh; ++pixel)
            for (int channel = 0; channel < 3; ++channel)
                worst = std::max(worst, std::abs(int(pixels[pixel * 4 + channel]) - int(std::round(colour.float32[channel] * 255))));
        fprintf(stderr, "frame %u, %ux%u -> %ux%u: max channel difference %d/255\n", frame, w, h, ow, oh, worst);
        assert(worst <= 3);
        vkUnmapMemory(device, staging); vkDestroyFence(device, fence, nullptr); vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, staging, nullptr);
    }
    VK_OK(vkDeviceWaitIdle(device)); fx::Shutdown();
    vkDestroyCommandPool(device, pool, nullptr); vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
    puts("MetalFX GPU bridge: colour, slots, timeline reuse, resize and bypass passed.");
}

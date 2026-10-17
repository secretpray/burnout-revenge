// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#include <TargetConditionals.h>
#if TARGET_OS_OSX && defined(BR_GAME_BURNOUT)
#include <platform/metalfx.h>
#include <vulkan/vulkan_metal.h>
#include <atomic>
#include <cstdio>
#import <Metal/Metal.h>
#import <MetalFX/MetalFX.h>

namespace platform::metalfx
{
    namespace
    {
        VkDevice s_device = VK_NULL_HANDLE;
        VkPhysicalDeviceMemoryProperties s_memory{};
        PFN_vkExportMetalObjectsEXT s_export = nullptr;
        bool s_available = false;
        std::atomic<bool> s_failed{false};
        std::atomic<uint64_t> s_frames{0}, s_gpuNs{0};
        id<MTLCommandQueue> s_queue = nil;
        struct Image
        {
            VkImage image = VK_NULL_HANDLE;
            VkDeviceMemory memory = VK_NULL_HANDLE;
            id<MTLTexture> texture = nil; // borrowed from Vulkan
        };
        struct Slot
        {
            Image input, output;
            VkImageView view = VK_NULL_HANDLE;
            VkSemaphore ready[2]{};
            id<MTLSharedEvent> events[2]{}; // borrowed from Vulkan
            id<MTLFXSpatialScaler> scaler = nil;
            id<MTLCommandBuffer> last = nil;
            uint32_t w = 0, h = 0, ow = 0, oh = 0;
            uint64_t value = 0;
        } s_slots[2];

        void DestroyImage(Image& image)
        {
            if (image.image) vkDestroyImage(s_device, image.image, nullptr);
            if (image.memory) vkFreeMemory(s_device, image.memory, nullptr);
            image = {};
        }
        bool MakeImage(Image& image, uint32_t w, uint32_t h)
        {
            VkExportMetalObjectCreateInfoEXT exportInfo{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
            exportInfo.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_TEXTURE_BIT_EXT;
            VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
            ci.pNext = &exportInfo;
            ci.imageType = VK_IMAGE_TYPE_2D;
            ci.format = VK_FORMAT_R8G8B8A8_UNORM;
            ci.extent = {w, h, 1};
            ci.mipLevels = ci.arrayLayers = 1;
            ci.samples = VK_SAMPLE_COUNT_1_BIT;
            ci.tiling = VK_IMAGE_TILING_OPTIMAL;
            ci.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
            if (vkCreateImage(s_device, &ci, nullptr, &image.image) != VK_SUCCESS) return false;
            VkMemoryRequirements req{};
            vkGetImageMemoryRequirements(s_device, image.image, &req);
            uint32_t type = UINT32_MAX;
            for (uint32_t i = 0; i < s_memory.memoryTypeCount; ++i)
                if ((req.memoryTypeBits & (1u << i)) && (s_memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
                { type = i; break; }
            if (type == UINT32_MAX) return false;
            VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
            ai.allocationSize = req.size;
            ai.memoryTypeIndex = type;
            if (vkAllocateMemory(s_device, &ai, nullptr, &image.memory) != VK_SUCCESS ||
                vkBindImageMemory(s_device, image.image, image.memory, 0) != VK_SUCCESS) return false;
            VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
            texture.image = image.image;
            texture.plane = VK_IMAGE_ASPECT_COLOR_BIT;
            VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
            objects.pNext = &texture;
            s_export(s_device, &objects);
            image.texture = texture.mtlTexture;
            return image.texture && image.texture.storageMode == MTLStorageModePrivate;
        }
        void DestroyPictures(Slot& slot)
        {
            // The presentation fence covers Metal's output event, but also
            // retire its completion handler before replacing these objects.
            [slot.last waitUntilCompleted];
            [slot.last release]; slot.last = nil;
            [slot.scaler release]; slot.scaler = nil;
            if (slot.view) vkDestroyImageView(s_device, slot.view, nullptr);
            slot.view = VK_NULL_HANDLE;
            DestroyImage(slot.input); DestroyImage(slot.output);
            slot.w = slot.h = slot.ow = slot.oh = 0;
        }
        bool MakeEvents(Slot& slot)
        {
            for (int i = 0; i < 2; ++i)
            {
                if (slot.ready[i]) continue;
                VkExportMetalObjectCreateInfoEXT exportInfo{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECT_CREATE_INFO_EXT};
                exportInfo.exportObjectType = VK_EXPORT_METAL_OBJECT_TYPE_METAL_SHARED_EVENT_BIT_EXT;
                VkSemaphoreTypeCreateInfo timeline{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
                timeline.pNext = &exportInfo;
                timeline.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
                VkSemaphoreCreateInfo ci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO}; ci.pNext = &timeline;
                if (vkCreateSemaphore(s_device, &ci, nullptr, &slot.ready[i]) != VK_SUCCESS) return false;
                VkExportMetalSharedEventInfoEXT event{VK_STRUCTURE_TYPE_EXPORT_METAL_SHARED_EVENT_INFO_EXT};
                event.semaphore = slot.ready[i];
                VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT}; objects.pNext = &event;
                s_export(s_device, &objects);
                slot.events[i] = event.mtlSharedEvent;
                if (!slot.events[i]) return false;
            }
            return true;
        }
    }

    bool Initialize(VkPhysicalDevice physical, VkDevice device, bool enabled)
    {
        @autoreleasepool
        {
            s_device = device;
            if (!enabled) return false;
            s_export = (PFN_vkExportMetalObjectsEXT)vkGetDeviceProcAddr(device, "vkExportMetalObjectsEXT");
            if (!s_export) return false;
            vkGetPhysicalDeviceMemoryProperties(physical, &s_memory);
            Image probe;
            if (MakeImage(probe, 16, 16))
            {
                id<MTLDevice> metal = probe.texture.device;
                s_available = [MTLFXSpatialScalerDescriptor supportsDevice:metal];
                if (s_available) s_queue = [metal newCommandQueue];
                s_available = s_available && s_queue;
            }
            DestroyImage(probe);
            fprintf(stderr, "[metalfx] Spatial %s; GPU texture/event bridge, default Bicubic\n", s_available ? "available" : "unavailable");
            return s_available;
        }
    }
    bool Available() { return s_available && !s_failed.load(); }

    bool ValidateTemporalInputs(const TemporalInputs& inputs)
    {
        @autoreleasepool
        {
            if (!s_export || !s_device || !inputs.width || !inputs.height) return false;
            if (@available(macOS 26.0, *))
            {
                const VkImage images[]={inputs.scene,inputs.ui,inputs.depth,inputs.motion};
                const MTLPixelFormat formats[]={MTLPixelFormatRGBA16Float,MTLPixelFormatRGBA16Float,
                    MTLPixelFormatR32Float,MTLPixelFormatRG16Float};
                for (uint32_t i=0;i<4;++i)
                {
                    if (!images[i]) return false;
                    VkExportMetalTextureInfoEXT texture{VK_STRUCTURE_TYPE_EXPORT_METAL_TEXTURE_INFO_EXT};
                    texture.image=images[i]; texture.plane=VK_IMAGE_ASPECT_COLOR_BIT;
                    VkExportMetalObjectsInfoEXT objects{VK_STRUCTURE_TYPE_EXPORT_METAL_OBJECTS_INFO_EXT};
                    objects.pNext=&texture;
                    s_export(s_device,&objects);
                    id<MTLTexture> t=texture.mtlTexture;
                    if (!t || t.pixelFormat!=formats[i] || t.width!=inputs.width || t.height!=inputs.height ||
                        t.storageMode!=MTLStorageModePrivate || !(t.usage&MTLTextureUsageShaderRead) ||
                        ![MTLFXFrameInterpolatorDescriptor supportsDevice:t.device]) return false;
                }
                fprintf(stderr,"[history] Metal private textures exported: RGBA16F/RGBA16F/R32F/RG16F; FI device supported\n");
                return true;
            }
            return false;
        }
    }

    Pass Prepare(uint32_t index, uint32_t w, uint32_t h, uint32_t ow, uint32_t oh)
    {
        @autoreleasepool
        {
            if (!Available() || index >= 2 || ow < w || oh < h || (ow == w && oh == h)) return {};
            Slot& slot = s_slots[index];
            if (slot.w != w || slot.h != h || slot.ow != ow || slot.oh != oh)
            {
                DestroyPictures(slot);
                bool ok = MakeImage(slot.input, w, h) && MakeImage(slot.output, ow, oh) && MakeEvents(slot);
                if (ok)
                {
                    MTLFXSpatialScalerDescriptor* desc = [[MTLFXSpatialScalerDescriptor alloc] init];
                    desc.inputWidth = w; desc.inputHeight = h; desc.outputWidth = ow; desc.outputHeight = oh;
                    desc.colorTextureFormat = desc.outputTextureFormat = MTLPixelFormatRGBA8Unorm;
                    desc.colorProcessingMode = MTLFXSpatialScalerColorProcessingModePerceptual;
                    slot.scaler = [desc newSpatialScalerWithDevice:slot.input.texture.device];
                    [desc release];
                    ok = slot.scaler && (slot.input.texture.usage & slot.scaler.colorTextureUsage) == slot.scaler.colorTextureUsage &&
                        (slot.output.texture.usage & slot.scaler.outputTextureUsage) == slot.scaler.outputTextureUsage;
                }
                if (ok)
                {
                    VkImageViewCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
                    ci.image = slot.output.image; ci.viewType = VK_IMAGE_VIEW_TYPE_2D; ci.format = VK_FORMAT_R8G8B8A8_UNORM;
                    ci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    ok = vkCreateImageView(s_device, &ci, nullptr, &slot.view) == VK_SUCCESS;
                }
                if (!ok)
                {
                    fprintf(stderr, "[metalfx] couldn't prepare %ux%u -> %ux%u; falling back to Bicubic\n", w, h, ow, oh);
                    s_failed = true; DestroyPictures(slot); return {};
                }
                slot.scaler.colorTexture = slot.input.texture;
                slot.scaler.outputTexture = slot.output.texture;
                slot.scaler.inputContentWidth = w; slot.scaler.inputContentHeight = h;
                slot.w = w; slot.h = h; slot.ow = ow; slot.oh = oh;
                fprintf(stderr, "[metalfx] Spatial slot %u: %ux%u -> %ux%u, perceptual RGBA8\n", index, w, h, ow, oh);
            }
            return {slot.input.image, slot.output.image, slot.view, slot.ready[0], slot.ready[1], ++slot.value};
        }
    }

    bool Encode(uint32_t index, const Pass& pass)
    {
        @autoreleasepool
        {
            Slot& slot = s_slots[index];
            id<MTLCommandBuffer> buffer = [s_queue commandBuffer];
            if (!buffer) { s_failed = true; return false; }
            @try
            {
                buffer.label = @"Burnout Revenge MetalFX Spatial";
                [buffer encodeWaitForEvent:slot.events[0] value:pass.value];
                [slot.scaler encodeToCommandBuffer:buffer];
                [buffer encodeSignalEvent:slot.events[1] value:pass.value];
            }
            @catch (NSException* error)
            {
                fprintf(stderr, "[metalfx] encode failed: %s; falling back to Bicubic\n", error.reason.UTF8String);
                s_failed = true; return false;
            }
            id<MTLSharedEvent> finished = slot.events[1];
            uint64_t value = pass.value;
            [buffer addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                if (completed.status == MTLCommandBufferStatusError)
                {
                    fprintf(stderr, "[metalfx] command failed: %s; falling back to Bicubic\n", completed.error.description.UTF8String);
                    s_failed = true;
                    // Don't leave Vulkan waiting forever after a Metal failure.
                    if (finished.signaledValue < value) finished.signaledValue = value;
                }
                else
                {
                    s_frames.fetch_add(1);
                    s_gpuNs.fetch_add(uint64_t((completed.GPUEndTime - completed.GPUStartTime) * 1e9));
                }
            }];
            [slot.last release]; slot.last = [buffer retain];
            [buffer commit];
            return true;
        }
    }
    void Shutdown()
    {
        @autoreleasepool
        {
            for (Slot& slot : s_slots)
            {
                DestroyPictures(slot);
                for (auto semaphore : slot.ready) if (semaphore) vkDestroySemaphore(s_device, semaphore, nullptr);
                slot = {};
            }
            [s_queue release]; s_queue = nil;
            if (uint64_t frames = s_frames.load())
                fprintf(stderr, "[metalfx] %llu frames, mean Metal GPU interval %.3f ms (includes input dependency waits)\n",
                    (unsigned long long)frames, double(s_gpuNs.load()) / double(frames) / 1e6);
            s_available = false;
        }
    }
}
#endif

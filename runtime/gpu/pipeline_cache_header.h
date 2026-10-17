// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The pipeline cache file's header (GetCachePath()/pipelines.bin), checked
// against this device before the file goes to the driver. Vulkan-free:
// tests/pipeline_cache_header_test.cpp checks it alone; gpu/renderer.cpp
// hands it the file's bytes and the device's VkPhysicalDeviceProperties.
//
// vkGetPipelineCacheData's data starts with VkPipelineCacheHeaderVersionOne,
// 32 bytes, every field least significant byte first on any host:
//    0 headerSize (32)    4 headerVersion (1)    8 vendorID    12 deviceID
//   16 pipelineCacheUUID[16]
// The spec puts it there so an application can tell when its saved data no
// longer fits the device. A driver sets such data aside without a word
// (MoltenVK and Mesa compare the whole header), so the cache starts empty
// and every pipeline compiles again on first use: a brief stutter, once,
// and the next save writes this device's header. The header has no
// driverVersion field: drivers fold their version into pipelineCacheUUID
// (MoltenVK its revision, Mesa its build ID, NVIDIA its driver version), so
// a driver update shows up as a UUID change.
//
// MoltenVK's device ID on an Apple GPU encodes the OS version: OS major (8
// bits), OS minor (8), Mac GPU family (8), Apple GPU family (8)
// (MVKPhysicalDevice::getHighestGPUCapability; its UUID carries the same
// word). An M1 Pro reads 0x1a050207 on macOS 26.5 and 0x1b000207 on 27.0,
// so every macOS update discards the cache: on the 27.0.1 upgrade, one
// 103 ms frame at the first world entry. On SteamOS each Mesa update does
// the same through the UUID.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace gpu::pipeline_cache
{
    constexpr size_t kHeaderBytes = 32;
    constexpr uint32_t kHeaderVersionOne = 1;  // VK_PIPELINE_CACHE_HEADER_VERSION_ONE
    constexpr uint32_t kAppleVendor = 0x106B;  // MoltenVK on Apple GPUs (Intel and AMD Macs report PCI IDs)

    // Who made a cache: a file's header, or this device's properties
    // (vendorID, deviceID, pipelineCacheUUID).
    struct Maker
    {
        uint32_t vendorID = 0, deviceID = 0;
        std::array<uint8_t, 16> uuid{};
    };

    enum class Verdict : uint8_t
    {
        Empty,       // no data: nothing to load
        Usable,      // this GPU, driver and OS made it: the driver takes it
        OtherMaker,  // another GPU, driver or OS version made it: the driver would set it aside
        Unreadable,  // shorter than a header, or not a version-one header
    };

    struct HeaderCheck
    {
        Verdict verdict = Verdict::Empty;
        size_t bytes = 0;
        uint32_t headerSize = 0, headerVersion = 0;  // as the file says (0 when shorter than a header)
        Maker file;                                  // Usable and OtherMaker: the file's header
        bool vendorDiffers = false, deviceDiffers = false, uuidDiffers = false;
    };

    inline uint32_t ReadLE32(const uint8_t* p)
    {
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }

    inline HeaderCheck CheckHeader(const uint8_t* data, size_t bytes, const Maker& device)
    {
        HeaderCheck c;
        c.bytes = bytes;
        if (bytes == 0)
            return c;
        c.verdict = Verdict::Unreadable;
        if (bytes < kHeaderBytes)
            return c;
        c.headerSize = ReadLE32(data);
        c.headerVersion = ReadLE32(data + 4);
        // MoltenVK and Mesa take exactly this header (Mesa compares all 32
        // bytes, headerSize included): a longer or newer one is unreadable.
        if (c.headerSize != kHeaderBytes || c.headerVersion != kHeaderVersionOne)
            return c;
        c.file.vendorID = ReadLE32(data + 8);
        c.file.deviceID = ReadLE32(data + 12);
        std::memcpy(c.file.uuid.data(), data + 16, c.file.uuid.size());
        c.vendorDiffers = c.file.vendorID != device.vendorID;
        c.deviceDiffers = c.file.deviceID != device.deviceID;
        c.uuidDiffers = c.file.uuid != device.uuid;
        c.verdict = c.vendorDiffers || c.deviceDiffers || c.uuidDiffers ? Verdict::OtherMaker : Verdict::Usable;
        return c;
    }

    // The UUID as 32 hex digits in byte order, as `xxd -p` shows the file
    // (MoltenVK's: its revision, the device ID's word, then flags). Not
    // 8-4-4-4-12, so a log line isn't taken for a device's identifier.
    inline std::string UuidText(const std::array<uint8_t, 16>& uuid)
    {
        std::string out;
        char hex[3];
        for (uint8_t byte : uuid)
        {
            std::snprintf(hex, sizeof(hex), "%02x", byte);
            out += hex;
        }
        return out;
    }

    // What differs, file -> device: "OS 26.5 -> 27.0, device ID 1a050207 ->
    // 1b000207, cache UUID ... -> ...". The OS part only for MoltenVK on an
    // Apple GPU, whose device ID carries it.
    inline std::string DescribeChange(const HeaderCheck& c, const Maker& device)
    {
        std::string out;
        char buf[96];
        auto add = [&](const std::string& part) { out += (out.empty() ? "" : ", ") + part; };
        if (c.vendorDiffers)
        {
            std::snprintf(buf, sizeof(buf), "vendor %04x -> %04x", c.file.vendorID, device.vendorID);
            add(buf);
        }
        if (c.deviceDiffers)
        {
            if (!c.vendorDiffers && device.vendorID == kAppleVendor && (c.file.deviceID >> 16) != (device.deviceID >> 16))
            {
                std::snprintf(buf, sizeof(buf), "OS %u.%u -> %u.%u", c.file.deviceID >> 24, (c.file.deviceID >> 16) & 0xFF,
                    device.deviceID >> 24, (device.deviceID >> 16) & 0xFF);
                add(buf);
            }
            std::snprintf(buf, sizeof(buf), "device ID %08x -> %08x", c.file.deviceID, device.deviceID);
            add(buf);
        }
        if (c.uuidDiffers)
            add("cache UUID " + UuidText(c.file.uuid) + " -> " + UuidText(device.uuid));
        return out;
    }

    // The log's line for a file the driver would set aside, "" for Empty
    // and Usable.
    inline std::string LogLine(const HeaderCheck& c, const Maker& device)
    {
        constexpr const char* kRebuild = "rebuilding shaders, expect a brief stutter on first use";
        const size_t kb = c.bytes / 1024;
        char buf[160];
        switch (c.verdict)
        {
        case Verdict::OtherMaker:
            return "[renderer] pipeline cache from another GPU, driver or OS version: " + std::string(kRebuild) + " (" +
                DescribeChange(c, device) + "; " + std::to_string(kb) + " KB set aside)";
        case Verdict::Unreadable:
            if (c.bytes < kHeaderBytes)
                std::snprintf(buf, sizeof(buf), "%zu bytes, shorter than its header", c.bytes);
            else
                std::snprintf(buf, sizeof(buf), "header size %u, version %u; %zu KB set aside", c.headerSize, c.headerVersion, kb);
            return "[renderer] pipeline cache file not readable: " + std::string(kRebuild) + " (" + buf + ")";
        default:
            return {};
        }
    }
}

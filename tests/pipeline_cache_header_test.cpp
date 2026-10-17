// Tests for gpu::pipeline_cache (runtime/gpu/pipeline_cache_header.h): when
// a saved pipelines.bin was made by another GPU, driver or OS version (the
// driver would set it aside, and the first drive stutters while pipelines
// compile again), and the log line that says so. From the repo root:
//   clang++ -std=c++20 -O2 -UNDEBUG -Iruntime tests/pipeline_cache_header_test.cpp -o build/pipeline_cache_header_test
//   build/pipeline_cache_header_test
// The two real headers are the first 32 bytes of the pipelines.bin MoltenVK
// 1.4.2 wrote on an M1 Pro under macOS 26.5 and under macOS 27.0.1.
#include <gpu/pipeline_cache_header.h>

#include <cstdio>
#include <string>
#include <utility>
#include <vector>

using namespace gpu::pipeline_cache;

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

static const std::vector<uint8_t> kMac265 = {
    0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x6b, 0x10, 0x00, 0x00, 0x07, 0x02, 0x05, 0x1a,
    0x00, 0x00, 0x28, 0xa2, 0x1a, 0x05, 0x02, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
static const std::vector<uint8_t> kMac270 = {
    0x20, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x6b, 0x10, 0x00, 0x00, 0x07, 0x02, 0x00, 0x1b,
    0x00, 0x00, 0x28, 0xa2, 0x1b, 0x00, 0x02, 0x07, 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };

// The device as MoltenVK reports it on each (vkGetPhysicalDeviceProperties).
static Maker Mac(uint32_t deviceID)
{
    Maker m{ kAppleVendor, deviceID, {} };
    m.uuid = { 0x00, 0x00, 0x28, 0xa2, uint8_t(deviceID >> 24), uint8_t(deviceID >> 16), uint8_t(deviceID >> 8),
        uint8_t(deviceID), 0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x00 };
    return m;
}

// A version-one header written least significant byte first, plus `extra` bytes of cache.
static std::vector<uint8_t> Header(const Maker& m, uint32_t size = 32, uint32_t version = 1, size_t extra = 0)
{
    std::vector<uint8_t> out;
    for (uint32_t v : { size, version, m.vendorID, m.deviceID })
        for (int i = 0; i < 4; i++)
            out.push_back(uint8_t(v >> (8 * i)));
    out.insert(out.end(), m.uuid.begin(), m.uuid.end());
    out.resize(out.size() + extra, 0xAB);
    return out;
}

static HeaderCheck Check(const std::vector<uint8_t>& data, const Maker& device)
{
    return CheckHeader(data.data(), data.size(), device);
}

int main()
{
    const Maker mac265 = Mac(0x1a050207), mac270 = Mac(0x1b000207);

    // The real headers parse as the device that wrote them.
    HeaderCheck c = Check(kMac270, mac270);
    CHECK(c.verdict == Verdict::Usable, "27.0's own cache on 27.0");
    CHECK(c.file.vendorID == 0x106B && c.file.deviceID == 0x1b000207 && c.file.uuid == mac270.uuid, "fields read LSB first");
    CHECK(LogLine(c, mac270).empty(), "nothing to say about a usable cache");
    CHECK(Check(kMac265, mac265).verdict == Verdict::Usable, "26.5's own cache on 26.5");
    CHECK(Header(mac270) == kMac270 && Header(mac265) == kMac265, "the test's writer matches MoltenVK's bytes");

    // The macOS update: 26.5's cache on 27.0, as the 27.0.1 upgrade saw it.
    std::vector<uint8_t> upgraded = kMac265;
    upgraded.resize(2563 * 1024 + 100, 0x5A);
    c = Check(upgraded, mac270);
    CHECK(c.verdict == Verdict::OtherMaker, "26.5's cache on 27.0");
    CHECK(!c.vendorDiffers && c.deviceDiffers && c.uuidDiffers, "the device ID and UUID carry the OS");
    std::string line = LogLine(c, mac270);
    CHECK(line == "[renderer] pipeline cache from another GPU, driver or OS version: rebuilding shaders, expect a brief "
                  "stutter on first use (OS 26.5 -> 27.0, device ID 1a050207 -> 1b000207, cache UUID "
                  "000028a21a0502070000000100000000 -> 000028a21b0002070000000100000000; 2563 KB set aside)",
        "%s", line.c_str());
    // And back (a second Mac, or a downgrade).
    line = LogLine(Check(kMac270, mac265), mac265);
    CHECK(line.find("(OS 27.0 -> 26.5, device ID 1b000207 -> 1a050207, ") != std::string::npos, "%s", line.c_str());
    CHECK(line.find("; 0 KB set aside)") != std::string::npos, "a header-only file: %s", line.c_str());

    // A new MoltenVK on the same macOS (a release that bundles another): the UUID alone.
    Maker newMvk = mac270;
    newMvk.uuid[3] = 0xa3;
    c = Check(kMac270, newMvk);
    CHECK(c.verdict == Verdict::OtherMaker && !c.vendorDiffers && !c.deviceDiffers && c.uuidDiffers, "MoltenVK update");
    line = LogLine(c, newMvk);
    CHECK(line.find("(cache UUID 000028a21b0002070000000100000000 -> 000028a31b0002070000000100000000; 0 KB set aside)")
            != std::string::npos && line.find("(OS ") == std::string::npos && line.find("device ID") == std::string::npos,
        "%s", line.c_str());

    // Mesa on the Steam Machine: a SteamOS update changes the UUID (its build ID) only.
    Maker radv{ 0x1002, 0x7480, {} };
    for (int i = 0; i < 16; i++)
        radv.uuid[i] = uint8_t(0x10 + i);
    Maker radvUpdated = radv;
    radvUpdated.uuid[15] ^= 0xFF;
    c = Check(Header(radv, 32, 1, 4096), radvUpdated);
    CHECK(c.verdict == Verdict::OtherMaker && c.uuidDiffers && !c.deviceDiffers, "Mesa update");
    CHECK(LogLine(c, radvUpdated).find("(cache UUID 101112131415161718191a1b1c1d1e1f -> 101112131415161718191a1b1c1d1ee0; 4 KB set aside)")
            != std::string::npos, "%s", LogLine(c, radvUpdated).c_str());
    CHECK(Check(Header(radv, 32, 1, 4096), radv).verdict == Verdict::Usable, "Mesa, same build");

    // Another GPU: the vendor and device IDs, and no OS reading (it's not one Apple GPU's ID against another).
    c = Check(Header(radv), mac270);
    CHECK(c.verdict == Verdict::OtherMaker && c.vendorDiffers && c.deviceDiffers && c.uuidDiffers, "AMD cache on an Apple GPU");
    line = LogLine(c, mac270);
    CHECK(line.find("(vendor 1002 -> 106b, device ID 00007480 -> 1b000207, cache UUID ") != std::string::npos
            && line.find("(OS ") == std::string::npos, "%s", line.c_str());
    // Another Apple GPU on the same macOS: the low half differs, the OS doesn't.
    const Maker m3 = Mac(0x1b000209);
    c = Check(kMac270, m3);
    CHECK(c.verdict == Verdict::OtherMaker && c.deviceDiffers, "M3 against an M1 cache");
    line = LogLine(c, m3);
    CHECK(line.find("(device ID 1b000207 -> 1b000209, ") != std::string::npos && line.find("(OS ") == std::string::npos, "%s",
        line.c_str());
    // An OS update that also moves the GPU family word: still read as the OS.
    const Maker m270b = Mac(0x1b000208);
    line = LogLine(Check(kMac265, m270b), m270b);
    CHECK(line.find("(OS 26.5 -> 27.0, device ID 1a050207 -> 1b000208, ") != std::string::npos, "%s", line.c_str());
    // Intel and AMD Macs report PCI IDs under MoltenVK: no OS reading for them.
    const Maker intel{ 0x8086, 0x1b000207, mac270.uuid }, intelOld{ 0x8086, 0x1a050207, mac265.uuid };
    line = LogLine(Check(Header(intelOld), intel), intel);
    CHECK(line.find("(device ID 1a050207 -> 1b000207, ") != std::string::npos && line.find("(OS ") == std::string::npos, "%s",
        line.c_str());

    // Nothing, and files that aren't a version-one header.
    c = CheckHeader(nullptr, 0, mac270);
    CHECK(c.verdict == Verdict::Empty && LogLine(c, mac270).empty(), "no file");
    c = CheckHeader(kMac270.data(), 31, mac270);
    CHECK(c.verdict == Verdict::Unreadable, "31 bytes");
    line = LogLine(c, mac270);
    CHECK(line == "[renderer] pipeline cache file not readable: rebuilding shaders, expect a brief stutter on first use "
                  "(31 bytes, shorter than its header)", "%s", line.c_str());
    CHECK(CheckHeader(kMac270.data(), 1, mac270).verdict == Verdict::Unreadable, "1 byte");
    c = Check(Header(mac270, 40, 1, 8), mac270);
    CHECK(c.verdict == Verdict::Unreadable && c.headerSize == 40, "a longer header (Mesa compares headerSize too)");
    line = LogLine(c, mac270);
    CHECK(line.find("(header size 40, version 1; 0 KB set aside)") != std::string::npos, "%s", line.c_str());
    c = Check(Header(mac270, 32, 2, 2048), mac270);
    CHECK(c.verdict == Verdict::Unreadable && c.headerVersion == 2, "another header version");
    CHECK(LogLine(c, mac270).find("(header size 32, version 2; 2 KB set aside)") != std::string::npos, "%s", LogLine(c, mac270).c_str());
    std::vector<uint8_t> bigEndian = kMac270;  // a writer that ignored the byte order: unreadable, not another device
    std::swap(bigEndian[0], bigEndian[3]);
    std::swap(bigEndian[4], bigEndian[7]);
    CHECK(Check(bigEndian, mac270).verdict == Verdict::Unreadable, "big-endian header fields");

    // Each field alone decides.
    for (int field = 0; field < 3; field++)
    {
        Maker other = mac270;
        if (field == 0)
            other.vendorID ^= 1;
        else if (field == 1)
            other.deviceID ^= 1;
        else
            other.uuid[15] ^= 1;
        c = Check(kMac270, other);
        CHECK(c.verdict == Verdict::OtherMaker && c.vendorDiffers == (field == 0) && c.deviceDiffers == (field == 1)
                && c.uuidDiffers == (field == 2), "field %d", field);
    }
    for (size_t i = 0; i < 16; i++)
    {
        Maker other = mac270;
        other.uuid[i] ^= 0x80;
        CHECK(Check(kMac270, other).verdict == Verdict::OtherMaker, "UUID byte %zu", i);
    }

    if (g_failures)
    {
        printf("pipeline_cache_header_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("pipeline_cache_header_test: all passed\n");
    return 0;
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Small, read-only STFS car packs for Burnout Revenge. Layout references:
// xenia-project/xenia: src/xenia/vfs/devices/stfs_xbox.h and
// stfs_container_device.cc (BSD-3-Clause). No RSA/license modification.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace game::addons
{
    struct File { std::string name; std::vector<uint8_t> bytes; };
    struct Package { std::u16string displayName; std::vector<File> files; };
    // One level-0 hash table, one file-table block, flat files. This is the
    // layout of the six promotional car packs, not a general STFS installer.
    inline constexpr size_t kMaxPackageSize = 1024 * 1024;

    template<class VerifySha1>
    std::optional<Package> Parse(std::span<const uint8_t> b, VerifySha1 verify, std::string& error)
    {
        auto fail = [&](const char* why) -> std::optional<Package> { error = why; return {}; };
        if (b.size() < 0x971A || b.size() > kMaxPackageSize)
            return fail("Truncated or oversized car pack");
        auto be = [&](size_t at, size_t n) { uint32_t v = 0; while (n--) v = (v << 8) | b[at++]; return v; };
        auto le24 = [&](size_t at) { return uint32_t(b[at]) | (uint32_t(b[at+1]) << 8) | (uint32_t(b[at+2]) << 16); };
        if (be(0, 4) != 0x4C495645 && be(0, 4) != 0x50495253)
            return fail("Expected a LIVE or PIRS read-only car pack");
        if (be(0x344, 4) != 2 || be(0x360, 4) != 0x454107DC)
            return fail("Not a Burnout Revenge Add-on (454107DC)");
        uint32_t blocks = be(0x395, 4);
        if (be(0x3A9, 4) != 0 || b[0x379] != 0x24 || (b[0x37B] & 3) != 1 ||
            b[0x37C] != 1 || b[0x37D] != 0 || blocks < 2 || blocks > 170)
            return fail("Unsupported STFS layout; only small promotional car packs are supported");
        uint32_t header = be(0x340, 4);
        if (header < 0x971A || header > kMaxPackageSize)
            return fail("Invalid STFS header size");
        size_t hash = (size_t(header) + 4095) & ~size_t(4095);
        if (hash + (size_t(blocks) + 1) * 4096 > b.size())
            return fail("Truncated STFS data blocks");
        if (!verify(b.subspan(hash, 4096), b.subspan(0x381, 20)))
            return fail("STFS hash-table checksum mismatch");
        auto blockOffset = [&](uint32_t n) { return hash + (size_t(n) + 1) * 4096; };
        auto nextBlock = [&](uint32_t n) { return be(hash + size_t(n) * 24 + 21, 3); };
        for (uint32_t n = 0; n < blocks; n++)
            if ((b[hash + size_t(n) * 24 + 20] >> 6) != 2 ||
                !verify(b.subspan(blockOffset(n), 4096), b.subspan(hash + size_t(n) * 24, 20)))
                return fail("STFS data-block checksum or allocation mismatch");
        uint32_t table = le24(0x37E);
        if (table >= blocks || nextBlock(table) != 0xFFFFFF)
            return fail("Invalid STFS file-table chain");
        Package result;
        for (size_t at = 0x411; at < 0x511 && be(at, 2); at += 2)
            result.displayName += char16_t(be(at, 2));
        std::array<bool, 170> used{};
        used[table] = true;
        for (size_t i = 0; i < 64; i++)
        {
            size_t at = blockOffset(table) + i * 64;
            if (!b[at]) break;
            size_t length = b[at + 40] & 63;
            if (!length || length > 40 || (b[at + 40] & 128) || be(at + 50, 2) != 0xFFFF)
                return fail("Only flat car-pack files are supported");
            std::string name(reinterpret_cast<const char*>(b.data() + at), length);
            if (name == "." || name == ".." || name.find_first_of("/\\:\0", 0, 4) != std::string::npos ||
                std::any_of(name.begin(), name.end(), [](unsigned char c) { return c < 32 || c >= 127; }))
                return fail("Invalid car-pack filename");
            for (const File& previous : result.files)
                if (std::equal(name.begin(), name.end(), previous.name.begin(), previous.name.end(),
                    [](unsigned char a, unsigned char b) { return (a | 32) == (b | 32); }))
                    return fail("Duplicate car-pack filename");
            uint32_t size = be(at + 52, 4), block = le24(at + 47);
            if (size > kMaxPackageSize || le24(at + 44) != (size + 4095) / 4096 ||
                le24(at + 41) != le24(at + 44))
                return fail("Invalid car-pack file size");
            File file{ std::move(name), {} };
            for (uint32_t remaining = size; remaining;)
            {
                if (block >= blocks || used[block]) return fail("Invalid or cyclic STFS file chain");
                used[block] = true;
                size_t count = std::min<uint32_t>(remaining, 4096), offset = blockOffset(block);
                file.bytes.insert(file.bytes.end(), b.begin() + offset, b.begin() + offset + count);
                remaining -= uint32_t(count);
                block = nextBlock(block);
            }
            if (size && block != 0xFFFFFF) return fail("Overlong STFS file chain");
            result.files.push_back(std::move(file));
        }
        if (result.files.empty()) return fail("Empty car pack");
        error.clear();
        return result;
    }
}

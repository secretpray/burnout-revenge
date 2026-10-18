// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// An Xbox 360 STFS package (CON, LIVE, PIRS) read whole: its header's title,
// content type, display name and thumbnail, and its files. For save imports
// (user/save_import.h). A console's own packages (CON) keep two copies of
// each hash table, written in turn; the copy in use is the one whose SHA-1
// the level above records (the volume descriptor for the top one), so each
// table is chosen by its hash, and every data block is checked against its
// entry. The add-on reader (game/burnout_addon_package.h) takes only the
// one-table layout of the car packs. Layout references: xenia-project/xenia
// src/xenia/vfs/devices/stfs_xbox.h and stfs_container_device.cc
// (BSD-3-Clause), and free60's STFS notes. No signature is checked or made:
// a console's RSA signature fits that console only, and the game doesn't
// look at it.
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace user::stfs
{
    using Digest = std::array<uint8_t, 20>;
    struct File
    {
        std::string path;  // "/" between folders
        std::vector<uint8_t> bytes;
    };
    struct Package
    {
        uint32_t contentType = 0, titleId = 0;
        std::u16string displayName;
        std::vector<uint8_t> thumbnail;  // a PNG, or empty
        std::vector<File> files;
    };

    inline constexpr size_t kBlock = 4096;
    inline constexpr size_t kMaxPackageSize = size_t(256) << 20;
    inline constexpr uint32_t kSaveContent = 1;

    // CON, LIVE or PIRS at the start of `b`.
    inline bool IsPackage(std::span<const uint8_t> b)
    {
        if (b.size() < 4)
            return false;
        const uint32_t magic = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
        return magic == 0x434F4E20 || magic == 0x4C495645 || magic == 0x50495253;
    }

    // `sha1(span) -> Digest`.
    template<class Sha1>
    std::optional<Package> Parse(std::span<const uint8_t> b, Sha1 sha1, std::string& error)
    {
        auto fail = [&](const std::string& why) -> std::optional<Package> { error = why; return {}; };
        auto fits = [&](size_t at, size_t n) { return at <= b.size() && n <= b.size() - at; };
        auto be = [&](size_t at, size_t n) { uint32_t v = 0; while (n--) v = (v << 8) | b[at++]; return v; };
        auto le = [&](size_t at, size_t n) { uint32_t v = 0; for (size_t i = 0; i < n; i++) v |= uint32_t(b[at + i]) << (8 * i); return v; };
        if (!IsPackage(b) || b.size() < 0xA000)
            return fail("Not an Xbox 360 package (CON, LIVE or PIRS).");
        if (b.size() > kMaxPackageSize)
            return fail("The package is too large for a save.");
        Package result;
        result.contentType = be(0x344, 4);
        result.titleId = be(0x360, 4);
        for (size_t at = 0x411; at < 0x511 && be(at, 2); at += 2)
            result.displayName += char16_t(be(at, 2));
        if (const uint32_t size = be(0x1712, 4); size && size <= 0x4000 && fits(0x171A, size))
            result.thumbnail.assign(b.begin() + 0x171A, b.begin() + 0x171A + size);
        if (b[0x379] != 0x24 || be(0x3A9, 4) != 0)
            return fail("The package isn't in the STFS layout saves use.");

        // Data blocks of 4 KB after the header, with their hash tables among
        // them: k copies of a level-0 table before each 170 blocks, of a
        // level-1 table before each 170 of those, and so on.
        const size_t start = (size_t(be(0x340, 4)) + kBlock - 1) & ~(kBlock - 1);
        const uint32_t k = (b[0x37B] & 1) ? 1 : 2;
        const uint32_t allocated = be(0x395, 4);
        if (start < 0x971A || allocated == 0 || allocated > kMaxPackageSize / kBlock)
            return fail("The package's header is damaged.");
        const int levels = allocated <= 170 ? 0 : allocated <= 170 * 170 ? 1 : 2;
        auto backing = [&](uint64_t n) {
            uint64_t at = n + k * (n / 170 + 1);
            if (levels >= 1)
                at += k * (n / (170 * 170) + 1);
            if (levels >= 2)
                at += k;
            return at;
        };
        auto blockAt = [&](uint64_t backingBlock) { return start + backingBlock * kBlock; };
        auto digestOf = [&](size_t at) { return sha1(b.subspan(at, kBlock)); };
        auto same = [&](const Digest& d, size_t at) { return std::equal(d.begin(), d.end(), b.begin() + at); };

        // The copy of table `index` at `level` in use: the one whose SHA-1 is
        // `expected` (an offset into `b`).
        std::map<std::pair<int, uint32_t>, size_t> tables;
        auto chooseTable = [&](int level, uint32_t index, size_t expected) -> size_t {
            const auto key = std::make_pair(level, index);
            if (auto it = tables.find(key); it != tables.end())
                return it->second;
            const uint64_t first = level == 0 ? uint64_t(index) * 170 : level == 1 ? uint64_t(index) * 170 * 170 : 0;
            uint64_t at = backing(first) - uint64_t(k) * (level + 1);
            size_t found = 0;
            for (uint32_t copy = 0; copy < k && !found; copy++)
                if (fits(blockAt(at + copy), kBlock) && same(digestOf(blockAt(at + copy)), expected))
                    found = blockAt(at + copy);
            tables[key] = found;
            return found;
        };
        // The level-0 hash entry of data block n (its SHA-1, status, next
        // block), or 0.
        auto entry = [&](uint32_t n) -> size_t {
            size_t table = chooseTable(levels, 0, 0x381);
            for (int level = levels; table && level > 0; level--)
            {
                const uint32_t divisor = level == 2 ? 170 * 170 : 170;
                const uint32_t index = n / divisor;
                const size_t record = table + size_t(index % 170) * 24;
                table = chooseTable(level - 1, index, record);
            }
            return table ? table + size_t(n % 170) * 24 : 0;
        };
        std::string blockError;
        auto readBlock = [&](uint32_t n, uint32_t* next) -> size_t {
            if (n >= allocated)
            {
                blockError = "a block past the package's end";
                return 0;
            }
            const size_t record = entry(n);
            if (!record)
            {
                blockError = "a hash table that doesn't match its checksum";
                return 0;
            }
            const size_t at = blockAt(backing(n));
            if (!fits(at, kBlock) || !same(digestOf(at), record))
            {
                blockError = "a data block that doesn't match its checksum";
                return 0;
            }
            if (next)
                *next = be(record + 21, 3);
            return at;
        };

        // The file table: 64-byte entries, 64 a block.
        struct Entry
        {
            std::string name;
            uint8_t flags;
            uint32_t blocks, first, size;
            uint16_t parent;
        };
        std::vector<Entry> entries;
        uint32_t block = le(0x37E, 3);
        bool ended = false;
        for (uint32_t i = 0, count = le(0x37C, 2); i < count && !ended; i++)
        {
            uint32_t next = 0xFFFFFF;
            const size_t at = readBlock(block, &next);
            if (!at)
                return fail("The package is damaged: " + blockError + ".");
            for (size_t e = 0; e < kBlock / 64; e++)
            {
                const size_t r = at + e * 64;
                const size_t length = b[r + 40] & 63;
                if (!length)
                {
                    ended = true;
                    break;
                }
                if (length > 40)
                    return fail("The package's file table is damaged.");
                Entry item{ std::string(reinterpret_cast<const char*>(b.data() + r), length), b[r + 40], le(r + 44, 3), le(r + 47, 3),
                    be(r + 52, 4), uint16_t(be(r + 50, 2)) };
                if (item.name == "." || item.name == ".." ||
                    std::any_of(item.name.begin(), item.name.end(), [](unsigned char c) { return c < 32 || c >= 127 || c == '/' || c == '\\' || c == ':'; }))
                    return fail("The package holds a file with an unusable name.");
                entries.push_back(std::move(item));
            }
            block = next;
        }

        size_t total = 0;
        for (size_t i = 0; i < entries.size(); i++)
        {
            const Entry& item = entries[i];
            if (item.flags & 0x80)
                continue;  // a folder: its files name it
            File file;
            file.path = item.name;
            for (uint16_t parent = item.parent, depth = 0; parent != 0xFFFF; parent = entries[parent].parent)
            {
                if (parent >= entries.size() || !(entries[parent].flags & 0x80) || ++depth > 16)
                    return fail("The package's folders are damaged.");
                file.path = entries[parent].name + "/" + file.path;
            }
            if (item.size > kMaxPackageSize || (total += item.size) > kMaxPackageSize ||
                uint64_t(item.blocks) * kBlock < item.size)
                return fail("The package's file sizes are damaged.");
            uint32_t n = item.first;
            for (uint32_t remaining = item.size, step = 0; remaining; step++)
            {
                uint32_t next = 0xFFFFFF;
                const size_t at = readBlock(n, &next);
                if (!at)
                    return fail("The package is damaged: " + blockError + ".");
                const size_t count = std::min<size_t>(remaining, kBlock);
                file.bytes.insert(file.bytes.end(), b.begin() + at, b.begin() + at + count);
                remaining -= uint32_t(count);
                // Consecutive (0x40): in order; else as the hash entries chain them.
                n = (item.flags & 0x40) ? n + 1 : next;
                if (step > allocated)
                    return fail("The package's block chain loops.");
            }
            result.files.push_back(std::move(file));
        }
        error.clear();
        return result;
    }
}

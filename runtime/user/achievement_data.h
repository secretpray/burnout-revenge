// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// XDBF/SPA layout documented by Xenia's kernel/util/xdbf_utils.{h,cc}.
// Read from the player's XEX at launch; no game text or artwork is shipped.
#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <vector>

namespace achievements
{
    struct Definition
    {
        uint32_t id = 0, imageId = 0, score = 0, flags = 0;
        std::string name, description, lockedDescription;
        std::vector<uint8_t> image;
    };

    inline uint32_t Read32(std::span<const uint8_t> b, size_t p)
    {
        return (uint32_t(b[p]) << 24) | (uint32_t(b[p + 1]) << 16) | (uint32_t(b[p + 2]) << 8) | b[p + 3];
    }
    inline uint16_t Read16(std::span<const uint8_t> b, size_t p) { return uint16_t((b[p] << 8) | b[p + 1]); }

    inline std::vector<Definition> ParseDatabase(std::span<const uint8_t> data, uint32_t language = 1)
    {
        if (data.size() < 24 || Read32(data, 0) != 0x58444246 || Read32(data, 4) != 0x10000) return {};
        const uint32_t capacity = Read32(data, 8), used = Read32(data, 12), free = Read32(data, 16);
        const uint64_t base = 24ull + 18ull * capacity + 8ull * free;
        if (used > capacity || base > data.size()) return {};
        std::map<std::pair<uint16_t, uint64_t>, std::span<const uint8_t>> blocks;
        for (uint32_t i = 0; i < used; ++i)
        {
            size_t p = 24 + 18 * size_t(i);
            uint64_t id = (uint64_t(Read32(data, p + 2)) << 32) | Read32(data, p + 6);
            uint64_t start = base + Read32(data, p + 10), length = Read32(data, p + 14);
            if (start > data.size() || length > data.size() - start) return {};
            if (!blocks.emplace(std::pair(Read16(data, p), id), data.subspan(start, length)).second) return {};
        }
        auto block = [&](uint16_t section, uint64_t id) -> std::span<const uint8_t> {
            auto it = blocks.find({section, id}); return it == blocks.end() ? std::span<const uint8_t>() : it->second;
        };
        auto table = block(1, 0x58414348);
        if (table.size() < 14 || Read32(table, 0) != 0x58414348 || Read32(table, 4) != 1) return {};
        auto readStrings = [](std::span<const uint8_t> strings) {
            std::map<uint16_t, std::string> text;
            if (strings.size() < 14 || Read32(strings, 0) != 0x58535452 || Read32(strings, 4) != 1) return text;
            size_t p = 14;
            for (uint16_t i = 0; i < Read16(strings, 12); ++i)
            {
                if (p + 4 > strings.size()) return std::map<uint16_t, std::string>{};
                uint16_t id = Read16(strings, p), length = Read16(strings, p + 2); p += 4;
                if (length > strings.size() - p || text.contains(id)) return std::map<uint16_t, std::string>{};
                text[id] = std::string(reinterpret_cast<const char*>(strings.data() + p), length); p += length;
            }
            return text;
        };
        auto text = readStrings(block(3, 1));
        if (text.empty()) return {};
        if (language != 1 && language >= 2 && language <= 6)
            for (auto& [id, value] : readStrings(block(3, language)))
                if (!value.empty()) text[id] = std::move(value);
        size_t p;
        uint16_t count = Read16(table, 12);
        if (count > 1024 || 14ull + 36ull * count > table.size()) return {};
        std::vector<Definition> result;
        for (uint16_t i = 0; i < count; ++i)
        {
            p = 14 + 36 * size_t(i);
            Definition d;
            d.id = Read16(table, p); d.name = text[Read16(table, p + 2)];
            d.description = text[Read16(table, p + 4)]; d.lockedDescription = text[Read16(table, p + 6)];
            d.imageId = Read32(table, p + 8); d.score = Read16(table, p + 12); d.flags = Read32(table, p + 16);
            if (!d.id || d.name.empty() || std::any_of(result.begin(), result.end(), [&](auto& a) { return a.id == d.id; })) return {};
            auto image = block(2, d.imageId); d.image.assign(image.begin(), image.end());
            result.push_back(std::move(d));
        }
        return result;
    }
}

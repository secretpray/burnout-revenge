// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
// X_ACHIEVEMENT_DETAILS, following Xenia's xam_user.cc layout.
#pragma once
#include <user/achievements.h>
#include <span>

namespace achievements
{
    inline void Put32(std::span<uint8_t> b, size_t p, uint32_t v)
    {
        b[p] = uint8_t(v >> 24); b[p + 1] = uint8_t(v >> 16); b[p + 2] = uint8_t(v >> 8); b[p + 3] = uint8_t(v);
    }
    inline size_t EnumerationSize(uint32_t count, uint32_t flags) { return size_t(count) * (36 + ((flags & 7) ? 464 : 0)); }
    // Strings follow the packed array of 36-byte details, not each detail.
    // UTF-8 -> UTF-16BE, reserving space for NUL and never splitting a pair.
    inline uint32_t AppendText(std::span<uint8_t> buffer, size_t& cursor, uint32_t guest, const std::string& text)
    {
        const size_t start = cursor;
        for (size_t i = 0; i < text.size();)
        {
            uint32_t cp = uint8_t(text[i++]);
            if (cp >= 0x80)
            {
                unsigned extra = cp < 0xE0 ? 1 : cp < 0xF0 ? 2 : 3;
                cp &= (1u << (6 - extra)) - 1;
                for (unsigned j = 0; j < extra; ++j)
                {
                    if (i == text.size() || (uint8_t(text[i]) & 0xC0) != 0x80) { cp = 0xFFFD; break; }
                    cp = (cp << 6) | (uint8_t(text[i++]) & 0x3F);
                }
            }
            if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = 0xFFFD;
            auto unit = [&](uint16_t u) { buffer[cursor++] = uint8_t(u >> 8); buffer[cursor++] = uint8_t(u); };
            if (cursor + (cp >= 0x10000 ? 6 : 4) > buffer.size()) break;
            if (cp >= 0x10000) { cp -= 0x10000; unit(uint16_t(0xD800 + (cp >> 10))); unit(uint16_t(0xDC00 + (cp & 1023))); }
            else unit(uint16_t(cp));
        }
        if (cursor + 2 > buffer.size()) return 0;
        buffer[cursor++] = 0; buffer[cursor++] = 0;
        return guest + uint32_t(start);
    }
    inline void WriteDetails(std::span<uint8_t> buffer, uint32_t guest, uint32_t flags, uint32_t capacity,
        std::span<const Definition> definitions, std::span<const Entry> entries)
    {
        std::fill(buffer.begin(), buffer.end(), 0);
        size_t cursor = size_t(capacity) * 36;
        for (size_t i = 0; i < entries.size(); ++i)
        {
            const auto& d = definitions[i]; const auto& e = entries[i]; size_t p = i * 36;
            Put32(buffer, p, d.id);
            if (flags & 1) Put32(buffer, p + 4, AppendText(buffer, cursor, guest, d.name));
            if (flags & 2) Put32(buffer, p + 8, AppendText(buffer, cursor, guest, d.description));
            if (flags & 4) Put32(buffer, p + 12, AppendText(buffer, cursor, guest, d.lockedDescription));
            Put32(buffer, p + 16, d.imageId); Put32(buffer, p + 20, d.score);
            uint64_t time = e.unlocked ? (uint64_t(e.unlocked) + 11644473600ull) * 10000000 : 0;
            Put32(buffer, p + 24, uint32_t(time >> 32)); Put32(buffer, p + 28, uint32_t(time));
            Put32(buffer, p + 32, (d.flags & ~0x30000u) | (e.unlocked ? 0x20000 : 0));
        }
    }
}

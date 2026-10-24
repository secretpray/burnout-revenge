// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include "crash_launch_texture.h"
#include <bit>
#include <cmath>

namespace game::crash_launch::texture
{
    struct Glyph
    {
        float u{}, v{}, width{}, height{}, x{}, y{}, advance{};
    };
    struct Font
    {
        std::array<Glyph, 128> glyphs{};
        Image image;
        float scaleX{}, scaleY{};
    };
    inline bool DecodeXboxFont(std::span<const uint8_t> file, Font& output)
    {
        auto little = [&](size_t p) { return uint32_t(file[p]) | uint32_t(file[p + 1]) << 8 |
            uint32_t(file[p + 2]) << 16 | uint32_t(file[p + 3]) << 24; };
        if (file.size() < 540 || little(0) != 0x76312E34) return false;
        const size_t texture = little(4);
        if (texture < 540 || texture > file.size() || file.size() - texture < 128) return false;
        auto number = [&](size_t p) { return std::bit_cast<float>(little(p)); };
        Font font;
        font.scaleX = number(8); font.scaleY = number(12);
        if (!std::isfinite(font.scaleX) || !std::isfinite(font.scaleY) ||
            font.scaleX <= 0 || font.scaleY <= 0 || font.scaleX > 64 || font.scaleY > 64) return false;
        for (unsigned c = 32; c < 127; ++c)
        {
            const size_t p = little(28 + c * 4);
            if (p < 540 || p > texture || texture - p < 32) return false;
            const unsigned code = unsigned(file[p + 28]) | unsigned(file[p + 29]) << 8;
            if (code != c) continue;
            Glyph g{number(p), number(p + 4), number(p + 8), number(p + 12),
                number(p + 16), number(p + 20), number(p + 24)};
            for (float n : {g.u, g.v, g.width, g.height, g.x, g.y, g.advance})
                if (!std::isfinite(n) || std::abs(n) > 2) return false;
            if (g.width < 0 || g.height < 0 || g.advance < 0 ||
                (g.height > 0 && (g.u < 0 || g.v < 0 || g.u + g.width > 1 || g.v + g.height > 1))) return false;
            font.glyphs[c] = g;
        }
        const unsigned width = little(texture + 56), height = little(texture + 60);
        if (!font.glyphs['A'].height || !font.glyphs[' '].advance || file[texture + 13] != 14 ||
            !width || !height || width > 1024 || height > 1024 || width % 4 || height % 4 ||
            !DecodeBlocks(file.subspan(texture + 128), width, height, 19, false, font.image)) return false;
        output = std::move(font);
        return true;
    }
    inline bool DecodeFont(std::span<const uint8_t> file, Font& output)
    {
        if (file.size() < 544 || Big32(file, 0) != 0x76312E34) return false;
        const size_t texture = Big32(file, 4);
        if (texture < 544 || texture > file.size()) return false;
        auto number = [&](size_t p) { return std::bit_cast<float>(Big32(file, p)); };
        Font font;
        font.scaleX = number(8); font.scaleY = number(12);
        if (!std::isfinite(font.scaleX) || !std::isfinite(font.scaleY) ||
            font.scaleX <= 0 || font.scaleY <= 0 || font.scaleX > 64 || font.scaleY > 64) return false;
        for (unsigned c = 32; c < 127; ++c)
        {
            size_t p = Big32(file, 32 + c * 4);
            // Hash chains may include other code points. Bound every link and
            // the traversal so a malformed resource cannot loop or escape.
            for (unsigned links = 0; p && links < 256; ++links)
            {
                if (p < 544 || p > texture || texture - p < 40) return false;
                const unsigned code = unsigned(file[p + 28]) << 8 | file[p + 29];
                if (code == c)
                {
                    Glyph g{number(p), number(p + 4), number(p + 8), number(p + 12),
                        number(p + 16), number(p + 20), number(p + 24)};
                    for (float n : {g.u, g.v, g.width, g.height, g.x, g.y, g.advance})
                        if (!std::isfinite(n) || std::abs(n) > 2) return false;
                    if (g.width < 0 || g.height < 0 || g.advance < 0 ||
                        (g.height > 0 && (g.u < 0 || g.v < 0 || g.u + g.width > 1 || g.v + g.height > 1))) return false;
                    font.glyphs[c] = g;
                    break;
                }
                const size_t next = Big32(file, p + 32);
                if (next == p || links == 255) return false;
                p = next;
            }
        }
        if (!font.glyphs['A'].height || !font.glyphs[' '].advance || !Decode(file, texture, font.image)) return false;
        output = std::move(font);
        return true;
    }
}

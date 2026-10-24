// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace game::crash_launch::texture
{
    struct Image { unsigned width{}, height{}; std::vector<uint8_t> rgba; };
    inline uint32_t Big32(std::span<const uint8_t> b, size_t p)
    {
        return uint32_t(b[p]) << 24 | uint32_t(b[p + 1]) << 16 | uint32_t(b[p + 2]) << 8 | b[p + 3];
    }
    // Same Xenos 2D tiling as the runtime texture uploader. Coordinates and
    // pitch are in blocks for BC textures and pixels for uncompressed textures.
    inline size_t Tiled(unsigned x, unsigned y, unsigned pitch, unsigned logBytes)
    {
        const unsigned outer = (((y >> 5) * (pitch >> 5)) + (x >> 5)) << 6;
        const unsigned inner = (((y >> 1) & 7) << 3) | (x & 7);
        const unsigned oi = (outer | inner) << logBytes;
        const unsigned pipe = ((x >> 3) & 3) ^ (((y >> 3) & 1) << 1);
        return ((y & 1) << 4) | (pipe << 6) | (((y >> 4) & 1) << 11) |
            (oi & 15) | (((oi >> 4) & 1) << 5) | (((oi >> 5) & 7) << 8) | ((oi >> 8) << 12);
    }
    inline bool DecodeBlocks(std::span<const uint8_t> pixels, unsigned width, unsigned height,
        unsigned format, bool tiled, Image& image)
    {
        if (!width || !height || width > 1024 || height > 1024 || width % 4 || height % 4 ||
            (format != 18 && format != 19)) return false;
        const unsigned bytes = format == 18 ? 8 : 16;
        const unsigned pitch = ((width / 4 + 31) / 32) * 32;
        Image decoded{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
        for (unsigned y = 0; y < height / 4; ++y)
            for (unsigned x = 0; x < width / 4; ++x)
            {
                const size_t p = tiled ? Tiled(x, y, pitch, bytes == 8 ? 3 : 4) : (size_t(y) * (width / 4) + x) * bytes;
                if (p > pixels.size() || pixels.size() - p < bytes) return false;
                std::array<uint8_t, 16> block{};
                for (unsigned i = 0; i < bytes; ++i) block[i] = pixels[p + (tiled ? i ^ 1 : i)];
                const unsigned c = bytes - 8;
                const unsigned a = unsigned(block[c]) | unsigned(block[c + 1]) << 8;
                const unsigned b = unsigned(block[c + 2]) | unsigned(block[c + 3]) << 8;
                const uint32_t indices = uint32_t(block[c + 4]) | uint32_t(block[c + 5]) << 8 |
                    uint32_t(block[c + 6]) << 16 | uint32_t(block[c + 7]) << 24;
                std::array<std::array<unsigned, 3>, 4> colors{};
                for (unsigned j = 0; j < 2; ++j)
                {
                    const unsigned value = j ? b : a;
                    colors[j] = {(value >> 11 & 31) * 255 / 31, (value >> 5 & 63) * 255 / 63, (value & 31) * 255 / 31};
                }
                const bool four = format == 19 || a > b;
                for (unsigned j = 0; j < 3; ++j)
                {
                    colors[2][j] = four ? (2 * colors[0][j] + colors[1][j]) / 3 : (colors[0][j] + colors[1][j]) / 2;
                    colors[3][j] = four ? (colors[0][j] + 2 * colors[1][j]) / 3 : 0;
                }
                for (unsigned i = 0; i < 16; ++i)
                {
                    const unsigned index = indices >> (2 * i) & 3;
                    const size_t dst = ((y * 4 + i / 4) * width + x * 4 + i % 4) * 4;
                    for (unsigned j = 0; j < 3; ++j) decoded.rgba[dst + j] = uint8_t(colors[index][j]);
                    decoded.rgba[dst + 3] = !four && index == 3 ? 0 : format == 18 ? 255 :
                        uint8_t(((block[i / 2] >> (4 * (i & 1))) & 15) * 17);
                }
            }
        image = std::move(decoded);
        return true;
    }
    inline bool Decode(std::span<const uint8_t> file, size_t offset, Image& image)
    {
        if (offset > file.size() || file.size() - offset < 96) return false;
        const auto header = file.subspan(offset);
        const unsigned width = unsigned(header[32]) << 8 | header[33];
        const unsigned height = unsigned(header[34]) << 8 | header[35];
        const unsigned format = Big32(header, 40) & 63;
        const unsigned length = Big32(header, 44), start = Big32(header, 48);
        if (!width || !height || width > 1024 || height > 1024 ||
            (format != 6 && format != 18 && format != 19) ||
            (format != 6 && (width % 4 || height % 4)) ||
            start < 96 || start > length || length > header.size()) return false;
        const auto pixels = header.subspan(start, length - start);
        if (format == 6)
        {
            Image decoded{width, height, std::vector<uint8_t>(size_t(width) * height * 4)};
            const unsigned pitch = (width + 31) / 32 * 32;
            for (unsigned y = 0; y < height; ++y)
                for (unsigned x = 0; x < width; ++x)
                {
                    const size_t p = Tiled(x, y, pitch, 2);
                    if (p > pixels.size() || pixels.size() - p < 4) return false;
                    const size_t dst = (size_t(y) * width + x) * 4;
                    // Retail font fetch constants specify 8-in-32: ARGB bytes.
                    for (unsigned i = 0; i < 3; ++i) decoded.rgba[dst + i] = pixels[p + i + 1];
                    decoded.rgba[dst + 3] = pixels[p];
                }
            image = std::move(decoded);
            return true;
        }
        return DecodeBlocks(pixels, width, height, format, true, image);
    }
}

// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#include <game/crash_launch_texture.h>
#include <game/crash_launch_font.h>
#include <cassert>

using namespace game::crash_launch::texture;
void Put(std::vector<uint8_t>& b, size_t p, uint32_t v)
{ for (unsigned i = 0; i < 4; ++i) b[p + i] = uint8_t(v >> (24 - i * 8)); }
int main()
{
    Image output;
    assert(!Decode({}, 0, output));
    for (unsigned format : {18u, 19u})
    {
        const unsigned bytes = format == 18 ? 8 : 16;
        std::vector<uint8_t> file(0x1000 + 0x4000, 0);
        Put(file, 32, 128u << 16 | 64);
        Put(file, 40, format);
        Put(file, 44, uint32_t(file.size()));
        Put(file, 48, 0x1000);
        for (unsigned y = 0; y < 16; ++y)
            for (unsigned x = 0; x < 32; ++x)
            {
                std::array<uint8_t, 16> block{};
                if (format == 19)
                    for (unsigned i = 0; i < 8; ++i) block[i] = uint8_t((i * 2) | (i * 2 + 1) << 4);
                const unsigned color = x << 11 | (y * 4) << 5 | 31;
                const unsigned c = bytes - 8;
                block[c] = uint8_t(color); block[c + 1] = uint8_t(color >> 8);
                const size_t off = 0x1000 + Tiled(x, y, 32, format == 18 ? 3 : 4);
                for (unsigned i = 0; i < bytes; ++i) file[off + (i ^ 1)] = block[i];
            }
        assert(Decode(file, 0, output));
        assert(output.width == 128 && output.height == 64);
        for (unsigned y = 0; y < 64; ++y)
            for (unsigned x = 0; x < 128; ++x)
            {
                const size_t p = (y * 128 + x) * 4;
                assert(output.rgba[p] == (x / 4) * 255 / 31);
                assert(output.rgba[p + 1] == (y / 4 * 4) * 255 / 63);
                assert(output.rgba[p + 2] == 255);
                assert(output.rgba[p + 3] == (format == 18 ? 255 : (y % 4 * 4 + x % 4) * 17));
            }
        const auto saved = output.rgba;
        for (size_t cut : {size_t(0), size_t(95), file.size() - 1})
        { assert(!Decode(std::span(file).first(cut), 0, output)); assert(output.rgba == saved); }
        assert(!Decode(file, file.size() + 1, output));
        auto bad = file; Put(bad, 32, 4096u << 16 | 64); assert(!Decode(bad, 0, output));
        bad = file; Put(bad, 40, 20); assert(!Decode(bad, 0, output));
        bad = file; Put(bad, 48, 0xFFFFFFF0); assert(!Decode(bad, 0, output));
    }
    // Uncompressed native font atlas, including Xenos macro/micro tile
    // boundaries, all channels, hash collisions and malformed chain offsets.
    std::vector<uint8_t> file(0x4000, 0);
    Put(file, 0, 0x76312E34); Put(file, 4, 0x1000);
    Put(file, 8, std::bit_cast<uint32_t>(8.f)); Put(file, 12, std::bit_cast<uint32_t>(8.f));
    Put(file, 32 + 'A' * 4, 600); Put(file, 32 + ' ' * 4, 640);
    Put(file, 600 + 8, std::bit_cast<uint32_t>(.1f));
    Put(file, 600 + 12, std::bit_cast<uint32_t>(.1f));
    Put(file, 600 + 24, std::bit_cast<uint32_t>(.12f));
    file[600 + 29] = 'A';
    Put(file, 640 + 24, std::bit_cast<uint32_t>(.04f)); file[640 + 29] = ' ';
    Put(file, 0x1000 + 32, 64u << 16 | 32);
    Put(file, 0x1000 + 40, 6);
    Put(file, 0x1000 + 44, 0x3000); Put(file, 0x1000 + 48, 0x1000);
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 64; ++x)
        {
            const size_t p = 0x2000 + Tiled(x, y, 64, 2);
            file[p] = uint8_t(x + y); file[p + 1] = uint8_t(x);
            file[p + 2] = uint8_t(y); file[p + 3] = uint8_t(x ^ y);
        }
    Font font;
    assert(DecodeFont(file, font));
    assert(font.glyphs['A'].advance == .12f && font.glyphs[' '].advance == .04f);
    for (unsigned y = 0; y < 32; ++y)
        for (unsigned x = 0; x < 64; ++x)
        {
            const size_t p = (y * 64 + x) * 4;
            assert(font.image.rgba[p] == x && font.image.rgba[p + 1] == y);
            assert(font.image.rgba[p + 2] == (x ^ y) && font.image.rgba[p + 3] == x + y);
        }
    auto bad = file; Put(bad, 32 + 'A' * 4, 0xFFFFF0); assert(!DecodeFont(bad, font));
    bad = file; Put(bad, 600 + 8, 0x7FC00000); assert(!DecodeFont(bad, font));
    bad = file; bad[600 + 29] = 200; Put(bad, 600 + 32, 600); assert(!DecodeFont(bad, font));
    bad = file; Put(bad, 0x1000 + 44, 0x3001); assert(!DecodeFont(bad, font));

    std::vector<uint8_t> xbox(0x1000 + 128 + 2048, 0);
    auto le = [&](size_t p, uint32_t v) {
        for (unsigned i = 0; i < 4; ++i) xbox[p + i] = uint8_t(v >> (i * 8));
    };
    le(0, 0x76312E34); le(4, 0x1000);
    le(8, std::bit_cast<uint32_t>(6.4f)); le(12, std::bit_cast<uint32_t>(12.8f));
    for (unsigned c = 32; c < 127; ++c) le(28 + c * 4, 600);
    le(28 + ' ' * 4, 640);
    le(600 + 8, std::bit_cast<uint32_t>(.1f)); le(600 + 12, std::bit_cast<uint32_t>(.1f));
    le(600 + 24, std::bit_cast<uint32_t>(.12f)); xbox[600 + 28] = 'A';
    le(640 + 24, std::bit_cast<uint32_t>(.04f)); xbox[640 + 28] = ' ';
    xbox[0x1000 + 13] = 14; le(0x1000 + 56, 64); le(0x1000 + 60, 32);
    for (unsigned block = 0; block < 128; ++block)
    {
        const size_t p = 0x1000 + 128 + block * 16;
        for (unsigned i = 0; i < 8; ++i) xbox[p + i] = 0xFF;
        xbox[p + 8] = 0xFF; xbox[p + 9] = 0xFF;
    }
    assert(DecodeXboxFont(xbox, font));
    assert(font.scaleX == 6.4f && font.image.width == 64);
    for (uint8_t v : font.image.rgba) assert(v == 255);
    assert(!DecodeXboxFont(std::span(xbox).first(xbox.size() - 1), font));
    le(28 + 'A' * 4, 0xFFFFFFFF); assert(!DecodeXboxFont(xbox, font));
}

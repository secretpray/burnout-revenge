// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// apu::CopyBits (apu/bit_copy.h) against copying a bit at a time, as the
// XMA decoder did: every alignment of source and destination, lengths from
// none to a whole XMA packet, the destination's other bits kept, and no
// byte read or written outside the bits copied.
#include <apu/bit_copy.h>
#include <cassert>
#include <random>
#include <vector>

int main()
{
    std::mt19937 rng(20261016);
    constexpr uint32_t kBytes = 2048 + 16;  // an XMA packet and some
    std::vector<uint8_t> src(kBytes), expected(kBytes), got(kBytes);
    auto check = [&](uint32_t dstPos, uint32_t srcPos, uint32_t count) {
        for (auto& b : src) b = uint8_t(rng());
        for (uint32_t i = 0; i < kBytes; i++)
            expected[i] = got[i] = uint8_t(rng());
        for (uint32_t i = 0; i < count; i++)
            apu::SetBit(expected.data(), dstPos + i, apu::GetBit(src.data(), srcPos + i));
        apu::CopyBits(got.data(), dstPos, src.data(), srcPos, count);
        assert(got == expected);
    };
    // Every alignment pair, short and long runs.
    for (uint32_t d = 0; d < 8; d++)
        for (uint32_t s = 0; s < 8; s++)
            for (uint32_t count : { 0u, 1u, 7u, 8u, 9u, 15u, 16u, 17u, 63u, 64u, 65u, 1000u, 16352u })
                check(d + 8, s + 16, count);
    // Random positions and lengths.
    for (int i = 0; i < 20000; i++)
    {
        uint32_t count = rng() % 4200;
        uint32_t dstPos = rng() % (kBytes * 8 - count);
        uint32_t srcPos = rng() % (kBytes * 8 - count);
        check(dstPos, srcPos, count);
    }

    // Nothing outside the bits: guard bytes either side, the source copied
    // into a buffer of exactly the bytes those bits touch.
    for (uint32_t s = 0; s < 8; s++)
        for (uint32_t count = 1; count < 70; count++)
        {
            uint32_t firstByte = s / 8, lastByte = (s + count - 1) / 8;
            std::vector<uint8_t> exact(lastByte - firstByte + 1);
            for (auto& b : exact) b = uint8_t(rng());
            std::vector<uint8_t> out(32, 0xA5);
            apu::CopyBits(out.data() + 8, 3, exact.data(), s % 8, count);
            for (uint32_t i = 0; i < 8; i++)
                assert(out[i] == 0xA5);
            for (uint32_t i = 8 + (3 + count + 7) / 8; i < 32; i++)
                assert(out[i] == 0xA5);
            for (uint32_t i = 0; i < count; i++)
                assert(apu::GetBit(out.data() + 8, 3 + i) == apu::GetBit(exact.data(), s % 8 + i));
        }
}

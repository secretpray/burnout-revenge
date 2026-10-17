// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Copying a run of bits between MSB-first bit strings (XMA packets and
// frames, apu/xma.cpp), header only so tests/bit_copy_test.cpp builds it
// alone. Bit n of a string is bit 7 - n % 8 of its byte n / 8.
//
// XMA frames are a few thousand bits, gathered out of their packets and
// laid into a packet again for FFmpeg on every frame of every voice. Done a
// bit at a time that was a third of the audio thread in a Burnout Revenge
// race (M1 Pro, 2026-10-16), which has 5.33 ms per frame for all of the
// game's mixing; whole bytes at a time it is a small part of that.
#pragma once
#include <cstdint>
#include <cstring>

namespace apu
{
    inline uint32_t GetBit(const uint8_t* data, uint32_t pos) { return (data[pos >> 3] >> (7 - (pos & 7))) & 1u; }

    inline void SetBit(uint8_t* data, uint32_t pos, uint32_t v)
    {
        uint8_t mask = uint8_t(0x80 >> (pos & 7));
        data[pos >> 3] = uint8_t(v ? data[pos >> 3] | mask : data[pos >> 3] & ~mask);
    }

    // dst's bits [dstPos, dstPos + count) become src's [srcPos, srcPos +
    // count); dst's other bits stay. Reads no byte of src outside the bits
    // copied, writes none of dst outside them.
    inline void CopyBits(uint8_t* dst, uint32_t dstPos, const uint8_t* src, uint32_t srcPos, uint32_t count)
    {
        // Up to dst's next byte.
        for (; count && (dstPos & 7); count--)
            SetBit(dst, dstPos++, GetBit(src, srcPos++));
        // Whole bytes of dst: from src's bytes as they are, or each from two.
        uint32_t bytes = count >> 3;
        uint8_t* d = dst + (dstPos >> 3);
        const uint8_t* s = src + (srcPos >> 3);
        if (uint32_t shift = srcPos & 7; shift == 0)
            memcpy(d, s, bytes);
        else
            for (uint32_t i = 0; i < bytes; i++)
                d[i] = uint8_t(s[i] << shift | s[i + 1] >> (8 - shift));
        dstPos += bytes * 8;
        srcPos += bytes * 8;
        // The rest.
        for (count &= 7; count; count--)
            SetBit(dst, dstPos++, GetBit(src, srcPos++));
    }
}

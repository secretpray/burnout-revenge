// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// STFS block checksums, not authentication. Apple keeps CommonCrypto.
#pragma once
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <span>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <thirdparty/TinySHA1.hpp>
#endif
namespace platform
{
    inline std::array<uint8_t, 20> Sha1(std::span<const uint8_t> bytes)
    {
        std::array<uint8_t, 20> digest{};
#ifdef __APPLE__
        CC_SHA1(bytes.data(), CC_LONG(bytes.size()), digest.data());
#else
        // This TinySHA1 version encodes a 32-bit bit count. STFS inputs are
        // bounded to 256 MiB (individual checksums cover 4 KiB blocks).
        assert(bytes.size() < (size_t(1) << 29));
        sha1::SHA1 hash;
        // Empty spans may have no data pointer; TinySHA1 computes data+size.
        if (!bytes.empty()) hash.processBytes(bytes.data(), bytes.size());
        sha1::SHA1::digest8_t result;
        hash.finalize(result);
        std::copy(std::begin(result), std::end(result), digest.begin());
#endif
        return digest;
    }
}

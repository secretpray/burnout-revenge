// Burnout Revenge. GPL-3.0-or-later (see COPYING).
// Published SHA-1 vectors plus independent hashlib padding-boundary vectors.
#include <platform/sha1.h>
#include <cassert>
#include <string>
static std::string Hex(const std::string& text)
{
    const auto digest = platform::Sha1({reinterpret_cast<const uint8_t*>(text.data()), text.size()});
    std::string hex;
    for (uint8_t byte : digest) { hex += "0123456789abcdef"[byte >> 4]; hex += "0123456789abcdef"[byte & 15]; }
    return hex;
}
int main()
{
    assert(Hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
    assert(Hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
    assert(Hex(std::string(1000000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
    assert(Hex(std::string(55, 'x')) == "cef734ba81a024479e09eb5a75b6ddae62e6abf1");
    assert(Hex(std::string(56, 'x')) == "901305367c259952f4e7af8323f480d59f81335b");
    assert(Hex(std::string(63, 'x')) == "0ddc4e0cccd9a12850deb5abb0853a4425559fec");
    assert(Hex(std::string(64, 'x')) == "bb2fa3ee7afb9f54c6dfb5d021f14b1ffe40c163");
    assert(Hex(std::string(65, 'x')) == "78c741ddc482e4cdf8c474a0876347a0905b6233");
    assert(Hex(std::string(4096, 'x')) == "0494dc592da04a1753223918ea73bcb86876372c");
}

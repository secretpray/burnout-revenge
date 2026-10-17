// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Synthetic STFS blocks; no game data. Exercises hostile paths and chains,
// bounds, title filtering and the mandatory integrity-verification callback.
#include <game/burnout_addon_package.h>
#include <cassert>
#include <cstring>
#include <cstdio>

using Bytes = std::vector<uint8_t>;
static void Be(Bytes& b, size_t at, uint32_t value, size_t n)
{ while (n) { b[at + --n] = uint8_t(value); value >>= 8; } }
static Bytes Fixture()
{
    Bytes b(0xE000);
    memcpy(b.data(), "LIVE", 4);
    Be(b, 0x340, 0xAD0E, 4); Be(b, 0x344, 2, 4); Be(b, 0x360, 0x454107DC, 4);
    b[0x379] = 0x24; b[0x37B] = 1; b[0x37C] = 1; Be(b, 0x395, 2, 4);
    b[0xB014] = b[0xB02C] = 0x80;
    Be(b, 0xB015, 0xFFFFFF, 3); Be(b, 0xB02D, 0xFFFFFF, 3);
    memcpy(b.data()+0xC000, "Content.txt", 11); b[0xC028] = 0x40 | 11;
    b[0xC029] = b[0xC02C] = b[0xC02F] = 1;
    Be(b, 0xC032, 0xFFFF, 2); Be(b, 0xC034, 3, 4);
    memcpy(b.data()+0xD000, "car", 3);
    return b;
}
int main()
{
    std::string error;
    size_t verified = 0;
    auto verify = [&](std::span<const uint8_t> data, std::span<const uint8_t> digest) {
        assert(data.size() == 4096 && digest.size() == 20); verified++; return true;
    };
    Bytes b = Fixture();
    auto parsed = game::addons::Parse(b, verify, error);
    assert(parsed && parsed->files.size() == 1 && parsed->files[0].name == "Content.txt");
    assert(parsed->files[0].bytes == Bytes({'c','a','r'}) && verified == 3);
    auto rejects = [&](Bytes bad) { assert(!game::addons::Parse(bad, verify, error) && !error.empty()); };
    for (size_t size : {0u, 0x9719u, 0xB000u, 0xDFFFu}) { auto bad = b; bad.resize(size); rejects(bad); }
    auto bad = b; Be(bad, 0x360, 0x454107D9, 4); rejects(bad); // another EA game
    bad = b; Be(bad, 0x344, 1, 4); rejects(bad); // save, not DLC
    bad = b; bad[0x37B] = 0; rejects(bad); // writable/double hash tables
    bad = b; Be(bad, 0x395, 171, 4); rejects(bad); // second hash level
    bad = b; Be(bad, 0x340, 0xFFFFFFFF, 4); rejects(bad); // offset overflow
    bad = b; bad[0x37E] = 2; rejects(bad); // file table past data
    bad = b; memcpy(bad.data()+0xC000, "../evil.txt", 11); rejects(bad);
    bad = b; bad[0xC004] = 0; rejects(bad); // embedded NUL
    bad = b; bad[0xC028] |= 0x80; rejects(bad); // unsupported directories
    bad = b; bad[0xC032] = 0; rejects(bad); // invalid parent
    bad = b; Be(bad, 0xC034, 0xFFFFFFFF, 4); rejects(bad); // huge file
    bad = b; Be(bad, 0xB02D, 1, 3); rejects(bad); // self cycle / overlong chain
    bad = b; bad[0xC02F] = 0; rejects(bad); // file overlaps its table
    assert(!game::addons::Parse(b, [](auto, auto) { return false; }, error)); // corrupt table
    size_t call = 0;
    assert(!game::addons::Parse(b, [&](auto, auto) { return ++call != 3; }, error)); // corrupt payload
    puts("Burnout car-pack bounds, paths, chains, title and integrity checks passed");
}

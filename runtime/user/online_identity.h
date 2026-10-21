// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
//
// Who this console is online (docs/ONLINE.md): its gamertag and the
// XUID that goes with it. Header only, so tests/online_identity_test.cpp can
// check it without the game.
//
// Offline every profile is 0xB13EBABEBABEBABE, "Player", and saves may hang
// on that, so it stays. Online the game tells the host of a session from the
// others by comparing XUIDs (the 'sess' step, 0x8240C908: host when the
// host's XUID, ref+0x70, is this console's, ref+0x80) and names the peer link
// after both ("…#$<host>-$<self>", CommUDP), so two players with one XUID
// would be one player. Each gamertag gets its own: the same 0xB13EBABE top
// half, and the gamertag's FNV-1a in the bottom one with its top bit set, so
// it never comes out as the offline value. The lobby server (Xerenge's
// ealobby, whose protocol we follow) knows a player by the FNV-1a of the
// gamertag as well (OPID), so two copies need two gamertags anyway.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>

namespace online
{
    constexpr uint64_t kOfflineXuid = 0xB13EBABEBABEBABEull;
    constexpr const char* kDefaultGamertag = "Player";
    constexpr size_t kMaxGamertag = 15;  // as Xbox Live's

    constexpr uint32_t Fnv1a(std::string_view text)
    {
        uint32_t h = 2166136261u;
        for (unsigned char c : text)
            h = (h ^ c) * 16777619u;
        return h;
    }

    constexpr uint64_t XuidFor(std::string_view gamertag)
    {
        uint32_t low = Fnv1a(gamertag) | 0x80000000u;
        if (low == uint32_t(kOfflineXuid))
            low ^= 1;
        return (kOfflineXuid & 0xFFFFFFFF00000000ull) | low;
    }

    // The gamertag when none is chosen: "Player" and four digits from the
    // Mac's account name - steady on one Mac, most likely different on the
    // next, and the name itself never leaves the Mac.
    inline std::string DefaultGamertagFor(std::string_view account)
    {
        const uint32_t digits = Fnv1a(account) % 10000;
        char text[16];
        snprintf(text, sizeof(text), "Player%04u", digits);
        return text;
    }

    // What the lobby accepts as a gamertag, as Xerenge's installer asks for
    // it: up to 15 letters, digits, '-' and '_'. Anything else is dropped;
    // nothing left gives the default.
    inline std::string SanitizeGamertag(std::string_view text)
    {
        std::string out;
        for (char c : text)
        {
            const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
            if (ok && out.size() < kMaxGamertag)
                out += c;
        }
        return out.empty() ? std::string(kDefaultGamertag) : out;
    }
}

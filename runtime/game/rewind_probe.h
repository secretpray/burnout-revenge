// Burnout Revenge port. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>
#include "codes.h"

namespace game::rewind
{
    // USA main game object and its current/next state, read by sub_8211FA28.
    inline constexpr uint32_t kGame = 0x828465E0;
    inline constexpr uint32_t kPhase = kGame + 0x73FA0;
    inline constexpr uint32_t kWorldClock = 0x82AB6FA4;
    // Record/playback clock flags tested by sub_8210B3C8 / sub_820F9848.
    // Road Rage live: 01 00 00 00; native Replay: 01 01 00 00.
    inline constexpr uint32_t kClockFlags = 0x82AB6F80 + 0x476E4;
    // Native sub_823456A8 unconditionally enters read-only Replay after loading.
    // The serializer itself shares this mode with sub_82344708.
    inline constexpr uint32_t kWorldPlayback = 0x82AB6F80 + 0x25BFFB;
    static_assert(kGame + 0x73F58 == codes::kSession);

    struct Boundary
    {
        uint32_t game{}, phase{}, session{}, controller{}, event{}, localPlayers{}, specialMode{}, crashVariant{};
        bool online{};
    };

    // Only the first, solo World Tour experiment. A successful predicate is
    // permission to OBSERVE this boundary, never permission to copy/restore RAM.
    constexpr bool CanObserve(const Boundary& b)
    {
        return b.game == kGame && b.phase == 5 && b.event != 0 &&
            codes::Allowed({true, b.online, b.session, b.controller,
                b.localPlayers, b.specialMode, b.crashVariant, 0, 0});
    }

    // Fail closed for playback, instructions/results and unverified flag sets.
    constexpr bool IsLiveClock(uint32_t flags) { return flags == 0x01000000; }
}

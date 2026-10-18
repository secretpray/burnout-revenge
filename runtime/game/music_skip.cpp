// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// The soundtrack's track skip in Crash mode. Once a frame the music update
// (sub_822CB4F0) asks sub_8210F940 whether a pad of the mode's players has
// the skip "just pressed": its button 20 down now (pad+37+20) and not at the
// poll before (pad+9+20) (sub_82360878). The pads are polled once per logic
// step, and Crash mode runs two steps a frame: by the time the music update
// looks, the second poll has made "before" equal "now", and a press only got
// through when it fell between the two. Here a press also counts as a frame
// sees it: down now and not at the last call. The finding and the addresses
// are from shipa-2's Xerenge (its music_trace.cpp); this code is ours.
#include <stdafx.h>

#include <cstdlib>

namespace
{
    constexpr uint32_t kSkipButton = 20;

    // Whether a pad the players are bound to (game+2441, +2442 their ports,
    // used when +2443, +2444 are set; a port's pad at game+1000+port*360+4)
    // is connected (pad+8) with the skip button down now.
    bool SkipDown(uint32_t game, uint8_t* base)
    {
        bool down = false;
        for (uint32_t i = 0; i < 2; i++)
        {
            if (!PPC_LOAD_U8(game + 2443 + i))
                continue;
            const int8_t port = int8_t(PPC_LOAD_U8(game + 2441 + i));
            if (port < 0 || port >= 4)
                continue;
            const uint32_t pad = PPC_LOAD_U32(game + 1000 + uint32_t(port) * 360 + 4);
            if (pad >= 0x10000 && PPC_LOAD_U8(pad + 8) && PPC_LOAD_U8(pad + 37 + kSkipButton))
                down = true;
        }
        return down;
    }
}

extern "C" PPC_FUNC(__imp__sub_8210F940);
PPC_FUNC(sub_8210F940)
{
    const uint32_t game = ctx.r3.u32;
    __imp__sub_8210F940(ctx, base);
    // (Its one caller is the music update, on the guest's main thread.)
    static bool wasDown = false;
    const bool down = SkipDown(game, base);
    const bool gamePressed = (ctx.r3.u32 & 0xFF) != 0;
    if (down && !wasDown && !gamePressed)
    {
        ctx.r3.u64 = 1;
        fprintf(stderr, "[music] track skip pressed between two of a frame's pad polls: taken\n");
    }
    else if (gamePressed)
    {
        fprintf(stderr, "[music] track skip pressed\n");
    }
    wasDown = down;

    // BURNOUT_LOG_PAD_BUTTONS=1: the bound pads' buttons down (pad+37), on change.
    static const bool logButtons = std::getenv("BURNOUT_LOG_PAD_BUTTONS") != nullptr;
    if (logButtons)
    {
        static std::string last;
        std::string now;
        for (uint32_t i = 0; i < 2; i++)
        {
            const int8_t port = int8_t(PPC_LOAD_U8(game + 2441 + i));
            if (!PPC_LOAD_U8(game + 2443 + i) || port < 0 || port >= 4)
                continue;
            const uint32_t pad = PPC_LOAD_U32(game + 1000 + uint32_t(port) * 360 + 4);
            if (pad < 0x10000)
                continue;
            now += "port " + std::to_string(port) + ":";
            for (uint32_t b = 0; b < 28; b++)
                if (PPC_LOAD_U8(pad + 37 + b))
                    now += " " + std::to_string(b);
            now += ";";
        }
        if (now != last)
            fprintf(stderr, "[music] pad buttons down: %s\n", now.c_str());
        last = now;
    }
}

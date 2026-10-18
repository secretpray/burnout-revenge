// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Skip Intro Movies (Settings > Advanced): the logos at launch (EA HD, EA,
// Criterion) and the attract film before the title screen end at once.
//
// sub_821FEBC0(screen, movie) starts a film: it writes the file's path at
// screen+164 and the screen's state 2 at +152; the screen then polls its
// player (screen+4) with sub_82357CC0, which reads the player's "finished"
// byte at +81, and leaves the film when it says so (state 29, 30, then 55:
// sub_821FE9C8, sub_821F91D8). Answering "finished" for that player only,
// from its third poll (it has opened the file by then), ends the film the
// way its own end does. Other films (menus' backgrounds, the locations',
// the credits) play. The idea and the two addresses are from shipa-2's
// Xerenge (its movie_trace.cpp); this code is ours.
#include <stdafx.h>
#include <user/settings.h>

#include "movies.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <string_view>

namespace
{
    std::atomic<uint32_t> s_skipPlayer{ 0 };  // the player of a film being skipped; 0: none
    std::atomic<uint32_t> s_polls{ 0 };       // its polls since it started
    std::atomic<uint32_t> s_introPlayer{ 0 };  // the player of a launch film playing (skipped or not); 0: none
    std::atomic<int64_t> s_introPolled{ 0 };   // when it was last polled (LastIntroMovieNs)

    bool IsIntro(std::string_view path)
    {
        // The USA disc's names (_N); EA's logos also in their _E variants.
        for (std::string_view name : { "EAHD_E_N.xmv", "EA_E_N.xmv", "CRRW_N.xmv", "CRRW_E_N.xmv", "ATTR_N.xmv" })
            if (path.size() >= name.size() && strncasecmp(path.data() + path.size() - name.size(), name.data(), name.size()) == 0)
                return true;
        return false;
    }
}

extern "C" PPC_FUNC(__imp__sub_821FEBC0);
PPC_FUNC(sub_821FEBC0)
{
    const uint32_t screen = ctx.r3.u32;
    __imp__sub_821FEBC0(ctx, base);
    const char* path = reinterpret_cast<const char*>(base + screen + 164);
    const std::string_view name(path, strnlen(path, 256));
    const bool skip = IsIntro(name) && settings::GetBool(settings::Id::SkipIntroMovies);
    s_polls.store(0, std::memory_order_relaxed);
    s_skipPlayer.store(skip ? screen + 4 : 0, std::memory_order_relaxed);
    s_introPlayer.store(IsIntro(name) ? screen + 4 : 0, std::memory_order_relaxed);
    fprintf(stderr, "[movie] %.*s%s\n", int(name.size()), name.data(), skip ? " (skipped: Skip Intro Movies)" : "");
}

extern "C" PPC_FUNC(__imp__sub_82357CC0);
PPC_FUNC(sub_82357CC0)
{
    const uint32_t player = ctx.r3.u32;
    __imp__sub_82357CC0(ctx, base);
    if (player != 0 && player == s_introPlayer.load(std::memory_order_relaxed) && ctx.r3.u32 == 0)
        s_introPolled.store(std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(),
            std::memory_order_relaxed);
    if (player != 0 && player == s_skipPlayer.load(std::memory_order_relaxed)
        && s_polls.fetch_add(1, std::memory_order_relaxed) >= 2)
        ctx.r3.u64 = 1;
}

int64_t game::LastIntroMovieNs()
{
    return s_introPolled.load(std::memory_order_relaxed);
}

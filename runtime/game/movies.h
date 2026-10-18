// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). See movies.cpp.
#pragma once
#include <cstdint>

namespace game
{
    // Any thread: when a launch film (the logos, the attract film) was last
    // seen playing (ns, steady clock; 0: none yet). The menus' background
    // films don't count: they play all the time.
    int64_t LastIntroMovieNs();
}

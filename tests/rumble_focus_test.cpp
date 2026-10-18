// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Regression: a guest vibration request arriving after focus loss must not
// restart the motors. Check both orders of the independent focus/app events.
#include <hid/rumble_focus.h>
#include <cassert>
#include <initializer_list>
#include <cstdio>

int main()
{
    using Gate = hid::RumbleFocus;
    using Change = Gate::Change;
    Gate g;
    assert(g.Active());
    assert(g.SetFocused(false) == Change::Stop);
    for (int frame = 0; frame < 600; ++frame)
    {
        uint16_t low = uint16_t(frame + 1), high = 65535;
        g.Filter(low, high);
        assert(low == 0 && high == 0);  // even new guest requests stay silent
    }
    assert(g.SetFocused(false) == Change::None);
    assert(g.SetFocused(true) == Change::Restore);
    uint16_t low = 1234, high = 4321;
    g.Filter(low, high);
    assert(low == 1234 && high == 4321);

    for (bool focusFirst : {false, true})
    {
        Gate activity;
        if (focusFirst)
        {
            assert(activity.SetFocused(false) == Change::Stop);
            assert(activity.SetSuspended(true) == Change::None);
        }
        else
        {
            assert(activity.SetSuspended(true) == Change::Stop);
            assert(activity.SetFocused(false) == Change::None);
        }
        assert(activity.SetFocused(true) == Change::None); // still suspended
        low = high = 65535;
        activity.Filter(low, high);
        assert(low == 0 && high == 0);
        assert(activity.SetSuspended(false) == Change::Restore);
        assert(activity.SetSuspended(false) == Change::None);
    }

    // Returning from suspension before focus arrives must stay silent too.
    g.SetFocused(false);
    g.SetSuspended(true);
    assert(g.SetSuspended(false) == Change::None);
    assert(!g.Active());
    assert(g.SetFocused(true) == Change::Restore);
    puts("rumble focus: background requests suppressed; resume ordering passed");
}

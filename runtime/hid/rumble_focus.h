// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdint>

namespace hid
{
    // Main-thread state. Window focus and app suspension are independent:
    // gaining focus must not resume output while the app is still suspended.
    // The guest may keep submitting vibration requests in either state.
    class RumbleFocus
    {
        bool focused = true;
        bool suspended = false;

    public:
        enum class Change { None, Stop, Restore };

        bool Active() const { return focused && !suspended; }

        Change SetFocused(bool value) { return Set(focused, value); }
        Change SetSuspended(bool value) { return Set(suspended, value); }

        void Filter(uint16_t& low, uint16_t& high) const
        {
            if (!Active())
                low = high = 0;
        }

    private:
        Change Set(bool& flag, bool value)
        {
            const bool before = Active();
            flag = value;
            return before == Active() ? Change::None : Active() ? Change::Restore : Change::Stop;
        }
    };
}

// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <functional>
#include <string>

namespace ui
{
    // Any thread. Completion runs on the main thread, after the modal closes.
    bool RequestGuestKeyboard(std::string title, std::string description,
        std::string initial, size_t maxUnits, std::function<void(bool, std::string)> complete);
    void TickGuestKeyboard();  // main thread
    void CancelGuestKeyboard(); // main thread, before shutdown
}

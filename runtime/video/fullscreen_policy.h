// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Settings > Display > Fullscreen and the window's own fullscreen state.
//
// The setting is the player's choice and the only thing that decides how the
// game starts. Changing it in the menu (or Reset page) also puts the window
// in that state now. The system can change the window too: the green button
// or Window > Enter Full Screen (Ctrl+Cmd+F) on a Mac, a window manager's
// fullscreen key on a Linux desktop, or a macOS fullscreen Space that fails
// to open at launch (SDL then reports the window leaving fullscreen). Those
// change the window for this session only and never the setting: until
// v0.1.3 every such change was written into the setting, so the next launch
// was fullscreen or not depending on how the window happened to be when the
// game last saved (a quit from a green-button fullscreen started fullscreen;
// a failed Space at launch turned the setting off).
//
// The menu shows the saved choice; when the window differs from it, the row
// says what the window is now ("NOW FULLSCREEN" / "NOW WINDOWED") and its
// help line which way the game starts (ui/settings_menu.cpp).
//
// Main thread. Header only (tests/fullscreen_policy_test.cpp).
#pragma once
#include <optional>

namespace video
{
    class FullscreenPolicy
    {
    public:
        // Who changed the window, for its log line.
        enum class Change
        {
            Requested,  // the game asked for it (launch, the setting): nothing to do
            System,     // the system did: the window only, the setting stays
        };

        // At launch: whether to create the window fullscreen. `setting` is
        // the effective value (BURNOUT_FULLSCREEN or settings.toml); `locked`:
        // always fullscreen (SteamOS Game Mode, iOS).
        bool Launch(bool setting, bool locked)
        {
            m_locked = locked;
            m_setting = setting;
            m_window = m_requested = locked || setting;
            return m_window;
        }

        // The setting's effective value now (checked whenever the settings
        // change: the menu, Reset page). When it changed, the window is to be
        // put in that state: the value to ask SDL for, unless the window is
        // there already or locked.
        std::optional<bool> SettingIs(bool setting)
        {
            if (m_locked || setting == m_setting)
                return std::nullopt;
            m_setting = setting;
            if (setting == m_window)
                return std::nullopt;
            m_window = m_requested = setting;
            return setting;
        }

        // SDL reported the window entering (true) or leaving fullscreen. The
        // setting is never touched: a change the game didn't ask for is the
        // system's, for this session only.
        Change WindowIs(bool fullscreen)
        {
            m_window = fullscreen;
            if (fullscreen == m_requested)
                return Change::Requested;
            m_requested = fullscreen;
            return Change::System;
        }

        bool Window() const { return m_window; }    // the window's state
        bool Setting() const { return m_setting; }  // the choice as last applied
        // The window isn't as the setting says (a system change this session).
        bool WindowDiffers() const { return !m_locked && m_window != m_setting; }

    private:
        bool m_locked = false;
        bool m_setting = false;
        bool m_window = false;
        bool m_requested = false;  // what the game last asked for (or the system last did)
    };
}

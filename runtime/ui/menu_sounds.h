// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The game's own front-end clicks for our menus (Settings, the first-run
// screens): short XMA sounds from the player's sound/fe.xwb (an XACT wave
// bank), read from the installed game or, during setup, from the checked
// disc image. Nothing of the game is shipped. Decoded once with the FFmpeg
// already linked for the game's audio; played on an SDL stream of their own,
// at the game's Volume and Mute. Main thread only.
#pragma once
#include <filesystem>

namespace install
{
    class DiscSource;
}

namespace ui::menu_sounds
{
    enum class Sound
    {
        Move,    // the cursor to another row or button
        Change,  // a value left or right
        Select,  // A / Enter / a click on an action
        Back,    // B / Escape: a menu or dialog closed
    };

    // The installed game's sound/fe.xwb (tried once, at the first Play, if
    // neither of these loaded the sounds first).
    void LoadFromGame(const std::filesystem::path& gameDir);
    void LoadFromDisc(const install::DiscSource& source);
    void Play(Sound sound);
    // Before SDL's audio shuts down.
    void Shutdown();
}

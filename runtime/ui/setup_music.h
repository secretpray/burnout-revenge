// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Music for the first-run screens: the game's own EA Trax songs, read from
// the player's disc image once it has been checked (eatrax/NN.wma, ASF/WMA
// v2), so nothing of the game is shipped. Decoded on a thread of its own
// into an SDL audio stream of its own, which is gone before the game's audio
// starts. Volume and Mute are the game's settings. Main thread only.
#pragma once
#include <string>

namespace install
{
    class DiscSource;
}

namespace ui::setup_music
{
    // Plays the disc's songs from a random one, fading in; replaces what
    // played. Nothing (logged) when the disc has none or audio is off.
    void Start(const install::DiscSource& source);
    // Every frame: settings, the player's on/off and fades.
    void Update();
    // Fades out over `seconds`; Update stops it at silence.
    void FadeOut(float seconds);
    // Stops now and waits for the decoder.
    void Stop();
    bool Playing();
    // The player's switch (M, or the button): kept until the screens close.
    void SetEnabled(bool enabled);
    bool Enabled();
    // "EA TRAX  /  TRACK 07", or empty while nothing plays.
    std::string NowPlaying();
}

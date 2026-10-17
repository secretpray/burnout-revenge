// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// When the mouse cursor shows over the game (ui.cpp, every frame). The game
// takes no mouse input, so while it plays the cursor hides once the mouse has
// been still for kHideAfterNs, and shows again the moment it moves, clicks or
// scrolls. The settings menu and the installer's screens (any modal page)
// take the mouse: while one is open it always shows. The cursor hides only
// over the game's window (SDL_HideCursor: on macOS the window's cursor
// rectangle, on X11 and Wayland the window's cursor), so in a window it still
// shows over the desktop, and nothing is captured or confined (no relative
// mode). Touch moves no cursor: the mouse SDL makes of a finger (and of a
// pen) doesn't count as movement, so a tap on the Steam Deck's screen
// doesn't bring a cursor back. Not used on iOS, where the system hides an
// iPad's pointer by itself.
#pragma once
#include <cstdint>

namespace ui
{
    struct CursorIdle
    {
        static constexpr uint64_t kHideAfterNs = 2'000'000'000;

        // When the mouse last moved (SDL's clock, ns). The first frame counts:
        // the cursor shows for kHideAfterNs after launch wherever it is.
        uint64_t movedNs = 0;
        bool started = false;

        // A mouse event (SDL's): `motion`, or a button or the wheel; whether
        // SDL made it of a finger or a pen (SDL_TOUCH_MOUSEID, SDL_PEN_MOUSEID);
        // a motion's distance. True: a real mouse moved, clicked or scrolled.
        static bool Activity(bool motion, bool touchOrPen, float dx, float dy)
        {
            return !touchOrPen && (!motion || dx != 0.0f || dy != 0.0f);
        }

        // A real mouse moved, clicked or scrolled at `t`.
        void Moved(uint64_t t)
        {
            if (!started || t > movedNs)
                movedNs = t;
            started = true;
        }

        // Whether the cursor shows at `now`: a menu or modal page has the
        // mouse, or it moved within kHideAfterNs.
        bool Shows(uint64_t now, bool menu)
        {
            if (!started)
            {
                movedNs = now;
                started = true;
            }
            return menu || now < movedNs + kHideAfterNs;
        }
    };
}

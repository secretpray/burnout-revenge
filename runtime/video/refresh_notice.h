// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING). From SpeedBreaker v0.1.7;
// here the Mac's words name System Settings, and only a Mac uses it.
//
// The one-time notice on a display whose refresh rate 60 fps can't divide
// evenly (the Steam Deck OLED at 90 Hz; 144 and 165 Hz monitors), and no
// variable refresh: frames there stay on screen for 1 or 2 (2 or 3)
// refreshes in turn, which looks like stutter, and only the player can
// change the rate. When it shows and what it says; the presenter feeds it
// (video/presenter.cpp), ui::ShowNotice draws it. Header only:
// tests/refresh_notice_test.cpp checks it.
//
// Shown when, all at once:
//   - the display mode's rate (SDL's) is no whole multiple of 60 Hz
//     (RefreshFit::MultipleOf 0), from 58.5 Hz up, and it hasn't changed for
//     kStableSec;
//   - the display is a fixed-refresh one at that rate, as far as can be told:
//     on SteamOS, Linux and Windows the present waits' completions fit a grid
//     at the mode's rate (RefreshFit::SetGridHz) for kStableSec (a display
//     with variable refresh shows each frame as it comes, which fits no such
//     grid; the vblank lock's own "the display follows the vblank" is no use
//     here: at such a rate it isn't at the display's multiple, and the
//     intro's 30 fps movies, 3 refreshes a frame at 90 Hz, fit its 60 Hz line
//     until it gives up on a fixed display too); on a Mac,
//     macOS says the screen's refresh is fixed (its present waits sit on no
//     grid: a ProMotion panel, or one with adaptive sync, never shows it);
//   - not on iOS, nor on arm64 Linux (the Steam Frame: a headset's
//     compositor sets the rate);
//   - the game has shown frames for kSessionSec (past the logos and the
//     first loads), they have come without a gap of kGapMs for kFlowingSec
//     (not a load), no movie has played for kMovieQuietSec (the intro, a
//     cutscene), and the controls have been idle for kIdleSec (a menu, a
//     pause, the title screen; not a race: its throttle is held), with no
//     other panel or the settings menu open;
//   - not shown for this rate before (settings.toml's [app] refresh_notices;
//     BURNOUT_REFRESH_NOTICE=0 never, =1 again every launch).
// Game Mode's words are for the Deck's own screen (Quick Access's Refresh
// Rate) unless a monitor or TV is connected (BURNOUT_REFRESH_NOTICE_SCREEN=
// builtin|external decides instead, for tests).
// While it shows the game pauses, as for the settings menu (the 360's
// Guide); A, B, Start, Esc or OK closes it, and the rate is recorded then.
#pragma once
#include "refresh_fit.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace video
{
    struct RefreshNotice
    {
        enum class Platform
        {
            GameMode,   // SteamOS Game Mode (gamescope)
            Desktop,    // desktop Linux, Windows
            Mac,
            Excluded,   // iOS, arm64 Linux (the Steam Frame)
        };
        struct Inputs
        {
            double modeHz = 0;          // SDL's display mode
            double modeStableSec = 0;   // since it last changed
            Platform platform = Platform::Desktop;
            bool externalScreen = false;  // Game Mode: a monitor or TV, not the Deck's own screen
            bool gridFixed = false;     // the grid check fits (not on a Mac)
            double gridStableSec = 0;   // ...for this long
            bool macFixed = false;      // macOS: the screen's refresh is fixed (min = max interval, not built in)
            bool shownBefore = false;   // this rate is in settings.toml's [app] refresh_notices
            int forced = -1;            // BURNOUT_REFRESH_NOTICE: -1 unset, 0 never, 1 always
            double sessionSec = 0;      // since the first game frame
            double flowingSec = 0;      // since the last gap of kGapMs between game frames
            double movieQuietSec = 1e9; // since the last movie frame (the XMV player's)
            double idleSec = 0;         // since the controls were last used
            bool uiBusy = false;        // a panel or the settings menu is open
        };
        enum class Verdict
        {
            NotApplicable,  // 60 Hz, a whole multiple, an unknown rate: nothing to say
            Never,          // this rate, here: not shown (reason())
            Wait,           // maybe later (reason())
            Show,
        };
        struct Decision
        {
            Verdict verdict = Verdict::NotApplicable;
            const char* reason = "";
        };

        static constexpr double kStableSec = 5.0;
        static constexpr double kSessionSec = 30.0;
        static constexpr double kFlowingSec = 5.0;
        static constexpr double kGapMs = 250.0;
        static constexpr double kIdleSec = 3.0;
        static constexpr double kMovieQuietSec = 2.0;

        // The rate as settings.toml records it: whole hertz.
        static int Key(double hz) { return int(std::lround(hz)); }

        static Decision Decide(const Inputs& in)
        {
            if (!(in.modeHz >= RefreshFit::kMinHz) || RefreshFit::MultipleOf(in.modeHz) != 0)
                return { Verdict::NotApplicable, "" };
            if (in.forced == 0)
                return { Verdict::Never, "BURNOUT_REFRESH_NOTICE=0" };
            if (in.platform == Platform::Excluded)
                return { Verdict::Never, "not on this device (iOS, or a headset's compositor)" };
            if (in.shownBefore && in.forced != 1)
                return { Verdict::Never, "dismissed before for this rate (settings.toml [app] refresh_notices)" };
            if (in.platform == Platform::Mac)
            {
                if (!in.macFixed)
                    return { Verdict::Never, "macOS: a variable refresh rate (ProMotion, adaptive sync) or the built-in screen" };
            }
            else if (!in.gridFixed)
                return { Verdict::Wait, "no fixed refresh grid measured yet (variable refresh shows none)" };
            if (in.modeStableSec < kStableSec || (in.platform != Platform::Mac && in.gridStableSec < kStableSec))
                return { Verdict::Wait, "the rate isn't settled yet" };
            if (in.sessionSec < kSessionSec)
                return { Verdict::Wait, "starting up" };
            if (in.flowingSec < kFlowingSec)
                return { Verdict::Wait, "loading" };
            if (in.movieQuietSec < kMovieQuietSec)
                return { Verdict::Wait, "a movie is playing" };
            if (in.uiBusy)
                return { Verdict::Wait, "another panel is open" };
            if (in.idleSec < kIdleSec)
                return { Verdict::Wait, "the controls are in use (playing)" };
            return { Verdict::Show, "" };
        }

        static constexpr const char* kTitle = "Screen refresh rate";

        // What it says.
        static std::string Text(const Inputs& in)
        {
            char head[160];
            std::snprintf(head, sizeof head, "This screen is running at %d Hz, which doesn't divide 60 fps evenly, so motion can stutter. ",
                Key(in.modeHz));
            if (in.platform == Platform::GameMode && !in.externalScreen)
                return std::string(head) + "For smooth play, set Refresh Rate to 60 Hz in Quick Access > Performance. Turn on "
                    "Use per-game profile there and Steam keeps it for this game.";
            if (in.platform == Platform::GameMode)
                return std::string(head) + "For smooth play, set the screen to 60 or 120 Hz in Steam's display settings, or turn "
                    "on variable refresh if it has it.";
            if (in.platform == Platform::Mac)
                return std::string(head) + "For smooth play, set it to 60 or 120 Hz in System Settings > Displays > Refresh Rate, "
                    "or choose Variable there if the screen offers it.";
            return std::string(head) + "For smooth play, set it to 60 or 120 Hz in your display settings, or turn on variable "
                "refresh (FreeSync, G-SYNC, Adaptive-Sync) if the screen has it.";
        }
    };
}

// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// Settings > Display > Fullscreen against the window's own state
// (video/fullscreen_policy.h): a change the system makes (the green button,
// Window > Enter Full Screen, a window manager, a macOS fullscreen Space that
// fails to open at launch) never changes the saved setting; the menu does,
// and puts the window in that state. The optional argument is
// runtime/video/presenter.cpp: its window events must not write the setting.
//
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/fullscreen_policy_test.cpp
#include <video/fullscreen_policy.h>

#include <cstdio>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>

using video::FullscreenPolicy;
using Change = FullscreenPolicy::Change;

namespace
{
    int s_failures = 0;

#define CHECK(cond, ...)                                              \
    do                                                                \
    {                                                                 \
        if (!(cond))                                                  \
        {                                                             \
            s_failures++;                                             \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                             \
            fprintf(stderr, "\n");                                    \
        }                                                             \
    } while (0)

    // One session as the presenter runs it: settings.toml's value (what a
    // save writes and the next launch reads), the policy, and SDL's window.
    struct Session
    {
        bool saved;  // video.fullscreen in settings.toml
        bool locked = false;
        FullscreenPolicy policy;
        bool sdlWindow = false;   // what SDL has the window at
        int requests = 0;         // SDL_SetWindowFullscreen calls
        int systemLines = 0;      // "[video] fullscreen ... by the system" lines

        explicit Session(bool fullscreen, bool lockedScreen = false) : saved(fullscreen), locked(lockedScreen)
        {
            sdlWindow = policy.Launch(saved, locked);  // SDL_CreateWindow(... SDL_WINDOW_FULLSCREEN)
            Event(sdlWindow);                           // SDL reports the window it made
        }
        // SDL_EVENT_WINDOW_ENTER/LEAVE_FULLSCREEN: the presenter's handler.
        void Event(bool fullscreen)
        {
            sdlWindow = fullscreen;
            if (policy.WindowIs(fullscreen) == Change::System)
                systemLines++;
        }
        // The system changes the window (the green button): SDL then reports it.
        void System(bool fullscreen) { Event(fullscreen); }
        // The menu's Fullscreen row (or Reset page): the setting changes, the
        // presenter's ApplySettings asks SDL, SDL reports.
        void Menu(bool fullscreen)
        {
            saved = fullscreen;
            if (std::optional<bool> want = policy.SettingIs(saved))
            {
                requests++;
                Event(*want);
            }
        }
        // The next launch reads what this one left in settings.toml.
        Session Next() const { return Session(saved, locked); }
    };

    // Start Fullscreen Off, take the green button, quit: the file still says
    // off and the next launch is windowed (v0.1.3 saved On here).
    void GreenButtonOn()
    {
        Session s(false);
        s.System(true);
        CHECK(!s.saved && s.policy.Window() && s.policy.WindowDiffers(), "the window is fullscreen for now, the setting stays Off");
        CHECK(s.systemLines == 1, "one line for the system's change: %d", s.systemLines);
        Session next = s.Next();
        CHECK(!next.sdlWindow && !next.policy.Window(), "the next launch is windowed");
    }

    // Start Fullscreen On, leave it with the green button, quit: the next
    // launch is fullscreen again.
    void GreenButtonOff()
    {
        Session s(true);
        CHECK(s.sdlWindow && s.systemLines == 0, "fullscreen from the launch, the game's own request");
        s.System(false);
        CHECK(s.saved && !s.policy.Window() && s.policy.WindowDiffers(), "windowed for now, the setting stays On");
        CHECK(s.Next().sdlWindow, "the next launch is fullscreen");
    }

    // A macOS fullscreen Space that fails to open at launch: SDL reports the
    // window leaving fullscreen (SDL_cocoawindow.m windowDidExitFullScreen
    // before windowDidFailToEnterFullScreen). It must not turn the setting off.
    void FailedSpaceAtLaunch()
    {
        Session s(true);
        s.Event(false);
        CHECK(s.saved && s.systemLines == 1, "the setting stays On; one line");
        CHECK(s.Next().sdlWindow, "the next launch tries fullscreen again");
    }

    // The menu saves, both ways, and moves the window.
    void MenuSaves()
    {
        Session s(false);
        s.Menu(true);
        CHECK(s.saved && s.sdlWindow && s.requests == 1 && s.systemLines == 0, "On: saved, the window goes fullscreen");
        CHECK(!s.policy.WindowDiffers(), "row and window agree");
        s.Event(true);  // SDL may report it again when the Space settles
        CHECK(s.systemLines == 0, "a repeat of the requested state is not the system's");
        s.Menu(false);
        CHECK(!s.saved && !s.sdlWindow && s.requests == 2 && s.systemLines == 0, "Off: saved, the window leaves fullscreen");
        CHECK(!s.Next().sdlWindow, "the next launch follows the menu");
    }

    // The menu while the system has the window the other way: the row shows
    // the saved choice; choosing the state the window is in already saves it
    // and moves nothing; choosing again moves the window.
    void MenuAfterSystemChange()
    {
        Session s(false);
        s.System(true);  // the green button
        s.Menu(true);
        CHECK(s.saved && s.sdlWindow && s.requests == 0 && !s.policy.WindowDiffers(), "On: saved; the window was fullscreen already");
        s.Menu(false);
        CHECK(!s.saved && !s.sdlWindow && s.requests == 1, "Off: the window leaves fullscreen");

        Session t(true);
        t.System(false);  // the green button out of fullscreen
        t.Menu(false);
        CHECK(!t.saved && !t.sdlWindow && t.requests == 0 && !t.policy.WindowDiffers(), "Off: saved; the window was windowed already");
        CHECK(!t.Next().sdlWindow, "the next launch is windowed");
    }

    // SteamOS Game Mode / iOS: always fullscreen, the setting kept as it is.
    void Locked()
    {
        Session s(false, true);
        CHECK(s.sdlWindow, "fullscreen while locked");
        s.Menu(true);
        s.Menu(false);
        CHECK(s.requests == 0 && s.sdlWindow && !s.policy.WindowDiffers(), "the menu doesn't move a locked window, and no note");
        s.System(false);
        CHECK(!s.policy.WindowDiffers(), "no note while locked");
    }

    // presenter.cpp: the window events go through the policy and never write
    // the setting (v0.1.3 did: settings::Set(settings::Id::Fullscreen, ...)).
    void Presenter(const char* path)
    {
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string src = ss.str();
        CHECK(!src.empty(), "can't read %s", path);
        CHECK(src.find("settings::Set(settings::Id::Fullscreen") == std::string::npos, "presenter.cpp writes the Fullscreen setting");
        const size_t handler = src.find("e.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN || e.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN");
        CHECK(handler != std::string::npos, "the window's fullscreen events are handled");
        if (handler != std::string::npos)
        {
            const std::string body = src.substr(handler, src.find("\n            }", handler) - handler);
            CHECK(body.find("s_fullscreen.WindowIs(") != std::string::npos && body.find("settings::Set") == std::string::npos,
                "the handler only tells the policy: %s", body.c_str());
        }
        CHECK(src.find("s_fullscreen.Launch(settings::GetBool(settings::Id::Fullscreen), FullscreenLocked())") != std::string::npos,
            "the window starts as the setting says");
        CHECK(src.find("s_fullscreen.SettingIs(settings::GetBool(settings::Id::Fullscreen))") != std::string::npos,
            "the setting moves the window");
    }
}

int main(int argc, char** argv)
{
    GreenButtonOn();
    GreenButtonOff();
    FailedSpaceAtLaunch();
    MenuSaves();
    MenuAfterSystemChange();
    Locked();
    if (argc > 1)
        Presenter(argv[1]);
    else
        printf("(presenter.cpp not given: its checks skipped)\n");
    if (s_failures)
    {
        fprintf(stderr, "%d failure(s)\n", s_failures);
        return 1;
    }
    printf("fullscreen_policy_test: all passed\n");
    return 0;
}

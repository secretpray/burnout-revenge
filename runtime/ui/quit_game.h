// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Settings > Quit Game (settings_menu.cpp): where it is offered, and the rules
// of its confirmation, apart from ImGui so tests/quit_game_test.cpp can run
// them. The button is the last of the menu's footer (Reset page, Close, Quit
// Game), on every page: the D-pad's (or the arrow keys') Down past the last
// row lands on the footer's first button (Reset page, or Close while the page
// has nothing to reset), and Right goes on to Quit Game. The mouse clicks it.
//   - Offered where an app may quit itself: macOS, Linux and SteamOS (x86-64
//     and arm64), Windows. Not on iPhone or iPad, where the player closes an
//     app from the app switcher and Apple expects apps not to quit
//     themselves: there the button and its prompt are compiled out
//     (BR_QUIT_GAME 0), as is the startup error page's Quit.
//   - Two steps: Quit Game asks "Quit BurnoutRevenge? Progress since your last
//     save will be lost." with Quit and Cancel, and only Quit quits. The
//     focus starts on Cancel, so the same button pressed again (A, Enter)
//     cancels, and a second click lands outside the prompt, which ignores
//     it. Quit takes a press only once the prompt has been up a frame: never
//     the one that opened it.
//   - B, Escape and Cancel cancel; so does the menu closing under it (F1,
//     Start or Back alone) or opening again.
//   - Quitting is an SDL_EVENT_QUIT (Cmd+Q's event), which the presenter
//     takes as it takes the window's close button, so the game leaves the one
//     way it always does (main.cpp: settings saved, the renderer, UI,
//     controllers and window shut down, the log flushed, exit code 0). Once
//     quitting, nothing in the prompt acts again.
#pragma once

#include <cstdint>
#include <unordered_set>

#if defined(__APPLE__)
#include <TargetConditionals.h>  // TARGET_OS_IPHONE: __APPLE__ alone means macOS or iOS
#endif

// iPhone and iPad (and Apple's other device systems): no Quit Game.
#if defined(__APPLE__) && TARGET_OS_IPHONE
#define BR_QUIT_GAME 0
#else
#define BR_QUIT_GAME 1
#endif

namespace ui::quit
{
    // Home/Guide/PS is an edge, never a held-button action. Remember even
    // ignored presses (background, another dialog) until their own release.
    class GuidePress
    {
    public:
        bool Button(uint32_t device, bool down, bool allowed)
        {
            if (!down) { m_held.erase(device); return false; }
            return m_held.insert(device).second && allowed;
        }
        void Removed(uint32_t device) { m_held.erase(device); }
        void Clear() { m_held.clear(); }
    private:
        std::unordered_set<uint32_t> m_held;
    };

    enum class System { Mac, IPhoneIPad, Linux, Windows, Other };

    constexpr System kSystem =
#if defined(__APPLE__) && TARGET_OS_IPHONE
        System::IPhoneIPad;
#elif defined(__APPLE__)
        System::Mac;
#elif defined(_WIN32)
        System::Windows;
#elif defined(__linux__)
        System::Linux;
#else
        System::Other;
#endif

    // Whether the app may quit itself there (Quit Game, the startup error
    // page's Quit).
    constexpr bool OfferedOn(System system)
    {
        return system != System::IPhoneIPad;
    }

    constexpr bool kOffered = BR_QUIT_GAME != 0;
    static_assert(kOffered == OfferedOn(kSystem), "BR_QUIT_GAME and kSystem disagree");

    enum class Button { Quit, Cancel };

    // What a step did: the menu logs it, and quits on Quit.
    enum class Outcome { None, Asking, Cancelled, Quit };

    class Prompt
    {
    public:
        bool Asking() const { return m_step == Step::Asking; }
        bool Quitting() const { return m_step == Step::Quitting; }
        // Where the focus starts when the prompt opens.
        static constexpr Button kDefaultFocus = Button::Cancel;

        // Quit Game was pressed: ask.
        Outcome Choose()
        {
            if (m_step != Step::Idle)
                return Outcome::None;
            m_step = Step::Asking;
            m_framesShown = 0;
            m_focusPending = true;
            return Outcome::Asking;
        }

        // Each frame the prompt is drawn, before its buttons.
        void Shown()
        {
            if (m_step == Step::Asking && m_framesShown < kFramesBeforeQuit)
                m_framesShown++;
        }

        // True once, on the prompt's first frame: focus kDefaultFocus.
        bool TakeFocus()
        {
            const bool focus = m_focusPending && m_step == Step::Asking;
            m_focusPending = false;
            return focus;
        }

        // One of the prompt's buttons was pressed.
        Outcome Press(Button button)
        {
            if (m_step != Step::Asking)
                return Outcome::None;
            if (button == Button::Cancel)
                return Cancel();
            if (m_framesShown < kFramesBeforeQuit)
                return Outcome::None;  // the prompt's first frame: not a press of Quit's
            m_step = Step::Quitting;
            return Outcome::Quit;
        }

        // B or Escape.
        Outcome Back()
        {
            return m_step == Step::Asking ? Cancel() : Outcome::None;
        }

        // The prompt went away unanswered (the menu closed, or opened again).
        Outcome Dismiss()
        {
            return m_step == Step::Asking ? Cancel() : Outcome::None;
        }

        // The quit couldn't be sent (SDL's event queue refused it): the
        // game carries on, and Quit Game can be chosen again.
        void QuitFailed()
        {
            if (m_step == Step::Quitting)
                m_step = Step::Idle;
        }

    private:
        enum class Step { Idle, Asking, Quitting };
        static constexpr int kFramesBeforeQuit = 2;  // the frame it opened, and one more

        Outcome Cancel()
        {
            m_step = Step::Idle;
            m_focusPending = false;
            return Outcome::Cancelled;
        }

        Step m_step = Step::Idle;
        int m_framesShown = 0;
        bool m_focusPending = false;
    };
}

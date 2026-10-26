// Tests for Settings > Quit Game (runtime/ui/quit_game.h): where it is
// offered, and its confirmation. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/quit_game_test.cpp -o build/quit_game_test
//   build/quit_game_test runtime
// With the runtime folder it also reads the code around it: the button and
// its prompt compiled only where BR_QUIT_GAME is on (not iPhone or iPad), the
// prompt's text, Quit before Cancel with the focus on Cancel, B and Escape
// cancelling, the menu closing cancelling, the quit sent as SDL_EVENT_QUIT
// (no exit of its own), the presenter's RunFrame ending on that event, and
// main()'s one way out after it (settings saved, renderer and window shut
// down, exit code 0).
#include <ui/quit_game.h>

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    using ui::quit::Button;
    using ui::quit::Outcome;
    using ui::quit::Prompt;
    using ui::quit::System;

    void Platforms()
    {
        CHECK(ui::quit::OfferedOn(System::Mac), "macOS offers Quit Game");
        CHECK(ui::quit::OfferedOn(System::Linux), "Linux and SteamOS (x86-64, the Frame's arm64) offer it");
        CHECK(ui::quit::OfferedOn(System::Windows), "Windows offers it");
        CHECK(ui::quit::OfferedOn(System::Other), "another desktop system offers it");
        CHECK(!ui::quit::OfferedOn(System::IPhoneIPad), "iPhone and iPad don't: the player closes apps there");
        // The system this test is built for (a Mac, Linux or Windows: the
        // ones the tests run on) is the one the header names.
#if defined(__APPLE__) && TARGET_OS_IPHONE
        CHECK(ui::quit::kSystem == System::IPhoneIPad && !ui::quit::kOffered && BR_QUIT_GAME == 0, "an iOS build: no Quit Game");
#elif defined(__APPLE__)
        CHECK(ui::quit::kSystem == System::Mac, "built for macOS: System::Mac");
#elif defined(_WIN32)
        CHECK(ui::quit::kSystem == System::Windows, "built for Windows: System::Windows");
#elif defined(__linux__)
        CHECK(ui::quit::kSystem == System::Linux, "built for Linux: System::Linux");
#endif
#if !(defined(__APPLE__) && TARGET_OS_IPHONE)
        CHECK(ui::quit::kOffered && BR_QUIT_GAME == 1, "offered on this desktop system");
#endif
        CHECK(ui::quit::kOffered == ui::quit::OfferedOn(ui::quit::kSystem), "the macro and the table agree");
    }

    void GuideButton()
    {
        using Guide = ui::quit::GuidePress;
        using A = Guide::Action;
        constexpr uint64_t second = 1000000000ull;
        Guide guide;
        auto allowed = [](uint32_t) { return true; };
        CHECK(guide.Button(1, true, true, 0) == A::None, "down waits: no settings flash before a hold");
        CHECK(guide.Tick(second, allowed) == 0, "a short hold has no action yet");
        CHECK(guide.Button(1, false, true, second) == A::Settings, "short release toggles settings");
        CHECK(guide.Button(1, false, true, second) == A::None, "duplicate release cannot toggle twice");

        guide.Button(1, true, true, 0);
        guide.Button(1, true, true, 2 * second);  // duplicate must not restart the clock
        CHECK(guide.Tick(3 * second - 1, allowed) == 0, "three-second boundary: not a nanosecond early");
        CHECK(guide.Tick(3 * second, allowed) == 1, "hold asks to quit at exactly three seconds");
        CHECK(guide.Tick(9 * second, allowed) == 0, "continued hold cannot repeat after Cancel");
        CHECK(guide.Button(1, false, true, 9 * second) == A::None, "long release never opens settings");

        guide.Button(1, true, true, 0);
        CHECK(guide.Button(1, false, true, 3 * second - 1) == A::Settings, "just under threshold is short");
        guide.Button(1, true, true, 0);
        CHECK(guide.Button(1, false, true, 3 * second) == A::Quit, "queued threshold release handles a stalled event pump");
        CHECK(guide.Tick(4 * second, allowed) == 0, "queued release plus tick still asks only once");

        guide.Button(1, true, true, 0);
        guide.Button(2, true, true, second);
        CHECK(guide.Tick(3 * second, allowed) == 1, "players have independent hold timers");
        CHECK(guide.Button(2, false, true, 3 * second) == A::Settings, "player 2 short release is independent");
        CHECK(guide.Button(1, false, true, 4 * second) == A::None, "player 2 release cannot rearm player 1");

        guide.Button(1, true, false, 0);
        CHECK(guide.Tick(4 * second, allowed) == 0, "background/unassigned/modal press cannot become eligible while held");
        CHECK(guide.Button(1, false, true, 4 * second) == A::None, "ignored press has no release action");
        guide.Button(1, true, true, 0);
        guide.Tick(second, [](uint32_t) { return false; });
        CHECK(guide.Tick(4 * second, allowed) == 0, "losing eligibility cancels a pending gesture");
        CHECK(guide.Button(1, false, true, 5 * second) == A::None, "regaining focus cannot turn old hold into a tap");

        guide.Button(1, true, true, 0);
        guide.Cancel();  // focus lost then regained within one event batch, or another modal
        guide.Button(1, true, true, second);
        CHECK(guide.Button(1, false, true, 2 * second) == A::None, "focus/modal cancellation survives duplicate downs");
        guide.Button(1, true, true, 3 * second);
        CHECK(guide.Button(1, false, true, 4 * second) == A::Settings, "fresh press after cancellation works");

        guide.Button(1, true, true, 0);
        guide.Removed(1);
        CHECK(guide.Tick(4 * second, allowed) == 0, "detach cancels the held pad");
        CHECK(guide.Button(1, false, true, 4 * second) == A::None, "detached pad's stray release is harmless");
        guide.Button(1, true, true, 0);
        guide.Clear();
        CHECK(guide.Tick(4 * second, allowed) == 0, "shutdown clears pending timers");

        Prompt p;
        p.Choose(); p.Shown(); p.Shown();
        CHECK(p.Press(Button::Cancel) == Outcome::Cancelled, "hold shortcut still defaults to Cancel");
    }

    void Steps()
    {
        // Nothing open: the prompt's buttons and B do nothing.
        {
            Prompt p;
            CHECK(!p.Asking() && !p.Quitting(), "starts closed");
            CHECK(p.Press(Button::Quit) == Outcome::None, "Quit without the prompt: nothing");
            CHECK(p.Press(Button::Cancel) == Outcome::None && p.Back() == Outcome::None && p.Dismiss() == Outcome::None,
                "Cancel, B, the menu closing without the prompt: nothing");
            CHECK(!p.TakeFocus(), "no focus to put anywhere");
        }
        // Quit Game asks, with the focus on Cancel, once.
        {
            Prompt p;
            CHECK(p.Choose() == Outcome::Asking && p.Asking(), "Quit Game opens the prompt");
            CHECK(Prompt::kDefaultFocus == Button::Cancel, "the focus starts on Cancel");
            p.Shown();
            CHECK(p.TakeFocus(), "the first frame puts the focus on Cancel");
            CHECK(!p.TakeFocus(), "...and only the first: the player's moves stay theirs");
            CHECK(p.Choose() == Outcome::None, "Quit Game again while asking: nothing new");
        }
        // Quit takes a press only once the prompt has been up a frame.
        {
            Prompt p;
            p.Choose();
            p.Shown();
            CHECK(p.Press(Button::Quit) == Outcome::None && p.Asking(), "Quit on the prompt's first frame: ignored, still asking");
            p.Shown();
            CHECK(p.Press(Button::Quit) == Outcome::Quit && p.Quitting(), "Quit a frame later: quits");
        }
        // Cancel, B / Escape, the menu closing: no quit, and asking again
        // starts over (the focus on Cancel again).
        for (int how = 0; how < 3; how++)
        {
            Prompt p;
            p.Choose();
            p.Shown();
            p.TakeFocus();
            p.Shown();
            Outcome o = how == 0 ? p.Press(Button::Cancel) : how == 1 ? p.Back() : p.Dismiss();
            CHECK(o == Outcome::Cancelled && !p.Asking() && !p.Quitting(), "cancel path %d: cancelled", how);
            CHECK(p.Press(Button::Quit) == Outcome::None, "cancel path %d: Quit after it does nothing", how);
            CHECK(p.Choose() == Outcome::Asking, "cancel path %d: Quit Game asks again", how);
            p.Shown();
            CHECK(p.TakeFocus(), "cancel path %d: the focus back on Cancel", how);
            CHECK(p.Press(Button::Quit) == Outcome::None, "cancel path %d: the new prompt's first frame ignores Quit too", how);
        }
        // Cancelled on its first frame (B at once): fine, no focus left pending.
        {
            Prompt p;
            p.Choose();
            CHECK(p.Back() == Outcome::Cancelled && !p.TakeFocus(), "B before the first frame: cancelled, no focus to take");
        }
        // Quitting is final: one SDL_EVENT_QUIT, whatever comes after.
        {
            Prompt p;
            p.Choose();
            p.Shown();
            p.Shown();
            CHECK(p.Press(Button::Quit) == Outcome::Quit, "quits");
            CHECK(p.Press(Button::Quit) == Outcome::None && p.Press(Button::Cancel) == Outcome::None, "no second quit, no cancel");
            CHECK(p.Back() == Outcome::None && p.Dismiss() == Outcome::None, "B or the menu closing (main() shutting the UI down) don't cancel it");
            CHECK(p.Choose() == Outcome::None && p.Quitting(), "Quit Game can't ask again while quitting");
            // SDL refused the event: start over.
            p.QuitFailed();
            CHECK(!p.Quitting() && !p.Asking() && p.Choose() == Outcome::Asking, "a quit that couldn't be sent: Quit Game works again");
        }
    }

    // The player, as ImGui gives the prompt their input: the footer's focus
    // on Quit Game (the hardest case: one press from the prompt), one input
    // a frame. A presses the focused button, B (or Escape) goes back, Left
    // and Right move between Quit (left) and Cancel (right).
    enum Input { kA, kB, kLeft, kRight, kInputs };
    const char* const kInputNames[] = { "A", "B", "Left", "Right" };

    // How many of the inputs it took to quit (0: they didn't).
    size_t QuitAfter(const std::vector<int>& inputs)
    {
        Prompt p;
        Button focus = Button::Cancel;
        bool menuOpen = true;
        for (size_t i = 0; i < inputs.size(); i++)
        {
            const int input = inputs[i];
            if (!menuOpen)
                return 0;
            if (!p.Asking())
            {
                // The footer: A presses Quit Game (the prompt draws in the
                // same frame); B closes the menu; Left and Right would move
                // off Quit Game (kept on it here: never easier to quit).
                if (input == kA && p.Choose() == Outcome::Asking)
                {
                    p.Shown();
                    if (p.TakeFocus())
                        focus = Prompt::kDefaultFocus;
                }
                else if (input == kB)
                    menuOpen = false;
                continue;
            }
            p.Shown();
            Outcome o = Outcome::None;
            if (input == kA)
                o = p.Press(focus);
            else if (input == kB)
                o = p.Back();
            else
                focus = input == kLeft ? Button::Quit : Button::Cancel;
            if (o == Outcome::Quit)
                return i + 1;
        }
        return 0;
    }

    bool Quits(const std::vector<int>& inputs)
    {
        return QuitAfter(inputs) > 0;
    }

    void NoAccidentalQuit()
    {
        CHECK(Quits({ kA, kLeft, kA }), "A, Left, A quits (Quit Game, to Quit, press)");
        CHECK(!Quits({ kA, kA }), "A, A: the second press is Cancel's");
        CHECK(!Quits({ kA, kB }) && !Quits({ kA, kLeft, kB }), "B cancels, from either button");
        CHECK(!Quits({ kA, kLeft, kRight, kA }), "back to Cancel, then A: cancelled");
        // Every input sequence up to 7 long.
        int sequences = 0, quitting = 0;
        for (int length = 1; length <= 7; length++)
        {
            int total = 1;
            for (int i = 0; i < length; i++)
                total *= kInputs;
            for (int code = 0; code < total; code++)
            {
                std::vector<int> seq;
                for (int i = 0, c = code; i < length; i++, c /= kInputs)
                    seq.push_back(c % kInputs);
                const size_t after = QuitAfter(seq);
                sequences++;
                quitting += after > 0;
                if (!after)
                    continue;
                seq.resize(after);  // the inputs up to the quit
                std::string how;
                for (int input : seq)
                    how += std::string(kInputNames[input]) + " ";
                bool oneButton = true;
                for (int input : seq)
                    oneButton &= input == seq[0];
                CHECK(!oneButton, "one button pressed over and over quit: %s", how.c_str());
                CHECK(seq.size() >= 3, "fewer than 3 presses quit: %s", how.c_str());
                // A quitting sequence ends with A, pressed with the focus moved
                // to Quit by a Left after the prompt opened.
                CHECK(seq.back() == kA, "a quit not on A: %s", how.c_str());
                size_t open = 0;
                while (open < seq.size() && seq[open] != kA)
                    open++;
                bool left = false;
                for (size_t i = open + 1; i + 1 < seq.size(); i++)
                    left |= seq[i] == kLeft;
                CHECK(left, "a quit without a Left in the prompt: %s", how.c_str());
            }
        }
        CHECK(quitting > 0 && quitting < sequences, "some sequences quit (%d of %d)", quitting, sequences);
    }

    // ---- The code around it (with the runtime folder's path) ----

    std::vector<std::string> Lines(const std::string& path)
    {
        std::ifstream in(path);
        std::vector<std::string> lines;
        for (std::string line; std::getline(in, line);)
            lines.push_back(line);
        return lines;
    }

    std::string Trim(const std::string& s)
    {
        size_t a = s.find_first_not_of(" \t");
        return a == std::string::npos ? std::string() : s.substr(a);
    }

    bool Has(const std::string& line, const char* text)
    {
        return line.find(text) != std::string::npos;
    }

    // For each line: inside the true branch of an `#if BR_QUIT_GAME`.
    std::vector<bool> QuitGated(const std::vector<std::string>& lines)
    {
        struct Level { bool quit; bool inElse; };
        std::vector<Level> stack;
        std::vector<bool> gated;
        for (const std::string& raw : lines)
        {
            std::string line = Trim(raw);
            if (line.rfind("#if", 0) == 0)
                stack.push_back({ Trim(line.substr(line.find_first_of(" \t") == std::string::npos ? line.size() : line.find_first_of(" \t"))) == "BR_QUIT_GAME", false });
            else if (line.rfind("#elif", 0) == 0 && !stack.empty())
                stack.back() = { false, true };
            else if (line.rfind("#else", 0) == 0 && !stack.empty())
                stack.back().inElse = true;
            else if (line.rfind("#endif", 0) == 0 && !stack.empty())
                stack.pop_back();
            bool in = false;
            for (const Level& l : stack)
                in |= l.quit && !l.inElse;
            gated.push_back(in);
        }
        return gated;
    }

    void SettingsMenu(const std::string& runtime)
    {
        const std::vector<std::string> lines = Lines(runtime + "/ui/settings_menu.cpp");
        CHECK(lines.size() > 100, "read ui/settings_menu.cpp (%zu lines)", lines.size());
        const std::vector<bool> gated = QuitGated(lines);
        int button = -1, text = -1, quitButton = -1, cancelButton = -1, focus = -1, defaultFocus = -1, push = -1, back = -1, close = -1;
        for (size_t i = 0; i < lines.size(); i++)
        {
            const std::string& l = lines[i];
            // Everything of Quit Game's is compiled only where it's offered.
            for (const char* name : { "QUIT GAME###Quit Game", "SDL_EVENT_QUIT", "SDL_PushEvent", "s_quit", "kQuitPopup", "Quit BurnoutRevenge" })
                if (Has(l, name))
                    CHECK(gated[i], "settings_menu.cpp:%zu: %s outside #if BR_QUIT_GAME: %s", i + 1, name, Trim(l).c_str());
            // No way out of its own.
            for (const char* name : { "exit(", "_Exit(", "abort(", "SDL_Quit(", "terminate(" })
                CHECK(!Has(l, name), "settings_menu.cpp:%zu: %s: %s", i + 1, name, Trim(l).c_str());
            if (Has(l, "ImGui::Button(locale::Text(\"QUIT GAME###Quit Game\"))"))
                button = int(i);
            if (Has(l, "\"Quit Burnout Revenge? Progress since your last save will be lost.\""))
                text = int(i);
            if (Has(l, "burnout::DialogOption(\"Quit\""))
                quitButton = int(i);
            if (Has(l, "burnout::DialogOption(\"Cancel\"") && quitButton >= 0 && cancelButton < 0 && int(i) > quitButton)
                cancelButton = int(i);
            if (Has(l, "s_quit.TakeFocus()"))
                focus = int(i);
            if (Has(l, "SetItemDefaultFocus") && cancelButton >= 0 && defaultFocus < 0)
                defaultFocus = int(i);
            if (Has(l, "SDL_PushEvent"))
                push = int(i);
            if (Has(l, "s_quit.Back()"))
                back = int(i);
            if (Has(l, "ImGui::Button(locale::Text(\"CLOSE###Close\"))"))
                close = int(i);
        }
        CHECK(button > 0, "the footer has a Quit Game button");
        CHECK(close > 0 && button > close, "Quit Game comes after Close (line %d, Close %d)", button + 1, close + 1);
        CHECK(button > 0 && Has(lines[button], "s_quit.Choose()"), "Quit Game only asks (s_quit.Choose())");
        CHECK(text > 0, "the prompt says \"Quit Burnout Revenge? Progress since your last save will be lost.\"");
        CHECK(quitButton > text && cancelButton > quitButton, "Quit, then Cancel to its right (lines %d, %d)", quitButton + 1, cancelButton + 1);
        CHECK(focus > quitButton && focus < cancelButton && Has(lines[focus + 1], "SetKeyboardFocusHere"),
            "the first frame's focus goes to the button after it: Cancel (line %d)", focus + 1);
        CHECK(defaultFocus == cancelButton + 1, "Cancel is the prompt's default focus (line %d)", defaultFocus + 1);
        CHECK(back > 0 && Has(lines[back - 1] + lines[back - 2], "ImGuiKey_GamepadFaceRight") && Has(lines[back - 1] + lines[back - 2], "ImGuiKey_Escape"),
            "B and Escape cancel the prompt (line %d)", back + 1);
        CHECK(push > 0 && (Has(lines[push - 1], "SDL_EVENT_QUIT") || Has(lines[push - 2], "SDL_EVENT_QUIT") || Has(lines[push - 3], "SDL_EVENT_QUIT")),
            "the quit is an SDL_EVENT_QUIT pushed for the main loop (line %d)", push + 1);
        // The menu closing under it cancels (ui.cpp's CloseSettings).
        bool forget = false;
        for (const std::string& l : Lines(runtime + "/ui/ui.cpp"))
            forget |= Has(l, "ForgetQuitPrompt();");
        CHECK(forget, "ui.cpp's CloseSettings forgets the prompt");
        // The startup error page's Quit follows the same rule.
        bool startup = false;
        for (const std::string& l : Lines(runtime + "/ui/startup_error.cpp"))
            startup |= Has(l, "kCanQuit = quit::kOffered");
        CHECK(startup, "startup_error.cpp's Quit uses quit::kOffered");
    }

    // The way out: RunFrame ends on SDL_EVENT_QUIT, and main() then shuts
    // down as for a window close.
    void WayOut(const std::string& runtime)
    {
        const std::vector<std::string> presenter = Lines(runtime + "/video/presenter.cpp");
        bool ends = false;
        for (size_t i = 0; i + 1 < presenter.size(); i++)
            if (Has(presenter[i], "e.type == SDL_EVENT_QUIT") && Has(presenter[i + 1], "return false;"))
                ends = true;
        CHECK(ends, "presenter.cpp's RunFrame returns false on SDL_EVENT_QUIT");

        const std::vector<std::string> main = Lines(runtime + "/main.cpp");
        int loop = -1;
        for (size_t i = 0; i < main.size(); i++)
            if (Has(main[i], "while (video::RunFrame())"))
                loop = int(i);
        CHECK(loop > 0, "main.cpp's game loop runs video::RunFrame");
        bool save = false, renderer = false, video = false, flush = false, exit0 = false, otherExit = false;
        for (size_t i = loop + 1; loop > 0 && i < main.size() && Trim(main[i]) != "}"; i++)
        {
            save |= Has(main[i], "settings::Save()");
            renderer |= Has(main[i], "gpu::renderer::Shutdown()");
            video |= Has(main[i], "video::Shutdown()");
            flush |= Has(main[i], "report::FlushLog()");
            exit0 |= Has(main[i], "std::_Exit(0)");
            otherExit |= (Has(main[i], "_Exit(") || Has(main[i], "exit(")) && !Has(main[i], "std::_Exit(0)");
        }
        CHECK(save && renderer && video && flush, "after the loop: settings saved, renderer and video shut down, log flushed");
        CHECK(exit0 && !otherExit, "exit code 0");
    }
}

int main(int argc, char** argv)
{
    Platforms();
    GuideButton();
    Steps();
    NoAccidentalQuit();
    if (argc > 1)
    {
        SettingsMenu(argv[1]);
        WayOut(argv[1]);
    }
    else
        printf("  (no runtime folder given: the code around it not checked)\n");
    if (g_failures)
    {
        printf("quit_game_test: %d FAILED\n", g_failures);
        return 1;
    }
    printf("quit_game_test: all passed\n");
    return 0;
}

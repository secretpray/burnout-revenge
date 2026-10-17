// Tests for ui::CursorIdle (runtime/ui/cursor_idle.h): when the mouse cursor
// shows over the game. From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/cursor_idle_test.cpp -o build/cursor_idle_test
//   build/cursor_idle_test runtime/ui/ui.cpp
// With ui.cpp's path it also checks how ui.cpp drives it: ImGui's backend
// kept off the cursor outside the menus (it shows it every frame it lays
// out: the performance overlay, a toast), SDL_HideCursor only from the
// policy, and no relative mode.
#include <ui/cursor_idle.h>

#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace
{
    constexpr uint64_t kS = 1'000'000'000;  // a second in ns
    constexpr uint64_t kStart = 5 * kS;     // SDL's clock at the first frame

    void Policy()
    {
        using ui::CursorIdle;
        CursorIdle c;
        // Launch: shown wherever the pointer is, for 2 s.
        CHECK(c.Shows(kStart, false), "the first frame: shown");
        CHECK(c.Shows(kStart + kS, false), "1 s in: shown");
        CHECK(!c.Shows(kStart + 2 * kS, false), "2 s without the mouse moving: hidden");
        CHECK(!c.Shows(kStart + 60 * kS, false), "a minute on: hidden");
        // The mouse moves: shown at once, for 2 s from its last movement.
        c.Moved(kStart + 60 * kS);
        CHECK(c.Shows(kStart + 60 * kS, false), "moved: shown");
        c.Moved(kStart + 61 * kS);
        CHECK(c.Shows(kStart + 62 * kS + kS / 2, false), "moving on: 2 s from the last movement");
        CHECK(!c.Shows(kStart + 63 * kS, false), "then hidden");
        // A menu or the installer has the mouse: shown, however still.
        CHECK(c.Shows(kStart + 70 * kS, true), "a menu open: shown");
        CHECK(c.Shows(kStart + 300 * kS, true), "a menu open for minutes: shown");
        // Closed by the controller (no mouse since): hidden at once.
        CHECK(!c.Shows(kStart + 301 * kS, false), "closed without the mouse: hidden");
        // Closed by a click: shown for 2 s from the click.
        c.Moved(kStart + 400 * kS);
        CHECK(c.Shows(kStart + 401 * kS, false) && !c.Shows(kStart + 402 * kS, false), "closed with a click: 2 s, then hidden");
        // An event stamped before a later one doesn't take the time back.
        c.Moved(kStart + 399 * kS);
        CHECK(c.Shows(kStart + 401 * kS + kS / 2, false), "an older timestamp doesn't move it back");
        // A movement before the first frame (SDL's clock from SDL_Init): counts.
        CursorIdle early;
        early.Moved(kStart - kS);
        CHECK(early.Shows(kStart, false) && !early.Shows(kStart + kS, false), "moved before the first frame: 2 s from then");

        // What counts as the mouse.
        CHECK(CursorIdle::Activity(true, false, 3.0f, 0.0f), "a real mouse's motion");
        CHECK(CursorIdle::Activity(true, false, 0.0f, -0.5f), "a sub-point motion");
        CHECK(!CursorIdle::Activity(true, false, 0.0f, 0.0f), "a motion that moved nothing (a window entered)");
        CHECK(CursorIdle::Activity(false, false, 0.0f, 0.0f), "a click or the wheel");
        CHECK(!CursorIdle::Activity(true, true, 25.0f, 4.0f), "a finger's (or a pen's) motion: not the mouse (the Deck's screen)");
        CHECK(!CursorIdle::Activity(false, true, 0.0f, 0.0f), "a tap: not the mouse");
    }

    size_t Count(const std::string& text, const std::string& what)
    {
        size_t n = 0;
        for (size_t at = text.find(what); at != std::string::npos; at = text.find(what, at + what.size()))
            n++;
        return n;
    }

    void Source(const char* path)
    {
        std::ifstream in(path);
        if (!in)
        {
            printf("  %s: can't read\n", path);
            g_failures++;
            return;
        }
        std::stringstream ss;
        ss << in.rdbuf();
        const std::string ui = ss.str();
        CHECK(Count(ui, "io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange") == 1 &&
                Count(ui, "io.ConfigFlags &= ~ImGuiConfigFlags_NoMouseCursorChange") == 1,
            "ui.cpp keeps ImGui's backend off the cursor outside the menus, and gives it back in them");
        CHECK(Count(ui, "SDL_HideCursor()") == 1 && Count(ui, "SDL_ShowCursor()") == 1, "ui.cpp shows and hides the cursor in one place");
        CHECK(Count(ui, "RelativeMouseMode") == 0, "no relative mode (it would capture the mouse)");
        CHECK(Count(ui, "s_cursorIdle.Moved(") == 1 && Count(ui, "        if (MouseActivity(e))\n") == 1, "every SDL event is looked at for the mouse");
        printf("  %s: checked\n", path);
    }
}

int main(int argc, char** argv)
{
    printf("when the cursor shows:\n");
    Policy();
    if (argc > 1)
    {
        printf("ui.cpp:\n");
        Source(argv[1]);
    }
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}

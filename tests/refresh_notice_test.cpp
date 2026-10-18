// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// The one-time refresh-rate notice (video/refresh_notice.h): when it shows (the
// display rates, variable refresh, Game Mode, a Mac's ProMotion or fixed
// external screen, a headset, already dismissed, BURNOUT_REFRESH_NOTICE, and the
// moment: settled rate, past start-up, not loading, the controls idle, no other
// panel, no movie); what it says on each platform; the grid check it rests on
// (RefreshFit::SetGridHz: a fixed 90/143.9/144/165 Hz display taken, variable
// refresh never); and the dismissal in settings.toml ([app] refresh_notices,
// user/settings.cpp as the game links it), in a scratch folder (the argument).
//
// Build by hand (from the repo root):
//   c++ -std=c++20 -O2 -UNDEBUG -I runtime tests/refresh_notice_test.cpp runtime/video/refresh_fit.cpp \
//     runtime/user/settings.cpp -o build/refresh_notice_test
//   build/refresh_notice_test build/refresh_notice_scratch
#include <user/settings.h>
#include <video/refresh_fit.h>
#include <video/refresh_notice.h>

#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>

namespace
{
    int s_failures = 0;

#define CHECK(cond, ...)                                                    \
    do                                                                      \
    {                                                                       \
        if (!(cond))                                                        \
        {                                                                   \
            s_failures++;                                                   \
            fprintf(stderr, "FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); \
            fprintf(stderr, __VA_ARGS__);                                   \
            fprintf(stderr, "\n");                                          \
        }                                                                   \
    } while (0)

    using video::RefreshNotice;
    using Platform = RefreshNotice::Platform;
    using Verdict = RefreshNotice::Verdict;

    const char* Name(Verdict v)
    {
        switch (v)
        {
        case Verdict::NotApplicable: return "n/a";
        case Verdict::Never: return "never";
        case Verdict::Wait: return "wait";
        case Verdict::Show: return "show";
        }
        return "?";
    }

    // Everything settled and fixed: a display it should show on.
    RefreshNotice::Inputs Ready(double hz, Platform platform)
    {
        RefreshNotice::Inputs in;
        in.modeHz = hz;
        in.modeStableSec = 60;
        in.platform = platform;
        in.gridFixed = platform != Platform::Mac;
        in.gridStableSec = in.gridFixed ? 40 : 0;
        in.macFixed = platform == Platform::Mac;
        in.sessionSec = 90;
        in.flowingSec = 20;
        in.idleSec = 10;
        return in;
    }

    std::string Read(const std::filesystem::path& path)
    {
        std::ifstream in(path, std::ios::binary);
        std::stringstream text;
        text << in.rdbuf();
        return text.str();
    }

    void WhenItShows()
    {
        printf("when it shows:\n");
        struct Row
        {
            const char* name;
            RefreshNotice::Inputs in;
            Verdict want;
        };
        auto with = [](RefreshNotice::Inputs in, auto change) { change(in); return in; };
        const Row rows[] = {
            // The rates.
            { "Steam Deck OLED, 90 Hz, Game Mode", Ready(90, Platform::GameMode), Verdict::Show },
            { "144 Hz desktop", Ready(144, Platform::Desktop), Verdict::Show },
            { "143.9 Hz desktop", Ready(143.9, Platform::Desktop), Verdict::Show },
            { "165 Hz desktop", Ready(165, Platform::Desktop), Verdict::Show },
            { "100 Hz desktop", Ready(100, Platform::Desktop), Verdict::Show },
            { "75 Hz desktop", Ready(75, Platform::Desktop), Verdict::Show },
            { "59.94 Hz: even", Ready(59.94, Platform::GameMode), Verdict::NotApplicable },
            { "60 Hz: even", Ready(60, Platform::Desktop), Verdict::NotApplicable },
            { "119.88 Hz: a multiple (locked)", Ready(119.88, Platform::Desktop), Verdict::NotApplicable },
            { "180 Hz: a multiple", Ready(180, Platform::Desktop), Verdict::NotApplicable },
            { "240 Hz: a multiple", Ready(240, Platform::GameMode), Verdict::NotApplicable },
            { "50 Hz: below 58.5", Ready(50, Platform::Desktop), Verdict::NotApplicable },
            { "unknown rate (0)", Ready(0, Platform::Desktop), Verdict::NotApplicable },
            // Variable refresh: no grid measured, or the lock let go.
            { "144 Hz, no fixed grid (variable refresh)", with(Ready(144, Platform::Desktop), [](auto& i) { i.gridFixed = false; i.gridStableSec = 0; }), Verdict::Wait },
            { "90 Hz Game Mode, no grid", with(Ready(90, Platform::GameMode), [](auto& i) { i.gridFixed = false; i.gridStableSec = 0; }), Verdict::Wait },
            // Macs: ProMotion (or adaptive sync) never; a fixed external screen yes.
            { "Mac ProMotion, 120 Hz: a multiple anyway", Ready(120, Platform::Mac), Verdict::NotApplicable },
            { "Mac ProMotion at 90 Hz (variable)", with(Ready(90, Platform::Mac), [](auto& i) { i.macFixed = false; }), Verdict::Never },
            { "Mac external 144 Hz, adaptive sync on", with(Ready(144, Platform::Mac), [](auto& i) { i.macFixed = false; }), Verdict::Never },
            { "Mac external 144 Hz, fixed", Ready(144, Platform::Mac), Verdict::Show },
            { "Mac external 144 Hz, fixed, no grid (Metal's waits don't sit on one)", with(Ready(144, Platform::Mac), [](auto& i) { i.gridFixed = false; }), Verdict::Show },
            // Not here at all.
            { "iOS or a headset (excluded)", Ready(90, Platform::Excluded), Verdict::Never },
            // Dismissed, and the override.
            { "90 Hz, dismissed before", with(Ready(90, Platform::GameMode), [](auto& i) { i.shownBefore = true; }), Verdict::Never },
            { "90 Hz, BURNOUT_REFRESH_NOTICE=0", with(Ready(90, Platform::GameMode), [](auto& i) { i.forced = 0; }), Verdict::Never },
            { "90 Hz, dismissed, BURNOUT_REFRESH_NOTICE=1", with(Ready(90, Platform::GameMode), [](auto& i) { i.shownBefore = true; i.forced = 1; }), Verdict::Show },
            // The moment.
            { "the mode just changed", with(Ready(144, Platform::Desktop), [](auto& i) { i.modeStableSec = 1; }), Verdict::Wait },
            { "the grid just fitted", with(Ready(144, Platform::Desktop), [](auto& i) { i.gridStableSec = 2; }), Verdict::Wait },
            { "starting up (20 s in)", with(Ready(90, Platform::GameMode), [](auto& i) { i.sessionSec = 20; }), Verdict::Wait },
            { "loading (frames back 1 s ago)", with(Ready(90, Platform::GameMode), [](auto& i) { i.flowingSec = 1; }), Verdict::Wait },
            { "racing (the throttle held)", with(Ready(90, Platform::GameMode), [](auto& i) { i.idleSec = 0; }), Verdict::Wait },
            { "the controls used 2 s ago", with(Ready(90, Platform::GameMode), [](auto& i) { i.idleSec = 2; }), Verdict::Wait },
            { "a movie playing (the intro, a cutscene)", with(Ready(90, Platform::GameMode), [](auto& i) { i.movieQuietSec = 0.1; }), Verdict::Wait },
            { "a movie ended 3 s ago", with(Ready(90, Platform::GameMode), [](auto& i) { i.movieQuietSec = 3; }), Verdict::Show },
            { "the settings menu or another panel is open", with(Ready(90, Platform::GameMode), [](auto& i) { i.uiBusy = true; }), Verdict::Wait },
            { "Mac: the mode just changed", with(Ready(144, Platform::Mac), [](auto& i) { i.modeStableSec = 1; }), Verdict::Wait },
        };
        int wrong = 0;
        for (const Row& r : rows)
        {
            const RefreshNotice::Decision d = RefreshNotice::Decide(r.in);
            const bool ok = d.verdict == r.want;
            wrong += !ok;
            printf("  %-62s %-6s %s%s\n", r.name, Name(d.verdict), d.reason, ok ? "" : "   <- WRONG");
            CHECK(ok, "%s: %s, want %s", r.name, Name(d.verdict), Name(r.want));
        }
        printf("  %zu cases, %d wrong\n", std::size(rows), wrong);
    }

    void WhatItSays()
    {
        printf("what it says:\n");
        const std::string deck = RefreshNotice::Text(Ready(90, Platform::GameMode));
        RefreshNotice::Inputs docked = Ready(144, Platform::GameMode);
        docked.externalScreen = true;
        const std::string tv = RefreshNotice::Text(docked);
        const std::string desktop = RefreshNotice::Text(Ready(143.9, Platform::Desktop));
        const std::string mac = RefreshNotice::Text(Ready(165, Platform::Mac));
        printf("  Game Mode, the Deck's screen: %s\n  Game Mode, a monitor: %s\n  desktop: %s\n  Mac: %s\n", deck.c_str(), tv.c_str(),
            desktop.c_str(), mac.c_str());
        CHECK(deck.find("90 Hz") != std::string::npos && deck.find("Quick Access > Performance") != std::string::npos &&
                  deck.find("60 Hz") != std::string::npos && deck.find("per-game profile") != std::string::npos, "%s", deck.c_str());
        CHECK(tv.find("144 Hz") != std::string::npos && tv.find("Quick Access") == std::string::npos &&
                  tv.find("60 or 120 Hz") != std::string::npos, "%s", tv.c_str());
        CHECK(desktop.find("144 Hz") != std::string::npos && desktop.find("display settings") != std::string::npos &&
                  desktop.find("variable refresh") != std::string::npos, "%s", desktop.c_str());
        CHECK(mac.find("165 Hz") != std::string::npos && mac.find("System Settings > Displays") != std::string::npos, "Mac: %s", mac.c_str());
        CHECK(RefreshNotice::Key(143.9) == 144 && RefreshNotice::Key(89.6) == 90 && RefreshNotice::Key(165.0) == 165, "Key");
    }

    // The grid check under it: completions of a 60 fps game on a fixed
    // display (each frame on the first refresh after it is ready, 1-2 ms
    // jitter in readiness, 0.2 ms in the waits), against a variable-refresh
    // display that shows each frame as it comes.
    void GridCheck()
    {
        printf("the grid check (RefreshFit::SetGridHz):\n");
        std::mt19937_64 rng(77);
        auto gauss = [&](double sd) { return std::normal_distribution<double>(0.0, sd)(rng); };
        for (double hz : { 90.0, 143.9, 144.0, 165.0, 100.0 })
            for (bool variable : { false, true })
            {
                video::RefreshFit grid;
                grid.SetGridHz(hz);
                const double p = 1e9 / hz;
                int64_t last = INT64_MIN;
                double validAt = -1;
                for (int i = 0; i < 60 * 30; i++)
                {
                    const double ready = double(i) * 1e9 / 60.0 + 3e6 + gauss(variable ? 0.3e6 : 1.0e6);
                    double on;
                    if (variable)
                        on = ready + 0.5e6;
                    else
                    {
                        int64_t k = std::max(int64_t(std::ceil(ready / p)), last + 1);
                        last = k;
                        on = double(k) * p;
                    }
                    grid.Add(int64_t(1e12 + on + 150'000 + gauss(200'000)));
                    if (grid.Estimate().valid && validAt < 0)
                        validAt = double(i) / 60.0;
                }
                const auto& e = grid.Estimate();
                printf("  %6.1f Hz %-9s: valid %s (after %.1f s), %.3f Hz, rms %.0f us, multiple %d\n", hz, variable ? "variable" : "fixed",
                    e.valid ? "yes" : "no", validAt, e.periodNs > 0 ? 1e9 / e.periodNs : 0.0, e.rmsNs / 1e3, e.multiple);
                if (variable)
                    CHECK(!e.valid, "%.1f Hz variable refresh taken for a grid", hz);
                else
                    CHECK(e.valid && std::abs(1e9 / e.periodNs - hz) < 2e-3 && validAt > 0 && validAt < 10.5 && e.multiple == 0,
                        "%.1f Hz fixed: valid %d %.4f Hz after %.1f s", hz, e.valid, 1e9 / e.periodNs, validAt);
            }
        // Planted: a grid check told the wrong rate never takes the display.
        for (auto [told, real] : { std::pair{ 144.0, 165.0 }, std::pair{ 90.0, 144.0 }, std::pair{ 165.0, 144.0 } })
        {
            video::RefreshFit grid;
            grid.SetGridHz(told);
            int64_t last = INT64_MIN;
            bool ever = false;
            for (int i = 0; i < 60 * 30; i++)
            {
                const double p = 1e9 / real;
                int64_t k = std::max(int64_t(std::ceil((double(i) * 1e9 / 60.0 + 3e6 + gauss(1e6)) / p)), last + 1);
                last = k;
                grid.Add(int64_t(1e12 + double(k) * p + 150'000 + gauss(200'000)));
                ever |= grid.Estimate().valid;
            }
            printf("  planted: told %.0f Hz on a %.0f Hz display: %s\n", told, real, ever ? "TAKEN" : "never taken (caught)");
            CHECK(!ever, "told %.0f on %.0f", told, real);
        }
    }

    void Dismissal(const std::filesystem::path& scratch)
    {
        printf("the dismissal in settings.toml:\n");
        std::error_code ec;
        std::filesystem::remove_all(scratch, ec);
        std::filesystem::create_directories(scratch, ec);
        const std::filesystem::path file = scratch / "settings.toml";
        std::ofstream(file) << "[app]\nlast_version = \"0.1.6\"\n";
        CHECK(settings::Load(file), "load");
        CHECK(!settings::RefreshNoticeShown(90), "90 shown before anything");
        CHECK(settings::RecordRefreshNotice(90), "record 90");
        CHECK(settings::RefreshNoticeShown(90) && !settings::RefreshNoticeShown(144), "after 90");
        std::string text = Read(file);
        CHECK(text.find("refresh_notices = \"90\"") != std::string::npos && text.find("last_version = \"0.1.6\"") != std::string::npos,
            "file after 90:\n%s", text.c_str());
        CHECK(settings::RecordRefreshNotice(144) && settings::RecordRefreshNotice(90), "record 144, 90 again");
        text = Read(file);
        CHECK(text.find("refresh_notices = \"90,144\"") != std::string::npos, "file after 144:\n%s", text.c_str());
        // Read back in a new load; a version record and a full save keep it.
        CHECK(settings::Load(file) && settings::RefreshNoticeShown(90) && settings::RefreshNoticeShown(144) && !settings::RefreshNoticeShown(165),
            "reload");
        CHECK(settings::RecordVersion("0.1.7") && settings::Save(), "version + save");
        text = Read(file);
        CHECK(text.find("refresh_notices = \"90,144\"") != std::string::npos && text.find("last_version = \"0.1.7\"") != std::string::npos,
            "after a save:\n%s", text.c_str());
        // A hand-edited list: junk and repeats dropped.
        std::ofstream(file) << "[app]\nrefresh_notices = \"90, x,144,90,-3\"\n";
        CHECK(settings::Load(file) && settings::RefreshNoticeShown(90) && settings::RefreshNoticeShown(144), "hand edit");
        // No list: the [app] table as before (only the version).
        std::ofstream(file) << "[app]\nlast_version = \"0.1.6\"\n";
        CHECK(settings::Load(file) && settings::RecordVersion("0.1.7"), "plain");
        text = Read(file);
        CHECK(text.find("refresh_notices") == std::string::npos, "a list appeared:\n%s", text.c_str());
        printf("  recorded, read back, kept by a save and a version record, junk ignored: %s\n", s_failures ? "see failures" : "ok");
        std::filesystem::remove_all(scratch, ec);
    }
}

int main(int argc, char** argv)
{
    const std::filesystem::path scratch = argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path() / "refresh_notice_test";
    WhenItShows();
    WhatItSays();
    GridCheck();
    Dismissal(scratch);
    printf(s_failures ? "\n%d FAILED\n" : "\nall passed\n", s_failures);
    return s_failures ? 1 : 0;
}

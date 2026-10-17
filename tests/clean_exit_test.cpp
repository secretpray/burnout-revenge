// SpeedBreaker tests. GPL-3.0-or-later (see COPYING).
//
// A requested termination quits the game cleanly, and only a session that
// didn't end cleanly leaves its crash marker (platform/quit_signals.h,
// report/crash_marker.h). v0.1.5 left the empty marker after every session
// (a Steam Frame session closed from Steam left one, as if it had crashed),
// and SIGHUP ended it at once, saving nothing.
//   - Signals, each case in a child process: SIGTERM, SIGINT and SIGHUP
//     become a quit request and the process lives on to quit; the first
//     request is the one kept; a signal ignored at launch (nohup) stays
//     ignored; SDL's own handler would leave them to us (it takes a signal
//     only while its action is the default); SIGKILL still kills. Controls:
//     without the handlers SIGHUP kills (v0.1.5).
//   - The marker: made empty, removed by a clean end; what the next launch
//     finds after a clean end, a SIGKILL, a crash report it couldn't rename,
//     and beside the current session's own. Then main()'s order end to end in
//     child processes: SIGTERM -> the quit -> no marker; SIGKILL -> marker.
//   - The optional argument is the runtime folder: main.cpp, presenter.cpp
//     and crash.cpp are read to check the handlers come before SDL, the frame
//     loop leaves on a request, every clean end clears the marker (last, just
//     before the exit) and a crash being reported keeps it; then planted
//     mutations of them that must fail.
//
// Build by hand: c++ -std=c++20 -O2 -I runtime tests/clean_exit_test.cpp -o build/clean_exit_test
//                build/clean_exit_test [<scratch folder>] [runtime]
#include <platform/quit_signals.h>
#include <report/crash_marker.h>

#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

static int g_failures = 0;
#define CHECK(cond, ...) do { if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;

namespace
{
    // `body` in a child process: its exit code (0: passed), or 128 + the
    // signal that killed it.
    int InChild(const std::function<int()>& body)
    {
        fflush(stdout);
        const pid_t pid = fork();
        if (pid == 0)
            _exit(body());
        int status = 0;
        waitpid(pid, &status, 0);
        return WIFEXITED(status) ? WEXITSTATUS(status) : WIFSIGNALED(status) ? 128 + WTERMSIG(status) : 255;
    }

    bool WaitFor(const std::function<bool()>& done, int ms)
    {
        for (int i = 0; i < ms; i++)
        {
            if (done())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return done();
    }

    void Signals()
    {
        printf("signals:\n");
        for (int sig : quitsignals::kSignals)
        {
            const int rc = InChild([sig] {
                quitsignals::Install();
                raise(sig);
                return quitsignals::Requested() == sig ? 0 : 1;
            });
            CHECK(rc == 0, "%s: a quit request, and the game lives on to quit (child: %d)", quitsignals::Name(sig), rc);
        }
        // From another process (as Steam, systemd or kill send it), while
        // the game runs.
        {
            fflush(stdout);
            const pid_t pid = fork();
            if (pid == 0)
            {
                quitsignals::Install();
                const bool seen = WaitFor([] { return quitsignals::Requested() != 0; }, 5000);
                _exit(seen && quitsignals::Requested() == SIGTERM ? 0 : 1);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            kill(pid, SIGTERM);
            int status = 0;
            waitpid(pid, &status, 0);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "SIGTERM from another process: seen, the child quit by itself");
        }
        {
            const int rc = InChild([] {
                quitsignals::Install();
                raise(SIGTERM);
                raise(SIGHUP);
                raise(SIGINT);
                return quitsignals::Requested() == SIGTERM ? 0 : 1;
            });
            CHECK(rc == 0, "the first request is kept; later ones change nothing (child: %d)", rc);
        }
        {
            const int rc = InChild([] {
                signal(SIGHUP, SIG_IGN);  // nohup
                quitsignals::Install();
                raise(SIGHUP);
                struct sigaction now{};
                sigaction(SIGHUP, nullptr, &now);
                return quitsignals::Requested() == 0 && now.sa_handler == SIG_IGN ? 0 : 1;
            });
            CHECK(rc == 0, "SIGHUP ignored at launch (nohup) stays ignored (child: %d)", rc);
        }
        {
            // SDL_EventSignal_Init (SDL 3.4.16, events/SDL_quit.c) puts its
            // handler only where the action is the default.
            const int rc = InChild([] {
                quitsignals::Install();
                for (int sig : quitsignals::kSignals)
                {
                    struct sigaction action{};
                    sigaction(sig, nullptr, &action);
                    if (action.sa_handler == SIG_DFL && reinterpret_cast<void*>(action.sa_sigaction) == reinterpret_cast<void*>(SIG_DFL))
                        return 1;  // SDL would take it
                }
                return 0;
            });
            CHECK(rc == 0, "SDL leaves the three signals to the game's handler (child: %d)", rc);
        }
        {
            const int rc = InChild([] {
                quitsignals::Install();
                raise(SIGKILL);
                return 0;
            });
            CHECK(rc == 128 + SIGKILL, "SIGKILL still kills (child: %d)", rc);
        }
        {
            const int rc = InChild([] {
                raise(SIGHUP);
                return 0;
            });
            CHECK(rc == 128 + SIGHUP, "control: without the handlers SIGHUP kills, as in v0.1.5 (child: %d)", rc);
        }
    }

    bool Exists(const fs::path& p)
    {
        std::error_code ec;
        return fs::exists(p, ec);
    }

    const report::marker::Leftover* FindSession(const std::vector<report::marker::Leftover>& found, const std::string& session)
    {
        for (const auto& l : found)
            if (l.session == session)
                return &l;
        return nullptr;
    }

    void Marker(const fs::path& scratch)
    {
        printf("the crash marker:\n");
        namespace marker = report::marker;
        std::error_code ec;
        fs::remove_all(scratch, ec);
        fs::create_directories(scratch, ec);
        CHECK(!ec, "can't make %s", scratch.c_str());

        const std::string clean = "2026-10-15_11-00-00", killed = "2026-10-15_11-10-00", crashed = "2026-10-15_11-20-00",
                          current = "2026-10-15_11-30-00";
        auto arm = [&](const std::string& session) {
            const std::string path = marker::PathFor(scratch, session).string();
            return std::pair{ marker::Arm(path.c_str()), path };
        };
        {
            auto [fd, path] = arm(clean);
            CHECK(fd >= 0 && Exists(path) && fs::file_size(path, ec) == 0, "armed: an empty %s", path.c_str());
            marker::Clear(fd, path.c_str());
            CHECK(!Exists(path), "a clean end removes it");
        }
        {
            auto [fd, path] = arm(killed);
            close(fd);  // the process gone without a word (SIGKILL)
        }
        {
            auto [fd, path] = arm(crashed);
            const char report[] = "BurnoutRevenge (Burnout Revenge, Xbox 360): crash report\n";
            CHECK(write(fd, report, sizeof(report) - 1) > 0, "write");
            close(fd);  // the handler's rename didn't happen
        }
        auto [currentFd, currentPath] = arm(current);
        const std::vector<marker::Leftover> found = marker::Find(scratch, current);
        CHECK(found.size() == 2, "the next launch finds 2 earlier markers (%zu)", found.size());
        const marker::Leftover* k = FindSession(found, killed);
        const marker::Leftover* c = FindSession(found, crashed);
        CHECK(k && k->ended == marker::Ended::WithoutQuitting, "SIGKILL's empty marker: ended without quitting");
        CHECK(c && c->ended == marker::Ended::Crashed, "a written marker: a crash report");
        CHECK(!FindSession(found, clean), "the clean end left nothing");
        CHECK(!FindSession(found, current), "the current session's own marker isn't an earlier one's");
        marker::Clear(currentFd, currentPath.c_str());

        // main()'s order, end to end: arm the marker, install the handlers,
        // run frames until a quit is requested, then the quit path clears
        // the marker and exits 0.
        auto session = [&](const std::string& name) {
            fflush(stdout);
            const pid_t pid = fork();
            if (pid == 0)
            {
                const std::string path = marker::PathFor(scratch, name).string();
                const int fd = marker::Arm(path.c_str());
                quitsignals::Install();
                std::ofstream(scratch / (name + ".ready")) << "ready\n";
                while (!quitsignals::Requested())  // RunFrame
                    std::this_thread::sleep_for(std::chrono::milliseconds(2));
                marker::Clear(fd, path.c_str());  // report::ClearCrashMarker
                _exit(0);
            }
            CHECK(WaitFor([&] { return Exists(scratch / (name + ".ready")); }, 5000), "%s: the child started", name.c_str());
            return pid;
        };
        for (int sig : quitsignals::kSignals)
        {
            const std::string name = std::string("signal-") + quitsignals::Name(sig);
            const pid_t pid = session(name);
            kill(pid, sig);
            int status = 0;
            waitpid(pid, &status, 0);
            CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0, "%s: the session quit by itself, exit 0", quitsignals::Name(sig));
            CHECK(!Exists(marker::PathFor(scratch, name)), "%s: no marker left", quitsignals::Name(sig));
        }
        {
            const pid_t pid = session("signal-SIGKILL");
            kill(pid, SIGKILL);
            int status = 0;
            waitpid(pid, &status, 0);
            CHECK(WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL, "SIGKILL: killed");
            const std::vector<marker::Leftover> after = marker::Find(scratch, current);
            const marker::Leftover* l = FindSession(after, "signal-SIGKILL");
            CHECK(l && l->ended == marker::Ended::WithoutQuitting, "SIGKILL: its marker stays, and the next launch finds it ended without quitting");
        }
        fs::remove_all(scratch, ec);
    }

    std::string Read(const fs::path& path)
    {
        std::ifstream in(path);
        std::stringstream ss;
        ss << in.rdbuf();
        return ss.str();
    }

    struct Sources
    {
        std::string main, presenter, crash;
    };

    // A function's body: from `head` to the brace at its own indent.
    std::string Body(const std::string& src, const std::string& head)
    {
        const size_t at = src.find(head);
        if (at == std::string::npos)
            return {};
        const size_t line = src.rfind('\n', at) + 1;
        const std::string end = "\n" + src.substr(line, at - line) + "}";
        const size_t stop = src.find(end, at);
        return src.substr(at, stop == std::string::npos ? std::string::npos : stop - at);
    }

    // `a` before `b` in `text`, both there.
    bool Before(const std::string& text, const std::string& a, const std::string& b, size_t from = 0)
    {
        const size_t x = text.find(a, from), y = text.find(b, from);
        return x != std::string::npos && y != std::string::npos && x < y;
    }

    std::vector<std::string> SourceProblems(const Sources& s)
    {
        std::vector<std::string> problems;
        // main.cpp: the handlers before SDL (the windowed start), and after
        // the crash handlers' marker exists.
        if (!Before(s.main, "report::PrepareCrashReports();", "quitsignals::Install();") ||
            !Before(s.main, "quitsignals::Install();", "bool windowed = video::Initialize();"))
            problems.push_back("main.cpp: quitsignals::Install() isn't between PrepareCrashReports and the window's video::Initialize");
        // Any return from main() after the marker exists clears it.
        const size_t prepared = s.main.find("report::PrepareCrashReports();");
        const size_t guard = s.main.find("~ClearMarkerOnReturn() { report::ClearCrashMarker(); }");
        const size_t firstReturnAfter = prepared == std::string::npos ? std::string::npos : s.main.find("        return ", prepared);
        if (guard == std::string::npos || prepared == std::string::npos || guard < prepared || guard > firstReturnAfter)
            problems.push_back("main.cpp: no marker-clearing guard between PrepareCrashReports and the first return after it");
        // The window's quit path: the marker cleared after the shutdown, just
        // before the log flush and the exit.
        const size_t loop = s.main.find("while (video::RunFrame())");
        if (loop == std::string::npos)
            problems.push_back("main.cpp: no game loop");
        else
        {
            const std::string tail = s.main.substr(loop, s.main.find("\n}", loop) - loop);
            if (!Before(tail, "video::Shutdown();", "report::ClearCrashMarker();") ||
                !Before(tail, "report::ClearCrashMarker();", "report::FlushLog();") ||
                !Before(tail, "report::ClearCrashMarker();", "std::_Exit(0);") || tail.find("settings::Save()") == std::string::npos)
                problems.push_back("main.cpp: the quit path doesn't save settings and clear the marker after the shutdown, before the exit");
        }
        // The launcher lifeline's exit and the headless one clear it too.
        for (const char* head : { "launcher (pid %d) is gone: exiting", "quitting\\n\", quitsignals::Name(sig));" })
        {
            const size_t at = s.main.find(head);
            if (at == std::string::npos || !Before(s.main, "report::ClearCrashMarker();", "std::_Exit(0);", at))
                problems.push_back(std::string("main.cpp: the exit after \"") + head + "\" doesn't clear the marker first");
        }
        if (s.main.find("while (!s_guestReturned.load() && !quitsignals::Requested())") == std::string::npos)
            problems.push_back("main.cpp: without a window, a quit request isn't waited for");
        // presenter.cpp: RunFrame leaves on a request, before taking events.
        const std::string run = Body(s.presenter, "bool RunFrame()");
        const size_t request = run.find("if (quitsignals::Requested())");
        if (request == std::string::npos || run.find("return false;", request) > run.find("while (SDL_PollEvent(&e))") ||
            run.find("while (SDL_PollEvent(&e))") == std::string::npos)
            problems.push_back("presenter.cpp: RunFrame doesn't leave on a quit request before its event loop");
        // crash.cpp: a crash being reported keeps its report.
        const std::string clear = Body(s.crash, "void ClearCrashMarker()");
        if (!Before(clear, "if (s_crashing.load())", "marker::Clear("))
            problems.push_back("crash.cpp: ClearCrashMarker doesn't leave a crash being reported alone");
        const std::string notice = Body(s.crash, "void NoticePreviousCrash(bool lastClearsMarker)");
        if (notice.find("marker::Ended::Crashed") == std::string::npos || notice.find("ended without quitting") == std::string::npos)
            problems.push_back("crash.cpp: the next launch doesn't rename crash reports and log sessions that ended without quitting");
        return problems;
    }

    std::string Replaced(std::string s, const std::string& from, const std::string& to)
    {
        const size_t at = s.find(from);
        if (at == std::string::npos)
            return {};
        return s.replace(at, from.size(), to);
    }

    void Runtime(const fs::path& runtime)
    {
        printf("main.cpp, presenter.cpp, crash.cpp:\n");
        const Sources src{ Read(runtime / "main.cpp"), Read(runtime / "video/presenter.cpp"), Read(runtime / "report/crash.cpp") };
        CHECK(!src.main.empty() && !src.presenter.empty() && !src.crash.empty(), "can't read the sources in %s", runtime.c_str());
        if (src.main.empty() || src.presenter.empty() || src.crash.empty())
            return;
        for (const std::string& p : SourceProblems(src))
            CHECK(false, "%s", p.c_str());
        struct Plant
        {
            const char* name;
            std::string Sources::*file;
            std::vector<std::pair<std::string, std::string>> edits;  // each: the first `from` becomes `to`
        };
        const Plant plants[] = {
            { "v0.1.5's quit path (no marker cleared)", &Sources::main,
                { { "    // A clean end: no crash marker left (a crash in the shutdown above still\n    // reported one).\n"
                    "    report::ClearCrashMarker();\n", "" } } },
            { "the marker cleared before the shutdown", &Sources::main,
                { { "    gpu::renderer::Shutdown();\n    video::Shutdown();\n",
                    "    report::ClearCrashMarker();\n    gpu::renderer::Shutdown();\n    video::Shutdown();\n" } } },
            { "no handlers (SDL's, and SIGHUP's default)", &Sources::main, { { "    quitsignals::Install();\n", "" } } },
            { "the handlers after SDL starts", &Sources::main,
                { { "    quitsignals::Install();\n", "" },
                    { "    bool windowed = video::Initialize();\n", "    bool windowed = video::Initialize();\n    quitsignals::Install();\n" } } },
            { "no guard for returns", &Sources::main, { { "~ClearMarkerOnReturn() { report::ClearCrashMarker(); }", "~ClearMarkerOnReturn() {}" } } },
            { "the lifeline's exit keeps the marker", &Sources::main,
                { { "                gpu::renderer::Shutdown();\n                report::ClearCrashMarker();\n",
                    "                gpu::renderer::Shutdown();\n" } } },
            { "headless: no wait for a request", &Sources::main,
                { { "while (!s_guestReturned.load() && !quitsignals::Requested())", "while (!s_guestReturned.load())" } } },
            { "RunFrame ignores the request", &Sources::presenter, { { "        if (quitsignals::Requested())\n            return false;\n", "" } } },
            { "a crash being reported cleared", &Sources::crash, { { "        if (s_crashing.load())\n            return;\n", "" } } },
        };
        for (const Plant& p : plants)
        {
            Sources mutated = src;
            bool applied = true;
            for (const auto& [from, to] : p.edits)
            {
                std::string next = Replaced(mutated.*p.file, from, to);
                applied = applied && !next.empty();
                if (!next.empty())
                    mutated.*p.file = next;
            }
            CHECK(applied, "plant \"%s\": its text isn't in the source", p.name);
            if (!applied)
                continue;
            const bool caught = !SourceProblems(mutated).empty();
            CHECK(caught, "plant \"%s\" not caught", p.name);
            printf("  plant: %s: %s\n", p.name, caught ? "caught" : "NOT caught");
        }
    }
}

int main(int argc, char** argv)
{
    Signals();
    fs::path scratch = argc > 1 ? fs::path(argv[1]) : fs::temp_directory_path() / ("clean_exit_test." + std::to_string(getpid()));
    Marker(scratch);
    if (argc > 2)
        Runtime(argv[2]);
    else
        printf("(the runtime folder not given: its checks skipped)\n");
    printf("%s (%d failures)\n", g_failures ? "FAILED" : "passed", g_failures);
    return g_failures ? 1 : 0;
}

// Tests for portable mode (user/portable.h) and switching into it and back
// (install/portable_switch.h): the layout a portable.txt turns on, the
// records that move with the folder, the data and cache paths, where the
// game may go inside BurnoutRevenge's folder, the startup write check, then the
// --portable on/off copies in a scratch folder (a stand-in copies the game).
// From the repo root:
//   clang++ -std=c++20 -O2 -Iruntime tests/portable_test.cpp runtime/user/portable.cpp runtime/install/{portable_switch,placement,installer,locate,manifest,result,disc_source,sha256}.cpp -o build/portable_test
//   (on macOS add -framework CoreFoundation)
//   build/portable_test [folder]
// The disk checks run in a new folder made inside [folder] (default: the
// current folder), and only that one is deleted afterwards. On macOS and iOS
// portable mode never turns on (portable::kSupported); the disk checks set
// the layout directly there, so the rest of it is checked on a Mac too.
#include <install/installer.h>
#include <install/locate.h>
#include <install/placement.h>
#include <install/portable_switch.h>
#include <user/paths.h>
#include <user/portable.h>

#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include <sys/stat.h>
#include <unistd.h>

static int g_failures = 0, g_checks = 0;
#define CHECK(cond, ...) do { g_checks++; if (!(cond)) { g_failures++; printf("  FAIL %s:%d: %s: ", __FILE__, __LINE__, #cond); printf(__VA_ARGS__); printf("\n"); } } while (0)

namespace fs = std::filesystem;
using namespace install;
using portable::Layout;
using portable::Resolve;

static bool Has(const std::string& text, const char* part)
{
    return text.find(part) != std::string::npos;
}

static bool HasLine(const std::vector<std::string>& lines, const char* part)
{
    for (const std::string& line : lines)
        if (Has(line, part))
            return true;
    return false;
}

static std::string Joined(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines)
        text += "\n    " + line;
    return text;
}

static void Write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary) << text;
}

static std::string Read(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    std::ostringstream text;
    text << in.rdbuf();
    return text.str();
}

static bool There(const fs::path& path)
{
    std::error_code ec;
    return fs::exists(fs::symlink_status(path, ec));
}

// --- Pure ---------------------------------------------------------------------

static void PureLayout()
{
    const fs::path root = "/home/p/Games/BurnoutRevenge";
    Layout on = Resolve(root, true, true);
    CHECK(on.Active() && on.root == root && on.data == root / "data" && on.cache == root / "cache", "marked: %s %s %s",
        on.root.c_str(), on.data.c_str(), on.cache.c_str());
    CHECK(!Resolve(root, false, true).Active(), "no marker: off");
    CHECK(!Resolve(root, true, false).Active(), "unsupported (Apple): off even with the marker");
    CHECK(!Resolve("", true, true).Active(), "BurnoutRevenge's folder unknown: off");
    CHECK(!Resolve("Games/BurnoutRevenge", true, true).Active(), "a relative folder: off");
    Layout trailing = Resolve("/home/p/Games/BurnoutRevenge/", true, true);
    CHECK(trailing.root == root && trailing.data == root / "data", "a trailing separator: %s, %s", trailing.root.c_str(),
        trailing.data.c_str());
    Layout dotted = Resolve("/home/p/Games/./x/../BurnoutRevenge", true, true);
    CHECK(dotted.root == root, "normalized: %s", dotted.root.c_str());
#ifdef __APPLE__
    CHECK(!portable::kSupported, "never on Apple platforms");
#else
    CHECK(portable::kSupported, "Linux (and Windows later)");
#endif
    CHECK(Has(portable::Describe(on), "portable mode") && Has(portable::Describe(on), "/home/p/Games/BurnoutRevenge/data")
        && Has(portable::Describe(on), "/home/p/Games/BurnoutRevenge/cache"), "%s", portable::Describe(on).c_str());
    CHECK(portable::Describe(Layout{}).empty(), "nothing to say when off");
}

static void PureRecords()
{
    const Layout on = Resolve("/home/p/SB", true, true), off;
    using portable::ParseRecord;
    using portable::RecordText;
    CHECK(RecordText("/home/p/SB/data/game2", on) == "data/game2", "inside: relative: %s",
        RecordText("/home/p/SB/data/game2", on).c_str());
    CHECK(RecordText("/home/p/SB/BurnoutRevenge Game", on) == "BurnoutRevenge Game", "%s",
        RecordText("/home/p/SB/BurnoutRevenge Game", on).c_str());
    CHECK(RecordText("/home/p/SB/x/y/", on) == "x/y", "a trailing separator: %s", RecordText("/home/p/SB/x/y/", on).c_str());
    CHECK(RecordText("/home/p/SB2/game", on) == "/home/p/SB2/game", "a sibling sharing the prefix: absolute: %s",
        RecordText("/home/p/SB2/game", on).c_str());
    CHECK(RecordText("/home/p/SB", on) == "/home/p/SB", "the folder itself: absolute");
    CHECK(RecordText("/run/media/p/SD/burnout-revenge/game", on) == "/run/media/p/SD/burnout-revenge/game", "outside: absolute");
    CHECK(RecordText("/home/p/SB/data/game2", off) == "/home/p/SB/data/game2", "not portable: absolute");

    auto parsed = [](std::string_view text, const Layout& layout) {
        std::optional<fs::path> p = ParseRecord(text, layout);
        return p ? p->string() : std::string("(none)");
    };
    CHECK(parsed("data/game2\n", on) == "/home/p/SB/data/game2", "relative, portable: %s", parsed("data/game2\n", on).c_str());
    CHECK(parsed("BurnoutRevenge Game\r\n", on) == "/home/p/SB/BurnoutRevenge Game", "CRLF: %s",
        parsed("BurnoutRevenge Game\r\n", on).c_str());
    CHECK(parsed("data/game2\n", off) == "(none)", "relative, not portable: ignored (as v0.1.1 did)");
    CHECK(parsed("/abs/game/\n", off) == "/abs/game", "absolute, trailing separator: %s", parsed("/abs/game/\n", off).c_str());
    CHECK(parsed("/abs/game", on) == "/abs/game", "absolute in portable mode: as it is");
    CHECK(parsed("../escape", on) == "(none)", "out of the folder: ignored");
    CHECK(parsed("data/../../escape", on) == "(none)", "out of the folder by a detour: ignored");
    CHECK(parsed("data/../BurnoutRevenge Game", on) == "/home/p/SB/BurnoutRevenge Game", "a detour that stays inside: %s",
        parsed("data/../BurnoutRevenge Game", on).c_str());
    for (const char* empty : { "", "\n", ".", "./" })
        CHECK(parsed(empty, on) == "(none)", "'%s': nothing", empty);
    // The point of it: the folder moves, the record still finds the game.
    const Layout moved = Resolve("/run/media/p/USB/Old Games/SB", true, true);
    CHECK(parsed(RecordText("/home/p/SB/BurnoutRevenge Game", on), moved) == "/run/media/p/USB/Old Games/SB/BurnoutRevenge Game",
        "after a move: %s", parsed(RecordText("/home/p/SB/BurnoutRevenge Game", on), moved).c_str());
}

static void Paths()
{
    setenv("HOME", "/home/p", 1);
    setenv("XDG_DATA_HOME", "/xdg/data", 1);
    setenv("XDG_CACHE_HOME", "/xdg/cache", 1);
    portable::CurrentLayout() = {};
    const fs::path homeData = HomeUserPath(), homeCache = HomeCachePath();
#ifndef __APPLE__
    CHECK(homeData == "/xdg/data/burnout-revenge" && homeCache == "/xdg/cache/burnout-revenge", "home: %s, %s", homeData.c_str(),
        homeCache.c_str());
#endif
    CHECK(GetUserPath() == homeData && GetCachePath() == homeCache, "not portable: the home folders");
    portable::CurrentLayout() = Resolve("/home/p/SB", true, true);
    CHECK(GetUserPath() == "/home/p/SB/data", "portable data: %s", GetUserPath().c_str());
    CHECK(GetCachePath() == "/home/p/SB/cache", "portable caches: %s", GetCachePath().c_str());
    CHECK(DefaultInstallPath() == "/home/p/SB/data/game", "the installer's default: %s", DefaultInstallPath().c_str());
    CHECK(GetSavePath(true) == "/home/p/SB/data/save", "saves: %s", GetSavePath(true).c_str());
    CHECK(HomeUserPath() == homeData && HomeCachePath() == homeCache, "the home folders still known (for --portable off)");
    portable::CurrentLayout() = {};
    CHECK(GetUserPath() == homeData, "off again");
    unsetenv("XDG_DATA_HOME");
    unsetenv("XDG_CACHE_HOME");
}

static FolderFacts Facts(const fs::path& path, FolderState state)
{
    FolderFacts f;
    f.path = path;
    f.state = state;
    f.fileSystem = "ext4";
    return f;
}

static void PlacementRules()
{
    const fs::path root = "/home/p/Games/BurnoutRevenge";
    PlacementContext c;
    c.home = "/home/p";
    c.bundle = root;
    c.portable = root;
    c.installs = { root / "data/game" };
    c.largestFile = 653262848;

    auto exact = [&](const fs::path& dir, FolderState state = FolderState::Missing) {
        return PlaceExact(Facts(dir, state), true, c);
    };
    for (const char* ok : { "data/game", "BurnoutRevenge Game", "data/other", "library", "data2", "caches", "lib2/game" })
    {
        FolderChoice choice = exact(root / ok);
        CHECK(choice.result.Ok(), "%s: allowed in portable mode: %s", ok, choice.result.message.c_str());
        CHECK(choice.notes.empty(), "%s: no note (it moves with the folder): %zu", ok, choice.notes.size());
    }
    FolderChoice choice = exact(root, FolderState::Other);
    CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "holds the program")
        && Has(choice.result.message, "/home/p/Games/BurnoutRevenge/data/game"), "the folder's top: %s",
        choice.result.message.c_str());
    for (const char* own : { "lib", "lib/game", "licenses", "licenses/x/y" })
    {
        choice = exact(root / own);
        CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "program's own files"),
            "%s: refused: %s", own, choice.result.message.c_str());
    }
    for (const char* cache : { "cache", "cache/game" })
    {
        choice = exact(root / cache);
        CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "cache folder"),
            "%s: refused: %s", cache, choice.result.message.c_str());
    }
    choice = exact(root / "data", FolderState::Other);
    CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "settings and saves"),
        "data/ itself: refused: %s", choice.result.message.c_str());
    choice = exact(root / "data/game/inner");
    CHECK(!choice.result.Ok() && Has(choice.result.message, "inside the game installed"), "inside the install: %s",
        choice.result.message.c_str());
    choice = exact("/home/p/Elsewhere");
    CHECK(choice.result.Ok() && choice.notes.size() == 1 && Has(choice.notes[0], "outside BurnoutRevenge's folder")
        && Has(choice.notes[0], "won't move"), "outside: allowed, with a note: %zu %s", choice.notes.size(),
        choice.notes.empty() ? "" : choice.notes[0].c_str());

    // Choose a folder... in portable mode.
    choice = PlaceInPicked(Facts(root, FolderState::Other), c);
    CHECK(choice.result.Ok() && choice.gameDir == root / kPickedSubfolder, "the folder picked: a subfolder in it: %s %s",
        choice.gameDir.c_str(), choice.result.message.c_str());
    choice = PlaceInPicked(Facts(root / "data", FolderState::Other), c);
    CHECK(choice.result.Ok() && choice.gameDir == root / "data" / kPickedSubfolder, "data/ picked: %s %s", choice.gameDir.c_str(),
        choice.result.message.c_str());
    choice = PlaceInPicked(Facts(root / "lib", FolderState::Other), c);
    CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "/home/p/Games/BurnoutRevenge/lib "),
        "lib/ picked: refused, naming lib/: %s", choice.result.message.c_str());

    // Not portable: all of BurnoutRevenge's folder is refused, as in v0.1.1.
    c.portable.clear();
    c.installs = { "/home/p/.local/share/burnout-revenge/game" };
    for (const char* inside : { "data/game", "BurnoutRevenge Game", "library" })
    {
        choice = exact(root / inside);
        CHECK(choice.result.error == Error::DestinationNotWritable && Has(choice.result.message, "BurnoutRevenge's own folder"),
            "%s, not portable: refused: %s", inside, choice.result.message.c_str());
    }
    choice = exact("/home/p/Elsewhere");
    CHECK(choice.result.Ok() && choice.notes.empty(), "not portable: no note: %zu", choice.notes.size());
}

// --- Through the disk ----------------------------------------------------------

// A SteamOS-style BurnoutRevenge folder: burnout-revenge.sh at the top, the program in lib/.
static fs::path MakeBundle(const fs::path& root)
{
    Write(root / "burnout-revenge.sh", "#!/bin/bash\n");
    Write(root / "lib/BurnoutRevenge", "program");
    Write(root / "README.md", "readme");
    return root;
}

static const ManifestFile s_files[] = { { "default.xex", 16, "00" } };
static const Manifest s_manifest{ "test-1", 0x454107D9, s_files, 16 };

static void FakeInstall(const fs::path& dir)
{
    Write(dir / "default.xex", std::string(16, 'x'));
    Write(dir / kInstallMarker, "manifest_version = \"test-1\"\n");
}

static void Detection(const fs::path& scratch)
{
    const fs::path root = MakeBundle(scratch / "detect/BurnoutRevenge");
    SetProgramPath((root / "lib/BurnoutRevenge").c_str());
    const fs::path bundle = BundleFolder();
#ifndef __APPLE__
    CHECK(bundle == root, "the bundle's folder: %s", bundle.c_str());
#endif
    CHECK(!portable::MarkerIn(root) && !portable::Detect(bundle).Active(), "no portable.txt: off");
    Write(root / portable::kMarker, "");
    CHECK(portable::MarkerIn(root), "portable.txt (empty) seen");
    CHECK(portable::Detect(bundle).Active() == portable::kSupported, "portable.txt (empty): on where supported");
    if (portable::kSupported)
        CHECK(portable::Current().data == root / "data" && GetUserPath() == root / "data", "current: %s",
            portable::Current().data.c_str());
    fs::remove(root / portable::kMarker);
    fs::create_directory(root / portable::kMarker);
    CHECK(!portable::MarkerIn(root) && !portable::Detect(bundle).Active(), "a folder named portable.txt: off");
    fs::remove(root / portable::kMarker);
    Write(root / "elsewhere.txt", "x");
    fs::create_symlink("elsewhere.txt", root / portable::kMarker);
    CHECK(portable::MarkerIn(root), "a link to a file: seen");
    CHECK(portable::Detect(bundle).Active() == portable::kSupported, "a link to a file: on where supported");
    fs::remove(root / portable::kMarker);
    fs::create_symlink("nowhere.txt", root / portable::kMarker);
    CHECK(!portable::MarkerIn(root), "a link to nothing: not seen");
    fs::remove(root / portable::kMarker);
    CHECK(!portable::MarkerIn(root) && !portable::Detect(bundle).Active(), "removed: off");
    CHECK(!portable::MarkerIn(""), "no folder: no marker");
    CHECK(GetUserPath() == HomeUserPath(), "off: the home folder");
    CHECK(!portable::Detect("").Active(), "unknown folder: off");

    // The startup write check.
    Layout layout = Resolve(root, true, true);
    CHECK(portable::CheckWritable(layout) == 0 && fs::is_directory(root / "data"), "makes data/ and writes there");
    CHECK(!There(root / "data/.write-check"), "and leaves nothing behind");
    CHECK(portable::CheckWritable(Layout{}) == 0, "off: nothing to check");
    if (geteuid() != 0)
    {
        chmod((root / "data").c_str(), 0555);
        int error = portable::CheckWritable(layout);
        CHECK(error == EACCES, "a read-only data folder: %d (%s)", error, strerror(error));
        chmod((root / "data").c_str(), 0755);
        fs::remove_all(root / "data");
        chmod(root.c_str(), 0555);
        error = portable::CheckWritable(layout);
        CHECK(error == EACCES && !There(root / "data"), "a read-only BurnoutRevenge folder, no data/: %d (%s)", error,
            strerror(error));
        chmod(root.c_str(), 0755);
    }
}

static void RecordsOnDisk(const fs::path& scratch)
{
    unsetenv("BURNOUT_GAME_DIR");
    setenv("HOME", (scratch / "home").c_str(), 1);
    fs::create_directories(scratch / "home");
    chdir(scratch.c_str());  // no ./game/files here for FindGameInstall to find first
    fs::path root = MakeBundle(scratch / "a/BurnoutRevenge");
    SetProgramPath((root / "lib/BurnoutRevenge").c_str());
    // The layout as Detect makes it where it's supported; set directly on a Mac.
    Write(root / portable::kMarker, "");
    auto activate = [&](const fs::path& folder) {
        portable::Detect(BundleFolder());
        if (!portable::kSupported)
            portable::CurrentLayout() = Resolve(ResolvedFolder(folder), true, true);
        return portable::Current().Active();
    };
    CHECK(activate(root), "on");
    root = ResolvedFolder(root);

    const fs::path chosen = root / kPickedSubfolder;
    FakeInstall(chosen);
    CHECK(RecordInstallPath(chosen), "recorded");
    CHECK(Read(root / "data/game-location") == std::string(kPickedSubfolder) + "\n", "relative in the file: '%s'",
        Read(root / "data/game-location").c_str());
    CHECK(RecordedInstallPath() == chosen, "read back: %s", RecordedInstallPath() ? RecordedInstallPath()->c_str() : "(none)");
    std::optional<GameInstall> found = FindGameInstall(s_manifest);
    CHECK(found && found->path == chosen && found->origin == InstallOrigin::User, "found there: %s",
        found ? found->path.c_str() : "(none)");

    std::vector<InstallDestination> where = InstallDestinations();
    CHECK(where.size() == 1 && where[0].label == "BurnoutRevenge's folder" && where[0].path == root / "data/game",
        "the To row: only BurnoutRevenge's folder: %zu, %s", where.size(), where.empty() ? "" : where[0].path.c_str());
    PlacementContext context = CurrentPlacementContext(s_manifest);
    CHECK(context.portable == root, "the placement context: %s", context.portable.c_str());
    FolderChoice lib = CheckExactFolder(root / "lib/game", true, s_manifest);
    CHECK(lib.result.error == Error::DestinationNotWritable, "lib/ refused through the disk: %s", lib.result.message.c_str());
    FolderChoice inside = CheckExactFolder(root / "data/game", true, s_manifest);
    CHECK(inside.result.Ok() || inside.result.error == Error::NotEnoughSpace, "data/game allowed through the disk: %s",
        inside.result.message.c_str());

    // The whole folder moves (another path, a space in it): the record follows.
    fs::create_directories(scratch / "b");
    fs::rename(scratch / "a/BurnoutRevenge", scratch / "b/Moved Folder");
    fs::path moved = scratch / "b/Moved Folder";
    SetProgramPath((moved / "lib/BurnoutRevenge").c_str());
    CHECK(activate(moved), "on, moved");
    moved = ResolvedFolder(moved);
    CHECK(portable::Current().root == moved, "the new root: %s", portable::Current().root.c_str());
    CHECK(RecordedInstallPath() == moved / kPickedSubfolder, "the record, moved: %s",
        RecordedInstallPath() ? RecordedInstallPath()->c_str() : "(none)");
    found = FindGameInstall(s_manifest);
    CHECK(found && found->path == moved / kPickedSubfolder, "found after the move: %s", found ? found->path.c_str() : "(none)");

    // The default place: no record; found after a move too.
    FakeInstall(moved / "data/game");
    CHECK(RecordInstallPath(moved / "data/game") && !There(moved / "data/game-location"), "the default: the record goes");
    found = FindGameInstall(s_manifest);
    CHECK(found && found->path == moved / "data/game", "the default install: %s", found ? found->path.c_str() : "(none)");
    // Outside the folder: absolute.
    const fs::path outside = ResolvedFolder(scratch / "outside/game");
    FakeInstall(outside);
    CHECK(RecordInstallPath(outside) && Read(moved / "data/game-location") == outside.string() + "\n",
        "outside: absolute: '%s'", Read(moved / "data/game-location").c_str());
    CHECK(RecordedInstallPath() == outside, "read back: %s", RecordedInstallPath() ? RecordedInstallPath()->c_str() : "(none)");

    // Off: the home folder's data, and a relative record means nothing there.
    portable::CurrentLayout() = {};
    CHECK(GetUserPath() == HomeUserPath() && !RecordedInstallPath(), "off: the home folder, no record");
    Write(HomeUserPath() / "game-location", "BurnoutRevenge Game\n");
    CHECK(!RecordedInstallPath(), "a relative record outside portable mode: ignored");
    fs::remove_all(scratch / "home");
    fs::remove_all(scratch / "a");
    fs::remove_all(scratch / "b");
    fs::remove_all(scratch / "outside");
}

// --- --portable on|off ---------------------------------------------------------

struct Copier
{
    std::vector<std::pair<fs::path, fs::path>> calls;
    bool fail = false;

    GameCopier Get()
    {
        return [this](const fs::path& from, const fs::path& to) -> Result {
            calls.push_back({ from, to });
            if (fail)
            {
                Result r;
                r.error = Error::WriteError;
                r.message = "the stand-in failed";
                return r;
            }
            FakeInstall(to);
            return {};
        };
    }
};

static fs::file_time_type Stamp(int daysAgo)
{
    return fs::file_time_type::clock::now() - std::chrono::hours(24 * daysAgo);
}

// A home data folder with settings, a save, a log (dated), and the game at its default place.
static void MakeHome(const fs::path& home, bool game = true)
{
    Write(home / "settings.toml", "[video]\nfullscreen = true\n");
    Write(home / "profile_settings.bin", "profile");
    Write(home / "save/content/slot1", "career");
    Write(home / "logs/2026-10-01_10-00-00.log", "an old session");
    fs::last_write_time(home / "logs/2026-10-01_10-00-00.log", Stamp(9));
    if (game)
        FakeInstall(home / "game");
}

static void SwitchOn(const fs::path& scratch)
{
    const fs::path base = scratch / "on";
    const fs::path home = base / "home/.local/share/burnout-revenge";
    const fs::path root = ResolvedFolder(MakeBundle(base / "Games/BurnoutRevenge"));
    const fs::path data = root / "data";
    MakeHome(home);
    const auto logTime = fs::last_write_time(home / "logs/2026-10-01_10-00-00.log");

    Copier copier;
    std::vector<std::string> said;
    Result r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok(), "on: %s%s", r.message.c_str(), Joined(said).c_str());
    CHECK(Read(data / "settings.toml") == "[video]\nfullscreen = true\n" && Read(data / "save/content/slot1") == "career"
        && Read(data / "profile_settings.bin") == "profile", "settings and saves copied");
    CHECK(fs::last_write_time(data / "logs/2026-10-01_10-00-00.log") == logTime, "the log keeps its date");
    CHECK(copier.calls.size() == 1 && copier.calls[0].first == ResolvedFolder(home / "game")
        && copier.calls[0].second == root / "data.partial/game", "the game copied once, verified, into the copy: %zu %s -> %s",
        copier.calls.size(), copier.calls.empty() ? "" : copier.calls[0].first.c_str(),
        copier.calls.empty() ? "" : copier.calls[0].second.c_str());
    CHECK(IsUsableInstall(data / "game", true, s_manifest), "the game in data/game");
    CHECK(!There(data / "game-location") && !There(data / kPortablePartialTag) && !There(root / "data.partial"),
        "no record, no tag, no partial copy left");
    CHECK(fs::is_regular_file(root / portable::kMarker) && Has(Read(root / portable::kMarker), "portable mode"),
        "portable.txt written");
    CHECK(Read(home / "settings.toml") == "[video]\nfullscreen = true\n" && IsUsableInstall(home / "game", true, s_manifest),
        "the home folder left as it was");
    CHECK(HasLine(said, "Portable mode is on") && HasLine(said, "Nothing was removed") && HasLine(said, "4 files"),
        "said:%s", Joined(said).c_str());

    // Again: already on.
    said.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::DestinationNotEmpty && Has(r.message, "already on") && copier.calls.size() == 1, "again: %s",
        r.message.c_str());
    // portable.txt deleted by hand, data/ still there: refused, nothing changed.
    fs::remove(root / portable::kMarker);
    Write(data / "settings.toml", "changed in portable mode");
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::DestinationNotEmpty && Has(r.message, "already holds files")
        && Read(data / "settings.toml") == "changed in portable mode" && !There(root / portable::kMarker),
        "data/ there: refused: %s", r.message.c_str());
    fs::remove_all(data);

    // An unfinished copy from an earlier attempt goes; anything else of that name is refused.
    Write(root / "data.partial" / kPortablePartialTag, "");
    Write(root / "data.partial/settings.toml", "half");
    said.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && HasLine(said, "unfinished copy") && Read(data / "settings.toml") == "[video]\nfullscreen = true\n",
        "an unfinished copy replaced: %s%s", r.message.c_str(), Joined(said).c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);
    Write(root / "data.partial/mine.txt", "the player's");
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::DestinationNotEmpty && Read(root / "data.partial/mine.txt") == "the player's"
        && !There(root / portable::kMarker), "a data.partial of the player's: refused, kept: %s", r.message.c_str());
    fs::remove_all(root / "data.partial");

    // The game copy fails: nothing changes.
    copier.fail = true;
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::WriteError && !There(data) && !There(root / "data.partial") && !There(root / portable::kMarker),
        "a failed game copy: nothing left: %s", r.message.c_str());
    copier.fail = false;

    // An empty data/ (made by a start without portable.txt? by hand) is used.
    fs::create_directory(data);
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && IsUsableInstall(data / "game", true, s_manifest), "an empty data/: used: %s", r.message.c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);

    // The game recorded elsewhere: that one is copied (as FindGameInstall
    // picks it: before one at the default place), and its record isn't.
    const fs::path elsewhere = ResolvedFolder(base / "card/burnout-revenge/game");
    FakeInstall(elsewhere);
    Write(home / "game-location", elsewhere.string() + "\n");
    copier.calls.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.size() == 1 && copier.calls[0].first == elsewhere && !There(data / "game-location"),
        "the recorded game: %s %s", r.message.c_str(), copier.calls.empty() ? "" : copier.calls[0].first.c_str());
    CHECK(!There(data / "game") || IsUsableInstall(data / "game", true, s_manifest), "data/game is the copied game only");
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);
    fs::remove_all(home / "game");
    copier.calls.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.size() == 1 && copier.calls[0].first == elsewhere, "the recorded game, none at the "
        "default place: %s %s", r.message.c_str(), copier.calls.empty() ? "" : copier.calls[0].first.c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);

    // Recorded, but not there (a card out), and none at the default place: refused, nothing copied.
    fs::remove_all(elsewhere);
    copier.calls.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::NotFound && Has(r.message, "isn't there now") && Has(r.message, "game-location")
        && copier.calls.empty() && !There(data) && !There(root / portable::kMarker), "a lost game: refused: %s",
        r.message.c_str());
    // Recorded and not there, but one at the default place: that one (as FindGameInstall would).
    FakeInstall(home / "game");
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.size() == 1 && copier.calls[0].first == ResolvedFolder(home / "game"),
        "the default one when the record's is gone: %s", r.message.c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);
    fs::remove(home / "game-location");

    // A game folder recorded INSIDE the home data folder isn't copied as data.
    fs::remove_all(home / "game");
    const fs::path inHome = ResolvedFolder(home / kPickedSubfolder);
    FakeInstall(inHome);
    Write(home / "game-location", inHome.string() + "\n");
    copier.calls.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.size() == 1 && copier.calls[0].first == inHome && !There(data / kPickedSubfolder),
        "a game inside the data folder: copied once, as the game: %s", r.message.c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);
    fs::remove_all(inHome);
    fs::remove(home / "game-location");

    // No game: the data only. A link in the data folder: not followed, said.
    fs::create_symlink("/", home / "a-link");
    copier.calls.clear();
    said.clear();
    r = MakePortable(root, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.empty() && HasLine(said, "No installed game") && HasLine(said, "a-link")
        && !There(data / "a-link") && fs::is_regular_file(root / portable::kMarker), "no game: %s%s", r.message.c_str(),
        Joined(said).c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);

    // No home data folder at all: a fresh portable folder.
    said.clear();
    r = MakePortable(root, base / "nobody/.local/share/burnout-revenge", copier.Get(), said, s_manifest);
    CHECK(r.Ok() && fs::is_directory(data) && fs::is_regular_file(root / portable::kMarker) && HasLine(said, "nothing of them"),
        "nothing at home: %s%s", r.message.c_str(), Joined(said).c_str());
    fs::remove_all(data);
    fs::remove(root / portable::kMarker);

    r = MakePortable("", home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::NotFound, "BurnoutRevenge's folder unknown: %s", r.message.c_str());
    if (geteuid() != 0)
    {
        chmod(root.c_str(), 0555);
        r = MakePortable(root, home, copier.Get(), said, s_manifest);
        CHECK(r.error == Error::DestinationNotWritable && !There(root / portable::kMarker), "a read-only folder: %s",
            r.message.c_str());
        chmod(root.c_str(), 0755);
    }
    fs::remove_all(base);
}

static void SwitchOff(const fs::path& scratch)
{
    const fs::path base = scratch / "off";
    const fs::path home = base / "home/.local/share/burnout-revenge";
    const fs::path root = ResolvedFolder(MakeBundle(base / "Games/BurnoutRevenge"));
    const fs::path data = root / "data";
    // Before: a home folder (the old copies), a portable folder with newer settings and the game.
    MakeHome(home);
    Write(home / "settings.toml", "old");
    MakeHome(data);
    Write(data / "settings.toml", "newer, portable");
    Write(root / portable::kMarker, "");
    Layout layout = Resolve(root, true, true);

    Copier copier;
    std::vector<std::string> said;
    Result r = LeavePortable(layout, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok(), "off: %s%s", r.message.c_str(), Joined(said).c_str());
    CHECK(Read(home / "settings.toml") == "newer, portable" && Read(home / "save/content/slot1") == "career",
        "the portable data in the home folder");
    CHECK(copier.calls.size() == 1 && copier.calls[0].first == data / "game"
        && copier.calls[0].second == fs::path(ResolvedFolder(home).string() + ".partial") / "game",
        "the game copied back, verified: %zu %s", copier.calls.size(), copier.calls.empty() ? "" : copier.calls[0].second.c_str());
    CHECK(IsUsableInstall(home / "game", true, s_manifest), "the game at home");
    CHECK(!There(root / portable::kMarker), "portable.txt removed");
    CHECK(Read(data / "settings.toml") == "newer, portable" && IsUsableInstall(data / "game", true, s_manifest),
        "data/ left as it was");
    fs::path aside;
    for (const auto& entry : fs::directory_iterator(home.parent_path()))
        if (entry.path().filename().string().starts_with("burnout-revenge.before-portable-off-"))
            aside = entry.path();
    CHECK(!aside.empty() && Read(aside / "settings.toml") == "old" && IsUsableInstall(aside / "game", true, s_manifest),
        "the old home folder kept, renamed aside: %s", aside.c_str());
    CHECK(HasLine(said, "Renamed the data folder") && HasLine(said, "Portable mode is off"), "said:%s", Joined(said).c_str());
    CHECK(!There(fs::path(home.string() + ".partial")), "no partial copy left");

    // Not portable: nothing to do.
    r = LeavePortable(Layout{}, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::NotFound && Has(r.message, "isn't on"), "not portable: %s", r.message.c_str());

    // A game recorded outside the folder stays there, recorded at home; the home folder there is empty: replaced.
    fs::remove_all(home.parent_path());
    fs::create_directories(home);
    fs::remove_all(data / "game");
    const fs::path outside = ResolvedFolder(base / "card/burnout-revenge/game");
    FakeInstall(outside);
    Write(data / "game-location", outside.string() + "\n");
    Write(root / portable::kMarker, "");
    copier.calls.clear();
    said.clear();
    r = LeavePortable(layout, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.empty() && Read(home / "game-location") == outside.string() + "\n"
        && HasLine(said, "stays where it is") && !HasLine(said, "Renamed"), "outside: recorded as it is: %s%s",
        r.message.c_str(), Joined(said).c_str());

    // A game recorded relative (inside the folder, not data/game): copied to the home folder's game/.
    fs::remove_all(home.parent_path());
    FakeInstall(root / kPickedSubfolder);
    Write(data / "game-location", std::string(kPickedSubfolder) + "\n");
    Write(root / portable::kMarker, "");
    copier.calls.clear();
    r = LeavePortable(layout, home, copier.Get(), said, s_manifest);
    CHECK(r.Ok() && copier.calls.size() == 1 && copier.calls[0].first == root / kPickedSubfolder
        && !There(home / "game-location") && IsUsableInstall(home / "game", true, s_manifest),
        "a relative record: copied home: %s", r.message.c_str());

    // The game copy fails: the home folder and portable.txt as they were.
    MakeHome(home);
    Write(home / "settings.toml", "untouched");
    Write(root / portable::kMarker, "");
    copier.fail = true;
    r = LeavePortable(layout, home, copier.Get(), said, s_manifest);
    CHECK(r.error == Error::WriteError && Read(home / "settings.toml") == "untouched" && There(root / portable::kMarker)
        && !There(fs::path(ResolvedFolder(home).string() + ".partial")), "a failed copy: nothing changed: %s",
        r.message.c_str());
    copier.fail = false;

    // This computer's data folder inside BurnoutRevenge's folder (XDG_DATA_HOME pointed there): refused.
    r = LeavePortable(layout, root / "xdg/burnout-revenge", copier.Get(), said, s_manifest);
    CHECK(r.error == Error::DestinationNotWritable && Has(r.message, "inside BurnoutRevenge's folder"), "home inside: %s",
        r.message.c_str());
    fs::remove_all(base);
}

int main(int argc, char** argv)
{
    PureLayout();
    PureRecords();
    Paths();
    PlacementRules();

    fs::path parent = argc > 1 ? fs::path(argv[1]) : fs::current_path();
    fs::create_directories(parent);
    std::string pattern = (parent / "portable-test-XXXXXX").string();
    if (!mkdtemp(pattern.data()))
    {
        printf("can't make a scratch folder in %s: %s\n", parent.c_str(), strerror(errno));
        return 2;
    }
    const fs::path scratch = fs::weakly_canonical(pattern);
    Detection(scratch);
    RecordsOnDisk(scratch);
    SwitchOn(scratch);
    SwitchOff(scratch);
    portable::CurrentLayout() = {};
    std::error_code ec;
    fs::remove_all(scratch, ec);

    if (g_failures)
    {
        printf("%d of %d checks failed\n", g_failures, g_checks);
        return 1;
    }
    printf("portable_test: all %d checks passed\n", g_checks);
    return 0;
}

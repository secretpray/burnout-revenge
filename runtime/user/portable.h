// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Portable mode (GitHub issue #1): everything BurnoutRevenge keeps (settings,
// saves, logs, caches and, by default, the installed game) in BurnoutRevenge's
// own folder, so the whole folder can be moved to another machine and run
// there. Opt-in: a file named portable.txt in that folder (the one with
// burnout-revenge.sh; a development build's program folder; on Windows, the
// one with BurnoutRevenge.exe: install::BundleFolder()). Its contents are
// ignored.
//
// Why a file: it travels with the folder (an environment variable lives in
// a Steam shortcut or a shell profile, per machine, so the next machine
// would quietly use its own data folder), nothing makes it by accident (the
// download never holds one, and unpacking an update over the folder leaves
// it), and players know it from Xenia, Dolphin and DuckStation. Not a data/
// folder as the switch: deleting or renaming a folder would flip the mode
// without a word, and a leftover can't be told from an opt-in.
//
// The layout, in BurnoutRevenge's folder (the "root"):
//   data/    what GetUserPath() is otherwise (~/.local/share/burnout-revenge),
//            the same layout: settings.toml, profile_settings.bin, save/,
//            logs/ (with crash and hang reports), game-location, and game/,
//            the installer's default
//   cache/   what GetCachePath() is otherwise (~/.cache/burnout-revenge):
//            SPIR-V (GPU-independent, keyed by shader source) and the Vulkan
//            pipeline cache (its header names the GPU and driver: another
//            GPU's driver ignores it and it is rebuilt). Beside data/, so it
//            can be deleted without touching saves.
// Nothing outside the folder is read or written for BurnoutRevenge's data:
// MigrateUserFolders does nothing, XDG_DATA_HOME and XDG_CACHE_HOME are
// ignored, and no symlink is made. The game's location is recorded relative
// to the folder when it is inside it (RecordText), so it moves with it.
//
// Not on Apple platforms: a signed Mac app can't hold data (macOS stops
// opening it), a folder beside it is App-Translocated or privacy-prompted in
// the common cases, and iOS apps write only their container. There the
// layout is never active.
//
// Switching an existing setup in or out with a copy: install/portable_switch.h
// (burnout-revenge.sh --portable on|off). By hand, making or deleting the file
// switches with nothing copied.
#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace portable
{
    inline constexpr const char* kMarker = "portable.txt";
    inline constexpr const char* kDataFolder = "data";
    inline constexpr const char* kCacheFolder = "cache";

#if defined(__APPLE__)
    inline constexpr bool kSupported = false;
#else
    inline constexpr bool kSupported = true;
#endif

    // Where portable mode keeps things; all empty when it's off.
    struct Layout
    {
        std::filesystem::path root;     // BurnoutRevenge's folder: absolute, normalized, no trailing separator
        std::filesystem::path data;     // root/data
        std::filesystem::path cache;    // root/cache

        bool Active() const { return !root.empty(); }
    };

    // Pure: the layout for BurnoutRevenge's folder `root` (absolute; empty if
    // unknown), with the marker in it or not. Off unless supported, marked
    // and absolute.
    Layout Resolve(const std::filesystem::path& root, bool marker, bool supported = kSupported);

    // The layout in effect: set by Detect at startup, before any other thread
    // starts and before anything uses a data folder (paths.h reads it).
    inline Layout& CurrentLayout()
    {
        static Layout layout;
        return layout;
    }

    inline const Layout& Current()
    {
        return CurrentLayout();
    }

    // Is there a portable.txt in `root`: a file, or a link to one (one stat)?
    bool MarkerIn(const std::filesystem::path& root);

    // Startup: Resolve(root, MarkerIn(root)) made current, and returned. No
    // look at the disk on Apple platforms.
    const Layout& Detect(const std::filesystem::path& root);

    // The text recorded for the game folder `folder` (absolute, links
    // resolved): relative to the root, with '/' separators, when portable
    // mode is on and the folder is inside the root; otherwise the absolute
    // path.
    std::string RecordText(const std::filesystem::path& folder, const Layout& layout);

    // The folder a record names, or nullopt: an absolute path as it is (no
    // trailing separator); a relative one only in portable mode, inside the
    // root (resolved against where the root is now; "../" out of it is
    // ignored). Trailing newlines are ignored.
    std::optional<std::filesystem::path> ParseRecord(std::string_view text, const Layout& layout);

    // Startup, portable mode: makes the data folder if needed and writes a
    // small file there (then deletes it). 0 if that worked, else the errno
    // (EROFS, EACCES...): settings and saves won't be kept. 0 when off.
    int CheckWritable(const Layout& layout);

    // One log line: where the data and caches are ("" when off).
    std::string Describe(const Layout& layout);
}

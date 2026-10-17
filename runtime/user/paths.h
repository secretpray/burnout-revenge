// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
#pragma once
#include <cstdlib>
#include <filesystem>
#include <string>
#ifdef __APPLE__
#include <TargetConditionals.h>
#endif

#include "portable.h"
#include <game/title.h>

// Extracted disc contents (default.xex, NFS/, Movies/): where startup found
// or installed the game (install::FindGameInstall, SetGamePath); before
// that, BURNOUT_GAME_DIR or the repo layout.
inline std::filesystem::path& GamePathStorage()
{
    static std::filesystem::path path;  // set once, before the game starts
    return path;
}

inline void SetGamePath(std::filesystem::path path)
{
    GamePathStorage() = std::move(path);
}

inline std::filesystem::path GetGamePath()
{
    if (!GamePathStorage().empty())
        return GamePathStorage();
    if (const char* dir = std::getenv(game::kDirectoryEnv))
        return dir;
    return game::kDevelopmentDirectory;
}

// Burnout uses its game's name; Most Wanted keeps BurnoutRevenge's folders.
// MigrateUserFolders renames an older development folder before any paths
// are used; if the rename fails, that folder remains the active one.
#if defined(BR_GAME_BURNOUT) && defined(__APPLE__)
inline constexpr const char* kUserFolderName = "BurnoutRevenge";
#else
inline constexpr const char* kUserFolderName = "burnout-revenge";
#endif
#if defined(BR_GAME_BURNOUT) && defined(__APPLE__)
inline constexpr const char* kOldUserFolderName = "BurnoutRevenge";
#else
inline constexpr const char* kOldUserFolderName = "burnout-revenge";
#endif

// The folders' names in use (set before any other thread starts).
inline const char*& DataFolderName()
{
    static const char* name = kUserFolderName;
    return name;
}

inline const char*& CacheFolderName()
{
    static const char* name = kUserFolderName;
    return name;
}

// Where the per-user folders go (outside portable mode: user/portable.h).
//   macOS: ~/Library/Application Support, ~/Library/Caches
//   iOS: the same in the app's container; $XDG_DATA_HOME and
//        $XDG_CACHE_HOME when set (test runs: iOS sets HOME itself)
//   Linux: $XDG_DATA_HOME or ~/.local/share, $XDG_CACHE_HOME or ~/.cache
inline std::filesystem::path UserDataBase()
{
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
#ifdef __APPLE__
#if TARGET_OS_IOS
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return xdg;
#endif
    return base / "Library/Application Support";
#else
    if (const char* xdg = std::getenv("XDG_DATA_HOME"))
        return xdg;
    return base / ".local/share";
#endif
}

inline std::filesystem::path CacheBase()
{
    const char* home = std::getenv("HOME");
    std::filesystem::path base = home ? home : ".";
#ifdef __APPLE__
#if TARGET_OS_IOS
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
        return xdg;
#endif
    return base / "Library/Caches";
#else
    if (const char* xdg = std::getenv("XDG_CACHE_HOME"))
        return xdg;
    return base / ".cache";
#endif
}

// This computer's own data and cache folders, portable mode or not
// (install/portable_switch.h copies between them and BurnoutRevenge's folder).
inline std::filesystem::path HomeUserPath()
{
    return UserDataBase() / DataFolderName();
}

inline std::filesystem::path HomeCachePath()
{
    return CacheBase() / CacheFolderName();
}

// Per-user data: saves, settings, logs and (by default) the installed game.
// In portable mode, the data folder in BurnoutRevenge's own folder.
inline std::filesystem::path GetUserPath()
{
    // Explicit development/test roots keep smoke runs out of the player's
    // installed profiles. Defaults and portable layouts are unchanged.
    if (const char* dir = std::getenv("BR_DATA_DIR"); dir && *dir)
        return dir;
    if (const portable::Layout& layout = portable::Current(); layout.Active())
        return layout.data;
    return HomeUserPath();
}

// Caches that can be deleted any time (compiled shaders, pipelines). In
// portable mode, the cache folder in BurnoutRevenge's own folder.
inline std::filesystem::path GetCachePath()
{
    if (const char* dir = std::getenv("BR_CACHE_DIR"); dir && *dir)
        return dir;
    if (const portable::Layout& layout = portable::Current(); layout.Active())
        return layout.cache;
    return HomeCachePath();
}

// Renames older folders to the current name. Only Most Wanted leaves its
// former name as a compatibility link; Burnout's first release does not.
// Call first thing
// in main (after portable::Detect: in portable mode it does nothing, since
// the home folder isn't BurnoutRevenge's then); returns what it did, for the
// log once that is open.
std::string MigrateUserFolders();

inline std::filesystem::path GetSavePath(bool)
{
    return GetUserPath() / "save";
}

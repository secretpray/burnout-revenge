// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// Backups of the player's saves (the data folder's save/), kept in
// backups/ beside it. Filesystem only (no SDL, no settings), so
// tests/save_backup_test.cpp runs it on scratch folders.
//
//   - At launch, before the game runs (so nothing is writing the saves),
//     OnLaunch snapshots save/ into backups/<date>_<time>/ when it differs
//     from the newest backup, and keeps the newest kKeep. A snapshot is
//     copied into backups/.incoming-<name>/, checked (same files, same
//     bytes), then renamed into place: a backup is whole or not there.
//   - Restoring is asked for while the game runs (RequestRestore writes
//     backups/restore-pending.txt) and done by the next OnLaunch, again
//     before the game runs: the current saves become a backup first
//     (<date>_<time>-before-restore), the chosen one is copied beside
//     save/, then swapped in by renames. Before-restore backups are never
//     pruned by count.
#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace user::save_backup
{
    constexpr size_t kKeep = 10;

    struct Backup
    {
        std::string name;               // its folder's name: "2026-10-18_13-05-42"
        std::filesystem::path path;
        uint64_t bytes = 0;
        size_t files = 0;
        bool beforeRestore = false;     // made by a restore, of the saves it replaced
        bool imported = false;          // made by an import
    };

    // Before the game starts. Applies a pending restore, then snapshots
    // `saveDir` if it changed. Returns what it did, one line each, for the log.
    std::vector<std::string> OnLaunch(const std::filesystem::path& saveDir, const std::filesystem::path& backupsDir);

    // Newest first.
    std::vector<Backup> List(const std::filesystem::path& backupsDir);

    // The next launch restores `name` (one of List's). False, with `error`,
    // when it isn't a usable backup.
    bool RequestRestore(const std::filesystem::path& backupsDir, const std::string& name, std::string& error);
    // A backup of the current saves (if any) that `add` then adds to (given
    // its folder), restored at the next launch. False, with `error` (from
    // `add` too), when nothing was recorded; `name` is the new backup's.
    bool RequestImport(const std::filesystem::path& saveDir, const std::filesystem::path& backupsDir,
        const std::function<bool(const std::filesystem::path& folder, std::string& error)>& add, std::string& name, std::string& error);
    // The backup a restore is pending for, or "".
    std::string PendingRestore(const std::filesystem::path& backupsDir);
    void CancelRestore(const std::filesystem::path& backupsDir);

    // "18 Oct 2026, 13:05" for a backup's name ("" if it isn't one).
    std::string DisplayName(const std::string& name);
}

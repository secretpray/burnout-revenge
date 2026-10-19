// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Saves that a crash or a power cut can't half-write. The game rewrites its
// save in place: it opens "Player Files" emptying it (FILE_OVERWRITE_IF)
// and writes 21 KB in three calls, so a process killed between the two
// left an empty or partial file. A file under the save folder opened for
// writing is written to a staging copy instead (a folder beside the save
// folder, so neither the game's listings nor the save backups see it),
// started from the file as it is unless the open empties it; when the
// game closes it, the copy is flushed to the disk (F_FULLFSYNC on the Mac)
// and renamed over the save in one step, and the folder flushed. Until
// then the old save is whole on disk. The idea is Fukami's (its memory
// card's journal: temporary file, fsync, rename); the code is ours.
// kernel/imports_file.cpp drives it; tests/save_commit_test.cpp checks it
// on a scratch folder.
#pragma once

#include <filesystem>
#include <optional>
#include <string>

namespace savecommit
{
    // Where `host` (a file the game opens for writing) is staged, if it is
    // inside `saveRoot`: <saveRoot>-incoming/<n>-<file name>. A name `n`
    // unique in this process, so two opens of one file never share a copy.
    std::optional<std::filesystem::path> StagingFor(const std::filesystem::path& host, const std::filesystem::path& saveRoot);

    // Creates the staging copy and returns an fd open on it for reading and
    // writing (-1 and `error` on failure). `keepContents`: the copy starts
    // as `target` is now (when it exists); otherwise empty.
    int Begin(const std::filesystem::path& staging, const std::filesystem::path& target, bool keepContents, std::string& error);

    // Flushes `fd` to the disk, closes it, and renames `staging` over
    // `target` (then flushes the folder). On failure the staging copy is
    // removed, `target` is as it was, and the reason is returned; "" when
    // it took.
    std::string Commit(int fd, const std::filesystem::path& staging, const std::filesystem::path& target);

    // At start-up: staging copies a killed process left are thrown away
    // (their saves were never replaced). Returns how many.
    size_t RemoveLeftovers(const std::filesystem::path& saveRoot);
}

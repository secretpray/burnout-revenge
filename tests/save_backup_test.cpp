// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
// Save backups (user/save_backup.h), on a scratch folder: a launch backs up
// changed saves only, keeps the newest kKeep, never leaves a half-copied
// backup, and a requested restore swaps the chosen saves in at the next
// launch after backing up the ones it replaces.
//   save_backup_test [folder]   (a new folder inside it is used, then deleted)
#include <user/save_backup.h>

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

namespace fs = std::filesystem;
namespace sb = user::save_backup;

static int s_failures = 0;
#define CHECK(cond, ...)                                       \
    do                                                         \
    {                                                          \
        if (!(cond))                                           \
        {                                                      \
            std::fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__); \
            std::fprintf(stderr, __VA_ARGS__);                 \
            std::fprintf(stderr, "\n");                        \
            s_failures++;                                      \
        }                                                      \
    } while (0)

static void Write(const fs::path& file, const std::string& text)
{
    fs::create_directories(file.parent_path());
    std::ofstream(file, std::ios::binary | std::ios::trunc) << text;
}

static std::string Read(const fs::path& file)
{
    std::ifstream in(file, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

int main(int argc, char** argv)
{
    const fs::path root = fs::path(argc > 1 ? argv[1] : ".") / "save_backup_scratch";
    fs::remove_all(root);
    const fs::path save = root / "save", backups = root / "backups";

    // No saves yet: nothing to back up.
    CHECK(sb::OnLaunch(save, backups).empty(), "no saves, no work");
    CHECK(sb::List(backups).empty(), "no backups without saves");

    // The first launch with saves backs them up whole.
    Write(save / "Player Files" / "Player Files", "profile v1");
    Write(save / "Player Files.xcontent", "meta");
    sb::OnLaunch(save, backups);
    auto list = sb::List(backups);
    CHECK(list.size() == 1, "one backup (%zu)", list.size());
    CHECK(!list.empty() && list[0].files == 2 && Read(list[0].path / "Player Files" / "Player Files") == "profile v1", "the backup holds the saves");
    CHECK(!list.empty() && !sb::DisplayName(list[0].name).empty(), "a readable name");

    // Unchanged saves: no new backup.
    sb::OnLaunch(save, backups);
    CHECK(sb::List(backups).size() == 1, "unchanged saves aren't backed up again");

    // A half-finished copy from an interrupted launch is cleaned up and never listed.
    Write(backups / ".incoming-2026-01-01_00-00-00" / "x", "partial");
    sb::OnLaunch(save, backups);
    CHECK(!fs::exists(backups / ".incoming-2026-01-01_00-00-00"), "an interrupted copy is removed");

    // Changed saves: a new backup each launch (names a second apart at least
    // differ by the -2 suffix), the newest kKeep kept.
    for (size_t i = 0; i < sb::kKeep + 3; i++)
    {
        Write(save / "Player Files" / "Player Files", "profile v" + std::to_string(i + 2));
        sb::OnLaunch(save, backups);
    }
    list = sb::List(backups);
    CHECK(list.size() == sb::kKeep, "kept %zu backups (%zu)", sb::kKeep, list.size());
    CHECK(!list.empty() && Read(list[0].path / "Player Files" / "Player Files") == "profile v" + std::to_string(sb::kKeep + 4),
        "the newest is first (%s: %s)", list.empty() ? "" : list[0].name.c_str(),
        list.empty() ? "" : Read(list[0].path / "Player Files" / "Player Files").c_str());

    // A restore: asked now, done by the next launch, the replaced saves kept.
    const std::string oldest = list.back().name, oldestText = Read(list.back().path / "Player Files" / "Player Files");
    std::string error;
    CHECK(!sb::RequestRestore(backups, "../save", error), "not a backup name");
    CHECK(!sb::RequestRestore(backups, "2000-01-01_00-00-00", error), "no such backup");
    CHECK(sb::RequestRestore(backups, oldest, error), "restore requested: %s", error.c_str());
    CHECK(sb::PendingRestore(backups) == oldest, "pending");
    const std::string before = Read(save / "Player Files" / "Player Files");
    sb::OnLaunch(save, backups);
    CHECK(Read(save / "Player Files" / "Player Files") == oldestText, "the chosen saves are in place");
    CHECK(sb::PendingRestore(backups).empty(), "the restore is done once");
    bool kept = false;
    for (const auto& b : sb::List(backups))
        kept |= b.beforeRestore && Read(b.path / "Player Files" / "Player Files") == before;
    CHECK(kept, "the replaced saves are a before-restore backup");
    CHECK(!fs::exists(root / "save.restoring") && !fs::exists(root / "save.replaced"), "no swap leftovers");

    // Before-restore backups aren't pruned by count.
    for (size_t i = 0; i < sb::kKeep + 1; i++)
    {
        Write(save / "Player Files" / "Player Files", "later " + std::to_string(i));
        sb::OnLaunch(save, backups);
    }
    kept = false;
    for (const auto& b : sb::List(backups))
        kept |= b.beforeRestore;
    CHECK(kept, "the before-restore backup survives pruning");

    fs::remove_all(root);
    std::printf("save_backup_test: %s\n", s_failures ? "FAILED" : "ok");
    return s_failures ? 1 : 0;
}
